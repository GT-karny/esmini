"""feature:F7 — post-RESUME false-latch FORENSICS (and, later, its regression test).

THE CLAIM UNDER TEST. "Right after AUTO_RESUME the override detector latches
MANUAL again with nobody touching the wheel, and because the latch is one-way
the servo stays dead — so the user cannot re-override." A prior session wrote
down a mechanism for it (the residual ends up measuring the AD target's own
movement rate rather than a driver's resistance). This probe exists to CHECK
that mechanism against numbers rather than adopt it.

WHY A NEW PROBE RATHER THAN vd_multi_cycle_override.py. That harness returns
to AUTO through `auto_return_timeout` (the idle path), because until now
HeadlessFfbInput::Poll() hardcoded buttons=0 and there was no way to press
AUTO_RESUME headless. Those are NOT the same code path:

    RESUME rising edge (OverrideManager::Update)  -> resets sustain accumulator,
        history validity, shadow validity, shadow velocity, free shadow.
    auto_return_timeout / RequestAutoMode()       -> resets NONE of that.

So every existing headless "resume" measurement was exercising a different
mechanism from the one a user's RESUME button takes. This probe presses the
real button (buttons now travel over the injection wire) and runs BOTH paths so
the difference is measured instead of assumed.

HANDS OFF MEANS HANDS OFF. mode=plant is the force-coupled wheel model: the
wire's value is DRIVER FORCE, not an axis offset, so sending 0.0 is literally
"no hand on the wheel". Every latch this probe reports therefore has no driver
behind it by construction. MANUAL is entered with the OVERRIDE BUTTON, which
needs no wheel movement at all -- keeping the entry clean of the very axis
displacement whose after-effects we are trying to observe.

TIME ALIGNMENT IS CHECKED, NOT ASSUMED. One telemetry line holds two instants
(VirtualDriverTelemetryJson.cpp): the top-level "ffb" block is the sink sample
written AFTER this frame's FFB update, while "ffb.gates" is OverrideManager's
view of the sample from BEFORE it -- i.e. the one in the PREVIOUS line's "ffb".
A consumer that pairs ffb.*(N) with gates.*(N) agrees while the wheel is
stationary and is silently one frame off exactly when the wheel moves, which is
the whole part that matters here. verify_time_alignment() re-measures that
relation on THIS run's data before any conclusion is drawn from it, and the
report says so out loud.
"""

from __future__ import annotations

import argparse
import ctypes
import json
import os
import socket
import statistics
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from vd_resume_transient import (  # noqa: E402
    DLL, MAGIC, WIRE, _load_lib, _make_variant,
)

BTN_OVERRIDE    = 1 << 0
BTN_AUTO_RESUME = 1 << 7

PUSHBACK_PORT = 9105          # HeadlessFfbInput GT_HEADLESS_FFB_PUSHBACK_PORT default

# Real-machine identification step. Every real-wheel number this program is
# compared against was recorded at 0.010 s (the --fixed_timestep knob did not
# reach GT_Step before 2026-07-27, so "0.05" runs were really 0.010). Matching
# it matters here because the quantity under investigation is a per-FRAME
# difference: at a different dt the same physical motion produces a different
# residual per frame.
DT = 0.01


# --------------------------------------------------------------------------
# Run one scripted episode
# --------------------------------------------------------------------------
def _apply_plant(plant: dict | None) -> None:
    """Plant constants for this episode. Varying them AWAY from the detector's
    shadow constants is the point: a plant identical to the shadow is a
    tautology that can never show a model-error false positive, and the real
    wheel is not the model (the shipped shadow explains only part of the
    measured residual)."""
    for key in ("BREAKAWAY", "KINETIC", "SLOPE", "VMAX", "NOISE_AMP", "SEED",
                "DEAD_TIME", "VELOCITY_TAU"):
        os.environ.pop(f"GT_HEADLESS_FFB_PLANT_{key}", None)
    for k, v in (plant or {}).items():
        os.environ[f"GT_HEADLESS_FFB_PLANT_{k}"] = str(v)


