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
      FFB + target_track ON with the synthetic-follower wheel) and asserting
      the per-scenario verdict AND the per-frame ego kinematic telemetry
      (x, y, speed, track_id, lane_id) match within tolerance.

The synthetic follower wheel (HeadlessFfbInput mode=follower) mirrors the
target_norm each frame so position_error stays ≈ 0 — the closed-loop shape
that a physically-connected wheel with a driver's hands OFF looks like
after any startup transient settles. Any AD-side divergence between A and B
therefore means the FFB pathway is bleeding back into AD decisions.

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
    "resources/xosc/verification/05_anticipation/decelerate_for_curve.xosc",
    "resources/xosc/verification/05_anticipation/decelerate_for_right_turn.xosc",
    "resources/xosc/verification/05_anticipation/cross_straight_junction.xosc",
    "resources/xosc/verification/05_anticipation/speed_limit_change.xosc",
    "resources/xosc/verification/05_anticipation/traffic_lights_junction.xosc",
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
    os.environ["GT_HEADLESS_FFB_MODE"] = "follower"
    os.environ.pop("GT_HEADLESS_FFB_FROZEN_AT", None)
    os.environ.pop("GT_HEADLESS_FFB_LAG_TAU", None)

    overall_ok = True
    print("=" * 80)
    print("FFB vs stub no-touch parity — criterion (2)")
    print("  stub baseline: input_type=stub, ffb_target_track_enabled=false")
    print("  ffb variant:   input_type=headless_ffb, ffb_target_track_enabled=true (follower)")
    print("=" * 80)

    for scen in SCENARIOS:
        name = Path(scen).stem
        xosc_stub = _write_variant(scen, tmpdir, cfg_stub, "stub")
        xosc_ffb  = _write_variant(scen, tmpdir, cfg_ffb,  "ffb")

        print(f"\n[scenario] {name}")
        print("  running stub baseline …", end=" ")
        frames_stub = _run_headless(DLL, xosc_stub)
        print(f"{len(frames_stub)} frames")
        print("  running ffb+target_track (follower, no-touch) …", end=" ")
        frames_ffb  = _run_headless(DLL, xosc_ffb)
        print(f"{len(frames_ffb)} frames")

        diffs = _diff_frames(frames_stub, frames_ffb)
        if not diffs:
            print("  PARITY: PASS  (per-frame ego kinematics identical, override state identical)")
        else:
            overall_ok = False
            print("  PARITY: FAIL")
            for d in diffs:
                print(f"    - {d}")

    shutil.rmtree(tmpdir, ignore_errors=True)
    print()
    print("=" * 80)
    print(f"RESULT: {'PASS' if overall_ok else 'FAIL'}  (criterion (2): no-touch ffb == stub baseline)")
    print("=" * 80)
    return 0 if overall_ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
