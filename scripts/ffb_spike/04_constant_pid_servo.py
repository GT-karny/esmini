"""F7b Day-1 spike: 04 — Constant-force PID servo (SPRING alternative).

Bypasses SDL_HAPTIC_SPRING's condition-effect coefficient path — commands raw
CONSTANT force torque via a Python PID loop that reads the axis and computes
the FFB level each tick. This isolates whether the poor SPRING-follow numbers
in 02/03 come from (a) the SPRING coefficient path being gain-limited by the
Logitech driver, or (b) the wheel's actual dynamics.

Same three profiles, same amplitude cap.
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


def _constant(level_frac: float) -> sdl2.SDL_HapticEffect:
    eff = sdl2.SDL_HapticEffect()
    eff.type = sdl2.SDL_HAPTIC_CONSTANT
    c = eff.constant
    c.direction.type = sdl2.SDL_HAPTIC_CARTESIAN
    c.direction.dir[0] = 1
    c.length = INF
    c.level = int(max(-1.0, min(1.0, level_frac)) * 32767)
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


def _safety_ramp_force(haptic, effect_id: int, joystick, steps: int = 20) -> None:
    """Ramp force to zero smoothly to release load."""
    for _ in range(steps):
        eff = _constant(0.0)
        sdl2.SDL_HapticUpdateEffect(haptic, effect_id, ctypes.byref(eff))
        time.sleep(0.01)


def _run(name: str, spec: dict, haptic, effect_id: int, joystick,
         kp: float, kd: float, ki: float, max_force: float) -> dict:
    dur = spec["duration"]
    fn = spec["fn"]
    csv_path = OUT_DIR / f"04_pid_{name}_kp{int(kp*100)}_kd{int(kd*100)}.csv"

    _safety_ramp_force(haptic, effect_id, joystick, steps=15)

    samples = []
    t0 = time.perf_counter()
    next_tick = t0
    ticks = 0

    prev_err = 0.0
    integ = 0.0

    while True:
        now = time.perf_counter()
        t = now - t0
        if t >= dur: break

        target_frac = fn(t, dur)

        sdl2.SDL_JoystickUpdate()
        actual_i = sdl2.SDL_JoystickGetAxis(joystick, STEERING_AXIS)
        actual_frac = actual_i / AXIS_FULL

        err = target_frac - actual_frac
        derr = (err - prev_err) / DT
        integ = max(-0.5, min(0.5, integ + err * DT))
        prev_err = err

        # G29 sign convention: positive CONSTANT level pushes wheel to negative
        # axis (LEFT). Axis positive = wheel turned RIGHT. So torque to reach a
        # positive target must be NEGATIVE force. Invert PID output.
        u = -(kp * err + kd * derr + ki * integ)
        u = max(-max_force, min(max_force, u))

        # Safety: when close to hard stops (|actual| > 0.85), attenuate any
        # force that would push further outward. Prevents driving into stops.
        if actual_frac > 0.85 and u < 0:  # pushing further right? (u<0 = right)
            u *= max(0.0, (1.0 - actual_frac) / 0.15)
        elif actual_frac < -0.85 and u > 0:  # pushing further left? (u>0 = left)
            u *= max(0.0, (1.0 + actual_frac) / 0.15)

        eff = _constant(u)
        sdl2.SDL_HapticUpdateEffect(haptic, effect_id, ctypes.byref(eff))

        samples.append((t, int(target_frac * AXIS_FULL), actual_i, u))
        ticks += 1

        next_tick += DT
        s = next_tick - time.perf_counter()
        if s > 0: time.sleep(s)
        else:     next_tick = time.perf_counter()

    _safety_ramp_force(haptic, effect_id, joystick, steps=15)

    with csv_path.open("w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["t_s", "target_raw", "actual_raw", "target_frac", "actual_frac",
                    "error_frac", "u_force"])
        for (t, tgt, act, u) in samples:
            w.writerow([f"{t:.5f}", tgt, act,
                        f"{tgt / AXIS_FULL:.6f}",
                        f"{act / AXIS_FULL:.6f}",
                        f"{(act - tgt) / AXIS_FULL:.6f}",
                        f"{u:.4f}"])

    errs = [(act - tgt) / AXIS_FULL for (_, tgt, act, _) in samples]
    abs_errs = [abs(e) for e in errs]
    u_max = max(abs(u) for (_, _, _, u) in samples)
    stats = {
        "profile": name, "duration_s": dur, "kp": kp, "kd": kd, "ki": ki,
        "max_force_cap": max_force,
        "samples": len(samples),
        "real_loop_hz": round(ticks / max(1e-6, samples[-1][0]), 2),
        "err_mean_abs_frac": round(sum(abs_errs) / len(abs_errs), 5),
        "err_rms_frac": round(math.sqrt(sum(e * e for e in errs) / len(errs)), 5),
        "err_max_abs_frac": round(max(abs_errs), 5),
        "u_max_abs": round(u_max, 4),
        "csv": csv_path.name,
    }
    print(f"[OK] {name} Kp={kp:.2f} Kd={kd:.2f} Ki={ki:.2f}: "
          f"|err|mean={stats['err_mean_abs_frac']} max={stats['err_max_abs_frac']} "
          f"|u|max={stats['u_max_abs']}")
    return stats


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--profile", choices=list(PROFILES.keys()) + ["all"], default="all")
    ap.add_argument("--kp", type=float, default=6.0)
    ap.add_argument("--kd", type=float, default=0.5)
    ap.add_argument("--ki", type=float, default=0.0)
    ap.add_argument("--max_force", type=float, default=0.85)
    ap.add_argument("--gain", type=int, default=100, help="SDL_HapticSetGain 0..100")
    ap.add_argument("--yes", action="store_true")
    args = ap.parse_args()

    print("=" * 60)
    print("F7b spike 04 — Constant-force PID servo (UNMANNED)")
    print("=" * 60)
    print(f"!! Keep hands off. Kp={args.kp} Kd={args.kd} max_force={args.max_force}")
    if not args.yes:
        for k in (3, 2, 1):
            print(f"   Starting in {k} s...", end="\r"); time.sleep(1)
        print()

    if sdl2.SDL_Init(sdl2.SDL_INIT_JOYSTICK | sdl2.SDL_INIT_HAPTIC) != 0:
        print("[FAIL] SDL_Init"); return 2

    joystick = None
    haptic = None
    effect_id = -1

    def _cleanup(*_):
        try:
            if haptic:
                sdl2.SDL_HapticStopAll(haptic)
                if effect_id >= 0:
                    sdl2.SDL_HapticDestroyEffect(haptic, effect_id)
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

        # Try to set global gain (some drivers cap by default). Ignore rc.
        rc = sdl2.SDL_HapticSetGain(haptic, args.gain)
        print(f"[INFO] SDL_HapticSetGain({args.gain}) rc={rc}")

        eff = _constant(0.0)
        effect_id = sdl2.SDL_HapticNewEffect(haptic, ctypes.byref(eff))
        if effect_id < 0:
            print(f"[FAIL] constant: {sdl2.SDL_GetError().decode(errors='ignore')}"); return 5
        sdl2.SDL_HapticRunEffect(haptic, effect_id, 1)

        which = list(PROFILES.keys()) if args.profile == "all" else [args.profile]
        results = []
        for name in which:
            results.append(_run(name, PROFILES[name], haptic, effect_id, joystick,
                                args.kp, args.kd, args.ki, args.max_force))

        (OUT_DIR / f"04_pid_summary_kp{int(args.kp*100)}_kd{int(args.kd*100)}.json").write_text(
            json.dumps({"kp": args.kp, "kd": args.kd, "ki": args.ki,
                        "max_force": args.max_force, "gain": args.gain,
                        "profiles": results}, indent=2, ensure_ascii=False),
            encoding="utf-8")
        return 0
    finally:
        _cleanup()


if __name__ == "__main__":
    sys.exit(main())
