"""feature:F7 — multi-cycle override repro.

Real-machine report (2026-07-25): "一度オーバーライドして復帰したあと、再度オー
バーライドしようと思ったら出来なかった" (override -> RESUME -> a SECOND
override attempt does not latch). scripts/vd_override_smoke.py only exercises
ONE intervene->latch->release->resume cycle — it never asserted a cycle 2.
This harness runs >=2 full cycles and checks whether MANUAL re-latches each
time, in both the steer and pedal domains, with ad_steering_envelope_enabled
toggled false/true (single-variable A/B — false must equal pre-envelope
behavior, so this isolates whether a repro is a pre-existing bug or a
regression from the envelope change).

Reuses vd_resume_transient.py's deterministic synchronous-UDP-send pattern
and ConfigFile/tmpdir variant builder (import, no duplication).
"""

from __future__ import annotations

import ctypes
import json
import os
import socket
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vd_resume_transient import (  # noqa: E402
    BASE_XOSC,
    BTN_AUTO_RESUME,
    DLL,
    DT,
    INPUT_PORT,
    MAGIC,
    WIRE,
    _load_lib,
    _make_variant,
)


def run_multi_cycle_network(
    domain: str, envelope_enabled: bool, speed_mps: float = 8.0, n_cycles: int = 3
) -> tuple[list[dict], list[dict]]:
    """domain: 'steer' (lateral, steering=0.5) or 'pedal' (longitudinal, brake=0.5)."""
    tmpdir = tempfile.mkdtemp(prefix="vd_multicycle_")
    cfg = {
        "input_type": "network",
        "input_port": INPUT_PORT,
        "input_transport": "udp",
        "ffb_target_track_enabled": False,
        "ad_steering_envelope_enabled": envelope_enabled,
    }
    xosc = _make_variant(tmpdir, cfg, speed_mps)

    lib = _load_lib()
    argv_list = [
        b"multicycle",
        b"--osc",
        xosc.encode(),
        b"--headless",
        b"--fixed_timestep",
        b"0.05",
    ]
    argv = (ctypes.c_char_p * len(argv_list))(*argv_list)
    rc = lib.GT_InitWithArgs(len(argv_list), argv)
    if rc != 0:
        raise RuntimeError(f"GT_InitWithArgs rc={rc}")

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    cmd = {"steering": 0.0, "throttle": 0.0, "brake": 0.0, "buttons": 0, "send": False}

    def send_now():
        if not cmd["send"]:
            return
        pkt = WIRE.pack(
            MAGIC,
            cmd["steering"],
            cmd["throttle"],
            cmd["brake"],
            0.0,
            0,
            cmd["buttons"] & 0xFFFFFFFF,
        )
        try:
            sock.sendto(pkt, ("127.0.0.1", INPUT_PORT))
        except OSError:
            pass

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
                f["_phase"] = phase
                frames.append(f)

    domain_key = "lateral" if domain == "steer" else "longitudinal"
    cycle_results = []

    cmd.update(steering=0.0, throttle=0.0, brake=0.0, buttons=0, send=False)
    run("settle0", int(1.5 / DT))

    for c in range(1, n_cycles + 1):
        if domain == "steer":
            cmd.update(steering=0.5, throttle=0.0, brake=0.0, buttons=0, send=True)
        else:
            cmd.update(steering=0.0, throttle=0.0, brake=0.5, buttons=0, send=True)
        run(f"c{c}_intervene", int(1.0 / DT))
        seg = [f for f in frames if f["_phase"] == f"c{c}_intervene"]
        manual_edge = any(f["override"].get("manual_transition") for f in seg)
        latched = seg[-1]["override"].get(domain_key, False) if seg else False

        cmd.update(steering=0.0, throttle=0.0, brake=0.0, buttons=0, send=True)
        run(f"c{c}_release", int(0.5 / DT))
        seg = [f for f in frames if f["_phase"] == f"c{c}_release"]
        held = seg[-1]["override"].get(domain_key, False) if seg else False

        cmd.update(buttons=BTN_AUTO_RESUME, send=True)
        run(f"c{c}_resume_pulse", int(0.4 / DT))
        cmd.update(buttons=0)
        run(f"c{c}_resume_settle", int(1.0 / DT))
        seg = [
            f
            for f in frames
            if f["_phase"] in (f"c{c}_resume_pulse", f"c{c}_resume_settle")
        ]
        auto_edge = any(f["override"].get("auto_transition") for f in seg)
        resumed = not seg[-1]["override"].get(domain_key, True) if seg else False

        cycle_results.append(
            {
                "cycle": c,
                "manual_edge_seen": manual_edge,
                "latched": latched,
                "held_after_release": held,
                "auto_edge_seen": auto_edge,
                "resumed": resumed,
            }
        )

    sock.close()
    lib.GT_Close()
    try:
        os.remove(xosc)
    except OSError:
        pass
    return frames, cycle_results


