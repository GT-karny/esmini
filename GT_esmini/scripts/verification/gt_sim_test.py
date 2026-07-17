#!/usr/bin/env python
"""gt_sim_test - VirtualDriver verification CLI (Step 3 core).

Runs a scenario in-process via the GT C-API, records per-frame VirtualDriver
telemetry, and supports numeric comparison against a Default baseline and
declarative assertion against an expectations.yaml.

Subcommands
-----------
  run     <scenario.xosc> --out results/<id>/
              -> telemetry.jsonl + meta.json + snapshots/*.png
  compare <run_dir> <baseline.osi | baselines/<name>/>
              -> compare.json   (ego XY / speed RMSE vs Default baseline)
  assert  <run_dir> --expectations <expectations.yaml>
              -> verdict.json    (declarative must[] events: pass/fail/skip)

The whole loop (change -> build -> run -> read verdict.json) is what Claude Code
drives for self-verification. Run via DriverScript/.venv (system Python is NG).

Examples
--------
  py gt_sim_test.py run resources/xosc/virtual_driver_basic.xosc --out results/vd_basic
  py gt_sim_test.py compare results/vd_basic results/baselines/straight_constant_speed
  py gt_sim_test.py assert  results/vd_basic --expectations <scenario>.expectations.yaml
"""
from __future__ import annotations

import argparse
import json
import math
import socket
import struct
import subprocess
import sys
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "scripts"))  # osi3 bindings

from gt_lib import GtLib  # noqa: E402  (local module, same dir)

# Verification math (trajectory metrics + expectation matchers) lives in the web
# backend's vd_metrics as the single source of truth; the CLI imports it instead of
# carrying a second copy (audit F5 / WEB-4). This CLI layer keeps only run/print/
# progress orchestration + snapshot/report rendering, and thin compare/assert
# wrappers that add stderr summaries. vd_metrics is config-free (stdlib + lazy
# yaml/osi3), so importing it flat here is safe in the dev tree and the package.
sys.path.insert(0, str(REPO_ROOT / "GT_esmini" / "web" / "backend" / "services"))
import vd_metrics as _vd  # noqa: E402
from vd_metrics import load_telemetry as _load_telemetry, _speed_accel_jerk  # noqa: E402

# OSI groundtruth capture (opt-in). GT_Sim/GT_esminiLib emit OSI over UDP with
# --osi (default port 48198), reassembled with the same counter/size framing as
# udp_osi_common.OSIReceiver. We capture in-process (loopback) so lead-vehicle
# distance (THW) and live signal phase are available to the matchers without a
# new C-API. Kept self-contained (no backend import) so the packaged build works.
OSI_UDP_PORT = 48198
OSI_BUFFER_SIZE = 8208  # max OSI UDP payload + 8-byte header (contract with esmini)

# osi3 enum -> string maps (mirror of api/osi_stream.py, kept local on purpose).
_TL_COLOR_MAP = {0: "unknown", 1: "other", 2: "red", 3: "yellow", 4: "green", 5: "blue", 6: "white"}
_TL_MODE_MAP = {0: "unknown", 1: "other", 2: "off", 3: "constant", 4: "flashing", 5: "counting"}
_MOVING_TYPE_MAP = {0: "unknown", 1: "other", 2: "vehicle", 3: "pedestrian", 4: "animal"}



# ---------------------------------------------------------------------------
# run
# ---------------------------------------------------------------------------

def _git_commit() -> str:
    try:
        out = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=str(REPO_ROOT),
                             capture_output=True, text=True, timeout=10)
        return out.stdout.strip() if out.returncode == 0 else ""
    except Exception:
        return ""


