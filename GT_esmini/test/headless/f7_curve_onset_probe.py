"""feature:F7 -- curve-onset repro (team-lead task, 2026-07-27; PM redesign same day).

## History (read this before the code -- the design changed twice today)

1. User report (Sec.6-3 / Sec.5-4 of test_results/f7_residual_handoff_C.md):
   "カーブ途中からシナリオが始まると即オーバーライド判定になる". Original
   hypothesis: AD's target jumps to a large curve-implied steer while the
   wheel starts neutral, and the SHADOW/residual detector's plant model
   over-predicts the wheel's catch-up speed, so residual grows past
   threshold. This module's `analyze()` implements that hypothesis's
   falsification test (SUPPORTED / REFUTED / INCONCLUSIVE) and is still used
   for the "curve x neutral" cell below.

2. PM correction #1: there is a SECOND, independent path --
   OverrideManager.cpp's DIRECT-AXIS threshold check (`if (!ffb_sample_.active)
   { lat_active = abs(ps.steering) > steering_threshold_; }`, shipped
   steering_threshold=0.05 axis-frac = 22.5 deg wheel-rotation,
   ManualDriveConfig.hpp:484) -- no debounce, live from frame 1, and (once it
   fires) SELF-PERPETUATING because SetSteerTarget(..., active=!lat_manual)
   permanently starves ffb_sample_.active back to false. This is what the
   Sec.5-5 "previous run ends MANUAL -> next run starts MANUAL" bug actually is.

3. PM correction #2 (supersedes #1's framing): `ps.steering` in that check is
   the PHYSICAL wheel input (InputFrame.pedal_steer.steering), NOT the AD's
   target -- confirmed by reading HeadlessFfbInput.cpp's Poll()
   (`ps.steering = sink_->CurrentAxis()`) and the call order in
   ControllerVirtualDriver::Step() (Poll -> UpdateFfbSample(LAST frame's
   sample) -> override_mgr_.Update() -> ... -> SetSteerTarget/sink Update
   THIS frame). Frame 1's `ffb_sample_` is ALWAYS the Configure()-time default
   (IFFBSink.hpp: `bool active = false;`), so the direct-axis check can only
   ever be decided by ONE thing: **what the physical wheel axis already was
   at t=0**, independent of what AD wants. A curve's steering DEMAND therefore
   cannot, by itself, trigger the direct-axis path -- it can only feed the
   SEPARATE shadow/residual path (item 1).

   This reframes the open question as a 2x2 (see CELLS below): does
   Sec.5-4's real-machine symptom come from the curve (shadow/residual path,
   still open) or from an already-non-neutral wheel at t=0 (direct-axis path,
   same mechanism as Sec.5-5, already understood)?

## Units warning (read before trusting any angle number here)

Two DIFFERENT axis-fraction normalizations share the same underlying number:
tire-angle-normalized (`delta_tire = steer_norm * max_steer_angle` 0.61 rad,
GT_esmini/config/virtual_driver.json:22, vd_resume_transient.py
kinematic_window_metrics) vs. wheel-rotation-normalized (`wheel_deg =
axis_frac * 450`, scripts/ffb_spike/wheel_session_report.py:287). The F7/FFB
override axis-fraction (ffb.target_norm / ffb.gates.actual_norm/shadow_norm/
residual, and OverrideManager's `steering_threshold_`/`ps.steering`) is ALWAYS
the wheel-rotation convention -- this project already misdiagnosed a
steering-ratio confusion of exactly this shape once (OSI HVD steering_angle,
~12.9x; memory verification_semantics_lesson.md). Every angle this script
prints is wheel-rotation degrees (x*450) unless explicitly labeled "tire".

## Observability gap (report honestly, do not paper over)

`ffb.target_norm` / `ffb.position_error` / `ffb.gates.*` are ALL zeroed
whenever `ffb_sample_.active` is false (OverrideManager.cpp: `ffb_diag_ = {};`
in the inactive branch; ControllerVirtualDriver.cpp:521-527 zeroes the
top-level ffb block the same way when there is no active sample). That is
EXACTLY the state on frame 1, and forever after a direct-axis latch (the
self-perpetuating loop in item 2 above). **Telemetry therefore cannot show
the physical wheel axis (`ps.steering`) or `steering_threshold_` at all in
the condition this script most needs to observe.** Those two values are
reported here as CONFIGURED CONSTANTS (what this script told
HeadlessFfbInput/ManualDriveConfig to use), not as telemetry readings --
see PRIMED_AXIS_A0 / STEERING_THRESHOLD_DEFAULT below and the "primed axis
state" columns in the per-cell summary. `block_reason` and
`override.lateral`, by contrast, ARE read live every frame with no such gap
(they are set inside the same OverrideManager::Update() call regardless of
branch), and are the primary observables this script relies on.

## Established-harness reuse

Reads scripts/vd_ffb_notouch_parity.py first (team-lead instruction) and
imports DLL / BASE_CFG / FOLLOWER_MODES / REAL_MACHINE_DT / _write_cfg /
_write_variant / _run_headless from it verbatim (same pattern
residual_vs_jerkcap.py / f7_jerk_cap_binding_check.py already use). No new
C++, no modification of any existing resource. The "frozen" wheel mode
(HeadlessFfbInput.cpp:58-64, `GT_HEADLESS_FFB_MODE=frozen` +
`GT_HEADLESS_FFB_FROZEN_AT=<axis-frac>`) is likewise pre-existing (already
used by vd_resume_transient.py's run_ffb_arm()) -- this script is the first
to reuse it for a NON-NEUTRAL starting axis rather than a mid-run pin.

Usage (DriverScript venv; DOES require an unlocked UDP port + the built DLL --
see f7_curve_onset_repro.md Sec.0 for the exclusivity check before running):
    DriverScript\\.venv\\Scripts\\python.exe GT_esmini\\test\\headless\\f7_curve_onset_probe.py
    DriverScript\\.venv\\Scripts\\python.exe GT_esmini\\test\\headless\\f7_curve_onset_probe.py --list
    DriverScript\\.venv\\Scripts\\python.exe GT_esmini\\test\\headless\\f7_curve_onset_probe.py --mode sweep --scenarios curve_onset_30deg basic
"""
from __future__ import annotations

import argparse
import csv
import json
import logging
import math
import os
import re
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts"))
from vd_ffb_notouch_parity import (  # noqa: E402
    DLL, BASE_CFG, FOLLOWER_MODES, REAL_MACHINE_DT, _write_cfg, _write_variant, _run_headless,
)
# --- PM redesign (2026-07-27, 3rd message): a0 must be VERIFIED from the
# DLL's own log, not just asserted from the env var this script set. Reuses
# the established leveled-log-relay wrapper (GtLib / GT_SetLogCallback,
# GT_esminiLib.hpp:220-253) rather than re-deriving the ctypes callback
# plumbing -- see gt_lib.py's own docstring for the "NULL-detach" ctypes trap
# this wrapper already guards against. Only the 2x2 path uses this; --mode
# sweep keeps using vd_ffb_notouch_parity._run_headless unchanged.
sys.path.insert(0, str(ROOT / "GT_esmini" / "scripts" / "verification"))
from gt_lib import GtLib  # noqa: E402

