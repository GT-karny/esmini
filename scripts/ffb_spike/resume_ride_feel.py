"""feature:F7 -- resume-transient RIDE-FEEL characterization (team-lead task,
2026-07-26). Measurement-only: no C++ changes, no config changes, no commits.

Question this answers (team-lead framing): AdSteeringEnvelope (see
GT_esmini/include/gt_esmini/control/virtualdriver/AdSteeringEnvelope.hpp)
already clamps lateral accel / yaw rate / steering RATE on the AD-commanded
steering, but not steering JERK (the RATE's own rate of change) -- so a
manual->AUTO_RESUME transition can still step the steering rate from 0 to its
cap in a single 0.01s frame. A steering-jerk cap of 25 [1/s^2] (normalized-
steer d^2/dt^2) has been PROPOSED, chosen from the "valley" of the real-wheel
commanded-jerk distribution (median 0.000 / p99 2.00 / max 290) -- i.e. from
FFB-shadow-residual concerns (wheel-space, speed-independent). This script
independently derives what a jerk cap needs to be to satisfy the OTHER
beneficiary: resume-transient RIDE FEEL (vehicle-motion-space, speed-
dependent), and checks whether 25/s^2 happens to satisfy it too.

Does NOT modify GT_esmini/test/headless/vd_resume_transient.py -- imports its
run_network_arm() / compute_metrics() / WHEEL_BASE / MAX_STEER_ANGLE and adds
NEW post-processing math here only (steer jerk d^2(steer_norm)/dt^2, lateral
jerk d(a_lat)/dt, lateral snap d^2(a_lat)/dt^2). arm2 (headless_ffb) is
EXCLUDED from this script by design: per vd_resume_transient.py's own
docstring it is "BEST-EFFORT" with no clean pre/post-resume phase separation
(the synthetic wheel is frozen from t=0, never "releases"), so its resume
transient is not a clean vehicle-motion measurement. arm1 (network,
AD-vs-physics) is that harness's own "MUST-RUN arm" and is the one that
isolates AD-command -> vehicle-motion causation, which is exactly what ride
feel is about.

Two independently-sourced datasets:
  (A) CURRENT resume transient: headless in-process re-run of arm1 across a
      speed x lateral-offset-target grid, envelope ad_steering_envelope_enabled
      =true (current shipped default -- see GT_esmini/config/virtual_driver.json
      "ad_steering_envelope_enabled": true), NO jerk cap (does not exist in
      code yet). This is CLOSED-LOOP: real simulated vehicle physics response
      to the AD's actual command.
  (B) NORMAL-DRIVING baseline: real-hardware (G29) unattended hands-off AD
      logs test_results/f7_realwheel_{basic,right_turn,tljunction}.jsonl
      (dt=0.01s, ffb.target_norm = AD's steering command per team-lead spec).
      Steer-command jerk here is a pure wheel-space quantity (speed-
      independent). Lateral jerk/snap are derived from the SAME steer command
      via the bicycle-model relation used by vd_resume_transient.py's own
      kinematic_window_metrics() (open-loop: filters the recorded command,
      does not re-run physics) -- explicitly flagged as OPEN-LOOP everywhere
      it appears below.

Usage (DriverScript venv):
  DriverScript\\.venv\\Scripts\\python.exe scripts\\ffb_spike\\resume_ride_feel.py
Output: prints a compact summary to stdout; full tables written to
  test_results/f7_resume_ride_feel.txt

DISCOVERED BUG (reported, NOT fixed here -- vd_resume_transient.py is
untouched per instructions): vd_resume_transient.py's _make_variant() sets
the AccelAction target speed via
  root.findall(".//AccelAction//AbsoluteTargetSpeed")
"AccelAction" is the `name` ATTRIBUTE of the scenario's `<Action>` element
(resources/xosc/virtual_driver_basic.xosc line 74: `<Action name="AccelAction">`),
NOT an element tag -- so this ElementTree find always returns [] and the
override silently never applies. Every existing run of this harness's
--speed / speed_mps parameter (arm1's run_network_arm, and run_normal_baseline)
has therefore ALWAYS driven the scenario at its hardcoded default target
(15.0 m/s, resources/xosc/virtual_driver_basic.xosc line 80), regardless of
the value requested. Confirmed directly: root.findall(".//AccelAction//
AbsoluteTargetSpeed") == [] on that file, while root.findall(".//
AbsoluteTargetSpeed") finds both the AccelAction's (15.0) and StopAction's
(0.0) targets. This script needs genuine speed control for the point-3/4
speed-dependence analysis, so a LOCAL corrected copy of _make_variant/
run_network_arm is defined below (_make_variant_speed_fixed /
run_network_arm_speed_fixed) with the one-line xpath fix
(".//Action[@name='AccelAction']//AbsoluteTargetSpeed"), used ONLY by this
script's sweep -- vd_resume_transient.py itself is never written to.
"""
from __future__ import annotations

import contextlib
import ctypes
import json
import math
import os
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "GT_esmini", "test", "headless"))
import vd_resume_transient as vrt  # noqa: E402  (path insert must precede this)

MAX_STEER_ANGLE = vrt.MAX_STEER_ANGLE  # 0.61 rad, config/virtual_driver.json
# Per-scenario wheel_base [m] = car_white bbox length * 0.6 (ControllerVirtualDriver.cpp:297).
# vd_resume_transient.py's own scenario (virtual_driver_basic.xosc) uses the
# VehicleCatalog car_white entry (length 5.04 -> 3.024, = vrt.WHEEL_BASE).
# The real-wheel logs replay THREE different scenarios with a slightly
# different inline car_white bbox (length 5.0 -> 3.0) for right_turn/
# tljunction; f7_realwheel_basic.jsonl's own scenario (same naming pattern as
# vd_resume_transient's) uses the catalog entry (3.024). Confirmed by reading
# resources/xosc/verification/05_anticipation/{decelerate_for_left_turn,
# traffic_lights_junction}.xosc (inline BoundingBox length="5.0") vs.
# resources/xosc/virtual_driver_basic.xosc (CatalogReference car_white,
# VehicleCatalog.xosc length="5.04"). Difference is 0.8%, kept exact anyway.
# Dict key stays "right_turn" (not renamed to "left_turn"): it matches the
# real-hardware log filename f7_realwheel_right_turn.jsonl (test_results/,
# pre-existing artifact from before the 2026-08-04 scenario rename).
REALWHEEL_WHEEL_BASE = {"basic": 3.024, "right_turn": 3.0, "tljunction": 3.0}
SIM_WHEEL_BASE = vrt.WHEEL_BASE  # 3.024 -- the vd_resume_transient.py scenario's own vehicle

# AdSteeringEnvelope.hpp kAdEnvelopeDefault{ALatMaxSteer,YawRateMax,VFloor} -- the
# kappa-safe-zone constants, needed here (team-lead 8th round) to detect when the
# NEW kappa-clamp-last safety fix pins the output at the kappa boundary.
A_LAT_MAX_STEER = 4.3   # [m/s^2]
YAW_RATE_MAX = 1.0      # [rad/s]
V_FLOOR = 1.0           # [m/s]

OUT_DIR = os.path.join(ROOT, "test_results")
OUT_TXT = os.path.join(OUT_DIR, "f7_resume_ride_feel.txt")
# team-lead correction (2026-07-26 16:16): these logs were captured under a
# PRE-FIX shadow-detector config (dead-time theta=0, tau=0, no onset grace),
# and other agents may concurrently touch the live test_results/ copies --
# read from the frozen snapshot instead. ffb.target_norm/ego.speed (what this
# script actually uses) do not depend on the detector config, so the
# normal-driving baseline itself is still valid PROVIDED no false MANUAL
# latch occurred (checked in load_realwheel_baseline() below: it would mean
# the AD gave up lateral control, so target_norm after that point would not
# be a normal AD command).
REALWHEEL_DIR = os.path.join(OUT_DIR, "f7_realwheel_frozen_20260726_1616")


# --------------------------------------------------------------------------
# generic stats / derivative helpers
# --------------------------------------------------------------------------
def _percentile(values: list, p: float) -> float:
    if not values:
        return 0.0
    s = sorted(values)
    if len(s) == 1:
        return s[0]
    idx = (p / 100.0) * (len(s) - 1)
    lo, hi = int(math.floor(idx)), int(math.ceil(idx))
    if lo == hi:
        return s[lo]
    return s[lo] + (s[hi] - s[lo]) * (idx - lo)


def _stats(values: list) -> dict:
    if not values:
        return {"n": 0, "median": 0.0, "p95": 0.0, "p99": 0.0, "max": 0.0}
    return {"n": len(values), "median": _percentile(values, 50),
            "p95": _percentile(values, 95), "p99": _percentile(values, 99),
            "max": max(values)}


def _deriv(vals: list, ts: list) -> list:
    """Simple backward-difference derivative, same convention as
    vd_resume_transient.kinematic_window_metrics's ddelta_dt_vals loop."""
    out = []
    for i in range(1, len(vals)):
        dt = ts[i] - ts[i - 1]
        if dt > 0:
            out.append((vals[i] - vals[i - 1]) / dt)
    return out


def _not_saturating_flags(vals: list, deriv_order: int, sat: float = 1.0, eps: float = 1e-6) -> list:
    """team-lead's a_lat/(1) follow-up (2026-07-26, 6th round) surfaced a THIRD
    mechanism, distinct from the manual<->auto splice: envelope_steer_out is
    hard-clamped to +-1.0 (normalized steering range) as the FINAL step in
    AdSteeringEnvelope.cpp. When a rate-limited ramp hits that physical
    ceiling, the last step gets truncated (can't go past +-1), so the RATE
    abruptly drops in that one frame -- a 2nd-derivative (jerk) computed
    across that frame reads huge (confirmed on a live case: rate went
    -2.459/s -> -0.2254/s in one frame purely because steer_out saturated at
    -1.0, giving jerk=223 despite jerk_max=25). This is an unavoidable
    property of ANY bounded ramp hitting its ceiling, not a jerk-limiter
    defect -- excluded the same way as a splice (same signature: True at
    position j iff NONE of the deriv_order+1 consecutive samples the
    derivative was computed from is pinned at +-sat)."""
    n = len(vals)
    out = []
    for j in range(n - deriv_order):
        window = vals[j:j + deriv_order + 1]
        out.append(all(abs(w) < sat - eps for w in window))
    return out


def _not_kappa_saturating_flags(applied: list, speeds: list, deriv_order: int,
                                 wheel_base: float = SIM_WHEEL_BASE, max_steer_angle: float = None,
                                 a_lat_max: float = A_LAT_MAX_STEER, yaw_rate_max: float = YAW_RATE_MAX,
                                 v_floor: float = V_FLOOR, ratio_eps: float = 0.02,
                                 kappa_out: list = None, kappa_limit: list = None) -> list:
    """team-lead 8th-round FOURTH mechanism, structurally identical to the
    +-1.0 saturation case above but at a DIFFERENT boundary: the safety fix
    just landed (kappa clamp applied LAST: `delta_final = clamp(delta_final,
    -atan(kappa_max*wb), +atan(kappa_max*wb))`) can snap an still-ramping,
    rate/jerk-limited output onto the kappa-safe boundary the instant it
    would cross it, then hold it pinned there while kappa_max itself drifts
    slowly with speed. Confirmed on a live frame (jerk_max=10, v=8):
    kappa/kappa_max ratio climbed smoothly 0.58->0.92->0.996 over 6 frames,
    then PINNED at 1.0002 for every subsequent frame -- the transition frame
    (still-ramping -> pinned) reads jerk=234.6 despite jerk_max=10, purely
    from this NEW boundary-snap, not a jerk-limiter defect. Same treatment as
    +-1.0 saturation: exclude any derivative stencil where kappa/kappa_max is
    at or past the boundary at ANY of the deriv_order+1 samples.

    WHERE THE "~1.0002" CAME FROM, AND WHY THE 2% TOLERANCE IS GONE
    ---------------------------------------------------------------
    A ratio that pins at 1.0002 is not a numerical residual of the product —
    the envelope clamps its output to the boundary exactly, so the true ratio
    pins at 1.000000. The 0.0002 was THIS FUNCTION's error: it rebuilt both
    sides of the ratio from a hard-coded wheelbase and from `ego.speed`, but
    the product derives wheel_base as boundingbox.length*0.6 and runs the clamp
    BEFORE the frame's physics integration while telemetry records speed after.
    The response at the time was `ratio_eps = 0.02` — a 2% tolerance to absorb
    a 0.02% error, i.e. a 100x margin over an artefact that was never traced.
    That is the same "widen the threshold instead of finding the cause" move
    that telemetry_golden.py made on 2026-07-04 and that hid a real engine
    nondeterminism for three weeks.

    Since 2026-07-28 the envelope publishes `kappa_out` and `kappa_limit`
    (AdSteeringEnvelope.cpp). When the caller supplies them, the ratio is
    exact and the tolerance collapses to the telemetry's own quantum. The
    derived path is kept ONLY for captures that predate those fields, and it
    keeps the loose tolerance because with a guessed wheelbase and speed
    sample nothing tighter is defensible — it says so out loud rather than
    presenting a 2% fudge as a physical threshold."""
    if max_steer_angle is None:
        max_steer_angle = MAX_STEER_ANGLE
    n = len(applied)
    # Published-value path: exact ratio, tolerance = one serialization quantum
    # of the fixed-9-decimal record, scaled by the smallest cap in play.
    have_pub = (kappa_out is not None and kappa_limit is not None
                and len(kappa_out) >= n and len(kappa_limit) >= n
                and all(isinstance(kappa_out[i], (int, float))
                        and isinstance(kappa_limit[i], (int, float))
                        and kappa_limit[i] > 0.0 for i in range(n)))
    out = []
    for j in range(n - deriv_order):
        ok = True
        for k in range(j, j + deriv_order + 1):
            if have_pub:
                ratio = abs(kappa_out[k]) / kappa_limit[k]
                eps = 1e-9 / kappa_limit[k]  # 1 quantum expressed as a ratio
            else:
                v_eff = max(speeds[k], v_floor)
                kappa_max = min(a_lat_max / (v_eff * v_eff), yaw_rate_max / v_eff)
                if kappa_max <= 0:
                    continue
                ratio = abs(math.tan(applied[k] * max_steer_angle) / wheel_base) / kappa_max
                eps = ratio_eps
            if ratio >= 1.0 - eps:
                ok = False
                break
        out.append(ok)
    return out


