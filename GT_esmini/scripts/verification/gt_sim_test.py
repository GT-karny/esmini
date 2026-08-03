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
import shutil
import subprocess
import sys
import time
from datetime import datetime, timezone
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
from vd_metrics import (
    load_telemetry as _load_telemetry,
    _speed_accel_jerk,
)  # noqa: E402

# OSI groundtruth capture (opt-in), so lead-vehicle distance (THW) and live
# signal phase are available to the matchers. feature:F7 gate hardening,
# 2026-07-28: this used to open a UDP socket and reassemble GT_Step's
# wire-format emission from a loopback socket (_OsiCapture), which silently
# dropped frames under loopback UDP buffer pressure (confirmed:
# normal_following captured only 403/440 frames in one run) and papered over
# the loss with a fabricated empty-but-truthy scene dict indistinguishable
# from "confirmed empty world" -- a measurement failure disguised as a real
# observation (test_results/f7_foundation_progress.md). Now retrieved
# in-process via SE_GetOSIGroundTruth (GtLib.get_osi_ground_truth) -- no
# socket, nothing to drop packets over. See run()'s capture_osi branch.

# feature:F7 gate hardening -- port occupancy check. Moved into vd_metrics.py
# (2026-07-28, second round): an audit found that keeping this logic here,
# even with run()/batch() as call sites, still missed the actual 2026-07-27
# incident party -- services/vd_verify.py's generate_baseline(), a web
# BACKEND production code path that never calls into this module at all.
# vd_metrics.py is the one module both this CLI and the web backend already
# share (see its own docstring), so the check now lives there, called from
# capture_osi() itself -- the true common low-level bind site, not a caller
# that has to remember to invoke a separate guard. Re-exported here under
# their original names so run()/batch() below (which check the FULL table --
# this CLI, unlike a production web-backend run, has no legitimate claim to
# ANY of these ports) and this module's existing tests keep working
# unchanged.
check_gate_ports_free = _vd.check_gate_ports_free
GatePortsBusyError = _vd.GatePortsBusyError
_require_gate_ports_free = _vd.require_gate_ports_free

# osi3 enum -> string maps (mirror of api/osi_stream.py, kept local on purpose).
_TL_COLOR_MAP = {
    0: "unknown",
    1: "other",
    2: "red",
    3: "yellow",
    4: "green",
    5: "blue",
    6: "white",
}
_TL_MODE_MAP = {
    0: "unknown",
    1: "other",
    2: "off",
    3: "constant",
    4: "flashing",
    5: "counting",
}
_MOVING_TYPE_MAP = {
    0: "unknown",
    1: "other",
    2: "vehicle",
    3: "pedestrian",
    4: "animal",
}

# Enum names for the fields added by the ④観測 wiring (traffic-sign classification,
# stationary-object type, traffic-light icon) are read off the protobuf descriptor
# instead of a hand-kept table: those enums are large (StVO sign catalogue) and a
# frozen copy would silently rot against the osi3 bindings actually installed.
_ENUM_NAME_CACHE: dict[tuple[str, str], dict[int, str]] = {}


def _enum_name(msg_cls, field: str, value: int, prefix: str) -> str:
    key = (msg_cls.DESCRIPTOR.full_name, field)
    table = _ENUM_NAME_CACHE.get(key)
    if table is None:
        try:
            enum_type = msg_cls.DESCRIPTOR.fields_by_name[field].enum_type
            table = {
                v.number: v.name.removeprefix(prefix).lower() for v in enum_type.values
            }
        except Exception:
            table = {}
        _ENUM_NAME_CACHE[key] = table
    # Unmapped is not always "unknown": esmini writes an INT_MAX sentinel for
    # "catalogue did not classify this sign", which must stay distinguishable
    # from OSI's TYPE_UNKNOWN(0) so a matcher can tell "no sign" from "sign we
    # could not name" (GT_OSIReporter_Traffic.cpp P4 fallback).
    return table.get(value, f"unmapped:{value}")


# OpenDRIVE object ids at/above this value are synthesised by GT's 1.8 junction
# <crossPath> expansion (OdrJunctionExtras.cpp kCrosswalkSynthIdBase) rather than
# authored in the xodr. The primary in-band crosswalk evidence is the
# "odr_type:crosswalk" source_reference identifier (OSI folds every "other" road
# object into TYPE_OTHER, so the ODR <object type> string is emitted alongside
# object_id); this id range remains only as a fallback for telemetry captured
# with a DLL that predates the odr_type identifier.
_CROSSWALK_SYNTH_ID_BASE = 900000000

# scene keys that hold static GroundTruth: emitted on the frame they were first
# captured and forward-filled by vd_metrics.load_telemetry (they never change).
_STATIC_SCENE_KEYS = ("traffic_signs", "stationary_objects", "lane_map")


