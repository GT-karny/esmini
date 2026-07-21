"""Shared VirtualDriver verification core (trajectory metrics + expectation matchers).

Single source of truth for the verification math that was previously duplicated
between the offline CLI (GT_esmini/scripts/verification/gt_sim_test.py) and the
backend port (services/vd_verify.py) — audit WEB-4. This module is:

- config-free: no import of backend config; all paths/ports are parameters, so
  it works identically in the dev tree, the packaged distribution, and the CLI.
- behavior-identical to gt_sim_test.py's current implementation (the superset:
  includes the V2 mid/long matchers and the Phase 3 traffic-policy matchers the
  old vd_verify.py port had drifted away from), minus the CLI's stdout prints.

Consumers:
- backend: services/vd_verify.py (facade; adds the config-dependent
  Default-baseline generation) -> api/verification.py.
- CLI: gt_sim_test.py still carries its own copy because it is frozen by a
  parallel branch; switch it to import from here after that branch merges.

Wire/file formats handled here:
- telemetry.jsonl: one JSON frame per line (sim_time, ego{x,y,speed,...}, ...).
- .osi trace: length-delimited [uint32 size][GroundTruth] frames.
- OSI UDP: esmini framing [counter:int32][size:uint32][chunk], counter<0 marks
  the final chunk of a frame.
"""

from __future__ import annotations

import json
import math
import socket
import struct
import subprocess
import time
from pathlib import Path

# Max OSI UDP payload + 8-byte header (contract with esmini).
OSI_BUFFER_SIZE = 8208


# ---------------------------------------------------------------------------
# OBB (oriented bounding box) separation — SAT
# ---------------------------------------------------------------------------

def _obb_corners(cx: float, cy: float, h: float, length: float, width: float) -> list[tuple[float, float]]:
    """Four world corners of an oriented rectangle centered at (cx,cy), heading h,
    extents length (along heading) x width (across). NOTE: the OSI scene reports the
    body CENTER, so we center the box on (cx,cy) — adjacent-lane passing then reads
    as a clean gap, not an overlap."""
    ch, sh = math.cos(h), math.sin(h)
    hl, hw = length / 2.0, width / 2.0
    # local corners (along, across) -> world
    out = []
    for ax, ay in ((hl, hw), (hl, -hw), (-hl, -hw), (-hl, hw)):
        out.append((cx + ax * ch - ay * sh, cy + ax * sh + ay * ch))
    return out


def obb_separation(a: dict, b: dict) -> float:
    """Separation distance between two oriented rectangles via the Separating Axis
    Theorem. Each box dict carries x,y,h,length,width (CENTER position).

    Returns:
      <= 0.0 if the rectangles OVERLAP (0.0 = touching/overlap; we collapse the
             penetration case to 0.0 — overlap is overlap for the anti-collision gate),
      > 0.0  the positive clearance gap otherwise. The reported gap is the MAX over
             the (up to 4) candidate separating axes of the positive axis-gap — i.e.
             the SAT separation distance (conservative: it is the smallest distance by
             which one box must move along some axis to separate, taken as the max
             positive axis gap, which lower-bounds the true Euclidean gap)."""
    ca = _obb_corners(a["x"], a["y"], a["h"], a.get("length", 4.0), a.get("width", 2.0))
    cb = _obb_corners(b["x"], b["y"], b["h"], b.get("length", 4.0), b.get("width", 2.0))

    # Candidate axes = the 2 unique edge normals of each box (4 total).
    def _axes(corners):
        axes = []
        for i in range(4):
            ex = corners[(i + 1) % 4][0] - corners[i][0]
            ey = corners[(i + 1) % 4][1] - corners[i][1]
            n = math.hypot(ex, ey)
            if n > 1e-12:
                axes.append((-ey / n, ex / n))  # edge normal (unit)
        return axes

    axes = _axes(ca) + _axes(cb)
    max_gap = -float("inf")  # positive on a separating axis; we want the largest
    overlap_on_all = True
    for ax, ay in axes:
        amin = min(px * ax + py * ay for px, py in ca)
        amax = max(px * ax + py * ay for px, py in ca)
        bmin = min(px * ax + py * ay for px, py in cb)
        bmax = max(px * ax + py * ay for px, py in cb)
        # gap > 0 -> the projections are disjoint on this axis (a separating axis).
        gap = max(bmin - amax, amin - bmax)
        if gap > 0:
            overlap_on_all = False
            if gap > max_gap:
                max_gap = gap
    if overlap_on_all:
        return 0.0  # no separating axis -> the boxes overlap (collision)
    return max_gap


# ---------------------------------------------------------------------------
# telemetry / baseline trajectory extraction
# ---------------------------------------------------------------------------

def load_telemetry(run_dir: Path) -> list[dict]:
    jsonl = run_dir / "telemetry.jsonl"
    if not jsonl.is_file():
        raise FileNotFoundError(f"{jsonl} not found")
    out = []
    for line in jsonl.read_text(encoding="utf-8").splitlines():
        if line.strip():
            out.append(json.loads(line))
    _forward_fill_static_scene(out)
    return out


# Static OSI GroundTruth (traffic signs, stationary objects) is emitted on the
# first frame only, so the recorder writes it once instead of on every frame
# (gt_sim_test._STATIC_SCENE_KEYS). Matchers must not have to know which frame
# that was: fill it forward here, sharing the list object rather than copying.
_STATIC_SCENE_KEYS = ("traffic_signs", "stationary_objects", "lane_map")


def _forward_fill_static_scene(frames: list[dict]) -> None:
    carried: dict = {}
    for fr in frames:
        scene = fr.get("scene")
        if not isinstance(scene, dict):
            continue
        for key in _STATIC_SCENE_KEYS:
            if scene.get(key):
                carried[key] = scene[key]
            elif key in carried:
                scene[key] = carried[key]


def ego_track_from_telemetry(frames: list[dict]) -> list[tuple[float, float, float, float]]:
    """-> [(t, x, y, speed)]"""
    return [(fr["sim_time"], fr["ego"]["x"], fr["ego"]["y"], fr["ego"]["speed"]) for fr in frames]


