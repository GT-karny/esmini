"""feature:F7 — servo tracking-lag characterization (τ identification).

Team-lead request (2026-07-26, F7 relatch follow-up): another agent is
designing a "residual vs. expected-lag-error" replacement for the current
crude rate-gate (OverrideManager.cpp ffb_target_rate_gate_=0.30, which stops
opposition detection ENTIRELY while the AD target is moving). The proposed
fix needs e_expected(target_rate) — the position_error a driver-force-free
servo would show while chasing a moving target — so that only the RESIDUAL
above e_expected can be attributed to a driver pushing back.

IMPORTANT DEVIATION FROM THE LITERAL INSTRUCTION, documented up front:
the request said to use HeadlessFfbInput's "follower" mode ("サーボに完全追従
＝ドライバー力ゼロ"). Reading HeadlessFfbInput.cpp::SyntheticSink::CurrentAxis()
shows "follower" is `return target_norm_;` — an EXACT, ZERO-LAG idealization
used elsewhere for AD-side parity testing (vd_ffb_notouch_parity.py). Under
follower, actual_norm == target_norm every frame BY CONSTRUCTION, so
position_error ≡ 0 for every target_rate — a degenerate, uninformative
dataset (τ would fit to 0 with a vacuous "perfect" R²).

The mode that actually models "zero driver force, but a real physical wheel
lags a moving target" is "lagging": a first-order low-pass
(alpha = 1-exp(-dt/tau); axis += alpha*(target-axis)), tau defaulted to
0.30s and explicitly commented as calibrated from real G29 step-response
data (ffb_spike Day-1 §1e). This script uses "lagging", not "follower", and
cross-checks a second tau (0.15s) via GT_HEADLESS_FFB_LAG_TAU to prove the
identification methodology actually recovers the configured tau rather than
just reporting back a hardcoded constant.

Two data sources, combined for the fit:
  (A) DIRECT simulation: this script runs input_type=headless_ffb,
      GT_HEADLESS_FFB_MODE=lagging, ffb_target_track_enabled=true, over the
      scenario's own natural straight/curve/lane-change (t=6s), envelope
      True/False, tau in {0.30, 0.15}. ffb.gates.target_rate and
      ffb.position_error are read directly from telemetry (real C++ output,
      not replayed).
  (B) OFFLINE REPLAY: HeadlessFfbInput's "lagging" mode has no offset-
      injection channel (mode is fixed at Configure(), like frozen/follower;
      only "pushback" has a live UDP channel, and pushback's axis formula
      has NO lag term — see vd_candidate_b_probe.py's module docstring), so
      a true "large real offset, THEN zero-force lag recovery" run isn't
      producible in one process without a C++ change (out of scope, not
      done). Instead this script replays ALREADY-RECORDED AD target_norm(t)
      trajectories (this session's vd_resume_transient.py arm1@0.5/1/2/3
      runs, envelope True/False — "envelope_steer_out" IS target_norm by
      construction, see OverrideManager.cpp's "target_rate（envelope.steer_out
      のフレーム間差分）" identity already established this session) through
      the SAME first-order-lag formula, implemented faithfully in Python
      from the C++ source. Validated bit-for-bit against source (A) before
      being trusted for source (B) — see the analysis script's validation
      step.

Usage (DriverScript venv):
  DriverScript\\.venv\\Scripts\\python.exe GT_esmini\\test\\headless\\vd_ffb_lag_characterization.py
"""

from __future__ import annotations

import ctypes
import json
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vd_resume_transient import BASE_XOSC, DT, _load_lib, _make_variant  # noqa: E402


def run_lagging_natural(
    envelope_enabled: bool,
    lag_tau: float,
    speed_mps: float = 8.0,
    duration_s: float = 20.0,
) -> list[dict]:
    """Full scenario run (straight -> curve -> lane-change @t=6s -> stop @t=13s
    left INTACT — push_triggers_out=False, matching run_frozen_grip_natural),
    zero driver force throughout (mode=lagging, no pushback/frozen offset
    ever injected), target_track active from t=0. Expected to stay in AUTO
    the entire run (passive lag-following does not trip either opposition
    signature — see OverrideManager.cpp's b6dc58f0 servo-creep design note);
    this is asserted, not just assumed, by the analysis script (0 manual
    edges expected)."""
    os.environ["GT_HEADLESS_FFB_MODE"] = "lagging"
    os.environ.pop("GT_HEADLESS_FFB_FROZEN_AT", None)
    os.environ["GT_HEADLESS_FFB_LAG_TAU"] = f"{lag_tau:.4f}"

    tmpdir = tempfile.mkdtemp(prefix="vd_lagchar_")
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
        b"lagchar",
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


def main() -> int:
    out_dir = os.path.join(
        os.path.dirname(BASE_XOSC),
        "..",
        "..",
        "test_results",
        "vd_ffb_lag_characterization",
    )
    out_dir = os.path.abspath(out_dir)
    os.makedirs(out_dir, exist_ok=True)

    for lag_tau in (0.30, 0.15):
        for envelope_enabled in (False, True):
            frames = run_lagging_natural(envelope_enabled, lag_tau)
            manual_edges = sum(
                1 for f in frames if f.get("override", {}).get("manual_transition")
            )
            print(
                f"tau={lag_tau} envelope_enabled={envelope_enabled}: n_frames={len(frames)} "
                f"manual_edges={manual_edges} (expect 0)"
            )
            tag = f"lagging_tau{lag_tau:g}_env{envelope_enabled}"
            with open(
                os.path.join(out_dir, f"{tag}.json"), "w", encoding="utf-8"
            ) as fh:
                json.dump(frames, fh, indent=1)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