# ---------------------------------------------------------------------------
# run
# ---------------------------------------------------------------------------


def _git_commit() -> str:
    try:
        out = subprocess.run(
            ["git", "rev-parse", "--short", "HEAD"],
            cwd=str(REPO_ROOT),
            capture_output=True,
            text=True,
            timeout=10,
        )
        return out.stdout.strip() if out.returncode == 0 else ""
    except Exception:
        return ""


def _gt_to_scene(raw: bytes, _gt_cache=[]) -> dict | None:
    """Parse a raw GroundTruth frame into a lightweight scene dict:

      objects:            [{id,name,x,y,h,speed,vx,vy,vz,ax,ay,az,length,width,
                            is_host,type,lane_global_id}]
      traffic_lights:     [{id,x,y,h,color,mode,icon,assigned_lane_ids}]
      traffic_signs:      [{id,type,value,value_unit,x,y,h}]        (static, see below)
      stationary_objects: [{id,type,x,y,h,length,width,height,odr_object_id,
                            odr_type,is_crosswalk,polygon}]         (static, see below)
      lane_map:           {str(lane_global_id): {road_id,lane_id}}  (static, see below)

    Keys are only ever added, never repurposed: `speed` stays the scalar
    magnitude it always was and `vx,vy,vz` are additive, because the scalar
    destroys the sign of the approach ("is the pedestrian moving away?") that
    OSI does carry (GT_OSIReporter_Moving.cpp:767-769). `ax,ay,az` are the OSI
    longitudinal/lateral acceleration vector (base.acceleration,
    GT_OSIReporter_Moving.cpp:772-774) — face-1's own acceleration, so the
    mid/long matchers no longer have to reconstruct it from a speed difference.
    `lane_global_id` is the object's OSI assigned_lane_id (classification
    preferred, deprecated MovingObject field4 fallback — the dual emit of
    signal:lane_id_indicator_nonvd); joined against `lane_map` (built from OSI
    Lane.source_reference, GT_OSIReporter_Geometry.cpp:1335-1344) it yields the
    OpenDRIVE road_id/lane_id from face-1. The join is trustworthy at LANE
    granularity since the GT-side fix (2026-07-21,
    spine-work:osi-assigned-lane-driving): assigned_lane_id used to re-derive
    the lane from (s, t) via GetLaneGlobalId() and drifted onto border/sidewalk
    lanes (measured red_stop_green_go: -1->-3 while the VD stayed lane -1); it
    now emits the object's cached DRIVING lane
    (ResolveMovingObjectAssignedLaneGlobalId) and agrees with the VD Position
    lane (same scenario: 1200/1200 frames). vd_metrics._ego_state resolves the
    host's road_id AND lane through this join, telemetry fallback.

    `traffic_signs` / `stationary_objects` / `lane_map` are *static* GroundTruth
    and in the default static-report mode are present on the first emitted frame
    only, so these keys are omitted from frames that carry no static content. The
    caller is responsible for carrying them forward (run() below stores them,
    vd_metrics.load_telemetry forward-fills them for the matchers).

    Reuses one GroundTruth message object across calls (parse churn)."""
    from osi3.osi_groundtruth_pb2 import GroundTruth
    from osi3.osi_object_pb2 import StationaryObject
    from osi3.osi_trafficlight_pb2 import TrafficLight
    from osi3.osi_trafficsign_pb2 import TrafficSign

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
        acc = o.base.acceleration
        dim = o.base.dimension
        name = ""
        for ref in o.source_reference:
            if ref.type == "net.asam.openscenario":
                for ident in ref.identifier:
                    if ident.startswith("entity_name:"):
                        name = ident[len("entity_name:") :]
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
            "speed": round(math.sqrt(vel.x**2 + vel.y**2 + vel.z**2), 3),
            "vx": round(vel.x, 3),
            "vy": round(vel.y, 3),
            "vz": round(vel.z, 3),
            "ax": round(acc.x, 3),
            "ay": round(acc.y, 3),
            "az": round(acc.z, 3),
            "length": round(dim.length, 2) if dim.length > 0 else 4.0,
            "width": round(dim.width, 2) if dim.width > 0 else 2.0,
            "is_host": (host_id is not None and o.id.value == host_id),
            "type": _MOVING_TYPE_MAP.get(o.type, "unknown"),
            # OSI assigned_lane_id (global lane id); resolve to OpenDRIVE
            # road_id/lane_id via scene["lane_map"]. None when OSI set no lane.
            # Prefer the OSI 3.7.0 home (MovingObjectClassification); the
            # MovingObject-level field is deprecated and kept as fallback for
            # telemetry captured with a DLL predating the dual emit.
            "lane_global_id": (
                o.moving_object_classification.assigned_lane_id[0].value
                if o.moving_object_classification.assigned_lane_id
                else (o.assigned_lane_id[0].value if o.assigned_lane_id else None)
            ),
        }
        if dims_fallback:
            obj["dims_fallback"] = True
        objects.append(obj)

    traffic_lights = []
    for tl in gt.traffic_light:
        pos = tl.base.position
        cls = tl.classification
        traffic_lights.append(
            {
                "id": tl.id.value,
                "x": round(pos.x, 3),
                "y": round(pos.y, 3),
                "h": round(tl.base.orientation.yaw, 4),
                "color": _TL_COLOR_MAP.get(cls.color, "unknown"),
                "mode": _TL_MODE_MAP.get(cls.mode, "unknown"),
                # icon is what distinguishes a pedestrian head (walk/dont_walk/
                # pedestrian) from a vehicle head; without it every lamp looks alike.
                "icon": _enum_name(
                    TrafficLight.Classification, "icon", cls.icon, "ICON_"
                ),
                # repeated in OSI: one head may govern several lanes.
                "assigned_lane_ids": [i.value for i in cls.assigned_lane_id],
            }
        )

    scene = {"objects": objects, "traffic_lights": traffic_lights}

    # --- static GroundTruth (first emitted frame only) ----------------------
    # OSI Lane -> OpenDRIVE road_id/lane_id table. esmini stamps every lane's
    # source_reference with type="net.asam.opendrive" and identifiers
    # road_id:<n>/road_s:<s>/lane_id:<m> (GT_OSIReporter_Geometry.cpp:1335-1344),
    # and the Lane.id shares the global-id space with moving_object's
    # assigned_lane_id, so this is a straight face-1 join key -> road/lane.
    lane_map = {}
    for ln in gt.lane:
        road_id = lane_id = None
        for ref in ln.source_reference:
            if ref.type != "net.asam.opendrive":
                continue
            for ident in ref.identifier:
                if ident.startswith("road_id:"):
                    road_id = ident[len("road_id:") :]
                elif ident.startswith("lane_id:"):
                    lane_id = ident[len("lane_id:") :]
        if road_id is None:
            continue
        try:
            entry = {
                "road_id": int(road_id),
                "lane_id": int(lane_id) if lane_id is not None else None,
            }
        except ValueError:
            continue
        # str key: telemetry.jsonl round-trips dict keys through JSON as strings,
        # so the consumer looks up str(lane_global_id) either way.
        lane_map[str(ln.id.value)] = entry
    if lane_map:
        scene["lane_map"] = lane_map

    # --- static GroundTruth: signs / stationary objects ---------------------
    traffic_signs = []
    for ts in gt.traffic_sign:
        main = ts.main_sign
        cls = main.classification
        traffic_signs.append(
            {
                "id": ts.id.value,
                "type": _enum_name(
                    TrafficSign.MainSign.Classification, "type", cls.type, "TYPE_"
                ),
                "value": round(cls.value.value, 3),
                "value_unit": _enum_name(
                    type(cls.value), "value_unit", cls.value.value_unit, "UNIT_"
                ),
                "x": round(main.base.position.x, 3),
                "y": round(main.base.position.y, 3),
                "h": round(main.base.orientation.yaw, 4),
            }
        )
    if traffic_signs:
        scene["traffic_signs"] = traffic_signs

    stationary = []
    for so in gt.stationary_object:
        dim = so.base.dimension
        odr_id = None
        odr_type = None
        for ref in so.source_reference:
            for ident in ref.identifier:
                if ident.startswith("object_id:"):
                    try:
                        odr_id = int(ident[len("object_id:") :])
                    except ValueError:
                        pass
                elif ident.startswith("odr_type:"):
                    odr_type = ident[len("odr_type:") :]
        stationary.append(
            {
                "id": so.id.value,
                "type": _enum_name(
                    StationaryObject.Classification,
                    "type",
                    so.classification.type,
                    "TYPE_",
                ),
                "x": round(so.base.position.x, 3),
                "y": round(so.base.position.y, 3),
                "h": round(so.base.orientation.yaw, 4),
                "length": round(dim.length, 2),
                "width": round(dim.width, 2),
                "height": round(dim.height, 2),
                "odr_object_id": odr_id,
                # The ODR <object type> string ("crosswalk", "railing", ...), or
                # None on telemetry from a DLL predating the odr_type identifier.
                "odr_type": odr_type,
                # False still means "not provably a crosswalk", NOT "provably not
                # a crosswalk": with odr_type present the answer is authoritative
                # (authored AND synthesised), without it only the synth id range
                # is recognisable (see _CROSSWALK_SYNTH_ID_BASE).
                "is_crosswalk": (odr_type == "crosswalk")
                or (
                    odr_type is None
                    and odr_id is not None
                    and odr_id >= _CROSSWALK_SYNTH_ID_BASE
                ),
                # Object-LOCAL corners, not world: esmini fills base_polygon from
                # Outline::GetPosLocal (GT_OSIReporter.cpp:840-843). A consumer
                # wanting world coordinates must rotate by `h` and offset by x,y.
                "polygon": [
                    [round(p.x, 3), round(p.y, 3)] for p in so.base.base_polygon
                ],
            }
        )
    if stationary:
        scene["stationary_objects"] = stationary

    return scene


