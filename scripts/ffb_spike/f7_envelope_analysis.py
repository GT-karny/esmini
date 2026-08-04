"""feature:F7 Phase-1 (offline) — AD normal-steering envelope statistics.

Consumes the already-extracted AD steering profiles
(`scripts/ffb_spike/profiles/{lc,curve,right_turn}.csv`, produced by
`extract_ad_profiles.py`; columns: t_s, steer, speed, heading, lat_accel)
and derives the distribution of steer-rate, curvature, lateral accel and
yaw-rate implied by the commanded steering, using the SAME kinematic model
the VD controller itself uses (PIDPurePursuitDriver.cpp / ControllerVirtualDriver.cpp):

    delta   = steer_norm * max_steer_angle      (max_steer_angle = 0.61 rad,
                                                   PIDPurePursuitDriver.hpp:17)
    kappa   = tan(delta) / wheel_base            (wheel_base = bbox_length*0.6,
                                                   ControllerVirtualDriver.cpp:292;
                                                   lc: 5.04*0.6=3.024m,
                                                   curve/right_turn: 5.0*0.6=3.0m)
    a_lat   = v^2 * kappa
    yaw_rate= v * kappa
    steer_rate = d(delta)/dt   (dt taken from consecutive t_s, = 0.05s / 20Hz
                                 in these CSVs — this is the telemetry logging
                                 period, not necessarily the sim physics step;
                                 flagged, not silently assumed finer)

No simulation is run by this script. Read-only analysis of existing CSVs.

Usage:
    DriverScript/.venv/Scripts/python.exe scripts/ffb_spike/f7_envelope_analysis.py
"""
from __future__ import annotations

import csv
import math
from pathlib import Path

HERE = Path(__file__).parent
PROFILES = HERE / "profiles"

MAX_STEER_ANGLE = 0.61  # rad, PIDPurePursuitDriver.hpp:17 / RealVehicle steer_gain
WHEEL_BASE = {
    "lc": 5.04 * 0.6,          # VehicleCatalog.xosc car_white length=5.04
    "curve": 5.0 * 0.6,        # inline car_white in decelerate_for_curve.xosc, length=5.0
    "right_turn": 5.0 * 0.6,   # inline car_white in decelerate_for_left_turn.xosc, length=5.0
    # (key stays "right_turn" -- matches the committed profiles/right_turn.csv /
    # index.json artifact name from before the 2026-08-04 scenario rename)
}

PATHOLOGICAL_KAPPA = 0.25  # known pathological case: lateral offset 2m @ 8m/s, alpha~=30deg
PATHOLOGICAL_V = 8.0
PATHOLOGICAL_ALAT = PATHOLOGICAL_V ** 2 * PATHOLOGICAL_KAPPA  # = 16 m/s^2


def pct(sorted_vals: list[float], p: float) -> float:
    if not sorted_vals:
        return float("nan")
    idx = min(len(sorted_vals) - 1, int(round(p * (len(sorted_vals) - 1))))
    return sorted_vals[idx]


def stats(vals: list[float]) -> dict:
    s = sorted(vals)
    return {
        "max": max(s) if s else float("nan"),
        "p99": pct(s, 0.99),
        "p95": pct(s, 0.95),
        "median": pct(s, 0.50),
        "n": len(s),
    }


def load_profile(name: str) -> list[dict]:
    path = PROFILES / f"{name}.csv"
    rows = []
    with path.open(encoding="utf-8") as fh:
        for r in csv.DictReader(fh):
            rows.append({k: float(v) for k, v in r.items()})
    return rows


def analyze(name: str) -> dict:
    rows = load_profile(name)
    wb = WHEEL_BASE[name]

    dts = [rows[i]["t_s"] - rows[i - 1]["t_s"] for i in range(1, len(rows))]
    dt_set = sorted(set(round(d, 4) for d in dts))

    deltas = [r["steer"] * MAX_STEER_ANGLE for r in rows]
    kappas = [math.tan(d) / wb for d in deltas]
    speeds = [r["speed"] for r in rows]

    a_lat = [v * v * k for v, k in zip(speeds, kappas)]
    yaw_rate = [v * k for v, k in zip(speeds, kappas)]

    steer_rates = [abs(deltas[i] - deltas[i - 1]) / dts[i - 1] for i in range(1, len(rows)) if dts[i - 1] > 0]

    return {
        "name": name,
        "n_frames": len(rows),
        "dt_set_s": dt_set,
        "wheel_base_m": wb,
        "speed_max": max(speeds),
        "steer_rate_rad_s": stats(steer_rates),
        "a_lat_mps2": stats([abs(x) for x in a_lat]),
        "yaw_rate_rad_s": stats([abs(x) for x in yaw_rate]),
        "kappa_1_m": stats([abs(x) for x in kappas]),
    }


def main() -> None:
    results = {name: analyze(name) for name in WHEEL_BASE}

    print("=== feature:F7 Phase-1 offline envelope analysis ===")
    print(f"pathological reference: lateral offset 2m @ v=8m/s -> kappa=0.25, "
          f"a_lat={PATHOLOGICAL_ALAT:.1f} m/s^2\n")

    for name, r in results.items():
        print(f"--- {name} (frames={r['n_frames']}, dt={r['dt_set_s']}s, "
              f"wheel_base={r['wheel_base_m']:.3f}m, v_max={r['speed_max']:.2f}m/s) ---")
        for label, key in (("steer_rate [rad/s]", "steer_rate_rad_s"),
                            ("a_lat [m/s^2]", "a_lat_mps2"),
                            ("yaw_rate [rad/s]", "yaw_rate_rad_s"),
                            ("|kappa| [1/m]", "kappa_1_m")):
            s = r[key]
            print(f"  {label:22s} max={s['max']:.4f}  p99={s['p99']:.4f}  "
                  f"p95={s['p95']:.4f}  median={s['median']:.4f}")
        print()

    # global (max over the 3 scenarios) per metric
    g_steer_rate = max(r["steer_rate_rad_s"]["max"] for r in results.values())
    g_a_lat = max(r["a_lat_mps2"]["max"] for r in results.values())
    g_yaw_rate = max(r["yaw_rate_rad_s"]["max"] for r in results.values())
    print(f"=== global max across {list(results)} ===")
    print(f"steer_rate_max_observed = {g_steer_rate:.4f} rad/s")
    print(f"a_lat_max_observed      = {g_a_lat:.4f} m/s^2")
    print(f"yaw_rate_max_observed   = {g_yaw_rate:.4f} rad/s")


if __name__ == "__main__":
    main()
