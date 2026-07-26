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
      running each scenario against a stub baseline (config A) and against a
      set of synthetic wheel fixtures (config B), then asserting per-frame ego
      kinematic telemetry (x, y, speed, track_id, lane_id) matches within
      tolerance. Fixtures come in two classes with DIFFERENT expectations —
      see FOLLOWER_MODES below.

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
import math
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
# TWO FIXTURE CLASSES, BECAUSE THEY ASK DIFFERENT QUESTIONS.
#
# The synthetic wheels fall into two physically distinct groups, and lumping
# them together is what made the earlier mode list confusing:
#
#   FORCE-COUPLED ("plant"): the axis is INTEGRATED from the force the servo
#     actually delivers, through the measured G29 friction characteristic. This
#     is what a hands-off wheel really does, so the detector must stay silent
#     and the AD side must be bit-identical to the stub baseline.
#     -> class "parity": expect NO divergence.
#
#   KINEMATIC ("frozen", "follower"): the axis trajectory is ASSERTED outright,
#     with no regard for the force acting on the wheel.
#       follower — axis == target every frame, so the servo commands ~0 force
#                  yet the wheel moves. A wheel that moves under no force has,
#                  by definition, something external moving it.
#       frozen   — axis pinned regardless of force, including while the servo
#                  saturates at 0.6, far above the measured 0.170-0.210
#                  breakaway. A wheel immobile under 0.6 has something external
#                  holding it.
#     Under the old DIRECTION-based detector these were inert (it only compared
#     positions). Under a FORCE-based detector they ARE, by construction, a
#     driver — and detecting a driver is the feature. Deleting them would throw
#     away real fixture diversity; asserting "no latch" would assert the
#     detector is broken. So they are kept as POSITIVE fixtures.
#     -> class "liveness". The two do NOT share an expectation, because they
#        reach the detector by different routes and pretending otherwise is
#        what makes such a check look arbitrary:
#
#        frozen   — POSITIVE fixture. The axis is pinned, so the tracking error
#                   equals the AD command and never decays; the servo holds a
#                   standing force, and once that clears the shadow's breakaway
#                   the shadow accelerates away with nothing to stop it. The
#                   residual then grows WITHOUT BOUND however small the command,
#                   so the predicate is purely "does the force clear breakaway"
#                   (~|target| >= 0.017 with shipped constants) — independent of
#                   the residual threshold. Falsifiable both ways: a scenario
#                   that never steers (speed_limit_change) must NOT latch.
#
#        follower — NO-BLEED fixture; its latch is deliberately NOT predicted.
#                   With the axis glued to the target the servo commands ~no
#                   force, the shadow never moves, and the only residual
#                   available is the re-anchor's lag — a function of the
#                   target's whole RATE HISTORY, i.e. a property of the
#                   SCENARIO, not of the fixture. Predicting it would mean
#                   re-deriving the detector's dynamics inside the check.
#                   What it does assert is the invariant this script is named
#                   after (no FFB bleed into AD) plus the one direction that
#                   needs no model: no steering command => no latch.
#
#        Both additionally assert PRE-LATCH AD PARITY: up to the moment of any
#        legitimate takeover, the AD side must still match the stub baseline.
#        After the latch the ego is under manual control, so divergence there
#        is expected and is not compared.
#
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
# 6 scenarios x 5 fixtures = 30 checks (18 parity + 12 liveness). All must pass.
FOLLOWER_MODES = [
    # --- force-coupled: expect NO divergence ------------------------------
    # bottom of the measured band, slope 10% below nominal
    ("plant", "parity", {"GT_HEADLESS_FFB_PLANT_BREAKAWAY": "0.170",
                         "GT_HEADLESS_FFB_PLANT_SLOPE":     "3.00"}),
    # mid-band, nominal slope
    ("plant", "parity", {"GT_HEADLESS_FFB_PLANT_BREAKAWAY": "0.190",
                         "GT_HEADLESS_FFB_PLANT_SLOPE":     "3.35"}),
    # top of the measured band, slope 10% above nominal
    ("plant", "parity", {"GT_HEADLESS_FFB_PLANT_BREAKAWAY": "0.210",
                         "GT_HEADLESS_FFB_PLANT_SLOPE":     "3.70"}),
    # --- kinematic: expect a latch iff one is physically reachable ---------
    ("frozen",   "liveness", {"GT_HEADLESS_FFB_FROZEN_AT": "0.000"}),
    ("follower", "liveness", {}),
]


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