def run_multi_cycle_ffb(
    envelope_enabled: bool,
    speed_mps: float = 8.0,
    frozen_at: float = 0.4,
    total_s: float = 20.0,
) -> tuple[list[dict], list[tuple[str, float]]]:
    """headless_ffb / torque-proxy path. frozen_at is fixed for the whole process
    (HeadlessFfbInput reads it once at Configure) — there is no scripted
    'release wheel' here. Instead this relies on the NATURAL repeat latch /
    auto_return_timeout cycling already observed in this config (a frozen
    off-center wheel keeps re-triggering the torque proxy after every
    idle-timeout resume) to see whether MANUAL keeps re-latching or stops."""
    os.environ["GT_HEADLESS_FFB_MODE"] = "frozen"
    os.environ["GT_HEADLESS_FFB_FROZEN_AT"] = f"{frozen_at:.4f}"
    os.environ.pop("GT_HEADLESS_FFB_LAG_TAU", None)

    tmpdir = tempfile.mkdtemp(prefix="vd_multicycle_ffb_")
    cfg = {
        "input_type": "headless_ffb",
        "ffb_target_track_enabled": True,
        "steering_threshold": max(1.0, abs(frozen_at) + 0.5),
        "auto_return_timeout": 1.0,
        "ad_steering_envelope_enabled": envelope_enabled,
    }
    xosc = _make_variant(tmpdir, cfg, speed_mps)

    lib = _load_lib()
    argv_list = [
        b"multicycle_ffb",
        b"--osc",
        xosc.encode(),
        b"--headless",
        b"--fixed_timestep",
        b"0.05",
    ]
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
            frames.append(json.loads(buf.value.decode()))

    lib.GT_Close()
    try:
        os.remove(xosc)
    except OSError:
        pass

    edges: list[tuple[str, float]] = []
    for f in frames:
        ov = f.get("override", {})
        if ov.get("manual_transition"):
            edges.append(("manual", f["sim_time"]))
        if ov.get("auto_transition"):
            edges.append(("auto", f["sim_time"]))
    return frames, edges


def run_frozen_grip_natural(
    envelope_enabled: bool,
    speed_mps: float = 8.0,
    frozen_at: float = 0.0,
    total_s: float = 20.0,
) -> tuple[list[dict], list[dict]]:
    """Team-lead refinement (2026-07-26) of the "known gap" check: the NEW
    OverrideManager fix ORs a force-vs-actual-VELOCITY sign-conflict test
    alongside the existing POSITION-based conflict test. That new path is
    STRUCTURALLY UNABLE to fire when d(actual)/dt==0 exactly (frozen mode,
    always) — this exercises exactly that: "driver grips the wheel and holds
    it perfectly still" while the AD's target moves away.

    frozen_at=0.0 approximates "gripped while driving straight" (steer_out is
    ~0 during the early straight segment — verified by inspection, not a
    deliberately-turned angle like the frozen_at=0.4 used elsewhere in this
    file). push_triggers_out=False keeps the SCENARIO'S OWN lane-change event
    (t=6s, resources/xosc/virtual_driver_basic.xosc) intact — that is what
    makes the AD's target "ramp away" realistically, rather than an
    artificial induced offset. Returns (frames, edges-with-gates) so the
    gates.* values at each transition are visible without a re-run.
    """
    os.environ["GT_HEADLESS_FFB_MODE"] = "frozen"
    os.environ["GT_HEADLESS_FFB_FROZEN_AT"] = f"{frozen_at:.4f}"
    os.environ.pop("GT_HEADLESS_FFB_LAG_TAU", None)

    tmpdir = tempfile.mkdtemp(prefix="vd_grip_natural_")
    cfg = {
        "input_type": "headless_ffb",
        "ffb_target_track_enabled": True,
        "steering_threshold": 1.0,
        "auto_return_timeout": 1.0,
        "ad_steering_envelope_enabled": envelope_enabled,
    }
    xosc = _make_variant(tmpdir, cfg, speed_mps, push_triggers_out=False)

    lib = _load_lib()
    argv_list = [
        b"grip_natural",
        b"--osc",
        xosc.encode(),
        b"--headless",
        b"--fixed_timestep",
        b"0.05",
    ]
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
            frames.append(json.loads(buf.value.decode()))

    lib.GT_Close()
    try:
        os.remove(xosc)
    except OSError:
        pass

    edges: list[dict] = []
    for f in frames:
        ov = f.get("override", {})
        if ov.get("manual_transition") or ov.get("auto_transition"):
            edges.append(
                {
                    "kind": "manual" if ov.get("manual_transition") else "auto",
                    "t": f["sim_time"],
                    "position_error": f.get("ffb", {}).get("position_error"),
                    "gates": f.get("ffb", {}).get("gates"),
                }
            )
    return frames, edges


