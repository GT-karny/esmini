"""feature:F7 (F7c investigation) — headless AUTO-resume steering transient repro.

User report (実機, 2026-07-25): after driving manually off-route and pressing
AUTO_RESUME, the AD "slams" the wheel to rejoin the route — safe but not
comfortable, not Level2/3-realistic. This harness reproduces that transient
HEADLESS (no G29/SDL2) so it can be measured and iterated on without hardware.

Two arms isolate which layer is responsible:
  arm1 (input_type=network, ffb_target_track_enabled=false):
    Pure AD-vs-vehicle-physics path. UDP PedalSteer (PSTC) packets latch a
    MANUAL steering override, create a lateral offset, then a BTN_AUTO_RESUME
    pulse hands back to AUTO. If the transient appears here, the AD path
    (PIDPurePursuitDriver / TrajectoryShortPlanner re-join) is implicated —
    this is the MUST-RUN arm (mechanically isolates AD-only causation).
  arm2 (input_type=headless_ffb, ffb_target_track_enabled=true):
    Exercises the FFB target-track servo (HeadlessFfbInput / SyntheticSink,
    same synthetic-wheel harness as scripts/vd_ffb_headless_smoke.py) so the
    servo's own target-jump-on-resume can be observed via ffb.commanded_force
    / ffb.position_error. BEST-EFFORT: HeadlessFfbInput::Poll() hardwires
    buttons=0 (no BTN_AUTO_RESUME channel) and GT_HEADLESS_FFB_FROZEN_AT is
    read once at Configure() time (no mid-run wheel-angle change) — see the
    "arm2 structural notes" comment on run_ffb_arm() for how this harness
    works around both.

Both arms build a per-run virtual_driver.json + scenario variant via the
Controller's ConfigFile property (same pattern as scripts/vd_override_smoke.py
and scripts/vd_ffb_headless_smoke.py) — build/GT_esmini/config/virtual_driver.json
is NEVER touched, so there is nothing to restore.

Usage (DriverScript venv):
  DriverScript\\.venv\\Scripts\\python.exe GT_esmini\\test\\headless\\vd_resume_transient.py
  DriverScript\\.venv\\Scripts\\python.exe GT_esmini\\test\\headless\\vd_resume_transient.py --arms arm1 --targets 1.0 2.0
"""
from __future__ import annotations

import argparse
import ctypes
import csv
import json
import math
import os
import socket
import struct
import tempfile
import xml.etree.ElementTree as ET

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
DLL = os.path.join(ROOT, "build", "GT_esmini", "Release", "GT_esminiLib.dll")
BASE_XOSC = os.path.join(ROOT, "resources", "xosc", "virtual_driver_basic.xosc")
SHIPPED_CFG = os.path.join(ROOT, "GT_esmini", "config", "virtual_driver.json")
BUILD_CFG = os.path.join(ROOT, "build", "GT_esmini", "config", "virtual_driver.json")
OUT_DIR = os.path.join(ROOT, "test_results", "vd_resume_transient")

DT = 0.05
INPUT_PORT = 9100
MAGIC = 0x50535443  # 'PSTC'
WIRE = struct.Struct("<I4diI")  # magic, steering, throttle, brake, clutch, gear, buttons
BTN_AUTO_RESUME = 1 << 7

# Bicycle-model constants used to derive delta/kappa/a_lat/omega from the AD's
# normalized driver.steer command (added for the normal-vs-pathological safety
# envelope comparison — team-lead spec). MAX_STEER_ANGLE matches
# virtual_driver.json's "max_steer_angle" (shipped default 0.61 rad). WHEEL_BASE
# CORRECTED per team-lead review: IDriverModel.hpp's "double wheel_base = 2.7"
# is only the struct DEFAULT and is overridden at runtime — the value actually
# used is ControllerVirtualDriver.cpp:297 "const double wheel_base =
# object_->boundingbox_.dimensions_.length_ * 0.6;". This harness's scenario
# (resources/xosc/virtual_driver_basic.xosc) uses VehicleCatalog "car_white",
# whose <BoundingBox><Dimensions length="5.04" .../> (VehicleCatalog.xosc:33)
# gives wheel_base = 5.04*0.6 = 3.024 — confirmed by reading both sources, not
# assumed. (The earlier 2.7 value overstated kappa, and therefore a_lat_cmd
# and omega_cmd, by ~11%; |dδ/dt| is unaffected — it has no wheel_base term.)
WHEEL_BASE = 3.024      # [m], = car_white bbox length (5.04) * 0.6, per
                        # ControllerVirtualDriver.cpp:297
MAX_STEER_ANGLE = 0.61  # [rad], virtual_driver.json "max_steer_angle" default

def _load_lib():
    lib = ctypes.CDLL(DLL)
    lib.GT_InitWithArgs.argtypes = [ctypes.c_int, ctypes.POINTER(ctypes.c_char_p)]
    lib.GT_InitWithArgs.restype = ctypes.c_int
    lib.GT_Step.argtypes = [ctypes.c_double]
    lib.GT_Close.argtypes = []
    lib.GT_GetVirtualDriverTelemetry.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
    lib.GT_GetVirtualDriverTelemetry.restype = ctypes.c_int
    return lib


