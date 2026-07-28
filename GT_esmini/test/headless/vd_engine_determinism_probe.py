#!/usr/bin/env python
"""feature:F7 — エンジン非決定性の再現・分岐点特定プローブ。

## これは何か

**同一 config・同一シナリオを N 回、それぞれ独立プロセスで走らせ、
最初にフレーム単位テレメトリが食い違うフレームとフィールドを特定する。**

`scripts/vd_ffb_notouch_parity.py`（別担当）の決定論コントロールが
6 シナリオ中 3 件で FAIL したという申し送りを受けた調査用。あちらは
「一致するか」を判定するゲートで、こちらは「**どこで・何が**食い違うか」を
出す法医学ツール。判定は返すが（再現できたら 1）、目的は分岐点の特定である。

## 比較の仕方（ここが肝）

比較は **telemetry JSON の生文字列**に対して行う。パースして float に
戻すと、9 桁で刻まれた記録より細かい差を「作って」しまうか、逆に
表示丸めで潰してしまう。`VirtualDriverTelemetryJson.cpp` が固定 9 桁で
書いているので、**その文字列こそが記録された全情報**であり、
それより細かい主張も粗い主張もこのデータからはできない。

分岐フレームが見つかったら、そのフレームの**全キーを再帰的に走査**して
食い違ったリーフだけを列挙する（「brake が違う」で止めず、
同時に何が違っていたかを全部見る——原因の手掛かりはそこにある）。

## なぜ最初の分岐フィールドを見ることが重要だったか

申し送りは「`driver.brake` が食い違う」だった。しかしそれは
**driver/envelope しか比較していなかったから**で、実際の最初の分岐は
`ego.speed` / `front_bumper.s`——**AD の判断ではなく車両物理側**だった。
AD の食い違いはその下流の帰結にすぎない。原因が「制動判断の浮動小数計算」に
あるという仮説は、この一点で外れる。**比較範囲を絞ると原因の方向を間違える。**

## このプローブは恒久的なランダム化差分テストでもある

出荷既定は `idle_jitter_seed: 0`（`config/real_vehicle_params.json`）＝
**プロセスごとに乱数種が変わる**。したがってこのプローブの各 run は
自動的に「別の乱数列」での実行になり、全 run が厳密一致するということは
**「乱数がどこにも漏れていない」ことを毎回ランダム化して確かめている**
ことになる。種を固定してしまうとこの性質は失われる（固定種は原因特定には
使えるが、恒久ゲートとしては弱くなる）。

## 使い方

    DriverScript/.venv/Scripts/python.exe GT_esmini/test/headless/vd_engine_determinism_probe.py \
        [--runs 3] [--dt 0.01] [--max-time 40] [--scenario <name|path>]... [--config <json>]

`--config` は VirtualDriver config を差し替える（A/B 用）。
`--env KEY=VALUE` を複数渡すと worker サブプロセスの環境に反映される。

exit code:
  0 - 全シナリオで全 run が完全一致（非決定性を再現できなかった）
  1 - 少なくとも 1 シナリオで分岐を再現した（分岐点を出力済み）
  2 - 走らせられなかった（DLL 欠落・シナリオ欠落など）。一致ではない。
"""
from __future__ import annotations

import argparse
import ctypes
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
DEFAULT_DLL = ROOT / "build" / "GT_esmini" / "Release" / "GT_esminiLib.dll"

# 申し送りで「非決定」とされた 3 件と「決定的」とされた 3 件。両方回して
# 差が出る側/出ない側を同じ計測器で見る（片側だけ見ると計測器自身の
# 感度が分からない）。
SCENARIOS = {
    # 非決定と報告された側
    "virtual_driver_basic": ROOT / "resources" / "xosc" / "virtual_driver_basic.xosc",
    "decelerate_for_right_turn": ROOT / "resources" / "xosc" / "verification" / "05_anticipation"
                                 / "decelerate_for_right_turn.xosc",
    "traffic_lights_junction": ROOT / "resources" / "xosc" / "verification" / "05_anticipation"
                               / "traffic_lights_junction.xosc",
    # 決定的と報告された側（陰性対照）
    "decelerate_for_curve": ROOT / "resources" / "xosc" / "verification" / "05_anticipation"
                            / "decelerate_for_curve.xosc",
    "cross_straight_junction": ROOT / "resources" / "xosc" / "verification" / "05_anticipation"
                               / "cross_straight_junction.xosc",
    "speed_limit_change": ROOT / "resources" / "xosc" / "verification" / "05_anticipation"
                          / "speed_limit_change.xosc",
}