def _splice_free_flags(source_key: list, deriv_order: int) -> list:
    """team-lead (2026-07-26, 6th round): `source_key` is index-aligned with
    the ORIGINAL series a derivative was computed from (e.g. override_lateral,
    aligned with `ext`/`applied`). Returns a list aligned with a derivative
    series of the given order (1=rate via one _deriv call, 2=jerk via two) --
    i.e. length len(source_key)-deriv_order -- True at position j iff all
    deriv_order+1 consecutive source samples it was computed from
    (source_key[j..j+deriv_order]) are THE SAME (no manual<->auto splice
    anywhere in that stencil); False means that derivative value straddles a
    signal-source change and is not a physical rate/jerk."""
    n = len(source_key)
    out = []
    for j in range(n - deriv_order):
        window = source_key[j:j + deriv_order + 1]
        out.append(all(w == window[0] for w in window))
    return out


def _selftest_derivative_chain(jerk_cap: float = 10.0, dt: float = 0.01, n: int = 8) -> dict:
    """team-lead ask (2026-07-26 4th round, item 3): a synthetic, closed-form
    self-check of the SAME _deriv-based jerk computation analyze_resume_case
    uses (rate_env = _deriv(env_out,t); jerk_env = _deriv(rate_env,t[1:])) --
    independent of any live simulation, pure arithmetic on the derivative
    code path. Builds a rate sequence that ramps at EXACTLY jerk_cap per
    frame (rate[i] = rate[i-1] + jerk_cap*dt, rate[0]=0) and integrates it
    into a steer_norm trajectory (steer[i] = steer[i-1] + rate[i]*dt --
    matching AdSteeringEnvelope.cpp's delta_final = delta_prev +
    allowed_rate*dt relation), then verifies the SAME two-stage _deriv chain
    recovers jerk_cap (not 2x or 0.5x it, the "2-frame diff over 1-frame dt"
    team-lead suspected) at every interior sample."""
    t = [i * dt for i in range(n)]
    rate = [0.0] * n
    steer = [0.0] * n
    for i in range(1, n):
        rate[i] = rate[i - 1] + jerk_cap * dt
        steer[i] = steer[i - 1] + rate[i] * dt
    rate_computed = _deriv(steer, t)
    jerk_computed = _deriv(rate_computed, t[1:])
    ok = bool(jerk_computed) and all(abs(j - jerk_cap) < 1e-6 for j in jerk_computed)
    return {"ok": ok, "jerk_cap_in": jerk_cap, "dt": dt, "jerk_computed": jerk_computed}


@contextlib.contextmanager
def _quiet_native_stdout():
    """Redirect the OS-level fd 1 (not just sys.stdout) to NUL for the
    duration of a headless sim run -- GT_esminiLib's logging goes through the
    native C++ runtime (stdout writes below the Python layer), so
    contextlib.redirect_stdout alone would not suppress it. Restores the
    original fd unconditionally."""
    stdout_fd = sys.stdout.fileno()
    saved_fd = os.dup(stdout_fd)
    devnull_fd = os.open(os.devnull, os.O_WRONLY)
    try:
        sys.stdout.flush()
        os.dup2(devnull_fd, stdout_fd)
        yield
    finally:
        sys.stdout.flush()
        os.dup2(saved_fd, stdout_fd)
        os.close(devnull_fd)
        os.close(saved_fd)


# --------------------------------------------------------------------------
# (B) normal-driving baseline from real-hardware logs
# --------------------------------------------------------------------------
def _detect_false_latch(frames: list) -> dict:
    """team-lead instruction (2026-07-26 16:16): these hands-off recordings
    should NEVER show a MANUAL lateral latch -- if one occurred anyway (a
    false latch from the pre-fix shadow detector, dead-time/tau=0, no onset
    grace), the AD gave up lateral control at that point and target_norm
    after it is not a normal AD command; that tail must be excluded from the
    normal-driving baseline. Detected directly from override.lateral (the
    authoritative flag), not inferred from the residual gate alone (residual
    momentarily exceeding threshold does not by itself mean a latch --
    sustain_accum must also cross ffb_target_track_override_sustain_time)."""
    onset_idx = None
    prev = False
    for i, f in enumerate(frames):
        cur = bool(f["override"]["lateral"])
        if cur and not prev:
            onset_idx = i
            break
        prev = cur
    max_res = max(f["ffb"]["gates"]["residual"] for f in frames)
    return {"latched": onset_idx is not None, "onset_idx": onset_idx,
            "onset_t": frames[onset_idx]["sim_time"] if onset_idx is not None else None,
            "max_residual": max_res, "n_total": len(frames)}


def load_realwheel_baseline() -> dict:
    """Per-file and POOLED (all 3 concatenated) distributions of:
      - steer command jerk [1/s^2]           (pure wheel-space, from ffb.target_norm)
      - lateral jerk d(a_lat)/dt [m/s^3]      (OPEN-LOOP: bicycle model applied to
      - lateral snap d^2(a_lat)/dt^2 [m/s^4]   the recorded command + recorded speed)
      - a_lat [m/s^2] itself, as a sanity cross-check against the
        AdSteeringEnvelope.hpp-cited "15-scenario pool" normal-driving max
        (3.289 m/s^2) -- these 3 files are a DIFFERENT (smaller) pool, so an
        approximate match validates the wheel_base/max_steer_angle/kappa
        formula used here, not an exact-reproduction claim.
    speed distribution is also captured (used for the resume-speed-domain
    question, point 4 of the report). Reads the FROZEN snapshot
    (REALWHEEL_DIR) per team-lead instruction, and excludes any frames from
    and after a detected false MANUAL latch (see _detect_false_latch).
    """
    per_file = {}
    pool_jerk, pool_lat_jerk, pool_lat_snap, pool_a_lat, pool_speed = [], [], [], [], []
    latch_report = {}
    for name, wb in REALWHEEL_WHEEL_BASE.items():
        path = os.path.join(REALWHEEL_DIR, f"f7_realwheel_{name}.jsonl")
        with open(path, encoding="utf-8") as fh:
            all_frames = [json.loads(line) for line in fh]
        latch = _detect_false_latch(all_frames)
        latch_report[name] = latch
        frames = all_frames[:latch["onset_idx"]] if latch["latched"] else all_frames
        n_excluded = len(all_frames) - len(frames)

        t = [f["sim_time"] for f in frames]
        v = [f["ego"]["speed"] for f in frames]
        sn = [f["ffb"]["target_norm"] for f in frames]  # AD steer command (team-lead spec)
        kappa = [math.tan(x * MAX_STEER_ANGLE) / wb for x in sn]
        a_lat = [vv * vv * kk for vv, kk in zip(v, kappa)]
        rate = _deriv(sn, t)
        jerk = _deriv(rate, t[1:])
        lat_jerk = _deriv(a_lat, t)
        lat_snap = _deriv(lat_jerk, t[1:])

        abs_jerk = [abs(x) for x in jerk]
        abs_lat_jerk = [abs(x) for x in lat_jerk]
        abs_lat_snap = [abs(x) for x in lat_snap]
        abs_a_lat = [abs(x) for x in a_lat]
        per_file[name] = {
            "wheel_base": wb, "n_frames": len(frames), "n_excluded_post_latch": n_excluded,
            "speed_min": min(v), "speed_max": max(v),
            "steer_jerk": _stats(abs_jerk),
            "lateral_jerk": _stats(abs_lat_jerk),
            "lateral_snap": _stats(abs_lat_snap),
            "a_lat": _stats(abs_a_lat),
        }
        pool_jerk += abs_jerk
        pool_lat_jerk += abs_lat_jerk
        pool_lat_snap += abs_lat_snap
        pool_a_lat += abs_a_lat
        pool_speed += v

    pooled = {
        "n_frames": sum(pf["n_frames"] for pf in per_file.values()),
        "speed_min": min(pool_speed), "speed_max": max(pool_speed),
        "steer_jerk": _stats(pool_jerk),
        "lateral_jerk": _stats(pool_lat_jerk),
        "lateral_snap": _stats(pool_lat_snap),
        "a_lat": _stats(pool_a_lat),
    }
    return {"per_file": per_file, "pooled": pooled, "latch_report": latch_report}