def _make_variant(tmpdir: str, cfg_overrides: dict, speed_mps: float, push_triggers_out: bool = True) -> str:
    """Per-run virtual_driver.json (base = shipped config + overrides) and a
    scenario variant pointing VirtualDriverController.ConfigFile at it.

    The base scenario's own StopAction/StopTrigger (t=13s / t=20s, see
    resources/xosc/virtual_driver_basic.xosc) are pushed far out so a slow
    lateral-offset ramp (large --targets) never runs into the vehicle
    braking to a stop mid-measurement (bit that vd_override_smoke.py's
    docstring calls out as a real trap). AccelAction target speed is set to
    speed_mps so the measured transient can be compared against a hand
    calculation at a known speed.
    """
    cfg_path = os.path.join(tmpdir, "virtual_driver.json")
    base = {}
    if os.path.exists(SHIPPED_CFG):
        base = json.loads(open(SHIPPED_CFG, encoding="utf-8").read())
    base.update(cfg_overrides)
    open(cfg_path, "w", encoding="utf-8").write(json.dumps(base, indent=2))

    tree = ET.parse(BASE_XOSC)
    root = tree.getroot()

    base_dir = os.path.dirname(os.path.abspath(BASE_XOSC))
    for tag in ("LogicFile", "SceneGraphFile"):
        for el in root.findall(f".//{tag}"):
            fp = el.get("filepath")
            if fp and not os.path.isabs(fp):
                el.set("filepath", os.path.abspath(os.path.join(base_dir, fp)))
    for el in root.findall(".//CatalogLocations//Directory"):
        pth = el.get("path")
        if pth and not os.path.isabs(pth):
            el.set("path", os.path.abspath(os.path.join(base_dir, pth)))

    # Push trigger times out of the way (headroom for slow ramps). Skipped for
    # the normal-operation baseline run, which wants the SCENARIO'S OWN
    # straight->curve->lane-change->stop timing intact (that is exactly the
    # "normal operation" envelope the safety clamp must not clip).
    if push_triggers_out:
        for cond_name, new_value in (("LaneChangeStart", "500.0"), ("StopStart", "500.0"),
                                      ("QuitCondition", "600")):
            for cond in root.findall(f".//Condition[@name='{cond_name}']"):
                stc = cond.find(".//SimulationTimeCondition")
                if stc is not None:
                    stc.set("value", new_value)

    # "AccelAction" is the Action's NAME ATTRIBUTE, not a tag — the old
    # ".//AccelAction//AbsoluteTargetSpeed" xpath therefore matched nothing and
    # every run silently used the scenario's hard-coded 15.0 m/s target no
    # matter what --speed said. Selecting on the attribute is not enough on its
    # own either: the scenario has a SECOND AbsoluteTargetSpeed (StopAction,
    # value 0.0) that must NOT be rewritten. Empty match is a hard failure so a
    # renamed action can never re-open the same silent hole.
    accel_targets = root.findall(
        ".//Action[@name='AccelAction']//AbsoluteTargetSpeed")
    if not accel_targets:
        raise RuntimeError(
            "vd_resume_transient: no AbsoluteTargetSpeed under Action[@name='AccelAction'] "
            f"in {BASE_XOSC} — the --speed sweep would silently run at the scenario's own "
            "hard-coded speed. Fix the xpath or the scenario before trusting any result.")
    for target in accel_targets:
        target.set("value", f"{speed_mps:.2f}")

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


def _slim(frame: dict, phase: str) -> dict:
    """Drop the bulky preview/midlong/policy blocks — not needed for this
    measurement and they would bloat the saved JSON/CSV across ~hundreds of
    frames x 5 runs."""
    ego = frame.get("ego", {})
    ov = frame.get("override", {})
    ffb = frame.get("ffb", {})
    dr = frame.get("driver", {})
    env = frame.get("envelope", {})
    return {
        "phase": phase,
        "sim_time": frame.get("sim_time"),
        "ego_x": ego.get("x"), "ego_y": ego.get("y"), "ego_h": ego.get("h"),
        "ego_speed": ego.get("speed"), "ego_lane": ego.get("lane"), "ego_offset": ego.get("offset"),
        "override_lateral": ov.get("lateral"), "override_longitudinal": ov.get("longitudinal"),
        "manual_transition": ov.get("manual_transition"), "auto_transition": ov.get("auto_transition"),
        "ffb_target_active": ffb.get("target_active"), "ffb_commanded_force": ffb.get("commanded_force"),
        "ffb_position_error": ffb.get("position_error"),
        "driver_steer": dr.get("steer"), "driver_lateral_error": dr.get("lateral_error"),
        # PIDPurePursuitDriver.cpp: heading_error = alpha = atan2(local_y, local_x) —
        # the pure-pursuit angle from vehicle heading to the lookahead point (NOT a
        # decomposed pure road-tangent heading error; it blends lateral offset and
        # heading misalignment). Kept as supplementary context alongside the
        # geometric heading-error-vs-road estimate computed in compute_metrics.
        "driver_heading_error": dr.get("heading_error"),
        # AdSteeringEnvelope telemetry (added post-envelope build). "driver_steer"
        # above stays the RAW pre-envelope AD proposal (ControllerVirtualDriver.cpp
        # dsnap.steer, untouched by the envelope); envelope_steer_out is what is
        # actually handed to physics + FFB after clamping. steer_in should equal
        # driver_steer bit-for-bit (both are the envelope's input).
        "envelope_active": env.get("active"),
        "envelope_lat_accel_active": env.get("lateral_accel_active"),
        "envelope_yaw_rate_active": env.get("yaw_rate_active"),
        "envelope_steer_rate_active": env.get("steer_rate_active"),
        "envelope_steer_in": env.get("steer_in"),
        "envelope_steer_out": env.get("steer_out"),
    }


