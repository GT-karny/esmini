"""F7b characterization 09 — replay REAL AD steering profiles, with the full
C++ force composite reproduced.

Characterization items (4) and (5).

Item (4): the Day-1 spike only ever drove the wheel with sine / step / chirp at
±0.45. Real AD steering is nothing like that — the reported lane change never
exceeds 0.065 and spends 96% of its time under 0.035. This script replays the
recorded `driver.steer` time series (profiles/*.csv, produced by
extract_ad_profiles.py) as the servo target, in real time.

Item (5): SDLFFBSink does not send the servo output to the wheel — it sends the
SUM of five components, and four of them are computed from the SIMULATED wheel
angle, not the physical one. Those four oppose the servo by construction (the
servo is trying to move the wheel to exactly the angle the SAT terms want to
centre away from). This script reproduces that composite exactly, so the
cancellation can be measured rather than argued, and so the proposed fix can be
tested BEFORE any C++ is written.

    --composite sim   reproduce today's C++ behaviour (SAT/friction/damping from
                      the SIM wheel angle) — the bug, as shipped
    --composite phys  H1: source the same terms from the PHYSICAL wheel instead
    --composite off   suppress the feel terms while the servo is active
    --composite none  servo output only (upper bound on servo authority)

Reproduction constants are read from GT_esmini/config/manual_drive.json so this
cannot silently drift from the product. The sim wheel angle is reconstructed as
  steering_pos[rad] = -steer_gain * lag(steer_cmd)
(`RealVehicle.cpp`: target_wheel_angle = -steering * steer_gain, steer_gain=0.61).
CHARACTERIZATION.md §5 shows this reproduces the field log.txt line-for-line.

    scripts/ffb_spike/.venv/Scripts/python.exe scripts/ffb_spike/09_profile_replay.py \
        --profile lc --composite sim --method none --yes
    ... --dry     runs against the fitted WheelModel instead of the rig (no hardware)
"""
from __future__ import annotations

import argparse
import csv
import json
import math
import sys
import time
from pathlib import Path

import g29lib
from g29lib import (LOGS, PROFILES, Rig, Servo, ServoParams, WheelModel,
                    banner, write_csv)

ROOT = Path(__file__).resolve().parents[2]
MD_CONFIG = ROOT / "GT_esmini" / "config" / "manual_drive.json"
RV_CONFIG = ROOT / "GT_esmini" / "config" / "real_vehicle_params.json"


def load_ffb_config() -> dict:
    md = json.loads(MD_CONFIG.read_text(encoding="utf-8"))
    rv = json.loads(RV_CONFIG.read_text(encoding="utf-8"))
    f = md["controller"]["ffb"] if "controller" in md else md["ffb"]
    f = dict(f)
    f["steer_gain"] = rv["steer_gain"]
    return f


class Composite:
    """SDLFFBSink::UpdateCombinedConstantForce, reproduced in Python.

    Mirrors the C++ term for term (sat_predictive / sat_reactive / friction /
    damping / soft_stop). `source` selects where the steering position that
    drives sat_predictive / friction / damping / soft_stop comes from.
    """

    def __init__(self, cfg: dict, source: str):
        self.c = cfg
        self.source = source
        self.prev_pos = 0.0
        self.primed = False

    def step(self, steering_pos: float, lat_accel: float, speed: float, dt: float) -> dict:
        c = self.c
        if self.source == "off":
            self.prev_pos = steering_pos
            return {"sat_p": 0.0, "sat_r": 0.0, "fric": 0.0, "damp": 0.0,
                    "stop": 0.0, "sum": 0.0}

        vel = (steering_pos - self.prev_pos) / max(dt, 1e-3) if self.primed else 0.0
        self.prev_pos, self.primed = steering_pos, True

        speed_factor = min(max(speed / 30.0, 0.0), 1.0)
        assist = c["assist_low_speed"] + (c["assist_high_speed"] - c["assist_low_speed"]) * speed_factor
        manual_ratio = 1.0 - assist

        caster_onset = min(max(speed / 5.0, 0.0), 1.0)
        sat_p = -steering_pos * c["sat_centering_gain"] * caster_onset

        slip = min(max(abs(lat_accel) / 9.81, 0.0), 1.0)
        trail = max(0.0, 1.0 - slip * slip)
        sat_r = -lat_accel * c["sat_gain"] * trail * manual_ratio

        fric_mag = c["friction_base"] + c["friction_speed_gain"] * speed_factor
        fric = -math.tanh(vel * 3.0) * fric_mag

        damp = -vel * (c["damper_base"] + c["damper_speed_gain"] * speed_factor)

        stop = 0.0
        zone = 0.1
        over = abs(steering_pos) - (c["lock_angle"] - zone)
        if over > 0.0:
            n = min(max(over / zone, 0.0), 1.0)
            stop = -math.copysign(n * n * c["soft_stop_gain"], steering_pos)

        return {"sat_p": sat_p, "sat_r": sat_r, "fric": fric, "damp": damp,
                "stop": stop, "sum": sat_p + sat_r + fric + damp + stop}


