"""F7b Day-1 spike: 03 — SPRING + DAMPER combined target-angle following.

The 02 run showed that SPRING alone gives an underdamped 2nd-order response —
the wheel oscillates around the target with huge overshoot. This adds a
concurrent DAMPER effect (velocity-proportional resistance) to check whether
a critically-damped combination is achievable within SDL_Haptic's effect
palette.

Both effects run in parallel and are updated every tick. The spring's center
is animated per profile; the damper's coefficient is fixed.
"""
from __future__ import annotations

import argparse
import csv
import ctypes
import json
import math
import signal
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

MAX_AMPLITUDE_FRAC = 0.45


def _spring(center: int, coeff_frac: float, sat_frac: float = 0.90) -> sdl2.SDL_HapticEffect:
    eff = sdl2.SDL_HapticEffect()
    eff.type = sdl2.SDL_HAPTIC_SPRING
    c = eff.condition
    c.direction.type = sdl2.SDL_HAPTIC_CARTESIAN
    c.direction.dir[0] = 1
    c.length = INF
    coeff = int(max(0, min(1, coeff_frac)) * 32767)
    sat = int(max(0, min(1, sat_frac)) * 32767)
    c.right_coeff[0] = coeff
    c.left_coeff[0]  = coeff
    c.right_sat[0]   = sat
    c.left_sat[0]    = sat
    c.center[0]      = int(max(-32768, min(32767, center)))
    return eff


def _damper(coeff_frac: float, sat_frac: float = 0.90) -> sdl2.SDL_HapticEffect:
    eff = sdl2.SDL_HapticEffect()
    eff.type = sdl2.SDL_HAPTIC_DAMPER
    c = eff.condition
    c.direction.type = sdl2.SDL_HAPTIC_CARTESIAN
    c.direction.dir[0] = 1
    c.length = INF
    coeff = int(max(0, min(1, coeff_frac)) * 32767)
    sat = int(max(0, min(1, sat_frac)) * 32767)
    c.right_coeff[0] = coeff
    c.left_coeff[0]  = coeff
    c.right_sat[0]   = sat
    c.left_sat[0]    = sat
    c.center[0] = 0
    return eff


def _profile_step(t: float) -> float:
    period = 4.0
    phase = (t % period) / period
    if phase < 0.25: return 0.0
    if phase < 0.5:  return +MAX_AMPLITUDE_FRAC
    if phase < 0.75: return 0.0
    return -MAX_AMPLITUDE_FRAC

def _profile_sine(t: float) -> float:
    return MAX_AMPLITUDE_FRAC * math.sin(2.0 * math.pi * 0.5 * t)

def _profile_chirp(t: float, dur: float) -> float:
    f0, f1 = 0.1, 3.0
    k = (f1 - f0) / max(dur, 0.1)
    phase = 2.0 * math.pi * (f0 * t + 0.5 * k * t * t)
    return MAX_AMPLITUDE_FRAC * math.sin(phase)

PROFILES = {
    "step":  {"duration": 12.0, "fn": lambda t, d: _profile_step(t)},
    "sine":  {"duration": 10.0, "fn": lambda t, d: _profile_sine(t)},
    "chirp": {"duration": 15.0, "fn": lambda t, d: _profile_chirp(t, d)},
}


def _safety_ramp(haptic, spring_id: int, joystick, target_end: int,
                 coeff_frac: float, steps: int = 30) -> None:
    sdl2.SDL_JoystickUpdate()
    start = sdl2.SDL_JoystickGetAxis(joystick, STEERING_AXIS)
    for k in range(steps + 1):
        alpha = k / steps
        c = int(start * (1 - alpha) + target_end * alpha)
        eff = _spring(c, coeff_frac)
        sdl2.SDL_HapticUpdateEffect(haptic, spring_id, ctypes.byref(eff))
        time.sleep(0.01)