# --------------------------------------------------------------------------
# (A) current resume transient -- closed-loop re-run of arm1
# --------------------------------------------------------------------------
def analyze_resume_case(frames: list, window_s: float = 1.0) -> dict | None:
    """Extends vd_resume_transient.compute_metrics with the quantities the
    team lead asked for that the existing function does not compute: steer
    JERK (2nd deriv) on both the raw AD command (driver_steer) and the
    post-(current)-envelope command actually handed to physics
    (envelope_steer_out), plus REALIZED (from ego heading/speed, i.e. the
    actual simulated vehicle motion -- same "layer3" convention as
    compute_metrics' yaw_rate_peak/a_lat_est_peak) lateral jerk and lateral
    snap. Also profiles the peak-so-far of steer jerk (env-out) and lateral
    snap at 0.1s/0.3s/1.0s post-edge, and the three envelope constraint
    activation rates within the window.
    """
    edge_idxs = [i for i, f in enumerate(frames) if f.get("auto_transition")]
    if not edge_idxs:
        return None
    idx0 = edge_idxs[0]
    t0 = frames[idx0]["sim_time"]
    # extended: 2 frames of pre-edge context so the derivative AT idx0 itself
    # is well-formed (a 2nd derivative needs 2 prior samples), but peaks are
    # only taken from frames wholly inside [t0, t0+window_s].
    ext_start = max(0, idx0 - 2)
    ext = [f for f in frames[ext_start:] if (f["sim_time"] - t0) <= window_s + 1e-9]
    t = [f["sim_time"] for f in ext]

    def series(key):
        return [f.get(key) for f in ext]

    # team-lead (2026-07-26, 5th round): restrict peak-taking EXPLICITLY to
    # AUTO-owned frames (override.lateral==False), not just implicitly via
    # idx0 alignment with the auto_transition edge -- defensive against any
    # off-by-one between the auto_transition flag and override.lateral
    # actually flipping. A frame with override_lateral is None (missing
    # telemetry) is conservatively treated as NOT eligible.
    override_lat_mask_src = series("override_lateral")

    def in_window_mask():
        return [(f["sim_time"] - t0) >= -1e-9 and override_lat_mask_src[i] is False
                for i, f in enumerate(ext)]

    mask = in_window_mask()

    def rel_t(i):
        return t[i] - t0

    def peak_in_window(vals_abs, idx_offset, cap_t=None, valid=None):
        """vals_abs is a derivative series (len = len(ext)-idx_offset), whose
        element j corresponds to ext[j+idx_offset]. Only counts elements whose
        ext frame is inside the window (mask), if cap_t given whose
        rel_t <= cap_t, and (team-lead 2026-07-26 6th round) if `valid` is
        given, whose stencil does not straddle a manual<->auto splice --
        see _splice_free_flags."""
        best = 0.0
        for j, val in enumerate(vals_abs):
            i = j + idx_offset
            if i >= len(mask) or not mask[i]:
                continue
            if valid is not None and (j >= len(valid) or not valid[j]):
                continue
            rt = rel_t(i)
            if cap_t is not None and rt > cap_t + 1e-9:
                continue
            best = max(best, abs(val))
        return best

    raw = series("driver_steer")
    env_out = series("envelope_steer_out")
    h = series("ego_h")
    v = series("ego_speed")
    lat_acc_act = series("envelope_lat_accel_active")
    yaw_act = series("envelope_yaw_rate_active")
    steer_rate_act = series("envelope_steer_rate_active")
    steer_jerk_act = series("envelope_steer_jerk_active")  # None throughout on pre-jerk-cap telemetry/DLLs

    if any(x is None for x in raw + env_out + h + v):
        return {"edge_found": True, "error": "missing telemetry fields in window"}

    # team-lead-diagnosed artifact (2026-07-26, 4th round): envelope_steer_out
    # is the envelope's CONTINUOUSLY-COMPUTED CANDIDATE -- it is only what is
    # actually handed to physics while override_lateral is False.  While
    # override_lateral is True, ControllerVirtualDriver overwrites
    # cmd.steering with the MANUAL input (auto_cmd.steering/envelope_steer_out
    # is computed but discarded), and it is THAT applied value --  not the
    # discarded candidate -- that feeds AdSteeringEnvelope's own rate/jerk
    # anchor (UpdateAdSteeringEnvelopeState) for the NEXT frame. A naive
    # derivative straight off envelope_steer_out therefore compares the wrong
    # "previous sample" for exactly the ONE frame after a manual->auto
    # handoff, producing an isolated ~2x jerk spike there that is a
    # MEASUREMENT artifact, not a real behavior of the limiter (traced and
    # confirmed frame-by-frame on a live run: jerk reads 50 at jerk_max=25 for
    # exactly one frame, then 25,25,25 correctly on every following frame).
    # applied_steer_norm reconstructs what was ACTUALLY handed to physics each
    # frame (manual_raw_steer during override, envelope_steer_out otherwise --
    # see run_network_arm_speed_fixed, which now records manual_raw_steer
    # alongside every captured frame) and is used below INSTEAD of env_out for
    # the steer rate/jerk peaks. Falls back to env_out (old behavior) if
    # manual_raw_steer/override_lateral are absent (frames captured before
    # this fix).
    override_lat = override_lat_mask_src  # reuse; same series computed above for the mask
    manual_raw = series("manual_raw_steer")
    if any(x is None for x in override_lat) or any(x is None for x in manual_raw):
        applied = env_out
    else:
        applied = [manual_raw[i] if override_lat[i] else env_out[i] for i in range(len(ext))]

    rate_raw = _deriv(raw, t)          # idx_offset=1 relative to ext
    jerk_raw = _deriv(rate_raw, t[1:])  # idx_offset=2

    rate_env = _deriv(applied, t)
    jerk_env = _deriv(rate_env, t[1:])

    # team-lead correction (2026-07-26, 6th round): `applied` is a SPLICE of
    # two different signals (manual_raw_steer during override, envelope
    # candidate otherwise). A derivative computed straddling the splice frame
    # is not a physical rate/jerk -- the envelope's own rate/jerk limiter
    # never sees or constrains a step between two DIFFERENT signal sources,
    # so measuring one is meaningless (confirmed: worst-case rows showed
    # steer_rate up to 22-28 /s, >10x the 2.459 physical ceiling, only
    # possible by diffing across a splice). rate_env/jerk_env stencils that
    # cross a source change are excluded from peak-taking via these flags
    # (peak_in_window's `valid` param) -- NOT by reinitializing the
    # derivative to zero/None, which would silently hide a real subsequent
    # value; excluding is stricter and auditable (steer_jerk_active_frames
    # already show the excluded count implicitly via fewer eligible samples).
    rate_env_valid = _splice_free_flags(override_lat, 1)
    jerk_env_valid_splice = _splice_free_flags(override_lat, 2)
    jerk_env_valid_sat = _not_saturating_flags(applied, 2)
    # team-lead 8th round: the just-landed kappa-clamp-last safety fix
    # introduces a FOURTH mechanism, structurally identical to the +-1.0
    # saturation case -- see _not_kappa_saturating_flags's doc. Confirmed
    # live: kappa/kappa_max ratio climbs smoothly then PINS at ~1.0002 the
    # instant the new final clamp engages, producing a jerk spike (234.6 at
    # jerk_max=10) that is the boundary-snap, not a jerk-limiter defect.
    # Pass the envelope's OWN curvature numbers when the capture carries them,
    # so the boundary test is exact instead of a 2%-tolerance approximation
    # (see _not_kappa_saturating_flags' doc).
    jerk_env_valid_kappa = _not_kappa_saturating_flags(
        applied, v, 2,
        kappa_out=series("envelope_kappa_out"),
        kappa_limit=series("envelope_kappa_limit"))
    jerk_env_valid = [a and b and c for a, b, c in
                       zip(jerk_env_valid_splice, jerk_env_valid_sat, jerk_env_valid_kappa)]

    # team-lead correction (2026-07-26, 8th round): the gating above is
    # correct (a splice-straddling derivative is not a physical rate/jerk in
    # general -- see the ROUND-4 envelope_steer_out-candidate-vs-applied
    # artifact it was built to exclude) but it ALSO excludes the manual->auto
    # handoff frame ITSELF from steer_jerk_peak_env_out -- and that frame is
    # exactly where the onset transient (the thing actually being complained
    # about: rate stepping 0 -> steer_rate_max in one frame) lives. Excluding
    # it from the gated peak is still correct (mixing it into "peak jerk
    # elsewhere in the trajectory" would conflate two different phenomena),
    # but it must not be discarded -- report it separately, UNGATED, under
    # its own name. onset_rate_step = the rate CHANGE across the handoff
    # frame (rate_at_idx0 - rate_just_before, both from the raw ungated
    # rate_env/applied series); onset_effective_jerk = that same value
    # (rate_env/jerk_env are built from the same _deriv chain, so this is
    # literally jerk_env's own first sample, just surfaced here instead of
    # silently dropped by the `valid` filter at peak-taking time).
    idx0_in_ext = next((i for i, m in enumerate(mask) if m), None)
    onset_rate_step = None
    onset_effective_jerk = None
    if idx0_in_ext is not None:
        i_rate, i_rate_prev = idx0_in_ext - 1, idx0_in_ext - 2
        if 0 <= i_rate_prev and i_rate < len(rate_env):
            onset_rate_step = rate_env[i_rate] - rate_env[i_rate_prev]
        i_jerk = idx0_in_ext - 2
        if 0 <= i_jerk < len(jerk_env):
            onset_effective_jerk = jerk_env[i_jerk]

    # kappa-boundary-clamp jerk (team-lead 8th round): the UNGATED peak among
    # frames excluded SPECIFICALLY by the new kappa-saturation gate (not also
    # a splice case), i.e. what the jerk reads at the moment the safety fix's
    # final clamp snaps the output onto the kappa boundary. Reported
    # separately for the same reason as onset_effective_jerk -- an exclusion
    # must not make the excluded quantity disappear from the report.
    kappa_clamp_max_jerk = 0.0
    kappa_clamp_n_frames = 0
    for k, val in enumerate(jerk_env):
        i = k + 2
        if i >= len(mask) or not mask[i]:
            continue
        if k < len(jerk_env_valid_splice) and jerk_env_valid_splice[k] and \
           k < len(jerk_env_valid_kappa) and not jerk_env_valid_kappa[k]:
            kappa_clamp_n_frames += 1
            kappa_clamp_max_jerk = max(kappa_clamp_max_jerk, abs(val))

    yaw_rate = []
    for i in range(1, len(h)):
        dt = t[i] - t[i - 1]
        if dt > 0:
            yaw_rate.append(vrt._wrapped_diff(h[i], h[i - 1]) / dt)
        else:
            yaw_rate.append(0.0)
    a_lat_realized = [yr * v[i + 1] for i, yr in enumerate(yaw_rate)]  # idx_offset=1
    lat_jerk_realized = _deriv(a_lat_realized, t[1:])   # idx_offset=2
    lat_snap_realized = _deriv(lat_jerk_realized, t[2:])  # idx_offset=3

    def window_vals(vals_abs, idx_offset, valid=None):
        out = []
        for j, val in enumerate(vals_abs):
            i = j + idx_offset
            if i >= len(mask) or not mask[i]:
                continue
            if valid is not None and (j >= len(valid) or not valid[j]):
                continue
            out.append(val)
        return out

    active_frames = [i for i in range(len(ext)) if mask[i]]
    n_active = len(active_frames)

    def active_rate(series_bool):
        if not n_active or all(series_bool[i] is None for i in active_frames):
            return None if all(x is None for x in series_bool) else 0.0
        cnt = sum(1 for i in active_frames if series_bool[i])
        return cnt / n_active

    peak_v_in_window = max((v[i] for i in active_frames), default=0.0)

    profiles = {}
    for T in (0.1, 0.3, 1.0):
        if T > window_s + 1e-9:
            continue
        profiles[T] = {
            "steer_jerk_env_out_peak": peak_in_window(jerk_env, 2, cap_t=T, valid=jerk_env_valid),
            "steer_jerk_raw_peak": peak_in_window(jerk_raw, 2, cap_t=T),
            "lateral_jerk_peak": peak_in_window(lat_jerk_realized, 2, cap_t=T),
            "lateral_snap_peak": peak_in_window(lat_snap_realized, 3, cap_t=T),
        }

    return {
        "edge_found": True,
        "offset_at_edge_m": frames[idx0].get("ego_offset"),
        "speed_at_edge_mps": frames[idx0].get("ego_speed"),
        "peak_speed_in_window_mps": peak_v_in_window,
        "steer_peak_raw": max((abs(x) for x in window_vals(raw, 0)), default=0.0),
        # NOTE: despite the "_env_out" key names (kept for backward compat),
        # these are now computed from `applied` (see applied_steer_norm doc
        # above) -- the reconstructed ACTUALLY-APPLIED steer series, not the
        # raw envelope_steer_out candidate.
        "steer_peak_env_out": max((abs(x) for x in window_vals(applied, 0)), default=0.0),
        "steer_rate_peak_raw": max((abs(x) for x in window_vals(rate_raw, 1)), default=0.0),
        "steer_rate_peak_env_out": max((abs(x) for x in window_vals(rate_env, 1, valid=rate_env_valid)), default=0.0),
        "steer_jerk_peak_raw": max((abs(x) for x in window_vals(jerk_raw, 2)), default=0.0),
        "steer_jerk_peak_env_out": max((abs(x) for x in window_vals(jerk_env, 2, valid=jerk_env_valid)), default=0.0),
        "yaw_rate_peak": max((abs(x) for x in window_vals(yaw_rate, 1)), default=0.0),
        "a_lat_peak": max((abs(x) for x in window_vals(a_lat_realized, 1)), default=0.0),
        "lateral_jerk_peak": max((abs(x) for x in window_vals(lat_jerk_realized, 2)), default=0.0),
        "lateral_snap_peak": max((abs(x) for x in window_vals(lat_snap_realized, 3)), default=0.0),
        "envelope_active_rate": active_rate(series("envelope_active")),
        "lateral_accel_active_rate": active_rate(lat_acc_act),
        "yaw_rate_active_rate": active_rate(yaw_act),
        "steer_rate_active_rate": active_rate(steer_rate_act),
        "steer_jerk_active_rate": active_rate(steer_jerk_act),  # None = telemetry predates this field
        "steer_jerk_active_frames": sum(1 for i in active_frames if steer_jerk_act[i]) if steer_jerk_act and not all(x is None for x in steer_jerk_act) else None,
        # UNGATED onset transition -- see comment above where these are computed.
        # Deliberately NOT mixed into steer_jerk_peak_env_out (which excludes the
        # handoff frame by design); report and interpret separately.
        "onset_rate_step": onset_rate_step,
        "onset_effective_jerk": onset_effective_jerk,
        # UNGATED kappa-boundary-clamp transition -- see comment above. Distinct
        # from onset (manual->auto handoff): this is the NEW safety fix's own
        # final-clamp boundary snap, which can occur later in the trajectory.
        "kappa_clamp_max_jerk": kappa_clamp_max_jerk,
        "kappa_clamp_n_frames": kappa_clamp_n_frames,
        "profiles": profiles,
    }


# --------------------------------------------------------------------------
# LOCAL bug workaround (see module docstring "DISCOVERED BUG"): a corrected
# copy of vd_resume_transient._make_variant / run_network_arm with the
# AccelAction-target-speed xpath fixed. Everything else is unchanged from the
# original (same phase A/B/C/D/E structure, same trigger push-out, same
# UDP/telemetry plumbing) -- diff against vd_resume_transient.py is exactly
# the one xpath line, called out below.
# --------------------------------------------------------------------------
def _make_variant_speed_fixed(tmpdir: str, cfg_overrides: dict, speed_mps: float,
                               push_triggers_out: bool = True) -> str:
    cfg_path = os.path.join(tmpdir, "virtual_driver.json")
    base = {}
    if os.path.exists(vrt.SHIPPED_CFG):
        base = json.loads(open(vrt.SHIPPED_CFG, encoding="utf-8").read())
    base.update(cfg_overrides)
    open(cfg_path, "w", encoding="utf-8").write(json.dumps(base, indent=2))

    import xml.etree.ElementTree as ET
    tree = ET.parse(vrt.BASE_XOSC)
    root = tree.getroot()

    base_dir = os.path.dirname(os.path.abspath(vrt.BASE_XOSC))
    for tag in ("LogicFile", "SceneGraphFile"):
        for el in root.findall(f".//{tag}"):
            fp = el.get("filepath")
            if fp and not os.path.isabs(fp):
                el.set("filepath", os.path.abspath(os.path.join(base_dir, fp)))
    for el in root.findall(".//CatalogLocations//Directory"):
        pth = el.get("path")
        if pth and not os.path.isabs(pth):
            el.set("path", os.path.abspath(os.path.join(base_dir, pth)))

    if push_triggers_out:
        for cond_name, new_value in (("LaneChangeStart", "500.0"), ("StopStart", "500.0"),
                                      ("QuitCondition", "600")):
            for cond in root.findall(f".//Condition[@name='{cond_name}']"):
                stc = cond.find(".//SimulationTimeCondition")
                if stc is not None:
                    stc.set("value", new_value)

    # FIX: "AccelAction" is the enclosing <Action name="AccelAction"> element's
    # NAME ATTRIBUTE, not a tag -- ".//AccelAction//AbsoluteTargetSpeed" (the
    # original harness's xpath) never matches. This predicate form does.
    n_matched = 0
    for target in root.findall(".//Action[@name='AccelAction']//AbsoluteTargetSpeed"):
        target.set("value", f"{speed_mps:.2f}")
        n_matched += 1
    if n_matched != 1:
        raise RuntimeError(f"_make_variant_speed_fixed: expected exactly 1 AccelAction "
                            f"AbsoluteTargetSpeed match, got {n_matched} -- scenario structure changed?")

    ctrl = root.find(".//ObjectController/Controller")
    if ctrl is None:
        raise RuntimeError("Could not find VirtualDriverController in base xosc")
    props = ctrl.find("Properties")
    p = ET.SubElement(props, "Property")
    p.set("name", "ConfigFile")
    p.set("value", cfg_path)

    out_xosc = os.path.join(tmpdir, "variant.xosc")
    tree.write(out_xosc, encoding="utf-8", xml_declaration=True)
    return out_xosc


def _slim_ext(frame: dict, phase: str) -> dict:
    """Extends vd_resume_transient._slim() with fields it predates/omits:
    envelope.steer_jerk_active (the C++ jerk-cap implementation),
    ffb.target_norm (team-lead's 5th-round evidence-dump request -- _slim()
    carries ffb_target_active/commanded_force/position_error but not
    target_norm itself), and ego.track/ego.s (7th-round: needed to detect
    route departure -- _slim() carries ego.lane/offset but not track/s, and
    a route re-anchor (SetPathS) can show up as a track_id or non-monotonic-s
    discontinuity even when lane/offset look superficially continuous).
    Everything else is byte-identical to _slim()'s own output."""
    s = vrt._slim(frame, phase)
    s["envelope_steer_jerk_active"] = frame.get("envelope", {}).get("steer_jerk_active")
    s["ffb_target_norm"] = frame.get("ffb", {}).get("target_norm")
    ego = frame.get("ego", {})
    s["ego_track"] = ego.get("track")
    s["ego_s"] = ego.get("s")
    return s