class _OsiCapture:
    """In-process OSI GroundTruth receiver. Binds the UDP port before init, then
    `drain()` is called once per step to reassemble all buffered packets and
    return the *latest* complete GroundTruth bytes (or None if none completed).

    The DLL sends OSI synchronously inside GT_Step on the same thread (loopback),
    so by the time GT_Step returns, that frame's packets are already in the OS
    socket buffer and a non-blocking drain yields complete frames."""

    def __init__(self, port: int = OSI_UDP_PORT):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1 << 20)
        self.sock.bind(("127.0.0.1", port))
        self.sock.setblocking(False)
        self._complete = b""
        self._next_index = 1

    def drain(self) -> bytes | None:
        last_raw: bytes | None = None
        while True:
            try:
                msg, _ = self.sock.recvfrom(OSI_BUFFER_SIZE)
            except (BlockingIOError, socket.timeout):
                break
            except OSError:
                break
            if len(msg) < 8:
                continue
            counter, _size = struct.unpack("iI", msg[:8])
            frame = msg[8:]
            if counter == 1:  # new message
                self._complete = b""
                self._next_index = 1
            if counter == 1 or abs(counter) == self._next_index:
                self._complete += frame
                self._next_index += 1
                if counter < 0:  # negative counter = final packet
                    last_raw = self._complete
                    self._complete = b""
                    self._next_index = 1
            else:
                self._next_index = 1  # out of sync, reset
        return last_raw

    def close(self) -> None:
        try:
            self.sock.close()
        except OSError:
            pass


def _gt_to_scene(raw: bytes, _gt_cache=[]) -> dict | None:
    """Parse a raw GroundTruth frame into a lightweight scene dict:
    {objects:[{id,name,x,y,h,speed,length,width}], traffic_lights:[{id,x,y,h,color,mode}]}.
    Reuses one GroundTruth message object across calls (parse churn)."""
    from osi3.osi_groundtruth_pb2 import GroundTruth

    if not _gt_cache:
        _gt_cache.append(GroundTruth())
    gt = _gt_cache[0]
    gt.Clear()
    try:
        gt.ParseFromString(raw)
    except Exception:
        return None

    host_id = gt.host_vehicle_id.value if gt.HasField("host_vehicle_id") else None
    objects = []
    for o in gt.moving_object:
        pos = o.base.position
        vel = o.base.velocity
        dim = o.base.dimension
        name = ""
        for ref in o.source_reference:
            if ref.type == "net.asam.openscenario":
                for ident in ref.identifier:
                    if ident.startswith("entity_name:"):
                        name = ident[len("entity_name:"):]
        # When OSI reports no body extents (dim <= 0) we emit the 4.0x2.0 m default
        # so the geometry maths still run, but flag it: an OBB anti-collision gate
        # must NOT report a clean measured pass on fabricated dimensions (a larger
        # real vehicle could overlap where the default footprint clears).
        dims_fallback = dim.length <= 0 or dim.width <= 0
        obj = {
            "id": o.id.value,
            "name": name,
            "x": round(pos.x, 3),
            "y": round(pos.y, 3),
            "h": round(o.base.orientation.yaw, 4),
            "speed": round(math.sqrt(vel.x ** 2 + vel.y ** 2 + vel.z ** 2), 3),
            "length": round(dim.length, 2) if dim.length > 0 else 4.0,
            "width": round(dim.width, 2) if dim.width > 0 else 2.0,
            "is_host": (host_id is not None and o.id.value == host_id),
            "type": _MOVING_TYPE_MAP.get(o.type, "unknown"),
        }
        if dims_fallback:
            obj["dims_fallback"] = True
        objects.append(obj)

    traffic_lights = []
    for tl in gt.traffic_light:
        pos = tl.base.position
        cls = tl.classification
        traffic_lights.append({
            "id": tl.id.value,
            "x": round(pos.x, 3),
            "y": round(pos.y, 3),
            "h": round(tl.base.orientation.yaw, 4),
            "color": _TL_COLOR_MAP.get(cls.color, "unknown"),
            "mode": _TL_MODE_MAP.get(cls.mode, "unknown"),
        })

    return {"objects": objects, "traffic_lights": traffic_lights}