def _run(name: str, spec: dict, haptic, spring_id: int, damper_id: int,
         joystick, spring_coeff: float, damper_coeff: float) -> dict:
    dur = spec["duration"]
    fn = spec["fn"]
    csv_path = OUT_DIR / f"03_spring_damper_{name}_kp{int(spring_coeff*100)}_kd{int(damper_coeff*100)}.csv"

    _safety_ramp(haptic, spring_id, joystick, 0, spring_coeff, steps=30)

    samples = []
    t0 = time.perf_counter()
    next_tick = t0
    ticks = 0
    while True:
        now = time.perf_counter()
        t = now - t0
        if t >= dur:
            break
        target_frac = fn(t, dur)
        target_i = int(target_frac * AXIS_FULL)
        eff = _spring(target_i, spring_coeff)
        sdl2.SDL_HapticUpdateEffect(haptic, spring_id, ctypes.byref(eff))

        sdl2.SDL_JoystickUpdate()
        actual_i = sdl2.SDL_JoystickGetAxis(joystick, STEERING_AXIS)
        samples.append((t, target_i, actual_i))
        ticks += 1

        next_tick += DT
        s = next_tick - time.perf_counter()
        if s > 0: time.sleep(s)
        else:     next_tick = time.perf_counter()

    _safety_ramp(haptic, spring_id, joystick, 0, spring_coeff, steps=30)

    with csv_path.open("w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["t_s", "target_raw", "actual_raw", "target_frac", "actual_frac", "error_frac"])
        for (t, tgt, act) in samples:
            w.writerow([f"{t:.5f}", tgt, act,
                        f"{tgt / AXIS_FULL:.6f}",
                        f"{act / AXIS_FULL:.6f}",
                        f"{(act - tgt) / AXIS_FULL:.6f}"])

    errs = [(act - tgt) / AXIS_FULL for (_, tgt, act) in samples]
    abs_errs = [abs(e) for e in errs]
    stats = {
        "profile": name, "duration_s": dur,
        "spring_coeff_frac": spring_coeff, "damper_coeff_frac": damper_coeff,
        "samples": len(samples),
        "real_loop_hz": round(ticks / max(1e-6, samples[-1][0]), 2),
        "err_mean_abs_frac": round(sum(abs_errs) / len(abs_errs), 5),
        "err_rms_frac": round(math.sqrt(sum(e * e for e in errs) / len(errs)), 5),
        "err_max_abs_frac": round(max(abs_errs), 5),
        "csv": csv_path.name,
    }
    print(f"[OK] {name} Kp={spring_coeff:.2f} Kd={damper_coeff:.2f}: "
          f"|err|mean={stats['err_mean_abs_frac']} max={stats['err_max_abs_frac']}")
    return stats


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--profile", choices=list(PROFILES.keys()) + ["all"], default="all")
    ap.add_argument("--spring", type=float, default=0.55, help="spring coeff (0..1)")
    ap.add_argument("--damper", type=float, default=0.55, help="damper coeff (0..1)")
    ap.add_argument("--sweep", action="store_true",
                    help="sweep (Kp, Kd) in a small grid instead of a single run")
    ap.add_argument("--yes", action="store_true")
    args = ap.parse_args()

    print("=" * 60)
    print("F7b spike 03 — SPRING + DAMPER combined follow (UNMANNED)")
    print("=" * 60)
    print("!! Keep hands off. Amplitude cap: +/- {:.0%} of full lock.".format(MAX_AMPLITUDE_FRAC))
    if not args.yes:
        for k in (3, 2, 1):
            print(f"   Starting in {k} s...", end="\r"); time.sleep(1)
        print()

    if sdl2.SDL_Init(sdl2.SDL_INIT_JOYSTICK | sdl2.SDL_INIT_HAPTIC) != 0:
        print("[FAIL] SDL_Init"); return 2

    joystick = None
    haptic = None
    spring_id = -1
    damper_id = -1

    def _cleanup(*_):
        try:
            if haptic:
                sdl2.SDL_HapticStopAll(haptic)
                for eid in (spring_id, damper_id):
                    if eid >= 0:
                        sdl2.SDL_HapticDestroyEffect(haptic, eid)
                sdl2.SDL_HapticClose(haptic)
            if joystick:
                sdl2.SDL_JoystickClose(joystick)
        finally:
            sdl2.SDL_Quit()

    signal.signal(signal.SIGINT, lambda *_: (_cleanup(), sys.exit(130)))

    try:
        joystick = sdl2.SDL_JoystickOpen(0)
        if not joystick: print("[FAIL] JoystickOpen"); return 3
        haptic = sdl2.SDL_HapticOpenFromJoystick(joystick)
        if not haptic: print("[FAIL] HapticOpen"); return 4

        eff = _spring(0, args.spring)
        spring_id = sdl2.SDL_HapticNewEffect(haptic, ctypes.byref(eff))
        if spring_id < 0:
            print(f"[FAIL] spring: {sdl2.SDL_GetError().decode(errors='ignore')}"); return 5
        sdl2.SDL_HapticRunEffect(haptic, spring_id, 1)

        eff = _damper(args.damper)
        damper_id = sdl2.SDL_HapticNewEffect(haptic, ctypes.byref(eff))
        if damper_id < 0:
            print(f"[FAIL] damper: {sdl2.SDL_GetError().decode(errors='ignore')}"); return 6
        sdl2.SDL_HapticRunEffect(haptic, damper_id, 1)

        _safety_ramp(haptic, spring_id, joystick, 0, args.spring, steps=25)

        which = list(PROFILES.keys()) if args.profile == "all" else [args.profile]

        if args.sweep:
            # Small grid for gain tuning; run sine only (most sensitive to damping).
            grid = [(0.35, 0.35), (0.35, 0.65), (0.55, 0.55), (0.55, 0.85), (0.75, 0.85)]
            results = []
            for (kp, kd) in grid:
                # rebuild damper effect coefficient
                eff = _damper(kd)
                sdl2.SDL_HapticUpdateEffect(haptic, damper_id, ctypes.byref(eff))
                results.append(_run("sine", PROFILES["sine"], haptic, spring_id, damper_id,
                                    joystick, kp, kd))
            (OUT_DIR / "03_sweep_summary.json").write_text(
                json.dumps({"grid": results}, indent=2, ensure_ascii=False), encoding="utf-8")
            print(f"[OK] wrote {OUT_DIR / '03_sweep_summary.json'}")
        else:
            results = []
            for name in which:
                results.append(_run(name, PROFILES[name], haptic, spring_id, damper_id,
                                    joystick, args.spring, args.damper))
            (OUT_DIR / f"03_spring_damper_summary_kp{int(args.spring*100)}_kd{int(args.damper*100)}.json").write_text(
                json.dumps({"spring": args.spring, "damper": args.damper, "profiles": results},
                           indent=2, ensure_ascii=False), encoding="utf-8")

        return 0
    finally:
        _cleanup()


if __name__ == "__main__":
    sys.exit(main())