def run_network_arm_speed_fixed(target_offset_m: float, speed_mps: float, envelope_enabled: bool,
                                 steer_cmd: float | None = None,
                                 ramp_cap_s: float = 20.0, post_resume_s: float = 5.0,
                                 jerk_max: float | None = None, snap_max: float | None = None,
                                 dt: float = 0.01) -> list:
    """Exact copy of vd_resume_transient.run_network_arm's control flow, using
    _make_variant_speed_fixed() instead of the original's buggy variant
    builder so speed_mps actually reaches the scenario's AccelAction, and
    _slim_ext() instead of _slim() to carry the new steer_jerk_active flag.

    dt: team-lead correction (2026-07-26, 4th round) -- vd_resume_transient.py's
    own DT=0.05 (and this script's earlier hardcoded "--fixed_timestep 0.05")
    is 5x coarser than the project's documented synthetic-vs-real-machine
    convention (dt=0.01, matching the real-wheel logs). A jerk cap acts
    PER FRAME (allowed rate change = jerk_max*dt), so dt=0.05 let the last
    round's whole sweep under-constrain the limiter by 5x (confirmed: a
    measured jerk of 49.180 at steer_rate=2.4590 is EXACTLY 2.4590/0.05,
    i.e. a full rate-cap jump attributed to one 0.05s frame). This function
    now takes dt explicitly and passes the SAME value to both
    --fixed_timestep and every GT_Step() call -- vd_resume_transient.py's own
    DT constant is NEVER read here anymore (only vrt.INPUT_PORT/MAGIC/WIRE/
    BTN_AUTO_RESUME/_load_lib()/_slim() are reused). vd_resume_transient.py
    itself is NOT modified -- its own default (used by other callers) stays
    0.05.

    jerk_max: sets ad_steering_envelope_steer_jerk_max (the FLAT term) in the
    per-run config override (<=0 disables that term; None leaves the shipped
    config default untouched).

    snap_max: sets ad_steering_envelope_steer_snap_max (the SPEED-DEPENDENT
    term) in the per-run config override. IMPORTANT (found during this
    script's own smoke test, 2026-07-26 3rd round): the shipped
    GT_esmini/config/virtual_driver.json CURRENTLY carries BOTH
    ad_steering_envelope_steer_jerk_max=25.0 AND
    ad_steering_envelope_steer_snap_max=101.1 (the effective cap in
    AdSteeringEnvelope.cpp is min(jerk_max, snap_cap(v)) when snap_max>0) --
    this is DIFFERENT from what team-lead described in the 3rd-round request
    ("実装は一律 steer_jerk_max のみで確定した" -- flat-only, snap term
    dropped). If you only override jerk_max and leave snap_max at the shipped
    default, your "flat term only" test is CONFOUNDED by the still-active
    snap term (e.g. at v=8m/s, snap_cap=101.1/(8^2*0.61/3.024)=~7.85/s^2,
    MUCH tighter than a jerk_max=25 override). Pass snap_max=0 (or any <=0)
    here to force the snap term off and isolate the flat term as intended;
    None leaves the shipped config's current value (101.1) in effect."""
    import socket
    import struct
    import tempfile

    cmd = {"steering": 0.0, "throttle": 0.0, "brake": 0.0, "buttons": 0, "send": False}
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    def send_now():
        if not cmd["send"]:
            return
        pkt = vrt.WIRE.pack(vrt.MAGIC, cmd["steering"], cmd["throttle"], cmd["brake"],
                             0.0, 0, cmd["buttons"] & 0xFFFFFFFF)
        try:
            sock.sendto(pkt, ("127.0.0.1", vrt.INPUT_PORT))
        except OSError:
            pass

    tmpdir = tempfile.mkdtemp(prefix="vd_resume_ridefeel_")
    cfg = {"input_type": "network", "input_port": vrt.INPUT_PORT, "input_transport": "udp",
           "ffb_target_track_enabled": False,
           "ad_steering_envelope_enabled": envelope_enabled}
    if jerk_max is not None:
        cfg["ad_steering_envelope_steer_jerk_max"] = jerk_max
    if snap_max is not None:
        cfg["ad_steering_envelope_steer_snap_max"] = snap_max
    xosc = _make_variant_speed_fixed(tmpdir, cfg, speed_mps)

    lib = vrt._load_lib()
    argv_list = [b"vd_resume_ridefeel", b"--osc", xosc.encode(), b"--headless",
                 b"--fixed_timestep", f"{dt:.6f}".encode()]
    argv = (ctypes.c_char_p * len(argv_list))(*argv_list)
    rc = lib.GT_InitWithArgs(len(argv_list), argv)
    if rc != 0:
        raise RuntimeError(f"GT_InitWithArgs rc={rc}")

    buf = ctypes.create_string_buffer(32768)  # headroom for the 9-digit-precision telemetry bump

    def tel():
        n = lib.GT_GetVirtualDriverTelemetry(0, buf, len(buf))
        return json.loads(buf.value.decode()) if n > 0 else None

    frames: list = []

    def run(phase: str, n_steps: int):
        for _ in range(n_steps):
            send_now()
            lib.GT_Step(dt)
            f = tel()
            if f:
                slim = _slim_ext(f, phase)
                slim["manual_raw_steer"] = cmd["steering"]  # see analyze_resume_case's applied_steer_norm doc
                frames.append(slim)

    cmd.update(steering=0.0, throttle=0.0, brake=0.0, buttons=0, send=False)
    run("A_baseline", int(round(2.0 / dt)))

    if steer_cmd is None:
        steer_cmd = max(0.15, min(0.5, 0.20 + 0.05 * target_offset_m))
    offset_hard_cap = target_offset_m * 2.0 + 1.0
    cmd.update(steering=steer_cmd, throttle=0.0, brake=0.0, buttons=0, send=True)
    for _ in range(int(round(ramp_cap_s / dt))):
        send_now()
        lib.GT_Step(dt)
        f = tel()
        if f:
            slim = _slim_ext(f, "B_offset_ramp")
            slim["manual_raw_steer"] = cmd["steering"]
            frames.append(slim)
            off = abs(f["ego"]["offset"])
            if off >= offset_hard_cap:
                break
            if off >= target_offset_m:
                break

    cmd.update(steering=0.0, throttle=0.0, brake=0.0, buttons=0, send=True)
    run("C_release", int(round(0.5 / dt)))

    cmd.update(steering=0.0, throttle=0.0, brake=0.0, buttons=vrt.BTN_AUTO_RESUME, send=True)
    run("D_resume_pulse", int(round(0.4 / dt)))
    cmd.update(buttons=0)

    run("E_post_resume", int(round(post_resume_s / dt)))

    sock.close()
    lib.GT_Close()
    try:
        os.remove(xosc)
    except OSError:
        pass
    return frames


# --------------------------------------------------------------------------
# team-lead (2026-07-26, 7th round): user's actual complaint is a FULL LANE-
# WIDTH offset (~3.5m, adjacent lane), not an arbitrary target_offset_m.
# run_network_arm_speed_fixed's Phase B holds ONE constant steering direction
# the whole ramp -- confirmed empirically (check_offroute_signal.py) that at
# target=2/3/3.5m this makes the vehicle's HEADING keep rotating away from
# the road with nothing to bring it back, so releasing steering (Phase C)
# does not stop the drift -- the chassis keeps coasting in whatever direction
# it was last pointed, and offset keeps growing well past the nominal target
# (measured final |offset| 9.5-14m against targets of 2-3.5m) with NO route-
# departure log line and NO ego.track/lane-id jump (checked directly: only
# lane -3->-4 boundary crossings, s monotonic, track constant) -- so the
# earlier author's "off route" comment is a real but DIFFERENT failure mode
# than what's actually driving the runaway here; either way the existing
# Phase B cannot produce a STABLE lane-width offset.
#
# Fix: a CLOSED-LOOP "lane change" maneuver -- steer, then countersteer,
# converging to a target offset AND a heading realigned with the road,
# instead of holding one direction indefinitely. Single heading-tracking P
# loop: desired_heading_dev(t) = k_p * offset_error(t) (saturated), and
# steer_cmd chases that moving heading target. As offset_error shrinks toward
# 0 the desired heading shrinks too, pulling the real heading back to
# parallel -- naturally produces an S-curve (steer away, then countersteer
# back to straight) with NO separate "align" phase needed.
#
# Calibrated empirically (calibrate_sign.py, dt=0.01, v=8m/s, envelope ON):
# steer_cmd=+0.1 held 1s -> delta_heading=-0.061 rad, delta_offset=-0.201m --
# i.e. heading_rate ~= -C*steer_cmd with C~=0.61 rad/s per unit steer (close
# to real_vehicle_params.json's own steer_gain=0.61 -- plausible, not
# asserted as the same constant), and offset moves the SAME sign as heading
# deviation (both negative together here), consistent with offset_rate ~=
# v*(heading-h0) for small angles.
# --------------------------------------------------------------------------
_LANE_SHIFT_C_STEER = 0.61   # [rad/s per unit steer_cmd], see calibration above
_LANE_SHIFT_KP = 0.06        # [rad/m] offset-error -> desired heading deviation
_LANE_SHIFT_KH = 2.5         # [1/s] heading-tracking gain
_LANE_SHIFT_MAX_DESIRED_HEADING = 0.25  # [rad] cap on the moving heading target
_LANE_SHIFT_MAX_STEER = 0.5              # [normalized] cap on the manual steer command


def run_network_arm_lane_shift(target_offset_m: float, speed_mps: float, envelope_enabled: bool,
                                jerk_max: float | None = None, snap_max: float | None = None,
                                dt: float = 0.01, maneuver_cap_s: float = 15.0,
                                offset_tol_m: float = 0.3, heading_tol_rad: float = 0.03,
                                stable_frames_needed: int = 30, post_resume_s: float = 3.5) -> dict:
    """Same overall structure as run_network_arm_speed_fixed (A_baseline ->
    [lane-shift maneuver replaces the old B_offset_ramp] -> C_release ->
    D_resume_pulse -> E_post_resume), but Phase B is the closed-loop
    heading-tracking maneuver described above instead of one constant
    steering direction. Returns {"frames": [...], "maneuver_ok": bool,
    "maneuver_diag": {...}} -- maneuver_diag always reports what actually
    happened (final |offset|, final heading deviation, step count, whether
    it converged within maneuver_cap_s) so a failed maneuver is never
    silently reported as if it had produced a valid lane-width offset."""
    import socket
    import tempfile

    cmd = {"steering": 0.0, "throttle": 0.0, "brake": 0.0, "buttons": 0, "send": False}
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    def send_now():
        if not cmd["send"]:
            return
        pkt = vrt.WIRE.pack(vrt.MAGIC, cmd["steering"], cmd["throttle"], cmd["brake"],
                             0.0, 0, cmd["buttons"] & 0xFFFFFFFF)
        try:
            sock.sendto(pkt, ("127.0.0.1", vrt.INPUT_PORT))
        except OSError:
            pass

    tmpdir = tempfile.mkdtemp(prefix="vd_resume_laneshift_")
    cfg = {"input_type": "network", "input_port": vrt.INPUT_PORT, "input_transport": "udp",
           "ffb_target_track_enabled": False,
           "ad_steering_envelope_enabled": envelope_enabled}
    if jerk_max is not None:
        cfg["ad_steering_envelope_steer_jerk_max"] = jerk_max
    if snap_max is not None:
        cfg["ad_steering_envelope_steer_snap_max"] = snap_max
    xosc = _make_variant_speed_fixed(tmpdir, cfg, speed_mps)

    lib = vrt._load_lib()
    argv_list = [b"vd_resume_laneshift", b"--osc", xosc.encode(), b"--headless",
                 b"--fixed_timestep", f"{dt:.6f}".encode()]
    argv = (ctypes.c_char_p * len(argv_list))(*argv_list)
    rc = lib.GT_InitWithArgs(len(argv_list), argv)
    if rc != 0:
        raise RuntimeError(f"GT_InitWithArgs rc={rc}")

    buf = ctypes.create_string_buffer(32768)

    def tel():
        n = lib.GT_GetVirtualDriverTelemetry(0, buf, len(buf))
        return json.loads(buf.value.decode()) if n > 0 else None

    frames: list = []

    def run(phase: str, n_steps: int):
        for _ in range(n_steps):
            send_now()
            lib.GT_Step(dt)
            f = tel()
            if f:
                slim = _slim_ext(f, phase)
                slim["manual_raw_steer"] = cmd["steering"]
                frames.append(slim)

    cmd.update(steering=0.0, throttle=0.0, brake=0.0, buttons=0, send=False)
    run("A_baseline", int(round(2.0 / dt)))

    # sign convention (calibrated): the target's SIGN is taken from whichever
    # direction positive steer_cmd actually pushes offset -- negative, per
    # calibrate_sign.py -- so a positive target_offset_m argument here means
    # "reach an offset of target_offset_m in that (empirically negative)
    # direction," matching run_network_arm_speed_fixed's own abs()-based
    # target_offset_m convention (sign-agnostic from the caller's point of
    # view; this function picks the same physical direction internally).
    target_signed = -abs(target_offset_m)

    h0 = frames[-1]["ego_h"] if frames else 0.0
    maneuver_steps = int(round(maneuver_cap_s / dt))
    converged = False
    stable_count = 0
    last_offset = None
    last_heading_dev = None
    steps_used = 0
    for _step_idx in range(maneuver_steps):
        steps_used = _step_idx + 1
        f = tel()
        if f is None:
            send_now()
            lib.GT_Step(dt)
            continue
        offset = f["ego"]["offset"]
        heading = f["ego"]["h"]
        offset_error = target_signed - offset
        heading_dev = vrt._wrapped_diff(heading, h0)
        desired_heading_dev = max(-_LANE_SHIFT_MAX_DESIRED_HEADING,
                                   min(_LANE_SHIFT_MAX_DESIRED_HEADING, _LANE_SHIFT_KP * offset_error))
        heading_tracking_error = heading_dev - desired_heading_dev
        steer = (_LANE_SHIFT_KH / _LANE_SHIFT_C_STEER) * heading_tracking_error
        steer = max(-_LANE_SHIFT_MAX_STEER, min(_LANE_SHIFT_MAX_STEER, steer))
        cmd.update(steering=steer, throttle=0.0, brake=0.0, buttons=0, send=True)

        send_now()
        lib.GT_Step(dt)
        f2 = tel()
        if f2:
            slim = _slim_ext(f2, "B_lane_shift")
            slim["manual_raw_steer"] = cmd["steering"]
            frames.append(slim)
            last_offset = f2["ego"]["offset"]
            last_heading_dev = vrt._wrapped_diff(f2["ego"]["h"], h0)

        if last_offset is not None and last_heading_dev is not None:
            if abs(abs(last_offset) - abs(target_signed)) <= offset_tol_m and abs(last_heading_dev) <= heading_tol_rad:
                stable_count += 1
                if stable_count >= stable_frames_needed:
                    converged = True
                    break
            else:
                stable_count = 0

    maneuver_diag = {
        "converged": converged,
        "steps_used": steps_used,
        "final_offset_m": last_offset,
        "final_heading_dev_rad": last_heading_dev,
        "target_offset_m": target_offset_m,
        "offset_tol_m": offset_tol_m,
        "heading_tol_rad": heading_tol_rad,
    }

    cmd.update(steering=0.0, throttle=0.0, brake=0.0, buttons=0, send=True)
    run("C_release", int(round(0.5 / dt)))

    cmd.update(steering=0.0, throttle=0.0, brake=0.0, buttons=vrt.BTN_AUTO_RESUME, send=True)
    run("D_resume_pulse", int(round(0.4 / dt)))
    cmd.update(buttons=0)

    run("E_post_resume", int(round(post_resume_s / dt)))

    sock.close()
    lib.GT_Close()
    try:
        os.remove(xosc)
    except OSError:
        pass
    return {"frames": frames, "maneuver_ok": converged, "maneuver_diag": maneuver_diag}


