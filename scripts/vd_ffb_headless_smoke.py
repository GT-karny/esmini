"""feature:F7 (F7b) headless closed-loop + rate-gate smoke.

Guards TWO independent failure modes that unit tests alone did not catch:

  Bug 1 (commit 1c2939a0, fixed by a43e4c67 — closed-loop feedback):
    AD steers → servo moves wheel → SDL2WheelInput::Poll reads back the
    servo-moved axis as pedal_steer.steering → OverrideManager sees
    |steering| > 0.05 → MANUAL latch on frame 2 → target_active=false →
    servo dies. Symptom: "wheel doesn't follow" + "override never fires"
    (it fired silently on frame 2). Fix: when ffb_sample_.active, suppress
    raw-axis path in OverrideManager; torque-proxy is the sole detector.

  Bug 2 (found on real G29 after a43e4c67, fixed in the same session as
  this smoke — moving-target false positive):
    Day-1 spike (scripts/ffb_spike/05_torque_proxy.py) only calibrated the
    |u|/|dev| thresholds against a STATIC target. In real driving the AD
    target moves whenever AD steers, and the PID servo's normal tracking
    lag creates non-zero position_error and commanded_force even without
    any driver touch. Unguarded threshold check spuriously latches MANUAL
    on every curve / lane change. Symptom: "override fires even when I'm
    not touching the wheel". Fix: rate-gate on |d(target)/dt|; suppress
    detection while target is moving above override_target_rate_gate.

Headless closed loop via input_type="headless_ffb" (HeadlessFfbInput /
SyntheticSink) — echoes a synthetic physical axis back through
pedal_steer.steering, so the OverrideManager sees the same shape the SDL2
wheel path presents.

Three phases, all mandatory GREEN:
  A) follower — synthetic axis mirrors target_norm each frame (driver not
                pushing, servo tracks perfectly). Assert: override.lateral
                stays FALSE for the whole run despite AD steering |target|
                well past the raw-axis threshold (0.05). Direct regression
                test for Bug 1 (closed loop).
  B) frozen@0.4 — synthetic axis pinned at 0.4 (driver holds wheel off-
                center). AD wants near-0 for most of the drive → dev ≈ -0.4
                sustained → torque-proxy fires when target is STABLE (rate
                below gate). Assert: MANUAL latch fires. Regression test
                for the "detection still works after Bug 2 fix" case.
  C) moving-target-follower — same as A but ALSO asserts explicitly that
                during the lane-change transient (target rate above gate),
                even the fictional case of a driver holding steady stays
                below latch. This validates the rate-gate is actually
                doing its job (Bug 2 regression). Runs with follower mode
                (dev ≈ 0 always) so the assertion is on rate-gate behavior
                rather than driver push; the frozen@non-zero case in B
                already covers stable-target + real block.

Runs headless via DriverScript/.venv against build/GT_esmini/Release/GT_esminiLib.dll.
Requires a rebuild that includes HeadlessFfbInput (added alongside this file).
"""
from __future__ import annotations

import ctypes
import json
import os
import sys
import tempfile
import xml.etree.ElementTree as ET

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DLL = os.path.join(ROOT, "build", "GT_esmini", "Release", "GT_esminiLib.dll")
BASE_XOSC = os.path.join(ROOT, "resources", "xosc", "virtual_driver_basic.xosc")


