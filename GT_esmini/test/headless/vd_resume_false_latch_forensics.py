#!/usr/bin/env python
"""feature:F7 — AUTO_RESUME 直後の誤ラッチ: 機序確定用フォレンジック。

## 何をするか

**誰も触っていない**構成（follower ホイール = サーボの指令に追従するだけ）で
走らせ、残差検出器が発火するかを見る。発火したらそれは定義上 100% 誤検知である。
発火フレームの周辺で、残差の各項が**同一時刻に何を測っているか**を数値で並べる。

## 時刻の同一性を最初に確かめる（ここを飛ばすと機序を取り違える）

`VirtualDriverTelemetryJson.cpp` 冒頭の警告どおり、**1 行に 2 つの時刻が入る**:
`ffb.*` はこのフレームの FFB 更新**後**、`ffb.gates.*` は OverrideManager が
更新**前**に渡されたサンプル（＝**前の行**の `ffb` ブロック）から計算した値。
恒等式 `gates.actual_norm(N) == (ffb.target_norm - ffb.position_error)(N-1)` が
成り立つはずで、本スクリプトは**まずこれを実測で確認**してから機序の議論に入る。
（成り立たなければ計器が壊れているので、機序の話はそこで止める。）

## 使い方

    DriverScript/.venv/Scripts/python.exe GT_esmini/test/headless/vd_resume_false_latch_forensics.py \
        [--mode follower|plant|frozen] [--total-s 20] [--out <dir>]

exit code: 0 = 誤ラッチ無し / 1 = 誤ラッチ検出 / 2 = 走らせられなかった
"""

from __future__ import annotations

import argparse
import ctypes
import json
import os
import socket
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
ROOT = HERE.parents[2]

from vd_resume_transient import (  # noqa: E402
    BTN_AUTO_RESUME,
    DLL,
    DT,
    INPUT_PORT,
    MAGIC,
    WIRE,
    _load_lib,
    _make_variant,
)


def run_grab_release_resume(
    envelope_enabled: bool, speed_mps: float, grab_force: float = 0.6
) -> list[dict]:
    """The fixture the bug actually lives in: hands-off -> GRAB -> release ->
    RESUME -> hands-off.

    Uses the force-coupled "plant" wheel, where the injection wire's `steering`
    field is reinterpreted as a DRIVER FORCE (HeadlessFfbInput.cpp:436-437), so
    a hand can be applied and then genuinely RELEASED — which the frozen wheel
    cannot do (it is pinned at one angle for the whole process, so there is no
    "let go" to test). Phases are tagged on each frame:

      A hands_off_pre   force=0   AUTO      -> must NOT latch (negative)
      B grab            force>0             -> MUST latch     (positive)
      C release         force=0   still MANUAL
      D resume_pulse    force=0   RESUME button
      E hands_off_post  force=0   AUTO      -> must NOT latch  <-- the defect

    Packets are sent synchronously, one per GT_Step, in program order — the
    background-sender race documented in vd_resume_transient.run_network_arm
    made peak metrics non-reproducible run to run.
    """
    os.environ["GT_HEADLESS_FFB_MODE"] = "plant"
    for k in ("GT_HEADLESS_FFB_FROZEN_AT", "GT_HEADLESS_FFB_LAG_TAU"):
        os.environ.pop(k, None)

    cmd = {"steering": 0.0, "buttons": 0}
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    # NOT INPUT_PORT (9100). In "plant"/"pushback" mode the engine opens its own
    # injection listener on GT_HEADLESS_FFB_PUSHBACK_PORT, default 9105
    # (HeadlessFfbInput.cpp:439-445) — a different socket from the
    # NetworkInputBridge port. Sending to 9100 here produced a silent no-op:
    # the run completed, the wheel never moved, and the POSITIVE control simply
    # did not fire. That is why this harness asserts the positive control
    # before reading anything into the negative one.
    PLANT_PORT = int(os.environ.get("GT_HEADLESS_FFB_PUSHBACK_PORT", "9105"))

    def send_now():
        pkt = WIRE.pack(
            MAGIC, cmd["steering"], 0.0, 0.0, 0.0, 0, cmd["buttons"] & 0xFFFFFFFF
        )
        try:
            sock.sendto(pkt, ("127.0.0.1", PLANT_PORT))
        except OSError:
            pass

    tmpdir = tempfile.mkdtemp(prefix="vd_falselatch_cycle_")
    cfg = {
        "input_type": "headless_ffb",
        "ffb_target_track_enabled": True,
        "input_port": INPUT_PORT,
        "input_transport": "udp",
        "steering_threshold": 0.05,
        "auto_return_timeout": 1e9,  # only the button returns
        "ad_steering_envelope_enabled": envelope_enabled,
    }
    xosc = _make_variant(tmpdir, cfg, speed_mps)

    lib = _load_lib()
    argv_list = [
        b"vd_falselatch_cycle",
        b"--osc",
        xosc.encode(),
        b"--headless",
        b"--fixed_timestep",
        f"{DT:.3f}".encode(),
    ]
    argv = (ctypes.c_char_p * len(argv_list))(*argv_list)
    rc = lib.GT_InitWithArgs(len(argv_list), argv)
    if rc != 0:
        raise RuntimeError(f"GT_InitWithArgs rc={rc}")

    buf = ctypes.create_string_buffer(65536)
    frames: list[dict] = []

    def run(phase: str, n: int):
        for _ in range(n):
            send_now()
            lib.GT_Step(DT)
            k = lib.GT_GetVirtualDriverTelemetry(0, buf, len(buf))
            if k > 0:
                f = json.loads(buf.value.decode())
                f["_phase"] = phase
                frames.append(f)

    run("A_hands_off_pre", int(4.0 / DT))
    cmd["steering"] = grab_force
    run("B_grab", int(2.0 / DT))
    cmd["steering"] = 0.0
    run("C_release", int(1.5 / DT))
    cmd["buttons"] = BTN_AUTO_RESUME
    run("D_resume_pulse", int(0.4 / DT))
    cmd["buttons"] = 0
    run("E_hands_off_post", int(8.0 / DT))

    lib.GT_Close()
    sock.close()
    try:
        os.remove(xosc)
    except OSError:
        pass
    return frames