def _route_departure_check(frames: list) -> dict:
    """team-lead (2026-07-26, 7th round) request #1: detect route departure
    directly from telemetry -- ego.track (road_id) changing, ego.s jumping
    non-monotonically/discontinuously (>2m in one dt=0.01 frame, i.e.
    >200m/s, unreasonable for any of these speeds), or an ego.lane value
    outside the {-3 (route), -2, -4} band (route lane and its two immediate
    neighbors -- anything further is not "adjacent lane", it is off in some
    unrelated lane)."""
    tracks = [f.get("ego_track") for f in frames]
    lanes = [f.get("ego_lane") for f in frames]
    ss = [f.get("ego_s") for f in frames]
    track_changes = [(i, tracks[i - 1], tracks[i]) for i in range(1, len(tracks))
                      if tracks[i] is not None and tracks[i - 1] is not None and tracks[i] != tracks[i - 1]]
    s_jumps = [(i, ss[i - 1], ss[i]) for i in range(1, len(ss))
               if ss[i - 1] is not None and ss[i] is not None and abs(ss[i] - ss[i - 1]) > 2.0]
    out_of_band_lanes = sorted({l for l in lanes if l is not None and l not in (-2, -3, -4)})
    departed = bool(track_changes) or bool(s_jumps) or bool(out_of_band_lanes)
    return {"departed": departed, "track_changes": track_changes, "s_jumps": s_jumps,
            "out_of_band_lanes": out_of_band_lanes, "lane_values_seen": sorted({l for l in lanes if l is not None})}


def run_lane_offset_resume_report(speeds: list, jerk_max_values: list, target_offset_m: float = 3.5,
                                   snap_max: float = 0.0, dt: float = 0.01, maneuver_cap_s: float = 15.0,
                                   conv_window_s: float = 10.0, window_s: float = 1.0) -> dict:
    """team-lead (2026-07-26, 7th/8th round): the user's actual complaint is a
    FULL LANE-WIDTH offset (adjacent lane, ~3.5m) resume, not an arbitrary
    target_offset_m severity. Uses run_network_arm_lane_shift (closed-loop
    steer+countersteer) instead of run_network_arm_speed_fixed. For every
    (speed, jerk_max) cell: verifies the maneuver converged AND stayed
    on-route before trusting ANY number, then reports peak lateral accel/yaw
    rate/steer rate/steer jerk (window_s), the ONSET transition
    (onset_rate_step/onset_effective_jerk -- UNGATED, see analyze_resume_case;
    team-lead's 8th-round catch that the gated steer_jerk_peak excludes the
    handoff frame by design), steer_jerk_active firing counts (both windowed
    and full-run/by-phase), the FULL offset trajectory, overshoot, and
    time-to-resume (vrt.compute_metrics over conv_window_s). jerk_max_values[0]
    is the baseline (team-lead: default is now 0/disabled, pass explicitly,
    never rely on the shipped default) -- every other value's row carries a
    diff_vs_baseline block. Self-check: steer_rate_peak never exceeds
    STEER_RATE_CAP_NORM, and (non-baseline only) steer_jerk_peak never exceeds
    its own jerk_max -- verified per cell, not assumed from one smoke case."""
    PEAK_KEYS = ("a_lat_peak", "yaw_rate_peak", "steer_rate_peak", "steer_jerk_peak", "offset_converge_s")
    results = []
    for speed in speeds:
        baseline = None
        for jm in jerk_max_values:
            print(f"  lane-shift + resume: target_offset={target_offset_m:g}m speed={speed:g}m/s "
                  f"jerk_max={jm:g} snap_max={snap_max:g} ...", flush=True)
            with _quiet_native_stdout():
                result = run_network_arm_lane_shift(target_offset_m, speed, True, jerk_max=jm, snap_max=snap_max,
                                                     dt=dt, maneuver_cap_s=maneuver_cap_s,
                                                     post_resume_s=conv_window_s + 0.5)
            frames, diag = result["frames"], result["maneuver_diag"]
            row = {"speed": speed, "jerk_max": jm, "maneuver_ok": result["maneuver_ok"], "maneuver_diag": diag}
            if not result["maneuver_ok"]:
                row["error"] = f"maneuver did not converge within {maneuver_cap_s}s -- excluded, not measured"
                results.append(row)
                continue
            route_check = _route_departure_check(frames)
            row["route_check"] = route_check
            if route_check["departed"]:
                row["error"] = "route departure detected -- excluded, not measured"
                results.append(row)
                continue

            m1s = analyze_resume_case(frames, window_s=window_s)
            if not m1s or not m1s.get("edge_found") or "error" in m1s:
                row["error"] = "no auto_transition edge / missing telemetry -- excluded"
                results.append(row)
                continue
            mconv = vrt.compute_metrics(frames, window_s=conv_window_s)

            rate_ok = m1s["steer_rate_peak_env_out"] <= STEER_RATE_CAP_NORM + 1e-3
            jerk_ok = True if jm <= 0 else (m1s["steer_jerk_peak_env_out"] <= jm + 1e-3)
            row["self_check_rate_ok"] = rate_ok
            row["self_check_jerk_ok"] = jerk_ok

            full_run_active = [f.get("envelope_steer_jerk_active") for f in frames]
            by_phase = {}
            for f in frames:
                ph = f["phase"]
                by_phase.setdefault(ph, [0, 0])
                by_phase[ph][0] += 1
                if f.get("envelope_steer_jerk_active"):
                    by_phase[ph][1] += 1

            edge_idxs = [i for i, f in enumerate(frames) if f.get("auto_transition")]
            idx0 = edge_idxs[0]
            t0 = frames[idx0]["sim_time"]
            offset_at_edge = frames[idx0].get("ego_offset")
            speed_at_edge = frames[idx0].get("ego_speed")
            traj = []
            next_t = 0.0
            for f in frames[idx0:]:
                rel = f["sim_time"] - t0
                if rel > conv_window_s + 1e-9:
                    break
                if rel >= next_t - 1e-6:
                    traj.append((round(rel, 2), round(f["ego_offset"], 4)))
                    next_t += 0.1

            row.update({
                "offset_at_edge_m": offset_at_edge, "speed_at_edge_mps": speed_at_edge,
                "a_lat_peak": m1s["a_lat_peak"], "yaw_rate_peak": m1s["yaw_rate_peak"],
                "steer_rate_peak": m1s["steer_rate_peak_env_out"], "steer_jerk_peak": m1s["steer_jerk_peak_env_out"],
                "onset_rate_step": m1s["onset_rate_step"], "onset_effective_jerk": m1s["onset_effective_jerk"],
                "kappa_clamp_max_jerk": m1s["kappa_clamp_max_jerk"], "kappa_clamp_n_frames": m1s["kappa_clamp_n_frames"],
                "offset_overshoot_m": mconv.get("offset_overshoot_m"),
                "offset_converge_s": mconv.get("offset_converge_s"),
                "steer_jerk_active_frames_window": m1s["steer_jerk_active_frames"],
                "steer_jerk_active_frames_full_run": sum(1 for x in full_run_active if x) if not all(x is None for x in full_run_active) else None,
                "steer_jerk_active_by_phase": by_phase,
                "trajectory": traj,
            })
            if jm == jerk_max_values[0]:
                baseline = row
            elif baseline is not None and "error" not in baseline:
                row["diff_vs_baseline"] = {k: row[k] - baseline[k] for k in PEAK_KEYS
                                            if row.get(k) is not None and baseline.get(k) is not None}
            results.append(row)
    return {"results": results}


def sweep_current_resume_transient(targets: list, speeds: list, window_s: float = 1.0) -> list:
    """Runs arm1 (network, envelope_enabled=True, no jerk cap -- current
    shipped default) across a target-offset x speed grid, in-process,
    headless, using the speed-fixed local runner (see module docstring).
    Returns a flat list of per-case result dicts."""
    results = []
    n = len(targets) * len(speeds)
    k = 0
    for target in targets:
        for speed in speeds:
            k += 1
            print(f"  [{k}/{n}] arm1 target={target:g}m speed={speed:g}m/s ...", flush=True)
            with _quiet_native_stdout():
                frames = run_network_arm_speed_fixed(target, speed, True, post_resume_s=max(1.5, window_s + 0.5))
            m = analyze_resume_case(frames, window_s=window_s)
            results.append({"target": target, "speed_cmd": speed, "metrics": m})
    return results


# --------------------------------------------------------------------------
# (3) required jerk cap -- speed-dependent (lateral-snap-preserving) and flat
# --------------------------------------------------------------------------
def required_cap_table(snap_target: float, speeds: list, wheel_base: float = SIM_WHEEL_BASE) -> dict:
    """steer_jerk_cap(v) = snap_target / (v^2 * C), C = max_steer_angle / wheel_base.
    Derivation (small-angle, open-loop, constant-v during the transient):
      delta = steer_norm * max_steer_angle
      kappa ~= delta / wheel_base = steer_norm * C,          C = max_steer_angle / wheel_base [1/m]
      a_lat = v^2 * kappa = v^2 * C * steer_norm
      d(a_lat)/dt   = v^2 * C * steer_rate            [m/s^3]  (lateral jerk)
      d^2(a_lat)/dt^2 = v^2 * C * steer_jerk           [m/s^4]  (lateral snap)
    => steer_jerk_cap(v) = snap_target / (v^2 * C)
    This is an OPEN-LOOP inversion (ignores dv/dt during the transient and
    the tan() nonlinearity used elsewhere for consistency with the harness --
    acceptable here since it is being SOLVED FOR the cap, not measuring one).
    """
    C = MAX_STEER_ANGLE / wheel_base
    return {"C_per_m": C, "wheel_base": wheel_base,
            "rows": [{"speed_mps": v, "steer_jerk_cap_per_s2": snap_target / (v * v * C)} for v in speeds]}


def evaluate_uniform_cap(cap: float, speeds: list, wheel_base: float = SIM_WHEEL_BASE) -> dict:
    """OPEN-LOOP: predicted lateral snap if the AD command's steer jerk maxes
    out at `cap` [1/s^2] continuously at speed v: predicted_snap(v) = cap * v^2 * C."""
    C = MAX_STEER_ANGLE / wheel_base
    return {"C_per_m": C, "wheel_base": wheel_base, "cap": cap,
            "rows": [{"speed_mps": v, "predicted_lateral_snap_mps4": cap * v * v * C} for v in speeds]}


# --------------------------------------------------------------------------
# PM sharpened scope (2026-07-26): central question is now "how much does a
# 25/s^2 steer-jerk cap smooth the RESUME TRANSIENT itself" (not general
# driving). No-cap side is CLOSED-LOOP (current build, real physics). The
# 25/s^2 side does not exist in code yet, so it is built OPEN-LOOP: a jerk-
# limiting filter is applied post-hoc to the CLOSED-LOOP run's own recorded
# envelope_steer_out (the command the current, no-jerk-cap system actually
# applies), then a simple kinematic-bicycle integrator re-derives vehicle
# motion (a_lat/lateral jerk/snap) and a lateral-offset trajectory from that
# filtered command. Every quantity below this point that comes from that
# re-derivation is labeled OPEN-LOOP; everything from the closed-loop run
# itself (steer command as actually applied, a_lat/yaw-rate from the ACTUAL
# simulated ego heading, offset from ACTUAL simulated ego position) is
# labeled CLOSED-LOOP.
# --------------------------------------------------------------------------
STEER_RATE_CAP_NORM = 1.5 / MAX_STEER_ANGLE  # existing envelope's steer_rate_max, normalized-steer units [1/s]


def _deriv_padded(vals: list, ts: list) -> list:
    """Same relation as _deriv, but INDEX-ALIGNED / same length as vals (element
    0 = 0.0 placeholder, element i>=1 = (vals[i]-vals[i-1])/(ts[i]-ts[i-1])) --
    simpler to window/mask by matching index than the offset-tracked _deriv,
    used only in this later paired-case section."""
    out = [0.0] * len(vals)
    for i in range(1, len(vals)):
        dt = ts[i] - ts[i - 1]
        if dt > 0:
            out[i] = (vals[i] - vals[i - 1]) / dt
    return out


def jerk_limit_series(target: list, t: list, jerk_cap: float, rate_cap: float) -> dict:
    """OPEN-LOOP construction of "what a steer-jerk<=jerk_cap limiter would
    have produced" if layered AFTER the current envelope, using the current
    (no-jerk-cap) envelope_steer_out trajectory as the reference/target: a
    discrete jerk-limited slew -- filtered_rate chases target's own
    instantaneous rate as fast as |d(filtered_rate)/dt| <= jerk_cap allows
    (clamped to +-rate_cap); filtered_steer integrates filtered_rate.
    filtered_steer[0]/filtered_rate[0] are seeded at (target[0], 0.0) --
    i.e. starting from rest at the extended window's first (pre-edge)
    sample, a reasonable assumption since the pre-edge release phase runs
    at a near-flat command.
    """
    n = len(target)
    filtered_steer = [target[0]] * n
    filtered_rate = [0.0] * n
    target_rate = [0.0] * n
    for i in range(1, n):
        dt = t[i] - t[i - 1]
        if dt <= 0:
            filtered_steer[i] = filtered_steer[i - 1]
            filtered_rate[i] = filtered_rate[i - 1]
            continue
        target_rate[i] = (target[i] - target[i - 1]) / dt
        desired = max(-rate_cap, min(rate_cap, target_rate[i]))
        max_delta = jerk_cap * dt
        delta = max(-max_delta, min(max_delta, desired - filtered_rate[i - 1]))
        filtered_rate[i] = filtered_rate[i - 1] + delta
        filtered_steer[i] = filtered_steer[i - 1] + filtered_rate[i] * dt
    filtered_jerk = _deriv_padded(filtered_rate, t)
    peak_rate = max((abs(r) for r in filtered_rate), default=0.0)
    reach99_t = None
    if peak_rate > 1e-9:
        for i in range(n):
            if abs(filtered_rate[i]) >= 0.99 * peak_rate:
                reach99_t = t[i] - t[0]
                break
    return {"steer": filtered_steer, "rate": filtered_rate, "jerk": filtered_jerk,
            "target_rate": target_rate, "peak_rate": peak_rate, "reach_99pct_t": reach99_t}