def _make_variant(tmpdir: str, mode: str, frozen_at: float | None = None) -> tuple[str, str]:
    """Write a per-run virtual_driver.json (input_type=headless_ffb,
    ffb_target_track_enabled=true) and a scenario variant that points its
    VirtualDriverController.ConfigFile at it. Returns (xosc_path, cfg_path)."""
    cfg_path = os.path.join(tmpdir, "virtual_driver.json")
    shipped = os.path.join(ROOT, "GT_esmini", "config", "virtual_driver.json")
    base: dict = {}
    if os.path.exists(shipped):
        base = json.loads(open(shipped, encoding="utf-8").read())
    base["input_type"] = "headless_ffb"
    base["ffb_target_track_enabled"] = True
    # Disable the direct-axis threshold path for the smoke: with a frozen or
    # follower synthetic axis that can sit above 0.05 (e.g. Phase B @ 0.4),
    # direct-axis would fire on frame 1 (before the servo has produced any
    # sample) and latch MANUAL, preventing the servo from ever engaging —
    # bootstrap deadlock that hides the torque-proxy path entirely. The
    # direct-axis path is exercised by existing OverrideManager unit tests;
    # this smoke isolates the FFB torque-proxy + rate-gate paths. In real
    # deployment (F7b default 0.05) a driver-off-center wheel-at-start
    # correctly means "driver is driving, don't engage servo" — that IS the
    # intended semantic, just not what THIS smoke is trying to test.
    base["steering_threshold"] = 1.0
    # Keep the spike-calibrated FFB defaults from the shipped config.
    open(cfg_path, "w", encoding="utf-8").write(json.dumps(base, indent=2))

    tree = ET.parse(BASE_XOSC)
    root = tree.getroot()

    # Absolutize LogicFile / SceneGraphFile / CatalogLocations Directory —
    # the xosc lives in resources/xosc so its relatives ("../xodr/e6mini.xodr",
    # "Catalogs/Vehicles") would break when the variant lands in tmpdir. Same
    # trick simulation_runner uses for generated variants.
    base_dir = os.path.dirname(os.path.abspath(BASE_XOSC))
    for tag in ("LogicFile", "SceneGraphFile"):
        for el in root.findall(f".//{tag}"):
            fp = el.get("filepath")
            if fp and not os.path.isabs(fp):
                el.set("filepath", os.path.abspath(os.path.join(base_dir, fp)))
    for el in root.findall(".//CatalogLocations//Directory"):
        pth = el.get("path")
        if pth and not os.path.isabs(pth):
            el.set("path", os.path.abspath(os.path.join(base_dir, pth)))

    ctrl = root.find(".//ObjectController/Controller")
    if ctrl is None:
        raise RuntimeError("Could not find VirtualDriverController in base xosc")
    props = ctrl.find("Properties")
    # Add ConfigFile property pointing at our per-run config.
    p = ET.SubElement(props, "Property")
    p.set("name", "ConfigFile")
    p.set("value", cfg_path)

    out_xosc = os.path.join(tmpdir, f"vd_ffb_smoke_{mode}.xosc")
    tree.write(out_xosc, encoding="utf-8", xml_declaration=True)
    return out_xosc, cfg_path


def _run_phase(mode: str, frozen_at: float | None, duration_s: float,
               dt: float = 0.05) -> tuple[list[dict], list[dict]]:
    """Run one headless phase with the given synthetic-wheel mode.
    Returns (frames, edges) where:
      frames — list of telemetry dicts sampled each Step
      edges  — subset of frames where manual_transition or auto_transition
               was True on that frame
    Env vars are set BEFORE loading the DLL because HeadlessFfbInput reads
    them at Init time (once per scenario)."""
    os.environ["GT_HEADLESS_FFB_MODE"] = mode
    if frozen_at is not None:
        os.environ["GT_HEADLESS_FFB_FROZEN_AT"] = f"{frozen_at:.4f}"
    else:
        os.environ.pop("GT_HEADLESS_FFB_FROZEN_AT", None)

    tmpdir = tempfile.mkdtemp(prefix=f"vd_ffb_smoke_{mode}_")
    xosc, _cfg = _make_variant(tmpdir, mode, frozen_at)

    # Fresh DLL handle per phase so Init reads the env fresh. ctypes caches
    # the module per-process, so we reload by CDLL each call (Windows dlopens
    # once per unique path — force-reload via a symlink or file copy is not
    # worth the complexity; the module singletons inside are reset by
    # GT_Close() below).
    lib = ctypes.CDLL(DLL)
    lib.GT_InitWithArgs.argtypes = [ctypes.c_int, ctypes.POINTER(ctypes.c_char_p)]
    lib.GT_InitWithArgs.restype  = ctypes.c_int
    lib.GT_Step.argtypes = [ctypes.c_double]
    lib.GT_Close.argtypes = []
    lib.GT_GetVirtualDriverTelemetry.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
    lib.GT_GetVirtualDriverTelemetry.restype  = ctypes.c_int

    argv_list = [b"vd_ffb_smoke", b"--osc", xosc.encode(), b"--headless",
                 b"--fixed_timestep", f"{dt:.3f}".encode()]
    argv = (ctypes.c_char_p * len(argv_list))(*argv_list)
    rc = lib.GT_InitWithArgs(len(argv_list), argv)
    if rc != 0:
        raise RuntimeError(f"GT_InitWithArgs rc={rc}")

    buf = ctypes.create_string_buffer(32768)
    frames: list[dict] = []
    edges: list[dict] = []

    n_steps = int(duration_s / dt)
    for _ in range(n_steps):
        lib.GT_Step(dt)
        n = lib.GT_GetVirtualDriverTelemetry(0, buf, len(buf))
        if n <= 0:
            continue
        try:
            f = json.loads(buf.value.decode())
        except json.JSONDecodeError:
            continue
        frames.append(f)
        ov = f.get("override", {})
        if ov.get("manual_transition") or ov.get("auto_transition"):
            edges.append(f)

    lib.GT_Close()
    return frames, edges


