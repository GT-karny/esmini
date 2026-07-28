"""feature:F7 -- permanent headless ACCEPTANCE harness for the AUTO_RESUME
lane-merge fix. Measurement-only: no C++ changes, no config changes.

WHY THIS FILE EXISTS: the existing lane-shift condition
(resume_ride_feel.run_network_arm_lane_shift) targets |ego.offset|=3.5m, but
ego.offset is roadmanager::Position::GetOffset() -- LANE-RELATIVE, re-
referenced at every lane boundary crossing. Proven in
test_results/f7_lane_offset_semantics_probe.txt (see also
vd_lane_offset_semantics_probe.py): at the AUTO_RESUME edge that condition
produced ego.lane=-4, ego.offset=-3.310, which is 1.360m BEYOND lane -4's
outer edge (half-width 1.950) -- i.e. the "one lane width" target actually
parked the car on the shoulder (lane -5, type="stop"), not in the adjacent
driving lane.

THE CORRECTED QUANTITY -- route-relative lateral deviation, continuous
across lane boundaries:

    dev = signed_center_to_center(route_lane -> current_lane) + ego.offset

signed_center_to_center is built from the parsed xodr lane-width table
(center-to-center distance between adjacent lane centers on the same side of
the road, e.g. lane -3 -> -4 on e6mini road 0 = -(3.5/2 + 3.9/2) = -3.70).
Verified against the probe data: dev just before the -3/-4 boundary =
-1.7482, just after = -3.70 + 1.9425 = -1.7575 -- continuous to 0.009m (one
frame of real motion). Widths and the route lane are PARSED (xodr / the
xosc's AssignRouteAction), never hardcoded.

DEFECT 2 CORRECTION (2026-07-27, verified against GT_esmini/src/control/
manualdrive/OverrideManager.cpp): the earlier "non_neutral_release" variant
pressed AUTO_RESUME WHILE THE WHEEL WAS STILL HELD, then released
RELEASE_DELAY_S=0.2s later. OverrideManager's resume_edge branch sets
lat_mode_=AUTO and suppresses the intervention check ONLY for the edge frame
itself (`return;` right after the mode flip) -- the very next frame runs the
normal per-frame check again, sees |steering|=initial_steer_hold still above
steering_threshold_, and re-latches MANUAL immediately. Releasing the wheel
0.2s later than that has no effect: override already re-latched on frame 1
and self-perpetuates (confirmed empirically: 1089 frames latched, 1 lone
AUTO-owned frame, NOT MEASURED). This is CORRECT PRODUCT BEHAVIOUR, not a
bug -- a resume press with the wheel still actively held is, by design, not
enough to hand lateral control back; the driver must actually let go.

That also means "the driver is still gripping the wheel at RESUME" is the
WRONG initial condition to test the merge feature with -- under that
condition override owns lateral and the merge profile never runs at all.
The merge's actual initial condition is a non-neutral VEHICLE DYNAMIC STATE
at the handover instant (a0_lat != 0, i.e. residual yaw rate / lateral
acceleration from a just-completed steering input), which is what the merge
profile matches at its start boundary -- a different thing from "wheel still
held". Four initial-steering conditions (neutral + len(SETTLE_S_VALUES)
non_neutral_settle<S> + non_neutral_held) are run for BOTH merge OFF and
merge ON (config-overlay, via resume_ride_feel._make_variant_speed_fixed --
shipped config as base, never touches build/):
  neutral                 -- steering released and settled before RESUME
                              (a0_lat ~= 0 at the edge, by design)
  non_neutral_settle<S>   -- hold steering at initial_steer_hold, RELEASE it
                              to 0, wait settle_s=<S> seconds (short enough
                              that the just-released turn's yaw rate has not
                              fully decayed), THEN press AUTO_RESUME with the
                              wheel already at 0 -- AUTO genuinely takes over
                              at a moment the vehicle is still turning
                              (a0_lat != 0). Run at settle_s in {0.15, 0.05}
                              (SETTLE_S_VALUES) to see how much residual yaw
                              survives at each settle window.
  non_neutral_held        -- RESUME pressed while the wheel is held nonzero
                              and NEVER released (settle_s=None) -- kept as
                              its own explicitly-labelled cell reproducing
                              the reported defect/correct-behaviour: this
                              latches MANUAL override on the very first
                              post-resume frame and self-perpetuates for the
                              rest of the run, so this variant's own cell is
                              expected to end up NOT MEASURED (0 AUTO-owned
                              post-resume frames) -- that is itself the
                              evidence (1089-frame latch count), not a
                              harness bug.
Each cell reports the number of frames override stayed latched immediately
after the resume edge, plus the edge dynamics (yaw_rate_at_edge,
a0_lat_at_edge, heading_dev_at_edge) -- a0_lat_at_edge is the exact quantity
the merge profile matches, and PROVES (rather than merely asserts) that a
non_neutral_settle* cell really did create a non-neutral condition. If
|a0_lat_at_edge| falls below a small floor (0.05 m/s^2), the cell prints an
explicit FAILED-TO-CREATE line instead of silently claiming non-neutrality
it did not achieve -- the same class of error the module's own WHY section
above documents for the old |offset|=3.5 condition.

DEFECT 3 CORRECTION (2026-07-28, see test_results/f7_resume_merge_handoff.md
"未確定の判断"): the non_neutral_settle{0.15,0.05} cells above both measured
|a0_lat_at_edge| ~= 0.19 -- INDISTINGUISHABLE from the neutral cell's own
~0.1947 (itself residual noise from the closed-loop lane-shift controller's
convergence tolerance, not steering-driven). Root cause, verified against
GT_esmini/src/control/RealVehicle.cpp StepLateralAndAttitude: the manual
steering command does not map to wheelAngle_ (and therefore yaw_rate/a_lat)
instantaneously -- wheelAngle_ RATE-LIMITS toward its target at a fixed
5 rad/s. Releasing the wheel to 0 after only NON_NEUTRAL_STEER_HOLD=0.15
lets wheelAngle_ unwind back to ~0 within ~0.03s -- so even the SHORTER
settle_s=0.05 window (5 frames) was already long enough to erase almost all
of the built-up yaw before the AUTO_RESUME edge was measured. The absolute
0.05 m/s^2 floor was also too weak on its own: it is comfortably cleared by
the neutral cell's own ~0.19 noise floor, so it could never distinguish a
real non-neutral condition from that noise (see "the weak check" fix
below).

The fix is two-part:
  (1) non_neutral_pulse<M> cells (PULSE_MAG_VALUES, magnitude M): run the
      existing dev-lane-shift maneuver to convergence UNCHANGED (steps 1-2
      of DEFECT 2's sequence are untouched), THEN apply a brief steering
      PULSE at magnitude M for PULSE_DUR_S seconds (long enough for
      wheelAngle_ to approach its own -- larger -- target, since ramp time
      scales with target angle / 5 rad/s), THEN release the wheel to 0 and
      press AUTO_RESUME in the SAME command packet (settle_s=0.0) so the
      edge-dynamics measurement frame is only ONE 0.01s ramp-step into
      wheelAngle_'s unwind, not after it has fully unwound. Swept at two
      magnitudes (PULSE_MAG_VALUES) so a0_lat_at_edge can be seen to SCALE
      with pulse magnitude, not just cross one pass/fail line. A short
      pulse (rather than a sustained hold) keeps the extra lateral drift it
      adds on top of the already-converged one-lane-width dev small, so the
      resume-edge inside-driving-lane check (unchanged, still gates every
      cell -- see _lane_check_at_resume) has margin to still pass; if it
      does not for a given pulse magnitude, that cell reports NOT MEASURED
      with the reason, same as any other cell.
  (2) the floor check itself (A0_LAT_NON_NEUTRAL_RATIO below) is now
      RELATIVE to the neutral cell's own measured |a0_lat_at_edge| for the
      same merge condition, not an absolute constant -- a non-neutral cell
      must clear >= 3x that reference to count as a genuinely distinct
      non-neutral condition; otherwise the cell prints an explicit
      FAILED-TO-CREATE line (same class of error the module's own WHY
      section above documents for the old |offset|=3.5 condition) instead
      of silently claiming non-neutrality it did not achieve.

Peak-quantity gate: a cell's peak/derivative quantities (a_lat_peak,
yaw_rate_peak, steer_rate_peak) require at least MIN_AUTO_FRAMES_PEAK_FLOOR
(10, i.e. 0.1s @ dt=0.01) AUTO-owned frames in the 1.0s post-edge window to
count as MEASURED -- a warned/thin sample is reported NOT MEASURED, never as
a number computed from too few samples to be meaningful (a single AUTO-owned
frame cannot even form a derivative: there is no second sample to difference
against).

The resume_merge_* config keys may not be wired into this DLL yet (parallel
work) -- detected via a dedicated preflight that checks for a "resume_merge"
block in the RAW telemetry JSON; if absent, ON cells are SKIPPED with a loud,
explicit message, never silently reported as "no effect".

Usage (DriverScript venv -- never bare python):
  DriverScript\\.venv\\Scripts\\python.exe GT_esmini\\test\\headless\\vd_resume_merge_acceptance.py

Output: printed to stdout AND written to test_results/f7_resume_merge_acceptance.txt
"""