def integrate_open_loop_kinematics(steer: list, v: list, t: list, wheel_base: float, max_steer_angle: float,
                                    heading0: float, road_heading_ref: float, offset0: float) -> dict:
    """OPEN-LOOP kinematic-bicycle re-derivation of heading/lateral-offset/
    a_lat from a steer_norm command series + a RECORDED speed trace (does not
    re-run physics; v(t) is taken as given from the closed-loop run, i.e.
    steering is assumed not to perturb the recorded forward-speed profile --
    reasonable for a lateral-only correction over ~1-3s). Same delta/kappa
    relation as vd_resume_transient.kinematic_window_metrics
    (delta=steer*max_steer_angle, kappa=tan(delta)/wheel_base). Lateral
    offset integrated via d(offset)/dt = v*sin(heading-road_heading_ref)
    (locally-straight-road approximation, same as the harness's own
    _road_heading_ref convention)."""
    n = len(steer)
    heading = [heading0] * n
    offset = [offset0] * n
    a_lat = [0.0] * n
    for i in range(n):
        delta = steer[i] * max_steer_angle
        kappa = math.tan(delta) / wheel_base
        a_lat[i] = v[i] * v[i] * kappa
        if i > 0:
            dt = t[i] - t[i - 1]
            yaw_rate = v[i] * kappa
            heading[i] = heading[i - 1] + yaw_rate * dt
            herr = vrt._wrapped_diff(heading[i], road_heading_ref)
            offset[i] = offset[i - 1] + v[i] * math.sin(herr) * dt
    lateral_jerk = _deriv_padded(a_lat, t)
    lateral_snap = _deriv_padded(lateral_jerk, t)
    return {"heading": heading, "offset": offset, "a_lat": a_lat,
            "lateral_jerk": lateral_jerk, "lateral_snap": lateral_snap}


def paired_case_analysis(target: float, speed: float, jerk_cap: float = 25.0,
                          window_s: float = 1.0, conv_window_s: float = 3.0) -> dict:
    """Runs ONE arm1 case closed-loop (current build, no jerk cap), then
    derives the paired CLOSED-LOOP (mode A, current) vs OPEN-LOOP (mode B,
    jerk_cap) comparison the PM asked for: steer rate/jerk profile+peak,
    a_lat/lateral-jerk/lateral-snap peak (both over the first window_s), plus
    lateral offset overshoot/convergence and rate-cap saturation timing (both
    over conv_window_s, since convergence can take longer than window_s)."""
    with _quiet_native_stdout():
        frames = run_network_arm_speed_fixed(target, speed, True, post_resume_s=conv_window_s + 0.5)

    modeA_1s = analyze_resume_case(frames, window_s=window_s)
    if not modeA_1s or not modeA_1s.get("edge_found") or "error" in modeA_1s:
        return {"target": target, "speed_cmd": speed, "error": "no edge / missing telemetry (mode A)"}
    modeA_conv = vrt.compute_metrics(frames, window_s=conv_window_s)

    edge_idxs = [i for i, f in enumerate(frames) if f.get("auto_transition")]
    idx0 = edge_idxs[0]
    t0 = frames[idx0]["sim_time"]
    ext_start = max(0, idx0 - 2)
    ext = [f for f in frames[ext_start:] if (f["sim_time"] - t0) <= conv_window_s + 1e-9]
    t = [f["sim_time"] for f in ext]
    v = [f.get("ego_speed") for f in ext]
    env_out = [f.get("envelope_steer_out") for f in ext]
    h0 = ext[0].get("ego_h")
    off0 = ext[0].get("ego_offset")
    if any(x is None for x in env_out + v) or h0 is None or off0 is None:
        return {"target": target, "speed_cmd": speed, "error": "missing telemetry fields in extended window"}

    road_ref = vrt._road_heading_ref(frames, idx0)
    filt = jerk_limit_series(env_out, t, jerk_cap, STEER_RATE_CAP_NORM)
    kin_capped = integrate_open_loop_kinematics(filt["steer"], v, t, SIM_WHEEL_BASE, MAX_STEER_ANGLE,
                                                 h0, road_ref, off0)
    # calibration check: replay the SAME open-loop integrator on the UNFILTERED
    # command, to see how well this open-loop re-derivation (which necessarily
    # ignores vehicle actuator/tire dynamics that the closed-loop physics has)
    # tracks the REAL closed-loop offset trajectory -- bounds how much to
    # trust the filtered (25/s^2) column's offset numbers.
    kin_replica = integrate_open_loop_kinematics(env_out, v, t, SIM_WHEEL_BASE, MAX_STEER_ANGLE,
                                                  h0, road_ref, off0)

    def window_mask(T):
        return [0 <= (tt - t0) <= T + 1e-9 for tt in t]

    def peak(series, T):
        mask = window_mask(T)
        return max((abs(x) for x, m in zip(series, mask) if m), default=0.0)

    real_offset_series = [f.get("ego_offset") for f in ext]

    def value_at(series, T):
        for x, tt in zip(series, t):
            if (tt - t0) >= T - 1e-9:
                return x
        return series[-1] if series else None

    real_off_1s = value_at(real_offset_series, window_s)
    replica_off_1s = value_at(kin_replica["offset"], window_s)
    calib_offset_err_1s = (replica_off_1s - real_off_1s) if (real_off_1s is not None and replica_off_1s is not None) else None
    calib_conv_replica_s = None
    for off, tt in zip(kin_replica["offset"], t):
        if abs(off) < 0.3 and tt >= t0:
            calib_conv_replica_s = tt - t0
            break

    # scoped to the SAME 1.0s window as modeA_1s's own steer_rate_peak, for an
    # apples-to-apples "target rate being chased" comparison (an earlier cut
    # of this script compared against the full conv_window_s peak instead,
    # which could be a later/unrelated spike outside the 1.0s window).
    peak_target_rate = peak(filt["target_rate"], window_s)

    # Offset/position reconstruction note: unlike a_lat/lateral-jerk/snap
    # (LOCAL functions of the bounded filtered_steer value, which itself
    # cannot run away since both its rate and its chase target are bounded),
    # offset requires DOUBLE-INTEGRATING heading with NO feedback loop -- an
    # open-loop replay of a recorded (originally closed-loop-generated) steer
    # trajectory has nothing pulling heading back toward the route, so small
    # per-step differences compound over the window. The calibration check
    # below (replaying the UNFILTERED command through this same integrator)
    # quantifies exactly how bad that compounding is; when it is large this
    # script does NOT report mode B offset numbers as credible.
    calib_ok = calib_offset_err_1s is not None and abs(calib_offset_err_1s) <= 0.5

    return {
        "target": target, "speed_cmd": speed, "offset_at_edge": off0, "speed_at_edge": frames[idx0].get("ego_speed"),
        "modeA_closed_loop": {
            "steer_jerk_peak_1s": modeA_1s["steer_jerk_peak_env_out"],
            "steer_rate_peak_1s": modeA_1s["steer_rate_peak_env_out"],
            "a_lat_peak_1s": modeA_1s["a_lat_peak"],
            "lateral_jerk_peak_1s": modeA_1s["lateral_jerk_peak"],
            "lateral_snap_peak_1s": modeA_1s["lateral_snap_peak"],
            "offset_overshoot_m": modeA_conv.get("offset_overshoot_m"),
            "offset_converge_s": modeA_conv.get("offset_converge_s"),
            "rate_reach_dt_s": t[3] - t[2] if len(t) > 3 else (t[1] - t[0] if len(t) > 1 else None),
        },
        "modeB_openloop_25": {
            "steer_jerk_peak_1s": peak(filt["jerk"], window_s),
            "steer_rate_peak_1s": peak(filt["rate"], window_s),
            "a_lat_peak_1s": peak(kin_capped["a_lat"], window_s),
            "lateral_jerk_peak_1s": peak(kin_capped["lateral_jerk"], window_s),
            "lateral_snap_peak_1s": peak(kin_capped["lateral_snap"], window_s),
            # offset numbers deliberately NOT surfaced here -- see calib_ok /
            # module note above; the (unreliable) raw values are kept under
            # "_unreliable_offset_debug" only for anyone who wants to inspect them.
            "_unreliable_offset_debug": {
                "overshoot_m": max((abs(o) for o, tt in zip(kin_capped["offset"], t) if tt >= t0), default=0.0),
                "converge_s": next((tt - t0 for o, tt in zip(kin_capped["offset"], t)
                                     if tt >= t0 and abs(o) < 0.3), None),
            },
            "peak_target_rate": peak_target_rate,
            "rate_reach99_dt_s": filt["reach_99pct_t"],
            "rate_reach99_formula_s": peak_target_rate / jerk_cap if jerk_cap > 0 else None,
        },
        "calibration": {
            "offset_err_at_1s_m": calib_offset_err_1s,
            "calib_ok": calib_ok,
            "real_offset_converge_s": modeA_conv.get("offset_converge_s"),
            "replica_offset_converge_s": calib_conv_replica_s,
        },
    }


# --------------------------------------------------------------------------
# team-lead (2026-07-26, after the C++ jerk-cap landed): the open-loop
# machinery above (jerk_limit_series / integrate_open_loop_kinematics /
# paired_case_analysis) is NO LONGER NEEDED for the primary comparison --
# ad_steering_envelope_steer_jerk_max is now a real config knob
# (AdSteeringEnvelope.hpp: <=0 disables the jerk stage, bit-identical to
# pre-jerk-cap behavior; default 25.0) and envelope.steer_jerk_active is real
# telemetry (VirtualDriverTelemetryJson.cpp), so B can be measured CLOSED-LOOP
# like A. Kept above for reference/history only -- not called by main().
# --------------------------------------------------------------------------
def closed_loop_case(target: float, speed: float, jerk_max: float, snap_max: float | None = None,
                      window_s: float = 1.0, conv_window_s: float = 3.0, dt: float = 0.01) -> dict:
    """ONE closed-loop arm1 run at a given ad_steering_envelope_steer_jerk_max
    (jerk_max<=0 => jerk stage disabled, i.e. today's shipped no-cap
    behavior). Returns the same peak quantities as analyze_resume_case (over
    window_s) PLUS -- now legitimately, since this is real physics, not an
    open-loop replay -- offset overshoot/convergence (over conv_window_s, via
    vrt.compute_metrics) and the steer_jerk_active duty-cycle, BOTH windowed
    (post-resume only) and full-run (all phases A-E) -- see
    run_network_arm_speed_fixed's snap_max doc for why the full-run count
    matters: a smoke-test run found steer_jerk_active firing heavily during
    the manual offset-ramp/release phases (B/C) while showing 0% in the
    post-resume window, even though the post-resume PEAK metric still moved
    -- the windowed rate alone does not tell the whole story here."""
    with _quiet_native_stdout():
        frames = run_network_arm_speed_fixed(target, speed, True, post_resume_s=conv_window_s + 0.5,
                                              jerk_max=jerk_max, snap_max=snap_max, dt=dt)
    m1s = analyze_resume_case(frames, window_s=window_s)
    if not m1s or not m1s.get("edge_found") or "error" in m1s:
        return {"target": target, "speed_cmd": speed, "jerk_max": jerk_max,
                "error": "no edge / missing telemetry"}
    mconv = vrt.compute_metrics(frames, window_s=conv_window_s)
    full_run_active = [f.get("envelope_steer_jerk_active") for f in frames]
    by_phase = {}
    for f in frames:
        ph = f["phase"]
        by_phase.setdefault(ph, [0, 0])
        by_phase[ph][0] += 1
        if f.get("envelope_steer_jerk_active"):
            by_phase[ph][1] += 1
    return {
        "target": target, "speed_cmd": speed, "jerk_max": jerk_max, "snap_max": snap_max,
        "offset_at_edge_m": m1s["offset_at_edge_m"], "speed_at_edge_mps": m1s["speed_at_edge_mps"],
        "steer_rate_peak": m1s["steer_rate_peak_env_out"],
        "steer_jerk_peak": m1s["steer_jerk_peak_env_out"],
        "a_lat_peak": m1s["a_lat_peak"],
        "lateral_jerk_peak": m1s["lateral_jerk_peak"],
        "lateral_snap_peak": m1s["lateral_snap_peak"],
        "offset_overshoot_m": mconv.get("offset_overshoot_m"),
        "offset_converge_s": mconv.get("offset_converge_s"),
        "steer_jerk_active_rate": m1s["steer_jerk_active_rate"],
        "steer_jerk_active_frames": m1s["steer_jerk_active_frames"],
        "steer_jerk_active_frames_full_run": sum(1 for x in full_run_active if x) if not all(x is None for x in full_run_active) else None,
        "steer_jerk_active_by_phase": by_phase,
    }


def closed_loop_jerk_grid(targets: list, speeds: list, jerk_max_values: list, snap_max: float | None = None,
                           window_s: float = 1.0, conv_window_s: float = 3.0, dt: float = 0.01) -> list:
    """Full CLOSED-LOOP target x speed x jerk_max grid. jerk_max_values[0] is
    treated as the baseline (team-lead spec: pass 0 first, e.g. [0,10,25,50])
    -- every other value's row carries a diff-vs-baseline block, with
    is_regression flags for the ride-feel peak metrics (True = this jerk_max
    made that metric WORSE than the baseline, not better). snap_max is a
    SINGLE value applied to every case in the grid (None = leave the shipped
    config's ad_steering_envelope_steer_snap_max, currently 101.1, in effect
    -- see run_network_arm_speed_fixed's snap_max doc; pass 0 to isolate the
    flat jerk_max term as team-lead's stated design intends)."""
    PEAK_KEYS = ("steer_jerk_peak", "a_lat_peak", "lateral_jerk_peak", "lateral_snap_peak")
    rows = []
    n = len(targets) * len(speeds) * len(jerk_max_values)
    k = 0
    for target in targets:
        for speed in speeds:
            baseline = None
            for jm in jerk_max_values:
                k += 1
                print(f"  [{k}/{n}] target={target:g}m speed={speed:g}m/s jerk_max={jm:g} "
                      f"snap_max={'shipped-default' if snap_max is None else snap_max} ...", flush=True)
                r = closed_loop_case(target, speed, jm, snap_max=snap_max, window_s=window_s,
                                      conv_window_s=conv_window_s, dt=dt)
                if jm == jerk_max_values[0]:
                    baseline = r
                if baseline is not None and "error" not in r and "error" not in baseline:
                    r["diff_vs_baseline"] = {}
                    r["regression"] = {}
                    for key in PEAK_KEYS:
                        d = r[key] - baseline[key]
                        r["diff_vs_baseline"][key] = d
                        r["regression"][key] = d > 1e-9  # worse (higher) than baseline
                rows.append(r)
    return rows


