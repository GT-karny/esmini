"""F7b acceptance — does the PRODUCT move the real wheel? (real-time, real G29)

This is the check the previous F7b iteration skipped, and skipping it is why a
"verified" fix shipped with the wheel physically motionless: telemetry showing a
non-zero servo force (`tt`) is NOT evidence that the wheel moved.

What makes this measurement trustworthy:
  * It runs the REAL product (GT_esminiLib via the GT C API) with
    input_type=sdl2_wheel — no Python re-implementation of the servo.
  * The wheel position is the PRODUCT's own reading of the hardware:
    SDLFFBSink::ReadPhysicalWheelNorm() feeds FfbInterventionSample.position_error,
    which is published per frame as telemetry `ffb.position_error`. With
    `driver.steer` as the target, actual_axis = steer - position_error.
  * It is paced to WALL CLOCK. gt_sim_test runs decoupled from real time, which
    would ask a physical wheel to replay 20 s of steering in ~2 s — it would
    fail for reasons having nothing to do with the servo.

Hands OFF the wheel for the whole run: the second thing this measures is that
nobody-touching does NOT latch a false override.

    scripts/ffb_spike/.venv/Scripts/python.exe scripts/ffb_spike/11_rig_followcheck.py \
        --scenario resources/xosc/virtual_driver_basic.xosc --label lc --yes

Requires build/GT_esmini/config/virtual_driver.json to have
input_type=sdl2_wheel and ffb_target_track_enabled=true. RESTORE IT FROM SOURCE
AFTERWARDS — a drifted build config silently poisons the next regression gate
(guarded by run_regression_gate.ps1 Step 0).
"""
from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "GT_esmini" / "scripts" / "verification"))

from gt_sim_test import GtLib  # noqa: E402

LOGS = Path(__file__).parent / "logs"
LOGS.mkdir(exist_ok=True)


def run(scenario: str, label: str, dt: float, max_time: float) -> dict:
    lib = GtLib()
    frames = []
    grace, grace_max, seen = 0, int(round(1.0 / dt)), False

    with lib:
        rc = lib.init_with_args(["--osc", str(ROOT / scenario), "--headless",
                                 "--fixed_timestep", str(dt)])
        if rc != 0:
            raise RuntimeError(f"GT_InitWithArgs rc={rc}: {lib.get_last_error()}")

        t0 = time.perf_counter()
        for i in range(int(round(max_time / dt))):
            lib.step(dt)
            tel = lib.get_vd_telemetry(-1)
            if tel is None:
                if seen:
                    grace += 1
                    if grace >= grace_max:
                        break
            else:
                seen, grace = True, 0
                frames.append(tel)
            # Pace to wall clock — the wheel is a physical object and must be
            # asked to follow at the speed the scenario actually plays at.
            # Pace against the DLL's OWN sim_time, not against frame_count*dt:
            # the telemetry frame rate is not necessarily 1 per requested step,
            # and pacing on the latter silently ran the wheel at half speed,
            # which makes following easier than reality.
            if tel is not None:
                slack = (t0 + tel["sim_time"]) - time.perf_counter()
                if slack > 0:
                    time.sleep(slack)
        wall = time.perf_counter() - t0

    rows = []
    for f in frames:
        ffb = f.get("ffb") or {}
        steer = f["driver"]["steer"]
        perr = ffb.get("position_error", 0.0)
        rows.append({
            "t": f["sim_time"],
            "target": steer,
            # The product's own read of the physical G29 axis.
            "axis": steer - perr if ffb.get("target_active") else 0.0,
            "active": bool(ffb.get("target_active")),
            "force": ffb.get("commanded_force", 0.0),
            "ov_lat": bool(f["override"]["lateral"]),
        })

    act = [r for r in rows if r["active"]]
    steering = [r for r in act if abs(r["target"]) > 0.005]
    peak_t = max((abs(r["target"]) for r in rows), default=0.0)
    peak_a = max((abs(r["axis"]) for r in act), default=0.0)
    moved = [r for r in act if abs(r["axis"]) > 0.004]

    res = {
        "label": label, "scenario": scenario,
        "frames": len(rows), "sim_s": round(rows[-1]["t"], 2) if rows else 0.0,
        "wall_s": round(wall, 2),
        "realtime_ratio": round(wall / rows[-1]["t"], 3) if rows and rows[-1]["t"] else None,
        "servo_active_frames": len(act),
        "wheel_ever_moved": bool(moved),
        "frac_time_moved": round(len(moved) / len(act), 4) if act else 0.0,
        "peak_abs_target": round(peak_t, 5),
        "peak_abs_axis": round(peak_a, 5),
        "follow_pct": round(100.0 * peak_a / peak_t, 1) if peak_t else None,
        "err_mean_while_steering": round(
            sum(abs(r["target"] - r["axis"]) for r in steering) / len(steering), 5)
        if steering else None,
        # False-latch evidence: hands off the whole run.
        "override_lateral_ever": any(r["ov_lat"] for r in rows),
        "max_commanded_force": round(max((r["force"] for r in rows), default=0.0), 4),
        "frac_force_over_020": round(
            sum(1 for r in act if r["force"] > 0.20) / len(act), 4) if act else 0.0,
    }
    # Summary JSON is committed; the per-frame trace goes to a gitignored CSV
    # (logs/*.csv), matching the Day-1 convention of committing only summaries.
    (LOGS / f"11_rig_{label}.json").write_text(json.dumps(res, indent=1), encoding="utf-8")
    if rows:
        import csv as _csv
        with (LOGS / f"11_rig_{label}.csv").open("w", newline="", encoding="utf-8") as fh:
            w = _csv.DictWriter(fh, fieldnames=list(rows[0].keys()))
            w.writeheader()
            w.writerows(rows)
    return res


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--scenario", default="resources/xosc/virtual_driver_basic.xosc")
    ap.add_argument("--label", default="lc")
    ap.add_argument("--dt", type=float, default=0.05)
    ap.add_argument("--max-time", type=float, default=40.0)
    ap.add_argument("--yes", action="store_true")
    args = ap.parse_args()

    print("=" * 68)
    print(f"F7b rig follow-check — {args.label} ({args.scenario})")
    print("=" * 68)
    print("!! The wheel WILL move. HANDS OFF for the whole run (this also")
    print("   measures that no-touch does not latch a false override).")
    if not args.yes:
        for k in (3, 2, 1):
            print(f"   starting in {k} s...", end="\r")
            time.sleep(1)
        print()

    res = run(args.scenario, args.label, args.dt, args.max_time)
    print(f"\n  frames={res['frames']} sim={res['sim_s']}s wall={res['wall_s']}s "
          f"(realtime x{res['realtime_ratio']})")
    print(f"  servo active frames : {res['servo_active_frames']}")
    print(f"  WHEEL MOVED         : {res['wheel_ever_moved']}  "
          f"(moving {res['frac_time_moved']*100:.1f}% of active frames)")
    print(f"  peak axis / target  : {res['peak_abs_axis']:.4f} / {res['peak_abs_target']:.4f}"
          f"  -> follow {res['follow_pct']}%")
    print(f"  err while steering  : {res['err_mean_while_steering']}")
    print(f"  override.lateral    : {res['override_lateral_ever']}  "
          f"(max |u_fb|={res['max_commanded_force']}, "
          f"over 0.20 for {res['frac_force_over_020']*100:.1f}% of frames)")
    print(f"\n[OK] wrote {LOGS / ('11_rig_' + args.label + '.json')}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
