"""F7b characterization 10 — stiction countermeasures, head to head.

Characterization item (6). Script 08 established that the shipped pure-P servo
has a static-friction deadband of F_break/Kp ≈ 0.047, which swallows an entire
AD lane change. This script measures the classical ways out, on the real wheel,
under one common test so the numbers are directly comparable:

  none       — baseline: u = -(Kp*err + Kd*derr)
  ff         — Coulomb friction feed-forward: u -= F_ff * tanh(err/eps).
               Compensates the known constant friction torque directly instead
               of asking the P term to synthesize it from position error.
  integrator — u -= Ki*∫err dt (contribution clamped). Winds up until the force
               exceeds breakaway, then unwinds.
  dither     — superimpose a small high-frequency oscillation on the command;
               the classic way to keep a joint out of the stuck regime.
  punch      — detect "commanded but not moving" and fire a short full-force
               pulse to break stiction, then hand back to the servo.

Two phases per method:
  A. amplitude sweep (the deadband question) — production-scale steps
  B. hold-at-target (the side-effect question) — chatter / buzz once settled,
     which is what a driver resting a hand on the wheel would feel.

    scripts/ffb_spike/.venv/Scripts/python.exe scripts/ffb_spike/10_stiction_methods.py --yes
"""
from __future__ import annotations

import argparse
import json
import sys
import time

from g29lib import DT, LOGS, Rig, Servo, ServoParams, banner, pstdev, write_csv

AMPS = [0.010, 0.020, 0.035, 0.065]
METHODS = ["none", "ff", "integrator", "dither", "punch"]


def sweep(rig: Rig, method: str, args, rows) -> list:
    out = []
    for amp in [float(a) for a in args.amps.split(",")]:
        for sign, dname in ((+1.0, "right"), (-1.0, "left")):
            rig.recenter(0.0)
            base = rig.settle(0.5)
            rig.set_reference(base)
            target = base + sign * amp
            servo = Servo(method, ServoParams.from_args(args))
            t0 = time.perf_counter()
            first_move, samples = None, []
            while True:
                t = time.perf_counter() - t0
                if t >= args.duration:
                    break
                a = rig.axis()
                u, _ = servo.step(target, a, t)
                if not rig.guard(a):
                    u = 0.0
                u = rig.set_force(u)
                samples.append((t, a, target - a, u))
                rows.append((method, f"{amp:.3f}", dname, f"{t:.4f}", f"{a:.6f}",
                             f"{target-a:+.6f}", f"{u:+.4f}"))
                if first_move is None and abs(a - base) > 0.004:
                    first_move = t
                time.sleep(DT)
            rig.set_force(0.0)
            tail = [s for s in samples if s[0] > args.duration * 0.6]
            r = {"method": method, "amp": amp, "dir": dname,
                 "moved": first_move is not None,
                 "first_move_s": None if first_move is None else round(first_move, 3),
                 "final_disp": round(rig.axis() - base, 5),
                 "tail_abs_err": round(sum(abs(s[2]) for s in tail) / len(tail), 5) if tail else None,
                 "tail_axis_ptp": round(max(s[1] for s in tail) - min(s[1] for s in tail), 5) if tail else None,
                 "tail_u_std": round(pstdev(s[3] for s in tail), 4) if tail else None,
                 "u_max": round(max(abs(s[3]) for s in samples), 4),
                 "punches": servo.punches}
            out.append(r)
            print(f"  {method:<10} amp={amp:.3f} {dname:<5}: moved={str(r['moved']):<5} "
                  f"final={r['final_disp']:+.4f} tail_err={r['tail_abs_err']} "
                  f"ptp={r['tail_axis_ptp']} u_std={r['tail_u_std']} punches={r['punches']}")
    return out