def verify_grid_self_check(rows: list, rate_cap: float = None, tol: float = 1e-3) -> dict:
    """team-lead (2026-07-26, 6th round): "1ケースのスモークで自己検証を通した
    ことにするな" -- extend the self-check to EVERY cell of the grid, not
    just one smoke case. For every non-baseline row (jerk_max>0), verifies:
      (a) steer_jerk_peak <= jerk_max + tol
      (b) steer_rate_peak <= rate_cap + tol (the physical steer_rate_max
          ceiling, independent of jerk_max -- STEER_RATE_CAP_NORM by default)
    Returns {"ok": bool, "violations": [...]} -- violations is a list of
    dicts with the failing row's identifying fields and both measured values,
    for every row that fails EITHER check (not just the first)."""
    if rate_cap is None:
        rate_cap = STEER_RATE_CAP_NORM
    violations = []
    for r in rows:
        if "error" in r or r.get("jerk_max", 0) <= 0:
            continue  # baseline (jerk stage disabled) has no cap to violate
        jm = r["jerk_max"]
        if r["steer_jerk_peak"] > jm + tol:
            violations.append({"target": r["target"], "speed_cmd": r["speed_cmd"], "jerk_max": jm,
                                "check": "steer_jerk_peak<=jerk_max",
                                "measured": r["steer_jerk_peak"], "limit": jm})
        if r["steer_rate_peak"] > rate_cap + tol:
            violations.append({"target": r["target"], "speed_cmd": r["speed_cmd"], "jerk_max": jm,
                                "check": "steer_rate_peak<=rate_cap",
                                "measured": r["steer_rate_peak"], "limit": rate_cap})
    return {"ok": len(violations) == 0, "n_checked": sum(1 for r in rows if "error" not in r and r.get("jerk_max", 0) > 0),
            "violations": violations}


def _preflight_check_new_telemetry(dt: float = 0.01) -> bool:
    """team-lead instruction (2026-07-26, 3rd round): never run the jerk-max
    sweep against a stale pre-jerk-cap DLL -- confirmed by checking whether
    envelope.steer_jerk_active is actually present in one live telemetry
    frame (not just inferred from the DLL's mtime). dt threaded through (4th
    round correction) purely for consistency -- a 1-step preflight probe
    isn't timing-sensitive, but --fixed_timestep and GT_Step() must still
    agree with each other here too."""
    import tempfile
    lib = vrt._load_lib()
    tmpdir = tempfile.mkdtemp(prefix="vd_resume_preflight_")
    xosc = _make_variant_speed_fixed(tmpdir, {"input_type": "stub"}, 5.0, push_triggers_out=True)
    argv_list = [b"vd_preflight", b"--osc", xosc.encode(), b"--headless",
                 b"--fixed_timestep", f"{dt:.6f}".encode()]
    argv = (ctypes.c_char_p * len(argv_list))(*argv_list)
    ok = False
    rc = lib.GT_InitWithArgs(len(argv_list), argv)
    if rc == 0:
        buf = ctypes.create_string_buffer(32768)
        lib.GT_Step(dt)
        n = lib.GT_GetVirtualDriverTelemetry(0, buf, len(buf))
        if n > 0:
            f = json.loads(buf.value.decode())
            ok = "steer_jerk_active" in f.get("envelope", {})
        lib.GT_Close()
    try:
        os.remove(xosc)
    except OSError:
        pass
    return ok


LANE_OFFSET_OUT_TXT = os.path.join(OUT_DIR, "f7_lane_offset_resume.txt")