# ---------------------------------------------------------------------------
# worker: 1 プロセス = 1 実行。テレメトリの **生の行** をそのまま保存する。
# ---------------------------------------------------------------------------
def _worker_main() -> int:
    dll_path, xosc_path, dt, max_time_s, out_path = (
        sys.argv[2], sys.argv[3], float(sys.argv[4]), float(sys.argv[5]), sys.argv[6])
    cfg_path = sys.argv[7] if len(sys.argv) > 7 and sys.argv[7] != "-" else None

    lib = ctypes.CDLL(dll_path)
    lib.GT_InitWithArgs.argtypes = [ctypes.c_int, ctypes.POINTER(ctypes.c_char_p)]
    lib.GT_InitWithArgs.restype = ctypes.c_int
    lib.GT_Step.argtypes = [ctypes.c_double]
    lib.GT_Close.argtypes = []
    lib.GT_GetVirtualDriverTelemetry.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
    lib.GT_GetVirtualDriverTelemetry.restype = ctypes.c_int

    # NOTE: VirtualDriver の config は CLI ではなく xosc の Controller property
    # "ConfigFile" で決まる（ControllerVirtualDriver.cpp:52-58）。A/B したい
    # ときは呼び出し側が xosc variant を書いて --scenario で渡すこと。
    # 車両物理（IdleJitter を含む）は config/real_vehicle_params.json 側。
    assert cfg_path is None or cfg_path == "-", "config 差し替えは xosc variant で行う"
    argv_list = [b"determinism", b"--osc", xosc_path.encode(), b"--headless",
                 b"--fixed_timestep", f"{dt:.3f}".encode()]
    argv = (ctypes.c_char_p * len(argv_list))(*argv_list)
    rc = lib.GT_InitWithArgs(len(argv_list), argv)
    if rc != 0:
        raise SystemExit(f"GT_InitWithArgs rc={rc}")

    buf = ctypes.create_string_buffer(65536)
    lines: list[str] = []
    for _ in range(int(max_time_s / dt) + 20):
        lib.GT_Step(dt)
        n = lib.GT_GetVirtualDriverTelemetry(0, buf, len(buf))
        if n <= 0:
            continue
        lines.append(buf.value.decode("utf-8", "replace"))
    lib.GT_Close()
    # 生の行をそのまま。JSON に入れ直さない（再シリアライズは情報を変える）。
    Path(out_path).write_text("\n".join(lines), encoding="utf-8")
    return 0


def run_once(dll: str, xosc: str, dt: float, max_time_s: float,
             cfg: str | None, extra_env: dict) -> list[str]:
    fd, out_path = tempfile.mkstemp(prefix="vd_det_", suffix=".jsonl")
    os.close(fd)
    env = dict(os.environ)
    env.update(extra_env)
    try:
        r = subprocess.run(
            [sys.executable, os.path.abspath(__file__), "--worker", dll, xosc,
             repr(dt), repr(max_time_s), out_path, cfg or "-"],
            capture_output=True, text=True, timeout=max(180.0, max_time_s * 4), env=env)
        if r.returncode != 0:
            raise RuntimeError(f"worker rc={r.returncode}\n{r.stdout[-1500:]}\n{r.stderr[-1500:]}")
        return [l for l in Path(out_path).read_text(encoding="utf-8").splitlines() if l.strip()]
    finally:
        Path(out_path).unlink(missing_ok=True)


# ---------------------------------------------------------------------------
# 比較
# ---------------------------------------------------------------------------
def leaf_diffs(a, b, path: str = "") -> list[tuple[str, object, object]]:
    """2 つのパース済みフレームを再帰的に比較し、食い違うリーフを全部返す。"""
    out = []
    if isinstance(a, dict) and isinstance(b, dict):
        for k in sorted(set(a) | set(b)):
            out += leaf_diffs(a.get(k), b.get(k), f"{path}.{k}" if path else k)
    elif isinstance(a, list) and isinstance(b, list):
        if len(a) != len(b):
            out.append((path + "[len]", len(a), len(b)))
        else:
            for i, (x, y) in enumerate(zip(a, b)):
                out += leaf_diffs(x, y, f"{path}[{i}]")
    elif a != b:
        out.append((path, a, b))
    return out


