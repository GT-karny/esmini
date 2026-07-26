"""feature:F7 (F7b) FFB-vs-stub no-touch parity check.

Acceptance criterion set by PM (post-f8a5ce56 real-machine iteration):

  "ハンコンを繋がないときと繋ぐときで、全く同じ挙動になるようにしてください"

Interpreted precisely:
  (1) input_type=stub, ffb_target_track_enabled=false (shipped default) →
      AD run must equal the committed pre-F7b baseline.  ← proven by 3-way
      validation (pre-F7b + shipped / F7b + shipped / F7b + drifted-config).
      Guarded permanently by run_regression_gate.ps1 Step 0 (drift check)
      + the existing baseline diff in Steps 2/2.6/2.7.

  (2) input_type=headless_ffb (or sdl2_wheel with no user touch) +
      ffb_target_track_enabled=true → AD run must EQUAL (1).  FFB may only
      be an OUTPUT path — the servo pushing the wheel must NEVER change what
      AD decides, plans, or drives. This script proves (2) headlessly by
      running each scenario twice (config A: stub baseline; config B: headless
      FFB + target_track ON, swept across THREE force-coupled wheel-physics
      variants — see FOLLOWER_MODES below) and asserting the per-scenario
      verdict AND the per-frame ego kinematic telemetry (x, y, speed,
      track_id, lane_id) match within tolerance, for every variant.

The sweep runs the force-coupled "plant" wheel at three points spanning the
real calibration uncertainty (breakaway across the measured 0.170-0.210 band,
force→velocity slope ±10 % around the measured 3.35). It is load-bearing in
two ways: it covers the band a real device could sit anywhere in, and it keeps
the plant's constants OFF the detector's shadow constants so a passing run is
evidence about the wheel rather than a tautology about a shared model (see the
INDEPENDENCE REQUIREMENT in ManualDriveConfig.hpp). Any AD-side divergence
between A and B, in ANY variant, means the FFB pathway is bleeding back into
AD decisions.

Runs against each scenario in resources/xosc/verification/anticipation_driving_batch.yaml
(covers straight/LC/curve/junction crossing/right-turn/traffic-lights).
Requires build/GT_esmini/Release/GT_esminiLib.dll (rebuild first).
"""
from __future__ import annotations

import ctypes
import json
import os
import shutil
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DLL = os.path.join(ROOT, "build", "GT_esmini", "Release", "GT_esminiLib.dll")
BASE_CFG = os.path.join(ROOT, "GT_esmini", "config", "virtual_driver.json")

SCENARIOS = [
    # LC-only reproducer for the post-f723fa90 wheel-stuck false-latch bug.
    # Prior to that fix, follower mode passed (perfect axis follow → no false
    # latch); the FROZEN@0 mode variant below catches the real-G29 case
    # (small AD steer command below breakaway friction → wheel stays at 0).
    "resources/xosc/virtual_driver_basic.xosc",
    "resources/xosc/verification/05_anticipation/decelerate_for_curve.xosc",
    "resources/xosc/verification/05_anticipation/decelerate_for_right_turn.xosc",
    "resources/xosc/verification/05_anticipation/cross_straight_junction.xosc",
    "resources/xosc/verification/05_anticipation/speed_limit_change.xosc",
    "resources/xosc/verification/05_anticipation/traffic_lights_junction.xosc",
]