def run_episode(*, return_path: str, steering_threshold: float,
                envelope_enabled: bool = True, speed_mps: float = 8.0,
                settle_s: float = 3.0, push_s: float = 1.2, release_s: float = 0.8,
                observe_s: float = 4.0, plant_init_at: float = 0.0,
                driver_force: float = 0.45, plant: dict | None = None,
                observe_force: float = 0.0) -> list[dict]:
    """One override -> steer away -> let go -> return-to-AUTO -> hands-off episode.

    The steer-away phase is what makes this a test of anything. A first version
    of this probe kept the wheel at zero throughout, on a straight road: the AD
    target stayed ~0, the servo had nothing to do, the wheel never moved, and
    of course nothing false-latched. It also made the time-alignment check
    untestable, since the two candidate pairings only differ once the wheel
    moves. The episode now reproduces what the user actually does -- take the
    wheel, move to the next lane, let go, press RESUME -- so that at the moment
    of return the wheel is off-centre AND the AD wants a large correction.

    In mode=plant the injected wire value is DRIVER FORCE, so `driver_force`
    during the push phase is a hand on the wheel, and 0.0 afterwards is a
    genuinely released wheel that coasts and settles on its own.

    return_path:
      "button" -- press AUTO_RESUME (the path a user takes).
      "idle"   -- let auto_return_timeout fire (the path older harnesses used).
    """
    _apply_plant(plant)
    os.environ["GT_HEADLESS_FFB_MODE"] = "plant"
    os.environ["GT_HEADLESS_FFB_PUSHBACK_PORT"] = str(PUSHBACK_PORT)
    os.environ["GT_HEADLESS_FFB_PLANT_INIT_AT"] = f"{plant_init_at:.5f}"
    os.environ.pop("GT_HEADLESS_FFB_FROZEN_AT", None)
    os.environ.pop("GT_HEADLESS_FFB_LAG_TAU", None)

    tmpdir = tempfile.mkdtemp(prefix="vd_resume_falselatch_")
    cfg = {
        "input_type": "headless_ffb",
        "ffb_target_track_enabled": True,
        "override_enabled": True,
        "override_button": True,
        "steering_threshold": steering_threshold,
        # Only the "idle" arm should ever return by timeout. Disabled (0.0)
        # for the button arm so the two paths cannot be confused for each
        # other in the record.
        "auto_return_timeout": 1.0 if return_path == "idle" else 0.0,
        "ad_steering_envelope_enabled": envelope_enabled,
    }
    xosc = _make_variant(tmpdir, cfg, speed_mps)

    lib = _load_lib()
    argv_list = [b"vd_resume_false_latch", b"--osc", xosc.encode(), b"--headless"]
    argv = (ctypes.c_char_p * len(argv_list))(*argv_list)
    rc = lib.GT_InitWithArgs(len(argv_list), argv)
    if rc != 0:
        raise RuntimeError(f"GT_InitWithArgs rc={rc}")

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    wire = {"force": 0.0, "buttons": 0}

    def send():
        pkt = WIRE.pack(MAGIC, wire["force"], 0.0, 0.0, 0.0, 0, wire["buttons"])
        try:
            sock.sendto(pkt, ("127.0.0.1", PUSHBACK_PORT))
        except OSError:
            pass

    buf = ctypes.create_string_buffer(32768)
    frames: list[dict] = []

    def run(phase: str, n_steps: int):
        for _ in range(n_steps):
            send()
            lib.GT_Step(DT)
            n = lib.GT_GetVirtualDriverTelemetry(0, buf, len(buf))
            if n > 0:
                f = json.loads(buf.value.decode())
                f["_phase"] = phase
                frames.append(f)

    # 1. Hands off, AD driving, servo tracking the wheel to the AD target.
    run("settle", int(settle_s / DT))

    # 2. Enter MANUAL with the button (no wheel displacement needed for the
    #    entry itself, so the entry cannot be confused with the axis-threshold
    #    path), then actually steer away from the route.
    wire["buttons"] = BTN_OVERRIDE
    run("override_press", 2)
    wire["buttons"] = 0
    wire["force"] = driver_force
    run("manual_push", int(push_s / DT))
    # Let go. The wheel is now off-centre and the car is off its route -- the
    # exact state a user is in when they reach for RESUME.
    wire["force"] = 0.0
    run("manual_release", int(release_s / DT))

    # 3. Return to AUTO by the requested path.
    if return_path == "button":
        wire["buttons"] = BTN_AUTO_RESUME
        run("resume_press", 1)
        wire["buttons"] = 0
    # "idle": nothing to send; auto_return_timeout fires on its own.

    # 4. The observation window. With observe_force == 0.0 this is hands off,
    #    so anything that latches is a false positive (negative fixture). With
    #    observe_force != 0.0 a hand is on the wheel throughout and a latch is
    #    REQUIRED (positive fixture) -- the same code path measured from both
    #    sides, because a detector that never fires would pass the first test
    #    perfectly.
    wire["force"] = observe_force
    run("observe", int(observe_s / DT))

    lib.GT_Close()
    sock.close()
    try:
        os.remove(xosc)
    except OSError:
        pass
    return frames