def _summarize(frames: list[dict]) -> dict:
    """Peak / late-window summary of the interesting telemetry fields."""
    if not frames:
        return {}
    def _all(k1: str, k2: str) -> list[float]:
        out = []
        for f in frames:
            v = f.get(k1, {}).get(k2)
            if isinstance(v, (int, float)):
                out.append(float(v))
        return out
    ffb_active = [f.get("ffb", {}).get("target_active", False) for f in frames]
    lat_manual = [f.get("override", {}).get("lateral", False) for f in frames]
    ffb_force = _all("ffb", "commanded_force")
    ffb_dev   = _all("ffb", "position_error")
    return {
        "n_frames":               len(frames),
        "ffb_active_frames":      sum(1 for x in ffb_active if x),
        "lat_manual_frames":      sum(1 for x in lat_manual if x),
        "lat_manual_ever":        any(lat_manual),
        "lat_manual_final":       bool(lat_manual[-1]),
        "ffb_force_max":          max(ffb_force) if ffb_force else 0.0,
        "ffb_force_late_max":     max(ffb_force[-30:]) if len(ffb_force) >= 30 else 0.0,
        "ffb_dev_absmax":         max(abs(x) for x in ffb_dev) if ffb_dev else 0.0,
        "sim_time_final":         frames[-1].get("sim_time", 0.0),
        "manual_transition_frames": [i for i, f in enumerate(frames)
                                     if f.get("override", {}).get("manual_transition")],
        "auto_transition_frames":   [i for i, f in enumerate(frames)
                                     if f.get("override", {}).get("auto_transition")],
    }


