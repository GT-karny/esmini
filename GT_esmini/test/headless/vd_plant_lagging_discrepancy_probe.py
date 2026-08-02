"""feature:F7 — Task X (PM instruction, 2026-07-26): explain the headless-vs-
real-machine discrepancy for "defect 3" (the lag-overshoot false latch found
in `lagging`-mode parity, `test_results/f7_parity_lagging_overshoot_findings.md`)
which does NOT manifest in the real-machine check log
(`test_results/f7_realwheel_stuck_check.log` — zero MANUAL-latch log lines).

**STATUS: WRITTEN, NOT EXECUTED.** Team-lead is running the regression gate
+ parity 18 against the current (09:22:18) DLL; running any headless sim
concurrently risks port conflicts (team-lead's explicit instruction). This
file also assumes the "plant" mode added to HeadlessFfbInput.{hpp,cpp} in
this session is compiled in, which it is NOT yet (no rebuild has happened).
Run only after team-lead confirms both (a) their gate run is done and (b) a
rebuilt DLL with "plant" mode is staged.

Hypothesis under test (plant-fidelity gap): `lagging` mode is a PURE
KINEMATIC 1st-order low-pass (HeadlessFfbInput.cpp::AdvanceLag) with NO
friction deadzone — it can produce arbitrarily fast, arbitrarily precise
position changes in response to a fast target reversal, because nothing
stops it from moving except the LPF's own smoothing. The real G29 has a
~0.17-0.21 breakaway deadzone (CHARACTERIZATION.md §2) that a small,
fast-reversing correction may simply never break through, OR the plant's
own inertia/friction may naturally damp the exact overshoot shape that
triggered `magnitude_opposition` in the `lagging` run (see
f7_parity_lagging_overshoot_findings.md §2.1's frame trace: actual_norm
overshoots target by ~0.08-0.1 for 2 frames right as the AD reverses
steering at the end of the right turn).

Test: replay the EXACT scenarios that showed the lagging-mode false latch
(decelerate_for_right_turn.xosc, traffic_lights_junction.xosc) through
"plant" mode instead, hands-off (driver_force=0 throughout — no pushback/
grip signal injected at all, matching the real-machine check's "don't touch
the wheel" protocol). Two possible outcomes, both informative (team-lead's
framing):
  - plant mode does NOT false-latch  -> confirms "lagging fidelity gap":
    the breakaway deadzone (or the force-coupled dynamics generally)
    prevents the overshoot from ever registering as magnitude_opposition.
    lagging's parity FAIL was an artifact of an unrealistically idealized
    plant, not evidence the real detector logic is unsafe.
  - plant mode DOES also false-latch -> the bug is real and condition-
    dependent; real-machine absence in THIS ONE run doesn't clear it (could
    need a sharper/faster real steering reversal, higher speed, etc. to
    reproduce on hardware).
"""
from __future__ import annotations

import ctypes
import json
import os
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "scripts"))
from vd_resume_transient import DT, _load_lib  # noqa: E402
from vd_ffb_notouch_parity import ROOT, DLL, BASE_CFG, _write_cfg, _write_variant  # noqa: E402

SCENARIOS = [
    "resources/xosc/verification/05_anticipation/decelerate_for_right_turn.xosc",
    "resources/xosc/verification/05_anticipation/traffic_lights_junction.xosc",
]

OUT_DIR = os.path.join(ROOT, "test_results", "vd_plant_lagging_discrepancy")