# HeadlessFfbInput's synthetic-wheel physics models. See HeadlessFfbInput.cpp.
#
# WHY follower/frozen ARE NO LONGER SWEPT HERE (2026-07-26, residual rework).
# Both are KINEMATIC fictions: they assert an axis trajectory outright, with no
# regard for the force acting on the wheel.
#   follower — axis == target every frame. The servo therefore commands ~0
#              force, yet the wheel moves. A wheel that moves with no force on
#              it has, by definition, an external hand on it.
#   frozen   — axis pinned regardless of force, including while the servo
#              saturates at 0.6. Measured G29 breakaway is 0.170-0.210, so a
#              real wheel is moving well before that. A wheel immobile under
#              0.6 also has an external hand on it.
# Against the previous DIRECTION-based detector these were harmless (it only
# looked at where the wheel sat relative to target). Against a FORCE-based
# detector they are, by construction, indistinguishable from a driver — and
# indeed both now produce MANUAL latches here. Keeping them would require
# either disabling the detector under test or calibrating the shadow to a
# device that does not exist, so they are retired rather than papered over.
#
# The evidence that this is a fixture defect and not a product defect:
#   - the force-coupled plant, whose constants are all measured, passes every
#     scenario including the two that used to FAIL;
#   - the real machine, run hands-off on decelerate_for_right_turn (the same
#     scenario), produced ZERO MANUAL latches (test_results/
#     f7_realwheel_stuck_check.log). Reality agrees with "plant", not with
#     "follower"/"frozen".
# The positional edge cases they used to cover are now covered honestly by the
# unit gate: a stuck wheel across the measured breakaway band
# (FalsePositive1_StuckWheelWithinMeasuredBand) and hands-off tracking
# (FalsePositive2/3). Set GT_VD_PARITY_LEGACY_MODES=1 to sweep them anyway for
# a one-off comparison; they are expected to FAIL and are not part of the gate.
#   plant     — FORCE-COUPLED stick-slip wheel. The axis is integrated from
#               the force the servo actually delivers, through the real G29
#               friction/velocity characteristic measured in
#               scripts/ffb_spike/CHARACTERIZATION.md §2/§3 (breakaway
#               0.170-0.210, kinetic floor 0.16, v ≈ 3.35·(|f|−0.16)
#               saturating at ~1.0/s). This is the ONLY mode where
#               position_error AND d(actual)/dt are BOTH simultaneously
#               non-zero for an extended, physically-shaped stretch — the real
#               "servo is dragging a hands-off wheel" state — AND the only one
#               that reproduces the hard sub-breakaway deadzone (below 0.16
#               the measured displacement is exactly zero, no creep).
#
#               It REPLACED the former "lagging" mode, which chased the target
#               through a 1st-order low-pass and never looked at force at all.
#               That made it a kinematic caricature: it could not represent a
#               deadzone, so a hands-off wheel "moved" in states where a real
#               one physically cannot, and it produced tracking shapes the
#               servo could never actually command. Its two standing FAILs
#               (decelerate_for_right_turn t=14.10, traffic_lights_junction
#               t=18.70 — hands-off MANUAL latches that the real machine does
#               NOT produce on the same scenarios) were artifacts of that
#               infidelity, not defects of the product. The plant's constants
#               come from measurement, so a FAIL here is evidence about the
#               product rather than about the model's tuning.
#
#
#               Swept across the measured breakaway band AND off the nominal
#               force->velocity slope. That sweep is load-bearing for a reason
#               beyond coverage: the detector's shadow model must NOT be the
#               same model as this plant (see the INDEPENDENCE REQUIREMENT in
#               HeadlessFfbInput.hpp / ManualDriveConfig.hpp), or "hands off
#               gives zero residual" degenerates into a statement about one
#               shared function rather than about the wheel. The variants below
#               deliberately place the plant away from the shadow's constants
#               (shadow: unconditional 0.210, band bottoms 0.170 left /
#               0.190 right, slope 3.35) so no run is a fixed point of both.
# 6 scenarios x 3 variants = 18 checks. All must pass.
FOLLOWER_MODES = [
    # bottom of the measured band, slope 10% below nominal
    ("plant", {"GT_HEADLESS_FFB_PLANT_BREAKAWAY": "0.170",
               "GT_HEADLESS_FFB_PLANT_SLOPE":     "3.00"}),
    # mid-band, nominal slope
    ("plant", {"GT_HEADLESS_FFB_PLANT_BREAKAWAY": "0.190",
               "GT_HEADLESS_FFB_PLANT_SLOPE":     "3.35"}),
    # top of the measured band, slope 10% above nominal
    ("plant", {"GT_HEADLESS_FFB_PLANT_BREAKAWAY": "0.210",
               "GT_HEADLESS_FFB_PLANT_SLOPE":     "3.70"}),
]

# Opt-in only; expected to FAIL (see the note above). Not part of the gate.
LEGACY_MODES = [
    ("follower", {}),
    ("frozen",   {"GT_HEADLESS_FFB_FROZEN_AT": "0.000"}),
]
if os.environ.get("GT_VD_PARITY_LEGACY_MODES") == "1":
    FOLLOWER_MODES = FOLLOWER_MODES + LEGACY_MODES