# --------------------------------------------------------------------------
# Instrument check: is the record time-aligned the way we think?
# --------------------------------------------------------------------------
def verify_time_alignment(frames: list[dict]) -> dict:
    """Re-measure gates.actual_norm(N) == (ffb.target_norm - position_error)(N-1).

    Reported for the frames where the wheel is actually MOVING -- while it sits
    still both pairings agree and the check proves nothing.
    """
    # Tolerance is set by the RECORD, not by taste: telemetry prints 9
    # decimals, so each value carries up to 5e-10 of rounding and a difference
    # of two of them up to 1e-9. A first version used "< 1e-9" and reported
    # UNRESOLVED with a worst mismatch of 1.0000000827e-09 -- the instrument
    # check was measuring its own print precision. 5e-9 is still seven orders
    # below the ~1e-2 signals being compared.
    tol = 5e-9
    same_line_hits = moving = shifted_hits = 0
    worst_shifted = 0.0
    for i in range(1, len(frames)):
        g = frames[i].get("ffb", {}).get("gates", {})
        prev = frames[i - 1].get("ffb", {})
        cur = frames[i].get("ffb", {})
        if not g or "actual_norm" not in g:
            continue
        a_gates = g["actual_norm"]
        a_prev_line = prev.get("target_norm", 0.0) - prev.get("position_error", 0.0)
        a_same_line = cur.get("target_norm", 0.0) - cur.get("position_error", 0.0)
        if abs(a_prev_line - a_same_line) < tol:
            continue                       # stationary: the two agree, tells us nothing
        moving += 1
        if abs(a_gates - a_prev_line) < tol:
            shifted_hits += 1
        else:
            worst_shifted = max(worst_shifted, abs(a_gates - a_prev_line))
        if abs(a_gates - a_same_line) < tol:
            same_line_hits += 1
    return {
        "moving_frames_compared": moving,
        "matches_previous_line": shifted_hits,
        "matches_same_line": same_line_hits,
        "worst_mismatch_vs_previous_line": worst_shifted,
        "alignment": ("gates(N) == ffb(N-1)" if moving and shifted_hits == moving else
                      "gates(N) == ffb(N)" if moving and same_line_hits == moving else
                      "UNRESOLVED" if moving else "NOT TESTABLE (wheel never moved)"),
    }


# --------------------------------------------------------------------------
# Forensics
# --------------------------------------------------------------------------
def _g(f: dict, key: str, default=0.0):
    return f.get("ffb", {}).get("gates", {}).get(key, default)


def _lat_manual(f: dict) -> bool:
    return bool(f.get("override", {}).get("lateral", False))


def find_first_false_latch(frames: list[dict]) -> int | None:
    """Index of the first RE-latch in the observation window.

    "Still MANUAL" is not the same event as "latched again", and conflating
    them made this probe report a false latch that never happened: on the idle
    (auto_return_timeout) arm the observation window opens while the ORIGINAL
    latch is still held -- the timeout has not expired yet -- so the first
    observe frames are legitimately MANUAL. Reporting those as a false
    positive would blame the product for the harness starting to watch too
    early.

    A false latch is therefore only counted once the run has been seen back in
    AUTO: from that point on, with nobody touching the wheel, any return to
    MANUAL is a false positive by construction.
    """
    seen_auto = False
    for i, f in enumerate(frames):
        if f["_phase"] != "observe":
            continue
        if not _lat_manual(f):
            seen_auto = True
        elif seen_auto:
            return i
    return None


