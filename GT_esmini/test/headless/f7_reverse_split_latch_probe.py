"""feature:F7 — reverse split (lateral=VirtualDriver / longitudinal=ManualDrive)
FFB override LATCH-FIRING + AUTO_RESUME ROUND-TRIP probe.

Predecessor (commit 1fa408b9) wired the bus so the FFB residual detector has
an input in this configuration, but never got the latch to actually fire —
ManualDrive had no headless input source, so no synthetic residual could be
injected. This probe closes that gap: it adds input_type="headless_ffb" to
ManualDrive (ControllerManualDrive.cpp), injects a synthetic "pushback" force
over UDP 9105, and checks not just that the latch fires but that it actually
DOES something:

  1. override.lateral flips false -> true, with exactly one manual_transition
     pulse (the latch fires).
  2. Once latched, the servo actually RELEASES the wheel (ffb.target_active
     goes false) instead of continuing to fight the driver toward whatever
     VirtualDriver's own (stub, always-zero) local frame would publish. Before
     the DomainOwnershipLedger PublishLateral/ConsumeLateral "manual" flag
     added in this session, ManualDriveCoordinator's servo-target call
     hardcoded active=true regardless of the lateral owner's override state,
     so the servo never went inert in this split.
  3. Once latched, the vehicle's REALIZED lateral command (driver.steer) keeps
     tracking the driver's actual pushed axis, not frozen at 0 — before the
     DomainOwnershipLedger device-axis channel added this session,
     VirtualDriver's own `cmd.steering = m.steering` read its own (stub, zero)
     local frame once lat_manual latched, so the published lateral command —
     and therefore what ManualDrive's physics integrator actually steered by —
     snapped to 0 the instant the latch fired.
  4. (commit after 4059daf7) AUTO_RESUME actually returns lateral to VD. Before
     the DomainOwnershipLedger PublishDeviceButtons/ConsumeDeviceButtons
     channel, OverrideManager::Update() read AUTO_RESUME from THIS
     controller's own (stub, always buttons=0) frame, so the physical
     AUTO_RESUME press — read by ManualDrive, the device holder — never
     reached VirtualDriver's OverrideManager: MANUAL was a one-way trip.

Usage (venv interpreter, absolute path):
  DriverScript/.venv/Scripts/python.exe GT_esmini/test/headless/f7_reverse_split_latch_probe.py
"""

from __future__ import annotations

import ctypes
import json
import os
import socket
import struct
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vd_domain_split_probe import (
    REPO_ROOT,
    make_variant,
    assert_config_loaded,
)  # noqa: E402

DLL = REPO_ROOT / "build" / "GT_esmini" / "Release" / "GT_esminiLib.dll"
SCENARIO = (
    REPO_ROOT
    / "resources"
    / "xosc"
    / "verification"
    / "08_handoff"
    / "scenario_realwheel_reverse_split.xosc"
)
MD_CONFIG = "manual_drive_realwheel_reverse_headless.json"  # build/GT_esmini/config/, headless_ffb variant

DT = (
    0.01  # match real-machine dt (see REAL_MACHINE_DT note in vd_ffb_notouch_parity.py)
)
PUSHBACK_PORT = 9105
MAGIC_PSTC = 0x50535443
WIRE = struct.Struct(
    "<I4diI"
)  # magic, steering(=pushback offset here), throttle, brake, clutch, gear, buttons
BTN_AUTO_RESUME = 1 << 7  # ButtonBits::AUTO_RESUME, VehicleCommand.hpp


def _load_lib():
    lib = ctypes.CDLL(str(DLL))
    lib.GT_InitWithArgs.argtypes = [ctypes.c_int, ctypes.POINTER(ctypes.c_char_p)]
    lib.GT_InitWithArgs.restype = ctypes.c_int
    lib.GT_Step.argtypes = [ctypes.c_double]
    lib.GT_Close.argtypes = []
    lib.GT_GetVirtualDriverTelemetry.argtypes = [
        ctypes.c_int,
        ctypes.c_char_p,
        ctypes.c_int,
    ]
    lib.GT_GetVirtualDriverTelemetry.restype = ctypes.c_int
    return lib


