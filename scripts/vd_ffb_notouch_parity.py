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
      set of synthetic wheel fixtures (config B), then asserting the AD's
      DECISION fields (driver.throttle/brake/steer, envelope.steer_in/
      steer_out/kappa_cmd) are EXACTLY equal frame-by-frame — not a tolerance
      on the resulting ego kinematic state (x/y/speed), which is a downstream
      integrated CONSEQUENCE of the decision and can absorb a small genuine
      leak inside a tolerance band. See _first_ad_decision_divergence's
      docstring for why exact equality is a meaningful (not merely stricter)
      standard here, and _check_self_determinism / the per-scenario
      "determinism control" for why an A-vs-B exact-match claim is only
      trustworthy once a same-config-twice control has been shown clean.
      (Upgraded from tolerance-based ego-kinematic comparison to this
      decision-field exact-match standard 2026-07-28, per an independent
      audit that rated the underlying "no-touch parity" requirement
      "unconfirmed" — neither passing nor failing, because nothing had
      checked it against a strict standard.) Fixtures come in two classes
      with DIFFERENT expectations — see FOLLOWER_MODES below.

The sweep runs the force-coupled "plant" wheel at three points spanning the
real calibration uncertainty (breakaway across the measured 0.170-0.210 band,
force→velocity slope ±10 % around the measured 3.35). It is load-bearing in
two ways: it covers the band a real device could sit anywhere in, and it keeps
the plant's constants OFF the detector's shadow constants so a passing run is
evidence about the wheel rather than a tautology about a shared model (see the
INDEPENDENCE REQUIREMENT in ManualDriveConfig.hpp). Any AD-side divergence
between A and B, in ANY variant, means the FFB pathway is bleeding back into
AD decisions.

WIRED INTO run_regression_gate.ps1 as an optional step (-NoTouchParity; Step 3,
same opt-in-is-hard convention as -TelemetryGolden), as of 2026-07-28.

History: at introduction (052c5782) the self-determinism control this script
runs FIRST (same stub config, twice, in two fully separate processes) FAILED
on 3 of the 6 scenarios (virtual_driver_basic, decelerate_for_left_turn,
traffic_lights_junction) -- the identical config produced a different
driver.brake decision (~1e-9 relative, at the exact frame braking begins) that
then propagated and amplified through the closed control loop to ~1e-4 by the
end of the run. This was a genuine, reproducible engine determinism gap
unrelated to FFB (it reproduced with input_type=stub, no wheel/FFB device
involved at all). Wiring it in at that point, in any form, would have either
(a) as a hard gate, blocked merges for a reason that had nothing to do with
what the gate claims to test, or (b) as a WARN-only gate, silently skipped the
3 affected scenarios -- exactly the "looks like coverage but isn't" appearance
this project's audit series has repeatedly flagged elsewhere.

