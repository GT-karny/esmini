"""F7b characterization data-prep: capture REAL AD steering profiles (no rig).

The Day-1 spike only ever drove the wheel with synthetic waveforms at ±0.45
amplitude. The real-machine bug lives an order of magnitude below that, so
scripts 09/10 replay the ACTUAL AD steering time series instead.

This script runs each scenario headless and in-process through the existing
GT_esminiLib build (no C++ rebuild needed), then writes one CSV per scenario:

    t_s, steer, speed, heading, lat_accel

* `steer`     is `driver.steer` — literally the value ControllerVirtualDriver
              hands to `IFFBSink::SetSteerTarget` (normalized [-1..1]).
* `lat_accel` is derived as v * dpsi/dt (body-frame lateral acceleration),
              which is what SDLFFBSink reads out of HVD for the reactive-SAT
              term. Derived rather than logged because telemetry.jsonl does
              not carry acceleration; the derivation is checked against the
              field log.txt values in CHARACTERIZATION.md §5.

Usage:
    DriverScript/.venv/Scripts/python.exe scripts/ffb_spike/extract_ad_profiles.py
"""
from __future__ import annotations

import json
import math
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(__file__).parent / "profiles"
VENV_PY = ROOT / "DriverScript" / ".venv" / "Scripts" / "python.exe"
RUNNER = ROOT / "GT_esmini" / "scripts" / "verification" / "gt_sim_test.py"

SCENARIOS = {
    # The reported failure case: a lane change whose whole steering command
    # stays under 0.07 normalized — 13x smaller than the Day-1 spike amplitude.
    "lc":         "resources/xosc/virtual_driver_basic.xosc",
    "curve":      "resources/xosc/verification/05_anticipation/decelerate_for_curve.xosc",
    "right_turn": "resources/xosc/verification/05_anticipation/decelerate_for_right_turn.xosc",
}


def unwrap(prev: float, cur: float) -> float:
    d = cur - prev
    while d > math.pi:
        d -= 2 * math.pi
    while d < -math.pi:
        d += 2 * math.pi
    return d


def convert(run_dir: Path, name: str) -> dict:
    rows = [json.loads(l) for l in (run_dir / "telemetry.jsonl").read_text().splitlines() if l.strip()]
    out = []
    prev_t = prev_h = None
    for r in rows:
        t = r["sim_time"]
        ego = r["ego"]
        h, v = ego["h"], ego["speed"]
        if prev_t is None or t <= prev_t:
            lat = 0.0
        else:
            lat = v * (unwrap(prev_h, h) / (t - prev_t))
        prev_t, prev_h = t, h
        out.append((t, r["driver"]["steer"], v, h, lat))

    OUT.mkdir(exist_ok=True)
    path = OUT / f"{name}.csv"
    with path.open("w", encoding="utf-8", newline="") as fh:
        fh.write("t_s,steer,speed,heading,lat_accel\n")
        for (t, s, v, h, lat) in out:
            fh.write(f"{t:.4f},{s:.6f},{v:.4f},{h:.6f},{lat:.4f}\n")

    steers = [abs(s) for (_, s, _, _, _) in out]
    lats = [abs(x) for (_, _, _, _, x) in out]
    return {"name": name, "frames": len(out), "dur_s": round(out[-1][0], 2),
            "steer_abs_max": round(max(steers), 4),
            "lat_abs_max": round(max(lats), 3),
            "frac_over_0035": round(sum(1 for s in steers if s > 0.035) / len(steers), 3),
            "csv": str(path.relative_to(ROOT))}


def main() -> int:
    if not (ROOT / "build" / "GT_esmini" / "Release" / "GT_esminiLib.dll").exists():
        print("[FAIL] GT_esminiLib.dll missing — build Release first")
        return 2
    summary = []
    with tempfile.TemporaryDirectory() as tmp:
        for name, rel in SCENARIOS.items():
            dest = Path(tmp) / name
            print(f"[RUN] {name}: {rel}")
            rc = subprocess.run([str(VENV_PY), str(RUNNER), "run", rel, "--out", str(dest)],
                                cwd=ROOT, capture_output=True, text=True)
            if not (dest / "telemetry.jsonl").exists():
                print(f"[FAIL] {name}: no telemetry\n{rc.stdout[-800:]}\n{rc.stderr[-800:]}")
                return 3
            info = convert(dest, name)
            summary.append(info)
            print(f"       frames={info['frames']} dur={info['dur_s']}s "
                  f"|steer|max={info['steer_abs_max']} |lat_a|max={info['lat_abs_max']} "
                  f"frac(|steer|>0.035)={info['frac_over_0035']}")
    (OUT / "index.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(f"\n[OK] wrote {len(summary)} profiles to {OUT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
