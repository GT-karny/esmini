"""F7b characterization 07 — G29 friction map.

Answers characterization items (1) and (3):

  part=breakaway : static-friction (breakaway) force, measured at several
                   initial wheel angles and in both directions, repeated so
                   the spread is visible. "Does it take more force to start
                   the wheel when it is already turned?"
  part=hysteresis: static (ramp force UP from rest until it starts) vs kinetic
                   (ramp force DOWN while moving until it stops) — the stiction
                   hysteresis band. This band is what a position servo must
                   live inside.
  part=fv        : force -> wheel speed. Above breakaway, how fast does a given
                   constant force actually turn the wheel? Determines whether
                   the servo can keep up with an AD steering rate at all.

All parts are UNMANNED. Forces start low and step up.

    scripts/ffb_spike/.venv/Scripts/python.exe scripts/ffb_spike/07_friction_map.py --part all --yes
"""
from __future__ import annotations

import argparse
import json
import sys
import time

from g29lib import DT, LOGS, Rig, banner, pstdev, write_csv

# Nothing moved at all below 0.16 in script 06, so the staircase starts just
# under that instead of wasting rig time from 0.02.
STAIR_START = 0.10
STAIR_STOP = 0.40
STAIR_STEP = 0.01
HOLD = 0.35
MOVE_THRESH = 0.008      # axis-frac; script 06 showed the noise floor is ~0


def staircase(rig: Rig, base: float, sign: float, args, rows, tag: str):
    """Ramp |force| up from rest until the wheel starts moving. Returns force."""
    rig.set_reference(base)
    f = STAIR_START
    while f <= STAIR_STOP + 1e-9:
        rig.set_force(sign * f)
        t0 = time.perf_counter()
        disp_max = 0.0
        while time.perf_counter() - t0 < HOLD:
            a = rig.axis()
            disp_max = max(disp_max, abs(a - base))
            rows.append((tag, f"{f:.3f}", f"{time.perf_counter()-t0:.4f}",
                         f"{a:.6f}", f"{a-base:+.6f}"))
            if not rig.guard(a):
                break
            time.sleep(DT)
        rig.set_force(0.0)
        if disp_max > MOVE_THRESH:
            return f
        f = round(f + STAIR_STEP, 6)
    return None


def part_breakaway(rig: Rig, args) -> dict:
    rows, res = [], {}
    positions = [float(x) for x in args.positions.split(",")]
    for pos in positions:
        for sign, dname in ((+1.0, "left"), (-1.0, "right")):
            vals = []
            for rep in range(args.repeats):
                rig.recenter(pos)
                base = rig.settle(0.6)
                tag = f"pos{pos:+.2f}_{dname}_r{rep}"
                f = staircase(rig, base, sign, args, rows, tag)
                vals.append(f)
                print(f"  init={pos:+.2f} push={dname:<5} rep{rep}: "
                      f"breakaway={'NONE' if f is None else f'{f:.3f}'} (rest axis {base:+.4f})")
            ok = [v for v in vals if v is not None]
            res[f"pos{pos:+.2f}_{dname}"] = {
                "trials": vals,
                "mean": round(sum(ok) / len(ok), 4) if ok else None,
                "std": round(pstdev(ok), 4) if ok else None,
            }
    rig.recenter(0.0)
    write_csv(LOGS / "07_breakaway_map.csv",
              ["tag", "force", "t_in_step", "axis", "disp"], rows)
    return res


def part_hysteresis(rig: Rig, args) -> dict:
    """Static (start) vs kinetic (stop) level at the centre, both directions."""
    rows, res = [], {}
    for sign, dname in ((+1.0, "left"), (-1.0, "right")):
        statics, kinetics = [], []
        for rep in range(args.repeats):
            rig.recenter(0.0)
            base = rig.settle(0.6)
            f_static = staircase(rig, base, sign, args, rows, f"hyst_{dname}_r{rep}_up")

            f_kin = None
            if f_static is not None:
                # Get it moving with a small margin, then ramp the force down
                # while it is ALREADY in motion and find where motion ceases.
                rig.recenter(0.0)
                base2 = rig.settle(0.5)
                rig.set_reference(base2)
                g = min(STAIR_STOP, f_static + 3 * STAIR_STEP)
                rig.set_force(sign * g)
                time.sleep(0.25)
                while g > 0.0:
                    g = round(g - STAIR_STEP, 6)
                    rig.set_force(sign * g)
                    t0 = time.perf_counter()
                    a0 = rig.axis()
                    broke = False
                    while time.perf_counter() - t0 < 0.22:
                        a = rig.axis()
                        rows.append((f"hyst_{dname}_r{rep}_down", f"{g:.3f}",
                                     f"{time.perf_counter()-t0:.4f}", f"{a:.6f}",
                                     f"{a-base2:+.6f}"))
                        if not rig.guard(a):
                            broke = True
                            break
                        time.sleep(DT)
                    moved = abs(rig.axis() - a0)
                    if broke:
                        break
                    if moved < 0.004:
                        f_kin = g
                        break
                rig.set_force(0.0)

            statics.append(f_static)
            kinetics.append(f_kin)
            print(f"  {dname:<5} rep{rep}: static={f_static} kinetic={f_kin}")
        s_ok = [v for v in statics if v is not None]
        k_ok = [v for v in kinetics if v is not None]
        res[dname] = {
            "static_trials": statics, "kinetic_trials": kinetics,
            "static_mean": round(sum(s_ok) / len(s_ok), 4) if s_ok else None,
            "kinetic_mean": round(sum(k_ok) / len(k_ok), 4) if k_ok else None,
            "hysteresis_band": (round(sum(s_ok) / len(s_ok) - sum(k_ok) / len(k_ok), 4)
                                if s_ok and k_ok else None),
        }
    rig.recenter(0.0)
    write_csv(LOGS / "07_hysteresis.csv",
              ["tag", "force", "t_in_step", "axis", "disp"], rows)
    return res