def run(scenario: Path, out_dir: Path, dt: float, max_time: float,
        snapshots: int, dll: Path | None, capture_osi: bool = False,
        osi_port: int = OSI_UDP_PORT) -> dict:
    out_dir.mkdir(parents=True, exist_ok=True)
    jsonl_path = out_dir / "telemetry.jsonl"

    args = ["--osc", str(scenario), "--headless", "--fixed_timestep", str(dt)]
    osi_cap: _OsiCapture | None = None
    if capture_osi:
        # Bind before init so the very first emitted frame isn't lost.
        osi_cap = _OsiCapture(osi_port)
    lib = GtLib(dll) if dll else GtLib()

    frames: list[dict] = []
    grace = 0
    GRACE_MAX = int(round(1.0 / dt))  # ~1s of consecutive "no telemetry" = ended
    seen_valid = False
    last_scene: dict | None = None

    try:
        with lib, open(jsonl_path, "w", encoding="utf-8") as f:
            rc = lib.init_with_args(args)
            if rc != 0:
                cause = lib.get_last_error()
                raise RuntimeError(f"GT_InitWithArgs failed (rc={rc}) for {scenario}"
                                   + (f": {cause}" if cause else ""))
            if osi_cap is not None:
                # GT_InitWithArgs doesn't open the OSI socket (only GT_Sim.exe does);
                # open it now so GT_Step emits groundtruth to 127.0.0.1:48198.
                lib.open_osi_socket("127.0.0.1")

            n_steps = int(round(max_time / dt))
            for _ in range(n_steps):
                lib.step(dt)
                if osi_cap is not None:
                    raw = osi_cap.drain()
                    if raw is not None:
                        scene = _gt_to_scene(raw)
                        if scene is not None:
                            last_scene = scene
                tel = lib.get_vd_telemetry(-1)
                if tel is None:
                    if seen_valid:
                        grace += 1
                        if grace >= GRACE_MAX:
                            break  # controller deactivated -> scenario ended
                    continue
                seen_valid = True
                grace = 0
                if osi_cap is not None:
                    tel["scene"] = last_scene or {"objects": [], "traffic_lights": []}
                f.write(json.dumps(tel, separators=(",", ":")) + "\n")
                frames.append(tel)
    finally:
        if osi_cap is not None:
            osi_cap.close()

    duration = frames[-1]["sim_time"] if frames else 0.0
    meta = {
        "scenario": str(scenario.relative_to(REPO_ROOT)) if scenario.is_absolute()
        and str(scenario).startswith(str(REPO_ROOT)) else str(scenario),
        "controller": "VirtualDriver",
        "dt": dt,
        "frames": len(frames),
        "sim_duration_s": round(duration, 3),
        "osi": bool(capture_osi),
        "commit": _git_commit(),
    }
    (out_dir / "meta.json").write_text(json.dumps(meta, indent=2), encoding="utf-8")

    if frames:
        _render_snapshots(frames, out_dir / "snapshots", max(1, snapshots))

    print(f"[run] {scenario.name}: {len(frames)} frames, {duration:.1f}s -> {jsonl_path}",
          file=sys.stderr)
    if not frames:
        print("[run] WARNING: no VirtualDriver telemetry captured - does the scenario "
              "assign a VirtualDriverController to the ego?", file=sys.stderr)
    return meta


def _render_snapshots(frames: list[dict], snap_dir: Path, count: int) -> None:
    """Top-down keyframes: ego path so far + the ego's short-horizon preview."""
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    snap_dir.mkdir(parents=True, exist_ok=True)
    xs = [fr["ego"]["x"] for fr in frames]
    ys = [fr["ego"]["y"] for fr in frames]
    pad = 8.0
    xlim = (min(xs) - pad, max(xs) + pad)
    ylim = (min(ys) - pad, max(ys) + pad)

    idxs = [int(round(i * (len(frames) - 1) / max(1, count - 1))) for i in range(count)] \
        if count > 1 else [len(frames) - 1]

    for k, idx in enumerate(idxs):
        fr = frames[idx]
        fig, ax = plt.subplots(figsize=(5, 6))
        ax.plot(xs[:idx + 1], ys[:idx + 1], "-", color="#7B88E8", lw=1.2, label="ego path")
        ego = fr["ego"]
        ax.plot(ego["x"], ego["y"], "o", color="#9B84E8", ms=8, label="ego")
        prev = fr.get("preview", {})
        pts = prev.get("points", [])
        if pts:
            ax.plot([p["x"] for p in pts], [p["y"] for p in pts], ".-",
                    color="#4FD18B", ms=2, lw=0.8, label="preview")
        ax.set_xlim(*xlim); ax.set_ylim(*ylim); ax.set_aspect("equal", "box")
        ax.set_title(f"t={ego.get('speed', 0):.1f} m/s  sim_t={fr['sim_time']:.1f}s")
        ax.legend(loc="upper right", fontsize=7); ax.grid(alpha=0.2)
        fig.tight_layout()
        fig.savefig(snap_dir / f"frame_{k:02d}_t{fr['sim_time']:.1f}.png", dpi=90)
        plt.close(fig)


