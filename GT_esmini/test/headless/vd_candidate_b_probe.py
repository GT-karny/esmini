"""feature:F7 — candidate-B direct probe (block_reason == wheel_not_engaged).

test_results/f7_relatch_handoff.md section 2.2 candidate B, restated:

  If a driver's pushback offset from the AD target is held CONSTANT (so
  position_error = target_norm - actual_norm stays constant) while the AD's
  own target_norm keeps GROWING in the corrective direction (as it does
  during the post-RESUME recovery transient, envelope-rate-limited), then
  once |target_norm| exceeds the constant offset magnitude, actual_norm
  becomes SAME-SIGNED as target_norm with SMALLER magnitude. Both
  independent opposition signatures then read false:
    - signature A (position): sign_opposition needs opposite signs (false,
      same-signed); magnitude_opposition needs |actual|>|target| (false,
      |actual|<|target|).
    - signature B (velocity, added 93b2c6c4/301a72ea specifically to defeat
      moving-target blackout): fires only when sign(push) == sign(target_rate)
      at the moment the push is constant and unclamped, actual_rate ==
      target_rate exactly, and commanded_force_signed's sign is fixed by
      -sign(push). Algebra: B fires iff sign(push) == sign(target_rate). If
      push is applied SAME-SIGNED as the pre-transient actual position (i.e.
      OPPOSITE-signed to the corrective target_rate the AD ramps toward —
      see run_pushback_recovery_timing's own push_mag reuse for phase 1 and
      phase 2), sign(push) != sign(target_rate) ALWAYS, so B never fires by
      construction in that geometry either.

  => block_reason should read wheel_not_engaged for the whole window once
     |target_norm| > |push2_mag|, for as long as the AD keeps ramping away
     from zero in the same direction.

This harness isolates that window: same build_offset/release/wait_resume
sequence as run_pushback_recovery_timing (large induced offset -> idle-
timeout auto-resume -> push again the instant the recovery transient
starts), but with push2's magnitude DECOUPLED from phase-1's build_offset
push_mag (the original harness reused a single push_mag for both, which
requires |target_norm| to blow past 0.5 before entering the window) and a
push2_hold_s long enough (default 2.0s, vs. the original 0.6/0.3/0.2s sweep)
to sit inside the window for many consecutive frames, not just graze it.

Usage (DriverScript venv):
  DriverScript\\.venv\\Scripts\\python.exe GT_esmini\\test\\headless\\vd_candidate_b_probe.py
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


def run_push2_signature_probe(envelope_enabled: bool, push2_mag: float, push2_hold_s: float = 2.0,
                               speed_mps: float = 8.0, target_offset_m: float = 1.3,
                               push_mag: float = 0.5, push2_sign: float = 1.0,
                               ramp_cap_s: float = 3.0, resume_wait_cap_s: float = 2.5,
                               post_push2_s: float = 0.5) -> tuple[list[dict], dict]:
    """push2_sign=+1.0 reuses the SAME sign as push_mag (phase-1 build_offset
    push) for push2 -- this is deliberately the geometry the handoff's
    algebra says opens the wheel_not_engaged window (see module docstring).
    push2_sign=-1.0 is available for an A/B check of the opposite geometry
    (expected: latches fast via signature A, since actual and target diverge
    in magnitude immediately -- included so the boundary claim is not taken
    on faith)."""
    os.environ["GT_HEADLESS_FFB_MODE"] = "pushback"
    os.environ.pop("GT_HEADLESS_FFB_FROZEN_AT", None)
    os.environ.pop("GT_HEADLESS_FFB_LAG_TAU", None)
    os.environ["GT_HEADLESS_FFB_PUSHBACK_PORT"] = str(PUSHBACK_PORT)

    tmpdir = tempfile.mkdtemp(prefix="vd_cand_b_")
    cfg = {"input_type": "headless_ffb", "ffb_target_track_enabled": True,
           "steering_threshold": 0.2, "auto_return_timeout": 1.0,
           "ad_steering_envelope_enabled": envelope_enabled}
    xosc = _make_variant(tmpdir, cfg, speed_mps)

    lib = _load_lib()
    argv_list = [b"cand_b_probe", b"--osc", xosc.encode(), b"--headless", b"--fixed_timestep", b"0.05"]
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

    pushback_val["v"] = push_mag
    reached = False
    for _ in range(int(ramp_cap_s / DT)):
        f = step("build_offset")
        if f and abs(f["ego"]["offset"]) >= target_offset_m:
            reached = True
            break
    offset_at_release = frames[-1]["ego"]["offset"] if frames else None
    if not reached:
        print(f"    WARNING: target_offset_m={target_offset_m} not reached within {ramp_cap_s}s cap "
              f"(last offset={offset_at_release})")

    pushback_val["v"] = 0.0
    for _ in range(int(0.3 / DT)):
        step("release")
    held_after_release = frames[-1]["override"].get("lateral", False) if frames else False

    auto_fired = False
    for _ in range(int(resume_wait_cap_s / DT)):
        f = step("wait_resume")
        if f and f["override"].get("auto_transition"):
            auto_fired = True
            break
    if not auto_fired:
        print(f"    WARNING: auto_transition never fired within {resume_wait_cap_s}s wait cap")

    push2_val = push2_sign * push2_mag
    pushback_val["v"] = push2_val
    for _ in range(int(push2_hold_s / DT)):
        step("push2_during_transient")
    seg2 = [f for f in frames if f["_phase"] == "push2_during_transient"]
    manual_edge_2 = any(f["override"].get("manual_transition") for f in seg2)
    latched_2 = seg2[-1]["override"].get("lateral", False) if seg2 else False
    ever_latched_2 = any(f["override"].get("lateral", False) for f in seg2)

    # Reconstruct target_norm = actual_norm + position_error (both exposed in
    # telemetry; target_norm itself is not). Track when block_reason first
    # reads wheel_not_engaged, and the |target_norm| at that instant, to
    # locate the |target_norm| > |push2_mag| boundary empirically.
    reason_counts: dict[str, int] = {}
    first_wne_t = None
    first_wne_target = None
    trace = []
    for f in seg2:
        gates = f.get("ffb", {}).get("gates", {})
        reason = gates.get("block_reason", "?")
        reason_counts[reason] = reason_counts.get(reason, 0) + 1
        actual_norm = gates.get("actual_norm")
        pos_err = f.get("ffb", {}).get("position_error")
        target_norm = (actual_norm + pos_err) if (actual_norm is not None and pos_err is not None) else None
        if reason == "wheel_not_engaged" and first_wne_t is None:
            first_wne_t = f["sim_time"]
            first_wne_target = target_norm
        trace.append({"t": f["sim_time"], "target_norm": target_norm, "actual_norm": actual_norm,
                       "position_error": pos_err, "sign_opp": gates.get("sign_opposition"),
                       "mag_opp": gates.get("magnitude_opposition"),
                       # post-revert schema (2026-07-26 09:22 rebuild): wheel_engaged_pos/
                       # wheel_engaged_vel/driver_opposing were consolidated into a single
                       # gates.wheel_engaged key (signature B removed -- this is now the
                       # position-only signature, equivalent to old wheel_engaged_pos /
                       # (sign_opp or mag_opp)). Key renamed here to match; old runs
                       # (301a72ea, pre-revert) that read wheel_engaged_vel would silently
                       # get None post-revert if not updated -- this fixes that trap.
                       "wheel_engaged": gates.get("wheel_engaged"),
                       "moving_target": gates.get("moving_target"),
                       "block_reason": reason, "lateral_latched": f["override"].get("lateral")})

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
        "envelope_enabled": envelope_enabled, "push2_mag": push2_mag, "push2_sign": push2_sign,
        "push2_hold_s": push2_hold_s, "offset_reached": reached, "offset_at_release": offset_at_release,
        "held_after_release": held_after_release, "auto_fired": auto_fired,
        "manual_edge_2_seen": manual_edge_2, "latched_2_final_frame": latched_2,
        "ever_latched_2_during_push2": ever_latched_2,
        "n_frames_push2": len(seg2), "block_reason_counts": reason_counts,
        "first_wheel_not_engaged_t_rel": (round(first_wne_t - seg2[0]["sim_time"], 3)
                                           if (first_wne_t is not None and seg2) else None),
        "first_wheel_not_engaged_target_norm": (round(first_wne_target, 4)
                                                 if first_wne_target is not None else None),
        "trace": trace,
    }
    return frames, result


def main() -> int:
    out_dir = os.path.join(os.path.dirname(BASE_XOSC), "..", "..", "test_results", "vd_multi_cycle_override")
    out_dir = os.path.abspath(out_dir)
    os.makedirs(out_dir, exist_ok=True)

    print("== candidate-B probe: push2_mag sweep (same-sign geometry) ==")
    for envelope_enabled in (False, True):
        for push2_mag in (0.1, 0.15, 0.2):
            frames, res = run_push2_signature_probe(envelope_enabled, push2_mag)
            summary = {k: v for k, v in res.items() if k != "trace"}
            print(f"  envelope={envelope_enabled} push2_mag={push2_mag}: {summary}")
            tag = f"candB_push2mag{push2_mag:g}_env{envelope_enabled}_301a72ea"
            with open(os.path.join(out_dir, f"{tag}.json"), "w", encoding="utf-8") as fh:
                json.dump({"result": res, "frames": frames}, fh, indent=1)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