def hold(rig: Rig, method: str, args, rows) -> dict:
    """Side-effect probe: sit at a reached target and measure residual activity."""
    rig.recenter(0.0)
    base = rig.settle(0.5)
    rig.set_reference(base)
    target = base + 0.065          # a reached, production-scale target
    servo = Servo(method, ServoParams.from_args(args))
    t0 = time.perf_counter()
    samples = []
    while True:
        t = time.perf_counter() - t0
        if t >= args.hold_s:
            break
        a = rig.axis()
        u, _ = servo.step(target, a, t)
        if not rig.guard(a):
            u = 0.0
        u = rig.set_force(u)
        samples.append((t, a, u))
        rows.append((method, "hold", "-", f"{t:.4f}", f"{a:.6f}",
                     f"{target-a:+.6f}", f"{u:+.4f}"))
        time.sleep(DT)
    rig.set_force(0.0)
    tail = [s for s in samples if s[0] > args.hold_s * 0.5]
    r = {"method": method,
         "hold_axis_ptp": round(max(s[1] for s in tail) - min(s[1] for s in tail), 5),
         "hold_axis_std": round(pstdev(s[1] for s in tail), 6),
         "hold_u_mean_abs": round(sum(abs(s[2]) for s in tail) / len(tail), 4),
         "hold_u_std": round(pstdev(s[2] for s in tail), 4),
         "hold_final_err": round(target - rig.axis(), 5)}
    print(f"  {method:<10} HOLD: axis_ptp={r['hold_axis_ptp']} axis_std={r['hold_axis_std']} "
          f"|u|mean={r['hold_u_mean_abs']} u_std={r['hold_u_std']} err={r['hold_final_err']:+.4f}")
    return r


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--methods", default=",".join(METHODS))
    ap.add_argument("--amps", default=",".join(str(a) for a in AMPS))
    ap.add_argument("--kp", type=float, default=4.0)
    ap.add_argument("--kd", type=float, default=0.35)
    ap.add_argument("--max-force", type=float, default=0.6)
    ap.add_argument("--fstat", type=float, default=0.15)
    ap.add_argument("--ff-eps", type=float, default=0.01)
    ap.add_argument("--ki", type=float, default=30.0)
    ap.add_argument("--i-max", type=float, default=0.25)
    ap.add_argument("--dither-amp", type=float, default=0.12)
    ap.add_argument("--dither-hz", type=float, default=30.0)
    ap.add_argument("--punch-force", type=float, default=0.30)
    ap.add_argument("--punch-ms", type=float, default=60.0)
    ap.add_argument("--punch-err", type=float, default=0.008)
    ap.add_argument("--punch-stuck-s", type=float, default=0.12)
    ap.add_argument("--duration", type=float, default=2.5)
    ap.add_argument("--hold-s", type=float, default=4.0)
    ap.add_argument("--out", default="10_stiction_methods.json")
    ap.add_argument("--yes", action="store_true")
    args = ap.parse_args()

    methods = [m.strip() for m in args.methods.split(",")]
    banner("10 stiction methods",
           f"methods={methods} Kp={args.kp} amps={args.amps} "
           f"(dither {args.dither_amp}@{args.dither_hz}Hz, punch {args.punch_force}/"
           f"{args.punch_ms}ms, Ki={args.ki})", args.yes)

    rows, sweeps, holds = [], [], []
    with Rig(force_cap=args.max_force, excursion_limit=0.45) as rig:
        rig.settle(0.8)
        for m in methods:
            print(f"\n--- method: {m} ---")
            sweeps += sweep(rig, m, args, rows)
            holds.append(hold(rig, m, args, rows))
        rig.recenter(0.0)

    # per-method roll-up
    summary = []
    amps = [float(a) for a in args.amps.split(",")]
    for m in methods:
        rs = [r for r in sweeps if r["method"] == m]
        moved = [r["amp"] for r in rs if r["moved"]]
        floor = next((a for a in amps
                      if a in moved and all(x in moved for x in amps if x >= a)), None)
        errs = [r["tail_abs_err"] for r in rs if r["tail_abs_err"] is not None]
        h = next(x for x in holds if x["method"] == m)
        summary.append({"method": m, "move_floor": floor,
                        "tail_err_mean": round(sum(errs) / len(errs), 5) if errs else None,
                        "tail_err_max": round(max(errs), 5) if errs else None,
                        **{k: v for k, v in h.items() if k != "method"}})

    print("\n=== SUMMARY ===")
    print(f"{'method':<11}{'move_floor':>11}{'err_mean':>10}{'err_max':>9}"
          f"{'hold_ptp':>10}{'hold|u|':>9}{'hold_u_std':>11}")
    for s in summary:
        print(f"{s['method']:<11}{str(s['move_floor']):>11}{s['tail_err_mean']:>10}"
              f"{s['tail_err_max']:>9}{s['hold_axis_ptp']:>10}"
              f"{s['hold_u_mean_abs']:>9}{s['hold_u_std']:>11}")

    write_csv(LOGS / "10_stiction_methods.csv",
              ["method", "amp", "dir", "t_s", "axis", "err", "u"], rows)
    path = LOGS / args.out
    path.write_text(json.dumps({"config": vars(args), "summary": summary,
                                "sweeps": sweeps, "holds": holds},
                               indent=2), encoding="utf-8")
    print(f"\n[OK] wrote {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