def run(
    scenario: Path,
    out_dir: Path,
    dt: float,
    max_time: float,
    snapshots: int,
    dll: Path | None,
    capture_osi: bool = False,
) -> dict:
    # feature:F7 gate hardening -- the actual common choke point (see
    # _require_gate_ports_free's module-level comment): every invocation path
    # (batch(), the CLI `run` subcommand, a skill calling this directly)
    # reaches this exact line before touching the DLL or any socket below.
    _require_gate_ports_free()

    out_dir.mkdir(parents=True, exist_ok=True)
    jsonl_path = out_dir / "telemetry.jsonl"

    args = ["--osc", str(scenario), "--headless", "--fixed_timestep", str(dt)]
    lib = GtLib(dll) if dll else GtLib()

    frames: list[dict] = []
    grace = 0
    GRACE_MAX = int(round(1.0 / dt))  # ~1s of consecutive "no telemetry" = ended
    seen_valid = False
    last_scene: dict | None = None
    # Written to the telemetry once (see _STATIC_SCENE_KEYS): repeating a whole
    # sign/stationary catalogue on every frame would multiply telemetry.jsonl by
    # the frame count for data that never changes.
    static_scene: dict = {}
    static_emitted = False
    # feature:F7 gate hardening, 2026-07-28: every step where capture_osi is on
    # and SE_GetOSIGroundTruth() came back empty/unparseable is a genuine
    # capture failure now (no UDP transport left to blame it on) -- counted
    # and raised at the end instead of silently reused/fabricated (see the
    # module-level comment above and gt_lib.get_osi_ground_truth's docstring).
    osi_misses = 0
    osi_steps = 0

    with lib, open(jsonl_path, "w", encoding="utf-8") as f:
        rc = lib.init_with_args(args)
        if rc != 0:
            cause = lib.get_last_error()
            raise RuntimeError(
                f"GT_InitWithArgs failed (rc={rc}) for {scenario}"
                + (f": {cause}" if cause else "")
            )
        if capture_osi:
            # GT_InitWithArgs doesn't turn on per-frame OSI updates by itself;
            # this must land before the first step() so frame 1 isn't missed.
            lib.set_osi_frequency(1)

        n_steps = int(round(max_time / dt))
        for _ in range(n_steps):
            lib.step(dt)
            if capture_osi:
                osi_steps += 1
                raw = lib.get_osi_ground_truth()
                scene = _gt_to_scene(raw) if raw is not None else None
                if scene is None:
                    osi_misses += 1
                else:
                    # Static content (signs, stationary objects) rides on the
                    # first emitted frame only; keep it once it shows up and
                    # re-attach it to every later (dynamic-only) scene.
                    found = {k: scene[k] for k in _STATIC_SCENE_KEYS if k in scene}
                    if found and not static_scene:
                        static_scene = found
                    last_scene = scene
                if last_scene is not None and static_scene and not static_emitted:
                    last_scene.update(static_scene)
                    static_emitted = True
            tel = lib.get_vd_telemetry(-1)
            if tel is None:
                if seen_valid:
                    grace += 1
                    if grace >= GRACE_MAX:
                        break  # controller deactivated -> scenario ended
                continue
            seen_valid = True
            grace = 0
            if capture_osi:
                # last_scene is None only when NOTHING has ever been captured
                # yet (e.g. this frame or an early one missed). Do not
                # fabricate {"objects": [], ...}: that dict is truthy, so
                # matchers' `if not scene: continue` guard would never catch
                # it and would read a capture failure as "confirmed empty
                # world" (test_results/f7_foundation_progress.md). None is
                # falsy and takes the existing skip path correctly.
                tel["scene"] = last_scene
            f.write(json.dumps(tel, separators=(",", ":")) + "\n")
            frames.append(tel)

    if capture_osi and osi_misses:
        raise RuntimeError(
            f"OSI ground-truth capture failed on {osi_misses}/{osi_steps} step(s) "
            f"for {scenario}: SE_GetOSIGroundTruth returned no/unparseable data "
            "despite capture_osi=True. This used to be silently papered over as "
            "an empty scene (feature:F7 gate hardening, 2026-07-28); it now "
            "fails loudly because a scene-dependent matcher's result would "
            "otherwise be silently corrupted."
        )

    duration = frames[-1]["sim_time"] if frames else 0.0
    meta = {
        "scenario": (
            str(scenario.relative_to(REPO_ROOT))
            if scenario.is_absolute() and str(scenario).startswith(str(REPO_ROOT))
            else str(scenario)
        ),
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

    print(
        f"[run] {scenario.name}: {len(frames)} frames, {duration:.1f}s -> {jsonl_path}",
        file=sys.stderr,
    )
    if not frames:
        print(
            "[run] WARNING: no VirtualDriver telemetry captured - does the scenario "
            "assign a VirtualDriverController to the ego?",
            file=sys.stderr,
        )
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

    idxs = (
        [int(round(i * (len(frames) - 1) / max(1, count - 1))) for i in range(count)]
        if count > 1
        else [len(frames) - 1]
    )

    for k, idx in enumerate(idxs):
        fr = frames[idx]
        fig, ax = plt.subplots(figsize=(5, 6))
        ax.plot(
            xs[: idx + 1], ys[: idx + 1], "-", color="#7B88E8", lw=1.2, label="ego path"
        )
        ego = fr["ego"]
        ax.plot(ego["x"], ego["y"], "o", color="#9B84E8", ms=8, label="ego")
        prev = fr.get("preview", {})
        pts = prev.get("points", [])
        if pts:
            ax.plot(
                [p["x"] for p in pts],
                [p["y"] for p in pts],
                ".-",
                color="#4FD18B",
                ms=2,
                lw=0.8,
                label="preview",
            )
        ax.set_xlim(*xlim)
        ax.set_ylim(*ylim)
        ax.set_aspect("equal", "box")
        ax.set_title(f"t={ego.get('speed', 0):.1f} m/s  sim_t={fr['sim_time']:.1f}s")
        ax.legend(loc="upper right", fontsize=7)
        ax.grid(alpha=0.2)
        fig.tight_layout()
        fig.savefig(snap_dir / f"frame_{k:02d}_t{fr['sim_time']:.1f}.png", dpi=90)
        plt.close(fig)


# ---------------------------------------------------------------------------
# compare
# ---------------------------------------------------------------------------


def compare(run_dir: Path, baseline: Path, grid_dt: float = 0.1) -> dict:
    """Thin CLI wrapper over vd_metrics.compare (adds a stderr summary line)."""
    result = _vd.compare(run_dir, baseline, grid_dt)
    print(
        f"[compare] xy_rmse={result['xy_rmse_m']}m  speed_rmse={result['speed_rmse_mps']}m/s  "
        f"max_dev={result['xy_max_dev_m']}m -> {run_dir / 'compare.json'} (+baseline_track.json)",
        file=sys.stderr,
    )
    return result


# ---------------------------------------------------------------------------
# assert (expectations.yaml)
# ---------------------------------------------------------------------------


def assert_expectations(run_dir: Path, expectations: Path) -> dict:
    """Thin CLI wrapper over vd_metrics.assert_expectations (adds stderr prints)."""
    verdict = _vd.assert_expectations(run_dir, expectations)
    s = verdict["summary"]
    print(
        f"[assert] overall={verdict['overall']}  pass={s['pass']} fail={s['fail']} "
        f"skip={s['skip']} -> {run_dir / 'verdict.json'}",
        file=sys.stderr,
    )
    for r in verdict["results"]:
        print(f"   [{r['status']:4}] {r['event']}: {r['detail']}", file=sys.stderr)
    return verdict


# ---------------------------------------------------------------------------
# deceleration-profile report
# ---------------------------------------------------------------------------


def _render_decel_report(
    frames: list[dict], out_path: Path, expectation: dict | None = None
) -> None:
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
        lm = next(
            (
                i
                for i in range(n)
                if (road_id is None or frames[i]["ego"].get("track") == road_id)
                and s[i] >= landmark_s
            ),
            None,
        )
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
        ax0.axhspan(
            target_speed - target_tol,
            target_speed + target_tol,
            color="#4FD18B",
            alpha=0.18,
            label=f"target {target_speed:g}±{target_tol:g}",
        )
    if landmark_s is not None:
        ax0.axvline(
            landmark_s,
            color="#E8590C",
            ls="--",
            lw=1.2,
            label=f"landmark s={landmark_s:g}",
        )
    if onset is not None and lm is not None and lm > onset:
        ax0.axvspan(s[onset], s[lm], color="#3B5BDB", alpha=0.07, label="decel phase")
    if onset is not None:
        ax0.plot(s[onset], v[onset], "v", color="#E03131", ms=9, label="decel onset")
    ax0.set_xlabel("route s [m]")
    ax0.set_ylabel("speed [m/s]")
    ax0.set_title(
        f"Deceleration profile  ({win_label}: max decel={max_decel:.2f} m/s^2, "
        f"max |jerk|={max_jerk:.2f} m/s^3)"
    )
    ax0.grid(alpha=0.25)
    ax0.legend(fontsize=7, loc="best")

    ax1.plot(t, v, color="#3B5BDB", lw=1.2, label="speed [m/s]")
    ax1b = ax1.twinx()
    ax1b.plot(t, a, color="#E8590C", lw=1.0, alpha=0.8, label="accel [m/s^2]")
    ax1b.axhline(0, color="#aaaaaa", lw=0.6)
    ax1.set_xlabel("sim_time [s]")
    ax1.set_ylabel("speed [m/s]")
    ax1b.set_ylabel("accel [m/s^2]")
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
    # vd-func:FUNC-055 (REQ-AD-017 step c): AD-initiated lane change. Not an
    # ITrafficPolicy (see lane_change_initiation.md §4), but the same
    # default-OFF opt-in mechanism applies -- reuse the "policies:" list
    # rather than adding a second config-injection path.
    "lane_change_initiation": "lane_change_initiation_enabled",
    # vd-func:FUNC-056 (overtake maneuver, docs/virtualdriver/design/overtake_maneuver.md).
    # Not an ITrafficPolicy either (same reasoning as lane_change_initiation
    # above) -- reuses this list rather than a second injection path.
    "overtake": "overtake_enabled",
    # Opposing-lane overtake (design doc section 7) is DOUBLE-gated: this flag
    # alone does nothing. It only takes effect when "overtake" is ALSO in the
    # policies list (overtake_use_opposing_lane_enabled is read only inside the
    # overtake_enabled branch), matching the design's "FUNC-030 未実装ゆえの
    # 二重ゲート" rationale for keeping opposing-lane overtaking off by default.
    "overtake_opposing_lane": "overtake_use_opposing_lane_enabled",
}


