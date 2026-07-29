"""feature:F7 Phase-2 (regression-baseline batch) — AD normal-steering envelope statistics.

Reads telemetry.jsonl produced by `gt_sim_test.py batch` for the
car_following_traffic_control_batch.yaml manifest (12 scenarios: 4 traffic
signal + 5 traffic sign + 3 lead-vehicle) and derives the SAME command-based
kinematic metrics as Phase 1 (`f7_envelope_analysis.py`):

    delta      = steer_norm * max_steer_angle   (0.61 rad, PIDPurePursuitDriver.hpp:17)
    kappa      = tan(delta) / wheel_base        (wheel_base = bbox_length*0.6 = 3.0m
                                                   for all 12 scenarios, confirmed by
                                                   grepping each xosc's Dimensions)
    a_lat      = v^2 * kappa           [m/s^2]
    yaw_rate   = v * kappa             [rad/s]
    steer_rate = d(delta)/dt           [rad/s]

dt = 0.05s (--fixed_timestep, same as Phase 1). Each run's telemetry has a
"frozen tail" after the storyboard's StopCondition fires (sim_time stops
advancing but frames keep being written up to max_time padding) -- this
script trims each scenario at the first repeated sim_time and computes
stats only on the active (pre-freeze) portion, so percentiles are not
diluted by padding rows.

No simulation is run by this script; it only reads already-produced
telemetry.jsonl files (produced by a separate `gt_sim_test.py batch`
invocation).

Usage:
    DriverScript/.venv/Scripts/python.exe scripts/ffb_spike/f7_envelope_phase2.py <out_root>
"""
from __future__ import annotations

import json
import math
import sys
from pathlib import Path

MAX_STEER_ANGLE = 0.61  # rad
# feature:F7 — NOT a safety comparison, and must never become one with this
# constant. The product derives wheel_base as boundingbox.length*0.6, so 3.0 is
# right for these 12 scenarios BY COINCIDENCE of their vehicle size, not by
# construction; a scenario with a different bbox silently invalidates it. That
# coincidence, plus the pre/post-integration speed skew, is exactly what
# produced a phantom 0.88% envelope "overshoot" in f7_envelope_acceptance.py.
# This file characterizes the DISTRIBUTION of the AD's raw request
# (driver.steer, pre-envelope) to inform limit selection — it makes no claim
# about what reached the vehicle. If you ever want that claim, read the
# published envelope.kappa_out / envelope.kappa_limit instead of computing here.
WHEEL_BASE = 3.0  # m, all 12 scenarios use bbox length=5.0 -> 5.0*0.6


def pct(sorted_vals: list[float], p: float) -> float:
    if not sorted_vals:
        return float("nan")
    idx = min(len(sorted_vals) - 1, int(round(p * (len(sorted_vals) - 1))))
    return sorted_vals[idx]


def stats(vals: list[float]) -> dict:
    s = sorted(vals)
    return {"max": max(s) if s else float("nan"), "p99": pct(s, 0.99),
            "p95": pct(s, 0.95), "median": pct(s, 0.50), "n": len(s)}


def load_active_rows(path: Path) -> list[dict]:
    rows = [json.loads(l) for l in path.read_text(encoding="utf-8").splitlines() if l.strip()]
    trimmed = []
    prev_t = None
    for r in rows:
        t = r["sim_time"]
        if prev_t is not None and t <= prev_t:
            break  # frozen tail starts here
        trimmed.append(r)
        prev_t = t
    return trimmed


def analyze(name: str, path: Path) -> dict:
    rows = load_active_rows(path)
    deltas = [r["driver"]["steer"] * MAX_STEER_ANGLE for r in rows]
    speeds = [r["ego"]["speed"] for r in rows]
    kappas = [math.tan(d) / WHEEL_BASE for d in deltas]
    a_lat = [abs(v * v * k) for v, k in zip(speeds, kappas)]
    yaw_rate = [abs(v * k) for v, k in zip(speeds, kappas)]
    dts = [rows[i]["sim_time"] - rows[i - 1]["sim_time"] for i in range(1, len(rows))]
    steer_rate = [abs(deltas[i] - deltas[i - 1]) / dts[i - 1] for i in range(1, len(rows)) if dts[i - 1] > 0]
    return {"name": name, "n_active": len(rows), "dur_s": rows[-1]["sim_time"] if rows else 0.0,
            "v_max": max(speeds) if speeds else 0.0,
            "a_lat": a_lat, "yaw_rate": yaw_rate, "steer_rate": steer_rate}


def main() -> None:
    out_root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("test_results/web/phase3")
    scen_dirs = sorted(p for p in out_root.iterdir() if (p / "telemetry.jsonl").exists())
    results = [analyze(p.name, p / "telemetry.jsonl") for p in scen_dirs]

    print(f"=== feature:F7 Phase-2 envelope analysis: {len(results)} scenarios from {out_root} ===\n")
    print(f"{'scenario':32s} {'frames':>7s} {'dur_s':>7s} {'v_max':>7s} "
          f"{'a_lat_max':>10s} {'yaw_max':>9s} {'steerRate_max':>14s}")
    for r in results:
        print(f"{r['name']:32s} {r['n_active']:7d} {r['dur_s']:7.2f} {r['v_max']:7.2f} "
              f"{max(r['a_lat']) if r['a_lat'] else 0:10.4f} "
              f"{max(r['yaw_rate']) if r['yaw_rate'] else 0:9.4f} "
              f"{max(r['steer_rate']) if r['steer_rate'] else 0:14.4f}")

    pooled = {"a_lat": [], "yaw_rate": [], "steer_rate": []}
    for r in results:
        for k in pooled:
            pooled[k].extend(r[k])

    print(f"\n=== pooled across all {len(results)} scenarios (n_a_lat={len(pooled['a_lat'])}) ===")
    for label, key in (("a_lat [m/s^2]", "a_lat"), ("yaw_rate [rad/s]", "yaw_rate"),
                        ("steer_rate [rad/s]", "steer_rate")):
        s = stats(pooled[key])
        print(f"  {label:20s} max={s['max']:.4f}  p99={s['p99']:.4f}  p95={s['p95']:.4f}  median={s['median']:.4f}")

    print("\n=== global max (scenario) per metric ===")
    for key in ("a_lat", "yaw_rate", "steer_rate"):
        best = max(results, key=lambda r: max(r[key]) if r[key] else 0)
        print(f"  {key}: {max(best[key]):.4f} ({best['name']})")


if __name__ == "__main__":
    main()