PUSHBACK_PORT = (
    9105  # must match HeadlessFfbInput.cpp GT_HEADLESS_FFB_PUSHBACK_PORT default
)


def run_multi_cycle_pushback(
    envelope_enabled: bool,
    speed_mps: float = 8.0,
    n_cycles: int = 3,
    pushback_mag: float = 0.5,
    push_hold_s: float = 0.6,
    release_wait_s: float = 1.4,
) -> tuple[list[dict], list[dict]]:
    """SCRIPTED driver-vs-servo torque-contest repro (team-lead addendum,
    2026-07-25): requires HeadlessFfbInput.cpp's new "pushback" mode
    (GT_HEADLESS_FFB_MODE=pushback) — the OLD "frozen"/"follower" modes
    cannot represent a scripted push/release sequence within one process (see
    run_multi_cycle_ffb docstring: frozen's offset is fixed for the whole
    process, so it only proves the degenerate always-latched case).

    "pushback" mode makes HeadlessFfbInput open its own tiny UDP listener
    (PUSHBACK_PORT) reusing the same PSTC wire format; sending a packet with
    steering=pushback_mag makes the synthetic wheel diverge from the AD
    target by exactly that much (dev grows -> torque-proxy should latch);
    sending steering=0.0 returns it to following the target. RESUME still has
    no button channel here, so — same trick as run_multi_cycle_ffb — a high
    steering_threshold suppresses the raw-axis path once the servo goes
    inactive, and a short auto_return_timeout drives the resume via idle-timeout.

    release_wait_s > auto_return_timeout(1.0s) so resume fires within each
    cycle's release phase, closing the loop before the next cycle's push.
    """
    os.environ["GT_HEADLESS_FFB_MODE"] = "pushback"
    os.environ.pop("GT_HEADLESS_FFB_FROZEN_AT", None)
    os.environ.pop("GT_HEADLESS_FFB_LAG_TAU", None)
    os.environ["GT_HEADLESS_FFB_PUSHBACK_PORT"] = str(PUSHBACK_PORT)

    tmpdir = tempfile.mkdtemp(prefix="vd_multicycle_pushback_")
    cfg = {
        "input_type": "headless_ffb",
        "ffb_target_track_enabled": True,
        "steering_threshold": 1.0,
        "auto_return_timeout": 1.0,
        "ad_steering_envelope_enabled": envelope_enabled,
    }
    xosc = _make_variant(tmpdir, cfg, speed_mps)

    lib = _load_lib()
    argv_list = [
        b"multicycle_pushback",
        b"--osc",
        xosc.encode(),
        b"--headless",
        b"--fixed_timestep",
        b"0.05",
    ]
    argv = (ctypes.c_char_p * len(argv_list))(*argv_list)
    rc = lib.GT_InitWithArgs(len(argv_list), argv)
    if rc != 0:
        raise RuntimeError(f"GT_InitWithArgs rc={rc}")

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    pushback_val = {"v": 0.0}

    def send_pushback():
        pkt = WIRE.pack(MAGIC, pushback_val["v"], 0.0, 0.0, 0.0, 0, 0)
        try:
            sock.sendto(pkt, ("127.0.0.1", PUSHBACK_PORT))
        except OSError:
            pass

    buf = ctypes.create_string_buffer(16384)

    def tel():
        n = lib.GT_GetVirtualDriverTelemetry(0, buf, len(buf))
        return json.loads(buf.value.decode()) if n > 0 else None

    frames: list[dict] = []

    def run(phase: str, n_steps: int):
        for _ in range(n_steps):
            send_pushback()
            lib.GT_Step(DT)
            f = tel()
            if f:
                f["_phase"] = phase
                frames.append(f)

    cycle_results = []
    pushback_val["v"] = 0.0
    run("settle0", int(1.0 / DT))

    for c in range(1, n_cycles + 1):
        pushback_val["v"] = pushback_mag
        run(f"c{c}_push", int(push_hold_s / DT))
        seg = [f for f in frames if f["_phase"] == f"c{c}_push"]
        manual_edge = any(f["override"].get("manual_transition") for f in seg)
        latched = seg[-1]["override"].get("lateral", False) if seg else False
        dev_peak = max(
            (abs(f.get("ffb", {}).get("position_error", 0.0)) for f in seg), default=0.0
        )

        pushback_val["v"] = 0.0
        run(f"c{c}_release_and_resume", int(release_wait_s / DT))
        seg = [f for f in frames if f["_phase"] == f"c{c}_release_and_resume"]
        auto_edge = any(f["override"].get("auto_transition") for f in seg)
        resumed = not seg[-1]["override"].get("lateral", True) if seg else False

        cycle_results.append(
            {
                "cycle": c,
                "manual_edge_seen": manual_edge,
                "latched": latched,
                "dev_peak": round(dev_peak, 4),
                "auto_edge_seen": auto_edge,
                "resumed": resumed,
            }
        )

    sock.close()
    lib.GT_Close()
    try:
        os.remove(xosc)
    except OSError:
        pass
    return frames, cycle_results


