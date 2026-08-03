"""feature:F7 — Task 7 (team-lead spec, 2026-07-26): reproduce the USER
SYMPTOM geometry — a driver clearly pushing back (|actual| > wheel_over_
target_epsilon=0.05) DURING THE RISING EDGE of the resume transient
(target_rate sustained in [1.0, 2.459]/s), not the tail.

**STATUS: WRITTEN BUT NOT YET EXECUTED**, per team-lead's explicit
instruction (2026-07-26): another agent is reverting 301a72ea's three
behavior changes (signature B / hold / velocity_gate) in source; team-lead
will rebuild and notify when the DLL is updated. Do not run this against
the current (301a72ea, sign-bug-confirmed) DLL — the result would be
uninterpretable (a real latch could be the genuine fix, or the sign-bug
misfiring on normal tracking; Task 6's reanalysis showed signature B was
NOT involved in the existing timing_hold data's 0.15s recovery — wev=False
at every latch across all 6 files — but that data's push2 always landed on
the transient's STATIC TAIL (target_rate ~0 by push2 start), which is
exactly why this script exists: to test the RISING EDGE instead).

Why vd_candidate_b_probe.py's push2 doesn't work for this: it starts push2
at the instant auto_transition fires via IDLE-TIMEOUT (auto_return_timeout
after the wheel goes quiet in "release_and_resume") -- by construction the
AD's target has already had time to settle toward its resting value before
that timeout elapses, so target_rate is near 0 by the time push2 starts
(confirmed in this session's frame-by-frame reanalysis: |target_rate|<0.03
throughout push2_during_transient across all timing_hold*.json files).

Solution used here: vd_lagging_false_latch_probe.py's TeleportAction
large-initial-lane-offset technique (forces AUTO to correct hard from t=0,
NO manual/resume cycle needed at all -- confirmed in this session's Task 5
data that target_rate hits the envelope's 2.459/s ceiling within 2 frames
and sustains it for ~0.4s: t=0.10-0.45s in the offset=2.5m/tau=0.3 case)
IS COMPATIBLE with "pushback" mode: pushback mode's axis formula
(clamp(target_norm_+pushback_norm_, -1, 1)) has no dependency on how the
AD's target got to be wherever it is (unlike "lagging", which is a fixed
model with no offset-injection channel) -- pushback simply reads target_norm_
each frame and adds the live-injected offset, so it works identically
whether target_norm_ arrived via a manual-then-resume cycle OR via AUTO
correcting a scripted initial teleport offset. This combination needs NO
new C++.

Default push window (t=0.10-0.55s, sign=+1.0 i.e. OPPOSING the target's
correction direction) is set from this session's already-recorded rising-
edge timeline (offset2.5_clean_envTrue_tau0.3.json): target_rate sustains
near the 2.459/s ceiling from t=0.10 to ~t=0.45-0.50s before a brief lull.
"""

from __future__ import annotations

import ctypes
import json
import os
import socket
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vd_resume_transient import BASE_XOSC, DT, _load_lib  # noqa: E402
from vd_lagging_false_latch_probe import _make_offset_variant  # noqa: E402
from vd_multi_cycle_override import PUSHBACK_PORT  # noqa: E402

WIRE_FMT = __import__("struct").Struct("<I4diI")
MAGIC = 0x50535443  # 'PSTC'