from __future__ import annotations

import ctypes
import json
import os
import sys
import tempfile
import time
import xml.etree.ElementTree as ET

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _HERE)
import vd_resume_transient as vrt  # noqa: E402

ROOT = vrt.ROOT
sys.path.insert(0, os.path.join(ROOT, "scripts", "ffb_spike"))
import resume_ride_feel as rrf  # noqa: E402

OUT_DIR = os.path.join(ROOT, "test_results")
OUT_TXT = os.path.join(OUT_DIR, "f7_resume_merge_acceptance.txt")

SPEED_MPS = 8.0
DT = 0.01
NON_NEUTRAL_STEER_HOLD = 0.15  # > steering_threshold (0.05 shipped) so it reliably keeps a raw-axis MANUAL latch armed
DEV_TOL_M = 0.3
HEADING_TOL_RAD = 0.03
STABLE_FRAMES_NEEDED = 30
MANEUVER_CAP_S = 15.0
POST_RESUME_S = 10.5
METRIC_WINDOW_PEAKS_S = 1.0
METRIC_WINDOW_DEV_S = 10.0
# defect fix (2026-07-27, see module docstring "DEFECT 2 CORRECTION"): the
# driver RELEASES the wheel to 0 BEFORE pressing AUTO_RESUME, waits settle_s
# seconds (residual yaw from the just-released turn has not fully decayed at
# these short windows), THEN presses RESUME with the wheel already at 0 --
# this is what actually lets AUTO take over while the vehicle is still in a
# non-neutral dynamic state (a0_lat != 0), unlike the old "resume while still
# holding" sequence which just re-latched MANUAL on frame 1 (verified against
# OverrideManager.cpp: resume_edge suppresses the intervention check for the
# edge frame ONLY, not thereafter). None = never release (the old
# always-held behavior), kept available as its own explicitly-labelled cell
# -- see main()'s cell list.
DEFAULT_SETTLE_S = 0.15
# run at least two settle windows so the report shows how much residual yaw
# survives at each -- see main()'s cell list.
SETTLE_S_VALUES = (0.15, 0.05)
# defect fix (2026-07-28, module docstring "DEFECT 3 CORRECTION"): PULSE
# cells -- run the dev-lane-shift maneuver to convergence UNCHANGED, then
# apply a brief steering pulse at each of these magnitudes for PULSE_DUR_S
# seconds, then release-and-resume in the SAME packet (settle_s=0.0) -- see
# run_dev_lane_shift's hold_dur_s/hold_phase_label params and main()'s cell
# list. PULSE_DUR_S is long enough for wheelAngle_ (RealVehicle.cpp,
# rate-limited at 5 rad/s) to approach its target for both magnitudes below
# (target_angle/5 rad/s = 0.0366s at 0.3, 0.0732s at 0.6 -- both << 0.15s)
# while staying short enough that the extra lateral drift it adds on top of
# the already-converged one-lane-width dev stays small (inside-driving-lane
# check margin). Two magnitudes so a0_lat_at_edge can be seen to SCALE with
# pulse magnitude, not just cross one pass/fail line.
PULSE_DUR_S = 0.15
PULSE_MAG_VALUES = (0.3, 0.6)
# defect fix (2026-07-28, "the weak check" -- module docstring "DEFECT 3
# CORRECTION" part 2): the OLD absolute floor (0.05 m/s^2) was cleared by
# the neutral cell's own ~0.19 m/s^2 noise floor and therefore could never
# prove a cell was genuinely non-neutral. The floor is now RELATIVE to the
# neutral cell's own measured |a0_lat_at_edge| for the SAME merge condition
# (tracked in main() as neutral_a0_lat_ref, passed into analyze_cell) -- a
# non-neutral cell must clear this many times that reference before it
# counts as a distinct non-neutral condition; otherwise the cell prints an
# explicit FAILED-TO-CREATE line instead of silently claiming non-neutrality
# it did not achieve.
A0_LAT_NON_NEUTRAL_RATIO = 3.0
# defect fix (thin-sample gate): a "peak" computed from fewer than this many
# AUTO-owned frames in the 1.0s peak window is not a measurement -- below
# this floor (10 frames = 0.1s @ dt=0.01) the cell is reported NOT MEASURED
# instead of printing numbers from a near-empty sample.
MIN_AUTO_FRAMES_PEAK_FLOOR = 10
# a derivative needs a 2nd sample to difference against -- below this count
# a derivative-based quantity (steer_rate, and transitively yaw_rate/a_lat,
# which are themselves built from a heading/position derivative) cannot be
# computed from the window AT ALL, let alone as a "peak".
MIN_AUTO_FRAMES_FOR_DERIVATIVE = 2


class MergeNotPresentError(RuntimeError):
    pass


# --------------------------------------------------------------------------
# xodr / route parsing -- nothing hardcoded
# --------------------------------------------------------------------------
def _resolve_xodr_path(xosc_path: str) -> str:
    root = ET.parse(xosc_path).getroot()
    el = root.find(".//RoadNetwork/LogicFile")
    if el is None or not el.get("filepath"):
        raise RuntimeError(
            f"{xosc_path}: no RoadNetwork/LogicFile -- cannot resolve xodr"
        )
    fp = el.get("filepath")
    return (
        fp
        if os.path.isabs(fp)
        else os.path.abspath(os.path.join(os.path.dirname(xosc_path), fp))
    )


def _parse_route(xosc_path: str) -> tuple[int, int]:
    root = ET.parse(xosc_path).getroot()
    wp = root.find(".//AssignRouteAction//Route/Waypoint/Position/LanePosition")
    if wp is None:
        raise RuntimeError(
            f"{xosc_path}: no AssignRouteAction/.../LanePosition -- cannot determine route lane"
        )
    return int(wp.get("roadId")), int(wp.get("laneId"))


def _parse_xodr_lanes(xodr_path: str, road_id: int) -> dict:
    root = ET.parse(xodr_path).getroot()
    road = root.find(f".//road[@id='{road_id}']")
    if road is None:
        raise RuntimeError(f"{xodr_path}: no <road id='{road_id}'>")
    sections = road.findall(".//laneSection")
    if not sections:
        raise RuntimeError(f"{xodr_path}: road {road_id} has no <laneSection>")
    if len(sections) > 1:
        print(
            f"  NOTE: road {road_id} has {len(sections)} laneSections; using only the first "
            f"(s={sections[0].get('s')}) -- this maneuver stays within a short s-range near the route origin."
        )
    lanes = []
    for lane in sections[0].findall(".//lane"):
        w_el = lane.find("width")
        lanes.append(
            {
                "id": int(lane.get("id")),
                "type": lane.get("type"),
                "width_m": float(w_el.get("a")) if w_el is not None else 0.0,
            }
        )
    lanes.sort(key=lambda x: -x["id"])
    return {"road_id": road_id, "lanes": lanes}