def run(
    outdir: Path,
    pushback_offset: float = 0.35,
    push_from_s: float = 3.0,
    push_until_s: float = 6.0,
    resume_at_s: float = 8.0,
    resume_hold_s: float = 0.05,
    duration_s: float = 14.0,
) -> list[dict]:
    outdir.mkdir(parents=True, exist_ok=True)
    variant = make_variant(SCENARIO, outdir / "reverse_latch.xosc", md_config=MD_CONFIG)
    # Shorten the committed 180s StopTrigger -- this probe only needs enough
    # time to establish AD steering, push, latch, release, and observe.
    text = variant.read_text(encoding="utf-8")
    text = text.replace(
        '<SimulationTimeCondition value="180" rule="greaterThan"/>',
        f'<SimulationTimeCondition value="{duration_s + 2}" rule="greaterThan"/>',
    )
    variant.write_text(text, encoding="utf-8")

    os.environ["GT_HEADLESS_FFB_MODE"] = "pushback"
    os.environ["GT_HEADLESS_FFB_PUSHBACK_PORT"] = str(PUSHBACK_PORT)
    tel_path = outdir / "reverse_latch.jsonl"
    os.environ["GT_VD_TELEMETRY_JSONL"] = str(tel_path)
    if tel_path.exists():
        tel_path.unlink()

    lib = _load_lib()
    argv_list = [
        b"reverselatch",
        b"--osc",
        str(variant).encode(),
        b"--headless",
        b"--fixed_timestep",
        str(DT).encode(),
    ]
    argv = (ctypes.c_char_p * len(argv_list))(*argv_list)
    rc = lib.GT_InitWithArgs(len(argv_list), argv)
    if rc != 0:
        raise RuntimeError(f"GT_InitWithArgs rc={rc}")

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    def send_pushback(offset: float, buttons: int = 0):
        pkt = WIRE.pack(MAGIC_PSTC, offset, 0.0, 0.0, 0.0, 0, buttons)
        sock.sendto(pkt, ("127.0.0.1", PUSHBACK_PORT))

    buf = ctypes.create_string_buffer(32768)
    frames: list[dict] = []
    n_steps = int(duration_s / DT)
    for i in range(n_steps):
        t = i * DT
        # Pushback listener reads fresh every frame (per HeadlessFfbInput.hpp);
        # resend every step so the injected value cannot go stale between UDP
        # sends and GT_Step().
        if push_from_s <= t < push_until_s:
            send_pushback(pushback_offset)
        elif resume_at_s <= t < resume_at_s + resume_hold_s:
            # Press-and-release AUTO_RESUME: held for a short window (like a
            # real button press spanning multiple frames), buttons=0 the rest
            # of the run so OverrideManager's rising-edge detector sees a
            # single clean edge, not a level held forever.
            send_pushback(0.0, buttons=BTN_AUTO_RESUME)
        else:
            send_pushback(0.0)
        lib.GT_Step(DT)
        n = lib.GT_GetVirtualDriverTelemetry(0, buf, len(buf))
        if n > 0:
            try:
                f = json.loads(buf.value.decode())
                f["_t_wall"] = t
                frames.append(f)
            except json.JSONDecodeError:
                continue
    lib.GT_Close()
    sock.close()
    return frames