def decompose(frames: list[dict], idx: int, window: int = 12) -> list[dict]:
    """Per-frame table around `idx`, aligned. Everything in the returned rows
    that comes from `gates` is one instant (the sample of the previous line);
    `target` is taken from the PREVIOUS line's ffb block so it belongs to that
    same instant."""
    rows = []
    lo = max(1, idx - window)
    hi = min(len(frames), idx + 4)
    for i in range(lo, hi):
        f, p = frames[i], frames[i - 1]
        target = p.get("ffb", {}).get("target_norm", 0.0)
        prev_target = frames[i - 2].get("ffb", {}).get("target_norm", 0.0) if i >= 2 else target
        row = {
            "i": i,
            "t": round(f.get("sim_time", 0.0), 4),
            "phase": f["_phase"],
            "target": target,
            "d_target": target - prev_target,
            "actual": _g(f, "actual_norm"),
            "d_actual": _g(f, "actual_norm") - _g(p, "actual_norm"),
            "shadow": _g(f, "shadow_norm"),
            "d_shadow": _g(f, "shadow_norm") - _g(p, "shadow_norm"),
            "residual": _g(f, "residual"),
            "d_residual": _g(f, "residual") - _g(p, "residual"),
            "thresh": _g(f, "residual_threshold"),
            "force": _g(f, "effective_force"),
            "sustain": _g(f, "sustain_accum"),
            "block": _g(f, "block_reason", ""),
            "reanchor": _g(f, "reanchor_source", ""),
            "free_residual": _g(f, "free_residual"),
            "shadow_moving": _g(f, "shadow_moving", False),
            "lat_manual": _lat_manual(f),
            "ffb_active": f.get("ffb", {}).get("target_active", False),
        }
        rows.append(row)
    return rows


def attribute(frames: list[dict], idx: int) -> dict:
    """Which detector fired, and what was the residual made of?

    The residual is |actual - shadow|. Its per-frame GROWTH is therefore
    d_actual - d_shadow. The question "is it measuring the AD target's motion?"
    is answered by comparing that growth against d_target over the frames that
    built the latch.
    """
    f = frames[idx]
    ffb_active = f.get("ffb", {}).get("target_active", False)
    block = _g(f, "block_reason", "")
    residual = _g(f, "residual")
    thresh = _g(f, "residual_threshold")

    # Which path? The direct-axis check only runs while the sink sample is
    # inactive; the residual path only runs while it is active.
    if not ffb_active or block == "inactive":
        path = "DIRECT_AXIS (parked wheel angle read as a driver)"
    elif residual > thresh:
        path = "RESIDUAL"
    else:
        path = "UNCLEAR"

    # Walk back over the frames that accumulated the sustain clock.
    lo = idx
    while lo > 1 and _g(frames[lo - 1], "sustain_accum") > 0.0:
        lo -= 1
    growth, tgt_move, act_move, shd_move = [], [], [], []
    for i in range(lo, idx + 1):
        p = frames[i - 1]
        growth.append(_g(frames[i], "residual") - _g(p, "residual"))
        act_move.append(_g(frames[i], "actual_norm") - _g(p, "actual_norm"))
        shd_move.append(_g(frames[i], "shadow_norm") - _g(p, "shadow_norm"))
        t_now = frames[i - 1].get("ffb", {}).get("target_norm", 0.0)
        t_prev = frames[i - 2].get("ffb", {}).get("target_norm", 0.0) if i >= 2 else t_now
        tgt_move.append(t_now - t_prev)
    return {
        "path": path,
        "latch_sim_time": f.get("sim_time"),
        "residual_at_latch": residual,
        "residual_threshold": thresh,
        "build_up_frames": idx - lo + 1,
        "sum_d_residual": sum(growth),
        "sum_d_actual": sum(act_move),
        "sum_d_shadow": sum(shd_move),
        "sum_d_target": sum(tgt_move),
        "mean_abs_d_target_per_frame": statistics.fmean([abs(x) for x in tgt_move]) if tgt_move else 0.0,
        "mean_abs_d_actual_per_frame": statistics.fmean([abs(x) for x in act_move]) if act_move else 0.0,
        "mean_abs_d_shadow_per_frame": statistics.fmean([abs(x) for x in shd_move]) if shd_move else 0.0,
        "free_residual_at_latch": _g(f, "free_residual"),
        "reanchor_source_at_latch": _g(f, "reanchor_source", ""),
    }