def _lane_centers(lanes: list) -> dict:
    """t-offset of each lane's CENTER, built by walking outward from the t=0
    reference line -- generalizes the WHY-section arithmetic to any lane id."""
    widths = {l["id"]: l["width_m"] for l in lanes}
    centers = {0: 0.0}
    b = 0.0
    for i in sorted(k for k in widths if k > 0):
        centers[i] = b + widths[i] / 2.0
        b += widths[i]
    c = 0.0
    for i in sorted((k for k in widths if k < 0), reverse=True):
        centers[i] = c - widths[i] / 2.0
        c -= widths[i]
    return centers


def _compute_dev(
    ego: dict, route_track: int, route_lane: int, centers: dict
) -> float | None:
    track, lane, off = ego.get("track"), ego.get("lane"), ego.get("offset")
    if (
        track != route_track
        or lane not in centers
        or route_lane not in centers
        or off is None
    ):
        return None
    return (centers[lane] - centers[route_lane]) + off


# --------------------------------------------------------------------------
# merge-support preflight (own throwaway init, never mixed into a real run)
# --------------------------------------------------------------------------
def _check_merge_block_present(speed_mps: float) -> tuple[bool, list]:
    tmpdir = tempfile.mkdtemp(prefix="vd_resume_merge_preflight_")
    cfg = {"input_type": "stub", "resume_merge_enabled": True}
    xosc = rrf._make_variant_speed_fixed(tmpdir, cfg, speed_mps)
    lib = vrt._load_lib()
    argv_list = [
        b"vd_resume_merge_preflight",
        b"--osc",
        xosc.encode(),
        b"--headless",
        b"--fixed_timestep",
        b"0.01",
    ]
    argv = (ctypes.c_char_p * len(argv_list))(*argv_list)
    rc = lib.GT_InitWithArgs(len(argv_list), argv)
    if rc != 0:
        raise RuntimeError(f"merge preflight: GT_InitWithArgs rc={rc}")
    buf = ctypes.create_string_buffer(40960)
    raw = None
    try:
        for _ in range(5):
            lib.GT_Step(0.01)
            n = lib.GT_GetVirtualDriverTelemetry(0, buf, len(buf))
            if n > 0:
                raw = json.loads(buf.value.decode())
    finally:
        lib.GT_Close()
        try:
            os.remove(xosc)
        except OSError:
            pass
    if raw is None:
        raise RuntimeError(
            "merge preflight: GT_GetVirtualDriverTelemetry never returned a frame"
        )
    return ("resume_merge" in raw), sorted(raw.keys())