# ---------------------------------------------------------------------------
# compare
# ---------------------------------------------------------------------------

def compare(run_dir: Path, baseline: Path, grid_dt: float = 0.1) -> dict:
    """Thin CLI wrapper over vd_metrics.compare (adds a stderr summary line)."""
    result = _vd.compare(run_dir, baseline, grid_dt)
    print(f"[compare] xy_rmse={result['xy_rmse_m']}m  speed_rmse={result['speed_rmse_mps']}m/s  "
          f"max_dev={result['xy_max_dev_m']}m -> {run_dir / 'compare.json'} (+baseline_track.json)",
          file=sys.stderr)
    return result


# ---------------------------------------------------------------------------
# assert (expectations.yaml)
# ---------------------------------------------------------------------------

def assert_expectations(run_dir: Path, expectations: Path) -> dict:
    """Thin CLI wrapper over vd_metrics.assert_expectations (adds stderr prints)."""
    verdict = _vd.assert_expectations(run_dir, expectations)
    s = verdict["summary"]
    print(f"[assert] overall={verdict['overall']}  pass={s['pass']} fail={s['fail']} "
          f"skip={s['skip']} -> {run_dir / 'verdict.json'}", file=sys.stderr)
    for r in verdict["results"]:
        print(f"   [{r['status']:4}] {r['event']}: {r['detail']}", file=sys.stderr)
    return verdict


# ---------------------------------------------------------------------------
# deceleration-profile report
# ---------------------------------------------------------------------------