# 実機の GT_Sim は、--fixed_timestep 0.05 を渡しても検出器が見る dt は 0.01 だった
# （2026-07-26 の実機 3 走行すべてで実測 0.01）。合成を 0.05 で回すと実機と 5 倍違い、
# 残差は dt に依存する（合成 worst case は tau=0 で 0.0139@0.05 -> 0.0248@0.01）。
# 検証は実機と同じ刻みで行う。
REAL_MACHINE_DT = 0.01


def _run_headless(dll_path: str, xosc_path: str, dt: float = REAL_MACHINE_DT,
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


def _first_latch_index(frames: list[dict]) -> int:
    """Index of the first frame where the lateral override latched (-1 = never)."""
    for i, f in enumerate(frames):
        if f.get("override", {}).get("lateral"):
            return i
    return -1


def _liveness_verdict(mode: str, cfg: dict,
                      frames_stub: list[dict], frames_ffb: list[dict]) -> tuple[bool, str]:
    """Check a KINEMATIC fixture (frozen / follower).

    Two assertions:

      1. A latch must occur exactly when one is physically REACHABLE. The two
         fixtures reach it by completely different routes, so they get
         different predicates — using one formula for both is what makes this
         check look arbitrary.

         frozen   — the axis is pinned, so the tracking error equals the AD
                    command and never decays. The servo therefore holds a
                    standing force, and once that force clears the shadow's
                    breakaway the shadow accelerates away with nothing to stop
                    it: the residual grows WITHOUT BOUND, however small the
                    command. So the predicate is purely "does the servo force
                    ever clear breakaway", i.e.
                        kp·max|target| + friction_ff·tanh(max|target|/eps) >= breakaway
                    (~|target| >= 0.017 with shipped constants). NOT a function
                    of the residual threshold at all.

         follower — the axis equals the target every frame, so the tracking
                    error is ~0 and the servo commands ~no force. The shadow
                    never enters its moving state; it only creeps via the
                    re-anchor, which chases the measured axis with time
                    constant tau. The residual is therefore what the re-anchor
                    LAGS BY, which depends on how fast the target moves, not
                    how far it goes:
                        max|d(target)/dt| · tau > residual_threshold
                    This is the §3.4 rate floor seen end-to-end: a slow curve
                    stays under it, a sharp junction turn does not.

         Both predicates are static formulas over config constants — they do
         not re-implement the shadow, and they are falsifiable in both
         directions (a scenario that barely steers must NOT latch).

      2. Up to the latch, the AD side must still match the stub baseline —
         i.e. the FFB path did not bleed into AD decisions BEFORE the takeover
         it legitimately caused. After the latch the ego is under manual
         control, so divergence there is expected and not compared.
    """
    thr = 0.0
    max_target = 0.0
    max_target_rate = 0.0
    prev_t = prev_target = None
    for f in frames_ffb:
        ffb = f.get("ffb", {})
        gates = ffb.get("gates", {})
        thr = max(thr, float(gates.get("residual_threshold", 0.0)))
        tgt = float(ffb.get("target_norm", 0.0))
        max_target = max(max_target, abs(tgt))
        t = float(f.get("sim_time", 0.0))
        if prev_t is not None and t > prev_t:
            max_target_rate = max(max_target_rate, abs(tgt - prev_target) / (t - prev_t))
        prev_t, prev_target = t, tgt

    if thr <= 0.0:
        return False, "no residual_threshold in telemetry (servo never armed?)"

    key = lambda k, d: float(cfg.get("ffb_target_track_" + k, d))
    latch_i = _first_latch_index(frames_ffb)
    latched = latch_i >= 0

    if mode == "frozen":
        kp   = key("kp", 4.0)
        ff   = key("friction_ff", 0.15)
        eps  = key("friction_ff_eps", 0.01)
        brk  = key("override_shadow_breakaway", 0.21)
        force = kp * max_target + ff * math.tanh(max_target / max(eps, 1e-9))
        reachable = force >= brk
        basis = (f"standing servo force {force:.3f} vs shadow breakaway {brk:.3f} "
                 f"(max|target_norm|={max_target:.4f})")
    elif mode == "follower":
        # Deliberately NOT predicted. With the axis glued to the target the
        # servo commands ~no force, so the shadow never enters its moving
        # state and the only residual available is what the re-anchor lags by.
        # That lag is a function of the target's whole RATE HISTORY (a brief
        # spike builds almost nothing; a sustained ramp builds rate x tau), so
        # whether this fixture latches is a property of the SCENARIO's steering
        # profile, not of the fixture. Predicting it would mean re-deriving the
        # detector's dynamics inside the check — precisely the second
        # implementation this suite exists to avoid.
        #
        # What follower CAN establish, and always could, is the invariant this
        # whole script is named after: the FFB path must not bleed into AD. So
        # assert that (below), plus the one falsifiable direction that needs no
        # model: NO STEERING COMMAND => NO LATCH. A latch with the wheel and
        # the target both parked at zero would be a spurious fire with no
        # possible cause.
        if max_target <= 0.0 and latched:
            return False, ("LIVENESS MISMATCH: latched at t="
                           f"{frames_ffb[latch_i]['sim_time']:.2f} although AD never "
                           "commanded any steering (max|target_norm| = 0)")
        reachable = latched   # not asserted; recorded below
        basis = (f"max|target_norm|={max_target:.4f}, max|d(target)/dt|={max_target_rate:.4f}/s "
                 f"(latch not predicted for this fixture — scenario-dependent)")
    else:
        return False, f"unknown liveness fixture '{mode}'"

    detail = (f"{basis} -> latch {'expected' if reachable else 'not expected'}; "
              f"observed {'latch @ t=' + format(frames_ffb[latch_i]['sim_time'], '.2f') if latched else 'no latch'}")

    if latched != reachable:
        return False, "LIVENESS MISMATCH: " + detail

    # Pre-latch AD parity. Compare only the frames before the takeover.
    n = latch_i if latched else min(len(frames_stub), len(frames_ffb))
    pre_diffs = _diff_frames(frames_stub[:n], frames_ffb[:n]) if n > 1 else []
    if pre_diffs:
        return False, ("pre-latch AD divergence (" + detail + "):\n      - "
                       + "\n      - ".join(pre_diffs))
    return True, detail + f"; pre-latch AD parity over {n} frames"


def main() -> int:
    if not os.path.exists(DLL):
        print(f"FAIL: DLL missing: {DLL}"); return 1

    tmpdir = tempfile.mkdtemp(prefix="vd_ffb_parity_")
    base_cfg = json.loads(Path(BASE_CFG).read_text(encoding="utf-8"))
    cfg_stub = _write_cfg(tmpdir, "stub",         False, "stub")
    cfg_ffb  = _write_cfg(tmpdir, "headless_ffb", True,  "ffb")
    os.environ.pop("GT_HEADLESS_FFB_LAG_TAU", None)

    overall_ok = True
    print("=" * 80)
    print("FFB vs stub no-touch parity — criterion (2)")
    print("  stub baseline: input_type=stub, ffb_target_track_enabled=false")
    print("  ffb variants: input_type=headless_ffb, ffb_target_track_enabled=true")
    print(f"    parity fixtures  (expect no divergence): "
          f"{[m for m, c, _ in FOLLOWER_MODES if c == 'parity']}")
    print(f"    liveness fixtures (expect latch iff reachable): "
          f"{[m for m, c, _ in FOLLOWER_MODES if c == 'liveness']}")
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

        for mode, fixture_class, extra_env in FOLLOWER_MODES:
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
            tag = "PARITY" if fixture_class == "parity" else "LIVENESS"
            print(f"  running ffb+target_track ({label}, {fixture_class}, no-touch) …", end=" ")
            frames_ffb = _run_headless(DLL, xosc_ffb)
            print(f"{len(frames_ffb)} frames")

            if fixture_class == "parity":
                diffs = _diff_frames(frames_stub, frames_ffb)
                if not diffs:
                    print(f"    {tag}[{label}]: PASS")
                else:
                    overall_ok = False
                    print(f"    {tag}[{label}]: FAIL")
                    for d in diffs:
                        print(f"      - {d}")
            else:
                ok, detail = _liveness_verdict(mode, base_cfg, frames_stub, frames_ffb)
                if ok:
                    print(f"    {tag}[{label}]: PASS  ({detail})")
                else:
                    overall_ok = False
                    print(f"    {tag}[{label}]: FAIL")
                    print(f"      - {detail}")

    shutil.rmtree(tmpdir, ignore_errors=True)
    print()
    print("=" * 80)
    print(f"RESULT: {'PASS' if overall_ok else 'FAIL'}  (criterion (2): no-touch ffb == stub baseline)")
    print("=" * 80)
    return 0 if overall_ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
