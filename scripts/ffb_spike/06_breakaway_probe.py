"""F7b Day-2 spike: 06 — Breakaway (static friction) probe + small-signal servo.

Motivation (F7b real-rig bug): the AD-driven target-track servo commands a
non-zero CONSTANT force (telemetry `tt=-0.249`) but the physical G29 wheel does
not move at all. Production telemetry showed the other FFB components (SAT
predictive/reactive + friction) cancel roughly half of it, leaving a combined
`total=-0.125`. Question: is 0.125 simply below the wheel's breakaway
(static-friction) threshold?

Modes
-----
`breakaway` : staircase a CONSTANT force upward, both directions, and record
              the first level at which the axis actually moves. Then step the
              force back down while moving to find the holding (kinetic) level.
`servo`     : PID step response to SMALL targets (production-scale, |err| ~0.03
              .. 0.30) for a given (kp, kd, max_force). Optional `--opposing`
              adds a constant counter-force to emulate the production SAT+
              friction cancellation, i.e. reproduces the real composite.

All runs are UNMANNED (hands off the wheel). Forces start small and ramp.
"""
from __future__ import annotations

import argparse
import csv
import ctypes
import json
import math
import signal
import statistics
import sys
import time
from pathlib import Path

import sdl2

OUT_DIR = Path(__file__).with_suffix("").parent / "logs"
OUT_DIR.mkdir(exist_ok=True)

AXIS_FULL = 32767
STEERING_AXIS = 0
LOOP_HZ = 250.0
DT = 1.0 / LOOP_HZ
INF = 0xFFFFFFFF

# Safety: never let the probe drive the wheel further than this from rest.
EXCURSION_LIMIT = 0.45


def _constant(level_frac: float) -> sdl2.SDL_HapticEffect:
    eff = sdl2.SDL_HapticEffect()
    eff.type = sdl2.SDL_HAPTIC_CONSTANT
    c = eff.constant
    c.direction.type = sdl2.SDL_HAPTIC_CARTESIAN
    c.direction.dir[0] = 1
    c.length = INF
    c.level = int(max(-1.0, min(1.0, level_frac)) * 32767)
    return eff