def _absolutize(root: ET.Element, base_dir: str) -> None:
    for tag in ("LogicFile", "SceneGraphFile"):
        for el in root.findall(f".//{tag}"):
            fp = el.get("filepath")
            if fp and not os.path.isabs(fp):
                el.set("filepath", os.path.abspath(os.path.join(base_dir, fp)))
    for el in root.findall(".//CatalogLocations//Directory"):
        pth = el.get("path")
        if pth and not os.path.isabs(pth):
            el.set("path", os.path.abspath(os.path.join(base_dir, pth)))


def _write_variant(scenario: str, tmpdir: str, cfg_path: str, tag: str) -> str:
    tree = ET.parse(scenario)
    root = tree.getroot()
    _absolutize(root, os.path.dirname(os.path.abspath(scenario)))
    ctrl = root.find(".//ObjectController/Controller[@name='VirtualDriverController']")
    if ctrl is None:
        raise RuntimeError(f"no VirtualDriverController in {scenario}")
    props = ctrl.find("Properties")
    for p in list(props.findall("Property")):
        if p.get("name") == "ConfigFile":
            props.remove(p)
    p = ET.SubElement(props, "Property")
    p.set("name", "ConfigFile")
    p.set("value", cfg_path)
    out = os.path.join(tmpdir, f"{Path(scenario).stem}.{tag}.xosc")
    tree.write(out, encoding="utf-8", xml_declaration=True)
    return out


def _write_cfg(tmpdir: str, input_type: str, target_track_enabled: bool, tag: str) -> str:
    with open(BASE_CFG, encoding="utf-8") as f:
        base = json.load(f)
    base["input_type"] = input_type
    base["ffb_target_track_enabled"] = target_track_enabled
    out = os.path.join(tmpdir, f"vd_config_{tag}.json")
    with open(out, "w", encoding="utf-8") as f:
        json.dump(base, f, indent=2, ensure_ascii=False)
    return out


def _run_headless(dll_path: str, xosc_path: str, dt: float = 0.05,
                  max_time_s: float = 40.0) -> list[dict]:
    """Run one scenario headless and collect per-frame telemetry dicts."""
    lib = ctypes.CDLL(dll_path)
    lib.GT_InitWithArgs.argtypes = [ctypes.c_int, ctypes.POINTER(ctypes.c_char_p)]
    lib.GT_InitWithArgs.restype  = ctypes.c_int
    lib.GT_Step.argtypes = [ctypes.c_double]
    lib.GT_Close.argtypes = []
    lib.GT_GetVirtualDriverTelemetry.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
    lib.GT_GetVirtualDriverTelemetry.restype  = ctypes.c_int

    argv_list = [b"parity", b"--osc", xosc_path.encode(), b"--headless",
                 b"--fixed_timestep", f"{dt:.3f}".encode()]
    argv = (ctypes.c_char_p * len(argv_list))(*argv_list)
    rc = lib.GT_InitWithArgs(len(argv_list), argv)
    if rc != 0:
        raise RuntimeError(f"GT_InitWithArgs rc={rc} on {xosc_path}")

    buf = ctypes.create_string_buffer(32768)
    frames: list[dict] = []
    for _ in range(int(max_time_s / dt) + 20):
        lib.GT_Step(dt)
        n = lib.GT_GetVirtualDriverTelemetry(0, buf, len(buf))
        if n <= 0:
            continue
        try:
            f = json.loads(buf.value.decode())
        except json.JSONDecodeError:
            continue
        frames.append(f)
    lib.GT_Close()
    return frames


def _diff_frames(a: list[dict], b: list[dict],
                 pos_tol: float = 0.05, speed_tol: float = 0.05) -> list[str]:
    """Return list of human-readable divergence lines (empty = parity holds)."""
    diffs = []
    if len(a) != len(b):
        diffs.append(f"frame count differs: stub={len(a)} ffb={len(b)}")
    n = min(len(a), len(b))
    max_dx = max_dy = max_dv = 0.0
    lane_mismatches: list[str] = []
    for i in range(n):
        ea = a[i]["ego"]; eb = b[i]["ego"]
        dx = abs(ea["x"] - eb["x"]); dy = abs(ea["y"] - eb["y"])
        dv = abs(ea["speed"] - eb["speed"])
        if dx > max_dx: max_dx = dx
        if dy > max_dy: max_dy = dy
        if dv > max_dv: max_dv = dv
        if ea.get("track") != eb.get("track") or ea.get("lane") != eb.get("lane"):
            if len(lane_mismatches) < 5:
                lane_mismatches.append(
                    f"  t={a[i]['sim_time']:.2f} stub(road={ea.get('track')},lane={ea.get('lane')}) "
                    f"vs ffb(road={eb.get('track')},lane={eb.get('lane')})"
                )
    if max_dx > pos_tol or max_dy > pos_tol:
        diffs.append(f"ego position max diff: dx={max_dx:.3f}m dy={max_dy:.3f}m (tol {pos_tol}m)")
    if max_dv > speed_tol:
        diffs.append(f"ego speed max diff: {max_dv:.3f}m/s (tol {speed_tol}m/s)")
    if lane_mismatches:
        diffs.append("road/lane mismatch:\n" + "\n".join(lane_mismatches))
    # override latch must be identical (any spurious latch on the FFB side is a violation)
    for i in range(n):
        oa = a[i].get("override", {}); ob = b[i].get("override", {})
        if oa.get("lateral") != ob.get("lateral") or oa.get("longitudinal") != ob.get("longitudinal"):
            diffs.append(
                f"override state diff at t={a[i]['sim_time']:.2f}: stub={oa} vs ffb={ob}"
            )
            break
    return diffs


