"""F7b characterization 08 — small-signal deadband of the target-track servo.

Characterization item (2). The Day-1 spike only measured following at ±0.45
amplitude; the real AD lane change never commands more than 0.065 (see
profiles/index.json). This script sweeps the amplitude DOWN through that
region and finds:

  * `move_floor`  — the smallest step amplitude that makes the wheel move AT ALL
  * `track_floor` — the smallest step amplitude the servo can track to within
                    `--tol` (default 0.01 axis-frac)

Both are reported per gain, because the governing relation is

      deadband ≈ F_break / Kp

i.e. a pure-P servo cannot generate more than Kp*err of force, so any error
smaller than F_break/Kp cannot break static friction. With the measured
F_break ≈ 0.19 (script 07) and the shipped Kp = 4.0 that floor is ≈ 0.047 —
which is most of a lane change.

    scripts/ffb_spike/.venv/Scripts/python.exe scripts/ffb_spike/08_smallsignal_deadband.py --yes
"""
from __future__ import annotations

import argparse
import json
import math
import sys
import time

from g29lib import DT, LOGS, Rig, banner, write_csv

AMPS = [0.010, 0.020, 0.035, 0.050, 0.075, 0.100, 0.150]


def step_trial(rig: Rig, amp: float, sign: float, kp: float, kd: float,
               max_force: float, fstat: float, duration: float, rows, tag: str) -> dict:
    base = rig.settle(0.6)
    rig.set_reference(base)
    target = base + sign * amp

    prev_err, primed = 0.0, False
    t0 = time.perf_counter()
    first_move = None
    samples = []
    while True:
        t = time.perf_counter() - t0
        if t >= duration:
            break
        a = rig.axis()
        err = target - a
        derr = (err - prev_err) / DT if primed else 0.0
        prev_err, primed = err, True
        u = -(kp * err + kd * derr)
        if fstat > 0.0:
            u -= fstat * math.tanh(err / 0.01)
        u = max(-max_force, min(max_force, u))
        if not rig.guard(a):
            u = 0.0
        u = rig.set_force(u)
        samples.append((t, a, err, u))
        rows.append((tag, f"{t:.4f}", f"{target:.5f}", f"{a:.6f}", f"{err:+.6f}", f"{u:+.4f}"))
        if first_move is None and abs(a - base) > 0.004:
            first_move = t
        time.sleep(DT)
    rig.set_force(0.0)

    tail = [s for s in samples if s[0] > duration * 0.6]
    tail_err = sum(abs(s[2]) for s in tail) / len(tail) if tail else float("nan")
    return {
        "amp": amp, "dir": "left" if sign < 0 else "right",
        "moved": first_move is not None,
        "first_move_s": None if first_move is None else round(first_move, 3),
        "final_disp": round(rig.axis() - base, 5),
        "tail_abs_err": round(tail_err, 5),
        "u_max": round(max(abs(s[3]) for s in samples), 4),
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--kp-list", default="4.0,8.0")
    ap.add_argument("--kd", type=float, default=0.35)
    ap.add_argument("--max-force", type=float, default=0.6)
    ap.add_argument("--fstat", type=float, default=0.0)
    ap.add_argument("--amps", default=",".join(str(a) for a in AMPS))
    ap.add_argument("--duration", type=float, default=2.5)
    ap.add_argument("--tol", type=float, default=0.010)
    ap.add_argument("--out", default="08_deadband.json")
    ap.add_argument("--yes", action="store_true")
    args = ap.parse_args()

    kps = [float(x) for x in args.kp_list.split(",")]
    amps = [float(x) for x in args.amps.split(",")]

    banner("08 small-signal deadband",
           f"Kp={kps} Kd={args.kd} max_force={args.max_force} fstat={args.fstat} "
           f"amps={amps}", args.yes)

    rows, results = [], []
    with Rig(force_cap=args.max_force, excursion_limit=0.45) as rig:
        rig.settle(0.8)
        for kp in kps:
            per_kp = []
            print(f"\n--- Kp={kp} Kd={args.kd} max_force={args.max_force} fstat={args.fstat} ---")
            for amp in amps:
                for sign in (+1.0, -1.0):
                    rig.recenter(0.0)
                    tag = f"kp{kp}_amp{amp}_{'R' if sign>0 else 'L'}"
                    r = step_trial(rig, amp, sign, kp, args.kd, args.max_force,
                                   args.fstat, args.duration, rows, tag)
                    r["kp"] = kp
                    per_kp.append(r)
                    print(f"  amp={amp:.3f} {r['dir']:<5}: moved={str(r['moved']):<5} "
                          f"final={r['final_disp']:+.4f} tail_err={r['tail_abs_err']:.4f} "
                          f"|u|max={r['u_max']:.3f}")
            moved = [r["amp"] for r in per_kp if r["moved"]]
            # `moved` is required: for amp <= tol a wheel that never budged has
            # tail_abs_err == amp <= tol and would otherwise score as "tracked".
            tracked = [r["amp"] for r in per_kp if r["moved"] and r["tail_abs_err"] <= args.tol]
            # A floor is only meaningful if every LARGER amplitude also passes.
            def floor(passing):
                for a in amps:
                    if a in passing and all(x in passing for x in amps if x >= a):
                        return a
                return None
            summary = {"kp": kp, "move_floor": floor(moved), "track_floor": floor(tracked),
                       "predicted_deadband": round(0.19 / kp, 4), "trials": per_kp}
            print(f"  => move_floor={summary['move_floor']} track_floor={summary['track_floor']} "
                  f"(F_break/Kp = {summary['predicted_deadband']})")
            results.append(summary)
        rig.recenter(0.0)

    write_csv(LOGS / "08_deadband.csv",
              ["tag", "t_s", "target", "axis", "err", "u"], rows)
    path = LOGS / args.out
    path.write_text(json.dumps({"kd": args.kd, "max_force": args.max_force,
                                "fstat": args.fstat, "tol": args.tol,
                                "results": results}, indent=2), encoding="utf-8")
    print(f"\n[OK] wrote {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