def observe_stats(frames: list[dict]) -> dict:
    """What the detector saw in the hands-off window, as MARGINS rather than a
    yes/no. A run that did not latch is not evidence of much on its own -- a
    peak residual sitting at 0.95x the threshold is a different world from one
    at 0.05x, and only the second is a real answer."""
    obs = [f for f in frames if f["_phase"] == "observe"]
    if not obs:
        return {}
    peak_res = 0.0
    peak_i = 0
    thresh = 0.0
    max_wheel_speed = 0.0
    max_shadow_speed = 0.0
    for i, f in enumerate(obs):
        if not f.get("ffb", {}).get("target_active", False):
            continue
        r = _g(f, "residual")
        thresh = _g(f, "residual_threshold") or thresh
        if r > peak_res:
            peak_res, peak_i = r, i
        if i > 0:
            max_wheel_speed = max(max_wheel_speed,
                                  abs(_g(f, "actual_norm") - _g(obs[i - 1], "actual_norm")) / DT)
            max_shadow_speed = max(max_shadow_speed,
                                   abs(_g(f, "shadow_norm") - _g(obs[i - 1], "shadow_norm")) / DT)
    return {
        "peak_residual": peak_res,
        "residual_threshold": thresh,
        "peak_over_threshold": (peak_res / thresh) if thresh else float("nan"),
        "peak_at_sim_time": obs[peak_i].get("sim_time"),
        "max_wheel_speed_per_s": max_wheel_speed,
        "max_shadow_speed_per_s": max_shadow_speed,
        # Same distinction as find_first_false_latch: a RE-latch after the run
        # has been seen back in AUTO, not the original latch still being held
        # while the return path does its work.
        "latched": find_first_false_latch(frames) is not None,
    }