class Rig:
    def __init__(self, gain: int = 100):
        self.joystick = None
        self.haptic = None
        self.effect_id = -1
        self.gain = gain

    def open(self) -> None:
        if sdl2.SDL_Init(sdl2.SDL_INIT_JOYSTICK | sdl2.SDL_INIT_HAPTIC) != 0:
            raise RuntimeError("SDL_Init failed")
        self.joystick = sdl2.SDL_JoystickOpen(0)
        if not self.joystick:
            raise RuntimeError("SDL_JoystickOpen(0) failed")
        self.haptic = sdl2.SDL_HapticOpenFromJoystick(self.joystick)
        if not self.haptic:
            raise RuntimeError("SDL_HapticOpenFromJoystick failed")
        rc = sdl2.SDL_HapticSetGain(self.haptic, self.gain)
        print(f"[INFO] SDL_HapticSetGain({self.gain}) rc={rc}")
        eff = _constant(0.0)
        self.effect_id = sdl2.SDL_HapticNewEffect(self.haptic, ctypes.byref(eff))
        if self.effect_id < 0:
            raise RuntimeError(f"NewEffect(CONSTANT): {sdl2.SDL_GetError().decode(errors='ignore')}")
        sdl2.SDL_HapticRunEffect(self.haptic, self.effect_id, 1)

    def set_force(self, u: float) -> None:
        eff = _constant(u)
        sdl2.SDL_HapticUpdateEffect(self.haptic, self.effect_id, ctypes.byref(eff))

    def axis(self) -> float:
        sdl2.SDL_JoystickUpdate()
        return sdl2.SDL_JoystickGetAxis(self.joystick, STEERING_AXIS) / AXIS_FULL

    def settle(self, seconds: float = 1.0) -> float:
        """Zero force, wait, return the mean resting axis position."""
        self.set_force(0.0)
        vals = []
        t0 = time.perf_counter()
        while time.perf_counter() - t0 < seconds:
            vals.append(self.axis())
            time.sleep(DT)
        tail = vals[len(vals) // 2:]
        return statistics.fmean(tail)

    def close(self) -> None:
        try:
            if self.haptic:
                self.set_force(0.0)
                time.sleep(0.05)
                sdl2.SDL_HapticStopAll(self.haptic)
                if self.effect_id >= 0:
                    sdl2.SDL_HapticDestroyEffect(self.haptic, self.effect_id)
                sdl2.SDL_HapticClose(self.haptic)
            if self.joystick:
                sdl2.SDL_JoystickClose(self.joystick)
        finally:
            sdl2.SDL_Quit()

    def servo_to(self, target: float, kp: float = 5.0, kd: float = 0.35,
                 max_force: float = 0.75, timeout: float = 4.0,
                 tol: float = 0.02) -> float:
        """PID the wheel back toward `target` (used to recentre between trials)."""
        prev_err = 0.0
        t0 = time.perf_counter()
        while time.perf_counter() - t0 < timeout:
            a = self.axis()
            err = target - a
            if abs(err) < tol:
                break
            derr = (err - prev_err) / DT
            prev_err = err
            u = max(-max_force, min(max_force, -(kp * err + kd * derr)))
            self.set_force(u)
            time.sleep(DT)
        self.set_force(0.0)
        time.sleep(0.2)
        return self.axis()


# --------------------------------------------------------------------------
# Mode: breakaway
# --------------------------------------------------------------------------

def run_breakaway(rig: Rig, args) -> dict:
    rows = []
    result = {"steps": [], "directions": {}}

    baseline = rig.settle(1.5)
    print(f"[INFO] rest axis baseline = {baseline:+.4f}")
    result["rest_axis"] = round(baseline, 5)

    for sign, label in ((+1.0, "force_pos_axis_left"), (-1.0, "force_neg_axis_right")):
        print(f"\n=== staircase {label} ===")
        rig.servo_to(baseline)
        base = rig.settle(1.0)

        moved_at = None
        first_creep_at = None
        per_step = []
        f = args.start
        while f <= args.stop + 1e-9:
            rig.set_force(sign * f)
            t0 = time.perf_counter()
            disp_max = 0.0
            while time.perf_counter() - t0 < args.hold:
                a = rig.axis()
                d = abs(a - base)
                disp_max = max(disp_max, d)
                rows.append((label, f, time.perf_counter() - t0, a, a - base, sign * f))
                if d > EXCURSION_LIMIT:
                    break
                time.sleep(DT)
            end = rig.axis()
            disp_end = abs(end - base)
            per_step.append({"force": round(f, 4),
                             "disp_max": round(disp_max, 5),
                             "disp_end": round(disp_end, 5)})
            print(f"  f={f:.3f}  disp_max={disp_max:.4f}  disp_end={disp_end:.4f}")
            if first_creep_at is None and disp_max > args.creep_thresh:
                first_creep_at = f
            if disp_max > args.move_thresh:
                moved_at = f
                break
            f = round(f + args.step, 6)

        rig.set_force(0.0)
        hold_force = None
        if moved_at is not None:
            print(f"  -> BREAKAWAY at |force| = {moved_at:.3f} "
                  f"(first creep >{args.creep_thresh} at {first_creep_at})")
            # kinetic / holding level: get it moving again, then ramp down.
            rig.servo_to(baseline)
            base2 = rig.settle(0.8)
            g = min(args.stop, moved_at + 2 * args.step)
            rig.set_force(sign * g)
            time.sleep(0.30)  # ensure it is in motion
            prev = rig.axis()
            while g > 0.0:
                g = round(g - args.step, 6)
                rig.set_force(sign * g)
                t0 = time.perf_counter()
                start_a = rig.axis()
                while time.perf_counter() - t0 < 0.25:
                    a = rig.axis()
                    rows.append((label + "_down", g, time.perf_counter() - t0,
                                 a, a - base2, sign * g))
                    if abs(a - base2) > EXCURSION_LIMIT:
                        break
                    time.sleep(DT)
                end_a = rig.axis()
                moved = abs(end_a - start_a)
                print(f"  down f={g:.3f}  moved_in_250ms={moved:.4f}")
                if abs(end_a - base2) > EXCURSION_LIMIT:
                    print("  (excursion limit reached — stopping ramp-down)")
                    break
                if moved < args.kinetic_thresh:
                    hold_force = g
                    break
                prev = end_a
            rig.set_force(0.0)
            if hold_force is not None:
                print(f"  -> HOLDING (kinetic) level ≈ {hold_force:.3f}")
        else:
            print(f"  -> NO MOVEMENT up to |force| = {args.stop:.3f}")

        rig.servo_to(baseline)
        rig.settle(0.5)

        result["directions"][label] = {
            "breakaway_force": moved_at,
            "first_creep_force": first_creep_at,
            "holding_force": hold_force,
            "steps": per_step,
        }

    csv_path = OUT_DIR / "06_breakaway.csv"
    with csv_path.open("w", newline="", encoding="utf-8") as fh:
        w = csv.writer(fh)
        w.writerow(["phase", "force_mag", "t_in_step", "axis", "disp", "signed_force"])
        for r in rows:
            w.writerow([r[0], f"{r[1]:.4f}", f"{r[2]:.4f}", f"{r[3]:.6f}",
                        f"{r[4]:+.6f}", f"{r[5]:+.4f}"])
    result["csv"] = csv_path.name
    return result


# --------------------------------------------------------------------------
# Mode: servo (small-signal step, optional opposing force)
# --------------------------------------------------------------------------

def run_servo(rig: Rig, args) -> dict:
    baseline = rig.settle(1.2)
    print(f"[INFO] rest axis baseline = {baseline:+.4f}")
    targets = [float(x) for x in args.targets.split(",")]
    out = {"rest_axis": round(baseline, 5), "kp": args.kp, "kd": args.kd,
           "max_force": args.max_force, "opposing": args.opposing,
           "fstat": args.fstat, "ff_eps": args.ff_eps, "runs": []}
    rows = []

    for tgt_rel in targets:
        rig.servo_to(baseline)
        base = rig.settle(0.8)
        target = base + tgt_rel
        print(f"\n=== step target = {tgt_rel:+.3f} (abs {target:+.3f}) "
              f"kp={args.kp} max_force={args.max_force} opposing={args.opposing} ===")

        prev_err = 0.0
        primed = False
        t0 = time.perf_counter()
        first_move_t = None
        reach_t = None
        samples = []
        while True:
            t = time.perf_counter() - t0
            if t >= args.duration:
                break
            a = rig.axis()
            err = target - a
            derr = (err - prev_err) / DT if primed else 0.0
            prev_err = err
            primed = True
            # Coulomb friction feed-forward: the measured static/kinetic friction
            # of the G29 is a fixed torque offset, so compensate it directly
            # instead of asking the P term to generate it out of position error.
            ff = -args.fstat * math.tanh(err / max(args.ff_eps, 1e-6))
            tt = -(args.kp * err + args.kd * derr) + ff
            tt = max(-args.max_force, min(args.max_force, tt))
            # `opposing` emulates the production SAT+friction counter-force:
            # it always fights the servo, scaled by |tt| ratio from the field log.
            opp = -math.copysign(args.opposing, tt) if args.opposing > 0 else 0.0
            u = max(-1.0, min(1.0, tt + opp))
            if abs(a - base) > EXCURSION_LIMIT:
                u = 0.0
            rig.set_force(u)
            samples.append((t, target, a, a - base, tt, opp, u))
            rows.append((f"{tgt_rel:+.3f}", t, target, a, a - base, tt, opp, u))
            if first_move_t is None and abs(a - base) > 0.01:
                first_move_t = t
            if reach_t is None and abs(target - a) < 0.02:
                reach_t = t
            time.sleep(DT)
        rig.set_force(0.0)

        final = rig.axis()
        tail = [s for s in samples if s[0] > args.duration * 0.6]
        tail_err = statistics.fmean([abs(s[1] - s[2]) for s in tail]) if tail else float("nan")
        peak_disp = max(abs(s[3]) for s in samples)
        run = {"target_rel": tgt_rel,
               "first_move_s": None if first_move_t is None else round(first_move_t, 3),
               "reach_s": None if reach_t is None else round(reach_t, 3),
               "final_disp": round(final - base, 5),
               "peak_disp": round(peak_disp, 5),
               "tail_abs_err": round(tail_err, 5),
               "u_max": round(max(abs(s[6]) for s in samples), 4),
               # chatter probes: how much does the command / the axis wobble once settled?
               "tail_u_std": round(statistics.pstdev([s[6] for s in tail]), 5) if len(tail) > 1 else 0.0,
               "tail_axis_std": round(statistics.pstdev([s[2] for s in tail]), 5) if len(tail) > 1 else 0.0,
               "tail_axis_ptp": round(max(s[2] for s in tail) - min(s[2] for s in tail), 5) if tail else 0.0}
        out["runs"].append(run)
        print(f"  first_move={run['first_move_s']}s reach(<0.02)={run['reach_s']}s "
              f"final_disp={run['final_disp']:+.4f} peak={run['peak_disp']:.4f} "
              f"tail_err={run['tail_abs_err']:.4f} |u|max={run['u_max']}")

    rig.servo_to(baseline)
    tag = f"kp{int(args.kp*100)}_mf{int(args.max_force*100)}_opp{int(args.opposing*1000)}"
    csv_path = OUT_DIR / f"06_servo_{tag}.csv"
    with csv_path.open("w", newline="", encoding="utf-8") as fh:
        w = csv.writer(fh)
        w.writerow(["target_rel", "t_s", "target_abs", "axis", "disp", "tt", "opposing", "u"])
        for r in rows:
            w.writerow([r[0], f"{r[1]:.4f}", f"{r[2]:.5f}", f"{r[3]:.6f}",
                        f"{r[4]:+.6f}", f"{r[5]:+.4f}", f"{r[6]:+.4f}", f"{r[7]:+.4f}"])
    out["csv"] = csv_path.name
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("mode", choices=["breakaway", "servo"])
    ap.add_argument("--yes", action="store_true")
    ap.add_argument("--gain", type=int, default=100)
    # breakaway
    ap.add_argument("--start", type=float, default=0.02)
    ap.add_argument("--stop", type=float, default=0.60)
    ap.add_argument("--step", type=float, default=0.02)
    ap.add_argument("--hold", type=float, default=0.45)
    ap.add_argument("--move-thresh", type=float, default=0.020)
    ap.add_argument("--creep-thresh", type=float, default=0.004)
    ap.add_argument("--kinetic-thresh", type=float, default=0.004)
    # servo
    ap.add_argument("--kp", type=float, default=4.0)
    ap.add_argument("--kd", type=float, default=0.35)
    ap.add_argument("--max_force", type=float, default=0.6)
    ap.add_argument("--opposing", type=float, default=0.0)
    ap.add_argument("--fstat", type=float, default=0.0,
                    help="Coulomb friction feed-forward magnitude (0 = off)")
    ap.add_argument("--ff-eps", type=float, default=0.01,
                    help="error scale over which the friction feed-forward ramps in")
    ap.add_argument("--targets", type=str, default="0.05,0.15,0.30")
    ap.add_argument("--duration", type=float, default=3.0)
    ap.add_argument("--out", type=str, default="")
    args = ap.parse_args()

    print("=" * 64)
    print(f"F7b spike 06 — {args.mode} (UNMANNED — keep hands off the wheel)")
    print("=" * 64)
    if not args.yes:
        for k in (3, 2, 1):
            print(f"   starting in {k} s...", end="\r")
            time.sleep(1)
        print()

    rig = Rig(args.gain)
    signal.signal(signal.SIGINT, lambda *_: (rig.close(), sys.exit(130)))
    try:
        rig.open()
        res = run_breakaway(rig, args) if args.mode == "breakaway" else run_servo(rig, args)
    finally:
        rig.close()

    name = args.out or f"06_{args.mode}_summary.json"
    (OUT_DIR / name).write_text(json.dumps(res, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"\n[OK] wrote {OUT_DIR / name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