def ego_track_from_osi(osi_path: Path) -> list[tuple[float, float, float, float]]:
    """Extract the host/ego trajectory from a length-delimited .osi trace
    ([uint32 size][GroundTruth] per frame)."""
    from osi3.osi_groundtruth_pb2 import GroundTruth

    track: list[tuple[float, float, float, float]] = []
    data = osi_path.read_bytes()
    off, n = 0, len(data)
    gt = GroundTruth()
    while off + 4 <= n:
        size = struct.unpack_from("I", data, off)[0]
        off += 4
        if off + size > n:
            break
        gt.Clear()
        gt.ParseFromString(data[off:off + size])
        off += size
        if not gt.moving_object:
            continue
        host_id = gt.host_vehicle_id.value if gt.HasField("host_vehicle_id") else None
        ego = None
        if host_id is not None:
            for o in gt.moving_object:
                if o.id.value == host_id:
                    ego = o
                    break
        if ego is None:
            ego = gt.moving_object[0]
        t = gt.timestamp.seconds + gt.timestamp.nanos * 1e-9
        v = ego.base.velocity
        track.append((t, ego.base.position.x, ego.base.position.y,
                      math.sqrt(v.x ** 2 + v.y ** 2 + v.z ** 2)))
    return track


def resolve_baseline_osi(baseline: Path) -> Path:
    if baseline.is_file():
        return baseline
    cand = baseline / "groundtruth.osi"
    if cand.is_file():
        return cand
    raise FileNotFoundError(f"No baseline .osi found at {baseline}")


# ---------------------------------------------------------------------------
# compare
# ---------------------------------------------------------------------------

def interp(track: list[tuple[float, float, float, float]], t: float) -> tuple[float, float, float]:
    """Linear interp of (x,y,speed) at time t (clamped to track ends)."""
    if t <= track[0][0]:
        return track[0][1], track[0][2], track[0][3]
    if t >= track[-1][0]:
        return track[-1][1], track[-1][2], track[-1][3]
    lo, hi = 0, len(track) - 1
    while hi - lo > 1:
        mid = (lo + hi) // 2
        if track[mid][0] <= t:
            lo = mid
        else:
            hi = mid
    t0, x0, y0, s0 = track[lo]
    t1, x1, y1, s1 = track[hi]
    a = (t - t0) / (t1 - t0) if t1 > t0 else 0.0
    return x0 + a * (x1 - x0), y0 + a * (y1 - y0), s0 + a * (s1 - s0)


def compare(run_dir: Path, baseline: Path, grid_dt: float = 0.1) -> dict:
    """Compare a VD run's telemetry against a Default-baseline .osi trace.
    Writes compare.json and baseline_track.json into run_dir; returns the metrics."""
    vd = ego_track_from_telemetry(load_telemetry(run_dir))
    base = ego_track_from_osi(resolve_baseline_osi(baseline))
    if not vd or not base:
        raise RuntimeError("empty trajectory (vd or baseline)")

    t_start = max(vd[0][0], base[0][0])
    t_end = min(vd[-1][0], base[-1][0])
    if t_end <= t_start:
        raise RuntimeError("no overlapping time range between run and baseline")

    n = int((t_end - t_start) / grid_dt) + 1
    sq_xy = sq_sp = 0.0
    max_xy = 0.0
    for i in range(n):
        t = t_start + i * grid_dt
        vx, vy, vs = interp(vd, t)
        bx, by, bs = interp(base, t)
        d2 = (vx - bx) ** 2 + (vy - by) ** 2
        sq_xy += d2
        max_xy = max(max_xy, math.sqrt(d2))
        sq_sp += (vs - bs) ** 2

    result = {
        "run": str(run_dir),
        "baseline": str(baseline),
        "overlap_s": round(t_end - t_start, 2),
        "samples": n,
        "xy_rmse_m": round(math.sqrt(sq_xy / n), 4),
        "xy_max_dev_m": round(max_xy, 4),
        "speed_rmse_mps": round(math.sqrt(sq_sp / n), 4),
        "endpoint_dist_m": round(math.dist((vd[-1][1], vd[-1][2]), (base[-1][1], base[-1][2])), 4),
    }
    (run_dir / "compare.json").write_text(json.dumps(result, indent=2), encoding="utf-8")

    # Baseline ego track resampled onto the VD frame times, so the replay UI can
    # overlay the Default "ghost" by simple index (baseline_track[i] <-> frames[i]).
    baseline_track = []
    for (t, _x, _y, _s) in vd:
        bx, by, bs = interp(base, t)
        baseline_track.append({"t": round(t, 3), "x": round(bx, 3), "y": round(by, 3), "speed": round(bs, 3)})
    (run_dir / "baseline_track.json").write_text(
        json.dumps(baseline_track, separators=(",", ":")), encoding="utf-8")
    return result


# ---------------------------------------------------------------------------
# assert (expectations.yaml)
# ---------------------------------------------------------------------------

def time_window_ok(t: float, spec: dict) -> bool:
    """Honour optional after/before sim_time gates on a must entry."""
    after = spec.get("after", {})
    before = spec.get("before", {})
    if "sim_time" in after and not (t >= after["sim_time"]):
        return False
    if "sim_time" in before and not (t <= before["sim_time"]):
        return False
    return True


def _speed_accel_jerk(frames: list[dict], smooth_window: int = 5) -> dict:
    """Derive (t, v, s, a, j) series for the mid/long matchers, anchored on the
    face-1 OSI scene wherever available (via _ego_state).

    Speed `v` and along-lane `s` come from _ego_state (scene-preferred; `s` is
    telemetry-only, OSI carries no host s). Acceleration `a` PREFERS the OSI
    longitudinal acceleration (base.acceleration projected on heading,
    GT_OSIReporter_Moving.cpp:772-774): when every frame carries it, `a` is that
    face-1 signal, smoothed with the same centered window used for speed;
    otherwise it falls back to central-differencing the smoothed speed (face-2 —
    telemetry has no acceleration, the historical path). Jerk `j` is the central
    difference of `a`. Endpoints clamp to neighbours. Returns equal-length lists
    t/v/s/a/j (n = len(frames)) plus `a_source` (osi | telemetry)."""
    n = len(frames)
    states = [_ego_state(fr) for fr in frames]
    t = [fr["sim_time"] for fr in frames]
    v_raw = [st["speed"] for st in states]
    s = [st["s"] if st["s"] is not None else 0.0 for st in states]
    a_osi = [st["accel_long"] for st in states]

    w = max(1, int(smooth_window))
    if w % 2 == 0:
        w += 1
    half = w // 2

    def _smooth(y: list[float]) -> list[float]:
        out = []
        for i in range(n):
            seg = y[max(0, i - half):min(n, i + half + 1)]
            out.append(sum(seg) / len(seg))
        return out

    def _central(y: list[float]) -> list[float]:
        d = [0.0] * n
        for i in range(1, n - 1):
            dt = t[i + 1] - t[i - 1]
            d[i] = (y[i + 1] - y[i - 1]) / dt if dt > 1e-9 else 0.0
        if n > 2:
            d[0], d[-1] = d[1], d[-2]
        return d

    v = _smooth(v_raw)
    if n >= 3 and all(a is not None for a in a_osi):
        a = _smooth(a_osi)          # face-1 OSI acceleration, same smoothing as v
        a_source = "osi"
    else:
        a = _central(v)             # face-2 fallback: central-difference of speed
        a_source = "telemetry"
    j = _central(a)
    return {"t": t, "v": v, "s": s, "a": a, "j": j, "a_source": a_source}