def _write_policy_config(policies: list[str], out_path: Path) -> Path:
    """Write a per-run virtual_driver.json = base config + the requested policy
    enable flags set true. Unknown policy names raise (typos shouldn't silently
    run with everything off)."""
    base = json.loads(BASE_VD_CONFIG.read_text(encoding="utf-8"))
    for p in policies:
        flag = _POLICY_FLAG.get(p)
        if flag is None:
            raise ValueError(
                f"unknown policy '{p}' (want one of {sorted(_POLICY_FLAG)})"
            )
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

    for tag, attr in (
        ("LogicFile", "filepath"),
        ("SceneGraphFile", "filepath"),
        ("Directory", "path"),
    ):
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
            ET.SubElement(
                props, "Property", {"name": "ConfigFile", "value": str(config_path)}
            )
            injected = True
    if not injected:
        raise RuntimeError(
            f"{scenario.name}: no VirtualDriverController to inject ConfigFile into"
        )

    run_dir.mkdir(parents=True, exist_ok=True)
    out = run_dir / (scenario.stem + ".policyrun.xosc")
    tree.write(out, encoding="utf-8", xml_declaration=True)
    return out


def _reset_batch_output_dir(out_root: Path) -> None:
    """feature:F7 gate hardening -- clear out_root before a batch run.

    Sibling hole to run_regression_gate.ps1's port preflight (commit
    8b006cff): that commit made "0 scenarios measured" a hard FAIL, but a
    process that dies AFTER writing per-scenario error records into
    batch_verdict.json is not the only way to measure nothing -- a process
    that dies BEFORE ever reaching that write (a native crash in the DLL, the
    process getting killed, Ctrl+C mid-batch) leaves batch() never touching
    out_root at all. Every prior invocation's batch_verdict.json (quite
    possibly overall=pass) is then still sitting there, indistinguishable
    from a fresh result to any caller that only checks "does the file exist
    and parse". check_regression_baseline.py / run_regression_gate.ps1 would
    read yesterday's green and report it as today's.

    rmtree + mkdir, not just deleting the two known top-level files: a
    per-scenario run_dir (telemetry.jsonl / snapshots/ / verdict.json) must
    not survive either, or a scenario that errors out on THIS run could be
    masked by a stale verdict.json a PREVIOUS run of the same manifest left
    behind at the same path (batch_summary.md's per-scenario "first failing
    event" column reads that file).

    Called as the FIRST action in batch(), before even the manifest is
    parsed, so a manifest-parse failure cannot leave a stale verdict in place
    either.
    """
    if out_root.exists():
        shutil.rmtree(out_root)
    out_root.mkdir(parents=True, exist_ok=True)