OUT_DIR = ROOT / "test_results" / "f7_curve_onset_probe"

# HeadlessFfbInput.cpp:150-157 LOG_INFO -- logged synchronously inside
# SyntheticSink::Configure(), i.e. AFTER the env vars have actually been
# parsed by the C++ side. Rendered example (mode=plant):
#   "HeadlessFfbSink: mode=plant frozen_at=0.000 lag_tau=0.300s ... plant_seed=12345 plant_init_at=-0.034"
# Both frozen_at AND plant_init_at are logged UNCONDITIONALLY every time,
# regardless of mode_ (HeadlessFfbInput.cpp:91-96 computes plant_init_at
# before the mode branch) -- this script picks whichever one is authoritative
# for the cell's configured mode. This is the ONLY independent confirmation
# available that GT_HEADLESS_FFB_MODE / GT_HEADLESS_FFB_FROZEN_AT /
# GT_HEADLESS_FFB_PLANT_INIT_AT actually took effect -- see the module
# docstring's "a0 verification" section for why this matters (the project's
# own documented pattern of "wrong key silently ignored" / "invalid input
# silently falls back to StubInputSource"). PM 5th message: GT_HEADLESS_FFB_
# PLANT_INIT_AT is NOT YET BUILT as of this edit -- source-only change,
# confirmed by reading HeadlessFfbInput.cpp directly (git diff), not run.
_HEADLESS_FFB_CONFIGURE_LOG_RE = re.compile(
    r"HeadlessFfbSink: mode=(\S+) frozen_at=(-?\d+\.\d+).*plant_init_at=(-?\d+\.\d+)"
)
# Both frozen_at and plant_init_at are logged at {:.3f} precision
# (HeadlessFfbInput.cpp:150-151), so the tolerance is exactly the rounding
# quantum of that format spec, per PM instruction "許容差は正規化の丸め分だけに
# 留めること" -- not a hand-picked slack.
A0_LOG_ROUND_TRIP_TOLERANCE = 5e-4

# Only the force-coupled "plant" fixtures are relevant here (the hands-off,
# realistic-hardware model; see HeadlessFfbInput.cpp AdvancePlant). "frozen"/
# "follower" are kinematic ASSERTIONS unrelated to this hypothesis (see
# vd_ffb_notouch_parity.py's FOLLOWER_MODES docstring). Reused, not
# reinvented -- same 3-point measured-variance sweep every other F7 headless
# comparison in this repo uses (residual_vs_jerkcap.py, etc.).
PLANT_VARIANTS = [(m, extra) for m, cls, extra in FOLLOWER_MODES if m == "plant"]

_ANT = ROOT / "resources" / "xosc" / "verification" / "05_anticipation"
SCENARIOS: dict[str, Path] = {
    # --- straight-start controls (existing F7 asset family; unmodified) ----
    "basic":              ROOT / "resources" / "xosc" / "virtual_driver_basic.xosc",
    "right_turn":         _ANT / "decelerate_for_right_turn.xosc",
    "tljunction":         _ANT / "traffic_lights_junction.xosc",
    # --- new curve-onset family (task 1; resources/xodr/f7_curve_onset.xodr) ---
    "curve_onset_00deg":  _ANT / "f7_curve_onset_00deg.xosc",
    "curve_onset_10deg":  _ANT / "f7_curve_onset_10deg.xosc",
    "curve_onset_20deg":  _ANT / "f7_curve_onset_20deg.xosc",
    "curve_onset_30deg":  _ANT / "f7_curve_onset_30deg.xosc",
    "curve_onset_45deg":  _ANT / "f7_curve_onset_45deg.xosc",
    # --- REAL shipped scenario named by the user as reproducing the bug ------
    # PM 2026-07-27: the user reports the curve-onset failure on
    # highway_merge. This is the SHIPPED copy (resources/, soderleden.xodr,
    # with A1-A3 traffic) -- i.e. what the distributed app actually runs.
    # NOT EnvironmentSimulator/Unittest/xosc/highway_merge.xosc, which is a
    # different, minimal unit fixture on highway_merge.xodr.
    #
    # Ego inits at LanePosition roadId=1 laneId=-1 s=10.0, which sits INSIDE
    # the first paramPoly3 (s=0..17.363, cV=6.2479e-3, dV=-3.5984e-4).
    # Curvature there: V''=2cV+6dV*p, V'=2cV*p+3dV*p^2; at p=10 that is
    # kappa = -0.00909 1/m -> R ~= 110 m -> tyre angle atan(3.0/110) = 1.56 deg
    # -> steering wheel ~20.1 deg = 0.0447 axis-frac with the same ratio
    # (12.87) used for the synthetic family. So the AD demand at t=0 is real
    # and large, unlike the three straight-start scenarios above.
    # The shipped file has NO ObjectController, so _write_variant() cannot
    # attach the VD config to it. f7_highway_merge_vd.xosc is a sibling copy
    # (same directory -> all relative paths unchanged) whose ONLY difference
    # is the VirtualDriverController block on Ego; diff vs shipped is
    # +14 / -0 lines. The shipped file itself is left untouched (R3).
    "highway_merge":      ROOT / "resources" / "xosc" / "f7_highway_merge_vd.xosc",
}

# Expected steering-WHEEL-degree onset for the new family only (by
# construction -- see f7_curve_onset.xodr's header for the derivation).
# None for the pre-existing 3: their own eventual turn is a DIFFERENT,
# much larger figure reached only ~9-10s into the ORIGINAL route (not at
# t=0), so within this script's short capture window they are plain
# straight-start controls, same as curve_onset_00deg.
EXPECTED_WHEEL_DEG: dict[str, float | None] = {
    "basic": None, "right_turn": None, "tljunction": None,
    "curve_onset_00deg": 0.0, "curve_onset_10deg": 10.0,
    "curve_onset_20deg": 20.0, "curve_onset_30deg": 30.0, "curve_onset_45deg": 45.0,
}

WHEEL_DEG_FULL_LOCK = 450.0  # scripts/ffb_spike/wheel_session_report.py:287 convention
LEAD_EPS = 1e-4              # axis-frac noise floor for the "shadow leads" comparison
RESIDUAL_SLOPE_EPS = 1e-3    # axis-frac/s; below this, call the trend "flat" not "growing"

# --- PM redesign (2026-07-27, 2nd message): direct-axis 2x2 constants -------
# steering_threshold shipped default, ManualDriveConfig.hpp:484. NOT exposed
# in telemetry anywhere (grepped VirtualDriverTelemetryJson.cpp/
# ControllerVirtualDriver.cpp: absent) -- this script never overrides it
# (BASE_CFG passthrough, same as the residual_threshold policy elsewhere in
# this file), so it is reported here as a CONFIGURED CONSTANT, not a reading.
STEERING_THRESHOLD_DEFAULT = 0.05  # axis-frac = 22.5 deg wheel-rotation

# Non-neutral primed axis for the "non-neutral" column of the 2x2. PM's real-
# machine numbers: primed axis state a0 in {-4451..-4510 counts} = 61.1-61.9
# deg wheel-rotation, clearly above STEERING_THRESHOLD_DEFAULT's 22.5 deg.
# axis_frac = 61.5/450 = 0.13667 (sign mirrors the real observation's
# negative count band; magnitude is what the threshold check uses).
PRIMED_AXIS_A0_NONNEUTRAL = -0.13667  # axis-frac (~ -61.5 deg wheel-rotation)
PRIMED_AXIS_A0_NEUTRAL = 0.0

