"""F7b Day-1 spike: 02 — SPRING target-angle following (unmanned).

Purpose
-------
Answer question 1: can we drive the wheel to a moving target by mutating
SDL_HAPTIC_SPRING's `center` field, and what are the following error / lag /
oscillation / practical update rate?

Method
------
- Open G29 joystick + haptic.
- Create ONE SPRING effect (SDL_HAPTIC_INFINITY length).
- Every tick, update `center` to the current target (axis raw units).
- Read the axis position (SDL_JoystickGetAxis) and log target/actual/error.
- Run three profiles back-to-back: step, sine, chirp. Return wheel to 0.

Safety
------
- **No hands on the wheel.** Ensure clearance around the wheel before starting.
- Amplitudes clamped to +/- 45% of lock. Start with moderate stiffness.
- On any exception or Ctrl+C: stop all effects and close haptic in `finally`.

Outputs
-------
- logs/02_spring_follow_<profile>.csv per profile
- logs/02_spring_follow_summary.json (per-profile stats)
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

# Axis units: Sint16 raw. G29 lock ~= +/-32767 across +/- 450 deg.
AXIS_FULL = 32767
STEERING_AXIS = 0  # G29 wheel = axis 0
LOOP_HZ = 250.0
DT = 1.0 / LOOP_HZ

# Safety caps
MAX_AMPLITUDE_FRAC = 0.45  # of full lock
COEFF_FRAC = 0.55          # spring coefficient (0..1)
SAT_FRAC = 0.90            # saturation (0..1)


def _mk_spring(coeff_frac: float, sat_frac: float, center: int) -> sdl2.SDL_HapticEffect:
    """Build a SPRING effect for G29.

    NOTE: On G29 (single-axis wheel) via SDL2/DirectInput, SPRING creation
    fails with 'Unable to create effect' unless `condition.direction.type` is
    set to CARTESIAN with dir[0]=1. Only fill index [0] (the steering axis) —
    filling [1]/[2] on a 1-axis device also causes creation failure. This
    behavior differs from CONSTANT effects and is not documented in SDL_haptic
    guide; the existing C++ SDLFFBSink omits the direction for SPRING and
    thus silently falls back to constant-emulation on G29 (LOG_WARN "Spring
    effect unsupported"). Fix that when porting.
    """
    eff = sdl2.SDL_HapticEffect()
    eff.type = sdl2.SDL_HAPTIC_SPRING
    cond = eff.condition
    cond.direction.type = sdl2.SDL_HAPTIC_CARTESIAN
    cond.direction.dir[0] = 1
    cond.length = 0xFFFFFFFF  # SDL_HAPTIC_INFINITY
    cond.delay = 0
    coeff = int(max(0, min(1, coeff_frac)) * 32767)
    sat = int(max(0, min(1, sat_frac)) * 32767)
    center_i = int(max(-32768, min(32767, center)))
    cond.right_coeff[0] = coeff
    cond.left_coeff[0]  = coeff
    cond.right_sat[0]   = sat
    cond.left_sat[0]    = sat
    cond.deadband[0]    = 0
    cond.center[0]      = center_i
    return eff


def _update_center(haptic, effect_id: int, center: int, coeff_frac: float, sat_frac: float) -> None:
    eff = _mk_spring(coeff_frac, sat_frac, center)
    sdl2.SDL_HapticUpdateEffect(haptic, effect_id, ctypes.byref(eff))


def _profile_step(t: float) -> float:
    period = 4.0
    phase = (t % period) / period
    if phase < 0.25:
        return 0.0
    if phase < 0.5:
        return +MAX_AMPLITUDE_FRAC
    if phase < 0.75:
        return 0.0
    return -MAX_AMPLITUDE_FRAC


def _profile_sine(t: float) -> float:
    return MAX_AMPLITUDE_FRAC * math.sin(2.0 * math.pi * 0.5 * t)


def _profile_chirp(t: float, duration: float) -> float:
    # Linear chirp 0.1 -> 3.0 Hz
    f0, f1 = 0.1, 3.0
    k = (f1 - f0) / max(duration, 0.1)
    phase = 2.0 * math.pi * (f0 * t + 0.5 * k * t * t)
    return MAX_AMPLITUDE_FRAC * math.sin(phase)


PROFILES = {
    "step":  {"duration": 12.0, "fn": lambda t, d: _profile_step(t)},
    "sine":  {"duration": 10.0, "fn": lambda t, d: _profile_sine(t)},
    "chirp": {"duration": 15.0, "fn": lambda t, d: _profile_chirp(t, d)},
}


def _safety_ramp(haptic, effect_id: int, joystick, target_end_center: int,
                 coeff_frac: float, sat_frac: float, steps: int = 50) -> None:
    """Slowly ramp the spring center from current axis position to target_end_center."""
    sdl2.SDL_JoystickUpdate()
    start = sdl2.SDL_JoystickGetAxis(joystick, STEERING_AXIS)
    for k in range(steps + 1):
        alpha = k / steps
        c = int(start * (1 - alpha) + target_end_center * alpha)
        _update_center(haptic, effect_id, c, coeff_frac, sat_frac)
        time.sleep(0.01)


def _run_profile(name: str, spec: dict, haptic, effect_id: int, joystick) -> dict:
    dur = spec["duration"]
    fn = spec["fn"]
    csv_path = OUT_DIR / f"02_spring_follow_{name}.csv"
    stats = {
        "profile": name, "duration_s": dur, "loop_hz_target": LOOP_HZ,
        "coeff_frac": COEFF_FRAC, "sat_frac": SAT_FRAC,
        "max_amplitude_frac": MAX_AMPLITUDE_FRAC,
    }

    # Ramp center to 0 before starting, in case a previous profile left residual load.
    _safety_ramp(haptic, effect_id, joystick, 0, COEFF_FRAC, SAT_FRAC, steps=30)

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
        _update_center(haptic, effect_id, target_i, COEFF_FRAC, SAT_FRAC)

        sdl2.SDL_JoystickUpdate()
        actual_i = sdl2.SDL_JoystickGetAxis(joystick, STEERING_AXIS)
        samples.append((t, target_i, actual_i))
        ticks += 1

        next_tick += DT
        sleep_for = next_tick - time.perf_counter()
        if sleep_for > 0:
            time.sleep(sleep_for)
        else:
            # Overrun: drop-in; do not accumulate lag.
            next_tick = time.perf_counter()

    # Ramp back to 0 to release load gently.
    _safety_ramp(haptic, effect_id, joystick, 0, COEFF_FRAC, SAT_FRAC, steps=30)

    with csv_path.open("w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["t_s", "target_raw", "actual_raw", "target_frac", "actual_frac", "error_frac"])
        for (t, tgt, act) in samples:
            w.writerow([f"{t:.5f}", tgt, act,
                        f"{tgt / AXIS_FULL:.6f}",
                        f"{act / AXIS_FULL:.6f}",
                        f"{(act - tgt) / AXIS_FULL:.6f}"])

    # Basic stats
    if samples:
        errs = [(act - tgt) / AXIS_FULL for (_, tgt, act) in samples]
        abs_errs = [abs(e) for e in errs]
        max_abs = max(abs_errs)
        mean_abs = sum(abs_errs) / len(abs_errs)
        rms = math.sqrt(sum(e * e for e in errs) / len(errs))
        real_hz = ticks / max(1e-6, samples[-1][0])
        stats.update({
            "samples": len(samples),
            "real_loop_hz": round(real_hz, 2),
            "err_mean_abs_frac": round(mean_abs, 5),
            "err_rms_frac": round(rms, 5),
            "err_max_abs_frac": round(max_abs, 5),
            "csv": str(csv_path.name),
        })

    print(f"[OK] {name}: samples={stats.get('samples')} hz~{stats.get('real_loop_hz')} "
          f"|err|mean={stats.get('err_mean_abs_frac')} max={stats.get('err_max_abs_frac')}")
    return stats


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--profile", choices=list(PROFILES.keys()) + ["all"], default="all")
    ap.add_argument("--yes", action="store_true", help="skip the safety pause")
    args = ap.parse_args()

    print("=" * 60)
    print("F7b spike 02 — SPRING target-angle following (UNMANNED)")
    print("=" * 60)
    print("!! WARNING !! The wheel WILL move on its own.")
    print("   - Keep hands off. Clear ~50 cm around the wheel.")
    print("   - Amplitude cap: +/- {:.0%} of full lock.".format(MAX_AMPLITUDE_FRAC))
    print("   - Ctrl+C to abort at any time.")
    if not args.yes:
        for k in (3, 2, 1):
            print(f"   Starting in {k} s...", end="\r")
            time.sleep(1)
        print()

    if sdl2.SDL_Init(sdl2.SDL_INIT_JOYSTICK | sdl2.SDL_INIT_HAPTIC) != 0:
        print("[FAIL] SDL_Init")
        return 2

    joystick = None
    haptic = None
    effect_id = -1
    stats_all = []

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

    # Also register signal for Ctrl+C so the finally still executes cleanly.
    signal.signal(signal.SIGINT, lambda *_: (_cleanup(), sys.exit(130)))

    try:
        joystick = sdl2.SDL_JoystickOpen(0)
        if not joystick:
            print("[FAIL] JoystickOpen(0)")
            return 3
        haptic = sdl2.SDL_HapticOpenFromJoystick(joystick)
        if not haptic:
            print("[FAIL] HapticOpenFromJoystick")
            return 4

        eff = _mk_spring(COEFF_FRAC, SAT_FRAC, 0)
        effect_id = sdl2.SDL_HapticNewEffect(haptic, ctypes.byref(eff))
        if effect_id < 0:
            print(f"[FAIL] HapticNewEffect (spring): {sdl2.SDL_GetError().decode(errors='ignore')}")
            return 5

        # Global gain (if supported) — keep at default; the wheel's own gain in
        # LGS may already scale forces.
        sdl2.SDL_HapticRunEffect(haptic, effect_id, 1)

        # Warm-up: hold center at 0 for 500 ms so the wheel settles.
        _safety_ramp(haptic, effect_id, joystick, 0, COEFF_FRAC, SAT_FRAC, steps=25)

        which = list(PROFILES.keys()) if args.profile == "all" else [args.profile]
        for name in which:
            stats_all.append(_run_profile(name, PROFILES[name], haptic, effect_id, joystick))

        (OUT_DIR / "02_spring_follow_summary.json").write_text(
            json.dumps({"profiles": stats_all}, indent=2, ensure_ascii=False), encoding="utf-8")
        print(f"[OK] wrote {OUT_DIR / '02_spring_follow_summary.json'}")
        return 0
    finally:
        _cleanup()


if __name__ == "__main__":
    sys.exit(main())