def run_network_arm(target_offset_m: float, speed_mps: float, envelope_enabled: bool,
                     steer_cmd: float | None = None,
                     ramp_cap_s: float = 20.0, post_resume_s: float = 5.0) -> list[dict]:
    """arm1: AUTO baseline -> steer to target_offset_m -> release -> RESUME -> capture.

    UDP packets are sent SYNCHRONOUSLY, one per GT_Step() call, from THIS
    thread, immediately before the Step — not from a free-running background
    thread. (First implementation used a background sender thread at 50Hz,
    matching scripts/vd_override_smoke.py's pattern; a determinism check —
    2x identical full sweeps — showed it was NOT reproducible: edge_sim_times
    shifted by +-1 frame [0.05s] run-to-run because the sender thread's real
    wall-clock timing raced this loop's own Step cadence, cascading into
    every downstream peak metric by 5-10%, up to ~10% on offset_at_edge.
    Synchronous send removes the race: every Step() is preceded by exactly
    one send of the CURRENT command state, in deterministic program order.
    Re-verified deterministic after this change — see the harness report.)
    """
    cmd = {"steering": 0.0, "throttle": 0.0, "brake": 0.0, "buttons": 0, "send": False}
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    def send_now():
        if not cmd["send"]:
            return
        pkt = WIRE.pack(MAGIC, cmd["steering"], cmd["throttle"], cmd["brake"],
                         0.0, 0, cmd["buttons"] & 0xFFFFFFFF)
        try:
            sock.sendto(pkt, ("127.0.0.1", INPUT_PORT))
        except OSError:
            pass

    tmpdir = tempfile.mkdtemp(prefix="vd_resume_arm1_")
    cfg = {"input_type": "network", "input_port": INPUT_PORT, "input_transport": "udp",
           "ffb_target_track_enabled": False,
           "ad_steering_envelope_enabled": envelope_enabled}
    xosc = _make_variant(tmpdir, cfg, speed_mps)

    lib = _load_lib()
    argv_list = [b"vd_resume", b"--osc", xosc.encode(), b"--headless", b"--fixed_timestep", b"0.05"]
    argv = (ctypes.c_char_p * len(argv_list))(*argv_list)
    rc = lib.GT_InitWithArgs(len(argv_list), argv)
    if rc != 0:
        raise RuntimeError(f"GT_InitWithArgs rc={rc}")

    buf = ctypes.create_string_buffer(16384)

    def tel():
        n = lib.GT_GetVirtualDriverTelemetry(0, buf, len(buf))
        return json.loads(buf.value.decode()) if n > 0 else None

    frames: list[dict] = []

    def run(phase: str, n_steps: int):
        for _ in range(n_steps):
            send_now()
            lib.GT_Step(DT)
            f = tel()
            if f:
                frames.append(_slim(f, phase))

    # Phase A: AUTO baseline
    cmd.update(steering=0.0, throttle=0.0, brake=0.0, buttons=0, send=False)
    run("A_baseline", int(2.0 / DT))

    # Phase B: steer to create lateral offset, monitor ego.offset until target reached.
    # steer_cmd scales down with target: a constant 0.5 was found (empirically, first
    # run of this harness) to overshoot small targets badly (0.5m target -> 1.7m+ at
    # the resume edge, because the vehicle keeps drifting on its post-release heading
    # through phase C/D) and to drive the vehicle OFF THE ASSIGNED ROUTE for 2-3m
    # targets (esmini logs "Ego moved out of route ... SetPathS()"), after which the
    # offset telemetry is not meaningful. A milder, target-scaled command tracks more
    # linearly; a hard divergence cap (below) still aborts the ramp if it runs away.
    if steer_cmd is None:
        steer_cmd = max(0.15, min(0.5, 0.20 + 0.05 * target_offset_m))
    offset_hard_cap = target_offset_m * 2.0 + 1.0
    cmd.update(steering=steer_cmd, throttle=0.0, brake=0.0, buttons=0, send=True)
    reached = False
    diverged = False
    for _ in range(int(ramp_cap_s / DT)):
        send_now()
        lib.GT_Step(DT)
        f = tel()
        if f:
            frames.append(_slim(f, "B_offset_ramp"))
            off = abs(f["ego"]["offset"])
            if off >= offset_hard_cap:
                diverged = True
                break
            if off >= target_offset_m:
                reached = True
                break
    if diverged:
        print(f"  WARNING: ramp DIVERGED past hard cap {offset_hard_cap:.1f}m before reaching "
              f"target {target_offset_m}m (steer_cmd={steer_cmd:.3f}) — likely off-route; "
              f"aborted phase B early")
    elif not reached:
        last_off = frames[-1]["ego_offset"] if frames else None
        print(f"  WARNING: target offset {target_offset_m}m not reached within {ramp_cap_s}s "
              f"cap (last ego.offset={last_off}, steer_cmd={steer_cmd:.3f})")

    # Phase C: release wheel (latch must hold — feature:F7 core guarantee)
    cmd.update(steering=0.0, throttle=0.0, brake=0.0, buttons=0, send=True)
    run("C_release", int(0.5 / DT))

    # Phase D: RESUME pulse
    cmd.update(steering=0.0, throttle=0.0, brake=0.0, buttons=BTN_AUTO_RESUME, send=True)
    run("D_resume_pulse", int(0.4 / DT))
    cmd.update(buttons=0)

    # Phase E: post-resume capture window
    run("E_post_resume", int(post_resume_s / DT))

    sock.close()
    lib.GT_Close()
    try:
        os.remove(xosc)
    except OSError:
        pass
    return frames


def run_ffb_arm(frozen_at: float, speed_mps: float, envelope_enabled: bool,
                 post_resume_s: float = 5.0, total_s: float = 20.0) -> list[dict]:
    """arm2 (best-effort): headless_ffb / SyntheticSink synthetic wheel.

    Structural notes (why this is NOT a clean A/B/C/D/E replay of arm1):
      - HeadlessFfbInput::Poll() always returns pedal_steer.buttons=0 — there
        is no channel to inject BTN_AUTO_RESUME under input_type=headless_ffb.
        Worked around via the existing auto_return_timeout idle-return path
        (OverrideManager.cpp): once torque-proxy latches MANUAL, the FFB
        servo goes inactive (target_active=false, per ControllerVirtualDriver
        Step: SetSteerTarget(..., active=!lat_manual)), which flips
        OverrideManager back onto the raw-axis threshold check. Setting
        steering_threshold above |frozen_at| suppresses that raw-axis
        re-latch (same trick scripts/vd_ffb_headless_smoke.py Phase B/D use
        for isolation), so lat_active stays false post-latch and the idle
        timer accumulates -> auto_return_timeout fires auto_transition.
      - GT_HEADLESS_FFB_FROZEN_AT is read once in SyntheticSink::Configure()
        (called from HeadlessFfbInput::Init(), itself called once from
        ControllerVirtualDriver::Activate()) — there is no mid-run way to
        change the synthetic wheel angle. So there is no clean "AUTO phase
        then driver grabs the wheel" — the synthetic wheel sits at frozen_at
        from t=0. The MANUAL latch and both transitions play out DURING what
        would be arm1's phase A/B, and the wheel stays physically frozen at
        frozen_at straight through resume (it never "releases" — arm1's
        phase C has no analogue here). This is reported as-is, not smoothed.
    """
    os.environ["GT_HEADLESS_FFB_MODE"] = "frozen"
    os.environ["GT_HEADLESS_FFB_FROZEN_AT"] = f"{frozen_at:.4f}"
    os.environ.pop("GT_HEADLESS_FFB_LAG_TAU", None)

    tmpdir = tempfile.mkdtemp(prefix="vd_resume_arm2_")
    cfg = {"input_type": "headless_ffb", "ffb_target_track_enabled": True,
           "steering_threshold": max(1.0, abs(frozen_at) + 0.5),
           "auto_return_timeout": 1.0,
           "ad_steering_envelope_enabled": envelope_enabled}
    xosc = _make_variant(tmpdir, cfg, speed_mps)

    lib = _load_lib()
    argv_list = [b"vd_resume_ffb", b"--osc", xosc.encode(), b"--headless", b"--fixed_timestep", b"0.05"]
    argv = (ctypes.c_char_p * len(argv_list))(*argv_list)
    rc = lib.GT_InitWithArgs(len(argv_list), argv)
    if rc != 0:
        raise RuntimeError(f"GT_InitWithArgs rc={rc}")

    buf = ctypes.create_string_buffer(16384)
    frames: list[dict] = []
    for _ in range(int(total_s / DT)):
        lib.GT_Step(DT)
        n = lib.GT_GetVirtualDriverTelemetry(0, buf, len(buf))
        if n > 0:
            f = json.loads(buf.value.decode())
            frames.append(_slim(f, "run"))

    lib.GT_Close()
    try:
        os.remove(xosc)
    except OSError:
        pass
    return frames