def _percentile(values: list[float], pct: float) -> float:
    """Nearest-rank percentile of a non-empty list (pct in [0,100])."""
    s = sorted(values)
    if len(s) == 1:
        return s[0]
    k = max(0, min(len(s) - 1, int(round((pct / 100.0) * (len(s) - 1)))))
    return s[k]


def _sustained_stop(frames: list[dict], must: dict, stop_speed: float):
    """Longest contiguous (by frame index) run within the must's window where
    ego.speed <= stop_speed. Window = after/before sim_time gates + optional
    road_id + s_range on the ego road frame. Returns
    (start_idx, end_idx, duration_s, first_gated_idx) or None if the window is
    never entered."""
    road_id = must.get("road_id")
    s_range = must.get("s_range")
    gated = []
    for i, fr in enumerate(frames):
        if not time_window_ok(fr["sim_time"], must):
            continue
        ego = fr["ego"]
        if road_id is not None and int(ego.get("track", -10 ** 9)) != road_id:
            continue
        if s_range is not None and "s" in ego and not (s_range[0] <= ego["s"] <= s_range[1]):
            continue
        gated.append(i)
    if not gated:
        return None

    best = None  # (start_idx, end_idx, duration)
    run_start = None
    prev = None
    for i in gated:
        stopped = frames[i]["ego"]["speed"] <= stop_speed
        contiguous = prev is not None and i == prev + 1
        if stopped and run_start is not None and contiguous:
            pass  # extend current run
        elif stopped:
            run_start = i  # (re)start a run
        if stopped:
            dur = frames[i]["sim_time"] - frames[run_start]["sim_time"]
            if best is None or dur > best[2]:
                best = (run_start, i, dur)
        else:
            run_start = None
        prev = i

    if best is None:
        return (gated[0], gated[0], 0.0, gated[0])
    return (best[0], best[1], best[2], gated[0])


def _closing_speed(ego: dict, obj: dict) -> float:
    """Closing (impact) speed between two scene bodies at a contact frame: the
    rate at which the ego<->object center-to-center separation is shrinking,
    clamped to >= 0 (a non-approaching pair has no "impact speed").

    Projects the relative velocity onto the line connecting the two body
    centers. Each body's velocity vector comes from the scene's raw `vx,vy`
    (the OSI velocity, GT_OSIReporter_Moving.cpp:767-769) when present; that is
    the only form that keeps the sign of a body moving against its own heading,
    which is exactly the case a pedestrian stepping backwards off the road, or a
    reversing vehicle, produces. Older telemetry captured before the scene
    carried vx/vy falls back to reconstructing the vector from the scalar
    `speed` and heading `h`, and finally to the plain scalar difference
    (ego.speed - obj.speed) when a heading is missing or the two centers
    coincide (direction undefined). For a body travelling along its own heading
    the vector and the reconstruction agree exactly."""
    dx, dy = obj["x"] - ego["x"], obj["y"] - ego["y"]
    dist = math.hypot(dx, dy)

    def _vec(body: dict):
        if body.get("vx") is not None and body.get("vy") is not None:
            return body["vx"], body["vy"]
        h = body.get("h")
        if h is None:
            return None
        speed = body.get("speed", 0.0)
        return speed * math.cos(h), speed * math.sin(h)

    ev, ov = _vec(ego), _vec(obj)
    if dist > 1e-6 and ev is not None and ov is not None:
        ux, uy = dx / dist, dy / dist  # unit vector ego -> object
        closing = (ev[0] - ov[0]) * ux + (ev[1] - ov[1]) * uy
    else:
        closing = ego.get("speed", 0.0) - obj.get("speed", 0.0)
    return max(0.0, closing)


def _ego_state(fr: dict) -> dict:
    """Resolve the host-vehicle anchor, preferring the face-1 OSI GroundTruth
    scene over the face-2 VD telemetry ego.

    Position / speed / heading and road_id (via the scene lane_map) come from
    the is_host moving object of frame["scene"]; longitudinal acceleration comes
    from that object's OSI acceleration vector (ax, ay projected on the heading,
    GT_OSIReporter_Moving.cpp:772-774). When no scene was captured (--osi off)
    every field falls back to the telemetry ego.

    Two fields are telemetry-only ON PURPOSE (documented face-1 gaps, not laziness):
      * `s` (along-lane distance): OSI GroundTruth does not populate MovingObject
        s_position (capability_model.md §2.3a), so the scene cannot supply it.
      * `lane`: OSI moving_object.assigned_lane_id USED to measure a different
        quantity than the VD's tracked Position lane — it re-derived the lane from
        (s, t) via GetLaneGlobalId(), so a laterally-drifting driving vehicle was
        reported as assigned to a border/sidewalk lane (red_stop_green_go 2026-07-21:
        assigned lane drifted -1 -> -2 -> -3 while telemetry held -1, 62% mismatch).
        That defect was FIXED GT-side (2026-07-21, spine-work:osi-assigned-lane-driving):
        the moving-object assigned_lane_id now emits the object's cached DRIVING lane
        (see GT_OSIReporter_Moving.cpp ResolveMovingObjectAssignedLaneGlobalId), so it
        no longer drifts and now agrees with the VD Position lane (same scenario:
        1200/1200 frames). `lane` is nonetheless STILL read from telemetry here: moving
        it to the face-1 scene is a deliberate, separate follow-on, because the
        lane_keep / lane_change_count assertions were authored against the VD Position
        lane and the regression baselines must be re-checked before the switch. The
        lane_map join keeps supplying road_id (matched telemetry track 777/777,
        including across a real road transition).

    `accel_long` is None when the scene is absent. The chosen face is recorded in
    "_source" (scene | telemetry) so the verdict can surface which one fed the
    anchor.

    Both the fr.get("scene") and the fr["ego"] reads live in this one helper by
    design: check_knowledge_graph.py (_inlined_helpers) inlines it one level into
    every matcher branch that calls _ego_state(...), so the OSI-preferred /
    telemetry-fallback coupling is attributed to that matcher instead of hidden
    behind the call — the scene-preferred read must not be buried two levels deep."""
    ego = fr["ego"]
    scene = fr.get("scene")
    src = "telemetry"
    x, y, speed, h = ego["x"], ego["y"], ego["speed"], ego.get("h", 0.0)
    track, lane, accel_long = ego.get("track"), ego.get("lane"), None
    if scene:
        host = next((o for o in scene.get("objects", []) if o.get("is_host")), None)
        if host is not None:
            src = "scene"
            x, y, speed = host["x"], host["y"], host["speed"]
            h = host.get("h", h)
            gid = host.get("lane_global_id")
            entry = (scene.get("lane_map") or {}).get(str(gid)) if gid is not None else None
            if entry is not None and entry.get("road_id") is not None:
                track = entry["road_id"]   # road_id only; lane stays telemetry (see docstring)
            ax, ay = host.get("ax"), host.get("ay")
            if ax is not None and ay is not None:
                accel_long = ax * math.cos(h) + ay * math.sin(h)
    return {"x": x, "y": y, "speed": speed, "h": h, "s": ego.get("s"),
            "track": track, "lane": lane, "accel_long": accel_long, "_source": src}