def run_rising_edge_push(
    envelope_enabled: bool,
    push_mag: float,
    push_start_s: float = 0.10,
    push_hold_s: float = 0.45,
    push_sign: float = 1.0,
    initial_offset_m: float = 2.5,
    speed_mps: float = 8.0,
    duration_s: float = 4.0,
) -> tuple[list[dict], dict]:
    """push_sign=+1.0 OPPOSES the AD's own correction direction (matches the
    established vd_candidate_b_probe.py convention: same sign as the
    pre-transient actual position, opposite to the direction target_rate
    ramps toward -- see that module's docstring for the sign algebra).
    push_mag in [0.2, 0.5] per team-lead spec (clearly exceeds
    wheel_over_target_epsilon=0.05).

    steering_threshold kept at 1.0 (raw-axis path suppressed) so any latch
    observed is attributable ONLY to the FFB torque-proxy signature logic
    -- same isolation pattern as vd_lagging_false_latch_probe.py.
    """
    os.environ["GT_HEADLESS_FFB_MODE"] = "pushback"
    os.environ.pop("GT_HEADLESS_FFB_FROZEN_AT", None)
    os.environ.pop("GT_HEADLESS_FFB_LAG_TAU", None)
    os.environ["GT_HEADLESS_FFB_PUSHBACK_PORT"] = str(PUSHBACK_PORT)

    tmpdir = tempfile.mkdtemp(prefix="vd_risingedge_")
    cfg = {
        "input_type": "headless_ffb",
        "ffb_target_track_enabled": True,
        "steering_threshold": 1.0,
        "auto_return_timeout": 1.0,
        "ad_steering_envelope_enabled": envelope_enabled,
    }
    xosc = _make_offset_variant(tmpdir, cfg, speed_mps, initial_offset_m)

    lib = _load_lib()
    argv_list = [
        b"risingedge",
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
        pkt = WIRE_FMT.pack(MAGIC, pushback_val["v"], 0.0, 0.0, 0.0, 0, 0)
        try:
            sock.sendto(pkt, ("127.0.0.1", PUSHBACK_PORT))
        except OSError:
            pass

    buf = ctypes.create_string_buffer(16384)

    def tel():
        n = lib.GT_GetVirtualDriverTelemetry(0, buf, len(buf))
        return json.loads(buf.value.decode()) if n > 0 else None

    frames: list[dict] = []
    n_steps = int(duration_s / DT)
    for i in range(n_steps):
        t = i * DT
        pushback_val["v"] = (
            push_sign * push_mag
            if (push_start_s <= t < push_start_s + push_hold_s)
            else 0.0
        )
        send_pushback()
        lib.GT_Step(DT)
        f = tel()
        if f:
            frames.append(f)

    sock.close()
    lib.GT_Close()
    try:
        os.remove(xosc)
    except OSError:
        pass

    push_seg = [
        f for f in frames if push_start_s <= f["sim_time"] < push_start_s + push_hold_s
    ]
    manual_edge = any(f["override"].get("manual_transition") for f in push_seg)
    latched_final = (
        push_seg[-1]["override"].get("lateral", False) if push_seg else False
    )
    reason_counts: dict[str, int] = {}
    max_target_rate_during_push = 0.0
    max_actual_norm_during_push = 0.0
    for f in push_seg:
        g = f["ffb"]["gates"]
        reason_counts[g["block_reason"]] = reason_counts.get(g["block_reason"], 0) + 1
        max_target_rate_during_push = max(
            max_target_rate_during_push, abs(g["target_rate"])
        )
        max_actual_norm_during_push = max(
            max_actual_norm_during_push, abs(g["actual_norm"])
        )

    result = {
        "envelope_enabled": envelope_enabled,
        "push_mag": push_mag,
        "push_sign": push_sign,
        "push_start_s": push_start_s,
        "push_hold_s": push_hold_s,
        "manual_edge_seen": manual_edge,
        "latched_final": latched_final,
        "block_reason_counts": reason_counts,
        "max_target_rate_during_push": max_target_rate_during_push,
        "max_actual_norm_during_push": max_actual_norm_during_push,
        "n_frames_push": len(push_seg),
    }
    return frames, result


def main() -> int:
    """NOT RUN YET (2026-07-26) -- see module docstring. When team-lead
    confirms the reverted+rebuilt DLL is staged, run this with:
      DriverScript\\.venv\\Scripts\\python.exe GT_esmini\\test\\headless\\vd_resume_rising_edge_probe.py
    """
    out_dir = os.path.join(
        os.path.dirname(BASE_XOSC), "..", "..", "test_results", "vd_resume_rising_edge"
    )
    out_dir = os.path.abspath(out_dir)
    os.makedirs(out_dir, exist_ok=True)

    print("=== rising-edge push-back probe: push_mag sweep x envelope True/False ===")
    for envelope_enabled in (True, False):
        for push_mag in (0.2, 0.35, 0.5):
            frames, res = run_rising_edge_push(envelope_enabled, push_mag)
            print(f"  envelope={envelope_enabled} push_mag={push_mag}: {res}")
            tag = f"risingedge_push{push_mag:g}_env{envelope_enabled}"
            with open(
                os.path.join(out_dir, f"{tag}.json"), "w", encoding="utf-8"
            ) as fh:
                json.dump({"result": res, "frames": frames}, fh, indent=1)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