# PM redesign (2026-07-27, 4th message): env-effectiveness CONTRAST PAIR.
# withdraws the GT_SetLogCallback-based a0 verification as unnecessary
# ("そこまでする必要はない") -- kept in place below (harmless, already unit-
# tested) but no longer the primary defense against "env silently ignored".
# Instead: a SECOND non-neutral value, deliberately BELOW
# STEERING_THRESHOLD_DEFAULT, real-machine-grounded (the sweep's -1.1k band,
# a0/32767 = 0.0295-0.0391, 9/9 runs never latched -- this is that band's
# median). If GT_HEADLESS_FFB_FROZEN_AT is silently ignored, BOTH
# straight_nonneutral and straight_nonneutral_sub would run neutral and
# produce IDENTICAL (NO_LATCH) signatures -- a visible, log-free tell.
PRIMED_AXIS_A0_NONNEUTRAL_SUBTHRESHOLD = -0.0339  # axis-frac (< 0.05 threshold; must NOT latch)


def _plant_tag(extra: dict) -> str:
    return f"brk{extra['GT_HEADLESS_FFB_PLANT_BREAKAWAY']}_slope{extra['GT_HEADLESS_FFB_PLANT_SLOPE']}"


def _set_wheel_env(mode: str, extra: dict) -> None:
    """Configure HeadlessFfbInput's synthetic wheel for the NEXT GT_InitWithArgs.

    mode="plant": force-coupled hands-off wheel. Starts at axis=0.0 UNLESS
      `extra["GT_HEADLESS_FFB_PLANT_INIT_AT"]` is set (HeadlessFfbInput.cpp:
      91-96, PM 5th message -- NOT YET BUILT as of this edit, source-only;
      confirmed by reading the file directly, not by running it). Models a
      wheel resting off-center but still free to move under force -- unlike
      "frozen" below, it CAN move if the servo pushes hard enough.
    mode="frozen": axis pinned at `extra["GT_HEADLESS_FFB_FROZEN_AT"]` FOREVER
      (HeadlessFfbInput.cpp:58-64/170) -- models a wheel being actively HELD
      (a genuine intervention), not one merely resting off-center. Never
      moves regardless of servo force. PM 5th message: this distinction is
      exactly why CELLS now has both a "plant, off-center" cell (negative
      control -- must NOT look like an intervention) and a "frozen, held"
      cell (positive control -- must genuinely latch via the residual path).
    """
    os.environ["GT_HEADLESS_FFB_MODE"] = mode
    for var in ("GT_HEADLESS_FFB_FROZEN_AT", "GT_HEADLESS_FFB_LAG_TAU",
                "GT_HEADLESS_FFB_PLANT_BREAKAWAY", "GT_HEADLESS_FFB_PLANT_SLOPE",
                "GT_HEADLESS_FFB_PLANT_DEAD_TIME", "GT_HEADLESS_FFB_PLANT_VELOCITY_TAU",
                "GT_HEADLESS_FFB_PLANT_INIT_AT"):
        os.environ.pop(var, None)
    os.environ.update(extra)


def _set_plant_env(extra: dict) -> None:
    """Back-compat alias for the original (--mode sweep) plant-only sweep."""
    _set_wheel_env("plant", extra)


def _row_from_frame(f: dict) -> dict | None:
    """Extract one CSV row from a raw VirtualDriverTelemetry frame dict.

    actual_norm is reconstructed as target_norm - position_error (the SAME
    formula OverrideManager.cpp:236 uses), NOT read from ffb.gates.actual_norm
    -- the latter is the PREVIOUS frame's reconstructed value (the documented
    "ONE-ROW GATES LAG", VirtualDriverTelemetryJson.cpp:8-20 /
    GT_esmini/test/tools/ffb_override_replay.cpp:31-51). Reconstructing
    directly gives the true same-instant actual_norm and keeps it paired
    consistently with ffb.gates.shadow_norm/residual, which ARE each other's
    same-instant pair (both come out of the SAME OverrideManager::Update()
    call). See f7_curve_onset_repro.md Sec.[units] for the full note.
    """
    ffb = f.get("ffb")
    if not ffb:
        return None
    gates = ffb.get("gates", {})
    target = ffb.get("target_norm")
    pos_err = ffb.get("position_error")
    if target is None or pos_err is None:
        return None
    actual = target - pos_err
    shadow = gates.get("shadow_norm")
    residual = gates.get("residual")
    override = f.get("override", {})
    return {
        "sim_time": f.get("sim_time"),
        "target_norm": target,
        "actual_norm": actual,
        "shadow_norm": shadow,
        "residual": residual,
        "residual_threshold": gates.get("residual_threshold"),
        "sustain_accum": gates.get("sustain_accum"),
        "sustain_time": gates.get("sustain_time"),
        "block_reason": gates.get("block_reason"),
        "shadow_moving": gates.get("shadow_moving"),
        "target_active": ffb.get("target_active"),
        "override_lateral": override.get("lateral"),
        "manual_transition": override.get("manual_transition"),
    }