def verdict(frames: list[dict]) -> list[tuple[str, bool, str]]:
    checks = []

    manual_edges = [f for f in frames if f.get("override", {}).get("manual_transition")]
    checks.append(
        (
            "ラッチが発火した（manual_transition が1回だけ立つ）",
            len(manual_edges) == 1,
            f"manual_transition 回数={len(manual_edges)}, at t={[round(f['sim_time'],3) for f in manual_edges]}",
        )
    )
    if not manual_edges:
        block_reasons = {
            f.get("ffb", {}).get("gates", {}).get("block_reason")
            for f in frames
            if f.get("ffb", {}).get("gates", {}).get("block_reason")
            not in (None, "none")
        }
        checks.append(
            (
                "(発火しなかったので) block_reason",
                False,
                f"observed: {block_reasons or '<none>'}",
            )
        )
        return checks

    t_latch = manual_edges[0]["sim_time"]
    before = [f for f in frames if f["sim_time"] < t_latch]
    after = [f for f in frames if f["sim_time"] >= t_latch]

    # Self-perpetuation only has to hold UNTIL AUTO_RESUME is pressed -- after
    # that, override.lateral flipping back to false is the whole point of
    # task 6, not a violation of it. Bound the window if a resume edge exists.
    auto_edges_all = [f for f in frames if f.get("override", {}).get("auto_transition")]
    t_resume_bound = auto_edges_all[0]["sim_time"] if auto_edges_all else None
    until_resume = [
        f for f in after if t_resume_bound is None or f["sim_time"] < t_resume_bound
    ]

    checks.append(
        (
            "発火前は override.lateral=false",
            all(not f.get("override", {}).get("lateral") for f in before),
            f"発火前フレーム中 lateral=true の件数={sum(1 for f in before if f.get('override',{}).get('lateral'))}",
        )
    )
    checks.append(
        (
            "発火後・AUTO_RESUME前は override.lateral=true を維持（自己永続）",
            all(f.get("override", {}).get("lateral") for f in until_resume),
            f"該当フレーム中 lateral=false の件数={sum(1 for f in until_resume if not f.get('override',{}).get('lateral'))}",
        )
    )

    # servo release: once latched, ffb.target_active must go (and stay) false
    # even while push_until_s has not yet been reached (release is driven by
    # the latch, not by the pushback returning to 0). Skip the exact latch
    # frame itself: the bus is DELIBERATELY up to one frame behind (declared
    # order MD-before-VD -- see DomainOwnershipLedger.hpp's "FRAME ALIGNMENT"
    # note), so MD's servo still sees last frame's manual=false for that one
    # frame. Measured: t_latch itself shows target_active=true, t_latch+DT
    # onward is false -- exactly the documented <=1-frame lag, not a defect
    # (same shape as 1fa408b9's own "1 frame off, 0 once shifted" finding).
    still_pushing_after_latch = [f for f in after if t_latch < f["sim_time"] < 6.0]
    if still_pushing_after_latch:
        checks.append(
            (
                "ラッチ後はサーボが不活性化する（ffb.target_active=false）— 修正Aの確認",
                all(
                    not f.get("ffb", {}).get("target_active", True)
                    for f in still_pushing_after_latch
                ),
                f"target_active=true の残存フレーム数={sum(1 for f in still_pushing_after_latch if f.get('ffb',{}).get('target_active'))}",
            )
        )

    # vehicle steering follow: driver.steer must NOT collapse to ~0 right at
    # the latch edge -- it should track something close to the pushed axis
    # (auto target + pushback offset), not VirtualDriver's own stub-zeroed
    # local frame. Compare the frame just after latch against the frame just
    # before.
    steer_before = before[-1]["driver"]["steer"] if before else None
    steer_after_latch = after[0]["driver"]["steer"] if after else None
    checks.append(
        (
            "ラッチ直後も driver.steer が 0 に張り付かない（修正Bの確認）",
            steer_after_latch is not None and abs(steer_after_latch) > 0.05,
            f"直前={steer_before}, 直後={steer_after_latch}",
        )
    )

    # --- round trip: AUTO_RESUME must actually bring VD back (修正C/task 6) ---
    auto_edges = [f for f in frames if f.get("override", {}).get("auto_transition")]
    checks.append(
        (
            "AUTO_RESUME でラッチが解ける（auto_transition が1回だけ立つ）",
            len(auto_edges) == 1,
            f"auto_transition 回数={len(auto_edges)}, at t={[round(f['sim_time'],3) for f in auto_edges]}",
        )
    )
    if auto_edges:
        t_resume = auto_edges[0]["sim_time"]
        post_resume = [f for f in frames if f["sim_time"] >= t_resume]
        checks.append(
            (
                "復帰後は override.lateral=false を維持",
                all(not f.get("override", {}).get("lateral") for f in post_resume),
                f"復帰後フレーム中 lateral=true の件数={sum(1 for f in post_resume if f.get('override',{}).get('lateral'))}",
            )
        )
        # servo re-arms: once resumed, ffb.target_active must go true again
        # (VD is lateral owner + lat_manual=false -> active=!lat_manual=true
        # at the source; and 6a's active=!owner_manual on the consume side).
        settled = [f for f in post_resume if f["sim_time"] >= t_resume + 0.5]
        if settled:
            checks.append(
                (
                    "復帰後はサーボが再アクティブ化する（ffb.target_active=true）",
                    all(f.get("ffb", {}).get("target_active") for f in settled),
                    f"target_active=false の残存フレーム数={sum(1 for f in settled if not f.get('ffb',{}).get('target_active'))}",
                )
            )

    return checks


def main() -> int:
    outdir = Path(tempfile.mkdtemp(prefix="f7_reverse_latch_"))
    print(f"outdir = {outdir}")
    frames = run(outdir)
    print(f"frames captured = {len(frames)}")
    if frames:
        sample = frames[len(frames) // 2]
        print("sample mid-run frame keys:", sorted(sample.keys()))

    checks = verdict(frames)
    ok_all = True
    for label, ok, detail in checks:
        print(f"  [{'PASS' if ok else 'FAIL'}] {label} — {detail}")
        ok_all = ok_all and ok
    print(f"\n==== OVERALL: {'PASS' if ok_all else 'FAIL'} ====")
    print(f"telemetry jsonl: {outdir / 'reverse_latch.jsonl'}")
    return 0 if ok_all else 1


if __name__ == "__main__":
    sys.exit(main())