def run_notouch(
    mode: str,
    envelope_enabled: bool,
    speed_mps: float,
    total_s: float,
    target_track: bool = True,
) -> list[dict]:
    """No-hand run. The synthetic wheel only ever moves because the servo moved
    it, so ANY latch here is a false positive by construction."""
    os.environ["GT_HEADLESS_FFB_MODE"] = mode
    for k in ("GT_HEADLESS_FFB_FROZEN_AT", "GT_HEADLESS_FFB_LAG_TAU"):
        os.environ.pop(k, None)

    tmpdir = tempfile.mkdtemp(prefix="vd_falselatch_")
    cfg = {
        "input_type": "headless_ffb",
        "ffb_target_track_enabled": target_track,
        "steering_threshold": 0.05,  # shipped
        "auto_return_timeout": 1.0,
        "ad_steering_envelope_enabled": envelope_enabled,
    }
    xosc = _make_variant(tmpdir, cfg, speed_mps)

    lib = _load_lib()
    argv_list = [
        b"vd_falselatch",
        b"--osc",
        xosc.encode(),
        b"--headless",
        b"--fixed_timestep",
        f"{DT:.3f}".encode(),
    ]
    argv = (ctypes.c_char_p * len(argv_list))(*argv_list)
    rc = lib.GT_InitWithArgs(len(argv_list), argv)
    if rc != 0:
        raise RuntimeError(f"GT_InitWithArgs rc={rc}")

    buf = ctypes.create_string_buffer(65536)
    frames: list[dict] = []
    for _ in range(int(total_s / DT)):
        lib.GT_Step(DT)
        n = lib.GT_GetVirtualDriverTelemetry(0, buf, len(buf))
        if n > 0:
            frames.append(json.loads(buf.value.decode()))
    lib.GT_Close()
    try:
        os.remove(xosc)
    except OSError:
        pass
    return frames