def run_normal_baseline(speed_mps: float, envelope_enabled: bool, duration_s: float = 20.0) -> list[dict]:
    """Normal-operation baseline: AUTO the entire run (input_type=stub — no
    input at all, so the override never latches), scenario's OWN trigger
    timing intact (straight -> curve -> lane change @t=6s -> stop @t=13s,
    see resources/xosc/virtual_driver_basic.xosc), full 20s. This is the
    "what does the AD ever command during ordinary driving, including a
    normal lane change" reference the safety-envelope limits get sized
    against — deliberately NOT the artificially-quiet 2s phase-A slice used
    inside run_network_arm (that slice never sees a curve or a lane change,
    which is precisely the normal-operation traffic a permanent clamp must
    not clip)."""
    tmpdir = tempfile.mkdtemp(prefix="vd_resume_baseline_")
    cfg = {"input_type": "stub", "ffb_target_track_enabled": False,
           "ad_steering_envelope_enabled": envelope_enabled}
    xosc = _make_variant(tmpdir, cfg, speed_mps, push_triggers_out=False)

    lib = _load_lib()
    argv_list = [b"vd_resume_baseline", b"--osc", xosc.encode(), b"--headless",
                 b"--fixed_timestep", b"0.05"]
    argv = (ctypes.c_char_p * len(argv_list))(*argv_list)
    rc = lib.GT_InitWithArgs(len(argv_list), argv)
    if rc != 0:
        raise RuntimeError(f"GT_InitWithArgs rc={rc}")

    buf = ctypes.create_string_buffer(16384)
    frames: list[dict] = []
    for _ in range(int(duration_s / DT)):
        lib.GT_Step(DT)
        n = lib.GT_GetVirtualDriverTelemetry(0, buf, len(buf))
        if n > 0:
            f = json.loads(buf.value.decode())
            frames.append(_slim(f, "normal"))

    lib.GT_Close()
    try:
        os.remove(xosc)
    except OSError:
        pass
    return frames


def _wrapped_diff(a: float, b: float) -> float:
    """a - b wrapped into [-pi, pi] (heading dt-diff, radians — esmini Position::GetH() convention)."""
    d = a - b
    return (d + math.pi) % (2 * math.pi) - math.pi


def _percentile(values: list[float], p: float) -> float:
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


def _stats(values: list[float]) -> dict:
    return {"max": max(values) if values else 0.0,
            "p99": _percentile(values, 99), "p95": _percentile(values, 95),
            "median": _percentile(values, 50), "n": len(values)}


def kinematic_window_metrics(frames: list[dict], steer_key: str = "driver_steer") -> dict:
    """delta/ddelta_dt/a_lat_cmd/omega_cmd, derived from a steer-command field
    (default driver_steer = the RAW pre-envelope AD command; pass
    steer_key="envelope_steer_out" for the POST-envelope command actually
    handed to physics/FFB — layer 1 vs. layer 2 of the before/after report)
    via the bicycle-model constants above. dt is the per-frame sim_time diff
    (nominally DT=0.05s, the --fixed_timestep this whole harness runs at)."""
    delta_vals: list[float] = []
    ddelta_dt_vals: list[float] = []
    a_lat_vals: list[float] = []
    omega_vals: list[float] = []
    prev_delta = None
    prev_t = None
    for f in frames:
        sn = f.get(steer_key)
        v = f.get("ego_speed")
        t = f.get("sim_time")
        if sn is None or v is None or t is None:
            continue
        delta = sn * MAX_STEER_ANGLE
        kappa = math.tan(delta) / WHEEL_BASE
        delta_vals.append(delta)
        a_lat_vals.append(v * v * kappa)
        omega_vals.append(v * kappa)
        if prev_delta is not None and prev_t is not None and (t - prev_t) > 0:
            ddelta_dt_vals.append((delta - prev_delta) / (t - prev_t))
        prev_delta, prev_t = delta, t
    return {
        "steer_key": steer_key,
        "dt_nominal_s": DT,
        "delta_rad": _stats([abs(x) for x in delta_vals]),
        "ddelta_dt_rad_s": _stats([abs(x) for x in ddelta_dt_vals]),
        "a_lat_cmd_mps2": _stats([abs(x) for x in a_lat_vals]),
        "omega_cmd_rad_s": _stats([abs(x) for x in omega_vals]),
    }