def _fmt_rows(rows: list[dict]) -> str:
    hdr = (f"{'t':>7} {'phase':<14} {'target':>9} {'dtgt':>9} {'actual':>9} {'dact':>9} "
           f"{'shadow':>9} {'dshd':>9} {'resid':>9} {'dres':>9} {'force':>8} "
           f"{'sust':>6} {'act?':>5} {'MAN':>4} {'block':<12} {'reanchor':<12}")
    out = [hdr, "-" * len(hdr)]
    for r in rows:
        out.append(f"{r['t']:>7.3f} {r['phase']:<14} {r['target']:>9.5f} {r['d_target']:>9.5f} "
                   f"{r['actual']:>9.5f} {r['d_actual']:>9.5f} {r['shadow']:>9.5f} {r['d_shadow']:>9.5f} "
                   f"{r['residual']:>9.5f} {r['d_residual']:>9.5f} {r['force']:>8.4f} "
                   f"{r['sustain']:>6.3f} {str(r['ffb_active']):>5} {str(r['lat_manual']):>4} "
                   f"{str(r['block']):<12} {str(r['reanchor']):<12}")
    return "\n".join(out)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--return-path", choices=["button", "idle", "both"], default="both")
    ap.add_argument("--steering-threshold", type=float, default=0.05,
                    help="0.05 = the SHIPPED value. Older harnesses used 1.0 to "
                         "suppress the direct-axis path; that also hid it.")
    ap.add_argument("--no-envelope", action="store_true")
    ap.add_argument("--out", default=None, help="write raw frames as jsonl")
    ap.add_argument("--fleet", action="store_true",
                    help="run a grid of plant variants x both fixture polarities and "
                         "report the false-latch rate as an interval")
    ap.add_argument("--sweep", action="store_true",
                    help="sweep the departure magnitude and report the residual MARGIN "
                         "for each, instead of one pass/fail episode")
    args = ap.parse_args()

    if args.fleet:
        # A distribution, not a verdict. "0 false latches" out of a handful of
        # runs is compatible with a double-digit true rate, so the fleet is
        # sized and the result reported as an interval.
        # VMAX is varied because it is the constant that actually decides this
        # regime. A first grid swept BREAKAWAY x SLOPE and produced identical
        # numbers for every variant at a given seed: the servo saturates the
        # wheel at plant v_max during the post-return correction, so the slope
        # never binds and the breakaway only matters for the frame or two at
        # motion onset. (The env vars WERE reaching the DLL -- 54 distinct
        # config lines were logged -- so this was a real property of the
        # regime, not a plumbing failure.) The shadow's own v_max is 1.0, so
        # 0.8/1.0/1.3 straddle it: a plant that outruns the shadow is exactly
        # the model mismatch that produces residual with nobody touching.
        plants = []
        for brk in (0.170, 0.210):
            for vmax in (0.8, 1.0, 1.3):
                for seed in (12345, 777, 24680):
                    plants.append({"BREAKAWAY": brk, "VMAX": vmax,
                                   "NOISE_AMP": 0.002, "SEED": seed})
        print(f"fleet: {len(plants)} plant variants x 2 polarities, "
              f"steering_threshold={args.steering_threshold}, dt={DT}")
        print(f"{'brk':>6} {'vmax':>6} {'seed':>7} | {'neg peak':>9} {'neg x_thr':>10} "
              f"{'neg latch':>10} | {'pos latch':>10}")
        neg_latched = pos_latched = 0
        neg_peaks = []
        for p in plants:
            fneg = run_episode(return_path="button", steering_threshold=args.steering_threshold,
                               envelope_enabled=not args.no_envelope, plant=p,
                               observe_force=0.0)
            sneg = observe_stats(fneg)
            fpos = run_episode(return_path="button", steering_threshold=args.steering_threshold,
                               envelope_enabled=not args.no_envelope, plant=p,
                               observe_force=0.55)
            spos = observe_stats(fpos)
            neg_latched += 1 if sneg.get("latched") else 0
            pos_latched += 1 if spos.get("latched") else 0
            neg_peaks.append(sneg.get("peak_over_threshold", float("nan")))
            print(f"{p['BREAKAWAY']:>6.3f} {p['VMAX']:>6.2f} {p['SEED']:>7} | "
                  f"{sneg.get('peak_residual', 0):>9.5f} {sneg.get('peak_over_threshold', 0):>10.3f} "
                  f"{str(sneg.get('latched')):>10} | {str(spos.get('latched')):>10}")
        n = len(plants)
        print(f"\nnegative fixture (hands off, must NOT latch): {neg_latched}/{n} latched")
        if neg_peaks:
            ordered = sorted(x for x in neg_peaks if x == x)
            print(f"   peak residual as a fraction of the threshold: "
                  f"min={ordered[0]:.3f} median={ordered[len(ordered)//2]:.3f} "
                  f"max={ordered[-1]:.3f}")
            print(f"   (a run that does not latch but sits at 0.95x is not a pass "
                  f"in any useful sense -- the margin is the result)")
        print(f"positive fixture (hand on the wheel, MUST latch): {pos_latched}/{n} latched")
        if neg_latched == 0:
            # Clopper-Pearson upper bound at 95% for 0 successes: 1-0.05^(1/n)
            ub = 1.0 - 0.05 ** (1.0 / n)
            print(f"   0/{n} bounds the false-latch rate at <= {ub*100:.1f}% (95% upper limit)")
        return 0

    if args.sweep:
        print(f"{'driver_force':>12} {'peak_resid':>11} {'thresh':>8} {'x_thresh':>9} "
              f"{'wheel_/s':>9} {'shadow_/s':>10} {'latched':>8}")
        for force in (0.25, 0.35, 0.45, 0.55, 0.65, 0.80, 1.00):
            frames = run_episode(return_path="button",
                                 steering_threshold=args.steering_threshold,
                                 envelope_enabled=not args.no_envelope,
                                 driver_force=force)
            s = observe_stats(frames)
            if not s:
                print(f"{force:>12.2f}  (no observation window)")
                continue
            print(f"{force:>12.2f} {s['peak_residual']:>11.5f} {s['residual_threshold']:>8.3f} "
                  f"{s['peak_over_threshold']:>9.3f} {s['max_wheel_speed_per_s']:>9.4f} "
                  f"{s['max_shadow_speed_per_s']:>10.4f} {str(s['latched']):>8}")
        return 0

    if not os.path.exists(DLL):
        print(f"DLL not found: {DLL}", file=sys.stderr)
        return 2

    paths = ["button", "idle"] if args.return_path == "both" else [args.return_path]
    verdicts = {}
    for path in paths:
        print("=" * 100)
        print(f"ARM: return_path={path}  steering_threshold={args.steering_threshold} "
              f"envelope={'off' if args.no_envelope else 'on'}  dt={DT}")
        print("=" * 100)
        frames = run_episode(return_path=path,
                             steering_threshold=args.steering_threshold,
                             envelope_enabled=not args.no_envelope)
        if args.out:
            with open(f"{args.out}.{path}.jsonl", "w", encoding="utf-8") as fh:
                for f in frames:
                    fh.write(json.dumps(f) + "\n")

        align = verify_time_alignment(frames)
        print("\n-- instrument check: telemetry time alignment --")
        for k, v in align.items():
            print(f"   {k}: {v}")
        if align["alignment"] not in ("gates(N) == ffb(N-1)",):
            print("   !! The pairing this probe assumes was NOT confirmed on this run.")
            print("      Every number below that mixes 'target' with 'gates' is suspect.")

        entered_manual = any(_lat_manual(f) for f in frames
                             if f["_phase"] in ("manual_push", "manual_release"))
        returned_auto = any(not _lat_manual(f) for f in frames if f["_phase"] == "observe")
        print(f"\n-- episode sanity --\n   entered MANUAL: {entered_manual}"
              f"\n   returned to AUTO at least once in observe: {returned_auto}")
        if not entered_manual:
            print("   !! The override button never latched -- the episode did not test anything.")
            verdicts[path] = {"valid": False}
            continue
        if not returned_auto:
            print("   !! Never returned to AUTO -- the return path did not fire.")
            verdicts[path] = {"valid": False}
            continue

        idx = find_first_false_latch(frames)
        if idx is None:
            print("\n-- RESULT: NO false latch in the hands-off observation window --")
            verdicts[path] = {"valid": True, "false_latch": False}
            continue

        att = attribute(frames, idx)
        print(f"\n-- RESULT: FALSE LATCH at t={att['latch_sim_time']:.3f} "
              f"via {att['path']} --")
        print("\n" + _fmt_rows(decompose(frames, idx)))
        print("\n-- what the residual was made of over the frames that built the latch --")
        for k in ("build_up_frames", "sum_d_residual", "sum_d_actual", "sum_d_shadow",
                  "sum_d_target", "mean_abs_d_target_per_frame",
                  "mean_abs_d_actual_per_frame", "mean_abs_d_shadow_per_frame",
                  "residual_at_latch", "residual_threshold", "free_residual_at_latch",
                  "reanchor_source_at_latch"):
            print(f"   {k}: {att[k]}")
        verdicts[path] = {"valid": True, "false_latch": True, **att}

    print("\n" + "=" * 100)
    print("SUMMARY")
    print("=" * 100)
    for path, v in verdicts.items():
        if not v.get("valid"):
            print(f"  {path:<7}: INVALID EPISODE (see above)")
        elif not v.get("false_latch"):
            print(f"  {path:<7}: no false latch")
        else:
            print(f"  {path:<7}: FALSE LATCH via {v['path']} at t={v['latch_sim_time']:.3f}, "
                  f"residual {v['residual_at_latch']:.4f} vs threshold {v['residual_threshold']:.4f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