def check_alignment(frames: list[dict]) -> dict:
    """Verify gates.actual_norm(N) == (ffb.target_norm - ffb.position_error)(N-1).

    Returns the worst mismatch under each pairing so the RIGHT one is chosen by
    measurement, not by trusting a comment.
    """
    worst_prev = worst_same = 0.0
    n_checked = 0
    for i in range(1, len(frames)):
        g = frames[i].get("ffb", {}).get("gates", {})
        if not g or not frames[i]["ffb"].get("target_active"):
            continue
        a = g.get("actual_norm")
        if a is None:
            continue
        prev, cur = frames[i - 1]["ffb"], frames[i]["ffb"]
        rec_prev = prev["target_norm"] - prev["position_error"]
        rec_same = cur["target_norm"] - cur["position_error"]
        worst_prev = max(worst_prev, abs(a - rec_prev))
        worst_same = max(worst_same, abs(a - rec_same))
        n_checked += 1
    return {
        "n": n_checked,
        "worst_vs_prev_frame": worst_prev,
        "worst_vs_same_frame": worst_same,
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--mode", default="follower")
    ap.add_argument("--total-s", type=float, default=20.0)
    ap.add_argument("--speed", type=float, default=8.0)
    ap.add_argument("--no-envelope", action="store_true")
    ap.add_argument("--out", default=None)
    ap.add_argument(
        "--context", type=int, default=6, help="frames printed around the latch"
    )
    ap.add_argument(
        "--cycle",
        action="store_true",
        help="hands-off -> GRAB -> release -> RESUME -> hands-off (the defect fixture)",
    )
    args = ap.parse_args()

    if not os.path.exists(DLL):
        print(f"NOT RUN — DLL missing: {DLL}")
        return 2

    if args.cycle:
        frames = run_grab_release_resume(not args.no_envelope, args.speed)
    else:
        frames = run_notouch(args.mode, not args.no_envelope, args.speed, args.total_s)
    if not frames:
        print("NOT RUN — no telemetry frames")
        return 2
    if args.out:
        p = Path(args.out)
        p.mkdir(parents=True, exist_ok=True)
        (p / f"falselatch_{args.mode}.jsonl").write_text(
            "\n".join(json.dumps(f) for f in frames) + "\n", encoding="utf-8"
        )

    # --- STEP 1: instrument time alignment -------------------------------
    al = check_alignment(frames)
    print(f"=== 時刻の同一性（機序の議論より先）  n={al['n']} active frames ===")
    print(
        f"  gates.actual_norm(N) vs (target-pos_err)(N-1): worst |Δ| = {al['worst_vs_prev_frame']:.3e}"
    )
    print(
        f"  gates.actual_norm(N) vs (target-pos_err)(N)  : worst |Δ| = {al['worst_vs_same_frame']:.3e}"
    )
    if al["n"] == 0:
        print(
            "  → サーボが一度も active にならなかった。この構成では機序を観測できない。"
        )
        return 2
    # どちらの対応づけが小さいかで決める。絶対しきい値で判定すると、
    # 「1 量子ぶん（1e-9）ずれた正しい対応づけ」を不一致と読んでしまう
    # ——実際この判定で一度取り違えた。比較すべきは 2 つの候補の相対関係。
    aligned_prev = al["worst_vs_prev_frame"] < al["worst_vs_same_frame"]
    ratio = (
        al["worst_vs_same_frame"] / al["worst_vs_prev_frame"]
        if al["worst_vs_prev_frame"] > 0
        else float("inf")
    )
    print(
        f"  → gates.* は {'前フレーム' if aligned_prev else '同一フレーム'} の ffb サンプル由来"
        f"（分離比 {ratio:.3g}倍）"
    )
    if aligned_prev:
        print(
            "     文書（VirtualDriverTelemetryJson.cpp 冒頭）どおり。以降の残差の議論は"
        )
        print(
            "     gates.* 同士で閉じており、ffb.*(N) と gates.*(N) を混ぜてはならない。"
        )

    # --- STEP 2: did the no-touch run latch? -----------------------------
    if args.cycle:
        # Phase B's latch is CORRECT (a hand really was on the wheel) — the
        # defect is a latch in phase E, after the hand is gone and AUTO resumed.
        latch_idx = next(
            (
                i
                for i, f in enumerate(frames)
                if f["_phase"] == "E_hands_off_post"
                and f.get("override", {}).get("lateral")
            ),
            None,
        )
        b_latched = any(
            f["_phase"] == "B_grab" and f.get("override", {}).get("lateral")
            for f in frames
        )
        e_frames = sum(1 for f in frames if f["_phase"] == "E_hands_off_post")
        print(f"  [positive control] phase B (hand on wheel) latched: {b_latched}")
        print(f"  [negative under test] phase E frames: {e_frames}")
        if not b_latched:
            print(
                "  !! 陽性対照が発火していない — この fixture では検出器を評価できない"
            )
            return 2
    else:
        latch_idx = next(
            (i for i, f in enumerate(frames) if f.get("override", {}).get("lateral")),
            None,
        )
    print()
    print(f"=== 誤ラッチ（誰も触っていない run） total_frames={len(frames)} ===")
    if latch_idx is None:
        print("  ラッチ無し。この構成では再現しない。")
        return 0

    f0 = frames[latch_idx]
    print(f"  **LATCH** frame={latch_idx} t={f0['sim_time']:.3f}")
    print()
    hdr = (
        f"{'frame':>6s} {'t':>7s} {'tgt_norm':>10s} {'pos_err':>10s} "
        f"{'g.actual':>10s} {'g.shadow':>10s} {'residual':>10s} {'tgt_rate':>10s} "
        f"{'eff_force':>10s} {'sustain':>8s} {'lat':>4s}"
    )
    print(hdr)
    lo = max(0, latch_idx - args.context)
    hi = min(len(frames), latch_idx + 3)
    prev_actual = None
    for i in range(lo, hi):
        f = frames[i]
        ffb = f.get("ffb", {})
        g = ffb.get("gates", {})
        print(
            f"{i:6d} {f['sim_time']:7.3f} {ffb.get('target_norm', 0):10.6f} "
            f"{ffb.get('position_error', 0):10.6f} {g.get('actual_norm', 0):10.6f} "
            f"{g.get('shadow_norm', 0):10.6f} {g.get('residual', 0):10.6f} "
            f"{g.get('target_rate', 0):10.4f} {g.get('effective_force', 0):10.4f} "
            f"{g.get('sustain_accum', 0):8.3f} "
            f"{'M' if f.get('override', {}).get('lateral') else 'A':>4s}"
        )

    # --- STEP 3: what is the residual actually measuring? ----------------
    print()
    print("=== 残差の各項が何を測っているか（ラッチ直前フレーム） ===")
    i = latch_idx
    g = frames[i]["ffb"]["gates"]
    gp = frames[i - 1]["ffb"]["gates"] if i > 0 else {}
    d_actual = g.get("actual_norm", 0.0) - gp.get("actual_norm", 0.0)
    d_shadow = g.get("shadow_norm", 0.0) - gp.get("shadow_norm", 0.0)
    print(
        f"  実測ホイールの1フレーム移動量  Δactual = {d_actual:+.6f}  (= {d_actual/DT:+.3f} /s)"
    )
    print(
        f"  影モデルの1フレーム移動量      Δshadow = {d_shadow:+.6f}  (= {d_shadow/DT:+.3f} /s)"
    )
    print(f"  差                                       = {d_actual - d_shadow:+.6f}")
    print(
        f"  AD目標のレート  gates.target_rate        = {g.get('target_rate', 0.0):+.3f} /s"
    )
    print(
        f"  residual                                 = {g.get('residual', 0.0):.6f}"
        f"  (threshold {g.get('residual_threshold', 0.0):.3f})"
    )
    print(
        f"  effective_force                          = {g.get('effective_force', 0.0):+.4f}"
    )
    print()
    print("  読み方: 誰も触っていないので Δactual は**サーボが動かした量**である。")
    print(
        "  影モデルがその速度を出せない（v_max で頭打ち）なら、差がそのまま残差になり、"
    )
    print("  検出器は『サーボが運んだ動き』を『運転者の抵抗』として計上している。")
    return 1


if __name__ == "__main__":
    sys.exit(main())