def _main_lane_offset(args) -> int:
    """team-lead (2026-07-26, 7th round): full report for the lane-width
    (~3.5m, adjacent-lane) resume condition -- see run_lane_offset_resume_report
    doc. Writes LANE_OFFSET_OUT_TXT instead of OUT_TXT."""
    lines = []

    def w(s=""):
        print(s)
        lines.append(s)

    w("=" * 78)
    w(f"sim step dt = {args.dt}s")
    w("feature:F7 LANE-WIDTH offset resume characterization (adjacent-lane condition)")
    w("=" * 78)

    w("\n--- context: user's actual complaint (corrected requirement) ---")
    w("  \"Overrode and drove manually, ego ends up in the lane ADJACENT to the original one. "
      "Pressing AUTO_RESUME from there causes an overly abrupt return.\" This is a FULL LANE-WIDTH "
      "offset (~3.5m), not an arbitrary target_offset_m severity.")
    w("  road 0 lane widths near the route (resources/xodr/e6mini.xodr): lane -3 (route) = 3.50m, "
      "lane -2 = 3.65m, lane -4 = 3.90m. Center-to-center distance from lane -3 to lane -4 (its "
      "neighbor, the one this maneuver reaches) = 3.5/2+3.9/2 = 3.70m; to lane -2 = 3.575m. "
      f"--lane-target-m={args.lane_target_m:g}m is a round-number approximation of one lane width.")

    w("\n--- request #1: validity of the OLD (target_offset_m, sustained-one-direction-steer) method "
      "at 2-3m targets ---")
    w("  Checked directly (ego.track/lane/s continuity + native log grep for \"moved out of route\"/"
      "\"SetPathS\"): NO route-departure signal at target=2/3/3.5m -- track stays 0, s monotonic, only "
      "lane -3->-4 boundary crossings (no lane values outside {-2,-3,-4}). The earlier author's "
      "\"off route\" comment does NOT reproduce as stated on this build/config.")
    w("  BUT the labels are still misleading: run_network_arm_speed_fixed's Phase B holds ONE constant "
      "steering direction the whole ramp, so heading keeps rotating away from the road with nothing to "
      "bring it back; releasing steering does not stop the drift (the chassis coasts in whatever "
      "direction it was pointed). Re-measured directly: target=3.0m/2.0m/3.5m at v=8 all produced a "
      "FINAL |offset| of 9.5-14m at the resume edge -- 3-4x the nominal target, not because of route "
      "loss but because of uncontrolled post-release coasting. CONCLUSION: the existing 84-cell grid's "
      "\"target=2/3m\" cells are not invalid (no departure), but they do NOT represent a 2-3m or "
      "lane-width offset either -- they are an uncharacterized larger, uncontrolled excursion. Excluded "
      "from the lane-width characterization below; a NEW method (next) was used instead.")

    w("\n--- request #2: closed-loop lane-shift maneuver (steer + countersteer, replaces sustained-hold) ---")
    w("  Method: run_network_arm_lane_shift() -- a heading-tracking P-controller on the MANUAL axis "
      "(desired_heading_dev(t) = k_p*offset_error(t), saturated; steer_cmd chases that MOVING heading "
      "target). As offset_error shrinks the desired heading shrinks too, pulling the real heading back "
      "to parallel -- produces a natural steer/countersteer S-curve with no separate 'align' phase. "
      "Sign/gain calibrated empirically (calibrate_sign.py): steer=+0.1 held 1s at v=8 -> "
      "delta_heading=-0.061rad, delta_offset=-0.201m (both move together, consistent with "
      "offset_rate~=v*(heading-h0)).")

    dll_mtime = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(os.path.getmtime(vrt.DLL)))
    w(f"\n  DLL under test: {vrt.DLL}  mtime={dll_mtime} (team-lead 8th round: must be the "
      f"kappa-clamp-applied-last safety-fix build)")

    w("\n--- request (8th round): jerk_max sweep on the lane-width condition -- this IS the ride-feel "
      "judgment ground (residual-currency already rejected C; here C's target phenomenon, the onset "
      "transient, is confirmed to be the dominant mechanism) ---")
    w(f"  jerk_max values swept: {args.lane_jerk_max} (baseline={args.lane_jerk_max[0]:g}) "
      f"snap_max={args.lane_snap_max:g}")

    results_data = run_lane_offset_resume_report(args.lane_speeds, args.lane_jerk_max,
                                                  target_offset_m=args.lane_target_m,
                                                  snap_max=args.lane_snap_max, dt=args.dt,
                                                  maneuver_cap_s=args.maneuver_cap_s,
                                                  conv_window_s=args.lane_conv_window_s, window_s=args.window_s)
    results = results_data["results"]

    for speed in args.lane_speeds:
        rows = [r for r in results if r["speed"] == speed]
        w(f"\n=== speed={speed:g}m/s ===")
        baseline_row = next((r for r in rows if r["jerk_max"] == args.lane_jerk_max[0] and "error" not in r), None)
        for row in rows:
            jm = row["jerk_max"]
            diag = row["maneuver_diag"]
            if "error" in row:
                w(f"  jerk_max={jm:g}: {row['error']} -- diag={diag}"
                  + (f" route_check={row['route_check']}" if "route_check" in row else ""))
                continue
            rc = row["route_check"]
            w(f"  jerk_max={jm:g}: maneuver CONVERGED in {diag['steps_used']} steps "
              f"({diag['steps_used']*args.dt:.2f}s) final_offset={diag['final_offset_m']:.3f}m "
              f"heading_dev={diag['final_heading_dev_rad']:.4f}rad  route departed={rc['departed']}")
            w(f"    self-check: steer_rate<=2.459 {'PASS' if row['self_check_rate_ok'] else 'FAIL'}"
              + ("" if jm <= 0 else f"  steer_jerk<={jm:g} {'PASS' if row['self_check_jerk_ok'] else 'FAIL'}"))
            w(f"    offset_at_edge={row['offset_at_edge_m']:.3f}m speed_at_edge={row['speed_at_edge_mps']:.3f}m/s  "
              f"PEAK a_lat={row['a_lat_peak']:.4f}m/s2 yaw_rate={row['yaw_rate_peak']:.4f}rad/s "
              f"steer_rate={row['steer_rate_peak']:.4f}/s steer_jerk(gated)={row['steer_jerk_peak']:.3f}/s2")
            w(f"    ONSET (ungated, handoff frame): onset_rate_step={row['onset_rate_step']:.4f}/s  "
              f"onset_effective_jerk={row['onset_effective_jerk']:.3f}/s2")
            w(f"    KAPPA-CLAMP boundary snap (ungated, new safety-fix side effect, n_frames="
              f"{row['kappa_clamp_n_frames']}): max_jerk={row['kappa_clamp_max_jerk']:.3f}/s2")
            w(f"    steer_jerk_active: window={row['steer_jerk_active_frames_window']}  "
              f"full_run={row['steer_jerk_active_frames_full_run']}  by_phase={row['steer_jerk_active_by_phase']}")
            w(f"    time-to-resume={row['offset_converge_s']}  overshoot={row['offset_overshoot_m']:.4f}m")
            traj_str = " ".join(f"t={t:g}:{o:.3f}" for t, o in row["trajectory"])
            w(f"    trajectory [t(s):offset(m)]: {traj_str}")
            if "diff_vs_baseline" in row and baseline_row is not None:
                d = row["diff_vs_baseline"]
                w(f"    diff vs baseline(jerk_max={args.lane_jerk_max[0]:g}): " +
                  "  ".join(f"d{k}={v:+.4f}" for k, v in d.items()))
                onset_gentler = abs(row["onset_effective_jerk"]) < abs(baseline_row["onset_effective_jerk"]) - 1e-6
                time_cost = (row["offset_converge_s"] or 0) - (baseline_row["offset_converge_s"] or 0) \
                    if row["offset_converge_s"] is not None and baseline_row["offset_converge_s"] is not None else None
                overshoot_appeared = row["offset_overshoot_m"] > 0.01 and baseline_row["offset_overshoot_m"] <= 0.01
                traj_vals = [o for _, o in row["trajectory"]]
                monotonic = all(abs(traj_vals[i]) <= abs(traj_vals[i-1]) + 1e-6 for i in range(1, len(traj_vals)))
                w(f"    JUDGMENT: onset_gentler={onset_gentler}  "
                  f"time_to_resume_cost_s={time_cost}  overshoot_appeared={overshoot_appeared}  "
                  f"trajectory_monotonic={monotonic}")

    w("\n--- hypothesis check (handed down, NOT verified/falsified by implementation -- team-lead's "
      "explicit instruction: measure only) ---")
    w("  Question: is the resume trajectory SHORTEST-PATH-type (monotonic single-sided approach, no "
      "S-curve/merge shape) or does some mitigation already flatten it? Read directly from the "
      "'offset trajectory' rows above: a shortest-path return would show a smooth, MONOTONIC decay of "
      "|offset| toward 0 with no early plateau/S-shape distinct from the existing accel/yaw-rate/rate "
      "envelope's own ramp-limited shape -- readers should compare the printed trajectories against that "
      "description directly; this script does not itself classify the shape.")

    with open(LANE_OFFSET_OUT_TXT, "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines) + "\n")
    print(f"\nfull report written: {LANE_OFFSET_OUT_TXT}")
    return 0


# --------------------------------------------------------------------------
# report
# --------------------------------------------------------------------------
def main() -> int:
    import argparse
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--targets", nargs="+", type=float, default=[0.5, 1.0, 2.0, 3.0],
                     help="lateral-offset-ramp targets [m] (arm1)")
    ap.add_argument("--speeds", nargs="+", type=float, default=[2.0, 4.0, 6.0, 8.0, 10.0, 12.0, 14.0],
                     help="AccelAction cruise speeds [m/s] (this script's speed-fixed variant builder)")
    ap.add_argument("--jerk-max", nargs="+", type=float, default=[0.0, 25.0],
                     help="ad_steering_envelope_steer_jerk_max (FLAT term) values to compare, closed-loop. "
                          "FIRST value is the baseline every other value is diffed against "
                          "(<=0 disables the jerk stage -- bit-identical to pre-jerk-cap behavior).")
    ap.add_argument("--snap-max", type=float, default=None,
                     help="ad_steering_envelope_steer_snap_max (SPEED-DEPENDENT term) override applied to "
                          "EVERY case. Default None leaves the shipped config value in effect -- which is "
                          "CURRENTLY 101.1 (active), confounding a flat-term-only jerk-max comparison "
                          "(effective cap = min(jerk_max, snap_cap(v)); see run_network_arm_speed_fixed's "
                          "docstring). Pass --snap-max 0 to disable it and isolate the flat term.")
    ap.add_argument("--window-s", type=float, default=1.0, help="post-resume peak-taking window [s]")
    ap.add_argument("--conv-window-s", type=float, default=3.0,
                     help="post-resume window [s] for offset overshoot/convergence (compute_metrics)")
    ap.add_argument("--dt", type=float, default=0.01,
                     help="sim step [s] passed to BOTH --fixed_timestep and GT_Step() (team-lead correction, "
                          "2026-07-26 4th round: default is 0.01 to match the real-machine/synthetic dt "
                          "convention -- vd_resume_transient.py's own DT=0.05 default is 5x coarser and is "
                          "NOT used by this script's runner anymore).")
    ap.add_argument("--mode", choices=["jerk-grid", "lane-offset"], default="jerk-grid",
                     help="jerk-grid (default): the closed-loop jerk_max comparison grid. lane-offset "
                          "(team-lead 2026-07-26 7th round): the full-lane-width (~3.5m, adjacent lane) "
                          "resume characterization using the closed-loop lane-shift maneuver -- writes "
                          "test_results/f7_lane_offset_resume.txt instead of f7_resume_ride_feel.txt.")
    ap.add_argument("--lane-target-m", type=float, default=3.5,
                     help="lane-offset mode: target lateral offset [m] (~one lane width; adjacent-lane "
                          "center-to-center distance for this scenario's road is 3.575m -- see report).")
    ap.add_argument("--lane-speeds", nargs="+", type=float, default=[2.0, 4.0, 8.0, 12.0, 14.0],
                     help="lane-offset mode: speeds [m/s] to test.")
    ap.add_argument("--lane-jerk-max", nargs="+", type=float, default=[0.0, 10.0, 25.0, 50.0],
                     help="lane-offset mode: ad_steering_envelope_steer_jerk_max values to sweep. FIRST "
                          "value is the baseline (team-lead 8th round: shipped default is now 0/disabled -- "
                          "pass explicitly, never rely on the default) every other value is diffed against.")
    ap.add_argument("--lane-snap-max", type=float, default=0.0,
                     help="lane-offset mode: ad_steering_envelope_steer_snap_max override applied to every "
                          "cell (default 0 = isolate the flat jerk_max term, consistent with the jerk-grid "
                          "mode's convention).")
    ap.add_argument("--maneuver-cap-s", type=float, default=15.0,
                     help="lane-offset mode: max time [s] allowed for the closed-loop lane-shift maneuver "
                          "to converge before it is reported as failed/excluded.")
    ap.add_argument("--lane-conv-window-s", type=float, default=10.0,
                     help="lane-offset mode: post-resume window [s] for time-to-resume/overshoot/trajectory.")
    args = ap.parse_args()

    print(f"sim step dt = {args.dt}s (passed to both --fixed_timestep and GT_Step(); "
          f"vd_resume_transient.py's own DT=0.05 is NOT used by this script)")

    print("self-test: verifying the _deriv jerk-computation chain recovers a known jerk_cap "
          f"from a synthetic ramp at dt={args.dt}s...")
    st = _selftest_derivative_chain(jerk_cap=10.0, dt=args.dt)
    if not st["ok"]:
        print(f"FAIL: derivative self-test did NOT recover jerk_cap={st['jerk_cap_in']} -- "
              f"got {st['jerk_computed']}. This is a bug in _deriv/analyze_resume_case's derivative chain, "
              f"not a simulation issue. Not proceeding until this is fixed.")
        return 1
    print(f"self-test OK: recovered jerk_cap={st['jerk_cap_in']} exactly at every interior sample "
          f"({st['jerk_computed'][:3]}...).\n")

    if not os.path.exists(vrt.DLL):
        print(f"FAIL: DLL not found at {vrt.DLL} -- run /build first (not doing it automatically)")
        return 1

    print("preflight: checking the loaded DLL emits envelope.steer_jerk_active (jerk-cap build)...")
    with _quiet_native_stdout():
        fresh = _preflight_check_new_telemetry(dt=args.dt)
    if not fresh:
        print(f"FAIL: DLL at {vrt.DLL} does NOT emit envelope.steer_jerk_active -- this is a PRE-jerk-cap "
              f"build (stale). Rebuild (/build) before running the jerk-max sweep. Not proceeding.")
        return 1
    print("preflight OK: steer_jerk_active present -- DLL has the jerk-cap implementation.\n")

    if args.mode == "lane-offset":
        return _main_lane_offset(args)

    lines = []

    def w(s=""):
        print(s)
        lines.append(s)

    w("=" * 78)
    w(f"sim step dt = {args.dt}s")
    w("feature:F7 resume-transient RIDE-FEEL characterization")
    w("=" * 78)

    # --- (B) normal-driving baseline ---
    w("\n--- (B) normal-driving baseline: real-hardware unattended logs (frozen snapshot) ---")
    w(f"  source dir: {REALWHEEL_DIR}")
    baseline = load_realwheel_baseline()
    w("  false-latch check (team-lead correction 2026-07-26 16:16 -- these hands-off runs must show "
      "override.lateral=False throughout; a false latch would mean target_norm after that point is not "
      "a normal AD command):")
    for name, latch in baseline["latch_report"].items():
        if latch["latched"]:
            n_excl = baseline["per_file"][name]["n_excluded_post_latch"]
            w(f"    {name}: LATCHED at frame {latch['onset_idx']} (t={latch['onset_t']:.2f}s) -- "
              f"excluded {n_excl} frames from onset onward from the baseline below "
              f"(max_residual={latch['max_residual']:.4f})")
        else:
            w(f"    {name}: none (override.lateral never True across all {latch['n_total']} frames; "
              f"max_residual={latch['max_residual']:.4f}"
              f"{' -- exceeded 0.08 threshold transiently but sustain_accum never reached the 0.1s '
                'sustain_time needed to actually latch' if latch['max_residual'] > 0.08 else ''})")
    for name, pf in baseline["per_file"].items():
        w(f"  {name}: n={pf['n_frames']} wheel_base={pf['wheel_base']} "
          f"speed=[{pf['speed_min']:.2f},{pf['speed_max']:.2f}]m/s")
        w(f"    a_lat[m/s2]       p99={pf['a_lat']['p99']:.3f} max={pf['a_lat']['max']:.3f}  "
          f"(cross-check vs. AdSteeringEnvelope.hpp-cited 15-scenario-pool normal max 3.289)")
        w(f"    steer_jerk[1/s2]  median={pf['steer_jerk']['median']:.3f} p99={pf['steer_jerk']['p99']:.3f} "
          f"max={pf['steer_jerk']['max']:.3f}")
        w(f"    lateral_jerk[m/s3] p99={pf['lateral_jerk']['p99']:.3f} max={pf['lateral_jerk']['max']:.3f}")
        w(f"    lateral_snap[m/s4] p99={pf['lateral_snap']['p99']:.3f} max={pf['lateral_snap']['max']:.3f}")
    pooled = baseline["pooled"]
    w(f"  POOLED (all 3, n={pooled['n_frames']}): speed=[{pooled['speed_min']:.2f},{pooled['speed_max']:.2f}]m/s")
    w(f"    a_lat[m/s2]        p99={pooled['a_lat']['p99']:.3f} max={pooled['a_lat']['max']:.3f}")
    w(f"    steer_jerk[1/s2]   median={pooled['steer_jerk']['median']:.3f} p99={pooled['steer_jerk']['p99']:.3f} "
      f"max={pooled['steer_jerk']['max']:.3f}")
    w(f"    lateral_jerk[m/s3] p99={pooled['lateral_jerk']['p99']:.3f} max={pooled['lateral_jerk']['max']:.3f}")
    w(f"    lateral_snap[m/s4] p99={pooled['lateral_snap']['p99']:.3f} max={pooled['lateral_snap']['max']:.3f}")
    w("    NOTE: these are OPEN-LOOP quantities (bicycle model applied to the "
      "recorded steer command + recorded speed, dt=0.01s), not re-run physics.")
    w("    PROVISIONAL (team-lead 2026-07-26 3rd round): these 3 real-wheel logs were captured with the "
      "OLD 4-decimal-digit telemetry precision (jerk quantum ~1.0/s2 -- steer_jerk p99=2.00 above is only "
      "~2 quantum steps of resolution). Telemetry precision is now 9 digits (quantum ~1e-5/s2); a fresh "
      "higher-precision real-wheel capture is planned separately. Treat the steer_jerk/lateral_jerk/"
      "lateral_snap p99/max figures above as provisional until that recapture lands.")

    # --- CLOSED-LOOP jerk-max grid (team-lead 2026-07-26, 3rd round): the
    # C++ jerk cap has landed (ad_steering_envelope_steer_jerk_max, telemetry
    # envelope.steer_jerk_active) so B is now measured CLOSED-LOOP like A --
    # no more open-loop approximation. jerk_max_values[0] is the baseline
    # (team-lead spec: pass 0 first) every other value is diffed against.
    w(f"\n--- CLOSED-LOOP jerk-max grid: target={args.targets} speed={args.speeds} "
      f"jerk_max={args.jerk_max} (baseline={args.jerk_max[0]:g}) snap_max_override="
      f"{'shipped-config-default(currently 101.1, ACTIVE)' if args.snap_max is None else args.snap_max} "
      f"window_s={args.window_s} conv_window_s={args.conv_window_s} ---")
    if args.snap_max is None:
        w("  WARNING (found during this script's own smoke test): GT_esmini/config/virtual_driver.json "
          "CURRENTLY also carries ad_steering_envelope_steer_snap_max=101.1 (speed-dependent term, ACTIVE by "
          "default) alongside steer_jerk_max -- this differs from the 'flat-only, snap term dropped' design "
          "described in the 3rd-round request. Effective cap here = min(jerk_max, snap_cap(v)); at v=8m/s "
          "snap_cap~=7.85/s2, tighter than jerk_max=25. Pass --snap-max 0 to isolate the flat term.")
    w(f"  existing steer_rate cap in normalized-steer units: steer_rate_max/max_steer_angle = "
      f"1.5/{MAX_STEER_ANGLE} = {STEER_RATE_CAP_NORM:.4f} /s")
    t_start = time.time()
    rows = closed_loop_jerk_grid(args.targets, args.speeds, args.jerk_max, snap_max=args.snap_max,
                                  window_s=args.window_s, conv_window_s=args.conv_window_s, dt=args.dt)
    w(f"  grid wall time: {time.time()-t_start:.1f}s, {len(rows)} cases "
      f"({len(args.targets)} targets x {len(args.speeds)} speeds x {len(args.jerk_max)} jerk_max values)")

    ok_rows = [r for r in rows if "error" not in r]
    err_rows = [r for r in rows if "error" in r]
    if err_rows:
        w(f"  WARNING: {len(err_rows)}/{len(rows)} cases had no auto_transition edge / missing telemetry: "
          f"{[(r['target'], r['speed_cmd'], r['jerk_max']) for r in err_rows]}")

    # team-lead (2026-07-26, 6th round): self-check EVERY cell, not just one
    # smoke case -- steer_jerk_peak<=jerk_max and steer_rate_peak<=rate_cap,
    # for every non-baseline row. Any failure is reported as FAIL, not
    # silently averaged away.
    check = verify_grid_self_check(rows)
    w(f"\n--- GRID SELF-CHECK: steer_jerk_peak<=jerk_max and steer_rate_peak<={STEER_RATE_CAP_NORM:.4f} "
      f"on every non-baseline cell ({check['n_checked']} cells checked) ---")
    if check["ok"]:
        w("  PASS: no violations in any checked cell.")
    else:
        w(f"  FAIL: {len(check['violations'])} violation(s):")
        for v in check["violations"]:
            w(f"    target={v['target']:g}m speed_cmd={v['speed_cmd']:g}m/s jerk_max={v['jerk_max']:g}  "
              f"{v['check']}: measured={v['measured']:.4f} limit={v['limit']:.4f}")

    diffed = [r for r in ok_rows if "diff_vs_baseline" in r]
    PEAK_KEYS = ("steer_jerk_peak", "a_lat_peak", "lateral_jerk_peak", "lateral_snap_peak")
    PEAK_LABELS = {"steer_jerk_peak": "steer jerk [1/s2]", "a_lat_peak": "lateral accel [m/s2]",
                   "lateral_jerk_peak": "lateral jerk [m/s3]", "lateral_snap_peak": "lateral snap [m/s4]"}
    if diffed:
        w("\n  WORST regression per metric (diff_vs_baseline > 0, i.e. WORSE than the baseline jerk_max "
          f"={args.jerk_max[0]:g}), across all non-baseline jerk_max values:")
        for key in PEAK_KEYS:
            regressed = [r for r in diffed if r["regression"][key]]
            if not regressed:
                w(f"    {PEAK_LABELS[key]}: no regressions vs baseline in any tested case")
                continue
            worst_r = max(regressed, key=lambda r: r["diff_vs_baseline"][key])
            w(f"    {PEAK_LABELS[key]}: WORST +{worst_r['diff_vs_baseline'][key]:.4f} "
              f"(baseline={worst_r[key]-worst_r['diff_vs_baseline'][key]:.4f} -> jerk_max={worst_r['jerk_max']:.4f} "
              f"gives {worst_r[key]:.4f}) at target={worst_r['target']:g}m speed_cmd={worst_r['speed_cmd']:g}m/s "
              f"-- {len(regressed)}/{len(diffed)} non-baseline cases regressed on this metric")

    w("\n  Full per-case table (target, speed_cmd, jerk_max, speed_at_edge, steer_rate_peak, steer_jerk_peak, "
      "a_lat_peak, lateral_jerk_peak, lateral_snap_peak, offset_overshoot_m, offset_converge_s, "
      "steer_jerk_active_rate/frames WINDOWED vs FULL-RUN by-phase):")
    for r in rows:
        if "error" in r:
            w(f"    target={r['target']:g} speed_cmd={r['speed_cmd']:g} jerk_max={r['jerk_max']:g}  "
              f"NO EDGE / ERROR: {r['error']}")
            continue
        diff_str = ""
        if "diff_vs_baseline" in r:
            parts = []
            for key in PEAK_KEYS:
                mark = " REGRESSION" if r["regression"][key] else ""
                parts.append(f"d{key}={r['diff_vs_baseline'][key]:+.4f}{mark}")
            diff_str = "  [" + " ".join(parts) + "]"
        else:
            diff_str = "  [BASELINE]"
        sja_rate = r["steer_jerk_active_rate"]
        sja_str = f"{sja_rate:.2f}" if sja_rate is not None else "n/a(old telemetry)"
        by_phase_str = " ".join(f"{ph}:{cnt[1]}/{cnt[0]}" for ph, cnt in r["steer_jerk_active_by_phase"].items())
        w(f"    target={r['target']:g} speed_cmd={r['speed_cmd']:g} jerk_max={r['jerk_max']:g} "
          f"speed_at_edge={r['speed_at_edge_mps']:.2f}  steer_rate={r['steer_rate_peak']:7.4f}  "
          f"steer_jerk={r['steer_jerk_peak']:8.3f}  a_lat={r['a_lat_peak']:7.4f}  "
          f"lat_jerk={r['lateral_jerk_peak']:8.3f}  lat_snap={r['lateral_snap_peak']:10.3f}  "
          f"offset_overshoot={r['offset_overshoot_m']}  offset_converge_s={r['offset_converge_s']}  "
          f"steer_jerk_active_rate(window)={sja_str} (n={r['steer_jerk_active_frames']})  "
          f"full_run_active={r['steer_jerk_active_frames_full_run']} [{by_phase_str}]{diff_str}")

    with open(OUT_TXT, "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines) + "\n")
    print(f"\nfull report written: {OUT_TXT}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