# --------------------------------------------------------------------------
# the acceptance maneuver: A_baseline -> B_dev_lane_shift (closed-loop,
# reuses resume_ride_feel's heading-tracking P-controller structure, driven
# by `dev` instead of raw ego.offset) -> C_hold (steering held at
# initial_steer_hold) -> [settle_s is not None:] C2_settle (steering RELEASED
# to 0, held there for settle_s seconds -- residual yaw from the just-released
# turn has not fully decayed at these short windows) -> D_resume_pulse
# (AUTO_RESUME pressed, wheel already at 0) -> E_post_resume.
# [settle_s is None:] D_resume_pulse / E_post_resume instead hold the wheel at
# initial_steer_hold FOREVER (never released) -- the old always-held defect-
# reproduction path, kept for the non_neutral_held cell only. See module
# docstring "DEFECT 2 CORRECTION" for why the settle_s path replaced the old
# release_delay_s ("resume while still held, release later") path.
#
# hold_dur_s / hold_phase_label (added 2026-07-28, module docstring "DEFECT 3
# CORRECTION"): the C_hold phase duration was hardcoded at 0.5s -- now a
# parameter so the PULSE cells (non_neutral_pulse<M>) can reuse this exact
# same hold->release->resume machinery with a SHORT hold (PULSE_DUR_S) at a
# LARGER initial_steer_hold (the pulse magnitude M) instead of the long
# 0.5s hold at NON_NEUTRAL_STEER_HOLD. Paired with settle_s=0.0 (which the
# existing settle branch below already supports but no prior cell combined
# with a nonzero initial_steer_hold), the release-and-resume happens in the
# SAME command packet that ends the hold, so wheelAngle_ (RealVehicle.cpp,
# 5 rad/s rate-limited) has only had one 0.01s step to start unwinding by
# the time the edge frame is captured. hold_phase_label overrides the
# auto-derived "C_hold"/"C_release" telemetry phase tag (pulse cells pass
# "C_pulse" so the frame dump is self-describing).
# --------------------------------------------------------------------------
def run_dev_lane_shift(
    dev_target_m: float,
    speed_mps: float,
    route_track: int,
    route_lane: int,
    centers: dict,
    merge_enabled: bool,
    initial_steer_hold: float = 0.0,
    settle_s: float | None = DEFAULT_SETTLE_S,
    hold_dur_s: float = 0.5,
    hold_phase_label: str | None = None,
) -> dict:
    import socket

    cmd = {"steering": 0.0, "throttle": 0.0, "brake": 0.0, "buttons": 0, "send": False}
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    def send_now():
        if cmd["send"]:
            pkt = vrt.WIRE.pack(
                vrt.MAGIC,
                cmd["steering"],
                cmd["throttle"],
                cmd["brake"],
                0.0,
                0,
                cmd["buttons"] & 0xFFFFFFFF,
            )
            try:
                sock.sendto(pkt, ("127.0.0.1", vrt.INPUT_PORT))
            except OSError:
                pass

    tmpdir = tempfile.mkdtemp(prefix="vd_resume_merge_accept_")
    cfg = {
        "input_type": "network",
        "input_port": vrt.INPUT_PORT,
        "input_transport": "udp",
        "ffb_target_track_enabled": False,
        "ad_steering_envelope_enabled": True,
        "resume_merge_enabled": bool(merge_enabled),
    }
    xosc = rrf._make_variant_speed_fixed(tmpdir, cfg, speed_mps)

    lib = vrt._load_lib()
    argv_list = [
        b"vd_resume_merge_accept",
        b"--osc",
        xosc.encode(),
        b"--headless",
        b"--fixed_timestep",
        f"{DT:.6f}".encode(),
    ]
    argv = (ctypes.c_char_p * len(argv_list))(*argv_list)
    rc = lib.GT_InitWithArgs(len(argv_list), argv)
    if rc != 0:
        raise RuntimeError(f"GT_InitWithArgs rc={rc}")

    buf = ctypes.create_string_buffer(40960)
    merge_block_seen = [False]

    def tel_raw():
        n = lib.GT_GetVirtualDriverTelemetry(0, buf, len(buf))
        return json.loads(buf.value.decode()) if n > 0 else None

    frames: list = []

    def step_and_capture(phase: str):
        send_now()
        lib.GT_Step(DT)
        raw = tel_raw()
        if raw is None:
            return None
        s = vrt._slim(raw, phase)
        ego = raw.get("ego", {})
        s["ego_track"] = ego.get("track")
        s["ego_s"] = ego.get("s")
        s["dev"] = _compute_dev(ego, route_track, route_lane, centers)
        if "resume_merge" in raw:
            merge_block_seen[0] = True
        s["manual_raw_steer"] = cmd["steering"]
        frames.append(s)
        return s

    cmd.update(steering=0.0, throttle=0.0, brake=0.0, buttons=0, send=False)
    for _ in range(int(round(2.0 / DT))):
        step_and_capture("A_baseline")

    h0 = frames[-1]["ego_h"] if frames else 0.0
    converged, stable_count, last_dev, last_hdev, steps_used = False, 0, None, None, 0
    for step_idx in range(int(round(MANEUVER_CAP_S / DT))):
        steps_used = step_idx + 1
        raw = tel_raw()
        if raw is None:
            send_now()
            lib.GT_Step(DT)
            continue
        dev_now = _compute_dev(raw.get("ego", {}), route_track, route_lane, centers)
        dev_for_control = (
            dev_now
            if dev_now is not None
            else (last_dev if last_dev is not None else 0.0)
        )
        heading_dev = vrt._wrapped_diff(raw["ego"]["h"], h0)
        dev_error = dev_target_m - dev_for_control
        desired_hdev = max(
            -rrf._LANE_SHIFT_MAX_DESIRED_HEADING,
            min(rrf._LANE_SHIFT_MAX_DESIRED_HEADING, rrf._LANE_SHIFT_KP * dev_error),
        )
        steer = (rrf._LANE_SHIFT_KH / rrf._LANE_SHIFT_C_STEER) * (
            heading_dev - desired_hdev
        )
        steer = max(-rrf._LANE_SHIFT_MAX_STEER, min(rrf._LANE_SHIFT_MAX_STEER, steer))
        cmd.update(steering=steer, throttle=0.0, brake=0.0, buttons=0, send=True)
        f2 = step_and_capture("B_dev_lane_shift")
        if f2 is not None:
            last_dev = f2.get("dev")
            last_hdev = vrt._wrapped_diff(f2["ego_h"], h0)
        if (
            last_dev is not None
            and last_hdev is not None
            and abs(last_dev - dev_target_m) <= DEV_TOL_M
            and abs(last_hdev) <= HEADING_TOL_RAD
        ):
            stable_count += 1
            if stable_count >= STABLE_FRAMES_NEEDED:
                converged = True
                break
        else:
            stable_count = 0

    maneuver_diag = {
        "converged": converged,
        "steps_used": steps_used,
        "final_dev_m": last_dev,
        "final_heading_dev_rad": last_hdev,
        "target_dev_m": dev_target_m,
        "dev_tol_m": DEV_TOL_M,
        "heading_tol_rad": HEADING_TOL_RAD,
    }

    cmd.update(
        steering=initial_steer_hold, throttle=0.0, brake=0.0, buttons=0, send=True
    )
    label = (
        hold_phase_label
        if hold_phase_label is not None
        else ("C_hold" if initial_steer_hold else "C_release")
    )
    for _ in range(int(round(hold_dur_s / DT))):
        step_and_capture(label)

    n_d = int(round(0.4 / DT))
    n_e = int(round(POST_RESUME_S / DT))
    if settle_s is None:
        # HELD (non_neutral_held only): the wheel is NEVER released -- resume
        # is pressed while still holding it, and it stays held straight
        # through D and E. Per OverrideManager.cpp (verified), the resume
        # edge only suppresses the intervention re-check for the edge frame
        # itself, so the very next frame sees |steering| still above
        # steering_threshold_ and re-latches MANUAL -- this is the old
        # always-held defect-reproduction path, kept as its own
        # explicitly-labelled cell (see module docstring).
        for i in range(n_d):
            cmd.update(
                steering=initial_steer_hold, buttons=vrt.BTN_AUTO_RESUME, send=True
            )
            step_and_capture("D_resume_pulse")
        for i in range(n_e):
            cmd.update(steering=initial_steer_hold, buttons=0, send=True)
            step_and_capture("E_post_resume")
    else:
        # RELEASE-THEN-SETTLE (neutral uses settle_s=0.0, non_neutral_settle*
        # uses settle_s in {0.15, 0.05}): release the wheel to 0 BEFORE
        # pressing AUTO_RESUME, so the resume edge always finds the wheel
        # already neutral and AUTO genuinely takes over lateral control. The
        # settle_s wait (short by design) is what leaves residual yaw
        # rate/lateral-acceleration in the vehicle at the moment RESUME is
        # pressed -- the actual non-neutral DYNAMIC-STATE condition the merge
        # profile's start boundary matches against.
        cmd.update(steering=0.0, throttle=0.0, brake=0.0, buttons=0, send=True)
        for _ in range(int(round(settle_s / DT))):
            step_and_capture("C2_settle")
        for i in range(n_d):
            cmd.update(steering=0.0, buttons=vrt.BTN_AUTO_RESUME, send=True)
            step_and_capture("D_resume_pulse")
        for i in range(n_e):
            cmd.update(steering=0.0, buttons=0, send=True)
            step_and_capture("E_post_resume")

    sock.close()
    lib.GT_Close()
    try:
        os.remove(xosc)
    except OSError:
        pass
    return {
        "frames": frames,
        "maneuver_ok": converged,
        "maneuver_diag": maneuver_diag,
        "merge_block_seen": merge_block_seen[0],
        "h0": h0,
    }


# --------------------------------------------------------------------------
# analysis
# --------------------------------------------------------------------------
def _frames_latched_after_edge(frames: list, idx0: int) -> int:
    """Number of consecutive frames immediately after the auto_transition
    edge (idx0, the DLL's own manual->auto transition flag) during which
    override_lateral re-latched back to True (MANUAL) before returning to
    AUTO (False) -- 0 means it never re-latched at all. This count is itself
    the interesting evidence for the non-neutral-steering defect: a held
    (never-released) wheel re-latches override on the very next frame after
    the edge (t=0.01s post-edge) and self-perpetuates -- the count then runs
    to (near) end-of-run."""
    n = 0
    for f in frames[idx0 + 1 :]:
        if f.get("override_lateral") is True:
            n += 1
        else:
            break
    return n


def _edge_dynamics(frames: list, idx0: int, h0: float) -> dict:
    """yaw_rate_at_edge / a0_lat_at_edge / heading_dev_at_edge at the
    AUTO_RESUME edge frame (idx0) -- the exact quantity (a0_lat) the
    resume-merge profile matches at its start boundary, so this is what
    PROVES a non_neutral_settle* cell actually created a non-neutral
    condition rather than merely being labelled one.

    Same backward-difference-over-dt convention as
    resume_ride_feel.analyze_resume_case's yaw_rate/a_lat_realized
    (yaw_rate = wrapped_diff(h[idx0], h[idx0-1]) / dt, a0_lat = yaw_rate *
    speed AT idx0 -- speed at the endpoint of the differencing interval, same
    alignment as that function's `a_lat_realized = [yr * v[i+1] ...]`) and
    vd_resume_transient.compute_metrics' heading_error_at_edge (deviation
    from h0, the pre-maneuver AUTO heading reference captured at the end of
    phase A in run_dev_lane_shift)."""
    if idx0 < 1:
        return {
            "yaw_rate_at_edge": None,
            "a0_lat_at_edge": None,
            "heading_dev_at_edge": None,
        }
    f0, f1 = frames[idx0 - 1], frames[idx0]
    dt = (f1.get("sim_time") or 0.0) - (f0.get("sim_time") or 0.0)
    h_prev, h_edge, v_edge = f0.get("ego_h"), f1.get("ego_h"), f1.get("ego_speed")
    if dt <= 0 or h_prev is None or h_edge is None or v_edge is None:
        return {
            "yaw_rate_at_edge": None,
            "a0_lat_at_edge": None,
            "heading_dev_at_edge": None,
        }
    yaw_rate = vrt._wrapped_diff(h_edge, h_prev) / dt
    a0_lat = yaw_rate * v_edge
    heading_dev = vrt._wrapped_diff(h_edge, h0)
    return {
        "yaw_rate_at_edge": yaw_rate,
        "a0_lat_at_edge": a0_lat,
        "heading_dev_at_edge": heading_dev,
    }