def run_plant_hands_off(scenario_path: str, duration_s: float = 40.0) -> list[dict]:
    """Hands-off (driver_force=0 for the whole run) replay of `scenario_path`
    through "plant" mode -- same input_type/config pattern as
    vd_ffb_notouch_parity.py's "ffb" variant (headless_ffb,
    ffb_target_track_enabled=true), only GT_HEADLESS_FFB_MODE differs
    (plant instead of lagging). No UDP force-injection packets are sent at
    all (equivalent to the real-machine protocol: "hands never touch the
    wheel"), so no pushback/plant listener traffic is needed -- the default
    driver_force_norm_=0.0 is never overridden.
    """
    os.environ["GT_HEADLESS_FFB_MODE"] = "plant"
    os.environ.pop("GT_HEADLESS_FFB_FROZEN_AT", None)
    os.environ.pop("GT_HEADLESS_FFB_LAG_TAU", None)
    # Plant physical constants left at spec defaults (breakaway=0.19,
    # kinetic=0.16, vmax=1.0) -- the real-measured CHARACTERIZATION.md
    # values, deliberately NOT tuned to "make the test pass either way".
    os.environ.pop("GT_HEADLESS_FFB_PLANT_BREAKAWAY", None)
    os.environ.pop("GT_HEADLESS_FFB_PLANT_KINETIC", None)
    os.environ.pop("GT_HEADLESS_FFB_PLANT_VMAX", None)
    os.environ["GT_HEADLESS_FFB_PLANT_NOISE_AMP"] = "0.0"   # deterministic, no jitter confound
    os.environ["GT_HEADLESS_FFB_PLANT_SEED"] = "12345"

    tmpdir = tempfile.mkdtemp(prefix="vd_plant_discrepancy_")
    cfg_ffb = _write_cfg(tmpdir, "headless_ffb", True, "plant")
    xosc_ffb = _write_variant(scenario_path, tmpdir, cfg_ffb, "plant")

    lib = _load_lib()
    argv_list = [b"plantdiscrepancy", b"--osc", xosc_ffb.encode(), b"--headless",
                 b"--fixed_timestep", b"0.050"]
    argv = (ctypes.c_char_p * len(argv_list))(*argv_list)
    rc = lib.GT_InitWithArgs(len(argv_list), argv)
    if rc != 0:
        raise RuntimeError(f"GT_InitWithArgs rc={rc}")

    buf = ctypes.create_string_buffer(32768)
    frames: list[dict] = []
    for _ in range(int(duration_s / DT) + 20):
        lib.GT_Step(DT)
        n = lib.GT_GetVirtualDriverTelemetry(0, buf, len(buf))
        if n > 0:
            try:
                frames.append(json.loads(buf.value.decode()))
            except json.JSONDecodeError:
                continue
    lib.GT_Close()
    try:
        os.remove(xosc_ffb)
    except OSError:
        pass
    return frames


def summarize(frames: list[dict]) -> dict:
    manual_edges = [f for f in frames if f.get("override", {}).get("manual_transition")]
    max_pos_err = max((abs(f["ffb"]["position_error"]) for f in frames if f["ffb"].get("target_active")), default=0.0)
    max_mag_opp_frames = sum(1 for f in frames if f.get("ffb", {}).get("gates", {}).get("magnitude_opposition"))
    return {
        "n_frames": len(frames),
        "n_manual_edges": len(manual_edges),
        "manual_edge_times": [f["sim_time"] for f in manual_edges],
        "max_position_error": max_pos_err,
        "n_frames_magnitude_opposition_true": max_mag_opp_frames,
    }


def main() -> int:
    print("NOT EXECUTED -- see module docstring. Requires: (1) team-lead's "
          "current gate run finished, (2) a rebuilt DLL with 'plant' mode "
          "compiled in. Once both hold, run this directly:")
    os.makedirs(OUT_DIR, exist_ok=True)
    for scen in SCENARIOS:
        name = Path(scen).stem
        print(f"  frames = run_plant_hands_off('{scen}')")
        print(f"  # save to {os.path.join(OUT_DIR, name + '_plant.json')}")
    print("\nCompare each scenario's result against the matching 'lagging' "
          "parity FAIL data already on hand:")
    print("  decelerate_for_right_turn: test_results/diag_lag_right_turn.json "
          "(manual_transition=True @ t=14.10)")
    print("  traffic_lights_junction:   printed trace only (not persisted), "
          "see f7_parity_lagging_overshoot_findings.md §2.2 "
          "(manual_transition=True @ t=18.70)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
