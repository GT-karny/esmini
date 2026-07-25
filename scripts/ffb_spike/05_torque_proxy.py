"""F7b Day-1 spike: 05 — Torque proxy via position deviation (USER IN THE LOOP).

Purpose
-------
Answer question 2: with FFB holding a fixed target angle, how does the wheel
position deviate from the target when the driver applies graded torque? Is
the deviation distinguishable enough to serve as an intervention signal
without false positives from a "resting hand"?

Method
------
- Two backend modes: --mode spring OR --mode pid.
  - spring: SPRING center held at 0 with the strongest coeff the driver accepts.
  - pid:    Constant-force PID servo holding target=0 (matches script 04 backend).
- For each phase (announced in the console), record axis position at 250 Hz
  for a fixed window. Phases:
     idle_1   (no touch)            — noise floor
     rest     (finger resting)      — "hand on wheel, no intent"
     light    (gentle steady push)  — "casual correction"
     medium   (moderate push)       — "assertive correction"
     firm     (firm hold off-target)— "clear override intent"
     idle_2   (no touch)            — post-run check
- Between phases, a 3-2-1 countdown prompts the driver.
- All CSVs saved; a JSON summary reports per-phase mean/std/max deviation and
  suggested threshold candidates.

Safety
------
- Torque here is being applied BY the user, but the FFB is also active. If FFB
  fights hard (e.g., strong spring), the wheel could snap back when hand
  releases. Amplitudes are bounded to center-hold — no target motion.
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

# Phase specs: (label, description, seconds)
PHASES = [
    ("idle_1", "手を完全に離してください（noise floor）",              4.0),
    ("rest",   "指先を軽く乗せるだけ（力は入れない）",                 5.0),
    ("light",  "軽く一定の力で押す（片側方向・軽い違和感程度）",       5.0),
    ("medium", "中程度の力で押す（明確に押している感）",               5.0),
    ("firm",   "しっかり押す（明らかな介入意図・怪我しない強さ）",     5.0),
    ("idle_2", "再び手を離す（post-check）",                             4.0),
]


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


def _constant(level_frac: float) -> sdl2.SDL_HapticEffect:
    eff = sdl2.SDL_HapticEffect()
    eff.type = sdl2.SDL_HAPTIC_CONSTANT
    c = eff.constant
    c.direction.type = sdl2.SDL_HAPTIC_CARTESIAN
    c.direction.dir[0] = 1
    c.length = INF
    c.level = int(max(-1.0, min(1.0, level_frac)) * 32767)
    return eff


def _countdown(msg: str, secs: int = 3) -> None:
    print()
    print(f"[NEXT] {msg}")
    for s in range(secs, 0, -1):
        print(f"       開始まで {s} 秒...", end="\r", flush=True)
        time.sleep(1)
    print("       >>> 計測中 <<<               ")


def _record_phase(label: str, secs: float, haptic, effect_id: int, joystick,
                  mode: str, kp: float, kd: float, max_force: float) -> tuple:
    """Return (samples, csv_path). samples = list of (t, actual_i, u_or_none)."""
    samples = []
    t0 = time.perf_counter()
    next_tick = t0
    prev_err = 0.0

    while True:
        now = time.perf_counter()
        t = now - t0
        if t >= secs: break

        sdl2.SDL_JoystickUpdate()
        actual_i = sdl2.SDL_JoystickGetAxis(joystick, STEERING_AXIS)
        actual_frac = actual_i / AXIS_FULL

        u = None
        if mode == "pid":
            err = 0.0 - actual_frac  # target = 0
            derr = (err - prev_err) / DT
            prev_err = err
            u = -(kp * err + kd * derr)  # sign inverted for G29 convention
            u = max(-max_force, min(max_force, u))
            eff = _constant(u)
            sdl2.SDL_HapticUpdateEffect(haptic, effect_id, ctypes.byref(eff))

        samples.append((t, actual_i, u))
        next_tick += DT
        s = next_tick - time.perf_counter()
        if s > 0: time.sleep(s)
        else: next_tick = time.perf_counter()

    return samples


def _stats(samples: list, drop_first_s: float = 0.5) -> dict:
    """Discard the first drop_first_s seconds (transition) then compute stats."""
    filtered = [(t, a, u) for (t, a, u) in samples if t >= drop_first_s]
    if not filtered:
        return {}
    devs = [a / AXIS_FULL for (_, a, _) in filtered]  # target = 0
    us = [u for (_, _, u) in filtered if u is not None]
    out = {
        "samples": len(filtered),
        "dev_mean_frac":       round(sum(devs) / len(devs), 5),
        "dev_std_frac":        round(statistics.stdev(devs) if len(devs) > 1 else 0.0, 5),
        "dev_max_abs_frac":    round(max(abs(d) for d in devs), 5),
        "dev_p50_abs_frac":    round(statistics.median(sorted(abs(d) for d in devs)), 5),
        "dev_p95_abs_frac":    round(sorted(abs(d) for d in devs)[int(0.95 * len(devs))], 5),
    }
    if us:
        out.update({
            "u_mean":     round(sum(us) / len(us), 4),
            "u_std":      round(statistics.stdev(us) if len(us) > 1 else 0.0, 4),
            "u_max_abs":  round(max(abs(u) for u in us), 4),
        })
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--mode", choices=["spring", "pid"], default="spring")
    ap.add_argument("--spring_coeff", type=float, default=0.95)
    ap.add_argument("--kp", type=float, default=4.0)
    ap.add_argument("--kd", type=float, default=0.35)
    ap.add_argument("--max_force", type=float, default=0.6)
    ap.add_argument("--gain", type=int, default=100)
    ap.add_argument("--yes", action="store_true")
    args = ap.parse_args()

    print("=" * 60)
    print(f"F7b spike 05 — Torque proxy measurement (mode={args.mode})")
    print("=" * 60)
    print("この計測は **ユーザーの手が必要** です。案内に従って、")
    print("目標角=0（センター位置）で、各フェーズで指示通りの力を掛けてください。")
    print("怪我のリスクを感じたら Ctrl+C で中断してください。")
    if not args.yes:
        input(">>> 準備できたら Enter で開始 <<<")

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

        sdl2.SDL_HapticSetGain(haptic, args.gain)

        if args.mode == "spring":
            eff = _spring(0, args.spring_coeff)
            effect_id = sdl2.SDL_HapticNewEffect(haptic, ctypes.byref(eff))
        else:
            eff = _constant(0.0)
            effect_id = sdl2.SDL_HapticNewEffect(haptic, ctypes.byref(eff))
        if effect_id < 0:
            print(f"[FAIL] NewEffect: {sdl2.SDL_GetError().decode(errors='ignore')}"); return 5
        sdl2.SDL_HapticRunEffect(haptic, effect_id, 1)

        # Let wheel settle briefly
        time.sleep(1.0)

        all_stats = {}
        all_csv = {}
        for (label, desc, secs) in PHASES:
            _countdown(desc, secs=3)
            samples = _record_phase(label, secs, haptic, effect_id, joystick,
                                    args.mode, args.kp, args.kd, args.max_force)
            csv_path = OUT_DIR / f"05_torque_proxy_{args.mode}_{label}.csv"
            with csv_path.open("w", newline="", encoding="utf-8") as f:
                w = csv.writer(f)
                w.writerow(["t_s", "actual_raw", "actual_frac", "u_force"])
                for (t, act, u) in samples:
                    w.writerow([f"{t:.5f}", act, f"{act/AXIS_FULL:.6f}",
                                f"{u:.4f}" if u is not None else ""])
            st = _stats(samples)
            all_stats[label] = st
            all_csv[label] = csv_path.name
            print(f"       done: dev|mean={st.get('dev_mean_frac')} std={st.get('dev_std_frac')} "
                  f"max={st.get('dev_max_abs_frac')} p95={st.get('dev_p95_abs_frac')}")

        # Threshold candidates
        candidates = {}
        for k in ("rest", "light"):
            if k in all_stats:
                p95 = all_stats[k].get("dev_p95_abs_frac", 0.0)
                # A threshold just above rest/light p95 catches medium+firm.
                candidates[f"above_{k}_p95"] = round(p95 * 1.5, 4)

        summary = {
            "mode": args.mode,
            "config": {"spring_coeff": args.spring_coeff, "kp": args.kp, "kd": args.kd,
                       "max_force": args.max_force, "gain": args.gain},
            "phases": all_stats, "csv": all_csv,
            "threshold_candidates_frac": candidates,
        }
        out = OUT_DIR / f"05_torque_proxy_summary_{args.mode}.json"
        out.write_text(json.dumps(summary, indent=2, ensure_ascii=False), encoding="utf-8")
        print(f"\n[OK] wrote {out}")
        return 0
    finally:
        _cleanup()


if __name__ == "__main__":
    sys.exit(main())