def load_profile(name: str) -> list:
    path = PROFILES / f"{name}.csv"
    if not path.exists():
        raise SystemExit(f"[FAIL] {path} missing — run extract_ad_profiles.py first")
    with path.open(encoding="utf-8") as fh:
        return [(float(r["t_s"]), float(r["steer"]), float(r["speed"]), float(r["lat_accel"]))
                for r in csv.DictReader(fh)]


def interp(prof: list, t: float) -> tuple:
    """Zero-order-hold interpolation, matching how the sim hands the FFB sink a
    value once per 0.05 s frame while the sink itself runs at 250 Hz."""
    lo, hi = 0, len(prof) - 1
    if t <= prof[0][0]:
        return prof[0][1:]
    if t >= prof[-1][0]:
        return prof[-1][1:]
    while lo < hi - 1:
        mid = (lo + hi) // 2
        if prof[mid][0] <= t:
            lo = mid
        else:
            hi = mid
    return prof[lo][1:]


def replay(profile: str, args, rig: Rig | None) -> dict:
    DT = 1.0 / args.loop_hz
    prof = load_profile(profile)
    cfg = load_ffb_config()
    steer_gain = cfg["steer_gain"]
    outer_max = cfg["max_force"]

    servo = Servo(args.method, ServoParams.from_args(args))
    comp = Composite(cfg, args.composite)
    model = WheelModel() if rig is None else None

    if rig is not None:
        base = rig.settle(0.8)
        rig.set_reference(base)
    else:
        base = 0.0

    dur = min(prof[-1][0], args.max_seconds)
    lag_steer = 0.0
    tau = args.sim_lag
    rows, samples = [], []
    t0 = time.perf_counter()
    next_tick = t0
    while True:
        t = (time.perf_counter() - t0) if rig is not None else (len(samples) * DT)
        if t >= dur:
            break
        steer, speed, lat = interp(prof, t)

        # sim wheel angle: first-order lag of the command, then RealVehicle's
        # target_wheel_angle = -steering * steer_gain
        lag_steer += (steer - lag_steer) * (DT / max(tau, DT)) if tau > 0 else (steer - lag_steer)
        sim_pos = -steer_gain * lag_steer

        actual = (rig.axis() if rig is not None else model.pos)
        rel = actual - base

        if args.composite == "phys":
            # H1: same transform, physical axis instead of the sim wheel angle.
            feel_pos = -steer_gain * rel
        else:
            feel_pos = sim_pos
        terms = comp.step(feel_pos, lat, speed, DT)

        target_abs = base + steer
        u_servo, u_fb = servo.step(target_abs, actual, t, DT)
        total = max(-outer_max, min(outer_max, u_servo + terms["sum"]))

        if rig is not None:
            if not rig.guard(actual):
                total = 0.0
            applied = rig.set_force(total)
        else:
            applied = max(-args.max_force_cap, min(args.max_force_cap, total))
            model.step(applied, DT)

        samples.append((t, steer, rel, steer - rel, u_servo, u_fb, terms["sum"], applied))
        rows.append((f"{t:.4f}", f"{steer:+.6f}", f"{rel:+.6f}", f"{steer-rel:+.6f}",
                     f"{u_servo:+.4f}", f"{u_fb:+.4f}", f"{terms['sat_p']:+.4f}",
                     f"{terms['sat_r']:+.4f}", f"{terms['fric']:+.4f}",
                     f"{terms['damp']:+.4f}", f"{applied:+.4f}"))

        if rig is not None:
            next_tick += DT
            s = next_tick - time.perf_counter()
            if s > 0:
                time.sleep(s)
            else:
                next_tick = time.perf_counter()

    if rig is not None:
        rig.set_force(0.0)
        rig.recenter(0.0)

    errs = [abs(s[3]) for s in samples]
    moved = [s for s in samples if abs(s[2]) > 0.004]
    active = [s for s in samples if abs(s[1]) > 0.005]     # AD actually steering
    act_errs = [abs(s[3]) for s in active] or [0.0]
    # False-latch pressure: the product's torque proxy thresholds |commanded
    # force| at 0.20 with the driver's hands OFF. Report both the full command
    # and the feedback-only part (see Servo.step docstring).
    fb_over = sum(1 for s in samples if abs(s[5]) > args.override_thresh)
    full_over = sum(1 for s in samples if abs(s[4]) > args.override_thresh)

    res = {
        "profile": profile, "method": args.method, "composite": args.composite,
        "kp": args.kp, "max_force": args.max_force, "samples": len(samples),
        "dur_s": round(samples[-1][0], 2) if samples else 0.0,
        "wheel_ever_moved": bool(moved),
        "frac_time_moved": round(len(moved) / len(samples), 4) if samples else 0.0,
        "peak_abs_axis": round(max((abs(s[2]) for s in samples), default=0.0), 5),
        "peak_abs_target": round(max((abs(s[1]) for s in samples), default=0.0), 5),
        "err_mean": round(sum(errs) / len(errs), 5) if errs else None,
        "err_max": round(max(errs), 5) if errs else None,
        "err_mean_while_steering": round(sum(act_errs) / len(act_errs), 5),
        "servo_u_max": round(max((abs(s[4]) for s in samples), default=0.0), 4),
        "feel_sum_mean_abs": round(sum(abs(s[6]) for s in samples) / len(samples), 5) if samples else 0.0,
        "applied_mean_abs": round(sum(abs(s[7]) for s in samples) / len(samples), 5) if samples else 0.0,
        "frac_full_u_over_thresh": round(full_over / len(samples), 4) if samples else 0.0,
        "frac_fb_u_over_thresh": round(fb_over / len(samples), 4) if samples else 0.0,
    }
    if rows and not args.no_csv:
        tag = f"{profile}_{args.composite}_{args.method}" + ("_dry" if rig is None else "")
        write_csv(LOGS / f"09_replay_{tag}.csv",
                  ["t_s", "target", "axis_rel", "err", "u_servo", "u_fb",
                   "sat_p", "sat_r", "fric", "damp", "applied"], rows)
    return res


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--profile", default="lc",
                    help="profile name(s), comma separated: lc,curve,right_turn")
    ap.add_argument("--composite", default="sim", choices=["sim", "phys", "off", "none"])
    ap.add_argument("--method", default="none",
                    choices=["none", "ff", "integrator", "dither", "punch", "ff+i"])
    ap.add_argument("--dry", action="store_true",
                    help="use the fitted WheelModel instead of the real wheel")
    ap.add_argument("--kp", type=float, default=4.0)
    ap.add_argument("--kd", type=float, default=0.35)
    ap.add_argument("--max-force", type=float, default=0.6)
    ap.add_argument("--fstat", type=float, default=0.15)
    ap.add_argument("--ff-eps", type=float, default=0.01)
    ap.add_argument("--ki", type=float, default=30.0)
    ap.add_argument("--i-max", type=float, default=0.25)
    ap.add_argument("--sim-lag", type=float, default=0.05,
                    help="first-order lag [s] from steer command to sim wheel angle")
    ap.add_argument("--max-seconds", type=float, default=30.0)
    ap.add_argument("--max-force-cap", type=float, default=1.0, help="dry-run outer clamp")
    ap.add_argument("--override-thresh", type=float, default=0.20)
    ap.add_argument("--loop-hz", type=float, default=250.0,
                    help="servo update rate; the PRODUCT runs the FFB sink once "
                         "per sim frame (~20-40 Hz), not at the spike's 250 Hz")
    ap.add_argument("--excursion", type=float, default=0.45,
                    help="safety travel limit from rest; raise only for large-signal "
                         "profiles like right_turn (target 0.83 = ~370 deg of wheel)")
    ap.add_argument("--no-csv", action="store_true")
    ap.add_argument("--out", default="")
    ap.add_argument("--yes", action="store_true")
    args = ap.parse_args()

    profiles = [p.strip() for p in args.profile.split(",")]
    results = []
    if args.dry:
        print(f"[DRY] WheelModel (F_break={WheelModel.F_BREAK} F_kin={WheelModel.F_KIN} "
              f"k_v={WheelModel.K_V}) — no hardware used")
        for p in profiles:
            r = replay(p, args, None)
            results.append(r)
            _print(r)
    else:
        banner("09 profile replay",
               f"profiles={profiles} composite={args.composite} method={args.method} "
               f"Kp={args.kp} max_force={args.max_force}", args.yes)
        with Rig(force_cap=args.max_force if args.composite == "none" else 1.0,
                 excursion_limit=args.excursion) as rig:
            rig.settle(0.8)
            for p in profiles:
                r = replay(p, args, rig)
                results.append(r)
                _print(r)

    name = args.out or f"09_replay_{args.composite}_{args.method}{'_dry' if args.dry else ''}.json"
    (LOGS / name).write_text(json.dumps(results, indent=2), encoding="utf-8")
    print(f"\n[OK] wrote {LOGS / name}")
    return 0


def _print(r: dict) -> None:
    print(f"  {r['profile']:<11} composite={r['composite']:<5} method={r['method']:<10} "
          f"moved={str(r['wheel_ever_moved']):<5} t_moved={r['frac_time_moved']:.3f} "
          f"peak_axis={r['peak_abs_axis']:.4f}/{r['peak_abs_target']:.4f} "
          f"err_steering={r['err_mean_while_steering']:.4f} "
          f"feel={r['feel_sum_mean_abs']:.4f} "
          f"u>thr full/fb={r['frac_full_u_over_thresh']:.3f}/{r['frac_fb_u_over_thresh']:.3f}")


if __name__ == "__main__":
    sys.exit(main())
