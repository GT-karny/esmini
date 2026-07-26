"""feature:F7 — signature-B sign-convention false-latch probe (team-lead
finding, 2026-07-26): FfbTargetServo.cpp:27 `u_fb = -(kp*err + kd*derr)`
(err = target-actual) is DELIBERATELY negated -- G29 hardware convention,
positive CONSTANT level pushes the wheel LEFT (axis negative), so a
POSITIVE err (need actual to increase) requires a NEGATIVE force command to
push the wheel the right way. This negation is verified present in the
source (read directly, not taken on faith).

For ANY model where actual genuinely converges toward target (unheld wheel,
zero driver force) -- including HeadlessFfbInput's "lagging" mode, which is
purely kinematic (`lag_axis += alpha*(target-lag_axis)`, independent of
u_fb entirely) -- actual_rate is positively correlated with err, while
commanded_force_signed (= u_fb) is NEGATIVELY correlated with err by
construction. So sign(actual_rate) = -sign(commanded_force_signed) for an
UNHELD wheel converging normally -- this is not hardware-specific, it is a
direct algebraic consequence of the sign flip at FfbTargetServo.cpp:27
combined with any "actual chases target" dynamics.

OverrideManager.cpp:269-271 signature B:
    wheel_engaged_velocity = !suppress && |actual_rate| > gate &&
        (commanded_force_signed * actual_rate) < 0.0   // "opposition"
Given the relationship above, an UNHELD, PASSIVELY-CONVERGING wheel
satisfies (commanded_force_signed * actual_rate) < 0 BY DEFAULT -- signature
B's polarity is backwards: it should read > 0 for opposition (same-signed:
driver actively holding against the servo, preventing normal convergence),
not < 0.

This script builds a scenario variant with a LARGE initial lane offset
(TeleportAction LanePosition offset != 0), forcing AUTO/lagging mode to
correct hard from t=0 -- a resume-transient-equivalent condition (sustained
high target_rate under envelope=True, up to the envelope's steer_rate_max
ceiling) WITHOUT needing an offset-injection channel that "lagging" mode
does not have (see vd_ffb_lag_characterization.py's module docstring for
why a true manual-then-resume offset isn't producible in "lagging" mode
without a C++ change). Driver force is EXACTLY zero throughout (no pushback
UDP channel used at all here -- "lagging" mode has none).
"""
from __future__ import annotations

import ctypes
import json
import os
import sys
import tempfile
import xml.etree.ElementTree as ET

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vd_resume_transient import BASE_XOSC, DT, SHIPPED_CFG, _load_lib  # noqa: E402


def _make_offset_variant(tmpdir: str, cfg_overrides: dict, speed_mps: float, initial_offset_m: float) -> str:
    """Like vd_resume_transient._make_variant, but ALSO sets the ego's
    initial TeleportAction LanePosition offset (forces AUTO to correct a
    real lateral deviation from t=0, entirely within continuous AUTO --
    no MANUAL/RESUME cycle involved). push_triggers_out=True (push the
    scenario's own LaneChangeStart/StopStart out) so this offset-correction
    transient is observed in isolation, uncontaminated by the scenario's
    own separate lane-change event at t=6s."""
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

    for cond_name, new_value in (("LaneChangeStart", "500.0"), ("StopStart", "500.0"),
                                  ("QuitCondition", "600")):
        for cond in root.findall(f".//Condition[@name='{cond_name}']"):
            stc = cond.find(".//SimulationTimeCondition")
            if stc is not None:
                stc.set("value", new_value)

    for target in root.findall(".//AccelAction//AbsoluteTargetSpeed"):
        target.set("value", f"{speed_mps:.2f}")

    lane_pos = root.find(".//TeleportAction//LanePosition")
    if lane_pos is None:
        raise RuntimeError("Could not find initial TeleportAction LanePosition in base xosc")
    lane_pos.set("offset", f"{initial_offset_m:.4f}")

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