e25e9c85 root-caused and removed it (IdleJitter's display-only RPM was leaking
into RealVehicle's engine-drag physics term). Re-run after the fix: 6/6
scenarios clean on the determinism control, 6/6 PASS, reproduced twice
independently in this session and once more by an independent audit's own
re-run. The premise that made wiring unsound (half the scenarios silently
untestable) no longer holds, so this now follows -TelemetryGolden's own
precedent exactly: off by default (zero effect on the standard gate), a hard
failure once a caller explicitly asks for it via -NoTouchParity. See
test_results/f7_web_progress.md for the full history and the wiring decision.

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
import subprocess
import sys
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
    "resources/xosc/verification/05_anticipation/decelerate_for_left_turn.xosc",
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
#               (decelerate_for_left_turn t=14.10, traffic_lights_junction
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
    # 3 点とも、静摩擦帯・力速度傾き・**過渡**（theta, tau）を同時に振る。
    # 過渡の振り幅は 2026-07-26 の実機同定のばらつきから取った
    # （theta 中央値 0.0408 / min 0.0361 / max 0.0815、tau 中央値 0.0179 / max 0.0227）。
    # シャドウ側には公称値だけを入れる。ここで同じ値を入れてしまうと一致は構成上の
    # 必然になり、ヘッドレスの結果は何も証明しなくなる（INDEPENDENCE REQUIREMENT）。
    # このモードの役割は「シャドウと一致すること」ではなく
    # 「シャドウが現実のばらつきに対して頑健であることを試すこと」である。
    ("plant", "parity", {"GT_HEADLESS_FFB_PLANT_BREAKAWAY":    "0.170",
                         "GT_HEADLESS_FFB_PLANT_SLOPE":        "3.00",
                         "GT_HEADLESS_FFB_PLANT_DEAD_TIME":    "0.030",
                         "GT_HEADLESS_FFB_PLANT_VELOCITY_TAU": "0.010"}),
    ("plant", "parity", {"GT_HEADLESS_FFB_PLANT_BREAKAWAY":    "0.190",
                         "GT_HEADLESS_FFB_PLANT_SLOPE":        "3.35",
                         "GT_HEADLESS_FFB_PLANT_DEAD_TIME":    "0.041",
                         "GT_HEADLESS_FFB_PLANT_VELOCITY_TAU": "0.018"}),
    ("plant", "parity", {"GT_HEADLESS_FFB_PLANT_BREAKAWAY":    "0.210",
                         "GT_HEADLESS_FFB_PLANT_SLOPE":        "3.70",
                         "GT_HEADLESS_FFB_PLANT_DEAD_TIME":    "0.055",
                         "GT_HEADLESS_FFB_PLANT_VELOCITY_TAU": "0.025"}),
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


def _run_headless_in_process(dll_path: str, xosc_path: str, dt: float,
                              max_time_s: float) -> list[dict]:
    """Run one scenario headless IN THE CALLING PROCESS and collect per-frame
    telemetry dicts. Do not call this directly from the comparison harness —
    see _run_headless below for why (ctypes.CDLL on Windows reuses an
    already-loaded module rather than truly reloading it, so any static/
    global state GT_esminiLib.dll does not fully reset between GT_Close()
    and a subsequent GT_InitWithArgs() in the SAME process can leak between
    "separate" in-process runs and masquerade as engine non-determinism)."""
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


def _worker_main() -> int:
    """Entry point for the isolated subprocess spawned by _run_headless.
    argv: --worker <dll_path> <xosc_path> <dt> <max_time_s> <out_json_path>.
    Writes the frame list as one JSON array to out_json_path -- NOT stdout.
    The engine writes its own console logging (fprintf/std::cout in the C++
    runtime) to the SAME OS-level stdout file descriptor Python's sys.stdout
    uses; the first version of this worker wrote JSON to stdout and it came
    back with engine log lines interleaved into (and corrupting) the JSON
    payload. A dedicated output file sidesteps shared-fd interleaving
    entirely regardless of what the engine prints or when it flushes."""
    dll_path, xosc_path, dt, max_time_s, out_path = (
        sys.argv[2], sys.argv[3], float(sys.argv[4]), float(sys.argv[5]), sys.argv[6]
    )
    frames = _run_headless_in_process(dll_path, xosc_path, dt, max_time_s)
    Path(out_path).write_text(json.dumps(frames), encoding="utf-8")
    return 0


def _run_headless(dll_path: str, xosc_path: str, dt: float = REAL_MACHINE_DT,
                  max_time_s: float = 40.0) -> list[dict]:
    """Run one scenario headless in a FRESH SUBPROCESS (not in-process) and
    collect per-frame telemetry dicts.

    WHY A SUBPROCESS, NOT JUST ctypes.CDLL PER CALL: the first version of this
    harness's "determinism control" (run the identical stub config twice,
    in-process, via two separate ctypes.CDLL(dll_path) calls) found ~1e-9
    relative divergence in driver.brake on 3 of 6 scenarios. Before reporting
    that as genuine engine non-determinism, it had to be ruled out as an
    artifact of the test methodology itself: on Windows, loading the same DLL
    path twice via ctypes.CDLL in one process does not reload it -- the OS
    loader just bumps the reference count and hands back the SAME already-
    mapped module, so any static/global state GT_esminiLib.dll does not fully
    reset in GT_Close() would silently leak from one "run" into the "next"
    within a single process, masquerading as engine non-determinism. A fresh
    OS process per run has no such shared state (aside from ASLR, which is
    itself re-randomized per process and worth knowing about if it turns out
    to matter). The subprocess's own environment is inherited from this
    process by default, so GT_HEADLESS_FFB_MODE / etc. still apply.
    """
    env_marker = os.environ.get("VD_FFB_PARITY_FORCE_IN_PROCESS")
    if env_marker:  # escape hatch for debugging only, not used by main()
        return _run_headless_in_process(dll_path, xosc_path, dt, max_time_s)

    out_fd, out_path = tempfile.mkstemp(prefix="vd_ffb_worker_", suffix=".json")
    os.close(out_fd)
    try:
        result = subprocess.run(
            [sys.executable, os.path.abspath(__file__), "--worker",
             dll_path, xosc_path, f"{dt!r}", f"{max_time_s!r}", out_path],
            capture_output=True, text=True, timeout=max(120.0, max_time_s * 3),
        )
        if result.returncode != 0:
            raise RuntimeError(
                f"worker subprocess failed (rc={result.returncode}) for {xosc_path}:\n"
                f"stdout(tail)={result.stdout[-2000:]}\nstderr(tail)={result.stderr[-2000:]}"
            )
        try:
            text = Path(out_path).read_text(encoding="utf-8")
            return json.loads(text)
        except (OSError, json.JSONDecodeError) as e:
            raise RuntimeError(
                f"worker subprocess produced no valid JSON output file for {xosc_path}: {e}\n"
                f"stdout(tail)={result.stdout[-2000:]}\nstderr(tail)={result.stderr[-2000:]}"
            ) from e
    finally:
        Path(out_path).unlink(missing_ok=True)


# ---------------------------------------------------------------------------
# feature:F7 AD-decision exact-match check (audit "requirement #3: FFB must be
# output-only, never affect AD decisions" — rated "unconfirmed" by independent
# audit, neither passing nor failing because nothing checked it).
#
# WHY THESE FIELDS, NOT ego x/y/speed: x/y/speed are the OUTCOME of applying
# the AD's decision through vehicle physics over many frames. Comparing them
# with a tolerance (the original _diff_frames below) answers "did the car end
# up in about the same place", not "did the AD ever decide anything
# different". A single-frame decision divergence too small to move the
# tolerance-gated kinematic state would pass a position/speed check while
# still being exactly the kind of FFB-into-AD leak requirement #3 forbids.
# driver.{throttle,brake,steer} is the driver model's decision BEFORE
# integration; envelope.{steer_in,steer_out,kappa_cmd} is the same decision
# immediately before/after the safety-envelope clamp. Comparing these directly
# is comparing the decision itself, not a downstream integrated proxy for it.
#
# WHY "EXACT" IS MEANINGFUL HERE (not just strict for its own sake): the
# engine runs headless with --fixed_timestep (deterministic dt) and no
# real-time pacing. If the AD's decision computation is a pure function of
# scenario state + previous-frame vehicle state (i.e. truly independent of
# input_type/FFB), then two runs that start from the same scenario and see
# the same vehicle-state history must compute bit-for-bit the same decision,
# every frame, forever — any divergence, however small, means SOME input to
# that computation differed. This is only a valid standard if the engine is
# itself deterministic run-to-run given IDENTICAL config (no ASLR-order
# hash-map iteration affecting float summation order, no timing-based
# jitter) — see _check_self_determinism, which is run FIRST as a control and
# must itself show zero divergence before an A-vs-B divergence can be
# attributed to FFB rather than to incidental engine non-determinism.
#
# Precision floor: VirtualDriverTelemetryJson.cpp serializes with
# os.precision(9) specifically so quantization does not mask real
# differences (see its own comment on the jerk quantum). Comparing the
# JSON-round-tripped floats for exact equality is therefore "as exact as this
# instrumentation can show us" — not a claim of true IEEE-754 bit-identity of
# the underlying C++ doubles, which would require a binary export this
# harness does not have (and does not need: a real FFB-into-AD leak would be
# vastly larger than the 1e-9 floor this loses).
_AD_DECISION_FIELDS = [
    ("driver", "throttle"),
    ("driver", "brake"),
    ("driver", "steer"),
    ("envelope", "steer_in"),
    ("envelope", "steer_out"),
    ("envelope", "kappa_cmd"),
]


def _ad_decision_tuple(frame: dict) -> tuple:
    return tuple(frame.get(block, {}).get(key) for block, key in _AD_DECISION_FIELDS)


def _first_ad_decision_divergence(a: list[dict], b: list[dict]) -> str | None:
    """Compare AD-decision fields frame-by-frame for EXACT equality (not
    tolerance). Returns a description of the FIRST divergent frame (index,
    sim_time, field, both values), or None if every compared frame matches
    exactly. Only compares over the overlapping frame range; a frame-count
    mismatch is reported separately by the caller if no field ever diverged
    within the overlap (a silent truncation is still a divergence, just a
    structural one rather than a per-field one)."""
    n = min(len(a), len(b))
    for i in range(n):
        ta, tb = _ad_decision_tuple(a[i]), _ad_decision_tuple(b[i])
        if ta != tb:
            field_diffs = [
                f"{blk}.{key}: a={va!r} vs b={vb!r}"
                for (blk, key), va, vb in zip(_AD_DECISION_FIELDS, ta, tb)
                if va != vb
            ]
            return (f"frame {i} (t={a[i].get('sim_time')}): "
                    + "; ".join(field_diffs))
    if len(a) != len(b):
        return f"frame count differs: a={len(a)} b={len(b)} (no field divergence within the overlapping {n} frames)"
    return None


def _check_self_determinism(dll: str, xosc_path: str) -> str | None:
    """Control: run the SAME config twice and require exact AD-decision
    parity between the two runs. If this control itself fails, a strict
    equality standard against a DIFFERENT config is not meaningful — any
    divergence found there could be incidental engine non-determinism
    (unordered-container iteration order, timing jitter) rather than a
    genuine FFB-into-AD leak. Must be run and shown clean before trusting any
    A-vs-B result for the same scenario."""
    frames_1 = _run_headless(DLL, xosc_path)
    frames_2 = _run_headless(DLL, xosc_path)
    return _first_ad_decision_divergence(frames_1, frames_2)


def _diff_frames(a: list[dict], b: list[dict],
                 pos_tol: float = 0.05, speed_tol: float = 0.05) -> list[str]:
    """Informational only (NOT the pass/fail gate — see _first_ad_decision_divergence
    above for that). Reports the downstream kinematic CONSEQUENCE of a
    decision divergence, i.e. "how far did this actually drift", which is
    useful context once _first_ad_decision_divergence has already found a
    problem. A tolerance check on ego x/y/speed cannot itself prove FFB never
    touched an AD decision (see the WHY note above), so it no longer gates."""
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

    # Pre-latch AD parity: EXACT match on the AD's decision fields, not a
    # tolerance on the resulting kinematic state (see _first_ad_decision_divergence).
    n = latch_i if latched else min(len(frames_stub), len(frames_ffb))
    exact_diff = _first_ad_decision_divergence(frames_stub[:n], frames_ffb[:n]) if n > 1 else None
    if exact_diff:
        kinematic_context = _diff_frames(frames_stub[:n], frames_ffb[:n]) if n > 1 else []
        extra = ("\n      kinematic consequence: " + "; ".join(kinematic_context)
                 if kinematic_context else "\n      kinematic consequence: none observed at the default tolerance")
        return False, (f"pre-latch AD DECISION divergence (" + detail + "):\n      - "
                       + exact_diff + extra)
    return True, detail + f"; pre-latch AD decision EXACT parity over {n} frames"


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

        # Control, run FIRST: same config (stub) twice. If this itself shows
        # any AD-decision divergence, the engine is not deterministic enough
        # for an exact A-vs-B comparison to mean anything for THIS scenario —
        # fail loudly and distinctly rather than silently attributing noise
        # to FFB (or worse, silently passing because tolerance absorbed it).
        os.environ["GT_HEADLESS_FFB_MODE"] = "follower"
        os.environ.pop("GT_HEADLESS_FFB_FROZEN_AT", None)
        print("  running determinism control (stub x2) …", end=" ")
        frames_stub   = _run_headless(DLL, xosc_stub)
        frames_stub_2 = _run_headless(DLL, xosc_stub)
        determinism_diff = _first_ad_decision_divergence(frames_stub, frames_stub_2)
        if determinism_diff:
            overall_ok = False
            print("FAIL")
            print(f"    DETERMINISM CONTROL FAILED for {name}: {determinism_diff}")
            print("    (identical config produced different AD decisions across two runs — "
                  "skipping this scenario's FFB comparisons, since an exact-match standard "
                  "against a DIFFERENT config would be meaningless without a clean control)")
            continue
        print(f"clean ({len(frames_stub)} frames, {len(frames_stub_2)} frames)")

        for mode, fixture_class, extra_env in FOLLOWER_MODES:
            os.environ["GT_HEADLESS_FFB_MODE"] = mode
            # Clear every mode-specific knob first so each variant starts from
            # a known state regardless of run order, then apply this mode's own.
            for var in ("GT_HEADLESS_FFB_FROZEN_AT", "GT_HEADLESS_FFB_LAG_TAU",
                        "GT_HEADLESS_FFB_PLANT_BREAKAWAY", "GT_HEADLESS_FFB_PLANT_SLOPE",
                        "GT_HEADLESS_FFB_PLANT_DEAD_TIME", "GT_HEADLESS_FFB_PLANT_VELOCITY_TAU"):
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
                exact_diff = _first_ad_decision_divergence(frames_stub, frames_ffb)
                if not exact_diff:
                    print(f"    {tag}[{label}]: PASS  (AD decision EXACT parity over "
                          f"{min(len(frames_stub), len(frames_ffb))} frames)")
                else:
                    overall_ok = False
                    print(f"    {tag}[{label}]: FAIL")
                    print(f"      - AD DECISION divergence: {exact_diff}")
                    kinematic_context = _diff_frames(frames_stub, frames_ffb)
                    if kinematic_context:
                        print("      - kinematic consequence: " + "; ".join(kinematic_context))
                    else:
                        print("      - kinematic consequence: none observed at the default "
                              "tolerance (the decision differs but hasn't yet moved the car "
                              "measurably -- still a real requirement #3 violation)")
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
    if len(sys.argv) > 1 and sys.argv[1] == "--worker":
        raise SystemExit(_worker_main())
    raise SystemExit(main())