def _lane_check_at_resume(frame: dict, width_by_id: dict, type_by_id: dict) -> dict:
    lane, off = frame.get("ego_lane"), frame.get("ego_offset")
    if lane is None or off is None:
        return {"ok": False, "reason": "missing ego_lane/ego_offset at resume edge"}
    width = width_by_id.get(lane)
    if width is None:
        return {
            "ok": False,
            "reason": f"lane {lane} not found in the parsed lane table",
        }
    half = width / 2.0
    ok = abs(off) <= half + 1e-9
    reason = (
        ""
        if ok
        else (
            f"BEYOND lane {lane}'s edge by {abs(off) - half:.3f}m "
            f"(|offset|={abs(off):.3f} > half-width {half:.3f}, type={type_by_id.get(lane)})"
        )
    )
    return {"ok": ok, "lane": lane, "offset": off, "half_width": half, "reason": reason}


def _dev_window_metrics(
    frames: list,
    idx0: int,
    window_s: float = METRIC_WINDOW_DEV_S,
    tol_m: float = DEV_TOL_M,
) -> dict:
    t0 = frames[idx0]["sim_time"]
    auto = [
        f
        for f in frames[idx0:]
        if (f["sim_time"] - t0) <= window_s + 1e-9
        and f.get("override_lateral") is False
    ]
    dev_at_edge = frames[idx0].get("dev")
    conv_t, overshoot, traj, next_t = None, 0.0, [], 0.0
    sign0 = 1 if (dev_at_edge or 0.0) >= 0 else -1
    for f in auto:
        d, rel = f.get("dev"), f["sim_time"] - t0
        if d is None:
            continue
        if conv_t is None and abs(d) < tol_m:
            conv_t = rel
        if (d * sign0) < 0:
            overshoot = max(overshoot, abs(d))
        if rel >= next_t - 1e-6:
            traj.append((round(rel, 2), round(d, 4)))
            next_t += 0.1
    return {
        "dev_at_edge_m": dev_at_edge,
        "auto_owned_frames_10s": len(auto),
        "dev_converge_s": conv_t,
        "dev_overshoot_m": overshoot,
        "dev_trajectory": traj,
    }


def _frames_equal(a: list, b: list) -> tuple[bool, str]:
    if len(a) != len(b):
        return False, f"frame count differs: {len(a)} vs {len(b)}"
    for i, (fa, fb) in enumerate(zip(a, b)):
        for k, va in fa.items():
            vb = fb.get(k)
            if va != vb:
                return False, f"frame {i} field {k!r}: {va!r} != {vb!r}"
    return True, ""