def _road_heading_ref(frames: list[dict], idx0: int) -> float:
    """Approximate the road/route tangent direction at the resume point as the
    EGO HEADING during AUTO control before the edge (override_lateral==False
    frames preceding idx0) — plain (non-circular) median. Valid here because
    (a) AUTO tracks lane center closely, so its heading IS the route tangent
    to good approximation, and (b) both scenario variants used by this
    harness keep the route locally straight through this window (arm1/arm2
    push LaneChangeStart out to 500s — see _make_variant), so headings
    cluster tightly and don't wrap across +-pi. Falls back to frame 0's
    heading if no pre-edge AUTO frame exists."""
    pre = [f["ego_h"] for f in frames[:idx0]
           if f.get("override_lateral") is False and f.get("ego_h") is not None]
    if not pre:
        pre = [frames[0]["ego_h"]] if frames and frames[0].get("ego_h") is not None else [0.0]
    s = sorted(pre)
    return s[len(s) // 2]


def compute_metrics(frames: list[dict], window_s: float = 5.0,
                     heading_converge_thresh_rad: float = 0.05) -> dict:
    """Find the first auto_transition edge and quantify the transient after it."""
    edge_idxs = [i for i, f in enumerate(frames) if f.get("auto_transition")]
    if not edge_idxs:
        return {"edge_found": False, "n_edges": 0}
    idx0 = edge_idxs[0]
    t0 = frames[idx0]["sim_time"]
    window = [f for f in frames[idx0:] if (f["sim_time"] - t0) <= window_s]

    # Team-lead addendum: yaw-angle error vs. route direction AT the resume
    # edge (distinct from lateral offset — tests the "heading misalignment,
    # not offset magnitude, drives transient severity" hypothesis).
    road_h_ref = _road_heading_ref(frames, idx0)
    heading_err_at_edge = _wrapped_diff(frames[idx0]["ego_h"], road_h_ref)
    heading_converge_t = None
    for f in window:
        h = f.get("ego_h")
        if h is not None and abs(_wrapped_diff(h, road_h_ref)) < heading_converge_thresh_rad:
            heading_converge_t = f["sim_time"] - t0
            break

    steer_vals = [f["driver_steer"] for f in window if f.get("driver_steer") is not None]
    steer_peak = max((abs(v) for v in steer_vals), default=0.0)
    steer_rate_peak = 0.0
    for i in range(1, len(window)):
        s0, s1 = window[i - 1].get("driver_steer"), window[i].get("driver_steer")
        t_a, t_b = window[i - 1].get("sim_time"), window[i].get("sim_time")
        if None in (s0, s1, t_a, t_b) or (t_b - t_a) <= 0:
            continue
        steer_rate_peak = max(steer_rate_peak, abs(s1 - s0) / (t_b - t_a))

    # Layer 2 (AdSteeringEnvelope, if present in this frame schema): the
    # POST-envelope command actually handed to physics/FFB. Absent (all
    # None) on frame data captured before the envelope's telemetry existed —
    # every stat below degenerates to 0.0/empty in that case, which the
    # caller must not mistake for "envelope did nothing".
    steer_out_vals = [f["envelope_steer_out"] for f in window if f.get("envelope_steer_out") is not None]
    steer_out_peak = max((abs(v) for v in steer_out_vals), default=0.0)
    steer_out_rate_peak = 0.0
    for i in range(1, len(window)):
        s0, s1 = window[i - 1].get("envelope_steer_out"), window[i].get("envelope_steer_out")
        t_a, t_b = window[i - 1].get("sim_time"), window[i].get("sim_time")
        if None in (s0, s1, t_a, t_b) or (t_b - t_a) <= 0:
            continue
        steer_out_rate_peak = max(steer_out_rate_peak, abs(s1 - s0) / (t_b - t_a))
    clip_vals = [abs(f["envelope_steer_in"] - f["envelope_steer_out"]) for f in window
                 if f.get("envelope_steer_in") is not None and f.get("envelope_steer_out") is not None]
    clip_peak = max(clip_vals, default=0.0)
    active_frames_in_window = sum(1 for f in window if f.get("envelope_active"))

    yaw_rate_peak = 0.0
    a_lat_peak = 0.0
    for i in range(1, len(window)):
        h0, h1 = window[i - 1].get("ego_h"), window[i].get("ego_h")
        t_a, t_b = window[i - 1].get("sim_time"), window[i].get("sim_time")
        if None in (h0, h1, t_a, t_b) or (t_b - t_a) <= 0:
            continue
        yr = _wrapped_diff(h1, h0) / (t_b - t_a)
        yaw_rate_peak = max(yaw_rate_peak, abs(yr))
        v = window[i].get("ego_speed") or 0.0
        a_lat_peak = max(a_lat_peak, abs(yr * v))

    offset_at_edge = frames[idx0].get("ego_offset", 0.0) or 0.0
    conv_t = None
    for f in window:
        off = f.get("ego_offset")
        if off is not None and abs(off) < 0.3:
            conv_t = f["sim_time"] - t0
            break
    overshoot = 0.0
    sign0 = 1 if offset_at_edge >= 0 else -1
    for f in window:
        off = f.get("ego_offset")
        if off is not None and (off * sign0) < 0:
            overshoot = max(overshoot, abs(off))

    # Team-lead addendum: for a window that ends WITHOUT converging, is the
    # tail still decreasing (slow but safe) or plateaued/diverging (envelope
    # is choking off the recovery)? ~1s-spaced |offset| samples for a visual
    # trace, plus a least-squares slope of |offset| over the final
    # min(3.0, window_s/2) seconds. Negative slope = still shrinking; ~0 =
    # plateau; positive = diverging.
    offset_trajectory: list[dict] = []
    next_sample_t = 0.0
    for f in window:
        off = f.get("ego_offset")
        tt = f["sim_time"] - t0
        if off is not None and tt >= next_sample_t - 1e-6:
            offset_trajectory.append({"t": round(tt, 2), "offset": off})
            next_sample_t += 1.0
    if window and window[-1].get("ego_offset") is not None:
        last_t = round(window[-1]["sim_time"] - t0, 2)
        if not offset_trajectory or offset_trajectory[-1]["t"] != last_t:
            offset_trajectory.append({"t": last_t, "offset": window[-1]["ego_offset"]})

    tail_slope = None
    offset_final_abs = None
    if window:
        tail_span = min(3.0, window_s / 2.0)
        end_t = window[-1]["sim_time"] - t0
        tail_pts = [(f["sim_time"] - t0, abs(f["ego_offset"])) for f in window
                    if f.get("ego_offset") is not None and (f["sim_time"] - t0) >= end_t - tail_span]
        if len(tail_pts) >= 2:
            n = len(tail_pts)
            sx = sum(p[0] for p in tail_pts); sy = sum(p[1] for p in tail_pts)
            sxx = sum(p[0] * p[0] for p in tail_pts); sxy = sum(p[0] * p[1] for p in tail_pts)
            denom = n * sxx - sx * sx
            if abs(denom) > 1e-9:
                tail_slope = (n * sxy - sx * sy) / denom
        if window[-1].get("ego_offset") is not None:
            offset_final_abs = abs(window[-1]["ego_offset"])

    ffb_force_vals = [f["ffb_commanded_force"] for f in window if f.get("ffb_commanded_force") is not None]
    ffb_dev_vals = [f["ffb_position_error"] for f in window if f.get("ffb_position_error") is not None]

    return {
        "edge_found": True,
        "n_edges": len(edge_idxs),
        "edge_sim_times": [frames[i]["sim_time"] for i in edge_idxs],
        "offset_at_edge_m": offset_at_edge,
        "steer_peak_abs": steer_peak,
        "steer_rate_peak_per_s": steer_rate_peak,
        "yaw_rate_peak_rad_s": yaw_rate_peak,
        "a_lat_est_peak_mps2": a_lat_peak,
        "offset_converge_s": conv_t,
        "offset_overshoot_m": overshoot,
        "offset_trajectory": offset_trajectory,
        "offset_tail_slope_mps": tail_slope,
        "offset_final_abs_m": offset_final_abs,
        "road_heading_ref_rad": road_h_ref,
        "heading_error_at_edge_rad": heading_err_at_edge,
        "heading_error_at_edge_deg": math.degrees(heading_err_at_edge),
        "heading_error_converge_s": heading_converge_t,
        "ffb_force_peak": max((abs(v) for v in ffb_force_vals), default=0.0),
        "ffb_dev_peak": max((abs(v) for v in ffb_dev_vals), default=0.0),
        # Same steer-command-derived quantities as the normal-op baseline
        # (kinematic_window_metrics), computed over the SAME 5s post-edge
        # window as everything else above — apples-to-apples with the
        # baseline's full-run stats for the envelope-sizing comparison.
        # "kinematic_window" = layer 1 (raw AD command, driver_steer) kept
        # under its original name for backward compat with earlier reports;
        # kinematic_window_env_out = layer 2 (post-envelope command).
        "kinematic_window": kinematic_window_metrics(window, "driver_steer"),
        "kinematic_window_env_out": kinematic_window_metrics(window, "envelope_steer_out"),
        "steer_out_peak_abs": steer_out_peak,
        "steer_out_rate_peak_per_s": steer_out_rate_peak,
        "envelope_clip_peak": clip_peak,
        "envelope_active_frames_in_window": active_frames_in_window,
        "envelope_active_frames_total": sum(1 for f in frames if f.get("envelope_active")),
    }


def _save(frames: list[dict], tag: str) -> None:
    os.makedirs(OUT_DIR, exist_ok=True)
    json_path = os.path.join(OUT_DIR, f"{tag}.json")
    csv_path = os.path.join(OUT_DIR, f"{tag}.csv")
    with open(json_path, "w", encoding="utf-8") as fh:
        json.dump(frames, fh, indent=1)
    if frames:
        with open(csv_path, "w", newline="", encoding="utf-8") as fh:
            w = csv.DictWriter(fh, fieldnames=list(frames[0].keys()))
            w.writeheader()
            w.writerows(frames)


# Pre-AdSteeringEnvelope saved data (backed up out-of-band before the first
# envelope-enabled run) — used to verify enabled=false is bit-identical to
# the harness's own pre-feature output (single-variable A/B design, team-lead
# spec). Old tags have no _envoff/_envon suffix.
BACKUP_DIR = os.path.join(ROOT, "test_results", "vd_resume_transient_pre_envelope_backup")


def _diff_frames_against_backup(new_frames: list[dict], backup_tag: str) -> dict:
    backup_path = os.path.join(BACKUP_DIR, f"{backup_tag}.json")
    if not os.path.exists(backup_path):
        return {"backup_found": False}
    old_frames = json.loads(open(backup_path, encoding="utf-8").read())
    if len(old_frames) != len(new_frames):
        return {"backup_found": True, "identical": False,
                "reason": f"frame count differs: old={len(old_frames)} new={len(new_frames)}"}
    diffs = []
    for i, (o, n) in enumerate(zip(old_frames, new_frames)):
        for k, v_old in o.items():  # only compare fields that existed in the OLD schema
            v_new = n.get(k)
            if v_old != v_new:
                diffs.append((i, k, v_old, v_new))
    return {"backup_found": True, "identical": len(diffs) == 0, "n_diffs": len(diffs),
            "sample_diffs": diffs[:8]}


def _print_case(m: dict, is_arm2: bool = False) -> None:
    print(f"  frames_edge={m.get('edge_found')} n_edges={m.get('n_edges', 0)} "
          f"offset_at_edge={m.get('offset_at_edge_m')}")
    if not m.get("edge_found"):
        return
    print(f"  [layer1 raw AD]     steer_peak={m['steer_peak_abs']:.3f} "
          f"steer_rate_peak={m['steer_rate_peak_per_s']:.3f}/s")
    print(f"  [layer2 post-env]   steer_out_peak={m['steer_out_peak_abs']:.3f} "
          f"steer_out_rate_peak={m['steer_out_rate_peak_per_s']:.3f}/s "
          f"clip_peak={m['envelope_clip_peak']:.3f} "
          f"active_frames_in_window={m['envelope_active_frames_in_window']} "
          f"active_frames_total={m['envelope_active_frames_total']}")
    print(f"  [layer3 realized]   yaw_rate_peak={m['yaw_rate_peak_rad_s']:.4f}rad/s "
          f"a_lat_est_peak={m['a_lat_est_peak_mps2']:.3f}m/s2 "
          f"conv_t={m['offset_converge_s']} overshoot={m['offset_overshoot_m']:.3f}m")
    traj = m.get("offset_trajectory") or []
    if traj:
        traj_str = " ".join(f"t={p['t']:.0f}s:{p['offset']:.3f}m" for p in traj)
        print(f"  offset_trajectory: {traj_str}")
    ts = m.get("offset_tail_slope_mps")
    print(f"  offset_final_abs={m.get('offset_final_abs_m')}  tail_slope[m/s]={ts if ts is None else f'{ts:.4f}'} "
          f"({'SHRINKING' if (ts is not None and ts < -0.005) else 'PLATEAU/DIVERGING' if ts is not None else 'n/a'})")
    print(f"  heading_error_at_edge={m['heading_error_at_edge_rad']:.4f}rad "
          f"({m['heading_error_at_edge_deg']:.2f}deg) heading_converge_t={m['heading_error_converge_s']}")
    if is_arm2:
        print(f"  ffb_force_peak={m['ffb_force_peak']:.3f} ffb_dev_peak={m['ffb_dev_peak']:.3f}")


def _run_suite(envelope_enabled: bool, args) -> tuple[list, dict, list, dict, int]:
    """Run arm1 sweep + arm2 + normal baseline with
    ad_steering_envelope_enabled fixed at envelope_enabled.
    Returns (results, frames_by_case, baseline_frames, baseline_kin, baseline_active_frames)."""
    tag_suffix = "envon" if envelope_enabled else "envoff"
    print(f"\n########## SUITE ad_steering_envelope_enabled={envelope_enabled} ({tag_suffix}) ##########")
    results = []
    frames_by_case: dict[str, list[dict]] = {}

    if "arm1" in args.arms:
        for target in args.targets:
            case = f"arm1@{target:g}"
            print(f"== {case} speed={args.speed}m/s envelope_enabled={envelope_enabled} "
                  f"window_s={args.window_s} ==")
            frames = run_network_arm(target, args.speed, envelope_enabled, post_resume_s=args.window_s)
            _save(frames, f"arm1_ffb_off_target{target:g}m_{tag_suffix}_w{args.window_s:g}s")
            frames_by_case[case] = frames
            m = compute_metrics(frames, window_s=args.window_s)
            results.append(("arm1", target, m))
            _print_case(m)

    if "arm2" in args.arms:
        case = f"arm2@{args.frozen_at:g}"
        print(f"== {case} (headless_ffb, BEST-EFFORT) speed={args.speed}m/s envelope_enabled={envelope_enabled} "
              f"window_s={args.window_s} ==")
        frames = run_ffb_arm(args.frozen_at, args.speed, envelope_enabled, post_resume_s=args.window_s)
        _save(frames, f"arm2_ffb_on_frozen{args.frozen_at:g}_{tag_suffix}_w{args.window_s:g}s")
        frames_by_case[case] = frames
        m = compute_metrics(frames, window_s=args.window_s)
        results.append(("arm2", args.frozen_at, m))
        _print_case(m, is_arm2=True)

    print(f"== normal-operation baseline envelope_enabled={envelope_enabled} ==")
    baseline_frames = run_normal_baseline(args.speed, envelope_enabled)
    _save(baseline_frames, f"normal_baseline_{tag_suffix}")
    baseline_kin = kinematic_window_metrics(baseline_frames, "driver_steer")
    baseline_active = sum(1 for f in baseline_frames if f.get("envelope_active"))
    print(f"  frames={len(baseline_frames)} envelope_active_frames={baseline_active} "
          f"(expect 0 when the envelope isn't clipping normal driving)")
    for key, label in (("ddelta_dt_rad_s", "|dδ/dt|[rad/s]"), ("a_lat_cmd_mps2", "a_lat_cmd[m/s2]"),
                        ("omega_cmd_rad_s", "omega_cmd[rad/s]")):
        s = baseline_kin[key]
        print(f"  {label}: max={s['max']:.4f} p99={s['p99']:.4f} p95={s['p95']:.4f} "
              f"median={s['median']:.4f} (n={s['n']})")

    return results, frames_by_case, baseline_frames, baseline_kin, baseline_active


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--arms", nargs="+", choices=["arm1", "arm2"], default=["arm1", "arm2"])
    ap.add_argument("--targets", nargs="+", type=float, default=[0.5, 1.0, 2.0, 3.0],
                     help="arm1 lateral-offset sweep targets [m]")
    ap.add_argument("--speed", type=float, default=8.0, help="AccelAction target speed [m/s]")
    ap.add_argument("--frozen-at", type=float, default=0.4, help="arm2 synthetic wheel angle [-1,1]")
    ap.add_argument("--window-s", type=float, default=5.0,
                     help="post-resume capture + analysis window [s] (both run_network_arm's "
                          "post_resume_s and compute_metrics' window_s — team-lead asked for 12s "
                          "to settle whether arm1@2/3 non-convergence is real or just a short window)")
    args = ap.parse_args()

    if not os.path.exists(DLL):
        print(f"FAIL: DLL not found at {DLL} — run /build first (not doing it automatically)")
        return 1
    if not os.path.exists(BASE_XOSC):
        print(f"FAIL: base xosc not found at {BASE_XOSC}")
        return 1

    build_cfg_before = None
    if os.path.exists(BUILD_CFG):
        build_cfg_before = json.loads(open(BUILD_CFG, encoding="utf-8").read())
        print(f"build config (untouched by this harness — ConfigFile property override used instead): "
              f"input_type={build_cfg_before.get('input_type')} "
              f"ffb_target_track_enabled={build_cfg_before.get('ffb_target_track_enabled')}")

    # --- Single-variable A/B: same new DLL, same script, only the config
    # flag toggles (team-lead spec — isolates the envelope as the ONLY
    # difference between "before" and "after"). ---
    results_off, frames_off, base_frames_off, base_kin_off, base_active_off = _run_suite(False, args)
    results_on, frames_on, base_frames_on, base_kin_on, base_active_on = _run_suite(True, args)

    # --- No-op verification: enabled=false vs. pre-envelope saved data. ---
    print("\n########## enabled=false vs. pre-envelope saved data (strict field diff) ##########")
    backup_checks = {}
    for case, backup_tag in (("arm1@0.5", "arm1_ffb_off_target0.5m"), ("arm1@1", "arm1_ffb_off_target1m"),
                              ("arm1@2", "arm1_ffb_off_target2m"), ("arm1@3", "arm1_ffb_off_target3m"),
                              (f"arm2@{args.frozen_at:g}", f"arm2_ffb_on_frozen{args.frozen_at:g}")):
        if case not in frames_off:
            continue
        d = _diff_frames_against_backup(frames_off[case], backup_tag)
        backup_checks[case] = d
        if not d.get("backup_found"):
            print(f"  {case}: NO BACKUP FOUND at {backup_tag}.json — skipped")
        elif d["identical"]:
            print(f"  {case}: IDENTICAL to pre-envelope saved data ({len(frames_off[case])} frames)")
        else:
            print(f"  {case}: **NOT IDENTICAL** — {d.get('n_diffs', d.get('reason'))}")
            for i, k, vo, vn in d.get("sample_diffs", []):
                print(f"      frame {i}: {k}: old={vo}  new(envelope=false)={vn}")
    d = _diff_frames_against_backup(base_frames_off, "normal_baseline")
    backup_checks["normal_baseline"] = d
    if d.get("backup_found"):
        if d["identical"]:
            print(f"  normal_baseline: IDENTICAL to pre-envelope saved data ({len(base_frames_off)} frames)")
        else:
            print(f"  normal_baseline: **NOT IDENTICAL** — {d.get('n_diffs', d.get('reason'))}")
            for i, k, vo, vn in d.get("sample_diffs", []):
                print(f"      frame {i}: {k}: old={vo}  new(envelope=false)={vn}")

    # --- Before/after 3-layer report on the canonical rows. ---
    print("\n########## before(envelope=false) / after(envelope=true) — canonical rows ##########")
    by_case_off = {f"{a}@{t:g}": m for a, t, m in results_off}
    by_case_on = {f"{a}@{t:g}": m for a, t, m in results_on}
    canonical = ["arm1@0.5", "arm1@1", "arm1@2", "arm1@3", f"arm2@{args.frozen_at:g}"]
    for case in canonical:
        mo, mn = by_case_off.get(case), by_case_on.get(case)
        if not mo or not mn or not mo.get("edge_found") or not mn.get("edge_found"):
            print(f"  {case}: missing edge in before or after — skipped")
            continue
        note = "" if case in ("arm1@0.5", "arm1@1") else "  [STRESS CASE — off-route, not a clean 'Xm' point]"
        print(f"-- {case} --{note}")
        print(f"   layer1 raw AD steer_peak      : before={mo['steer_peak_abs']:.3f}  after={mn['steer_peak_abs']:.3f}")
        print(f"   layer2 post-env steer_out_peak: before={mo['steer_out_peak_abs']:.3f}  after={mn['steer_out_peak_abs']:.3f}  "
              f"clip_peak before={mo['envelope_clip_peak']:.3f} after={mn['envelope_clip_peak']:.3f}")
        print(f"   layer3 yaw_rate_peak[rad/s]   : before={mo['yaw_rate_peak_rad_s']:.4f}  after={mn['yaw_rate_peak_rad_s']:.4f}")
        print(f"   layer3 a_lat_est_peak[m/s2]   : before={mo['a_lat_est_peak_mps2']:.3f}  after={mn['a_lat_est_peak_mps2']:.3f}")
        print(f"   convergence conv_t[s]         : before={mo['offset_converge_s']}  after={mn['offset_converge_s']}")
        print(f"   overshoot[m]                  : before={mo['offset_overshoot_m']:.3f}  after={mn['offset_overshoot_m']:.3f}")
        print(f"   envelope active frames (win)  : before={mo['envelope_active_frames_in_window']}  after={mn['envelope_active_frames_in_window']}")
        if case.startswith("arm2"):
            print(f"   ffb_force_peak                : before={mo['ffb_force_peak']:.3f}  after={mn['ffb_force_peak']:.3f}")
            print(f"   ffb_dev_peak                   : before={mo['ffb_dev_peak']:.3f}  after={mn['ffb_dev_peak']:.3f}")

    print(f"\nnormal_baseline envelope_active_frames: before={base_active_off} after={base_active_on} "
          f"(expect both 0 — 'never clips normal driving')")

    summary_path = os.path.join(OUT_DIR, "summary_envelope_ab.json")
    os.makedirs(OUT_DIR, exist_ok=True)
    with open(summary_path, "w", encoding="utf-8") as fh:
        json.dump({
            "envelope_off": {"pathological": [{"arm": a, "target": t, "metrics": m} for a, t, m in results_off],
                              "normal_baseline_kin": base_kin_off, "normal_baseline_active": base_active_off},
            "envelope_on": {"pathological": [{"arm": a, "target": t, "metrics": m} for a, t, m in results_on],
                             "normal_baseline_kin": base_kin_on, "normal_baseline_active": base_active_on},
            "backup_checks": backup_checks,
        }, fh, indent=1)
    print(f"\nsummary written: {summary_path}")

    if os.path.exists(BUILD_CFG):
        build_cfg_after = json.loads(open(BUILD_CFG, encoding="utf-8").read())
        unchanged = build_cfg_after == build_cfg_before
        print(f"build config unchanged: {unchanged} "
              f"(input_type={build_cfg_after.get('input_type')} "
              f"ffb_target_track_enabled={build_cfg_after.get('ffb_target_track_enabled')})")
        if not unchanged:
            print("  WARNING: build/GT_esmini/config/virtual_driver.json changed during this run "
                  "— this harness never writes it, so this would indicate another process touched it.")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