def analyze(rows: list[dict], onset_window_s: float) -> dict:
    """Directional judgment per team-lead spec (handoff Sec.5-4 prediction):
    SUPPORTED  -- during onset, shadow_norm consistently overtakes actual_norm
                  TOWARD target_norm (i.e. |target-shadow| < |target-actual|),
                  and residual trends upward (grows) over the window.
    REFUTED    -- shadow consistently LAGS actual (opposite direction) --
                  "逆に遅れているなら別機序" -- a different mechanism.
    INCONCLUSIVE -- too few valid onset samples, or direction is mixed, or
                  residual is flat/declining.
    """
    onset = [r for r in rows if r["sim_time"] is not None and 0.0 < r["sim_time"] <= onset_window_s]
    valid = []
    for r in onset:
        if r["block_reason"] == "bootstrap":
            continue  # shadow not yet seeded this frame -- not a real comparison
        t, a, s = r["target_norm"], r["actual_norm"], r["shadow_norm"]
        if t is None or a is None or s is None:
            continue
        direction = 0.0
        if t != a:
            direction = 1.0 if (t - a) > 0 else -1.0
        if direction == 0.0:
            continue
        lead_amount = abs(t - a) - abs(t - s)  # >0: shadow closer to target than actual (overtakes)
        valid.append({**r, "lead_amount": lead_amount})

    n_valid = len(valid)
    n_leads = sum(1 for r in valid if r["lead_amount"] > LEAD_EPS)
    n_lags = sum(1 for r in valid if r["lead_amount"] < -LEAD_EPS)
    frac_leads = (n_leads / n_valid) if n_valid else None
    frac_lags = (n_lags / n_valid) if n_valid else None

    # Residual slope up to the first latch (or end of onset window if none).
    latch_idx = next((i for i, r in enumerate(onset) if r.get("override_lateral")), None)
    span = onset[: latch_idx + 1] if latch_idx is not None else onset
    span = [r for r in span if r["residual"] is not None and r["sim_time"] is not None]
    slope = None
    if len(span) >= 2:
        t0, t1 = span[0]["sim_time"], span[-1]["sim_time"]
        if t1 > t0:
            slope = (span[-1]["residual"] - span[0]["residual"]) / (t1 - t0)

    max_target_onset = max((abs(r["target_norm"]) for r in onset if r["target_norm"] is not None), default=0.0)
    max_residual_onset = max((r["residual"] for r in onset if r["residual"] is not None), default=0.0)
    residual_threshold = next((r["residual_threshold"] for r in onset if r["residual_threshold"]), None)
    bootstrap_frames = sum(1 for r in onset if r["block_reason"] == "bootstrap")

    if n_valid < 3 or frac_leads is None:
        verdict = "INCONCLUSIVE (fewer than 3 valid onset samples -- target never moved off actual)"
    elif frac_leads >= 0.7 and slope is not None and slope > RESIDUAL_SLOPE_EPS:
        verdict = ("SUPPORTED: shadow overtakes actual toward target "
                   f"({n_leads}/{n_valid} onset frames) and residual grows "
                   f"(slope={slope:.5f} axis-frac/s)")
    elif frac_lags >= 0.7 and slope is not None and slope > RESIDUAL_SLOPE_EPS:
        verdict = ("REFUTED (different mechanism -- team-lead falsification rule): "
                   f"shadow LAGS actual instead of overtaking it "
                   f"({n_lags}/{n_valid} onset frames), residual still grows "
                   f"(slope={slope:.5f} axis-frac/s)")
    else:
        slope_str = "n/a" if slope is None else f"{slope:.5f}"
        verdict = (f"INCONCLUSIVE: mixed direction (leads={n_leads}/{n_valid} "
                   f"lags={n_lags}/{n_valid}) or flat/declining residual (slope={slope_str})")

    return {
        "n_onset_frames": len(onset),
        "n_valid_direction_frames": n_valid,
        "n_leads": n_leads,
        "n_lags": n_lags,
        "frac_leads": frac_leads,
        "residual_slope_axisfrac_per_s": slope,
        "latch_sim_time": onset[latch_idx]["sim_time"] if latch_idx is not None else None,
        "max_abs_target_norm_onset": max_target_onset,
        "max_abs_target_wheel_deg_onset": max_target_onset * WHEEL_DEG_FULL_LOCK,
        "max_residual_onset": max_residual_onset,
        "residual_threshold": residual_threshold,
        "bootstrap_frames_onset": bootstrap_frames,
        "bootstrap_frames_ok": bootstrap_frames <= 1,  # OverrideManager.cpp:232 -- suppression is 1 frame only
        "verdict": verdict,
    }


def run_one(scenario_name: str, scenario_path: Path, plant_extra: dict, duration_s: float,
            onset_window_s: float, dt: float) -> tuple[list[dict], dict]:
    tmpdir = tempfile.mkdtemp(prefix=f"f7_curve_onset_{scenario_name}_")
    cfg_path = _write_cfg(tmpdir, "headless_ffb", True, "ffb")
    xosc_path = _write_variant(str(scenario_path), tmpdir, cfg_path, "ffb")
    _set_plant_env(plant_extra)
    frames = _run_headless(DLL, xosc_path, dt=dt, max_time_s=duration_s)
    rows = [r for r in (_row_from_frame(f) for f in frames) if r is not None]
    verdict = analyze(rows, onset_window_s)
    verdict["scenario"] = scenario_name
    verdict["plant_variant"] = _plant_tag(plant_extra)
    verdict["expected_wheel_deg"] = EXPECTED_WHEEL_DEG.get(scenario_name)
    verdict["n_frames_total"] = len(rows)
    return rows, verdict