def _render_decel_report(frames: list[dict], out_path: Path, expectation: dict | None = None) -> None:
    """Speed-vs-s (primary) + speed/accel-vs-t (twin) chart for the mid/long
    deceleration case. Overlays decel onset, the landmark, and the target band
    pulled from the expectations spec. Written as a PNG that Claude can Read."""
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    if not frames:
        return

    # Mirror the deceleration_profile_smooth matcher: same smoothing window and
    # the same decel-phase scoping, so the PNG annotation agrees with the verdict.
    sw, road_id, landmark_s = 5, None, None
    target_speed = target_tol = None
    if isinstance(expectation, dict):
        for m in expectation.get("must", []):
            if m.get("event") == "deceleration_profile_smooth":
                sw = int(m.get("smooth_window", 5))
                road_id = m.get("road_id")
            if landmark_s is None and m.get("landmark_s") is not None:
                landmark_s = float(m["landmark_s"])
            if target_speed is None and m.get("target_speed") is not None:
                target_speed = float(m["target_speed"])
                target_tol = float(m.get("target_tol", m.get("tolerance", 0.5)))

    prof = _speed_accel_jerk(frames, sw)
    t, v, s, a, j = prof["t"], prof["v"], prof["s"], prof["a"], prof["j"]
    v_raw = [fr["ego"]["speed"] for fr in frames]
    n = len(frames)

    # decel-phase window [onset -> landmark passage], same as the matcher
    lm = None
    if landmark_s is not None:
        lm = next((i for i in range(n)
                   if (road_id is None or frames[i]["ego"].get("track") == road_id)
                   and s[i] >= landmark_s), None)
    onset = next((i for i in range(n) if a[i] < -0.3 and (lm is None or i < lm)), None)
    if onset is not None and lm is not None and lm - onset >= 2:
        seg = range(onset, lm + 1)
        win_label = f"decel phase t{t[onset]:.1f}-{t[lm]:.1f}s"
    else:
        seg = range(n)
        win_label = "full run"
    max_decel = -min(a[i] for i in seg)
    max_jerk = max(abs(j[i]) for i in seg)

    fig, (ax0, ax1) = plt.subplots(2, 1, figsize=(7, 6))

    ax0.plot(s, v_raw, color="#9AA7FF", lw=0.8, alpha=0.6, label="speed (raw)")
    ax0.plot(s, v, color="#3B5BDB", lw=1.4, label="speed (smoothed)")
    if target_speed is not None:
        ax0.axhspan(target_speed - target_tol, target_speed + target_tol,
                    color="#4FD18B", alpha=0.18, label=f"target {target_speed:g}±{target_tol:g}")
    if landmark_s is not None:
        ax0.axvline(landmark_s, color="#E8590C", ls="--", lw=1.2, label=f"landmark s={landmark_s:g}")
    if onset is not None and lm is not None and lm > onset:
        ax0.axvspan(s[onset], s[lm], color="#3B5BDB", alpha=0.07, label="decel phase")
    if onset is not None:
        ax0.plot(s[onset], v[onset], "v", color="#E03131", ms=9, label="decel onset")
    ax0.set_xlabel("route s [m]"); ax0.set_ylabel("speed [m/s]")
    ax0.set_title(f"Deceleration profile  ({win_label}: max decel={max_decel:.2f} m/s^2, "
                  f"max |jerk|={max_jerk:.2f} m/s^3)")
    ax0.grid(alpha=0.25); ax0.legend(fontsize=7, loc="best")

    ax1.plot(t, v, color="#3B5BDB", lw=1.2, label="speed [m/s]")
    ax1b = ax1.twinx()
    ax1b.plot(t, a, color="#E8590C", lw=1.0, alpha=0.8, label="accel [m/s^2]")
    ax1b.axhline(0, color="#aaaaaa", lw=0.6)
    ax1.set_xlabel("sim_time [s]"); ax1.set_ylabel("speed [m/s]"); ax1b.set_ylabel("accel [m/s^2]")
    ax1.grid(alpha=0.25)

    fig.tight_layout()
    out_path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(out_path, dpi=90)
    plt.close(fig)
    print(f"[report] -> {out_path}", file=sys.stderr)


# ---------------------------------------------------------------------------
# batch
# ---------------------------------------------------------------------------

def _resolve_repo(p) -> Path:
    """Resolve a manifest path: absolute as-is, else relative to the repo root."""
    p = Path(p)
    return p if p.is_absolute() else (REPO_ROOT / p)


# --- traffic-policy enablement (Phase 3) ------------------------------------
# The VirtualDriver traffic policies (3a lead / 3b traffic-light / 3c stop-yield)
# default OFF in config so Phase 1/2 stays unchanged; a scenario opts in via its
# ConfigFile. The in-process harness can't load an exe-relative config (it resolves
# against host python.exe), but ControllerVirtualDriver DOES honour an ABSOLUTE
# ConfigFile path. So to enable policies we generate a per-run config (base
# virtual_driver.json + the requested enable flags) and inject an absolute
# ConfigFile property into a temp copy of the scenario (road/scene paths
# absolutized so the temp can live in the run dir). Mirrors the web runner's
# _generate_virtual_driver_variant.
BASE_VD_CONFIG = REPO_ROOT / "GT_esmini" / "config" / "virtual_driver.json"
_POLICY_FLAG = {
    "lead": "policy_lead_enabled",
    "traffic_light": "policy_traffic_light_enabled",
    "stop_yield": "policy_stop_yield_enabled",
    "conflict": "policy_conflict_enabled",
    "crosswalk": "policy_crosswalk_enabled",
    # F3 (Phase 3e): unsignalised-junction right-of-way. Gates INSIDE the conflict
    # resolver, so enable it together with "conflict" (e.g. policies:[conflict,
    # junction_priority]).
    "junction_priority": "policy_junction_priority_enabled",
    # AEB phase 1: forward-collision emergency braking guardian (see AebSafety).
    # Independent of "lead" -- composes alongside it.
    "aeb": "policy_aeb_enabled",
}


