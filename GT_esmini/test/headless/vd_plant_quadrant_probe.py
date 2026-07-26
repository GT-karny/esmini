"""feature:F7 — Task 8 follow-up: 4-quadrant / false-positive probe for the
new "plant" mode (force-coupled, HeadlessFfbInput.cpp, 2026-07-26).

**STATUS: WRITTEN, NOT EXECUTED.** Source-only work is permitted during the
PM binary/build/config freeze (2026-07-26), but running any headless
simulation is not -- the DLL (09:22:18) predates this file's C++ changes,
so a run right now would silently test OLD compiled logic while this file
assumes the NEW "plant" mode exists. Do not run until team-lead confirms a
rebuilt DLL is staged (see f7_force_coupled_plant_spec.md's note: "現行DLL
のままparity 18本・回帰ゲート・lagging誤ラッチ0/6を回す(=revertの証明)。
その後にリビルドして、新モードを使った検証に入る").

PM design constraint (2026-07-26): override detection must not use
direction ("オーバーライドはADと逆方向とは限らない"). The acceptance matrix
has 4 detection quadrants (a-d) and 5 false-positive conditions, all
constructible through the SAME "plant" mode force-injection channel
(GT_HEADLESS_FFB_PUSHBACK_PORT, "steering" field reinterpreted as a driver
FORCE -- see HeadlessFfbInput.hpp's "plant" mode doc). This is the value
"pushback"'s axis=target+push algebra could not deliver: real force-balance
lets the driver settle INSIDE the target (quadrant a) as an emergent
equilibrium, not a scripted offset.

Sign convention: pos_err = target_norm - actual_norm (telemetry field
ffb.position_error). All control laws below key off pos_err's sign to decide
which direction OPPOSES vs REINFORCES the servo -- this is a Python-side
convenience for *constructing* test conditions; it is NOT how the detector
itself should work post-fix (the whole point of the PM's constraint is that
the DETECTOR must not need direction). target_norm is reconstructed each
frame as actual_norm + position_error (both already in telemetry).
"""
from __future__ import annotations

import ctypes
import json
import os
import socket
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vd_resume_transient import BASE_XOSC, DT, _load_lib, _make_variant  # noqa: E402
from vd_multi_cycle_override import PUSHBACK_PORT  # noqa: E402

WIRE_FMT = __import__("struct").Struct("<I4diI")
MAGIC = 0x50535443  # 'PSTC'


# ---------------------------------------------------------------------------
# Control laws — each takes the LATEST telemetry frame (dict) and returns the
# force (double, same units as servo_force/max_force=0.6 scale) to inject
# THIS step via the UDP channel. All are simple/physically-motivated, not
# empirically tuned (cannot tune without running — see module docstring).
# Magnitudes are starting points; re-tune once a real run is possible.
# ---------------------------------------------------------------------------

def _pos_err(frame: dict) -> float:
    return frame["ffb"]["position_error"]


def law_a_hold_inside(frame: dict, f_resist: float = 0.12) -> float:
    """(a) 同方向・AD より内側で押さえる — most important, structurally
    undetectable by the pre-fix direction-based check. Constant PARTIAL
    resistance (magnitude < what's needed to fully stop the servo, and
    intentionally kept near/below breakaway=0.19 so it mostly acts as drag
    rather than a hard stop) opposing the servo's current push direction.
    Emergent physics (not scripted): actual settles somewhere between 0 and
    target, same side, smaller magnitude -- exactly quadrant (a)."""
    e = _pos_err(frame)
    if e == 0.0:
        return 0.0
    return -f_resist if e > 0 else f_resist


def law_b_overtake(frame: dict, f_boost: float = 0.5) -> float:
    """(b) 同方向・AD を追い越して切る. Push in the SAME direction the servo
    is already going (reinforcing, not opposing), large enough that actual
    overshoots past target in magnitude."""
    e = _pos_err(frame)
    if e == 0.0:
        return 0.0
    return f_boost if e > 0 else -f_boost


def law_c_opposite(frame: dict, f_strong: float = 0.5) -> float:
    """(c) 逆方向へ切る. Same sign convention as law_a (opposing the servo)
    but LARGE enough to overpower it and drive actual past zero to the
    opposite side from target."""
    e = _pos_err(frame)
    if e == 0.0:
        return 0.0
    return -f_strong if e > 0 else f_strong


def make_law_d_grip(hold_position: float = 0.0, kp_grip: float = 8.0,
                     kd_grip: float = 1.0, f_max_grip: float = 0.6):
    """(d) 0付近で握って動かさない. Closed-loop PD hold at `hold_position`
    (default 0.0) — unlike (a)/(b)/(c), a fixed-magnitude force cannot track
    a MOVING servo target, so this needs genuine feedback. Returns a
    stateful callable (closes over previous actual_norm to estimate
    velocity via finite difference, since telemetry only exposes position).
    Same physical construction as the b6dc58f0 stuck-wheel false-positive
    condition (§3.4 of the spec) — intentional, per spec §3.4's note that
    (d) and b6dc58f0 are physically indistinguishable by design."""
    state = {"prev_actual": None, "prev_t": None}

    def law(frame: dict) -> float:
        actual = frame["ffb"]["gates"]["actual_norm"]
        t = frame["sim_time"]
        vel = 0.0
        if state["prev_actual"] is not None and state["prev_t"] is not None:
            dt = t - state["prev_t"]
            if dt > 0:
                vel = (actual - state["prev_actual"]) / dt
        state["prev_actual"], state["prev_t"] = actual, t
        f = -kp_grip * (actual - hold_position) - kd_grip * vel
        return max(-f_max_grip, min(f_max_grip, f))

    return law