def eval_must(must: dict, frames: list[dict]) -> dict:
    """Evaluate one must[] entry. Fail results carry the first offending frame's
    `t` and `idx` so the UI can jump straight to the failure. Matchers that
    anchor on the host vehicle resolve it through _ego_state (face-1 OSI scene
    preferred, face-2 telemetry fallback) and pass the chosen face as
    `ego_source`, which the verdict carries so a telemetry fallback stays
    visible (never silently presented as a face-1 result)."""
    kind = must.get("event")
    reason = must.get("reason", "")

    def res(status, detail, fail_idx=None, ego_source=None):
        out = {"event": kind, "status": status, "detail": detail, "reason": reason}
        if ego_source is not None:
            out["ego_source"] = ego_source
        if status == "fail" and fail_idx is not None:
            out["idx"] = fail_idx
            out["t"] = round(frames[fail_idx]["sim_time"], 3)
        return out

    if kind in ("speed_above", "speed_below"):
        thr = float(must["threshold"])
        gated = [(i, _ego_state(frames[i])) for i in range(len(frames))
                 if time_window_ok(frames[i]["sim_time"], must)]
        if not gated:
            return res("skip", "no frames in time window")
        src = gated[0][1]["_source"]
        speeds = {i: eg["speed"] for i, eg in gated}
        # NB: a bare `if kind == "speed_above"` here would read as a second
        # matcher-branch head to the coupling lint and mask this branch's
        # _ego_state read; use a boolean so the whole branch stays one unit.
        want_above = kind == "speed_above"
        if want_above:
            ok = any(s >= thr for s in speeds.values())
            worst_i = max(speeds, key=speeds.get)  # closest attempt
            detail = f"max speed in window = {speeds[worst_i]:.2f} (>= {thr}?)"
            return res("pass" if ok else "fail", detail, None if ok else worst_i, ego_source=src)
        else:
            offenders = [i for i, s in speeds.items() if s > thr]
            ok = not offenders
            worst = max(speeds.values())
            detail = f"max speed in window = {worst:.2f} (<= {thr}?)"
            return res("pass" if ok else "fail", detail, None if ok else offenders[0], ego_source=src)

    if kind == "min_speed_above":
        # Lowest speed in the window must stay above threshold: a "do not slow
        # down here" assertion (e.g. crossing a junction the ego drives straight
        # through). Optional road_id confines the window to one road/connector.
        thr = float(must["threshold"])
        road_id = must.get("road_id")
        states = {i: _ego_state(frames[i]) for i in range(len(frames))
                  if time_window_ok(frames[i]["sim_time"], must)}
        gated = [i for i, eg in states.items()
                 if road_id is None or int(eg["track"] if eg["track"] is not None
                                           else -10 ** 9) == road_id]
        if not gated:
            where = f" on road {road_id}" if road_id is not None else ""
            return res("skip", f"no frames in time window{where}")
        src = states[gated[0]]["_source"]
        worst_i = min(gated, key=lambda i: states[i]["speed"])
        v_min = states[worst_i]["speed"]
        ok = v_min >= thr
        detail = f"min speed in window = {v_min:.2f} (>= {thr}?)"
        return res("pass" if ok else "fail", detail, None if ok else worst_i, ego_source=src)

    if kind == "no_constraint_kind":
        # Assert the mid/long planner never raises a constraint of the given kind
        # in the window. Reads telemetry midlong.constraints[].kind. Used to prove
        # a straight pass-through junction does not emit a "junction" constraint.
        target = must.get("kind")
        if target is None:
            return res("skip", "kind is required")
        gated = [i for i in range(len(frames)) if time_window_ok(frames[i]["sim_time"], must)]
        if not gated:
            return res("skip", "no frames in time window")
        offenders = [i for i in gated
                     if any(c.get("kind") == target
                            for c in frames[i].get("midlong", {}).get("constraints", []))]
        ok = not offenders
        detail = (f"{len(gated)} frames checked; {len(offenders)} raised a "
                  f"'{target}' constraint")
        return res("pass" if ok else "fail", detail, None if ok else offenders[0])

    # Lane events use the ego's road-coordinate anchor (road_id / lane). It is
    # resolved through _ego_state, which prefers the face-1 OSI scene: the
    # is_host object's lane_global_id joined against scene["lane_map"] (built
    # from OSI Lane.source_reference) yields OpenDRIVE road_id/lane_id, falling
    # back to the telemetry ego.track/ego.lane. A frame lacking both skips.
    # (Building the series inline in each branch — rather than via a shared
    # nested helper — keeps the _ego_state read inside the matcher's own branch,
    # where the coupling lint's one-level inlining can see it.)
    if kind == "lane_keep":
        road_id = must.get("road_id")
        lane_id = must.get("lane_id")
        ls, src = [], "telemetry"
        for i, fr in enumerate(frames):
            if not time_window_ok(fr["sim_time"], must):
                continue
            eg = _ego_state(fr)
            if eg["lane"] is None or eg["track"] is None:
                continue
            src = eg["_source"]
            ls.append((i, int(eg["track"]), int(eg["lane"])))
        if not ls:
            return res("skip", "no lane data in window (lane/track absent or empty window)")
        bad = [i for (i, trk, ln) in ls
               if (road_id is not None and trk != road_id) or (lane_id is not None and ln != lane_id)]
        detail = (f"{len(ls)} frames in window on lane(s) "
                  f"{sorted(set(ln for _, _, ln in ls))} road(s) {sorted(set(trk for _, trk, _ in ls))}; "
                  f"expected road={road_id} lane={lane_id}")
        return res("pass" if not bad else "fail", detail, None if not bad else bad[0], ego_source=src)

    if kind == "lane_change_count":
        expected = must.get("count")
        ls, src = [], "telemetry"
        for i, fr in enumerate(frames):
            eg = _ego_state(fr)
            if eg["lane"] is None or eg["track"] is None:
                continue
            src = eg["_source"]
            ls.append((i, int(eg["track"]), int(eg["lane"])))
        if not ls:
            return res("skip", "no lane data")
        changes = [ls[i][0] for i in range(1, len(ls)) if ls[i][2] != ls[i - 1][2]]
        ok = (expected is None) or (len(changes) == expected)
        detail = f"observed {len(changes)} lane change(s); expected {expected}"
        return res("pass" if ok else "fail", detail,
                   None if ok else (changes[0] if changes else None), ego_source=src)

    # --- mid/long anticipation matchers (V2) ---------------------------------
    # "Physically plausible deceleration" judged from VirtualDriver telemetry
    # alone (position equivalence vs Default is meaningless for the mid/long case).

    if kind == "deceleration_profile_smooth":
        gated = [i for i in range(len(frames)) if time_window_ok(frames[i]["sim_time"], must)]
        if len(gated) < 3:
            return res("skip", "fewer than 3 frames in time window")
        prof = _speed_accel_jerk(frames, int(must.get("smooth_window", 5)))
        # Anchor road_id/heading on the face-1 scene where captured (falls back
        # to telemetry). `a_src` tells whether the acceleration judged below is
        # the OSI signal or the telemetry central-difference fallback.
        egos = {i: _ego_state(frames[i]) for i in gated}
        src = egos[gated[0]]["_source"]
        a_src = prof["a_source"]

        # Scope jerk/decel to the deceleration approach: [onset -> landmark
        # passage]. This excludes the launch-from-rest spike, the post-landmark
        # re-acceleration, and the recording-tail boundary artifact, none of
        # which are part of the deceleration profile being judged. (Reaching the
        # target speed by the landmark is asserted separately by
        # speed_reduction_before_landmark.)
        road_id = must.get("road_id")
        landmark_s = must.get("landmark_s")
        eval_idx, win = gated, f" [a={a_src}]"
        if landmark_s is not None:
            lm = next((i for i in gated
                       if (road_id is None
                           or int(egos[i]["track"] if egos[i]["track"] is not None else -10 ** 9) == road_id)
                       and prof["s"][i] >= float(landmark_s)), None)
            if lm is not None:
                onset = next((i for i in gated if i <= lm and prof["a"][i] < -0.3), None)
                if onset is not None and lm - onset >= 2:
                    eval_idx = [i for i in gated if onset <= i <= lm]
                    win = (f" [decel phase t{frames[onset]['sim_time']:.1f}-"
                           f"{frames[lm]['sim_time']:.1f}s, a={a_src}]")

        max_jerk = must.get("max_jerk")
        if max_jerk is not None:
            worst = max(eval_idx, key=lambda i: abs(prof["j"][i]))
            if abs(prof["j"][worst]) > float(max_jerk):
                return res("fail",
                           f"max |jerk| = {abs(prof['j'][worst]):.2f} m/s^3 (<= {max_jerk}?){win}",
                           worst, ego_source=src)

        max_decel = must.get("max_decel")
        if max_decel is not None:
            worst = min(eval_idx, key=lambda i: prof["a"][i])  # most negative accel
            if -prof["a"][worst] > float(max_decel):
                return res("fail",
                           f"max deceleration = {-prof['a'][worst]:.2f} m/s^2 (<= {max_decel}?){win}",
                           worst, ego_source=src)

        wj = max(eval_idx, key=lambda i: abs(prof["j"][i]))
        wa = min(eval_idx, key=lambda i: prof["a"][i])
        return res("pass", f"max |jerk|={abs(prof['j'][wj]):.2f} m/s^3, "
                           f"max decel={-prof['a'][wa]:.2f} m/s^2 within bounds{win}",
                   ego_source=src)

    if kind == "speed_reduction_before_landmark":
        landmark_s = must.get("landmark_s")
        target_speed = must.get("target_speed")
        if landmark_s is None or target_speed is None:
            return res("skip", "landmark_s and target_speed are required")
        tol = float(must.get("tolerance", 0.5))
        road_id = must.get("road_id")
        hit, hit_eg = None, None
        for i in range(len(frames)):
            if not time_window_ok(frames[i]["sim_time"], must):
                continue
            eg = _ego_state(frames[i])
            if eg["s"] is None:  # along-lane s is telemetry-only (not in OSI)
                continue
            if road_id is not None and int(eg["track"] if eg["track"] is not None else -10 ** 9) != road_id:
                continue
            if eg["s"] >= float(landmark_s):
                hit, hit_eg = i, eg
                break
        if hit is None:
            where = f" on road {road_id}" if road_id is not None else ""
            return res("skip", f"landmark s={landmark_s}{where} not reached")
        v_hit = hit_eg["speed"]
        ok = v_hit <= float(target_speed) + tol
        detail = f"speed at landmark s={landmark_s} = {v_hit:.2f} m/s (<= {target_speed}+{tol}?)"
        return res("pass" if ok else "fail", detail, None if ok else hit, ego_source=hit_eg["_source"])

    if kind == "steer_not_saturated":
        thr = float(must.get("threshold", 0.98))
        gated = [i for i in range(len(frames))
                 if time_window_ok(frames[i]["sim_time"], must)
                 and frames[i].get("driver", {}).get("steer") is not None]
        if not gated:
            return res("skip", "no driver.steer data in window")
        offenders = [i for i in gated if abs(frames[i]["driver"]["steer"]) > thr]
        worst = max(gated, key=lambda i: abs(frames[i]["driver"]["steer"]))
        detail = f"max |steer| in window = {abs(frames[worst]['driver']['steer']):.3f} (<= {thr}?)"
        return res("pass" if not offenders else "fail", detail, None if not offenders else offenders[0])

    # --- Phase 3 traffic-policy matchers (Step 1) ---------------------------
    # stopped_at_stop_sign / stopped_at_signal: a full stop sustained for
    # >= min_duration within an s-window. The sign/signal is identified by id for
    # documentation; the geometric anchor is road_id + s_range (the stop line).

    if kind in ("stopped_at_stop_sign", "stopped_at_signal"):
        min_duration = float(must.get("min_duration", 1.0))
        stop_speed = float(must.get("stop_speed", 0.3))
        run = _sustained_stop(frames, must, stop_speed)
        if run is None:
            return res("skip", "stop window never entered (road_id/s_range/time gate)")
        start_i, _end_i, dur, first_i = run
        ok = dur >= min_duration
        ident = (f"sign {must.get('sign_id')}" if kind == "stopped_at_stop_sign"
                 else f"signal {must.get('signal_id')}")
        detail = (f"longest full stop (<= {stop_speed} m/s) at {ident} = {dur:.2f}s "
                  f"(>= {min_duration}?)")

        # stopped_at_stop_sign: optionally confirm a stop/give-way sign actually
        # exists in the captured OSI scene, i.e. that the geometric s_range
        # anchor really is a signed stop line and not just a spot where the ego
        # happened to halt. Same best-effort contract as require_red below: the
        # sub-check only ever fires when the scene positively contradicts the
        # expectation, because esmini leaves signs its country catalogue does
        # not know as an "unmapped:" sentinel rather than as stop/give_way.
        if ok and kind == "stopped_at_stop_sign" and must.get("require_sign", True):
            scene = frames[start_i].get("scene")
            signs = (scene or {}).get("traffic_signs") or []
            if signs:
                sid = must.get("sign_id")
                stopish = [s["id"] for s in signs if s.get("type") in ("stop", "give_way")]
                ids = [s["id"] for s in signs]
                sign_ok = (sid in stopish) or (sid not in ids and len(stopish) > 0)
                if not sign_ok:
                    return res("fail", detail + f"; but no stop/give-way sign in the OSI "
                                                f"scene (stop-ish ids={stopish})", start_i)
                detail += "; stop sign confirmed in scene"

        # stopped_at_signal: optionally confirm the signal was red at stop onset
        # using the captured OSI scene. OSI traffic-light id<->signal id mapping
        # can vary, so this is best-effort: fail only when reds exist but none
        # match; skip the sub-check entirely if no scene was captured.
        if ok and kind == "stopped_at_signal" and must.get("require_red", True):
            scene = frames[start_i].get("scene")
            if scene is not None:
                tls = scene.get("traffic_lights", [])
                # With several heads in one junction, `lane_id` picks the ones
                # OSI says govern that lane instead of colour-voting over every
                # head in sight (traffic_light.classification.assigned_lane_id).
                want_lane = must.get("lane_id")
                if want_lane is not None:
                    on_lane = [t for t in tls if want_lane in (t.get("assigned_lane_ids") or [])]
                    if on_lane:
                        tls = on_lane
                if tls:
                    sig = must.get("signal_id")
                    reds = [t["id"] for t in tls if t.get("color") == "red"]
                    ids = [t["id"] for t in tls]
                    red_ok = (sig in reds) or (sig not in ids and len(reds) > 0)
                    if not red_ok:
                        return res("fail", detail + f"; but signal was not red at stop onset "
                                                    f"(reds={reds})", start_i)
                    detail += "; red confirmed at onset"
        return res("pass" if ok else "fail", detail, None if ok else first_i)

    if kind == "maintained_following_distance":
        # Time-headway (THW) to the nearest lead in the same lane, from the OSI
        # scene. Requires capture_osi (telemetry.scene). THW = gap / ego_speed,
        # gap = forward distance to lead minus half the two body lengths.
        min_thw = must.get("min_thw")
        max_thw = must.get("max_thw")
        pct = float(must.get("percentile", 50))
        target_id = must.get("target_id")
        lane_half = float(must.get("lane_half_width", 2.5))
        eps, stop_speed = 0.1, 0.3
        thws, src = [], "telemetry"
        for i, fr in enumerate(frames):
            if not time_window_ok(fr["sim_time"], must):
                continue
            scene = fr.get("scene")
            if not scene:
                continue
            # Anchor the ego on the SAME face-1 scene frame as the lead (was
            # telemetry ego vs scene lead — the mixed-basis THW §2.3a flagged);
            # _ego_state falls back to telemetry when no scene is present, but
            # this branch already requires a scene, so here it is the is_host.
            eg = _ego_state(fr)
            src = eg["_source"]
            v = eg["speed"]
            if v < stop_speed:
                continue  # standstill -> THW undefined
            ch, sh = math.cos(eg["h"]), math.sin(eg["h"])
            ego_len = next((o["length"] for o in scene["objects"] if o.get("is_host")), 5.0)
            best = None  # (forward, lead_len)
            for o in scene["objects"]:
                if o.get("is_host"):
                    continue
                if target_id is not None and o["id"] != target_id:
                    continue
                dx, dy = o["x"] - eg["x"], o["y"] - eg["y"]
                forward = dx * ch + dy * sh
                lateral = -dx * sh + dy * ch
                if forward <= 0 or abs(lateral) > lane_half:
                    continue
                if best is None or forward < best[0]:
                    best = (forward, o.get("length", 4.0))
            if best is None:
                continue
            gap = best[0] - (ego_len + best[1]) / 2.0
            if gap <= 0:
                gap = 0.0
            thws.append(gap / max(v, eps))
        if not thws:
            return res("skip", "no lead-vehicle frames with a captured scene "
                               "(needs --osi / batch osi:true and a lead in lane)")
        val = _percentile(thws, pct)
        lo_ok = (min_thw is None) or (val >= float(min_thw))
        hi_ok = (max_thw is None) or (val <= float(max_thw))
        detail = (f"p{pct:g} THW = {val:.2f}s over {len(thws)} frames "
                  f"(want {min_thw}..{max_thw}s)")
        return res("pass" if (lo_ok and hi_ok) else "fail", detail, ego_source=src)

    if kind == "min_separation_above":
        # Anti-collision gate: over the window, the minimum center-to-center
        # distance between the ego (is_host) and EVERY other scene object must stay
        # >= threshold. Requires capture_osi (telemetry.scene). Unlike a speed proxy
        # this cannot false-positive: it measures the actual closing distance, so it
        # catches a collision regardless of how the ego moves.
        thr = float(must["threshold"])
        worst_sep = None  # (sep, frame_idx)
        for i, fr in enumerate(frames):
            if not time_window_ok(fr["sim_time"], must):
                continue
            scene = fr.get("scene")
            if not scene:
                continue
            ego = next((o for o in scene["objects"] if o.get("is_host")), None)
            if ego is None:
                continue
            for o in scene["objects"]:
                if o.get("is_host"):
                    continue
                sep = math.hypot(o["x"] - ego["x"], o["y"] - ego["y"])
                if worst_sep is None or sep < worst_sep[0]:
                    worst_sep = (sep, i)
        if worst_sep is None:
            return res("skip", "no scene frames in time window")
        sep, worst_i = worst_sep
        ok = sep >= thr
        detail = (f"min center-to-center separation = {sep:.2f} m at "
                  f"t={frames[worst_i]['sim_time']:.2f} (>= {thr}?)")
        return res("pass" if ok else "fail", detail, None if ok else worst_i)

    if kind == "min_obb_separation_above":
        # Anti-collision gate, OBB (oriented bounding box) edition. Over the window,
        # the minimum SAT separation between the ego (is_host) length x width
        # footprint and EVERY other scene object footprint must stay >= threshold.
        # 0 (or negative) means the oriented rectangles OVERLAP (collision); a small
        # positive threshold adds a safety gap. Unlike center-to-center distance this
        # correctly reads adjacent-lane passing (~2.8 m center gap) as SAFE — the
        # bodies do not overlap. Requires capture_osi (telemetry.scene).
        thr = float(must["threshold"])
        worst = None  # (sep, frame_idx)
        any_overlap = False
        # Track fabricated dimensions: an object with dims_fallback (or missing
        # length/width) had no real OSI extents, so its footprint is a 4.0x2.0 m
        # guess. A measured PASS on such a footprint is untrustworthy (a larger real
        # body could overlap where the default clears) -> we must not report clean.
        fallback_names: set = set()

        def _is_fallback(o: dict) -> bool:
            return bool(o.get("dims_fallback")) or "length" not in o or "width" not in o

        for i, fr in enumerate(frames):
            if not time_window_ok(fr["sim_time"], must):
                continue
            scene = fr.get("scene")
            if not scene:
                continue
            ego = next((o for o in scene["objects"] if o.get("is_host")), None)
            if ego is None:
                continue
            for o in scene["objects"]:
                if o.get("is_host"):
                    continue
                if _is_fallback(ego):
                    fallback_names.add(ego.get("name") or f"host#{ego.get('id')}")
                if _is_fallback(o):
                    fallback_names.add(o.get("name") or f"#{o.get('id')}")
                sep = obb_separation(ego, o)
                if sep <= 0.0:
                    any_overlap = True
                if worst is None or sep < worst[0]:
                    worst = (sep, i)
        if worst is None:
            return res("skip", "no scene frames in time window")
        sep, worst_i = worst
        ok = sep >= thr
        # A real measured pass requires real dims. If an involved body carried
        # fallback extents, a clean result is inconclusive — report skip (which the
        # verdict rolls up to needs-review) rather than a false green. An overlap /
        # fail is still authoritative: the default footprint is a LOWER bound, so an
        # overlap on it implies overlap on the (larger-or-equal) real body too.
        if ok and fallback_names:
            names = ", ".join(sorted(fallback_names))
            return res("skip", f"OBB separation inconclusive: object(s) lacked real "
                               f"OSI dimensions ({names}); measured min "
                               f"{sep:.2f} m uses a 4.0x2.0 m fallback footprint, so a "
                               f"clean pass is not trustworthy")
        detail = (f"min OBB separation = {sep:.2f} m at "
                  f"t={frames[worst_i]['sim_time']:.2f} (>= {thr}?); "
                  f"overlap occurred: {any_overlap}")
        return res("pass" if ok else "fail", detail, None if ok else worst_i)

    if kind == "impact_speed_below":
        # AEB mitigation gate (Euro-NCAP colour-band philosophy): when full
        # collision avoidance is physically impossible (closing speed exceeds
        # what the vehicle's braking limit can shed before the gap runs out),
        # acceptance shifts from "never touch" to "IF contact occurs, the
        # closing speed at first contact must be below a floor". AEB is judged
        # by how much it cut the impact speed, not only by zero-contact.
        #
        # Reuses obb_separation() (the same SAT routine as
        # min_obb_separation_above, above) to find the FIRST frame where the
        # ego and some other body's OBBs are within contact_sep of each other,
        # identifies which body that is (the one at minimum separation on that
        # frame), then reports the closing speed at that one frame - later
        # frames are not scored (esmini has no collision response, so bodies
        # can keep interpenetrating; only the moment of first contact is a
        # physically meaningful "impact speed"). Requires capture_osi
        # (telemetry.scene), same as its sibling anti-collision gates.
        thr = float(must["threshold"])
        contact_sep = float(must.get("contact_sep", 0.0))
        gated = [i for i in range(len(frames)) if time_window_ok(frames[i]["sim_time"], must)]
        if not gated:
            return res("skip", "no frames in time window")

        any_scene = False
        best_sep = None   # closest approach ever seen (for the no-contact detail)
        contact = None    # (idx, sep, ego, obj) at the first contact frame
        for i in gated:
            scene = frames[i].get("scene")
            if not scene:
                continue
            ego = next((o for o in scene["objects"] if o.get("is_host")), None)
            if ego is None:
                continue
            any_scene = True
            frame_worst = None  # (sep, obj) = the closest other body this frame
            for o in scene["objects"]:
                if o.get("is_host"):
                    continue
                sep = obb_separation(ego, o)
                if frame_worst is None or sep < frame_worst[0]:
                    frame_worst = (sep, o)
            if frame_worst is None:
                continue  # single-object scene this frame (no partner) -> can't contact
            sep, o = frame_worst
            if best_sep is None or sep < best_sep:
                best_sep = sep
            if sep <= contact_sep:
                contact = (i, sep, ego, o)
                break  # first contact only; impact speed is judged here

        if not any_scene:
            return res("skip", "no scene frames in time window (needs --osi capture)")

        if contact is None:
            # Best case: full avoidance, including the degenerate case where no
            # other body was ever present in the captured scene.
            detail = (f"no contact (min separation {best_sep:.2f} m) -> pass"
                      if best_sep is not None else
                      "no contact (no other bodies observed in scene) -> pass")
            return res("pass", detail)

        idx, sep, c_ego, c_obj = contact
        closing = _closing_speed(c_ego, c_obj)
        ok = closing <= thr
        who = c_obj.get("name") or f"#{c_obj.get('id')}"
        t_contact = frames[idx]["sim_time"]
        detail = (f"impact speed = {closing:.2f} m/s at t={t_contact:.2f} "
                  f"(<= {thr}?) [contact with {who}, sep={sep:.2f} m]")
        return res("pass" if ok else "fail", detail, None if ok else idx)

    if kind == "no_emergency_without_conflict":
        # REQ-AD-013 (SOTIF negative, the misfire-avoidance mirror of the AEB
        # positive tests): AEB must never emit its SAFETY-tier emergency
        # STOP_AT_S constraint (PolicyConstraint::source == "aeb", see
        # AebSafety::Evaluate) unless a genuine collision course exists. This
        # matcher does not re-derive TTC/a_req itself - it just watches for the
        # observable effect of a misfire: an "aeb"-sourced entry in
        # policy.constraints on any telemetry frame.
        #
        # Reads frame["policy"]["constraints"], a list of {kind,s,value,source}
        # dicts - the union of all enabled traffic-policy constraints for that
        # frame, written by VirtualDriverTelemetryJson.cpp (ToJson(), the
        # ",\"policy\":{...}" tail) and passed through verbatim by
        # gt_sim_test.py's run loop (tel = lib.get_vd_telemetry(-1); no
        # reshaping). A frame missing "policy" or "constraints" (e.g. a
        # synthetic/older frame) is treated as carrying no aeb constraint,
        # not as skip - absence of the key is not evidence of a misfire.
        #
        # PASS: no frame in the (optionally after/before-windowed) range
        # carries a source=="aeb" constraint - AEB stayed dormant, whether or
        # not it was ever even admitted as a candidate.
        # FAIL: the earliest frame that does - a misfire - identified via the
        # standard res(fail_idx) contract so the UI can jump straight to it.
        gated = [i for i in range(len(frames)) if time_window_ok(frames[i]["sim_time"], must)]
        if not gated:
            return res("skip", "no frames in time window")
        offenders = [i for i in gated
                     if any(c.get("source") == "aeb"
                            for c in frames[i].get("policy", {}).get("constraints", []))]
        if offenders:
            i0 = offenders[0]
            detail = (f"AEB emergency fired at t={frames[i0]['sim_time']:.2f} "
                      f"(no collision course) -> misfire")
            return res("fail", detail, i0)
        detail = f"no AEB emergency constraint over {len(gated)} frames -> pass"
        return res("pass", detail)

    return {"event": kind, "status": "skip", "detail": "unknown event type", "reason": reason}