def _write_policy_config(policies: list[str], out_path: Path) -> Path:
    """Write a per-run virtual_driver.json = base config + the requested policy
    enable flags set true. Unknown policy names raise (typos shouldn't silently
    run with everything off)."""
    base = json.loads(BASE_VD_CONFIG.read_text(encoding="utf-8"))
    for p in policies:
        flag = _POLICY_FLAG.get(p)
        if flag is None:
            raise ValueError(f"unknown policy '{p}' (want one of {sorted(_POLICY_FLAG)})")
        base[flag] = True
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(base, indent=2), encoding="utf-8")
    return out_path


def _prepare_policy_xosc(scenario: Path, run_dir: Path, config_path: Path) -> Path:
    """Write a temp copy of the scenario with (1) road/scene/catalog file paths
    absolutized (so it can live outside the original dir) and (2) an absolute
    ConfigFile property injected into the VirtualDriverController. Returns the
    temp path. Raises if the controller isn't found."""
    import xml.etree.ElementTree as ET

    tree = ET.parse(scenario)
    root = tree.getroot()
    base_dir = scenario.parent

    for tag, attr in (("LogicFile", "filepath"), ("SceneGraphFile", "filepath"),
                      ("Directory", "path")):
        for el in root.iter(tag):
            fp = el.get(attr)
            if fp and not Path(fp).is_absolute():
                el.set(attr, str((base_dir / fp).resolve()))

    injected = False
    for ctrl in root.iter("Controller"):
        if ctrl.get("name") == "VirtualDriverController":
            props = ctrl.find("Properties")
            if props is None:
                props = ET.SubElement(ctrl, "Properties")
            ET.SubElement(props, "Property", {"name": "ConfigFile", "value": str(config_path)})
            injected = True
    if not injected:
        raise RuntimeError(f"{scenario.name}: no VirtualDriverController to inject ConfigFile into")

    run_dir.mkdir(parents=True, exist_ok=True)
    out = run_dir / (scenario.stem + ".policyrun.xosc")
    tree.write(out, encoding="utf-8", xml_declaration=True)
    return out