def main() -> int:
    if not os.path.exists(DLL):
        print(f"FAIL: DLL missing: {DLL}"); return 1

    tmpdir = tempfile.mkdtemp(prefix="vd_ffb_parity_")
    cfg_stub = _write_cfg(tmpdir, "stub",         False, "stub")
    cfg_ffb  = _write_cfg(tmpdir, "headless_ffb", True,  "ffb")
    os.environ.pop("GT_HEADLESS_FFB_LAG_TAU", None)

    overall_ok = True
    print("=" * 80)
    print("FFB vs stub no-touch parity — criterion (2)")
    print("  stub baseline: input_type=stub, ffb_target_track_enabled=false")
    print("  ffb variants: input_type=headless_ffb, ffb_target_track_enabled=true")
    print(f"    modes: {[m for m, _ in FOLLOWER_MODES]}")
    print("=" * 80)

    for scen in SCENARIOS:
        name = Path(scen).stem
        xosc_stub = _write_variant(scen, tmpdir, cfg_stub, "stub")
        xosc_ffb  = _write_variant(scen, tmpdir, cfg_ffb,  "ffb")

        print(f"\n[scenario] {name}")
        print("  running stub baseline …", end=" ")
        # stub baseline is independent of FOLLOWER_MODES (no FFB sink).
        os.environ["GT_HEADLESS_FFB_MODE"] = "follower"
        os.environ.pop("GT_HEADLESS_FFB_FROZEN_AT", None)
        frames_stub = _run_headless(DLL, xosc_stub)
        print(f"{len(frames_stub)} frames")

        for mode, extra_env in FOLLOWER_MODES:
            os.environ["GT_HEADLESS_FFB_MODE"] = mode
            # Clear every mode-specific knob first so each variant starts from
            # a known state regardless of run order, then apply this mode's own.
            for var in ("GT_HEADLESS_FFB_FROZEN_AT", "GT_HEADLESS_FFB_LAG_TAU",
                        "GT_HEADLESS_FFB_PLANT_BREAKAWAY", "GT_HEADLESS_FFB_PLANT_SLOPE"):
                os.environ.pop(var, None)
            os.environ.update(extra_env)
            if mode == "frozen":
                label = f"{mode}@{extra_env['GT_HEADLESS_FFB_FROZEN_AT']}"
            elif mode == "plant":
                label = (f"{mode}(brk={extra_env['GT_HEADLESS_FFB_PLANT_BREAKAWAY']},"
                         f"slope={extra_env['GT_HEADLESS_FFB_PLANT_SLOPE']})")
            else:
                label = mode
            print(f"  running ffb+target_track ({label}, no-touch) …", end=" ")
            frames_ffb = _run_headless(DLL, xosc_ffb)
            print(f"{len(frames_ffb)} frames")

            diffs = _diff_frames(frames_stub, frames_ffb)
            if not diffs:
                print(f"    PARITY[{label}]: PASS")
            else:
                overall_ok = False
                print(f"    PARITY[{label}]: FAIL")
                for d in diffs:
                    print(f"      - {d}")

    shutil.rmtree(tmpdir, ignore_errors=True)
    print()
    print("=" * 80)
    print(f"RESULT: {'PASS' if overall_ok else 'FAIL'}  (criterion (2): no-touch ffb == stub baseline)")
    print("=" * 80)
    return 0 if overall_ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