def run_pushback_recovery_timing(
    envelope_enabled: bool,
    push2_hold_s: float,
    speed_mps: float = 8.0,
    target_offset_m: float = 1.3,
    push_mag: float = 0.5,
    ramp_cap_s: float = 3.0,
    resume_wait_cap_s: float = 2.5,
    post_push2_s: float = 1.0,
) -> tuple[list[dict], dict]:
    """Team-lead decisive variant (2026-07-25): the previous run_multi_cycle_pushback
    held the 2nd push for a full 0.6s, which was long enough for the AD's
    resume-correction target_rate to eventually settle below the override
    gate — the latch fired late (0.5s) but NOT never. Two axes this asks:
    (1) push2_hold_s SHORTER (0.6/0.3/0.2s — how long a real driver plausibly
        keeps pushing before concluding "it's not working" and letting go),
    (2) inject push2 WHILE THE RECOVERY TRANSIENT IS STILL ACTIVE, i.e. right
        after auto_transition from a LARGE induced offset (~1.3m, matching
        vd_resume_transient.py arm1@0.5, NOT the tiny near-zero-offset state
        the earlier pushback cycles happened to resume from) — the real
        machine's recovery lasts 1.35-2.0s+ with the target moving at its
        rate ceiling the whole time, which the earlier test did not build up.

    Sequence: settle -> push (build_mag) held until |ego.offset|>=target_offset_m
    (ramp_cap_s safety cap) -> release (latch must hold, sticky) -> wait for
    auto_return_timeout's idle-resume, watched frame-by-frame -> THE INSTANT
    auto_transition fires, immediately push again for push2_hold_s -> report
    whether THAT second push ever latches.
    """
    os.environ["GT_HEADLESS_FFB_MODE"] = "pushback"
    os.environ.pop("GT_HEADLESS_FFB_FROZEN_AT", None)
    os.environ.pop("GT_HEADLESS_FFB_LAG_TAU", None)
    os.environ["GT_HEADLESS_FFB_PUSHBACK_PORT"] = str(PUSHBACK_PORT)

    # steering_threshold=0.2 (NOT 1.0 like the frozen-mode tests): here the
    # raw axis DOES need to read "active" (> threshold) while pushback=0.5 is
    # held during build_offset — otherwise, once the torque-proxy latches and
    # the servo goes inactive, the raw-axis path (threshold=1.0 > 0.5) reads
    # "no activity" WHILE STILL PUSHING, so auto_return_timeout's idle timer
    # fires mid-ramp and resumes long before the offset target is reached
    # (found empirically: offset_reached=False, held_after_release=False,
    # auto_fired=False on the first attempt at this test — the idle-resume
    # had already happened earlier and been missed). 0.2 sits strictly
    # between the released state (~0, small AD target) and push_mag (0.5).
    tmpdir = tempfile.mkdtemp(prefix="vd_pushback_timing_")
    cfg = {
        "input_type": "headless_ffb",
        "ffb_target_track_enabled": True,
        "steering_threshold": 0.2,
        "auto_return_timeout": 1.0,
        "ad_steering_envelope_enabled": envelope_enabled,
    }
    xosc = _make_variant(tmpdir, cfg, speed_mps)

    lib = _load_lib()
    argv_list = [
        b"pushback_timing",
        b"--osc",
        xosc.encode(),
        b"--headless",
        b"--fixed_timestep",
        b"0.05",
    ]
    argv = (ctypes.c_char_p * len(argv_list))(*argv_list)
    rc = lib.GT_InitWithArgs(len(argv_list), argv)
    if rc != 0:
        raise RuntimeError(f"GT_InitWithArgs rc={rc}")

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    pushback_val = {"v": 0.0}

    def send_pushback():
        pkt = WIRE.pack(MAGIC, pushback_val["v"], 0.0, 0.0, 0.0, 0, 0)
        try:
            sock.sendto(pkt, ("127.0.0.1", PUSHBACK_PORT))
        except OSError:
            pass

    buf = ctypes.create_string_buffer(16384)

    def tel():
        n = lib.GT_GetVirtualDriverTelemetry(0, buf, len(buf))
        return json.loads(buf.value.decode()) if n > 0 else None

    frames: list[dict] = []

    def step(phase: str) -> dict | None:
        send_pushback()
        lib.GT_Step(DT)
        f = tel()
        if f:
            f["_phase"] = phase
            frames.append(f)
        return f

    pushback_val["v"] = 0.0
    for _ in range(int(0.5 / DT)):
        step("settle0")

    # Build a real ~target_offset_m lateral offset (mirrors
    # vd_resume_transient.py's phase B ramp-until-reached loop).
    pushback_val["v"] = push_mag
    reached = False
    for _ in range(int(ramp_cap_s / DT)):
        f = step("build_offset")
        if f and abs(f["ego"]["offset"]) >= target_offset_m:
            reached = True
            break
    offset_at_release = frames[-1]["ego"]["offset"] if frames else None
    if not reached:
        print(
            f"    WARNING: target_offset_m={target_offset_m} not reached within {ramp_cap_s}s cap "
            f"(last offset={offset_at_release})"
        )

    pushback_val["v"] = 0.0
    for _ in range(int(0.3 / DT)):
        step("release")
    held_after_release = (
        frames[-1]["override"].get("lateral", False) if frames else False
    )

    # Watch frame-by-frame for the idle-timeout auto_transition; stop the
    # instant it fires (or at resume_wait_cap_s, whichever first).
    auto_fired = False
    for _ in range(int(resume_wait_cap_s / DT)):
        f = step("wait_resume")
        if f and f["override"].get("auto_transition"):
            auto_fired = True
            break
    if not auto_fired:
        print(
            f"    WARNING: auto_transition never fired within {resume_wait_cap_s}s wait cap"
        )

    # THE decisive window: push again WHILE the recovery transient is (or
    # should still be) in progress, for only push2_hold_s.
    pushback_val["v"] = push_mag
    for _ in range(int(push2_hold_s / DT)):
        step("push2_during_transient")
    seg2 = [f for f in frames if f["_phase"] == "push2_during_transient"]
    manual_edge_2 = any(f["override"].get("manual_transition") for f in seg2)
    latched_2 = seg2[-1]["override"].get("lateral", False) if seg2 else False

    prev_out = None
    prev_t = None
    over_rate_time = 0.0
    dev_vals = []
    for f in seg2:
        env_blk = f.get("envelope", {})
        t = f["sim_time"]
        steer_out = env_blk.get("steer_out")
        if prev_out is not None and prev_t is not None and (t - prev_t) > 0:
            rate = (steer_out - prev_out) / (t - prev_t)
            if abs(rate) > 0.30:
                over_rate_time += t - prev_t
        prev_out, prev_t = steer_out, t
        dev = f.get("ffb", {}).get("position_error")
        if dev is not None:
            dev_vals.append(abs(dev))

    pushback_val["v"] = 0.0
    for _ in range(int(post_push2_s / DT)):
        step("post_push2_settle")

    sock.close()
    lib.GT_Close()
    try:
        os.remove(xosc)
    except OSError:
        pass

    result = {
        "envelope_enabled": envelope_enabled,
        "push2_hold_s": push2_hold_s,
        "offset_reached": reached,
        "offset_at_release": offset_at_release,
        "held_after_release": held_after_release,
        "auto_fired": auto_fired,
        "manual_edge_2_seen": manual_edge_2,
        "latched_2": latched_2,
        "over_rate_time_s_in_push2": round(over_rate_time, 3),
        "dev_peak_in_push2": round(max(dev_vals), 4) if dev_vals else None,
        "n_frames_push2": len(seg2),
    }
    return frames, result