def batch(manifest: Path, out_root: Path, dll: Path | None = None) -> dict:
    """Run a manifest of scenarios: for each, run() -> (compare if baseline) ->
    assert (+ optional decel report). Per-scenario failures are recorded as
    'error' and do not abort the batch. Writes batch_verdict.json + a
    Claude-readable batch_summary.md."""
    import yaml
    spec = yaml.safe_load(manifest.read_text(encoding="utf-8"))
    name = spec.get("name", manifest.stem)
    defaults = spec.get("defaults", {}) or {}
    dt = float(defaults.get("dt", 0.05))
    max_time = float(defaults.get("max_time", 60.0))
    snapshots = int(defaults.get("snapshots", 3))
    default_osi = bool(defaults.get("osi", False))
    osi_port = int(defaults.get("osi_port", OSI_UDP_PORT))
    out_root.mkdir(parents=True, exist_ok=True)

    scen_results: list[dict] = []
    for entry in spec.get("scenarios", []):
        scen_path = _resolve_repo(entry["scenario"])
        stem = scen_path.stem
        run_dir = out_root / stem
        rec = {"scenario": entry["scenario"], "run_dir": str(run_dir),
               "frames": 0, "compare": None, "verdict": None, "error": None}
        capture_osi = bool(entry.get("osi", default_osi))
        policies = entry.get("policies") or defaults.get("policies") or []
        rec["policies"] = policies
        try:
            scen_to_run = scen_path
            if policies:
                cfg = _write_policy_config(policies, run_dir / "virtual_driver.run.json")
                scen_to_run = _prepare_policy_xosc(scen_path, run_dir, cfg)
            meta = run(scen_to_run, run_dir, dt, max_time, snapshots, dll,
                       capture_osi=capture_osi, osi_port=osi_port)
            rec["frames"] = meta["frames"]
            if meta["frames"] == 0:
                rec["error"] = "no VirtualDriver telemetry captured"
                scen_results.append(rec)
                continue

            baseline = entry.get("baseline")
            if baseline:
                try:
                    cmp = compare(run_dir, _resolve_repo(baseline))
                    rec["compare"] = {k: cmp[k] for k in
                                      ("xy_rmse_m", "xy_max_dev_m", "speed_rmse_mps")}
                except Exception as e:  # baseline missing / no overlap -> non-fatal
                    print(f"[batch] {stem}: compare skipped ({e})", file=sys.stderr)

            exp_spec = None
            if entry.get("expectations"):
                exp_path = _resolve_repo(entry["expectations"])
                exp_spec = yaml.safe_load(exp_path.read_text(encoding="utf-8"))
                v = assert_expectations(run_dir, exp_path)
                rec["verdict"] = {"overall": v["overall"], "summary": v["summary"]}

            if entry.get("report") == "decel":
                _render_decel_report(_load_telemetry(run_dir),
                                     run_dir / "decel_report.png", exp_spec)
        except Exception as e:
            rec["error"] = str(e)
            print(f"[batch] {stem}: ERROR {e}", file=sys.stderr)
        scen_results.append(rec)

    def _status(rec: dict) -> str:
        if rec["error"]:
            return "error"
        if rec["verdict"]:
            return rec["verdict"]["overall"]
        return "needs-review"

    counts = {"pass": 0, "fail": 0, "needs-review": 0, "error": 0}
    for rec in scen_results:
        st = _status(rec)
        counts[st] = counts.get(st, 0) + 1
    overall = ("fail" if counts["fail"] or counts["error"]
               else "needs-review" if counts["needs-review"] else "pass")

    agg = {"name": name, "manifest": str(manifest), "commit": _git_commit(),
           "scenarios": scen_results, "summary": counts, "overall": overall}
    (out_root / "batch_verdict.json").write_text(json.dumps(agg, indent=2), encoding="utf-8")

    lines = [f"# Batch: {name}", "",
             f"**Overall: {overall}**  (pass={counts['pass']} fail={counts['fail']} "
             f"needs-review={counts['needs-review']} error={counts['error']})  "
             f"commit={agg['commit']}", "",
             "| scenario | status | pass/fail/skip | xy_rmse | speed_rmse | first failing event |",
             "| :-- | :-- | :-- | --: | --: | :-- |"]
    for rec in scen_results:
        st = _status(rec)
        v = rec["verdict"]
        pfs = f"{v['summary']['pass']}/{v['summary']['fail']}/{v['summary']['skip']}" if v else "-"
        c = rec["compare"]
        xy = f"{c['xy_rmse_m']:.2f}" if c else "-"
        sp = f"{c['speed_rmse_mps']:.2f}" if c else "-"
        first_fail = "-"
        vj = Path(rec["run_dir"]) / "verdict.json"
        if vj.is_file():
            try:
                vd = json.loads(vj.read_text(encoding="utf-8"))
                ff = next((r for r in vd["results"] if r["status"] == "fail"), None)
                if ff:
                    first_fail = ff["event"]
            except Exception:
                pass
        if rec["error"]:
            first_fail = f"ERROR: {rec['error']}"
        lines.append(f"| {Path(rec['scenario']).name} | {st} | {pfs} | {xy} | {sp} | {first_fail} |")
    lines += ["", f"OVERALL: {overall}"]
    (out_root / "batch_summary.md").write_text("\n".join(lines), encoding="utf-8")

    print(f"[batch] {name}: overall={overall}  {counts} "
          f"-> {out_root / 'batch_verdict.json'} (+batch_summary.md)", file=sys.stderr)
    return agg


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)

    pr = sub.add_parser("run", help="run a scenario in-process and record telemetry")
    pr.add_argument("scenario", type=Path)
    pr.add_argument("--out", type=Path, required=True)
    pr.add_argument("--dt", type=float, default=0.05, help="fixed timestep [s]")
    pr.add_argument("--max-time", type=float, default=60.0, help="safety cap [s]")
    pr.add_argument("--snapshots", type=int, default=3, help="number of keyframe PNGs")
    pr.add_argument("--dll", type=Path, default=None, help="GT_esminiLib.dll path override")
    pr.add_argument("--osi", action="store_true",
                    help="capture OSI groundtruth (objects + signal phase) into telemetry.scene")
    pr.add_argument("--osi-port", type=int, default=OSI_UDP_PORT, help="OSI UDP port to bind")
    pr.add_argument("--policy", default=None,
                    help="comma list of traffic policies to enable "
                         "(lead,traffic_light,stop_yield); injects a ConfigFile into a temp xosc")

    pc = sub.add_parser("compare", help="compare run vs Default baseline (ego RMSE)")
    pc.add_argument("run_dir", type=Path)
    pc.add_argument("baseline", type=Path, help=".osi file or baselines/<name>/ dir")
    pc.add_argument("--max-xy-rmse", type=float, default=None,
                    help="fail (exit 1) when xy_rmse_m exceeds this [m]")
    pc.add_argument("--max-xy-dev", type=float, default=None,
                    help="fail (exit 1) when xy_max_dev_m exceeds this [m]")
    pc.add_argument("--max-speed-rmse", type=float, default=None,
                    help="fail (exit 1) when speed_rmse_mps exceeds this [m/s]")

    pa = sub.add_parser("assert", help="match run telemetry against expectations.yaml")
    pa.add_argument("run_dir", type=Path)
    pa.add_argument("--expectations", type=Path, required=True)

    pb = sub.add_parser("batch", help="run a manifest of scenarios (run+compare+assert each)")
    pb.add_argument("manifest", type=Path)
    pb.add_argument("--out", type=Path, required=True)
    pb.add_argument("--dll", type=Path, default=None, help="GT_esminiLib.dll path override")

    prep = sub.add_parser("report", help="render a deceleration-profile PNG for an existing run")
    prep.add_argument("run_dir", type=Path)
    prep.add_argument("--expectations", type=Path, default=None)

    args = p.parse_args(argv)

    if args.cmd == "run":
        if not args.scenario.is_file():
            print(f"ERROR: scenario not found: {args.scenario}", file=sys.stderr)
            return 2
        scen = args.scenario.resolve()
        out_dir = args.out.resolve()
        if args.policy:
            policies = [p.strip() for p in args.policy.split(",") if p.strip()]
            cfg = _write_policy_config(policies, out_dir / "virtual_driver.run.json")
            scen = _prepare_policy_xosc(scen, out_dir, cfg)
        meta = run(scen, out_dir, args.dt,
                   args.max_time, args.snapshots, args.dll,
                   capture_osi=args.osi, osi_port=args.osi_port)
        return 0 if meta["frames"] > 0 else 1

    if args.cmd == "compare":
        try:
            result = compare(args.run_dir.resolve(), args.baseline.resolve())
        except (FileNotFoundError, RuntimeError) as e:
            print(f"[compare] ERROR: {e}", file=sys.stderr)
            return 1
        violations = []
        for opt, key, unit in (("max_xy_rmse", "xy_rmse_m", "m"),
                               ("max_xy_dev", "xy_max_dev_m", "m"),
                               ("max_speed_rmse", "speed_rmse_mps", "m/s")):
            limit = getattr(args, opt)
            if limit is not None and result[key] > limit:
                violations.append(f"{key}={result[key]}{unit} > {limit}{unit}")
        if violations:
            print(f"[compare] FAIL: {'; '.join(violations)}", file=sys.stderr)
            return 1
        return 0

    if args.cmd == "assert":
        v = assert_expectations(args.run_dir.resolve(), args.expectations.resolve())
        return 0 if v["overall"] in ("pass", "needs-review") else 1

    if args.cmd == "batch":
        if not args.manifest.is_file():
            print(f"ERROR: manifest not found: {args.manifest}", file=sys.stderr)
            return 2
        agg = batch(args.manifest.resolve(), args.out.resolve(), args.dll)
        return 0 if agg["overall"] in ("pass", "needs-review") else 1

    if args.cmd == "report":
        frames = _load_telemetry(args.run_dir.resolve())
        exp = None
        if args.expectations:
            import yaml
            exp = yaml.safe_load(args.expectations.resolve().read_text(encoding="utf-8"))
        _render_decel_report(frames, args.run_dir.resolve() / "decel_report.png", exp)
        return 0

    return 2


if __name__ == "__main__":
    raise SystemExit(main())