def assert_expectations(run_dir: Path, expectations: Path) -> dict:
    """Evaluate an expectations.yaml against run_dir's telemetry. Writes
    verdict.json into run_dir; returns the verdict dict."""
    import yaml

    frames = load_telemetry(run_dir)
    spec = yaml.safe_load(expectations.read_text(encoding="utf-8"))
    musts = spec.get("must", []) if isinstance(spec, dict) else []

    results = [eval_must(m, frames) for m in musts]
    n_pass = sum(1 for r in results if r["status"] == "pass")
    n_fail = sum(1 for r in results if r["status"] == "fail")
    n_skip = sum(1 for r in results if r["status"] == "skip")
    overall = "fail" if n_fail else ("pass" if n_pass and not n_skip else
                                     "needs-review" if n_skip else "pass")

    verdict = {
        "run": str(run_dir),
        "expectations": str(expectations),
        "scenario": spec.get("scenario") if isinstance(spec, dict) else None,
        "overall": overall,
        "summary": {"pass": n_pass, "fail": n_fail, "skip": n_skip},
        "results": results,
    }
    (run_dir / "verdict.json").write_text(json.dumps(verdict, indent=2), encoding="utf-8")
    return verdict


# ---------------------------------------------------------------------------
# OSI UDP capture (multi-packet GroundTruth reassembly -> .osi file)
# ---------------------------------------------------------------------------