def analyze_cell(
    frames: list,
    maneuver_ok: bool,
    maneuver_diag: dict,
    route_track: int,
    width_by_id: dict,
    type_by_id: dict,
    h0: float,
    claims_non_neutral: bool,
    neutral_a0_lat_ref: float | None,
    w,
) -> dict:
    if not maneuver_ok:
        w(
            f"  NOT MEASURED -- dev-lane-shift maneuver did not converge: {maneuver_diag}"
        )
        return {"status": "NOT MEASURED", "reason": "maneuver did not converge"}
    route_check = rrf._route_departure_check(frames)
    w(
        f"  route/lane-departure check (reused: resume_ride_feel._route_departure_check): {route_check}"
    )
    if route_check["departed"]:
        w("  NOT MEASURED -- route departure detected")
        return {
            "status": "NOT MEASURED",
            "reason": "route departure",
            "route_check": route_check,
        }
    edge_idxs = [i for i, f in enumerate(frames) if f.get("auto_transition")]
    if not edge_idxs:
        w("  NOT MEASURED -- no auto_transition edge found")
        return {"status": "NOT MEASURED", "reason": "no auto_transition edge"}
    idx0 = edge_idxs[0]
    latched_after_edge = _frames_latched_after_edge(frames, idx0)
    w(
        f"  frames override stayed latched (MANUAL) immediately after the resume edge (t={frames[idx0]['sim_time']:.2f}s): "
        f"{latched_after_edge}"
    )

    # defect fix (2026-07-27, evidence requirement): a0_lat_at_edge is the
    # EXACT quantity the resume-merge profile matches at its start boundary
    # -- printing it (and yaw_rate/heading_dev alongside it) PROVES whether a
    # cell that claims to be non-neutral actually is, rather than asserting
    # it via label alone.
    edge_dyn = _edge_dynamics(frames, idx0, h0)
    yre, a0e, hde = (
        edge_dyn["yaw_rate_at_edge"],
        edge_dyn["a0_lat_at_edge"],
        edge_dyn["heading_dev_at_edge"],
    )
    w(
        f"  edge dynamics (a0_lat_at_edge is the exact quantity the merge profile matches at its start "
        f"boundary): yaw_rate_at_edge={'n/a' if yre is None else f'{yre:.4f}'} rad/s  "
        f"a0_lat_at_edge={'n/a' if a0e is None else f'{a0e:.4f}'} m/s^2  "
        f"heading_dev_at_edge={'n/a' if hde is None else f'{hde:.4f}'} rad"
    )
    # defect fix (2026-07-28, module docstring "DEFECT 3 CORRECTION" part 2 /
    # "the weak check"): the floor is RELATIVE to the neutral cell's own
    # measured |a0_lat_at_edge| for the same merge condition (passed in as
    # neutral_a0_lat_ref -- see main()'s neutral_a0_lat tracking), not an
    # absolute constant. An absolute floor here was proven too weak: the old
    # 0.05 m/s^2 floor was cleared by the NEUTRAL cell's own ~0.19 m/s^2
    # noise floor too, so it could never distinguish a real non-neutral
    # condition from that noise.
    a0_lat_ratio_vs_neutral = None
    non_neutral_confirmed = None
    if claims_non_neutral:
        if a0e is None:
            w(
                "  CANNOT VERIFY non-neutral claim -- a0_lat_at_edge is n/a for this cell (missing "
                "telemetry at the edge frame), so it cannot be compared against the neutral reference."
            )
        elif neutral_a0_lat_ref is None:
            w(
                "  CANNOT VERIFY non-neutral claim -- the neutral cell's own |a0_lat_at_edge| for this "
                "merge condition was not measured, so there is no reference to compare against (reporting "
                f"this cell's own |a0_lat_at_edge|={abs(a0e):.4f} m/s^2 for the record, unjudged)."
            )
        else:
            a0_lat_ratio_vs_neutral = (
                abs(a0e) / abs(neutral_a0_lat_ref)
                if neutral_a0_lat_ref != 0
                else float("inf")
            )
            required = A0_LAT_NON_NEUTRAL_RATIO * abs(neutral_a0_lat_ref)
            non_neutral_confirmed = abs(a0e) >= required
            if not non_neutral_confirmed:
                w(
                    f"  FAILED TO CREATE a distinct non-neutral condition -- |a0_lat_at_edge|={abs(a0e):.4f} "
                    f"m/s^2 is only {a0_lat_ratio_vs_neutral:.2f}x the neutral cell's own |a0_lat_at_edge|="
                    f"{abs(neutral_a0_lat_ref):.4f} m/s^2 (same merge condition), below the required "
                    f"{A0_LAT_NON_NEUTRAL_RATIO:g}x ({required:.4f} m/s^2). This cell's steering sequence did "
                    "not leave the vehicle in a state distinguishable from the neutral cell's own noise floor "
                    "at the AUTO_RESUME edge, despite being labelled non_neutral -- the same class of error as "
                    "the old |offset|=3.5 condition that never created the condition it claimed (see module "
                    "docstring)."
                )
            else:
                w(
                    f"  non-neutral condition CONFIRMED at the edge: |a0_lat_at_edge|={abs(a0e):.4f} m/s^2 is "
                    f"{a0_lat_ratio_vs_neutral:.2f}x the neutral cell's own |a0_lat_at_edge|="
                    f"{abs(neutral_a0_lat_ref):.4f} m/s^2 (same merge condition) -- clears the required "
                    f"{A0_LAT_NON_NEUTRAL_RATIO:g}x."
                )

    lc = _lane_check_at_resume(frames[idx0], width_by_id, type_by_id)
    w(f"  resume-edge inside-driving-lane check: {lc}")
    if not lc["ok"]:
        w(
            "  NOT MEASURED -- ego is not inside a driving lane at the resume edge (this is the exact "
            "failure the old |offset|=3.5 condition had)"
        )
        return {
            "status": "NOT MEASURED",
            "reason": "ego beyond lane edge at resume",
            "lane_check": lc,
            "latched_after_edge": latched_after_edge,
            "a0_lat_ratio_vs_neutral": a0_lat_ratio_vs_neutral,
            "non_neutral_confirmed": non_neutral_confirmed,
            **edge_dyn,
        }

    m1s = rrf.analyze_resume_case(frames, window_s=METRIC_WINDOW_PEAKS_S)
    t0 = frames[idx0]["sim_time"]
    n_auto_1s = sum(
        1
        for f in frames[idx0:]
        if (f["sim_time"] - t0) <= METRIC_WINDOW_PEAKS_S + 1e-9
        and f.get("override_lateral") is False
    )
    devm = _dev_window_metrics(frames, idx0)
    w(
        f"  AUTO-owned frames: 1.0s peak-window={n_auto_1s}  10.0s dev-window={devm['auto_owned_frames_10s']}"
    )
    if n_auto_1s == 0 or devm["auto_owned_frames_10s"] == 0:
        w(
            f"  NOT MEASURED -- 0 AUTO-owned frames post-resume: override never returned to AUTO "
            f"(self-perpetuating MANUAL re-latch -- override stayed latched for {latched_after_edge} frame(s) "
            "after the edge -- exactly the reported non-neutral-steering defect if this is a non_neutral cell)"
        )
        return {
            "status": "NOT MEASURED",
            "reason": "0 AUTO-owned post-resume frames",
            "lane_check": lc,
            "route_check": route_check,
            "latched_after_edge": latched_after_edge,
            "n_auto_1s": n_auto_1s,
            "a0_lat_ratio_vs_neutral": a0_lat_ratio_vs_neutral,
            "non_neutral_confirmed": non_neutral_confirmed,
            **edge_dyn,
            **devm,
        }

    # defect fix (thin-sample gate): a warned condition is never reported as
    # if it were a measurement. Below MIN_AUTO_FRAMES_PEAK_FLOOR AUTO-owned
    # frames in the 1.0s peak window, the peak/derivative quantities
    # (a_lat_peak, yaw_rate_peak, steer_rate_peak) are suppressed -- printed
    # as NOT MEASURED, never as a number -- while all raw evidence (frame
    # counts, latch count, dev trajectory, warning text) is still printed so
    # the reader sees everything that happened.
    if n_auto_1s < MIN_AUTO_FRAMES_PEAK_FLOOR:
        if n_auto_1s < MIN_AUTO_FRAMES_FOR_DERIVATIVE:
            w(
                f"  WARNING: only {n_auto_1s} AUTO-owned frame(s) in the 1.0s peak window -- fewer than "
                f"{MIN_AUTO_FRAMES_FOR_DERIVATIVE}, so there is no second AUTO-owned sample to difference "
                "against; derivative-based quantities (steer_rate, and transitively yaw_rate/a_lat) cannot "
                "be computed from this window at all, let alone as a 'peak'."
            )
        else:
            w(
                f"  WARNING: only {n_auto_1s} AUTO-owned frames in the 1.0s peak window -- thin sample, "
                "treat peak numbers with caution"
            )
        w(
            f"  NOT MEASURED -- {n_auto_1s} AUTO-owned frame(s) in the 1.0s peak window is below the "
            f"measurement floor of {MIN_AUTO_FRAMES_PEAK_FLOOR} (0.1s @ dt=0.01 required for peak quantities "
            "to count as measured); a warned condition is never reported as measured."
        )
        w(
            "  peak |a_lat|=NOT MEASURED  peak |yaw_rate|=NOT MEASURED  peak |steer_rate|=NOT MEASURED"
        )
        w(
            f"  onset_effective_jerk (handoff frame, ungated)={m1s['onset_effective_jerk']}"
        )
        w(
            f"  dev_at_edge={devm['dev_at_edge_m']}  dev_converge_s(<{DEV_TOL_M}m)={devm['dev_converge_s']}  "
            f"dev_overshoot_past_zero_m={devm['dev_overshoot_m']:.4f}"
        )
        w(f"  dev_trajectory (0.1s, AUTO-owned only): {devm['dev_trajectory']}")
        return {
            "status": "NOT MEASURED",
            "reason": f"only {n_auto_1s} AUTO-owned frame(s) in the 1.0s peak window "
            f"(< floor of {MIN_AUTO_FRAMES_PEAK_FLOOR})",
            "lane_check": lc,
            "route_check": route_check,
            "latched_after_edge": latched_after_edge,
            "n_auto_1s": n_auto_1s,
            "onset_effective_jerk": m1s["onset_effective_jerk"],
            "a0_lat_ratio_vs_neutral": a0_lat_ratio_vs_neutral,
            "non_neutral_confirmed": non_neutral_confirmed,
            **edge_dyn,
            **devm,
        }

    w(
        f"  peak |a_lat|={m1s['a_lat_peak']:.4f} m/s^2  peak |yaw_rate|={m1s['yaw_rate_peak']:.4f} rad/s  "
        f"peak |steer_rate|={m1s['steer_rate_peak_env_out']:.4f} /s"
    )
    w(f"  onset_effective_jerk (handoff frame, ungated)={m1s['onset_effective_jerk']}")
    w(
        f"  dev_at_edge={devm['dev_at_edge_m']}  dev_converge_s(<{DEV_TOL_M}m)={devm['dev_converge_s']}  "
        f"dev_overshoot_past_zero_m={devm['dev_overshoot_m']:.4f}"
    )
    w(f"  dev_trajectory (0.1s, AUTO-owned only): {devm['dev_trajectory']}")
    return {
        "status": "MEASURED",
        "lane_check": lc,
        "route_check": route_check,
        "a_lat_peak": m1s["a_lat_peak"],
        "yaw_rate_peak": m1s["yaw_rate_peak"],
        "steer_rate_peak": m1s["steer_rate_peak_env_out"],
        "onset_effective_jerk": m1s["onset_effective_jerk"],
        "n_auto_1s": n_auto_1s,
        "latched_after_edge": latched_after_edge,
        "a0_lat_ratio_vs_neutral": a0_lat_ratio_vs_neutral,
        "non_neutral_confirmed": non_neutral_confirmed,
        **edge_dyn,
        **devm,
    }


# --------------------------------------------------------------------------
def _check_exclusive_input_port() -> str:
    """HARD GATE: this harness drives the VD through a FIXED UDP port
    (vd_resume_transient.INPUT_PORT = 9100). The DLL binds it; we send to it.

    If anything else already holds that port -- a second copy of this harness, a
    packaged GT_Sim the user is driving, another worker's run -- Windows UDP will
    happily let both sockets bind and then deliver each datagram to only ONE of
    them, nondeterministically. The run does not crash: it produces PLAUSIBLE BUT
    WRONG numbers, which is the worst possible failure mode for this project (a
    whole day was lost to 'failure that looks like success').

    So: probe the port with SO_EXCLUSIVEADDRUSE and refuse to run if it is taken.
    Fail loudly rather than measure quietly.
    """
    import socket as _s

    probe = _s.socket(_s.AF_INET, _s.SOCK_DGRAM)
    try:
        # Windows-only flag; on other platforms fall back to a plain bind probe.
        if hasattr(_s, "SO_EXCLUSIVEADDRUSE"):
            probe.setsockopt(_s.SOL_SOCKET, _s.SO_EXCLUSIVEADDRUSE, 1)
        probe.bind(("127.0.0.1", vrt.INPUT_PORT))
    except OSError as e:
        return (
            f"FAIL: UDP port {vrt.INPUT_PORT} (vd_resume_transient.INPUT_PORT) is already in "
            f"use ({e}). Something else is holding it -- another harness run, another worker, "
            f"or a packaged GT_Sim the user is driving. Refusing to run: a shared input port "
            f"silently misroutes steering datagrams and yields plausible-but-wrong numbers "
            f"instead of an error. Stop the other process (or wait for it) and re-run."
        )
    finally:
        probe.close()
    return ""