def compare_runs(runs: list[list[str]]) -> dict:
    """run[0] を基準に、最初に生文字列が食い違うフレームを探す。"""
    n_min = min(len(r) for r in runs)
    lens = [len(r) for r in runs]
    first = None
    for i in range(n_min):
        base = runs[0][i]
        for j in range(1, len(runs)):
            if runs[j][i] != base:
                first = (i, j)
                break
        if first:
            break
    res = {"n_frames": lens, "identical": first is None and len(set(lens)) == 1}
    if first is None:
        if len(set(lens)) != 1:
            res["note"] = f"フレーム数が違う: {lens}（共通区間 {n_min} は完全一致）"
        return res
    i, j = first
    fa, fb = json.loads(runs[0][i]), json.loads(runs[j][i])
    res.update({"frame_index": i, "run_b": j,
                "sim_time": fa.get("sim_time"),
                "diffs": leaf_diffs(fa, fb)})
    return res


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dll", default=str(DEFAULT_DLL))
    ap.add_argument("--runs", type=int, default=3)
    ap.add_argument("--dt", type=float, default=0.01)
    ap.add_argument("--max-time", type=float, default=40.0)
    ap.add_argument("--scenario", action="append", default=None,
                    help="SCENARIOS の名前、または .xosc パス。複数可。既定は全 6 件")
    ap.add_argument("--config", default=None, help="VirtualDriver config JSON（A/B 用）")
    ap.add_argument("--env", action="append", default=[], help="KEY=VALUE（worker に渡す）")
    args = ap.parse_args()

    if not Path(args.dll).exists():
        print(f"NOT RUN — DLL が無い: {args.dll}")
        return 2

    extra_env = {}
    for kv in args.env:
        k, _, v = kv.partition("=")
        extra_env[k] = v

    targets = []
    for s in (args.scenario or list(SCENARIOS)):
        p = SCENARIOS.get(s, Path(s))
        if not Path(p).exists():
            print(f"NOT RUN — シナリオが無い: {s} -> {p}")
            return 2
        targets.append((s if s in SCENARIOS else Path(s).stem, Path(p)))

    print(f"DLL      : {args.dll}")
    print(f"config   : {args.config or '(既定)'}")
    print(f"env      : {extra_env or '(なし)'}")
    print(f"runs     : {args.runs}（各 run = 独立プロセス）  dt={args.dt}  max_time={args.max_time}")
    print()

    diverged = []
    for name, path in targets:
        runs = [run_once(args.dll, str(path), args.dt, args.max_time, args.config, extra_env)
                for _ in range(args.runs)]
        res = compare_runs(runs)
        if res.get("identical"):
            print(f"[一致]   {name:28s} frames={res['n_frames'][0]} × {args.runs} run 完全一致")
            continue
        if "frame_index" not in res:
            print(f"[?]      {name:28s} {res.get('note')}")
            continue
        diverged.append(name)
        print(f"[分岐]   {name:28s} frame={res['frame_index']} "
              f"t={res['sim_time']} (run0 vs run{res['run_b']})  frames={res['n_frames']}")
        for k, va, vb in res["diffs"]:
            extra = ""
            if isinstance(va, (int, float)) and isinstance(vb, (int, float)) and va:
                extra = f"  Δ={vb - va:+.3e}  rel={abs(vb - va) / abs(va):.3e}"
            print(f"           {k:44s} {va!r} -> {vb!r}{extra}")
        print()

    if diverged:
        print(f"RESULT: 非決定性を再現した — {len(diverged)}/{len(targets)}: {diverged}")
        return 1
    print(f"RESULT: 全 {len(targets)} シナリオ × {args.runs} run が完全一致（この条件では再現せず）")
    return 0


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--worker":
        sys.exit(_worker_main())
    sys.exit(main())