def part_fv(rig: Rig, args) -> dict:
    """Constant force -> wheel angular speed (axis-frac per second)."""
    rows, res = [], []
    forces = [round(x, 3) for x in _frange(args.fv_start, args.fv_stop, args.fv_step)]
    for f in forces:
        for sign, dname in ((+1.0, "left"), (-1.0, "right")):
            rig.recenter(0.0)
            base = rig.settle(0.5)
            rig.set_reference(base)
            trace = []
            rig.set_force(sign * f)
            t0 = time.perf_counter()
            while True:
                t = time.perf_counter() - t0
                a = rig.axis()
                trace.append((t, a))
                rows.append((f"{f:.3f}", dname, f"{t:.4f}", f"{a:.6f}", f"{a-base:+.6f}"))
                if t > args.fv_burst or abs(a - base) > args.fv_travel:
                    break
                time.sleep(DT)
            rig.set_force(0.0)
            vel = _velocity(trace, base)
            res.append({"force": f, "dir": dname, **vel})
            print(f"  f={f:.3f} {dname:<5}: v_peak={vel['v_peak']:+.4f} "
                  f"v_late={vel['v_late']:+.4f} travel={vel['travel']:+.4f} in {vel['t_end']:.2f}s")
    rig.recenter(0.0)
    write_csv(LOGS / "07_force_velocity.csv",
              ["force", "dir", "t_s", "axis", "disp"], rows)
    return res


def _frange(a, b, s):
    x = a
    while x <= b + 1e-9:
        yield x
        x += s


def _velocity(trace, base) -> dict:
    """Peak and late-window speed from an (t, axis) trace."""
    if len(trace) < 12:
        return {"v_peak": 0.0, "v_late": 0.0, "travel": 0.0, "t_end": 0.0}
    win = 10  # 40 ms at 250 Hz
    vels = []
    for i in range(win, len(trace)):
        dt = trace[i][0] - trace[i - win][0]
        if dt > 1e-6:
            vels.append((trace[i][1] - trace[i - win][1]) / dt)
    if not vels:
        vels = [0.0]
    late = vels[max(0, len(vels) - len(vels) // 3):] or vels
    return {"v_peak": round(max(vels, key=abs), 4),
            "v_late": round(sum(late) / len(late), 4),
            "travel": round(trace[-1][1] - base, 4),
            "t_end": round(trace[-1][0], 3)}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--part", choices=["breakaway", "hysteresis", "fv", "all"], default="all")
    ap.add_argument("--positions", default="0.0,-0.15,0.15,-0.35,0.35")
    ap.add_argument("--repeats", type=int, default=2)
    ap.add_argument("--fv-start", type=float, default=0.16)
    ap.add_argument("--fv-stop", type=float, default=0.60)
    ap.add_argument("--fv-step", type=float, default=0.06)
    ap.add_argument("--fv-burst", type=float, default=0.7)
    ap.add_argument("--fv-travel", type=float, default=0.30)
    ap.add_argument("--yes", action="store_true")
    args = ap.parse_args()

    banner("07 friction map",
           f"part={args.part} force<=0.6, excursion<=0.45, repeats={args.repeats}",
           args.yes)

    out = {}
    with Rig(force_cap=0.6, excursion_limit=0.45) as rig:
        rig.settle(1.0)
        if args.part in ("breakaway", "all"):
            print("\n--- part: breakaway vs initial angle ---")
            out["breakaway"] = part_breakaway(rig, args)
        if args.part in ("hysteresis", "all"):
            print("\n--- part: static vs kinetic hysteresis ---")
            out["hysteresis"] = part_hysteresis(rig, args)
        if args.part in ("fv", "all"):
            print("\n--- part: force -> velocity ---")
            out["force_velocity"] = part_fv(rig, args)

    path = LOGS / "07_friction_map.json"
    path.write_text(json.dumps(out, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"\n[OK] wrote {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