def run_lagging_offset_correction(envelope_enabled: bool, lag_tau: float, initial_offset_m: float,
                                   speed_mps: float = 8.0, duration_s: float = 8.0) -> list[dict]:
    os.environ["GT_HEADLESS_FFB_MODE"] = "lagging"
    os.environ.pop("GT_HEADLESS_FFB_FROZEN_AT", None)
    os.environ["GT_HEADLESS_FFB_LAG_TAU"] = f"{lag_tau:.4f}"

    tmpdir = tempfile.mkdtemp(prefix="vd_lagfalselatch_")
    # steering_threshold set HIGH (1.0) to suppress the SEPARATE raw-axis
    # direct-threshold MANUAL path (OverrideManager's non-FFB check) --
    # isolates whether a latch comes from the FFB torque-proxy signature
    # logic specifically (what we're testing), not from the raw axis simply
    # exceeding a low threshold (a different, unrelated latch path).
    cfg = {"input_type": "headless_ffb", "ffb_target_track_enabled": True,
           "steering_threshold": 1.0, "auto_return_timeout": 1.0,
           "ad_steering_envelope_enabled": envelope_enabled}
    xosc = _make_offset_variant(tmpdir, cfg, speed_mps, initial_offset_m)

    lib = _load_lib()
    argv_list = [b"lagfalselatch", b"--osc", xosc.encode(), b"--headless", b"--fixed_timestep", b"0.05"]
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
            frames.append(json.loads(buf.value.decode()))

    lib.GT_Close()
    try:
        os.remove(xosc)
    except OSError:
        pass
    os.environ.pop("GT_HEADLESS_FFB_LAG_TAU", None)
    return frames


def summarize(frames: list[dict]) -> dict:
    """post-revert schema (2026-07-26 09:22 rebuild): gates.wheel_engaged_pos/
    wheel_engaged_vel/driver_opposing/actual_rate were REMOVED (signature B
    reverted); gates.wheel_engaged is the single consolidated key that
    replaces wheel_engaged_pos (position-based signature only now -- there
    is no velocity signature to report post-revert). max_actual_rate is
    dropped for the same reason (the field no longer exists)."""
    manual_edges = [f for f in frames if f.get("override", {}).get("manual_transition")]
    weng_true = [f for f in frames if f.get("ffb", {}).get("gates", {}).get("wheel_engaged")]
    max_target_rate = max((abs(f["ffb"]["gates"]["target_rate"]) for f in frames), default=0.0)
    max_pos_err = max((abs(f["ffb"]["position_error"]) for f in frames), default=0.0)
    max_force = max((f["ffb"]["commanded_force"] for f in frames), default=0.0)
    return {
        "n_frames": len(frames), "n_manual_edges": len(manual_edges),
        "manual_edge_times": [f["sim_time"] for f in manual_edges],
        "n_wheel_engaged_true": len(weng_true),
        "first_wheel_engaged_true_t": weng_true[0]["sim_time"] if weng_true else None,
        "max_target_rate": max_target_rate,
        "max_position_error": max_pos_err, "max_commanded_force": max_force,
    }


def main() -> int:
    out_dir = os.path.join(os.path.dirname(BASE_XOSC), "..", "..", "test_results", "vd_lagging_false_latch")
    out_dir = os.path.abspath(out_dir)
    os.makedirs(out_dir, exist_ok=True)

    print("=== resume-transient-equivalent (large initial lane offset=2.5m), tau sweep ===")
    for envelope_enabled in (True, False):
        for lag_tau in (0.1, 0.3, 0.5):
            frames = run_lagging_offset_correction(envelope_enabled, lag_tau, initial_offset_m=2.5)
            s = summarize(frames)
            print(f"  envelope={envelope_enabled} tau={lag_tau}: {s}")
            tag = f"offset2.5_env{envelope_enabled}_tau{lag_tau:g}"
            with open(os.path.join(out_dir, f"{tag}.json"), "w", encoding="utf-8") as fh:
                json.dump(frames, fh, indent=1)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