def law_zero(frame: dict) -> float:
    """Hands-off (driver_force=0) — used for the 4 false-positive
    conditions that don't involve a driver at all (起動過渡/サーボ遅れ/
    a43e4c67/549e5823)."""
    return 0.0


# ---------------------------------------------------------------------------
# Generic runner: applies a control law each frame via the plant-mode force
# channel (same UDP port/wire format as "pushback" -- see
# HeadlessFfbInput.cpp::Poll(), routed to SetDriverForce when mode="plant").
# ---------------------------------------------------------------------------

def run_plant_condition(control_law, envelope_enabled: bool, speed_mps: float = 8.0,
                         duration_s: float = 10.0, breakaway: float | None = None,
                         kinetic: float | None = None, vmax: float | None = None,
                         noise_amp: float | None = None) -> list[dict]:
    os.environ["GT_HEADLESS_FFB_MODE"] = "plant"
    os.environ.pop("GT_HEADLESS_FFB_FROZEN_AT", None)
    os.environ.pop("GT_HEADLESS_FFB_LAG_TAU", None)
    os.environ["GT_HEADLESS_FFB_PUSHBACK_PORT"] = str(PUSHBACK_PORT)
    if breakaway is not None:
        os.environ["GT_HEADLESS_FFB_PLANT_BREAKAWAY"] = f"{breakaway:.4f}"
    if kinetic is not None:
        os.environ["GT_HEADLESS_FFB_PLANT_KINETIC"] = f"{kinetic:.4f}"
    if vmax is not None:
        os.environ["GT_HEADLESS_FFB_PLANT_VMAX"] = f"{vmax:.4f}"
    os.environ["GT_HEADLESS_FFB_PLANT_NOISE_AMP"] = f"{noise_amp:.5f}" if noise_amp is not None else "0.0"
    os.environ["GT_HEADLESS_FFB_PLANT_SEED"] = "12345"  # deterministic

    tmpdir = tempfile.mkdtemp(prefix="vd_plant_")
    cfg = {"input_type": "headless_ffb", "ffb_target_track_enabled": True,
           "steering_threshold": 1.0, "auto_return_timeout": 1.0,
           "ad_steering_envelope_enabled": envelope_enabled}
    xosc = _make_variant(tmpdir, cfg, speed_mps, push_triggers_out=False)

    lib = _load_lib()
    argv_list = [b"plantquad", b"--osc", xosc.encode(), b"--headless", b"--fixed_timestep", b"0.05"]
    argv = (ctypes.c_char_p * len(argv_list))(*argv_list)
    rc = lib.GT_InitWithArgs(len(argv_list), argv)
    if rc != 0:
        raise RuntimeError(f"GT_InitWithArgs rc={rc}")

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    buf = ctypes.create_string_buffer(16384)
    frames: list[dict] = []
    force = 0.0
    for _ in range(int(duration_s / DT)):
        pkt = WIRE_FMT.pack(MAGIC, force, 0.0, 0.0, 0.0, 0, 0)
        try:
            sock.sendto(pkt, ("127.0.0.1", PUSHBACK_PORT))
        except OSError:
            pass
        lib.GT_Step(DT)
        n = lib.GT_GetVirtualDriverTelemetry(0, buf, len(buf))
        if n > 0:
            f = json.loads(buf.value.decode())
            frames.append(f)
            force = control_law(f)   # next frame's injected force, closed-loop

    sock.close()
    lib.GT_Close()
    try:
        os.remove(xosc)
    except OSError:
        pass
    return frames


# ---------------------------------------------------------------------------
# The 9-condition acceptance matrix (spec §4). All hang off run_plant_condition
# with a control law + optional plant-parameter overrides.
# ---------------------------------------------------------------------------

CONDITIONS = {
    # --- detection quadrants (should eventually be flagged by whatever
    # residual/detector design replaces direction-based checks) ---
    "a_hold_inside":   lambda env: run_plant_condition(law_a_hold_inside, env),
    "b_overtake":      lambda env: run_plant_condition(law_b_overtake, env),
    "c_opposite":      lambda env: run_plant_condition(law_c_opposite, env),
    "d_grip_near_zero": lambda env: run_plant_condition(make_law_d_grip(hold_position=0.0), env),
    # --- false-positive conditions (must NOT be flagged) ---
    "fp_b6dc58f0_stuck": lambda env: run_plant_condition(
        make_law_d_grip(hold_position=0.0, kp_grip=20.0, f_max_grip=0.6), env),  # same construction as (d), by design (spec §3.4)
    "fp_startup_transient": lambda env: run_plant_condition(law_zero, env),      # servo alone from rest, breakaway must be overcome first
    "fp_servo_lag": lambda env: run_plant_condition(law_zero, env),             # same run as startup -- stick-slip IS the lag model here
    "fp_a43e4c67_moving_target": lambda env: run_plant_condition(law_zero, True),   # envelope=True forces a sustained fast-moving target
    "fp_549e5823_static_target_inertia": lambda env: run_plant_condition(law_zero, False),  # envelope=False: target settles fast, wheel-inertia startup dominates
}


def main() -> int:
    print("NOT EXECUTED — see module docstring. Once a rebuilt DLL with "
          "'plant' mode is staged and team-lead confirms, run per-condition:")
    for name in CONDITIONS:
        print(f"  {name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