def batch(manifest: Path, out_root: Path, dll: Path | None = None) -> dict:
    """Run a manifest of scenarios: for each, run() -> (compare if baseline) ->
    assert (+ optional decel report). Per-scenario failures are recorded as
    'error' and do not abort the batch. Writes batch_verdict.json + a
    Claude-readable batch_summary.md.

    See _reset_batch_output_dir for why out_root is wiped before anything
    else: a stale (e.g. previous-run, overall=pass) batch_verdict.json must
    never survive to be misread as this run's result.

    Port occupancy is also checked here, OUTSIDE the per-scenario try/except
    below, so a busy port aborts the WHOLE batch immediately with one clear
    GatePortsBusyError instead of N identical per-scenario error records (one
    per scenario, since run() -- called inside that try/except -- checks
    again itself; see _require_gate_ports_free's module comment for why the
    check lives there too and is not redundant to remove).
    """
    import yaml

    _reset_batch_output_dir(out_root)
    _require_gate_ports_free()
    spec = yaml.safe_load(manifest.read_text(encoding="utf-8"))
    name = spec.get("name", manifest.stem)
    defaults = spec.get("defaults", {}) or {}
    dt = float(defaults.get("dt", 0.05))
    max_time = float(defaults.get("max_time", 60.0))
    snapshots = int(defaults.get("snapshots", 3))
    default_osi = bool(defaults.get("osi", False))

    scen_results: list[dict] = []
    for entry in spec.get("scenarios", []):
        scen_path = _resolve_repo(entry["scenario"])
        stem = scen_path.stem
        run_dir = out_root / stem
        rec = {
            "scenario": entry["scenario"],
            "run_dir": str(run_dir),
            "frames": 0,
            "compare": None,
            "verdict": None,
            "error": None,
        }
        capture_osi = bool(entry.get("osi", default_osi))
        policies = entry.get("policies") or defaults.get("policies") or []
        rec["policies"] = policies
        try:
            scen_to_run = scen_path
            if policies:
                cfg = _write_policy_config(
                    policies, run_dir / "virtual_driver.run.json"
                )
                scen_to_run = _prepare_policy_xosc(scen_path, run_dir, cfg)
            meta = run(
                scen_to_run,
                run_dir,
                dt,
                max_time,
                snapshots,
                dll,
                capture_osi=capture_osi,
            )
            rec["frames"] = meta["frames"]
            if meta["frames"] == 0:
                rec["error"] = "no VirtualDriver telemetry captured"
                scen_results.append(rec)
                continue

            baseline = entry.get("baseline")
            if baseline:
                try:
                    cmp = compare(run_dir, _resolve_repo(baseline))
                    rec["compare"] = {
                        k: cmp[k]
                        for k in ("xy_rmse_m", "xy_max_dev_m", "speed_rmse_mps")
                    }
                except Exception as e:  # baseline missing / no overlap -> non-fatal
                    print(f"[batch] {stem}: compare skipped ({e})", file=sys.stderr)

            exp_spec = None
            if entry.get("expectations"):
                exp_path = _resolve_repo(entry["expectations"])
                exp_spec = yaml.safe_load(exp_path.read_text(encoding="utf-8"))
                v = assert_expectations(run_dir, exp_path)
                rec["verdict"] = {"overall": v["overall"], "summary": v["summary"]}

            if entry.get("report") == "decel":
                _render_decel_report(
                    _load_telemetry(run_dir), run_dir / "decel_report.png", exp_spec
                )
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
    overall = (
        "fail"
        if counts["fail"] or counts["error"]
        else "needs-review" if counts["needs-review"] else "pass"
    )

    agg = {
        "name": name,
        "manifest": str(manifest),
        "commit": _git_commit(),
        # feature:F7 gate hardening -- generation timestamp, for a human/CI
        # skimming batch_verdict.json to sanity-check "is this actually from
        # just now" without cross-referencing file mtimes. The mechanical
        # freshness guarantee itself is _reset_batch_output_dir() above, not
        # this field -- a timestamp alone cannot be trusted (nothing stops a
        # stale file's mtime/embedded clock from looking recent by
        # coincidence); the guarantee is that a stale file cannot physically
        # be present at all by the time this dict is written.
        "generated_at": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "scenarios": scen_results,
        "summary": counts,
        "overall": overall,
    }
    (out_root / "batch_verdict.json").write_text(
        json.dumps(agg, indent=2), encoding="utf-8"
    )

    lines = [
        f"# Batch: {name}",
        "",
        f"**Overall: {overall}**  (pass={counts['pass']} fail={counts['fail']} "
        f"needs-review={counts['needs-review']} error={counts['error']})  "
        f"commit={agg['commit']}",
        "",
        "| scenario | status | pass/fail/skip | xy_rmse | speed_rmse | first failing event |",
        "| :-- | :-- | :-- | --: | --: | :-- |",
    ]
    for rec in scen_results:
        st = _status(rec)
        v = rec["verdict"]
        pfs = (
            f"{v['summary']['pass']}/{v['summary']['fail']}/{v['summary']['skip']}"
            if v
            else "-"
        )
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
        lines.append(
            f"| {Path(rec['scenario']).name} | {st} | {pfs} | {xy} | {sp} | {first_fail} |"
        )
    lines += ["", f"OVERALL: {overall}"]
    (out_root / "batch_summary.md").write_text("\n".join(lines), encoding="utf-8")

    print(
        f"[batch] {name}: overall={overall}  {counts} "
        f"-> {out_root / 'batch_verdict.json'} (+batch_summary.md)",
        file=sys.stderr,
    )
    return agg


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    sub = p.add_subparsers(dest="cmd", required=True)

    pr = sub.add_parser("run", help="run a scenario in-process and record telemetry")
    pr.add_argument("scenario", type=Path)
    pr.add_argument("--out", type=Path, required=True)
    pr.add_argument("--dt", type=float, default=0.05, help="fixed timestep [s]")
    pr.add_argument("--max-time", type=float, default=60.0, help="safety cap [s]")
    pr.add_argument("--snapshots", type=int, default=3, help="number of keyframe PNGs")
    pr.add_argument(
        "--dll", type=Path, default=None, help="GT_esminiLib.dll path override"
    )
    pr.add_argument(
        "--osi",
        action="store_true",
        help="capture OSI groundtruth (objects + signal phase) into telemetry.scene",
    )
    pr.add_argument(
        "--policy",
        default=None,
        help="comma list of traffic policies to enable "
        "(lead,traffic_light,stop_yield); injects a ConfigFile into a temp xosc",
    )

    pc = sub.add_parser("compare", help="compare run vs Default baseline (ego RMSE)")
    pc.add_argument("run_dir", type=Path)
    pc.add_argument("baseline", type=Path, help=".osi file or baselines/<name>/ dir")
    pc.add_argument(
        "--max-xy-rmse",
        type=float,
        default=None,
        help="fail (exit 1) when xy_rmse_m exceeds this [m]",
    )
    pc.add_argument(
        "--max-xy-dev",
        type=float,
        default=None,
        help="fail (exit 1) when xy_max_dev_m exceeds this [m]",
    )
    pc.add_argument(
        "--max-speed-rmse",
        type=float,
        default=None,
        help="fail (exit 1) when speed_rmse_mps exceeds this [m/s]",
    )

    pa = sub.add_parser("assert", help="match run telemetry against expectations.yaml")
    pa.add_argument("run_dir", type=Path)
    pa.add_argument("--expectations", type=Path, required=True)

    pb = sub.add_parser(
        "batch", help="run a manifest of scenarios (run+compare+assert each)"
    )
    pb.add_argument("manifest", type=Path)
    pb.add_argument("--out", type=Path, required=True)
    pb.add_argument(
        "--dll", type=Path, default=None, help="GT_esminiLib.dll path override"
    )

    prep = sub.add_parser(
        "report", help="render a deceleration-profile PNG for an existing run"
    )
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
        try:
            meta = run(
                scen,
                out_dir,
                args.dt,
                args.max_time,
                args.snapshots,
                args.dll,
                capture_osi=args.osi,
            )
        except GatePortsBusyError as e:
            # feature:F7 gate hardening -- distinct exit code 2, same meaning
            # as run_regression_gate.ps1's port preflight: refused to run
            # (measured nothing), not "ran and found a real problem" (that's 1).
            print(f"[run] ABORTED: {e}", file=sys.stderr)
            return 2
        return 0 if meta["frames"] > 0 else 1

    if args.cmd == "compare":
        try:
            result = compare(args.run_dir.resolve(), args.baseline.resolve())
        except (FileNotFoundError, RuntimeError) as e:
            print(f"[compare] ERROR: {e}", file=sys.stderr)
            return 1
        violations = []
        for opt, key, unit in (
            ("max_xy_rmse", "xy_rmse_m", "m"),
            ("max_xy_dev", "xy_max_dev_m", "m"),
            ("max_speed_rmse", "speed_rmse_mps", "m/s"),
        ):
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
        try:
            agg = batch(args.manifest.resolve(), args.out.resolve(), args.dll)
        except GatePortsBusyError as e:
            # feature:F7 gate hardening -- same exit-code convention as the
            # `run` subcommand above: 2 = refused to measure, not "measured
            # and found a difference" (1).
            print(f"[batch] ABORTED: {e}", file=sys.stderr)
            return 2
        return 0 if agg["overall"] in ("pass", "needs-review") else 1

    if args.cmd == "report":
        frames = _load_telemetry(args.run_dir.resolve())
        exp = None
        if args.expectations:
            import yaml

            exp = yaml.safe_load(
                args.expectations.resolve().read_text(encoding="utf-8")
            )
        _render_decel_report(frames, args.run_dir.resolve() / "decel_report.png", exp)
        return 0

    return 2


if __name__ == "__main__":
    raise SystemExit(main())