def main() -> int:
    lines: list = []

    def w(s: str = ""):
        print(s)
        lines.append(s)

    w("=" * 78)
    w("feature:F7 AUTO_RESUME lane-merge ACCEPTANCE harness")
    w("=" * 78)

    port_err = _check_exclusive_input_port()
    if port_err:
        w(port_err)
        os.makedirs(OUT_DIR, exist_ok=True)
        open(OUT_TXT, "w", encoding="utf-8").write("\n".join(lines) + "\n")
        return 1
    w(f"input-port exclusivity check: PASS (UDP {vrt.INPUT_PORT} free)")

    if not os.path.exists(vrt.DLL):
        w(
            f"FAIL: DLL not found at {vrt.DLL} -- run /build first (not doing it automatically)"
        )
        os.makedirs(OUT_DIR, exist_ok=True)
        open(OUT_TXT, "w", encoding="utf-8").write("\n".join(lines) + "\n")
        return 1
    dll_mtime = time.strftime(
        "%Y-%m-%d %H:%M:%S", time.localtime(os.path.getmtime(vrt.DLL))
    )
    w(f"DLL under test : {vrt.DLL}  mtime={dll_mtime}")
    w(f"scenario base  : {vrt.BASE_XOSC}")
    w(f"config (SHIPPED, overlay-only, build/ never touched): {vrt.SHIPPED_CFG}")

    xodr_path = _resolve_xodr_path(vrt.BASE_XOSC)
    route_track, route_lane = _parse_route(vrt.BASE_XOSC)
    w(f"xodr (resolved from xosc RoadNetwork/LogicFile): {xodr_path}")
    w(f"route (parsed from AssignRouteAction): road={route_track} lane={route_lane}")

    xodr_info = _parse_xodr_lanes(xodr_path, route_track)
    width_by_id = {l["id"]: l["width_m"] for l in xodr_info["lanes"]}
    type_by_id = {l["id"]: l["type"] for l in xodr_info["lanes"]}
    w("\n--- lane geometry (parsed, not hardcoded) ---")
    for l in xodr_info["lanes"]:
        w(f"  lane {l['id']:>3}  type={l['type']:<8}  width={l['width_m']:.3f}m")
    centers = _lane_centers(xodr_info["lanes"])
    target_lane = route_lane - 1
    dev_target = centers[target_lane] - centers[route_lane]
    w(
        f"\ntarget lane (one lane further from center, this maneuver's calibrated push direction) = {target_lane}"
    )
    w(
        f"dev_target = centers[{target_lane}] - centers[{route_lane}] = {dev_target:.4f}m "
        f"(route-relative; this is what 'true adjacent-lane center' means here)"
    )

    w("\n" + "=" * 78)
    w(
        "DETERMINISM GATE (neutral / merge OFF, run twice, frame series must be identical)"
    )
    w("=" * 78)
    r1 = run_dev_lane_shift(
        dev_target,
        SPEED_MPS,
        route_track,
        route_lane,
        centers,
        merge_enabled=False,
        initial_steer_hold=0.0,
        settle_s=0.0,
    )
    r2 = run_dev_lane_shift(
        dev_target,
        SPEED_MPS,
        route_track,
        route_lane,
        centers,
        merge_enabled=False,
        initial_steer_hold=0.0,
        settle_s=0.0,
    )
    ident, why = _frames_equal(r1["frames"], r2["frames"])
    w(f"identical={ident}  {why}")
    if not ident:
        w(
            "\nDETERMINISM GATE FAILED -- every downstream number is NOT MEASURED. Stopping."
        )
        os.makedirs(OUT_DIR, exist_ok=True)
        open(OUT_TXT, "w", encoding="utf-8").write("\n".join(lines) + "\n")
        return 1
    w("PASS -- proceeding, run #1 reused as the neutral/merge-OFF result.")

    w("\n" + "=" * 78)
    w(
        "MERGE-SUPPORT PREFLIGHT (own throwaway init; checks for a 'resume_merge' block in RAW telemetry)"
    )
    w("=" * 78)
    try:
        merge_supported, raw_keys = _check_merge_block_present(SPEED_MPS)
    except RuntimeError as e:
        w(f"PREFLIGHT ERROR: {e}")
        merge_supported = False
    w(
        f"resume_merge_enabled=true requested. top-level telemetry keys={raw_keys if not merge_supported else '(see below)'}"
        if not merge_supported
        else f"resume_merge block PRESENT. top-level telemetry keys={raw_keys}"
    )
    if not merge_supported:
        w(
            "MERGE NOT PRESENT IN THIS DLL: resume_merge_enabled=true was requested but the telemetry JSON "
            "carries no 'resume_merge' block. This is expected -- the config keys are parsed "
            "(VirtualDriverConfig::ResumeMergeCfg) but not yet wired into ControllerVirtualDriver::Step or "
            "VirtualDriverTelemetryJson -- the implementation is landing in parallel. "
            "ON-vs-OFF comparison is SKIPPED for both steering conditions below; this is reported explicitly, "
            "never as a silent 'no effect' finding."
        )

    # defect fix (2026-07-27, see module docstring "DEFECT 2 CORRECTION"):
    # non_neutral now runs as len(SETTLE_S_VALUES) explicitly-labelled
    # release-then-settle steering variants ("non_neutral_settle<S>" -- wheel
    # RELEASED to 0, held there settle_s seconds, THEN AUTO_RESUME pressed --
    # the fix for the cell that could never measure the merge, replacing the
    # old "resume while still held, release later" sequence which just
    # re-latched MANUAL on frame 1 per OverrideManager.cpp) plus
    # "non_neutral_held" (the wheel never released, the old always-held
    # behavior, kept as its own cell so the self-perpetuating-latch case
    # stays reproducible and reportable -- claims_non_neutral=True here too
    # since it is a labelled non-neutral condition, expected to FAIL the
    # a0_lat floor check for a different reason: override never hands
    # lateral back to AUTO at all, so the edge dynamics come from a frame
    # where AUTO's own candidate was never applied).
    # variants tuple: (steer_label, steer_val, settle_s, claims_nn, pulse_mag, pulse_dur_s).
    # pulse_mag/pulse_dur_s are None for every pre-existing variant (unchanged
    # behavior); non_neutral_pulse<M> is new (module docstring "DEFECT 3
    # CORRECTION") -- see PULSE_DUR_S/PULSE_MAG_VALUES.
    variants = [("neutral", 0.0, 0.0, False, None, None)]
    for settle_s in SETTLE_S_VALUES:
        variants.append(
            (
                f"non_neutral_settle{settle_s:g}",
                NON_NEUTRAL_STEER_HOLD,
                settle_s,
                True,
                None,
                None,
            )
        )
    variants.append(
        ("non_neutral_held", NON_NEUTRAL_STEER_HOLD, None, True, None, None)
    )
    for pulse_mag in PULSE_MAG_VALUES:
        variants.append(
            (
                f"non_neutral_pulse{pulse_mag:g}",
                pulse_mag,
                0.0,
                True,
                pulse_mag,
                PULSE_DUR_S,
            )
        )

    cells = [("neutral", 0.0, False, 0.0, False, None, None, r1)]
    for steer_label, steer_val, settle_s, claims_nn, pulse_mag, pulse_dur_s in variants:
        for merge_label, merge_on in (("merge_off", False), ("merge_on", True)):
            if steer_label == "neutral" and not merge_on:
                continue  # already have r1 from the determinism gate
            if merge_on and not merge_supported:
                w(
                    f"\n-- {steer_label}/{merge_label} -- SKIPPED (merge not present in this DLL)"
                )
                continue
            cells.append(
                (
                    steer_label,
                    steer_val,
                    merge_on,
                    settle_s,
                    claims_nn,
                    pulse_mag,
                    pulse_dur_s,
                    None,
                )
            )

    # defect fix (2026-07-28, module docstring "DEFECT 3 CORRECTION" part 2):
    # each merge condition's own neutral-cell |a0_lat_at_edge| is tracked here
    # so every non_neutral_* cell for that SAME merge condition can be judged
    # RELATIVE to it (A0_LAT_NON_NEUTRAL_RATIO), not against an absolute
    # constant -- see analyze_cell's neutral_a0_lat_ref param. variants'/
    # cells' construction order guarantees "neutral" runs (for both merge_off
    # and, if supported, merge_on) before any non_neutral_* variant, so the
    # reference is always populated by the time it is looked up below (falls
    # back to the OTHER merge condition's neutral value only if this
    # condition's own neutral cell failed to produce a measured edge -- the
    # edge dynamics are measured BEFORE the merge feature's own trajectory
    # shaping engages, so a cross-condition reference is still physically
    # meaningful, just not the preferred apples-to-apples one).
    neutral_a0_lat: dict[bool, float | None] = {}

    results = {}
    for (
        steer_label,
        steer_val,
        merge_on,
        settle_s,
        claims_nn,
        pulse_mag,
        pulse_dur_s,
        reuse,
    ) in cells:
        merge_label = "merge_on" if merge_on else "merge_off"
        key = f"{steer_label}/{merge_label}"
        if pulse_mag is not None:
            variant_desc = (
                f"PULSE: dev-lane-shift converges as usual (steps 1-2 unchanged), then "
                f"steering={pulse_mag:g} held for {pulse_dur_s:g}s (phase C_pulse), then "
                "released to 0 and AUTO_RESUME pressed in the SAME command packet (settle_s=0.0, "
                "i.e. settle ~0 -- see module docstring 'DEFECT 3 CORRECTION')"
            )
        elif settle_s is None:
            variant_desc = (
                f"settle_s=None (wheel held at {steer_val:g} forever after AUTO_RESUME)"
            )
        else:
            variant_desc = (
                f"settle_s={settle_s:g}s (wheel held at {steer_val:g}, RELEASED to 0, then "
                f"AUTO_RESUME pressed {settle_s:g}s after the release)"
            )
        w(
            f"\n{'=' * 78}\nCELL {key}  (initial_steer_hold={steer_val:g}; steering variant: {variant_desc})\n{'=' * 78}"
        )
        run = (
            reuse
            if reuse is not None
            else run_dev_lane_shift(
                dev_target,
                SPEED_MPS,
                route_track,
                route_lane,
                centers,
                merge_enabled=merge_on,
                initial_steer_hold=steer_val,
                settle_s=settle_s,
                hold_dur_s=(pulse_dur_s if pulse_mag is not None else 0.5),
                hold_phase_label=("C_pulse" if pulse_mag is not None else None),
            )
        )
        if merge_on and not run["merge_block_seen"]:
            w(
                "  WARNING: preflight reported merge support, but this run's own telemetry never showed a "
                "'resume_merge' block -- inconsistent DLL behavior, treat this cell's results with caution."
            )
        neutral_ref = None
        if steer_label != "neutral":
            neutral_ref = neutral_a0_lat.get(merge_on)
            if neutral_ref is None:
                neutral_ref = neutral_a0_lat.get(not merge_on)
        results[key] = analyze_cell(
            run["frames"],
            run["maneuver_ok"],
            run["maneuver_diag"],
            route_track,
            width_by_id,
            type_by_id,
            run["h0"],
            claims_nn,
            neutral_ref,
            w,
        )
        if steer_label == "neutral":
            neutral_a0_lat[merge_on] = results[key].get("a0_lat_at_edge")

    w(f"\n{'=' * 78}\nSUMMARY\n{'=' * 78}")
    for key, r in results.items():
        a0e = r.get("a0_lat_at_edge")
        a0_str = "n/a" if a0e is None else f"{a0e:.4f}"
        ratio = r.get("a0_lat_ratio_vs_neutral")
        ratio_str = "" if ratio is None else f"  ratio_vs_neutral={ratio:.2f}x"
        confirmed = r.get("non_neutral_confirmed")
        confirmed_str = (
            "" if confirmed is None else f"  non_neutral_confirmed={confirmed}"
        )
        w(
            f"  {key:<30} status={r['status']}  a0_lat_at_edge={a0_str}{ratio_str}{confirmed_str}"
            + (
                f"  reason={r.get('reason')}"
                if r["status"] != "MEASURED"
                else f"  a_lat_peak={r['a_lat_peak']:.3f} yaw_rate_peak={r['yaw_rate_peak']:.3f} "
                f"steer_rate_peak={r['steer_rate_peak']:.3f} dev_converge_s={r['dev_converge_s']} "
                f"dev_overshoot_m={r['dev_overshoot_m']:.3f}"
            )
        )

    # --- VERDICT ---------------------------------------------------------
    #
    # feature:F7 — this file is named "acceptance" and returned 0 no matter
    # what every cell reported. Its own vocabulary already carries the answer:
    # analyze_cell() returns status "MEASURED" only when the cell produced a
    # usable measurement, and "NOT MEASURED" with a reason otherwise (maneuver
    # did not converge, route departure, no auto_transition edge, ego beyond
    # the lane edge at resume, zero AUTO-owned post-resume frames). A cell that
    # could not be measured is not a cell that passed -- it is a cell whose
    # question went unanswered, and reporting that as success is the same
    # "zero evaluations = green" mistake this project has now hit three times.
    #
    # The non-neutral cells carry a second, independent obligation: they exist
    # to prove the wheel really was off-centre when AUTO_RESUME landed, which
    # analyze_cell records as non_neutral_confirmed. A cell that claims to be
    # non-neutral but could not confirm it is measuring something other than
    # what its name says.
    unmeasured = [k for k, r in results.items() if r.get("status") != "MEASURED"]
    unconfirmed = [
        k for k, r in results.items() if r.get("non_neutral_confirmed") is False
    ]

    w(f"\n{'=' * 78}\nVERDICT\n{'=' * 78}")
    if not results:
        w("  RESULT: NOT MEASURED — no cells ran at all.")
    elif unmeasured:
        w(
            f"  RESULT: NOT MEASURED — {len(unmeasured)}/{len(results)} cell(s) produced no "
            f"usable measurement: {', '.join(unmeasured)}"
        )
        for k in unmeasured:
            w(f"      {k}: {results[k].get('reason')}")
    elif unconfirmed:
        w(
            f"  RESULT: FAIL — {len(unconfirmed)} cell(s) claim a non-neutral wheel but "
            f"could not confirm it: {', '.join(unconfirmed)}"
        )
    else:
        w(
            f"  RESULT: PASS — {len(results)}/{len(results)} cells measured"
            + (
                f"; non-neutral confirmed where claimed"
                if any(r.get("non_neutral_confirmed") for r in results.values())
                else ""
            )
        )

    os.makedirs(OUT_DIR, exist_ok=True)
    with open(OUT_TXT, "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines) + "\n")
    print(f"\nfull report written: {OUT_TXT}")

    if not results:
        return 2
    if unmeasured:
        return 2
    if unconfirmed:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