def capture_osi(out_osi: Path, proc: subprocess.Popen, port: int, idle_timeout: float) -> int:
    """Reassemble multi-packet GroundTruth frames from UDP into a length-delimited
    .osi file. Stops once the process has exited and the stream is idle.
    Returns the number of complete frames written."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1 << 20)
    sock.bind(("127.0.0.1", port))
    sock.settimeout(0.5)

    frames = 0
    complete = b""
    next_index = 1
    last_data = time.time()
    try:
        with open(out_osi, "wb") as f:
            while True:
                try:
                    msg, _ = sock.recvfrom(OSI_BUFFER_SIZE)
                except socket.timeout:
                    if proc.poll() is not None and (time.time() - last_data) > idle_timeout:
                        break
                    continue
                last_data = time.time()
                if len(msg) < 8:
                    continue
                counter, _size = struct.unpack("iI", msg[:8])
                frame = msg[8:]
                if counter == 1:
                    complete = b""
                    next_index = 1
                if counter == 1 or abs(counter) == next_index:
                    complete += frame
                    next_index += 1
                    if counter < 0:  # final packet
                        f.write(struct.pack("I", len(complete)))
                        f.write(complete)
                        frames += 1
                        complete = b""
                        next_index = 1
                else:
                    next_index = 1
    finally:
        sock.close()
    return frames


__all__ = [
    "OSI_BUFFER_SIZE",
    "assert_expectations",
    "capture_osi",
    "compare",
    "ego_track_from_osi",
    "ego_track_from_telemetry",
    "eval_must",
    "interp",
    "load_telemetry",
    "obb_separation",
    "resolve_baseline_osi",
    "time_window_ok",
]