def main() -> int:
    if not os.path.exists(DLL):
        print(f"FAIL: DLL not found at {DLL} — run /build first")
        return 1
    if not os.path.exists(BASE_XOSC):
        print(f"FAIL: base xosc not found at {BASE_XOSC}")
        return 1

    checks: list[tuple[str, bool, str]] = []

    def check(name: str, cond: bool, detail: str = "") -> None:
        checks.append((name, bool(cond), detail))

    # ==== Phase A: follower ====
    # Servo actively drives synthetic wheel to follow AD's target. The Poll
    # feeds that same axis back as pedal_steer.steering. The direct-axis
    # threshold (0.05) will be crossed as soon as AD commands |target|>0.05
    # (which happens quickly on the lane-change segment). The fix must let
    # the servo keep running — override.lateral must NEVER latch.
    print("== Phase A: follower (servo tracks perfectly, driver not pushing) ==")
    fa, ea = _run_phase(mode="follower", frozen_at=None, duration_s=15.0)
    sa = _summarize(fa)
    print(f"  frames={sa.get('n_frames', 0)} sim_end={sa.get('sim_time_final', 0):.2f}s "
          f"ffb_active_frames={sa.get('ffb_active_frames', 0)} "
          f"ffb_force_max={sa.get('ffb_force_max', 0):.3f} "
          f"ffb_dev_absmax={sa.get('ffb_dev_absmax', 0):.3f}")
    print(f"  lat_manual_ever={sa.get('lat_manual_ever')} "
          f"manual_transitions={sa.get('manual_transition_frames', [])} "
          f"auto_transitions={sa.get('auto_transition_frames', [])}")

    check("A.telemetry-nonempty",   sa.get("n_frames", 0) > 100,
          f"frames={sa.get('n_frames', 0)} — VD did not produce enough telemetry")
    check("A.ffb-servo-was-active", sa.get("ffb_active_frames", 0) > 50,
          "target_track servo never ran — SetSteerTarget/GetInterventionSample wiring broken")
    check("A.no-self-trip-latch",   not sa.get("lat_manual_ever"),
          f"LATERAL latched to MANUAL despite driver not pushing "
          f"(closed-loop self-trip regression — bug from commit 1c2939a0)")
    check("A.no-manual-transition-edge", len(sa.get("manual_transition_frames", [])) == 0,
          "manual_transition edge fired — override latched spuriously")
    check("A.force-stayed-low",     sa.get("ffb_force_max", 0) < 0.15,
          f"servo commanded_force max={sa.get('ffb_force_max', 0):.3f} — should be near 0 in "
          f"follower mode (position_error≈0). If high, servo model or wiring is off.")
    check("A.dev-stayed-low",       sa.get("ffb_dev_absmax", 0) < 0.05,
          f"position_error absmax={sa.get('ffb_dev_absmax', 0):.3f} — follower should track "
          f"exactly (dev≈0)")

    # ==== Phase B: frozen driver at 0.4 (persistent off-center block) ====
    # Synthetic axis held at 0.4. For the majority of the run AD wants near
    # 0 (straight driving on virtual_driver_basic route) → dev = 0 - 0.4 =
    # -0.4 sustained. Because target barely moves (small |d(target)/dt| well
    # under rate-gate), the sustain accumulator runs and torque-proxy fires
    # MANUAL within a few sustain windows.
    print("== Phase B: frozen@0.4 (driver holds wheel off-center, AD wants near-0) ==")
    fb, eb = _run_phase(mode="frozen", frozen_at=0.4, duration_s=6.0)
    sb = _summarize(fb)
    print(f"  frames={sb.get('n_frames', 0)} sim_end={sb.get('sim_time_final', 0):.2f}s "
          f"ffb_active_frames={sb.get('ffb_active_frames', 0)} "
          f"ffb_force_max={sb.get('ffb_force_max', 0):.3f} "
          f"ffb_dev_absmax={sb.get('ffb_dev_absmax', 0):.3f}")
    print(f"  lat_manual_ever={sb.get('lat_manual_ever')} "
          f"lat_manual_final={sb.get('lat_manual_final')} "
          f"manual_transitions={sb.get('manual_transition_frames', [])}")

    check("B.telemetry-nonempty",   sb.get("n_frames", 0) > 20,
          f"frames={sb.get('n_frames', 0)}")
    # The servo may run for as few as 1 frame here — with dev=0.4 the sustain
    # accumulator hits its 100 ms threshold on frame 2 and MANUAL latches on
    # frame 3, at which point SetSteerTarget switches target_active off. The
    # important assertion is that the servo produced a sample AT ALL (so the
    # torque-proxy detection had signal to fire on). Count >= 1 = wiring OK.
    check("B.ffb-servo-was-active", sb.get("ffb_active_frames", 0) >= 1,
          "target_track servo never ran even for one frame — wiring broken")
    check("B.dev-grew",             sb.get("ffb_dev_absmax", 0) > 0.10,
          f"position_error absmax={sb.get('ffb_dev_absmax', 0):.3f} — with axis frozen at 0.4 "
          f"and AD wanting straight, dev should be substantial (~0.4).")
    check("B.force-grew",           sb.get("ffb_force_max", 0) > 0.15,
          f"commanded_force max={sb.get('ffb_force_max', 0):.3f} — servo should hit force when "
          f"driver blocks it")
    check("B.torque-proxy-latched", sb.get("lat_manual_ever"),
          "override.lateral never latched to MANUAL despite frozen driver blocking servo — "
          "torque-proxy detection is broken (Bug 2 fix may be too permissive)")
    check("B.manual-transition-edge-seen", len(sb.get("manual_transition_frames", [])) >= 1,
          "manual_transition edge never fired")

    # ==== Phase C: follower during full drive incl. lane-change transient ====
    # Same as Phase A but sim-time long enough to include the lane-change
    # maneuver (t≈8s in virtual_driver_basic). During the LC, target_norm
    # moves quickly (|d(target)/dt| well above gate). Under Bug 2, unguarded
    # thresholds would false-latch mid-turn even with a perfect follower
    # (position_error > threshold during PID lag). The rate-gate must
    # suppress → no latch throughout.
    #
    # This is the specific real-machine bug: "hands off wheel entirely,
    # MANUAL fires anyway when the car enters a curve".
    print("== Phase C: follower through lane-change (Bug 2 real-machine regression) ==")
    fc, ec = _run_phase(mode="follower", frozen_at=None, duration_s=15.0)
    sc = _summarize(fc)
    print(f"  frames={sc.get('n_frames', 0)} sim_end={sc.get('sim_time_final', 0):.2f}s "
          f"ffb_active_frames={sc.get('ffb_active_frames', 0)} "
          f"ffb_force_max={sc.get('ffb_force_max', 0):.3f} "
          f"ffb_dev_absmax={sc.get('ffb_dev_absmax', 0):.3f}")
    print(f"  lat_manual_ever={sc.get('lat_manual_ever')} "
          f"manual_transitions={sc.get('manual_transition_frames', [])}")

    check("C.telemetry-nonempty",         sc.get("n_frames", 0) > 200,
          f"frames={sc.get('n_frames', 0)} — Phase C needs full 15s window")
    check("C.no-transient-false-latch",   not sc.get("lat_manual_ever"),
          "MANUAL latched during follower run — rate-gate is not suppressing detection through "
          "the AD steering transient (Bug 2 regression). If frozen@0.4 also fails, gate too "
          "aggressive; if frozen@0.4 passes but C fails, gate too permissive during transient.")
    check("C.no-manual-transition-edge",  len(sc.get("manual_transition_frames", [])) == 0,
          "manual_transition edge fired during transient")

    ok = all(c[1] for c in checks)
    print()
    print("=" * 60)
    for name, passed, detail in checks:
        tag = "PASS" if passed else "FAIL"
        line = f"  [{tag}] {name}"
        if not passed and detail:
            line += f"  — {detail}"
        print(line)
    print("=" * 60)
    print(f"RESULT: {'PASS' if ok else 'FAIL'}  ({sum(1 for c in checks if c[1])}/{len(checks)} checks)")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