def _write_csv(rows: list[dict], path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if not rows:
        path.write_text("", encoding="utf-8")
        return
    fieldnames = list(rows[0].keys())
    with open(path, "w", newline="", encoding="utf-8") as fh:
        w = csv.DictWriter(fh, fieldnames=fieldnames)
        w.writeheader()
        w.writerows(rows)


# --- PM redesign (2026-07-27, 5th message): plant-mode init + dual controls -
#
# History: the 2nd message set up a 2x2 (straight/curve x neutral/non-neutral).
# The 4th message added straight_nonneutral_sub as an env-effectiveness
# contrast partner. This 5th message fixes a DESIGN BUG in that contrast: the
# non-neutral cells used "frozen" (a HELD wheel -- a genuine intervention by
# construction), so straight_nonneutral_sub kept coming back RESIDUAL_PATH_
# LATCH instead of the real machine's observed NO_LATCH for that axis band.
# The real -1.1k band (9/9 no-latch) is a wheel RESTING off-center but still
# free to move under force -- exactly what the new GT_HEADLESS_FFB_PLANT_
# INIT_AT (HeadlessFfbInput.cpp:91-96, PM 5th message, NOT YET BUILT as of
# this edit -- confirmed by reading the source diff directly, not by running
# it) lets "plant" mode do, which "frozen" structurally cannot (see
# _set_wheel_env's docstring for the held-vs-resting distinction).
#
# This makes almost every cell "plant" now, with "frozen" reserved for the
# ONE cell that specifically needs a genuine held-wheel intervention:
#
#   straight_neutral         plant, a0=0            -> NO_LATCH (baseline)
#   straight_nonneutral_sub  plant, a0=-0.0339       -> NO_LATCH (negative control:
#                                                        matches real -1.1k band, 9/9 no-latch)
#   straight_nonneutral      plant, a0=-0.13667      -> DIRECT_AXIS_LATCH_FRAME1 (Sec.5-5 repro;
#                                                        frame-1 direct-axis check is position-only,
#                                                        so it fires under "plant" exactly as it did
#                                                        under "frozen" -- only the AFTER-latch
#                                                        behavior differs, and that is irrelevant
#                                                        here since the servo shuts off on latch)
#   straight_held (NEW)      frozen, a0=-0.0339      -> RESIDUAL_PATH_LATCH (positive control:
#                                                        a HELD wheel at the same magnitude that
#                                                        does NOT trip the direct-axis threshold
#                                                        must still be caught by the residual path
#                                                        -- proves the detector isn't just blind)
#   curve_neutral (MAIN)     plant, a0=0             -> the open question
#   curve_nonneutral         plant, a0=-0.13667      -> expected to latch; not used for triage
#
# "curve-start" still uses ONLY curve_onset_30deg (~0.0667 axis-frac AD
# demand, matches the user's real-machine "30 deg" report scale) -- the 2nd
# message's withdrawal of the finer 20/25 deg pair still stands (AD's demand
# cannot cross the direct-axis threshold; see that message's reasoning,
# unchanged by this redesign). "straight-start" uses the pre-existing `basic`
# scenario (no new asset needed).
CELLS: list[tuple[str, str, str, str, dict]] = [
    # (cell_id, scenario_name, wheel_condition, mode, extra)
    ("straight_neutral",        "basic",             "neutral",           "plant",  {"GT_HEADLESS_FFB_PLANT_INIT_AT": f"{PRIMED_AXIS_A0_NEUTRAL:.5f}"}),
    ("straight_nonneutral_sub", "basic",             "nonneutral_sub",    "plant",  {"GT_HEADLESS_FFB_PLANT_INIT_AT": f"{PRIMED_AXIS_A0_NONNEUTRAL_SUBTHRESHOLD:.5f}"}),
    ("straight_nonneutral",     "basic",             "nonneutral",        "plant",  {"GT_HEADLESS_FFB_PLANT_INIT_AT": f"{PRIMED_AXIS_A0_NONNEUTRAL:.5f}"}),
    # Positive control (PM 5th message, NEW): a HELD wheel (frozen, i.e. a
    # genuine intervention) at the SAME sub-threshold magnitude as the
    # negative control above. Must latch via the RESIDUAL path (direct-axis
    # is structurally out of reach at this magnitude) -- proves the residual
    # detector can fire at all, so straight_nonneutral_sub's NO_LATCH means
    # "not an intervention", not "the detector is deaf".
    ("straight_held",           "basic",             "held",              "frozen", {"GT_HEADLESS_FFB_FROZEN_AT": f"{PRIMED_AXIS_A0_NONNEUTRAL_SUBTHRESHOLD:.5f}"}),
    ("curve_neutral",           "curve_onset_30deg", "neutral",           "plant",  {"GT_HEADLESS_FFB_PLANT_INIT_AT": f"{PRIMED_AXIS_A0_NEUTRAL:.5f}"}),
    ("curve_nonneutral",        "curve_onset_30deg", "nonneutral",        "plant",  {"GT_HEADLESS_FFB_PLANT_INIT_AT": f"{PRIMED_AXIS_A0_NONNEUTRAL:.5f}"}),
]
_NOMINAL_PLANT_EXTRA = PLANT_VARIANTS[1][1]  # brk=0.19/slope=3.35/dead_time=0.041/tau=0.018 (shadow's own nominal constants)


class _LineCapture(logging.Handler):
    """Collects every DLL log record's rendered message, in order. Attached to
    the "gt_esmini.dll" logger BEFORE GtLib() is constructed, so GtLib's own
    _ensure_default_handler() (gt_lib.py:57-63) sees hasHandlers()==True and
    does not also attach its stderr handler -- we still get every line, just
    without also spamming stderr for a probe that runs 4 times."""

    def __init__(self):
        super().__init__()
        self.lines: list[str] = []

    def emit(self, record: logging.LogRecord) -> None:
        self.lines.append(record.getMessage())


def _run_headless_with_log(dll_path: str, xosc_path: str, dt: float,
                            max_time_s: float) -> tuple[list[dict], list[str]]:
    """Same frame-collection contract as vd_ffb_notouch_parity._run_headless
    (same step count `int(max_time_s/dt)+20`, same "decode or skip" per-frame
    handling), but via GtLib (gt_lib.py) instead of a bare ctypes.CDLL so the
    DLL's own GT_SetLogCallback log lines are captured alongside the frames.
    Returns (frames, captured_log_lines)."""
    dll_logger = logging.getLogger("gt_esmini.dll")
    capture = _LineCapture()
    dll_logger.addHandler(capture)
    dll_logger.setLevel(logging.DEBUG)  # HeadlessFfbSink's Configure() line logs at INFO (level=2)
    frames: list[dict] = []
    try:
        with GtLib(dll_path) as lib:
            rc = lib.init_with_args(["--osc", xosc_path, "--headless", "--fixed_timestep", f"{dt:.3f}"])
            if rc != 0:
                raise RuntimeError(f"GT_InitWithArgs rc={rc} on {xosc_path}: {lib.get_last_error()}")
            for _ in range(int(max_time_s / dt) + 20):
                lib.step(dt)
                tel = lib.get_vd_telemetry(-1)
                if tel is not None:
                    frames.append(tel)
    finally:
        dll_logger.removeHandler(capture)
    return frames, capture.lines


def _verify_a0(log_lines: list[str], mode: str, a0_configured: float) -> dict:
    """Parse HeadlessFfbSink's Configure()-time log line and compare against
    what THIS SCRIPT told it to configure. Independent confirmation, not a
    second assertion of the same value -- see the module's a0-verification
    docstring section for why this matters (the project's documented "wrong
    key silently ignored" failure shape).

    PM 5th message: GT_HEADLESS_FFB_PLANT_INIT_AT (HeadlessFfbInput.cpp:91-96)
    makes "plant" mode's initial axis configurable too (previously hardcoded
    0.0) -- both frozen_at and plant_init_at are logged UNCONDITIONALLY every
    Configure() call, so this function picks whichever field is authoritative
    for `mode` and verifies THAT one, exactly the same way for both modes
    (no more "plant has nothing to verify" special-case)."""
    parsed_mode = None
    parsed_frozen_at = None
    parsed_plant_init_at = None
    for line in log_lines:
        m = _HEADLESS_FFB_CONFIGURE_LOG_RE.search(line)
        if m:
            parsed_mode = m.group(1)
            parsed_frozen_at = float(m.group(2))
            parsed_plant_init_at = float(m.group(3))
            break  # Configure() logs exactly once per Init(); first match is authoritative
    found = parsed_mode is not None
    mode_ok = found and parsed_mode == mode
    parsed_a0 = parsed_frozen_at if mode == "frozen" else parsed_plant_init_at
    a0_ok = found and mode_ok and parsed_a0 is not None and \
        abs(parsed_a0 - a0_configured) <= A0_LOG_ROUND_TRIP_TOLERANCE
    a0_verified = parsed_a0 if found else None
    return {
        "log_line_found": found,
        "parsed_mode": parsed_mode,
        "parsed_frozen_at": parsed_frozen_at,
        "parsed_plant_init_at": parsed_plant_init_at,
        "mode_matches_configured": mode_ok,
        "a0_verified": a0_verified,
        "a0_verification_ok": a0_ok,
    }


def run_cell(cell_id: str, scenario_name: str, mode: str, extra: dict, duration_s: float,
             onset_window_s: float, dt: float) -> tuple[list[dict], dict]:
    scenario_path = SCENARIOS[scenario_name]
    tmpdir = tempfile.mkdtemp(prefix=f"f7_2x2_{cell_id}_")
    cfg_path = _write_cfg(tmpdir, "headless_ffb", True, "ffb")
    xosc_path = _write_variant(str(scenario_path), tmpdir, cfg_path, "ffb")
    wheel_extra = dict(extra)
    if mode == "plant":
        wheel_extra = {**_NOMINAL_PLANT_EXTRA, **wheel_extra}
    _set_wheel_env(mode, wheel_extra)
    # a0_configured is a SET value (read back from the dict THIS FUNCTION just
    # built two lines above) -- NOT an observation of what the sim actually
    # did. Do not read this as evidence the env var took effect; that is what
    # _verify_a0()/verification["a0_verified"] below is for (PM 3rd + 5th
    # messages: both "frozen" (GT_HEADLESS_FFB_FROZEN_AT) and "plant"
    # (GT_HEADLESS_FFB_PLANT_INIT_AT) now have a real configured value to
    # verify -- plant is no longer a hardcoded-0.0 special case).
    a0_env_key = "GT_HEADLESS_FFB_FROZEN_AT" if mode == "frozen" else "GT_HEADLESS_FFB_PLANT_INIT_AT"
    a0_configured = float(wheel_extra.get(a0_env_key, 0.0))

    frames, log_lines = _run_headless_with_log(DLL, xosc_path, dt=dt, max_time_s=duration_s)
    verification = _verify_a0(log_lines, mode, a0_configured)
    rows = [r for r in (_row_from_frame(f) for f in frames) if r is not None]

    # PM instruction (3rd message) item 3: classify_cell() gets the MEASURED
    # a0, not the configured one. Falls back to the configured value only when
    # verification itself is impossible (log line never seen at all) so
    # classify_cell() still has SOME number to work with -- but that case is
    # exactly what cell_valid=False below exists to flag loudly.
    a0_for_classification = verification["a0_verified"] if verification["a0_verified"] is not None else a0_configured
    result = classify_cell(rows, a0_for_classification, STEERING_THRESHOLD_DEFAULT)
    result["cell_id"] = cell_id
    result["scenario"] = scenario_name
    result["wheel_mode"] = mode
    result["n_frames_total"] = len(rows)
    result["a0_configured"] = a0_configured
    result.update(verification)
    result["cell_valid"] = verification["a0_verification_ok"]
    result["warnings"] = []
    if not verification["log_line_found"]:
        result["warnings"].append(
            "HeadlessFfbSink Configure() log line NEVER SEEN -- cannot confirm GT_HEADLESS_FFB_MODE/"
            "GT_HEADLESS_FFB_FROZEN_AT took effect. Falling back to the CONFIGURED value "
            f"(a0={a0_configured}) for classify_cell(), but this cell is INVALID until investigated."
        )
    elif not verification["mode_matches_configured"]:
        result["warnings"].append(
            f"MODE MISMATCH: configured GT_HEADLESS_FFB_MODE={mode!r} but the DLL's own log reports "
            f"mode={verification['parsed_mode']!r}. Cell INVALID."
        )
    elif mode == "frozen" and not verification["a0_verification_ok"]:
        result["warnings"].append(
            f"A0 MISMATCH: configured GT_HEADLESS_FFB_FROZEN_AT={a0_configured:.5f} but the DLL's own "
            f"log reports frozen_at={verification['parsed_frozen_at']}. Cell INVALID -- do not trust "
            "this cell's signature/latch result."
        )
    # The shadow/residual analyze() is only meaningful once the direct-axis
    # path is ruled out for THIS cell (i.e. it never latches on frame 1) --
    # always run it anyway (cheap) so "curve_neutral" gets its full verdict.
    result["shadow_path_analysis"] = analyze(rows, onset_window_s)
    return rows, result


def classify_cell(rows: list[dict], a0: float, steering_threshold: float) -> dict:
    """Direct-axis-vs-residual classification for one 2x2 cell.

    a0 / steering_threshold are CONFIGURED CONSTANTS this script set (see the
    module docstring's "Observability gap" section for why they cannot be
    read back from telemetry) -- reported alongside the live-observed fields
    (block_reason, override_lateral) so a reader can see both what was
    configured and what the product actually did with it.
    """
    frame1 = rows[0] if rows else None
    frame1_block_reason = frame1["block_reason"] if frame1 else None
    frame1_latched = bool(frame1["override_lateral"]) if frame1 else None
    # Structural self-check (OverrideManager.cpp Configure(): ffb_sample_={}
    # -> active=false; IFFBSink.hpp: FfbInterventionSample default active=
    # false) -- frame 1 must ALWAYS take the inactive branch regardless of
    # cell. If this is ever False, something upstream of this script changed
    # and every other conclusion here is suspect.
    frame1_block_reason_is_inactive = (frame1_block_reason == "inactive")

    direct_axis_predicted = abs(a0) > steering_threshold

    latch_idx = next((i for i, r in enumerate(rows) if r.get("override_lateral")), None)
    latch_sim_time = rows[latch_idx]["sim_time"] if latch_idx is not None else None
    latch_block_reason_at_latch = rows[latch_idx]["block_reason"] if latch_idx is not None else None

    if latch_idx == 0 and frame1_block_reason == "inactive":
        signature = "DIRECT_AXIS_LATCH_FRAME1"
    elif latch_idx is not None and latch_block_reason_at_latch != "inactive":
        signature = "RESIDUAL_PATH_LATCH"
    elif latch_idx is not None:
        # Latched later than frame 1, but still via the inactive branch --
        # would mean the direct-axis path fired on a LATER frame (only
        # possible if ffb_sample_.active went false again post-bootstrap,
        # e.g. servo genuinely stopped) rather than frame 1's known-inert state.
        signature = "INACTIVE_LATCH_NOT_FRAME1"
    else:
        signature = "NO_LATCH"

    # Self-perpetuation check (PM msg 1): once direct-axis latches, EVERY
    # subsequent frame should stay block_reason==inactive AND override_lateral
    # True (SetSteerTarget(active=!lat_manual) permanently starves the servo).
    self_perpetuating = None
    if signature == "DIRECT_AXIS_LATCH_FRAME1":
        tail = rows[1:]
        self_perpetuating = all(r["block_reason"] == "inactive" and r.get("override_lateral") for r in tail) if tail else True

    return {
        "primed_axis_a0": a0,
        "primed_axis_a0_wheel_deg": a0 * WHEEL_DEG_FULL_LOCK,
        "steering_threshold_configured": steering_threshold,
        "direct_axis_predicted_to_fire": direct_axis_predicted,
        "frame1_block_reason": frame1_block_reason,
        "frame1_block_reason_is_inactive": frame1_block_reason_is_inactive,
        "frame1_latched": frame1_latched,
        "latch_sim_time": latch_sim_time,
        "latch_block_reason_at_latch": latch_block_reason_at_latch,
        "signature": signature,
        "self_perpetuating": self_perpetuating,
    }


def print_2x2_report(cell_results: list[dict]) -> None:
    by_id = {r["cell_id"]: r for r in cell_results}
    print("\n=== 2x2 direct-axis vs shadow-path classification ===")
    for r in cell_results:
        print(f"-- {r['cell_id']} (scenario={r['scenario']}, wheel_mode={r['wheel_mode']}) --")
        print(f"   a0 CONFIGURED (env var this script set): {r['a0_configured']:.5f} axis-frac")
        print(f"   a0 VERIFIED (parsed from DLL's own HeadlessFfbSink Configure() log line): "
              f"log_line_found={r['log_line_found']} parsed_mode={r['parsed_mode']!r} "
              f"parsed_frozen_at={r['parsed_frozen_at']} parsed_plant_init_at={r['parsed_plant_init_at']} "
              f"-> a0_verification_ok={r['a0_verification_ok']} CELL_VALID={r['cell_valid']}")
        for w in r["warnings"]:
            print(f"   !! WARNING: {w}")
        print(f"   classified against: a0={r['primed_axis_a0']:.5f} axis-frac ({r['primed_axis_a0_wheel_deg']:.1f} deg wheel-equiv), "
              f"steering_threshold={r['steering_threshold_configured']:.3f} axis-frac, "
              f"direct_axis_predicted_to_fire={r['direct_axis_predicted_to_fire']}")
        print(f"   frame1: block_reason={r['frame1_block_reason']} (inactive as expected: {r['frame1_block_reason_is_inactive']}) "
              f"latched={r['frame1_latched']}")
        print(f"   signature={r['signature']}  latch_sim_time={r['latch_sim_time']}  "
              f"self_perpetuating={r['self_perpetuating']}")
        if r["cell_id"] == "curve_neutral":
            print(f"   shadow-path analysis (original Sec.5-4 hypothesis): {r['shadow_path_analysis']['verdict']}")

    invalid = [r for r in cell_results if not r["cell_valid"]]
    if invalid:
        print(f"\n!! {len(invalid)}/{len(cell_results)} CELL(S) INVALID (a0 verification failed) -- "
              f"{[r['cell_id'] for r in invalid]}. Any conclusion drawn from these below is UNRELIABLE.")

    # Combined verdict -- PM 5th message: THREE named conditions, ALL must
    # hold before curve_neutral is adopted. Supersedes the 4th message's
    # 2-cell contrast (kept the negative control, added a positive one).
    print("\n=== combined verdict ===")
    curve_n = by_id.get("curve_neutral")
    straight_nn = by_id.get("straight_nonneutral")
    straight_sub = by_id.get("straight_nonneutral_sub")
    straight_held = by_id.get("straight_held")

    if curve_n and not curve_n["cell_valid"]:
        print("  curve x neutral: SKIPPED -- a0 verification failed for this cell (see WARNING above). "
              "No conclusion drawn; fix the env-var wiring and re-run before trusting anything else here.")
        curve_n = None
    for label, cell in (("straight_nonneutral", straight_nn), ("straight_nonneutral_sub", straight_sub),
                        ("straight_held", straight_held)):
        if cell and not cell["cell_valid"]:
            print(f"  {label}: SKIPPED -- a0 verification failed for this cell (see WARNING above). "
                  "The acceptance conditions cannot be evaluated, so curve_neutral cannot be trusted "
                  "either even if it individually looked valid.")

    # PM 5th message's 3 named acceptance conditions. Each is checked and
    # reported BY NAME (not just an aggregate pass/fail) so a failure points
    # straight at which assumption broke, per 引き継ぎ §7-1-8.
    # CONDITION 1 WAS INVERTED ON 2026-07-28, when the defect it named was
    # fixed (OverrideManager startup axis reference).
    #
    # It used to require straight_nonneutral == DIRECT_AXIS_LATCH_FRAME1 --
    # i.e. it asserted that the bug still reproduces, as the proof that the
    # direct-axis path was wired up at all. That was the right check while the
    # bug was under investigation. Left as-is afterwards it would be a detector
    # that goes red precisely BECAUSE the product got fixed, and a permanently
    # red check is one nobody reads.
    #
    # So it now states the fixed contract: a wheel left off-centre by the
    # PREVIOUS session must not be mistaken for a driver on frame 1. The
    # "is the direct-axis path alive at all" question moved to the unit tests,
    # where a driver's MOVEMENT (rather than a leftover level) can be modelled
    # directly: OverrideManagerTest.StartupAxisReference* and
    # FfbInactiveKeepsDirectSteeringThreshold.
    #
    # Pre-fix reference measurement, kept so this cell's flip stays legible:
    #   test_results/f7_2x2_final.log -> DIRECT_AXIS_LATCH_FRAME1, latch at
    #   t=0.01, self_perpetuating=True.
    conditions = []
    if straight_nn is None or not straight_nn["cell_valid"]:
        conditions.append(("1: straight_nonneutral == NO_LATCH (a leftover t=0 wheel angle is not an intervention)",
                            False, "cell missing or a0-verification failed"))
    else:
        ok = straight_nn["signature"] == "NO_LATCH"
        detail = f"got signature={straight_nn['signature']!r}"
        if straight_nn["signature"] == "DIRECT_AXIS_LATCH_FRAME1":
            detail += (" -- this is the pre-2026-07-28 defect reappearing: the startup axis"
                       " reference in OverrideManager::Update() is not taking effect")
        conditions.append(("1: straight_nonneutral == NO_LATCH (a leftover t=0 wheel angle is not an intervention)",
                            ok, detail))
    if straight_sub is None or not straight_sub["cell_valid"]:
        conditions.append(("2: straight_nonneutral_sub == NO_LATCH (reproduces real -1.1k band, 9/9 no-latch; not over-sensitive)",
                            False, "cell missing or a0-verification failed"))
    else:
        ok = straight_sub["signature"] == "NO_LATCH"
        conditions.append(("2: straight_nonneutral_sub == NO_LATCH (reproduces real -1.1k band, 9/9 no-latch; not over-sensitive)",
                            ok, f"got signature={straight_sub['signature']!r}"))
    if straight_held is None or not straight_held["cell_valid"]:
        conditions.append(("3: straight_held == RESIDUAL_PATH_LATCH (a genuine held-wheel intervention IS caught; not deaf)",
                            False, "cell missing or a0-verification failed"))
    else:
        ok = straight_held["signature"] == "RESIDUAL_PATH_LATCH"
        conditions.append(("3: straight_held == RESIDUAL_PATH_LATCH (a genuine held-wheel intervention IS caught; not deaf)",
                            ok, f"got signature={straight_held['signature']!r}"))

    all_ok = all(ok for _, ok, _ in conditions)
    for name, ok, detail in conditions:
        status = "PASS" if ok else "!! FAIL"
        print(f"  [{status}] condition {name}: {detail}")

    if all_ok:
        print("  All 3 acceptance conditions PASS: curve_neutral is TRUSTED.")
    else:
        failed = [name.split(":")[0] for name, ok, _ in conditions if not ok]
        print(f"  !! {len(failed)}/3 acceptance condition(s) FAILED ({', '.join(failed)}): "
              "curve_neutral is NOT adopted. Investigate the named condition(s) above before trusting "
              "anything drawn from curve_neutral.")

    if curve_n and all_ok:
        if curve_n["signature"] == "RESIDUAL_PATH_LATCH":
            print("  curve x neutral LATCHED via the shadow/residual path: Sec.5-4 is a genuine, "
                  "INDEPENDENT defect (not just Sec.5-5 in disguise). Shadow-path verdict: "
                  f"{curve_n['shadow_path_analysis']['verdict']}")
        elif curve_n["signature"] == "NO_LATCH":
            print("  curve x neutral did NOT latch: Sec.5-4's real-machine symptom is NOT reproduced by "
                  "curvature alone. Consistent with 'Sec.5-4 == Sec.5-5 in disguise' (the curve is "
                  "incidental; a non-neutral t=0 wheel is the actual cause).")
        elif curve_n["signature"] == "DIRECT_AXIS_LATCH_FRAME1":
            print("  UNEXPECTED: curve x neutral latched via the DIRECT-AXIS path on frame 1 despite "
                  "a0=0 (<= steering_threshold). This contradicts the source-code reading in this "
                  "module's docstring -- investigate before trusting anything else here.")
        else:
            print(f"  curve x neutral: unclassified signature {curve_n['signature']!r} -- inspect raw CSV.")
    elif curve_n and not all_ok:
        print(f"  curve x neutral raw result (NOT adopted -- see failed condition(s) above): "
              f"signature={curve_n['signature']!r}")


def main_2x2(args) -> int:
    """PM redesign (2026-07-27): the primary experiment. Started as 4 fixed
    cells (2nd message), grew a negative control (4th message), then a 5th
    message replaced the non-neutral cells' mode with "plant" (now that
    GT_HEADLESS_FFB_PLANT_INIT_AT exists) and added a 6th cell (straight_held)
    as a positive control -- see CELLS' own comments for the full history and
    current 6-cell table. No --scenarios subset selection."""
    if not os.path.exists(DLL):
        print(f"FAIL: DLL not found at {DLL} -- run /build first (not doing it automatically). "
              "This script does NOT run headless simulation without it; nothing was executed.")
        return 1
    needed_scenarios = {scenario_name for _, scenario_name, _, _, _ in CELLS}
    for name in needed_scenarios:
        if not SCENARIOS[name].exists():
            print(f"FAIL: scenario file missing for '{name}': {SCENARIOS[name]}")
            return 1

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    cell_results = []
    for cell_id, scenario_name, wheel_condition, mode, extra in CELLS:
        print(f"== {cell_id} (scenario={scenario_name}, wheel={wheel_condition}) ==")
        rows, result = run_cell(cell_id, scenario_name, mode, extra, args.duration, args.onset_window, args.dt)
        _write_csv(rows, OUT_DIR / f"{cell_id}.csv")
        cell_results.append(result)
        print(f"  frames={result['n_frames_total']} a0={result['primed_axis_a0']:.5f} "
              f"({result['primed_axis_a0_wheel_deg']:.1f} deg) "
              f"direct_axis_predicted={result['direct_axis_predicted_to_fire']}")
        print(f"  frame1_block_reason={result['frame1_block_reason']} "
              f"(inactive_as_expected={result['frame1_block_reason_is_inactive']})")
        print(f"  signature={result['signature']} latch_t={result['latch_sim_time']} "
              f"self_perpetuating={result['self_perpetuating']}")

    summary_path = OUT_DIR / "summary_2x2.json"
    with open(summary_path, "w", encoding="utf-8") as fh:
        json.dump(cell_results, fh, indent=2, default=str)
    print(f"\nsummary written: {summary_path}")

    print_2x2_report(cell_results)
    return 0


def main_sweep(args) -> int:
    """Original 8-scenario x 3-plant-variant curvature sweep (task 1/2 as
    originally scoped, before the PM redesign). Kept available under
    --mode sweep -- still a valid, separately-useful measurement (the
    shadow/residual mechanism across curvature AND across the measured G29
    variance band), just no longer the PRIMARY deliverable."""
    matrix = [(name, variant) for name in args.scenarios for variant in PLANT_VARIANTS]

    if args.list:
        print(f"{len(args.scenarios)} scenarios x {len(PLANT_VARIANTS)} plant variants = {len(matrix)} runs")
        for name in args.scenarios:
            p = SCENARIOS[name]
            exists = "OK" if p.exists() else "MISSING"
            exp = EXPECTED_WHEEL_DEG.get(name)
            exp_str = "control (~0 deg expected)" if exp in (None, 0.0) else f"expected ~{exp:g} deg wheel-rotation onset"
            print(f"  [{exists}] {name}: {p}  ({exp_str})")
        for m, extra in PLANT_VARIANTS:
            print(f"  plant variant: {_plant_tag(extra)} -> {extra}")
        print(f"\nDLL: {DLL} ({'OK' if os.path.exists(DLL) else 'MISSING'})")
        return 0

    if not os.path.exists(DLL):
        print(f"FAIL: DLL not found at {DLL} -- run /build first (not doing it automatically). "
              "This script does NOT run headless simulation without it; nothing was executed.")
        return 1
    for name in args.scenarios:
        if not SCENARIOS[name].exists():
            print(f"FAIL: scenario file missing for '{name}': {SCENARIOS[name]}")
            return 1

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    all_verdicts = []
    for name, (mode, extra) in matrix:
        tag = _plant_tag(extra)
        print(f"== {name} / plant({tag}) ==")
        rows, verdict = run_one(name, SCENARIOS[name], extra, args.duration, args.onset_window, args.dt)
        _write_csv(rows, OUT_DIR / f"{name}__{tag}.csv")
        all_verdicts.append(verdict)
        print(f"  frames={verdict['n_frames_total']} "
              f"max|target| onset={verdict['max_abs_target_norm_onset']:.5f} axis-frac "
              f"({verdict['max_abs_target_wheel_deg_onset']:.2f} deg wheel-equiv, "
              f"expected ~{verdict['expected_wheel_deg']!r} deg)")
        print(f"  max residual onset={verdict['max_residual_onset']:.5f} "
              f"(threshold={verdict['residual_threshold']}) latch_t={verdict['latch_sim_time']}")
        print(f"  bootstrap_frames_onset={verdict['bootstrap_frames_onset']} "
              f"(ok<=1: {verdict['bootstrap_frames_ok']})")
        print(f"  VERDICT: {verdict['verdict']}")

    summary_path = OUT_DIR / "summary.json"
    with open(summary_path, "w", encoding="utf-8") as fh:
        json.dump(all_verdicts, fh, indent=2)
    print(f"\nsummary written: {summary_path}")

    print("\n=== onset-window verdict table ===")
    for v in all_verdicts:
        print(f"  {v['scenario']:<18} {v['plant_variant']:<22} {v['verdict']}")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--mode", choices=["2x2", "sweep"], default="2x2",
                     help="'2x2' (default, PM's current primary design): 6 fixed cells, see CELLS. "
                          "'sweep': original 8-scenario x 3-plant-variant curvature sweep.")
    ap.add_argument("--scenarios", nargs="+", choices=sorted(SCENARIOS), default=sorted(SCENARIOS),
                     help="(--mode sweep only) Subset of scenarios to run (default: all 8).")
    ap.add_argument("--duration", type=float, default=4.0, help="Capture window [s] (default 4.0).")
    ap.add_argument("--onset-window", type=float, default=1.0,
                     help="Onset analysis window [s] from t=0 (default 1.0, per team-lead spec).")
    ap.add_argument("--dt", type=float, default=REAL_MACHINE_DT,
                     help=f"Fixed timestep [s] (default {REAL_MACHINE_DT}, matches real-machine dt=0.01).")
    ap.add_argument("--list", action="store_true",
                     help="(--mode sweep only) Print the scenario x plant-variant matrix and exit "
                          "WITHOUT touching the DLL/simulator.")
    args = ap.parse_args()

    if args.mode == "2x2":
        if args.list:
            print(f"2x2 mode: {len(CELLS)} fixed cells (no --scenarios subset selection)")
            for cell_id, scenario_name, wheel_condition, mode, extra in CELLS:
                p = SCENARIOS[scenario_name]
                exists = "OK" if p.exists() else "MISSING"
                print(f"  [{exists}] {cell_id}: scenario={scenario_name} wheel={wheel_condition} "
                      f"mode={mode} extra={extra} -> {p}")
            print(f"\nDLL: {DLL} ({'OK' if os.path.exists(DLL) else 'MISSING'})")
            return 0
        return main_2x2(args)
    return main_sweep(args)


if __name__ == "__main__":
    raise SystemExit(main())