def run_timing_sweep() -> int:
    """Team-lead decisive variant: push2_hold_s in {0.6, 0.3, 0.2} x
    envelope {False, True}, 2nd push injected the instant auto_transition
    fires from a real ~1.3m induced offset (long recovery transient)."""
    out_dir = os.path.join(
        os.path.dirname(BASE_XOSC),
        "..",
        "..",
        "test_results",
        "vd_multi_cycle_override",
    )
    out_dir = os.path.abspath(out_dir)
    os.makedirs(out_dir, exist_ok=True)

    print("== pushback-during-recovery-transient timing sweep (target_offset=1.3m) ==")
    for push2_hold_s in (0.6, 0.3, 0.2):
        for envelope_enabled in (False, True):
            frames, res = run_pushback_recovery_timing(envelope_enabled, push2_hold_s)
            print(
                f"  push2_hold_s={push2_hold_s} envelope_enabled={envelope_enabled}: {res}"
            )
            tag = f"timing_hold{push2_hold_s:g}_env{envelope_enabled}"
            with open(
                os.path.join(out_dir, f"{tag}.json"), "w", encoding="utf-8"
            ) as fh:
                json.dump(frames, fh, indent=1)
    return 0


def run_grip_suite() -> int:
    """Team-lead 'known gap' check: static-grip (zero actual-velocity, frozen
    mode) across the scenario's own natural lane-change (t=6s), envelope
    false/true. Permanent regression per team-lead instruction."""
    out_dir = os.path.join(
        os.path.dirname(BASE_XOSC),
        "..",
        "..",
        "test_results",
        "vd_multi_cycle_override",
    )
    out_dir = os.path.abspath(out_dir)
    os.makedirs(out_dir, exist_ok=True)

    print("== static grip (frozen_at=0.0, natural lane-change @t=6s) ==")
    for envelope_enabled in (False, True):
        frames, edges = run_frozen_grip_natural(envelope_enabled)
        print(f"  envelope_enabled={envelope_enabled}: n_edges={len(edges)}")
        for e in edges:
            print(f"    {e}")
        tag = f"grip_natural_env{envelope_enabled}"
        with open(os.path.join(out_dir, f"{tag}.json"), "w", encoding="utf-8") as fh:
            json.dump(frames, fh, indent=1)
    return 0


def main() -> int:
    if not os.path.exists(DLL):
        print(f"FAIL: DLL not found at {DLL}")
        return 1
    if not os.path.exists(BASE_XOSC):
        print(f"FAIL: base xosc not found at {BASE_XOSC}")
        return 1

    if "--suite" in sys.argv and "timing" in sys.argv:
        return run_timing_sweep()
    if "--suite" in sys.argv and "grip" in sys.argv:
        return run_grip_suite()

    # feature:F7 2026-07-28 -- this harness used to `return 0` unconditionally.
    # It reproduces the user's original complaint ("override -> RESUME -> a
    # SECOND override does not latch") and it has been FAILING: with
    # envelope_enabled=True the scripted-pushback run shows cycle 1 not
    # resuming and cycle 2 not latching. Nothing noticed, because a harness
    # that always exits 0 cannot fail a gate and nobody re-reads its stdout.
    #
    # Collect every cycle verdict and exit non-zero if any of them is bad, so
    # this can be wired into the gate. Ignoring the result is now a choice
    # somebody has to make explicitly (`|| true`), not the default.
    failures: list[str] = []

    def _check(label: str, cycles: list[dict]) -> None:
        for c in cycles:
            if not c.get("latched"):
                failures.append(f"{label} cycle {c.get('cycle')}: did not latch")
            if not c.get("resumed"):
                failures.append(f"{label} cycle {c.get('cycle')}: did not resume")

    print("== network input, domain=steer ==")
    for envelope_enabled in (False, True):
        _, cyc = run_multi_cycle_network("steer", envelope_enabled)
        print(f"  envelope_enabled={envelope_enabled}: {cyc}")
        _check(f"network/steer env={envelope_enabled}", cyc)

    print("== network input, domain=pedal ==")
    for envelope_enabled in (False, True):
        _, cyc = run_multi_cycle_network("pedal", envelope_enabled)
        print(f"  envelope_enabled={envelope_enabled}: {cyc}")
        _check(f"network/pedal env={envelope_enabled}", cyc)

    print("== headless_ffb (torque-proxy), frozen_at=0.4 ==")
    for envelope_enabled in (False, True):
        _, edges = run_multi_cycle_ffb(envelope_enabled)
        manual_count = sum(1 for k, _ in edges if k == "manual")
        auto_count = sum(1 for k, _ in edges if k == "auto")
        print(
            f"  envelope_enabled={envelope_enabled}: manual_edges={manual_count} auto_edges={auto_count} "
            f"edges={edges}"
        )

    print(
        "== headless_ffb SCRIPTED pushback (requires HeadlessFfbInput 'pushback' mode) =="
    )
    out_dir = os.path.join(
        os.path.dirname(BASE_XOSC),
        "..",
        "..",
        "test_results",
        "vd_multi_cycle_override",
    )
    out_dir = os.path.abspath(out_dir)
    os.makedirs(out_dir, exist_ok=True)
    for envelope_enabled in (False, True):
        frames, cyc = run_multi_cycle_pushback(envelope_enabled)
        print(f"  envelope_enabled={envelope_enabled}: {cyc}")
        _check(f"pushback env={envelope_enabled}", cyc)
        tag = f"pushback_env{envelope_enabled}"
        with open(os.path.join(out_dir, f"{tag}.json"), "w", encoding="utf-8") as fh:
            json.dump(frames, fh, indent=1)
        # If any cycle after the first failed to latch, dump target/actual
        # (steer_in / position_error) around that cycle's push phase so the
        # failure mode (target and actual doing what?) is visible without a
        # re-run.
        for c in cyc:
            if c["cycle"] > 1 and not c["latched"]:
                seg = [f for f in frames if f["_phase"] == f"c{c['cycle']}_push"]
                print(f"    cycle {c['cycle']} FAILED TO LATCH — target/actual trace:")
                for f in seg[:20]:
                    env = f.get("envelope", {})
                    print(
                        f"      t={f['sim_time']:.2f} steer_in={env.get('steer_in')} "
                        f"steer_out={env.get('steer_out')} dev={f.get('ffb', {}).get('position_error')} "
                        f"active={f.get('ffb', {}).get('target_active')}"
                    )

    print()
    print("=" * 60)
    if failures:
        print(f"MULTI-CYCLE OVERRIDE: FAIL ({len(failures)} cycle verdict(s) bad)")
        for f_ in failures:
            print(f"  - {f_}")
        print("=" * 60)
        return 1
    print("MULTI-CYCLE OVERRIDE: PASS (every cycle latched and resumed)")
    print("=" * 60)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
