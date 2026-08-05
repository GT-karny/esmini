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
# Port occupancy guard (feature:F7 gate hardening, moved here 2026-07-28)
#
# Originally lived only in GT_esmini/scripts/verification/gt_sim_test.py,
# reached from its run()/batch(). An audit found that placement still missed
# the ACTUAL 2026-07-27 incident party: services/vd_verify.py's
# generate_baseline() (launches GT_Sim.exe as a subprocess and captures OSI
# via capture_osi() below) never went anywhere near gt_sim_test.py, so it had
# no port defense at all -- "run() is the common path every launch route
# passes through" was false. This module (vd_metrics.py) is the one thing
# BOTH the CLI (gt_sim_test.py) and the web backend (vd_verify.py) already
# import as their shared verification core (see the module docstring above),
# so the check lives here now, called from capture_osi() itself -- the
# actual lowest layer that binds the port, not a caller that has to
# remember to invoke a separate guard first. gt_sim_test.py re-exports these
# names for its own run()/batch() (which also check the non-OSI ports; see
# that module) and for backward compatibility with its existing tests.
# ---------------------------------------------------------------------------
class GatePortsBusyError(RuntimeError):
    """Raised when a port an about-to-run operation needs is already
    occupied. A distinct type (not a bare RuntimeError/OSError) so a caller
    can give a clean "refused to run" message instead of a raw socket
    traceback, and so main()-style CLI wrappers can map it to a distinct
    "measured nothing" exit code."""


def _udp_port_busy(port: int) -> bool:
    """True if this process cannot bind ``port`` itself. bind-then-close is
    the same technique run_regression_gate.ps1's Get-NetUDPEndpoint check
    approximates from the OS side, done here directly against the socket API
    so it needs no Windows-specific tooling and fails the same way the real
    bind (capture_osi below, or the DLL) would."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.bind(("0.0.0.0", port))
        return False
    except OSError:
        return True
    finally:
        s.close()


def _tcp_port_listening(port: int) -> bool:
    """True if something is already accepting connections on
    127.0.0.1:port (a bind-check would not detect this -- a second process
    CAN bind a free TCP port fine; the risk here is reaching an
    already-running server there by accident, not failing to bind)."""
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(0.25)
    try:
        s.connect(("127.0.0.1", port))
        return True
    except OSError:
        return False
    finally:
        s.close()


def require_udp_port_free(port: int, what: str) -> None:
    """Raise GatePortsBusyError if ``port`` is already bound by someone
    else. Call this immediately before binding ``port`` yourself -- see
    capture_osi() below for the canonical call site."""
    if _udp_port_busy(port):
        raise GatePortsBusyError(
            f"port {port} ({what}) is already in use (UDP) -- refusing to "
            "bind it ourselves. A packaged GT_Sim.exe, another verification "
            "run, or another job already using this port is the common "
            "cause. There is deliberately no override: an override is how "
            "a run that measured nothing gets reported as a real result."
        )


# feature:F7 gate hardening -- the gt_sim_test.py-specific port table (the
# full collision+contamination set a headless VERIFICATION run cares about;
# see that module for why it, unlike a production web-backend run, has NO
# legitimate claim to any of these ports). Kept here so it travels with the
# rest of this shared module instead of living only in the CLI.
GATE_UDP_PORTS: dict[int, tuple[str, str]] = {
    48198: ("OSI ground-truth", "collision"),
    48199: ("HostVehicleData", "contamination"),
    48200: ("ScenarioVariables", "contamination"),
    48202: ("VirtualDriver telemetry", "contamination"),
    9100: ("manual-drive / VD network input", "collision"),
    9105: ("HeadlessFfbSink pushback", "collision"),
}
GATE_TCP_LISTEN_PORTS: dict[int, tuple[str, str]] = {
    8000: ("web backend (packaged GT_Sim / gt_sim_web)", "collision"),
}


def check_gate_ports_free(
    udp_ports: dict[int, tuple[str, str]] | None = None,
    tcp_listen_ports: dict[int, tuple[str, str]] | None = None,
) -> list[str]:
    """Return human-readable problem strings for every busy port; empty list
    means all clear. Defaults to GATE_UDP_PORTS / GATE_TCP_LISTEN_PORTS; the
    parameters exist so tests can point this at disposable high ports
    instead of the real 48198-and-friends range."""
    if udp_ports is None:
        udp_ports = GATE_UDP_PORTS
    if tcp_listen_ports is None:
        tcp_listen_ports = GATE_TCP_LISTEN_PORTS
    problems = []
    for port, (what, why) in udp_ports.items():
        if _udp_port_busy(port):
            problems.append(f"port {port} ({what}) [{why}] already in use (UDP)")
    for port, (what, why) in tcp_listen_ports.items():
        if _tcp_port_listening(port):
            problems.append(f"port {port} ({what}) [{why}] already listening (TCP)")
    return problems


def require_gate_ports_free() -> None:
    """Full-table version of require_udp_port_free, for a caller (gt_sim_test.py's
    run()/batch()) that -- unlike a production web-backend run -- has no
    legitimate claim to ANY of GATE_UDP_PORTS/GATE_TCP_LISTEN_PORTS and
    should refuse to start if any of them are occupied."""
    problems = check_gate_ports_free()
    if not problems:
        return
    detail = "\n".join(f"  - {p}" for p in problems)
    raise GatePortsBusyError(
        "required ports are already in use -- refusing to run scenarios that "
        "would either fail on our own bind or silently contaminate another "
        "process's telemetry:\n"
        f"{detail}\n"
        "Stop whatever holds them (a packaged GT_Sim.exe left running is the "
        "common case) and retry. There is deliberately no override: an "
        "override is how a run that measured nothing gets reported as green."
    )


# ---------------------------------------------------------------------------
# OBB (oriented bounding box) separation — SAT
# ---------------------------------------------------------------------------


def _obb_corners(
    cx: float, cy: float, h: float, length: float, width: float
) -> list[tuple[float, float]]:
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


def ego_track_from_telemetry(
    frames: list[dict],
) -> list[tuple[float, float, float, float]]:
    """-> [(t, x, y, speed)]"""
    return [
        (fr["sim_time"], fr["ego"]["x"], fr["ego"]["y"], fr["ego"]["speed"])
        for fr in frames
    ]


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
        gt.ParseFromString(data[off : off + size])
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
        track.append(
            (
                t,
                ego.base.position.x,
                ego.base.position.y,
                math.sqrt(v.x**2 + v.y**2 + v.z**2),
            )
        )
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


def interp(
    track: list[tuple[float, float, float, float]], t: float
) -> tuple[float, float, float]:
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
        "endpoint_dist_m": round(
            math.dist((vd[-1][1], vd[-1][2]), (base[-1][1], base[-1][2])), 4
        ),
    }
    (run_dir / "compare.json").write_text(
        json.dumps(result, indent=2), encoding="utf-8"
    )

    # Baseline ego track resampled onto the VD frame times, so the replay UI can
    # overlay the Default "ghost" by simple index (baseline_track[i] <-> frames[i]).
    baseline_track = []
    for t, _x, _y, _s in vd:
        bx, by, bs = interp(base, t)
        baseline_track.append(
            {
                "t": round(t, 3),
                "x": round(bx, 3),
                "y": round(by, 3),
                "speed": round(bs, 3),
            }
        )
    (run_dir / "baseline_track.json").write_text(
        json.dumps(baseline_track, separators=(",", ":")), encoding="utf-8"
    )
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
            seg = y[max(0, i - half) : min(n, i + half + 1)]
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
        a = _smooth(a_osi)  # face-1 OSI acceleration, same smoothing as v
        a_source = "osi"
    else:
        a = _central(v)  # face-2 fallback: central-difference of speed
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
        if road_id is not None and int(ego.get("track", -(10**9))) != road_id:
            continue
        if (
            s_range is not None
            and "s" in ego
            and not (s_range[0] <= ego["s"] <= s_range[1])
        ):
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

    One field is telemetry-only ON PURPOSE (documented face-1 gap, not laziness):
      * `s` (along-lane distance): OSI GroundTruth does not populate MovingObject
        s_position (capability_model.md §2.3a), so the scene cannot supply it.

    `lane` is scene-preferred since 2026-07-24 (follow-on of
    spine-work:ego-anchor-face1-migration, which had deliberately left it on
    telemetry): the host's lane_global_id joined against scene["lane_map"] now
    yields the OpenDRIVE lane_id through the same join that already supplied
    road_id (777/777 track match incl. a real road transition). The unlock was
    the GT-side fix (2026-07-21, spine-work:osi-assigned-lane-driving): OSI
    assigned_lane_id USED to re-derive the lane from (s, t) via GetLaneGlobalId(),
    so a laterally-drifting driving vehicle was reported on a border/sidewalk
    lane (red_stop_green_go 2026-07-21: drifted -1 -> -2 -> -3 while telemetry
    held -1, 62% mismatch); it now emits the object's cached DRIVING lane
    (GT_OSIReporter_Moving.cpp ResolveMovingObjectAssignedLaneGlobalId) and
    agrees with the VD Position lane (same scenario: 1200/1200 frames) — the
    basis the lane_keep / lane_change_count baselines were authored against, so
    the switch was gated on a regression-gate re-check. When the join cannot
    supply a lane (no scene, no lane_map entry, or entry without lane_id) the
    telemetry lane is the fallback.

    `accel_long` is None when the scene is absent. The chosen face is recorded in
    "_source" (scene | telemetry) so the verdict can surface which one fed the
    anchor; "_lane_source" records the same for `lane` separately, because a
    scene-anchored frame can still fall back to the telemetry lane (lane_map
    entry missing/laneless) and the lane matchers must not present that as a
    face-1 result.

    Both the fr.get("scene") and the fr["ego"] reads live in this one helper by
    design: check_knowledge_graph.py (_inlined_helpers) inlines it one level into
    every matcher branch that calls _ego_state(...), so the OSI-preferred /
    telemetry-fallback coupling is attributed to that matcher instead of hidden
    behind the call — the scene-preferred read must not be buried two levels deep."""
    ego = fr["ego"]
    scene = fr.get("scene")
    src = lane_src = "telemetry"
    x, y, speed, h = ego["x"], ego["y"], ego["speed"], ego.get("h", 0.0)
    track, lane, accel_long = ego.get("track"), ego.get("lane"), None
    if scene:
        host = next((o for o in scene.get("objects", []) if o.get("is_host")), None)
        if host is not None:
            src = "scene"
            x, y, speed = host["x"], host["y"], host["speed"]
            h = host.get("h", h)
            gid = host.get("lane_global_id")
            entry = (
                (scene.get("lane_map") or {}).get(str(gid)) if gid is not None else None
            )
            if entry is not None and entry.get("road_id") is not None:
                track = entry["road_id"]
            if entry is not None and entry.get("lane_id") is not None:
                lane = entry["lane_id"]
                lane_src = "scene"
            ax, ay = host.get("ax"), host.get("ay")
            if ax is not None and ay is not None:
                accel_long = ax * math.cos(h) + ay * math.sin(h)
    return {
        "x": x,
        "y": y,
        "speed": speed,
        "h": h,
        "s": ego.get("s"),
        "track": track,
        "lane": lane,
        "accel_long": accel_long,
        "_source": src,
        "_lane_source": lane_src,
    }


# ---------------------------------------------------------------------------
# ManualDrive HVD ADAS helpers (req-vd-ad:REQ-AD-025, phase A)
# ---------------------------------------------------------------------------
#
# Contract (coordinator-defined; produced by the ManualDrive/HVD side of this
# feature, not by this module): a `controller: manualdrive` telemetry frame
# carries
#   frame["hvd"]["adas"][<custom_name>] = {
#       "name": int, "state": int, "state_name": str,
#       "detail": {"<custom_name>.<field>": "<string>", ...},
#       "driver_override": {"present": bool, "active": bool, "reasons": [...]},
#       "custom_state": str,
#   }
# keyed by custom_name (e.g. "gt.aeb", "gt.fcw"). state_name is one of
# unavailable/available/standby/active/errored/unknown. `detail` VALUES ARE
# STRINGS (fixed 3-decimal for reals, "true"/"false" for booleans) -- the OSI
# custom_detail KeyValuePair contract, not a bug -- so _adas_detail_float
# below parses defensively: a missing key returns None, never a fabricated
# 0.0.
#
# `driver_override`/`custom_state` are the phase-B observation channel
# (req-vd-ad:REQ-AD-028 段b), read by driver_override_reported below.
# `present` distinguishes "the producer evaluated the override question and
# measured nothing" (present=True, active=False) from "nothing ever wrote this
# channel" (present=False) -- the same absent-is-not-zero discipline the
# detail parsing above follows, and the reason the matcher's negative
# direction can refuse to pass vacuously. Frames captured before phase B carry
# no "present" key at all; _adas_override normalises that to present=False,
# which is the correct reading for them.


def _hvd_adas_record(fr: dict, function: str) -> dict | None:
    """The hvd.adas[function] dict for one frame, or None if hvd/adas/function
    is absent or malformed. Absence is a real observation -- the function was
    never reported this frame -- and must never be coerced into a default
    record (that is exactly how a vacuous pass sneaks in)."""
    hvd = fr.get("hvd")
    if not isinstance(hvd, dict):
        return None
    adas = hvd.get("adas")
    if not isinstance(adas, dict):
        return None
    rec = adas.get(function)
    return rec if isinstance(rec, dict) else None


def _adas_state_name(rec: dict | None) -> str | None:
    if rec is None:
        return None
    val = rec.get("state_name")
    return val if isinstance(val, str) else None


def _adas_detail_float(rec: dict | None, key: str) -> float | None:
    """Parse rec["detail"][key] (a string, per the OSI custom_detail
    contract) as a float. Returns None -- never 0.0 -- when the record is
    absent, the detail block is missing, the key is absent, or the string
    does not parse; callers must treat None as "unmeasured", not "zero"."""
    if rec is None:
        return None
    detail = rec.get("detail")
    if not isinstance(detail, dict) or key not in detail:
        return None
    try:
        return float(detail[key])
    except (TypeError, ValueError):
        return None


def _adas_override(rec: dict | None) -> dict:
    """rec["driver_override"] normalised to {"present","active","reasons"}.

    Always returns a dict so callers can read it without None-guarding, but
    every field defaults to the "nothing was measured" value: a missing record
    or a missing/malformed driver_override block yields present=False, which
    the caller must treat as "the channel was never written", NOT as "no
    override occurred". Pre-phase-B telemetry has no "present" key; it is
    normalised to False here for the same reason -- such a run genuinely did
    not populate the channel."""
    if rec is None:
        return {"present": False, "active": False, "reasons": []}
    ovr = rec.get("driver_override")
    if not isinstance(ovr, dict):
        return {"present": False, "active": False, "reasons": []}
    reasons = ovr.get("reasons")
    return {
        "present": bool(ovr.get("present", False)),
        "active": bool(ovr.get("active", False)),
        "reasons": list(reasons) if isinstance(reasons, list) else [],
    }


def _gated_frame_indices(frames: list[dict], must: dict) -> list[int]:
    return [
        i for i in range(len(frames)) if time_window_ok(frames[i]["sim_time"], must)
    ]


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
        gated = [
            (i, _ego_state(frames[i]))
            for i in range(len(frames))
            if time_window_ok(frames[i]["sim_time"], must)
        ]
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
            return res(
                "pass" if ok else "fail",
                detail,
                None if ok else worst_i,
                ego_source=src,
            )
        else:
            offenders = [i for i, s in speeds.items() if s > thr]
            ok = not offenders
            worst = max(speeds.values())
            detail = f"max speed in window = {worst:.2f} (<= {thr}?)"
            return res(
                "pass" if ok else "fail",
                detail,
                None if ok else offenders[0],
                ego_source=src,
            )

    if kind == "min_speed_above":
        # Lowest speed in the window must stay above threshold: a "do not slow
        # down here" assertion (e.g. crossing a junction the ego drives straight
        # through). Optional road_id confines the window to one road/connector.
        thr = float(must["threshold"])
        road_id = must.get("road_id")
        states = {
            i: _ego_state(frames[i])
            for i in range(len(frames))
            if time_window_ok(frames[i]["sim_time"], must)
        }
        gated = [
            i
            for i, eg in states.items()
            if road_id is None
            or int(eg["track"] if eg["track"] is not None else -(10**9)) == road_id
        ]
        if not gated:
            where = f" on road {road_id}" if road_id is not None else ""
            return res("skip", f"no frames in time window{where}")
        src = states[gated[0]]["_source"]
        worst_i = min(gated, key=lambda i: states[i]["speed"])
        v_min = states[worst_i]["speed"]
        ok = v_min >= thr
        detail = f"min speed in window = {v_min:.2f} (>= {thr}?)"
        return res(
            "pass" if ok else "fail", detail, None if ok else worst_i, ego_source=src
        )

    if kind == "no_constraint_kind":
        # Assert the mid/long planner never raises a constraint of the given kind
        # in the window. Reads telemetry midlong.constraints[].kind. Used to prove
        # a straight pass-through junction does not emit a "junction" constraint.
        target = must.get("kind")
        if target is None:
            return res("skip", "kind is required")
        gated = [
            i for i in range(len(frames)) if time_window_ok(frames[i]["sim_time"], must)
        ]
        if not gated:
            return res("skip", "no frames in time window")
        offenders = [
            i
            for i in gated
            if any(
                c.get("kind") == target
                for c in frames[i].get("midlong", {}).get("constraints", [])
            )
        ]
        ok = not offenders
        detail = (
            f"{len(gated)} frames checked; {len(offenders)} raised a "
            f"'{target}' constraint"
        )
        return res("pass" if ok else "fail", detail, None if ok else offenders[0])

    if kind == "domain_split_holds":
        # feature:F7 S4 — asserts a per-domain split is actually holding:
        # lateral driven by one controller, longitudinal by the other, with a
        # single physics integrator underneath
        # (docs/virtualdriver/design/domain_split_ownership.md).
        #
        # Three checks, and all three are needed. The first two attribute the
        # two domains in OPPOSITE directions, so neither controller can satisfy
        # both by taking everything — which is exactly the failure mode this
        # scenario used to exhibit. The third is the guard against the
        # "looks like it works" trap.
        #
        #   1. the reporting VirtualDriver is the integrator
        #      (telemetry.domain_integrator)
        #   2. LONGITUDINAL is VD's: speed RISES by at least `min_speed_gain`
        #      after its minimum. Deliberately not "speed holds a target": VD's
        #      mid/long planner is supposed to slow for the curve, so asserting a
        #      constant target would assert something false about VD and fail a
        #      correct run. What ManualDrive cannot do is the thing tested here —
        #      it commands zero throttle under the stub config, so it can only
        #      ever coast DOWN. Any sustained re-acceleration is therefore
        #      powered, and only VD can be commanding it.
        #   3. LATERAL is NOT VD's: |lane_offset| grows past `min_lane_departure`.
        #      The road is a constant-radius arc, so a VD that owned lateral
        #      would track the lane; departing proves the wheel is following the
        #      other controller's command (zero, under the socket-free stub
        #      config) rather than VD's lane-keeping.
        #   4. reported speed and travelled path agree within `ratio_band`.
        #      A state-stage merge (A writes the pose, B writes the speed field)
        #      passes checks 1-3 and still produces a vehicle travelling 32%
        #      faster than it reports, publishing the wrong speed to OSI. The
        #      speed column alone cannot see it; this ratio can. Distance is the
        #      SUM of per-frame segments (path length) — an endpoint-to-endpoint
        #      chord reads low by sin(x)/x on a curve and would fail a healthy
        #      run.
        if not frames:
            return res("skip", "no frames")

        min_speed_gain = float(must.get("min_speed_gain", 2.0))
        min_departure = float(must.get("min_lane_departure", 1.0))
        band = must.get("ratio_band", [0.98, 1.02])
        ratio_lo, ratio_hi = float(band[0]), float(band[1])

        # Drop the frozen tail before anything else. Once the scenario's
        # StopTrigger fires, ScenarioEngine stops stepping the controller but
        # GT_GetVirtualDriverTelemetry keeps returning the LAST value, so the
        # capture loop records hundreds of identical frames with a frozen
        # sim_time. Those are not samples: left in, they dilute the speed and
        # ratio statistics with one repeated instant. Keeping only the first
        # frame of any repeated sim_time leaves exactly the live run.
        seen_t: set[float] = set()
        live = []
        for i in range(len(frames)):
            t = frames[i]["sim_time"]
            if t in seen_t:
                continue
            seen_t.add(t)
            live.append(i)

        gated = [i for i in live if time_window_ok(frames[i]["sim_time"], must)]
        if len(gated) < 2:
            return res("skip", "fewer than 2 frames in time window")

        # 1. integrator
        non_integrator = [
            i for i in gated if not bool(frames[i].get("domain_integrator", False))
        ]
        if non_integrator:
            return res(
                "fail",
                f"domain_integrator false on {len(non_integrator)}/{len(gated)} gated frames "
                "(the reporting VirtualDriver is not advancing the body)",
                non_integrator[0],
            )

        # 2. longitudinal attribution
        # ego state is nested under frame["ego"] in the VD telemetry record
        speeds = [float(frames[i]["ego"]["speed"]) for i in gated]
        k_min = min(range(len(speeds)), key=lambda k: speeds[k])
        speed_min = speeds[k_min]
        speed_gain = max(speeds[k_min:]) - speed_min  # recovery must come AFTER the dip
        if speed_gain < min_speed_gain:
            return res(
                "fail",
                f"speed rose only {speed_gain:.3f} m/s after its minimum "
                f"{speed_min:.3f} m/s (need >= {min_speed_gain}); with ManualDrive "
                "commanding zero throttle this looks like a coast-down, i.e. nobody "
                "is driving the longitudinal domain",
                gated[k_min],
            )

        # 3. lateral attribution — must NOT be VD's lane keeping
        departures = [abs(float(frames[i]["ego"]["offset"])) for i in gated]
        max_departure = max(departures)
        if max_departure < min_departure:
            return res(
                "fail",
                f"max |lane_offset| {max_departure:.3f} < {min_departure} — the vehicle is "
                "still tracking the lane, so lateral is being driven by VirtualDriver, "
                "not by the lateral owner",
                gated[0],
            )

        # 4. reported speed vs travelled path
        path = 0.0
        for a, b in zip(gated, gated[1:]):
            path += math.hypot(
                float(frames[b]["ego"]["x"]) - float(frames[a]["ego"]["x"]),
                float(frames[b]["ego"]["y"]) - float(frames[a]["ego"]["y"]),
            )
        span = float(frames[gated[-1]]["sim_time"]) - float(
            frames[gated[0]]["sim_time"]
        )
        mean_reported = sum(speeds) / len(speeds)
        if span <= 0 or mean_reported <= 0.5:
            return res("skip", "window too short or vehicle not moving")
        ratio = (path / span) / mean_reported
        if not (ratio_lo <= ratio <= ratio_hi):
            return res(
                "fail",
                f"travelled/reported speed ratio {ratio:.4f} outside [{ratio_lo}, {ratio_hi}] "
                f"(travelled {path / span:.3f} m/s vs reported {mean_reported:.3f} m/s) — "
                "the body and the speed field are not coming from one integrator",
                gated[0],
            )

        return res(
            "pass",
            f"split holds over {len(gated)} live frames "
            f"(t={frames[gated[0]]['sim_time']:.2f}..{frames[gated[-1]]['sim_time']:.2f}): "
            f"integrator=VD; longitudinal=VD (speed re-accelerated {speed_gain:.3f} m/s "
            f"after its {speed_min:.3f} m/s minimum, >= {min_speed_gain}); "
            f"lateral!=VD (max |lane_offset| {max_departure:.3f} m >= {min_departure}); "
            f"single integrator (travelled/reported ratio {ratio:.4f} in "
            f"[{ratio_lo}, {ratio_hi}])",
        )

    if kind == "vd_control_relinquished":
        # feature:F7 scenario-driven handover (docs/virtualdriver/design/
        # scenario_control_handoff_design.md §5.1). Asserts the ego's
        # VirtualDriverController actually gave up control at some point in
        # the run and never resumed it. Reads the top-level telemetry.vd_active
        # field, which mirrors Controller::Active() at the instant
        # SetUpControlOutputs()/TearDownControlOutputs() ran in
        # ControllerVirtualDriver.cpp — the ONE telemetry field that is not
        # frozen once the controller goes inactive. Every other field
        # (sim_time included) holds its last-active-frame value forever after
        # deactivation, since ScenarioEngine stops calling Step() on an
        # inactive controller (design doc Fact D) — do not try to detect the
        # handoff via a sim_time freeze or an ffb.target_active edge; both are
        # heuristics this field replaces.
        if not frames:
            return res("skip", "no frames")
        active_flags = [bool(fr.get("vd_active", False)) for fr in frames]
        if not active_flags[0]:
            return res(
                "fail", "vd_active is already false on frame 0 (VD never activated)", 0
            )
        drop_idx = next(
            (
                i
                for i in range(1, len(active_flags))
                if active_flags[i - 1] and not active_flags[i]
            ),
            None,
        )
        if drop_idx is None:
            return res(
                "fail",
                "vd_active never transitioned to false during the run",
                len(active_flags) - 1,
            )
        not_before = must.get("after", {}).get("sim_time")
        drop_t = frames[drop_idx]["sim_time"]
        if not_before is not None and drop_t < not_before:
            return res(
                "fail",
                f"vd_active dropped at t={drop_t:.2f}, expected not before t={not_before}",
                drop_idx,
            )
        reactivate_idx = next(
            (i for i in range(drop_idx, len(active_flags)) if active_flags[i]), None
        )
        if reactivate_idx is not None:
            return res(
                "fail",
                f"vd_active flipped back to true at frame {reactivate_idx} "
                f"after dropping at frame {drop_idx} (t={drop_t:.2f})",
                reactivate_idx,
            )
        return res(
            "pass",
            f"vd_active dropped to false at t={drop_t:.2f} (frame {drop_idx}) "
            f"and stayed false through end of run ({len(active_flags)} frames)",
        )

    # Lane events use the ego's road-coordinate anchor (road_id / lane). It is
    # resolved through _ego_state, which prefers the face-1 OSI scene: the
    # is_host object's lane_global_id joined against scene["lane_map"] (built
    # from OSI Lane.source_reference) yields OpenDRIVE road_id AND lane_id,
    # falling back to the telemetry ego.track/ego.lane. A frame lacking both
    # skips. The verdict carries eg["_lane_source"] (not "_source"): the lane is
    # the judged quantity here, and it can fall back to telemetry even on a
    # scene-anchored frame. (Building the series inline in each branch — rather
    # than via a shared nested helper — keeps the _ego_state read inside the
    # matcher's own branch, where the coupling lint's one-level inlining can
    # see it.)
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
            src = eg["_lane_source"]
            ls.append((i, int(eg["track"]), int(eg["lane"])))
        if not ls:
            return res(
                "skip", "no lane data in window (lane/track absent or empty window)"
            )
        bad = [
            i
            for (i, trk, ln) in ls
            if (road_id is not None and trk != road_id)
            or (lane_id is not None and ln != lane_id)
        ]
        detail = (
            f"{len(ls)} frames in window on lane(s) "
            f"{sorted(set(ln for _, _, ln in ls))} road(s) {sorted(set(trk for _, trk, _ in ls))}; "
            f"expected road={road_id} lane={lane_id}"
        )
        return res(
            "pass" if not bad else "fail",
            detail,
            None if not bad else bad[0],
            ego_source=src,
        )

    if kind == "lane_change_count":
        expected = must.get("count")
        ls, src = [], "telemetry"
        for i, fr in enumerate(frames):
            eg = _ego_state(fr)
            if eg["lane"] is None or eg["track"] is None:
                continue
            src = eg["_lane_source"]
            ls.append((i, int(eg["track"]), int(eg["lane"])))
        if not ls:
            return res("skip", "no lane data")
        changes = [ls[i][0] for i in range(1, len(ls)) if ls[i][2] != ls[i - 1][2]]
        ok = (expected is None) or (len(changes) == expected)
        detail = f"observed {len(changes)} lane change(s); expected {expected}"
        return res(
            "pass" if ok else "fail",
            detail,
            None if ok else (changes[0] if changes else None),
            ego_source=src,
        )

    # --- mid/long anticipation matchers (V2) ---------------------------------
    # "Physically plausible deceleration" judged from VirtualDriver telemetry
    # alone (position equivalence vs Default is meaningless for the mid/long case).

    if kind == "deceleration_profile_smooth":
        gated = [
            i for i in range(len(frames)) if time_window_ok(frames[i]["sim_time"], must)
        ]
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
            lm = next(
                (
                    i
                    for i in gated
                    if (
                        road_id is None
                        or int(
                            egos[i]["track"]
                            if egos[i]["track"] is not None
                            else -(10**9)
                        )
                        == road_id
                    )
                    and prof["s"][i] >= float(landmark_s)
                ),
                None,
            )
            if lm is not None:
                onset = next(
                    (i for i in gated if i <= lm and prof["a"][i] < -0.3), None
                )
                if onset is not None and lm - onset >= 2:
                    eval_idx = [i for i in gated if onset <= i <= lm]
                    win = (
                        f" [decel phase t{frames[onset]['sim_time']:.1f}-"
                        f"{frames[lm]['sim_time']:.1f}s, a={a_src}]"
                    )

        max_jerk = must.get("max_jerk")
        if max_jerk is not None:
            worst = max(eval_idx, key=lambda i: abs(prof["j"][i]))
            if abs(prof["j"][worst]) > float(max_jerk):
                return res(
                    "fail",
                    f"max |jerk| = {abs(prof['j'][worst]):.2f} m/s^3 (<= {max_jerk}?){win}",
                    worst,
                    ego_source=src,
                )

        max_decel = must.get("max_decel")
        if max_decel is not None:
            worst = min(eval_idx, key=lambda i: prof["a"][i])  # most negative accel
            if -prof["a"][worst] > float(max_decel):
                return res(
                    "fail",
                    f"max deceleration = {-prof['a'][worst]:.2f} m/s^2 (<= {max_decel}?){win}",
                    worst,
                    ego_source=src,
                )

        wj = max(eval_idx, key=lambda i: abs(prof["j"][i]))
        wa = min(eval_idx, key=lambda i: prof["a"][i])
        return res(
            "pass",
            f"max |jerk|={abs(prof['j'][wj]):.2f} m/s^3, "
            f"max decel={-prof['a'][wa]:.2f} m/s^2 within bounds{win}",
            ego_source=src,
        )

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
            if (
                road_id is not None
                and int(eg["track"] if eg["track"] is not None else -(10**9)) != road_id
            ):
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
        return res(
            "pass" if ok else "fail",
            detail,
            None if ok else hit,
            ego_source=hit_eg["_source"],
        )

    if kind == "steer_not_saturated":
        thr = float(must.get("threshold", 0.98))
        gated = [
            i
            for i in range(len(frames))
            if time_window_ok(frames[i]["sim_time"], must)
            and frames[i].get("driver", {}).get("steer") is not None
        ]
        if not gated:
            return res("skip", "no driver.steer data in window")
        offenders = [i for i in gated if abs(frames[i]["driver"]["steer"]) > thr]
        worst = max(gated, key=lambda i: abs(frames[i]["driver"]["steer"]))
        detail = f"max |steer| in window = {abs(frames[worst]['driver']['steer']):.3f} (<= {thr}?)"
        return res(
            "pass" if not offenders else "fail",
            detail,
            None if not offenders else offenders[0],
        )

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
        ident = (
            f"sign {must.get('sign_id')}"
            if kind == "stopped_at_stop_sign"
            else f"signal {must.get('signal_id')}"
        )
        detail = (
            f"longest full stop (<= {stop_speed} m/s) at {ident} = {dur:.2f}s "
            f"(>= {min_duration}?)"
        )

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
                stopish = [
                    s["id"] for s in signs if s.get("type") in ("stop", "give_way")
                ]
                ids = [s["id"] for s in signs]
                sign_ok = (sid in stopish) or (sid not in ids and len(stopish) > 0)
                if not sign_ok:
                    return res(
                        "fail",
                        detail + f"; but no stop/give-way sign in the OSI "
                        f"scene (stop-ish ids={stopish})",
                        start_i,
                    )
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
                    on_lane = [
                        t
                        for t in tls
                        if want_lane in (t.get("assigned_lane_ids") or [])
                    ]
                    if on_lane:
                        tls = on_lane
                if tls:
                    sig = must.get("signal_id")
                    reds = [t["id"] for t in tls if t.get("color") == "red"]
                    ids = [t["id"] for t in tls]
                    red_ok = (sig in reds) or (sig not in ids and len(reds) > 0)
                    if not red_ok:
                        return res(
                            "fail",
                            detail + f"; but signal was not red at stop onset "
                            f"(reds={reds})",
                            start_i,
                        )
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
            ego_len = next(
                (o["length"] for o in scene["objects"] if o.get("is_host")), 5.0
            )
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
            return res(
                "skip",
                "no lead-vehicle frames with a captured scene "
                "(needs --osi / batch osi:true and a lead in lane)",
            )
        val = _percentile(thws, pct)
        lo_ok = (min_thw is None) or (val >= float(min_thw))
        hi_ok = (max_thw is None) or (val <= float(max_thw))
        detail = (
            f"p{pct:g} THW = {val:.2f}s over {len(thws)} frames "
            f"(want {min_thw}..{max_thw}s)"
        )
        return res("pass" if (lo_ok and hi_ok) else "fail", detail, ego_source=src)

    # "min_separation_above" (center-to-center distance) was removed 2026-07-24:
    # it was a strictly inferior twin of min_obb_separation_above (an adjacent-lane
    # pass at ~2.8 m center distance reads as "close" while the OBB test correctly
    # reads non-overlapping bodies as safe) and no asset ever referenced it.
    # Rationale is recorded on the matcher namespace in graph.yaml (DEPRECATED note).

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
            return res(
                "skip",
                f"OBB separation inconclusive: object(s) lacked real "
                f"OSI dimensions ({names}); measured min "
                f"{sep:.2f} m uses a 4.0x2.0 m fallback footprint, so a "
                f"clean pass is not trustworthy",
            )
        detail = (
            f"min OBB separation = {sep:.2f} m at "
            f"t={frames[worst_i]['sim_time']:.2f} (>= {thr}?); "
            f"overlap occurred: {any_overlap}"
        )
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
        gated = [
            i for i in range(len(frames)) if time_window_ok(frames[i]["sim_time"], must)
        ]
        if not gated:
            return res("skip", "no frames in time window")

        any_scene = False
        best_sep = None  # closest approach ever seen (for the no-contact detail)
        contact = None  # (idx, sep, ego, obj) at the first contact frame
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
            detail = (
                f"no contact (min separation {best_sep:.2f} m) -> pass"
                if best_sep is not None
                else "no contact (no other bodies observed in scene) -> pass"
            )
            return res("pass", detail)

        idx, sep, c_ego, c_obj = contact
        closing = _closing_speed(c_ego, c_obj)
        ok = closing <= thr
        who = c_obj.get("name") or f"#{c_obj.get('id')}"
        t_contact = frames[idx]["sim_time"]
        detail = (
            f"impact speed = {closing:.2f} m/s at t={t_contact:.2f} "
            f"(<= {thr}?) [contact with {who}, sep={sep:.2f} m]"
        )
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
        gated = [
            i for i in range(len(frames)) if time_window_ok(frames[i]["sim_time"], must)
        ]
        if not gated:
            return res("skip", "no frames in time window")
        offenders = [
            i
            for i in gated
            if any(
                c.get("source") == "aeb"
                for c in frames[i].get("policy", {}).get("constraints", [])
            )
        ]
        if offenders:
            i0 = offenders[0]
            detail = (
                f"AEB emergency fired at t={frames[i0]['sim_time']:.2f} "
                f"(no collision course) -> misfire"
            )
            return res("fail", detail, i0)
        detail = f"no AEB emergency constraint over {len(gated)} frames -> pass"
        return res("pass", detail)

    # --- ManualDrive ADAS matchers (req-vd-ad:REQ-AD-025, phase A) ----------
    # Read frame["hvd"]["adas"][function] (see the _hvd_adas_record docstring
    # above for the wire contract). Every branch below follows the same
    # vacuous-pass discipline documented in
    # docs/virtualdriver/design/manualdrive_adas_verification_plan.md §4-1:
    # a matcher must never report pass/fail having measured nothing. Zero
    # gated frames, or `function` absent from hvd.adas on every gated frame,
    # is always skip (with a detail that names which of the two happened --
    # "never reported" is a different fact than "reported and never active").
    # All five accept an optional `min_frames: N` (default 1) that raises the
    # "too few frames to trust" floor above the bare non-empty check.

    if kind == "manual_aeb_fires":
        # req-vd-ad:REQ-AD-025 step a (positive): at least one in-window
        # frame has hvd.adas[function].state_name == "active". `function`
        # defaults to "gt.aeb" (the phase-A row) but is a parameter so the
        # same branch covers other functions (e.g. gt.fcw) if ever driven
        # directly instead of through fcw_leads_intervention.
        function = must.get("function", "gt.aeb")
        min_frames = int(must.get("min_frames", 1))
        gated = _gated_frame_indices(frames, must)
        if len(gated) < min_frames:
            return res(
                "skip",
                f"only {len(gated)} frame(s) in time window (< min_frames={min_frames})",
            )
        reported = [
            i for i in gated if _hvd_adas_record(frames[i], function) is not None
        ]
        if not reported:
            return res(
                "skip",
                f"{function!r} never reported in hvd.adas over {len(gated)} gated "
                "frame(s) -- distinct from 'reported and never active'",
            )
        active = [
            i
            for i in reported
            if _adas_state_name(_hvd_adas_record(frames[i], function)) == "active"
        ]
        if active:
            i0 = active[0]
            detail = (
                f"{function} went ACTIVE at t={frames[i0]['sim_time']:.2f} "
                f"(frame {i0}); {len(active)}/{len(reported)} reported frame(s) active"
            )
            return res("pass", detail)
        states_seen = sorted(
            {_adas_state_name(_hvd_adas_record(frames[i], function)) for i in reported}
        )
        detail = (
            f"{function} reported on {len(reported)} frame(s) but never ACTIVE "
            f"(state_names observed: {states_seen})"
        )
        return res("fail", detail, reported[-1])

    if kind == "no_intervention_in_window":
        # req-vd-ad:REQ-AD-025 step b (negative): NO in-window frame may have
        # state_name == "active" for `function`. An empty ACTIVE set is
        # exactly what a run that never exercised the ADAS stack ALSO
        # produces, so "no actives" alone is not sufficient evidence: the
        # function must additionally have been reported and NOT unavailable
        # on at least one gated frame (REQ-AD-028's STANDBY-vs-UNAVAILABLE
        # distinction is load-bearing for this verdict -- a function that was
        # switched off cannot evidence "it correctly declined to intervene").
        function = must.get("function", "gt.aeb")
        min_frames = int(must.get("min_frames", 1))
        gated = _gated_frame_indices(frames, must)
        if len(gated) < min_frames:
            return res(
                "skip",
                f"only {len(gated)} frame(s) in time window (< min_frames={min_frames})",
            )
        reported = [
            i for i in gated if _hvd_adas_record(frames[i], function) is not None
        ]
        if not reported:
            return res(
                "skip",
                f"{function!r} never reported in hvd.adas over {len(gated)} gated "
                "frame(s) -- cannot evidence non-intervention for a function that "
                "never ran",
            )
        live = [
            i
            for i in reported
            if _adas_state_name(_hvd_adas_record(frames[i], function)) != "unavailable"
        ]
        if not live:
            return res(
                "skip",
                f"{function!r} reported but UNAVAILABLE on all {len(reported)} "
                "frame(s) -- switched off cannot evidence 'correctly declined to "
                "intervene' (REQ-AD-028 STANDBY vs UNAVAILABLE)",
            )
        active = [
            i
            for i in live
            if _adas_state_name(_hvd_adas_record(frames[i], function)) == "active"
        ]
        if active:
            i0 = active[0]
            detail = (
                f"{function} went ACTIVE at t={frames[i0]['sim_time']:.2f} "
                f"(frame {i0}) -- misfire (expected no intervention in window)"
            )
            return res("fail", detail, i0)
        detail = (
            f"{function} never went ACTIVE over {len(live)} not-unavailable / "
            f"{len(reported)} reported frame(s) in window -- no misfire"
        )
        return res("pass", detail)

    if kind == "brake_not_stacked":
        # req-vd-ad:REQ-AD-025 step c restated in the observable domain: over
        # in-window frames where `function` is ACTIVE and the human is
        # already braking at or beyond the system's own request, the
        # effective brake output must equal the human's own value (max
        # composition, not additive stacking). Evaluated only on that
        # precondition subset -- a run where the human never out-brakes the
        # request has nothing to check and must skip, not pass.
        function = must.get("function", "gt.aeb")
        tol = float(must.get("tolerance", 0.01))
        min_frames = int(must.get("min_frames", 1))
        gated = _gated_frame_indices(frames, must)
        if len(gated) < min_frames:
            return res(
                "skip",
                f"only {len(gated)} frame(s) in time window (< min_frames={min_frames})",
            )
        reported = [
            i for i in gated if _hvd_adas_record(frames[i], function) is not None
        ]
        if not reported:
            return res(
                "skip",
                f"{function!r} never reported in hvd.adas over {len(gated)} gated "
                "frame(s)",
            )
        active = [
            i
            for i in reported
            if _adas_state_name(_hvd_adas_record(frames[i], function)) == "active"
        ]
        if not active:
            return res(
                "skip",
                f"{function!r} never ACTIVE over {len(reported)} reported frame(s) "
                "-- nothing to check for stacking",
            )
        driver_key = f"{function}.driver_brake"
        request_key = f"{function}.brake_request"
        out_key = f"{function}.brake_out"
        eligible = []  # (idx, driver_brake, brake_out)
        for i in active:
            rec = _hvd_adas_record(frames[i], function)
            driver_brake = _adas_detail_float(rec, driver_key)
            brake_request = _adas_detail_float(rec, request_key)
            brake_out = _adas_detail_float(rec, out_key)
            if driver_brake is None or brake_request is None or brake_out is None:
                continue  # a missing detail key is never fabricated as 0.0
            if driver_brake >= brake_request:
                eligible.append((i, driver_brake, brake_out))
        if not eligible:
            return res(
                "skip",
                f"no ACTIVE frame had driver_brake >= brake_request with all three "
                f"detail keys present (checked {len(active)} active frame(s)) -- "
                "nothing to evaluate",
            )
        offenders = [(i, db, bo) for (i, db, bo) in eligible if abs(bo - db) > tol]
        if offenders:
            i0, db, bo = offenders[0]
            detail = (
                f"brake_out={bo:.3f} != driver_brake={db:.3f} "
                f"(|diff|={abs(bo - db):.3f} > tol={tol}) at t={frames[i0]['sim_time']:.2f} "
                f"(frame {i0}) -- stacked on top of the human"
            )
            return res("fail", detail, i0)
        detail = (
            f"brake_out == driver_brake within tol={tol} over {len(eligible)} "
            f"eligible frame(s) (driver_brake >= brake_request while {function} "
            "ACTIVE) -- max-composed, not stacked"
        )
        return res("pass", detail)

    if kind == "fcw_leads_intervention":
        # req-vd-ad:REQ-AD-025 step e: the warning function's first ACTIVE
        # frame must precede the intervention function's first ACTIVE frame
        # by at least min_lead_s. If the intervention never fired there is
        # nothing to measure a lead against (skip, not pass -- the
        # warning-only episode is judged by adas_state_matches /
        # no_intervention_in_window, not here). If the intervention DID fire
        # but the warning never went active, that is a real defect (FCW
        # failed to precede AEB) and must FAIL, not skip.
        warn_fn = must.get("warning_function", "gt.fcw")
        interv_fn = must.get("intervention_function", "gt.aeb")
        if "min_lead_s" not in must:
            return res(
                "skip",
                "must entry names no min_lead_s -- a matcher that checks nothing "
                "must not report pass",
            )
        min_lead_s = float(must["min_lead_s"])
        min_frames = int(must.get("min_frames", 1))
        gated = _gated_frame_indices(frames, must)
        if len(gated) < min_frames:
            return res(
                "skip",
                f"only {len(gated)} frame(s) in time window (< min_frames={min_frames})",
            )
        warn_reported = [
            i for i in gated if _hvd_adas_record(frames[i], warn_fn) is not None
        ]
        interv_reported = [
            i for i in gated if _hvd_adas_record(frames[i], interv_fn) is not None
        ]
        if not warn_reported or not interv_reported:
            missing = []
            if not warn_reported:
                missing.append(f"warning_function {warn_fn!r}")
            if not interv_reported:
                missing.append(f"intervention_function {interv_fn!r}")
            return res(
                "skip",
                f"{' and '.join(missing)} never reported in hvd.adas over "
                f"{len(gated)} gated frame(s)",
            )
        interv_idx = next(
            (
                i
                for i in interv_reported
                if _adas_state_name(_hvd_adas_record(frames[i], interv_fn)) == "active"
            ),
            None,
        )
        if interv_idx is None:
            return res(
                "skip",
                f"{interv_fn!r} never went ACTIVE in window -- no intervention to "
                "measure a lead against",
            )
        t_interv = frames[interv_idx]["sim_time"]
        warn_idx = next(
            (
                i
                for i in warn_reported
                if _adas_state_name(_hvd_adas_record(frames[i], warn_fn)) == "active"
            ),
            None,
        )
        if warn_idx is None:
            detail = (
                f"{interv_fn} went ACTIVE at t={t_interv:.2f} (frame {interv_idx}) "
                f"but {warn_fn} never went ACTIVE in window -- warning failed to "
                "precede intervention"
            )
            return res("fail", detail, interv_idx)
        t_warn = frames[warn_idx]["sim_time"]
        lead = t_interv - t_warn
        ok = lead >= min_lead_s
        detail = (
            f"measured lead = {lead:.3f}s ({warn_fn} ACTIVE at t={t_warn:.2f} frame "
            f"{warn_idx}, {interv_fn} ACTIVE at t={t_interv:.2f} frame {interv_idx}; "
            f"want >= {min_lead_s}s)"
        )
        return res("pass" if ok else "fail", detail, None if ok else interv_idx)

    if kind == "adas_state_matches":
        # req-vd-ad:REQ-AD-026 step c / REQ-AD-028 step a: the in-window
        # state_name column for `function` matches `expect`, either on EVERY
        # reported+gated frame (mode="all", default) or on AT LEAST ONE
        # (mode="any").
        if "function" not in must or "expect" not in must:
            return res(
                "skip",
                "must entry names no function/expect -- a matcher that checks "
                "nothing must not report pass",
            )
        function = must["function"]
        expect = must["expect"]
        valid_states = {
            "unavailable",
            "available",
            "standby",
            "active",
            "errored",
            "unknown",
        }
        if expect not in valid_states:
            return res(
                "skip",
                f"expect={expect!r} is not a recognised state_name "
                f"({sorted(valid_states)})",
            )
        mode = must.get("mode", "all")
        if mode not in ("all", "any"):
            return res("skip", f"mode must be 'all' or 'any', got {mode!r}")
        min_frames = int(must.get("min_frames", 1))
        gated = _gated_frame_indices(frames, must)
        if len(gated) < min_frames:
            return res(
                "skip",
                f"only {len(gated)} frame(s) in time window (< min_frames={min_frames})",
            )
        reported = [
            (i, _adas_state_name(_hvd_adas_record(frames[i], function)))
            for i in gated
            if _hvd_adas_record(frames[i], function) is not None
        ]
        if not reported:
            return res(
                "skip",
                f"{function!r} never reported in hvd.adas over {len(gated)} gated "
                "frame(s)",
            )
        if mode == "all":
            offenders = [(i, st) for i, st in reported if st != expect]
            if offenders:
                i0, got = offenders[0]
                detail = (
                    f"{function}.state_name == {got!r} at t={frames[i0]['sim_time']:.2f} "
                    f"(frame {i0}) (want {expect!r} on every reported frame)"
                )
                return res("fail", detail, i0)
            detail = (
                f"{function}.state_name == {expect!r} on all {len(reported)} "
                "reported frame(s)"
            )
            return res("pass", detail)
        else:  # mode == "any"
            found = [(i, st) for i, st in reported if st == expect]
            if not found:
                observed = sorted({st for _, st in reported})
                detail = (
                    f"no reported frame had {function}.state_name == {expect!r} "
                    f"over {len(reported)} frame(s) (observed: {observed})"
                )
                return res("fail", detail, reported[-1][0])
            i0 = found[0][0]
            detail = (
                f"{function}.state_name == {expect!r} at t={frames[i0]['sim_time']:.2f} "
                f"(frame {i0})"
            )
            return res("pass", detail)

    if kind == "driver_override_reported":
        # req-vd-ad:REQ-AD-028 段b (phase B): the driver-override event the
        # human's input produced is visible on the function's HVD row, in the
        # window where that input was actually applied.
        #
        #   function            str  : hvd.adas key (required -- there is no
        #                              sensible default; the AEB row and the
        #                              FCW row answer DIFFERENT questions here)
        #   expect_active       bool : default True
        #   expect_reason       str  : optional -- must appear in
        #                              driver_override.reasons. The harness
        #                              projects the OSI enum through
        #                              _enum_name(), which STRIPS the "REASON_"
        #                              prefix and lower-cases the rest, so the
        #                              value written in an expectations file is
        #                              "brake_pedal" / "steering_input", NOT the
        #                              proto spelling REASON_BRAKE_PEDAL. (Noted
        #                              in phase C, when the first real producer
        #                              of a Reason value appeared -- until then
        #                              nothing had ever populated `reasons` and
        #                              this comment's earlier example went
        #                              unchallenged.)
        #   expect_custom_state str  : optional, e.g. "DRIVER_OVERRIDE_ACCEL"
        #                              -- exact match on the row's custom_state
        #   mode                str  : "all" (default) | "any"
        #   min_frames          int  : default 1
        #   after / before           : the usual sim_time window
        #
        # WHY expect_custom_state EXISTS AT ALL: OSI's DriverOverride.Reason
        # enum has exactly two values (brake pedal, steering input) and no
        # accelerator value, so an accelerator-origin override cannot be
        # expressed as a Reason. design §8-3 routes it through custom_state
        # instead. A matcher that only knew about `reasons` would therefore be
        # structurally unable to observe the one override producer phase B
        # actually implements.
        #
        # WHY THE POSITIVE AND NEGATIVE DIRECTIONS HAVE DIFFERENT PRECONDITIONS
        # (this is the part that keeps the matcher honest, see verification
        # plan §4-1):
        #   * expect_active=True needs no presence precondition. The function
        #     row being reported at all already proves the instrument is live,
        #     so an ABSENT driver_override submessage is a genuine negative
        #     observation -- "the populate mechanism did not report an
        #     override" -- and must FAIL. That is exactly what makes deleting
        #     the populate code turn this matcher red rather than grey.
        #   * expect_active=False DOES need it. "No override was ever
        #     reported" is what a run with the whole mechanism removed also
        #     looks like, so without requiring that the channel was written at
        #     least once, the negative would pass vacuously for the wrong
        #     reason. This is the same STANDBY-vs-UNAVAILABLE argument
        #     no_intervention_in_window makes about State, applied to the
        #     override channel: a channel nobody wrote cannot evidence "the
        #     driver did not override".
        if "function" not in must:
            return res(
                "skip",
                "must entry names no function -- a matcher that checks nothing "
                "must not report pass",
            )
        function = must["function"]
        expect_active = bool(must.get("expect_active", True))
        expect_reason = must.get("expect_reason")
        expect_custom_state = must.get("expect_custom_state")
        mode = must.get("mode", "all")
        if mode not in ("all", "any"):
            return res("skip", f"mode must be 'all' or 'any', got {mode!r}")
        if not expect_active and mode == "any":
            # "at least one frame reported no override" is satisfied by almost
            # any run, including one where the override fired on every other
            # frame. Refuse the combination rather than report a pass nobody
            # should trust.
            return res(
                "skip",
                "expect_active: false with mode: any asserts only that SOME frame "
                "lacked an override, which nearly any run satisfies -- use "
                "mode: all for the negative direction",
            )
        min_frames = int(must.get("min_frames", 1))
        gated = _gated_frame_indices(frames, must)
        if len(gated) < min_frames:
            return res(
                "skip",
                f"only {len(gated)} frame(s) in time window (< min_frames={min_frames})",
            )
        reported = [
            i for i in gated if _hvd_adas_record(frames[i], function) is not None
        ]
        if not reported:
            return res(
                "skip",
                f"{function!r} never reported in hvd.adas over {len(gated)} gated "
                "frame(s) -- no row to read a driver override off",
            )

        if not expect_active:
            live = [
                i
                for i in reported
                if _adas_override(_hvd_adas_record(frames[i], function)).get("present")
            ]
            if not live:
                return res(
                    "skip",
                    f"{function!r} reported on {len(reported)} frame(s) but its "
                    "driver_override channel was never populated -- a channel "
                    "nobody wrote cannot evidence 'the driver did not override' "
                    "(REQ-AD-028 段b, mirrors the STANDBY-vs-UNAVAILABLE rule)",
                )
        else:
            live = reported

        def _ok(idx: int) -> bool:
            rec = _hvd_adas_record(frames[idx], function)
            ovr = _adas_override(rec)
            if bool(ovr.get("active")) != expect_active:
                return False
            if expect_reason is not None and expect_reason not in (
                ovr.get("reasons") or []
            ):
                return False
            if (
                expect_custom_state is not None
                and (rec.get("custom_state") or "") != expect_custom_state
            ):
                return False
            return True

        wanted = [
            f"active={expect_active}",
            *(
                [f"reason includes {expect_reason!r}"]
                if expect_reason is not None
                else []
            ),
            *(
                [f"custom_state == {expect_custom_state!r}"]
                if expect_custom_state is not None
                else []
            ),
        ]
        want_txt = ", ".join(wanted)

        def _seen(idx: int) -> str:
            rec = _hvd_adas_record(frames[idx], function)
            ovr = _adas_override(rec)
            return (
                f"present={bool(ovr.get('present'))} active={bool(ovr.get('active'))} "
                f"reasons={ovr.get('reasons') or []} "
                f"custom_state={(rec.get('custom_state') or '')!r}"
            )

        if mode == "all":
            offenders = [i for i in live if not _ok(i)]
            if offenders:
                i0 = offenders[0]
                detail = (
                    f"{function} driver override at t={frames[i0]['sim_time']:.2f} "
                    f"(frame {i0}): {_seen(i0)} (want {want_txt} on every frame; "
                    f"{len(offenders)}/{len(live)} disagreed)"
                )
                return res("fail", detail, i0)
            detail = (
                f"{function} driver override matched [{want_txt}] on all "
                f"{len(live)} evaluated frame(s)"
            )
            return res("pass", detail)

        found = [i for i in live if _ok(i)]
        if not found:
            i_last = live[-1]
            detail = (
                f"no frame in window had {function} driver override [{want_txt}] "
                f"over {len(live)} evaluated frame(s); last observed: {_seen(i_last)}"
            )
            return res("fail", detail, i_last)
        i0 = found[0]
        detail = (
            f"{function} driver override [{want_txt}] first seen at "
            f"t={frames[i0]['sim_time']:.2f} (frame {i0}); "
            f"{len(found)}/{len(live)} evaluated frame(s) matched"
        )
        return res("pass", detail)

    if kind == "adas_state_sequence":
        # req-vd-ad:REQ-AD-026 step c (and design §6's ACC/MSL exclusivity):
        # the function's state column must pass through `expect` IN ORDER, as
        # a SUBSEQUENCE of the observed run-length-compressed state列. Same
        # shape as the overtake matcher's expect_phases.
        #
        #   function     str        : hvd.adas key (required)
        #   expect       list[str]  : ordered state_names, e.g.
        #                             ["unavailable","standby","active","standby"]
        #   after/before            : the usual sim_time window
        #
        # SUBSEQUENCE, NOT EQUALITY, and the run-length compression: a state
        # column sampled at 20 Hz repeats each state for many frames, and the
        # claim REQ-AD-026 step c actually makes is about the ORDER of the
        # transitions, not about how long the run sat in each one. Requiring
        # exact equality would make the matcher a dwell-time assertion nobody
        # wrote, failing on every timing nudge.
        #
        # ...but a subsequence match is also how a matcher goes vacuous, so
        # two guards: an `expect` with fewer than 2 entries is REFUSED (a
        # one-element subsequence is just "this state occurred", which
        # adas_state_matches already says better), and repeated adjacent
        # entries in `expect` are refused too -- after compression they can
        # never match, and silently accepting them would turn an authoring
        # mistake into a permanent skip.
        if "function" not in must or "expect" not in must:
            return res(
                "skip",
                "must entry names no function/expect -- a matcher that checks "
                "nothing must not report pass",
            )
        function = must["function"]
        expect = must["expect"]
        if not isinstance(expect, list) or len(expect) < 2:
            return res(
                "skip",
                f"expect must be a list of >= 2 state names, got {expect!r} -- a "
                "single-state expectation is adas_state_matches' job, not a "
                "sequence claim",
            )
        valid_states = {
            "unavailable",
            "available",
            "standby",
            "active",
            "errored",
            "unknown",
        }
        bad = [s for s in expect if s not in valid_states]
        if bad:
            return res(
                "skip",
                f"unrecognised state name(s) {bad} (valid: {sorted(valid_states)})",
            )
        if any(a == b for a, b in zip(expect, expect[1:])):
            return res(
                "skip",
                f"expect={expect} repeats a state on adjacent positions, which can "
                "never match a run-length-compressed column -- express dwell with "
                "a windowed adas_state_matches instead",
            )
        min_frames = int(must.get("min_frames", 1))
        gated = _gated_frame_indices(frames, must)
        if len(gated) < min_frames:
            return res(
                "skip",
                f"only {len(gated)} frame(s) in time window (< min_frames={min_frames})",
            )
        observed: list[tuple[int, str]] = []
        for i in gated:
            st = _adas_state_name(_hvd_adas_record(frames[i], function))
            if st is None:
                continue
            if not observed or observed[-1][1] != st:
                observed.append((i, st))
        if not observed:
            return res(
                "skip",
                f"{function!r} never reported in hvd.adas over {len(gated)} gated "
                "frame(s)",
            )
        matched: list[tuple[int, str]] = []
        pos = 0
        for idx, st in observed:
            if pos < len(expect) and st == expect[pos]:
                matched.append((idx, st))
                pos += 1
        seen_txt = " -> ".join(st for _, st in observed)
        if pos < len(expect):
            detail = (
                f"{function} state sequence stopped at {expect[pos]!r} "
                f"(matched {pos}/{len(expect)}); observed: {seen_txt}"
            )
            return res("fail", detail, observed[-1][0])
        at = ", ".join(f"{st}@t={frames[i]['sim_time']:.2f}" for i, st in matched)
        detail = (
            f"{function} passed through {expect} in order ({at}); observed: {seen_txt}"
        )
        return res("pass", detail)

    if kind == "setting_reflected":
        # req-vd-ad:REQ-AD-026 steps e/g/h: a change the driver made to a
        # SETTING has to show up as a step in the corresponding EFFECTIVE
        # value. Two custom_detail keys, read as time series.
        #
        #   function       str   : hvd.adas key (required)
        #   setting_key    str   : e.g. "gt.acc.set_speed_mps" (required)
        #   effective_key  str   : e.g. "gt.acc.effective_cap_mps" (required)
        #   min_step       float : minimum |change| that counts, default 0.5
        #   settle_s       float : seconds allowed for the effective value to
        #                          follow the setting, default 2.0
        #   after/before         : the usual sim_time window
        #
        # WHY A RUN WITH NO CHANGE IS A **FAIL**, NOT A PASS (verification plan
        # §4-2 spells this out): both keys are emitted on every frame, so a
        # controller that stored the setting and never applied it, and a run in
        # which the driver simply never touched the stalk, produce the SAME
        # constant columns. If "no change observed" passed, the matcher would
        # be green on a scenario whose ops profile silently stopped working --
        # the exact fabricated-measurement failure this project has paid for
        # before. So: no setting change in the window => FAIL, with a message
        # that says the stimulus, not the system, is what looks broken.
        for req_key in ("function", "setting_key", "effective_key"):
            if req_key not in must:
                return res(
                    "skip",
                    f"must entry names no {req_key} -- a matcher that checks nothing "
                    "must not report pass",
                )
        function = must["function"]
        setting_key = must["setting_key"]
        effective_key = must["effective_key"]
        min_step = float(must.get("min_step", 0.5))
        settle_s = float(must.get("settle_s", 2.0))
        min_frames = int(must.get("min_frames", 2))
        gated = _gated_frame_indices(frames, must)
        if len(gated) < min_frames:
            return res(
                "skip",
                f"only {len(gated)} frame(s) in time window (< min_frames={min_frames})",
            )
        series = []  # (idx, t, setting, effective)
        for i in gated:
            rec = _hvd_adas_record(frames[i], function)
            s_val = _adas_detail_float(rec, setting_key)
            e_val = _adas_detail_float(rec, effective_key)
            if s_val is None or e_val is None:
                continue
            series.append((i, frames[i]["sim_time"], s_val, e_val))
        if len(series) < 2:
            return res(
                "skip",
                f"{function!r} reported {setting_key}/{effective_key} on only "
                f"{len(series)} frame(s) -- nothing to read as a time series",
            )
        changes = [
            k
            for k in range(1, len(series))
            if abs(series[k][2] - series[k - 1][2]) >= min_step
        ]
        if not changes:
            span = (series[-1][2] - series[0][2]) if series else 0.0
            return res(
                "fail",
                f"{setting_key} never changed by >= {min_step} over "
                f"{len(series)} frame(s) (total drift {span:+.3f}) -- this matcher "
                "cannot evidence 'a setting change was reflected' from a run in "
                "which no setting change happened; check the ops profile, not the "
                "controller",
                series[-1][0],
            )
        # Every change must be followed, within settle_s, by the effective
        # value moving in the SAME direction. Direction rather than equality:
        # the effective value is a min() over the setting, the policy ceiling
        # and (optionally) the speed limit, so it legitimately need not reach
        # the new setting -- but it must not sit still or move the other way.
        for k in changes:
            before_eff = series[k - 1][3]
            direction = 1.0 if series[k][2] > series[k - 1][2] else -1.0
            t0 = series[k][1]
            window = [row for row in series[k:] if row[1] <= t0 + settle_s]
            best = max(
                ((row[3] - before_eff) * direction for row in window), default=0.0
            )
            if best < min_step * 0.5:
                i0 = series[k][0]
                detail = (
                    f"{setting_key} stepped to {series[k][2]:.3f} at t={t0:.2f} but "
                    f"{effective_key} moved only {best * direction:+.3f} within "
                    f"{settle_s}s (want >= {min_step * 0.5:.3f} in the same "
                    "direction) -- the setting was stored but not applied"
                )
                return res("fail", detail, i0)
        detail = (
            f"{len(changes)} {setting_key} change(s) each reflected in "
            f"{effective_key} within {settle_s}s"
        )
        return res("pass", detail)

    if kind == "speed_capped_at":
        # req-vd-ad:REQ-AD-026 step g / REQ-AD-030 step a: the ego's speed in
        # the window stays at or below a cap.
        #
        #   cap        float : cap [m/s] (required unless cap_key is given)
        #   cap_key    str   : read the cap PER FRAME from a custom_detail key
        #                      (e.g. "gt.msl.cap_mps"), which is what a
        #                      speed-limit-linked cap needs
        #   function   str   : hvd.adas key, required when cap_key is used
        #   tolerance  float : allowance above the cap, default 0.5 m/s
        #   after/before     : the usual sim_time window
        #
        # The tolerance exists because a cap is enforced by shutting the
        # throttle, not by braking: a vehicle already above the cap when the
        # window opens, or one on a descent, coasts down rather than being
        # pulled down. Setting it to 0 would turn this matcher into an
        # assertion about the powertrain's drag, which is not what any
        # requirement step claims.
        if "cap" not in must and "cap_key" not in must:
            return res(
                "skip",
                "must entry names neither cap nor cap_key -- a matcher that checks "
                "nothing must not report pass",
            )
        tol = float(must.get("tolerance", 0.5))
        min_frames = int(must.get("min_frames", 1))
        gated = _gated_frame_indices(frames, must)
        if len(gated) < min_frames:
            return res(
                "skip",
                f"only {len(gated)} frame(s) in time window (< min_frames={min_frames})",
            )
        cap_key = must.get("cap_key")
        function = must.get("function")
        if cap_key is not None and function is None:
            return res(
                "skip", "cap_key requires `function` (which row to read it from)"
            )
        samples = []  # (idx, speed, cap)
        src = None
        for i in gated:
            eg = _ego_state(frames[i])
            src = src or eg["_source"]
            if cap_key is None:
                cap = float(must["cap"])
            else:
                cap = _adas_detail_float(_hvd_adas_record(frames[i], function), cap_key)
                if cap is None or cap <= 0.0:
                    # An unreported or zero cap is not a cap of zero -- skipping
                    # the frame is the only reading that does not fabricate one.
                    continue
            samples.append((i, eg["speed"], cap))
        if not samples:
            return res(
                "skip",
                "no gated frame carried both a speed and a usable cap "
                f"({'cap_key ' + repr(cap_key) if cap_key else 'literal cap'})",
                ego_source=src,
            )
        offenders = [(i, v, c) for (i, v, c) in samples if v > c + tol]
        if offenders:
            i0, v0, c0 = max(offenders, key=lambda r: r[1] - r[2])
            detail = (
                f"speed {v0:.2f} exceeded cap {c0:.2f} (+tol {tol}) by "
                f"{v0 - c0:.2f} at t={frames[i0]['sim_time']:.2f} (frame {i0}); "
                f"{len(offenders)}/{len(samples)} frame(s) over"
            )
            return res("fail", detail, i0, ego_source=src)
        worst = max(samples, key=lambda r: r[1] - r[2])
        detail = (
            f"max overshoot {worst[1] - worst[2]:+.2f} m/s vs cap (tol {tol}) over "
            f"{len(samples)} frame(s); peak speed {max(v for _, v, _ in samples):.2f}"
        )
        return res("pass", detail, ego_source=src)

    if kind == "no_brake_output":
        # req-vd-ad:REQ-AD-030 step a (negative): the named function produced
        # no brake in the window. A LIMITER clamps throttle and never brakes,
        # and this is the observation that says so.
        #
        #   function   str   : hvd.adas key (required)
        #   brake_key  str   : custom_detail key, default "<function>.brake_out"
        #   tolerance  float : brake fraction treated as zero, default 0.001
        #
        # Reads the FUNCTION'S OWN brake contribution, not the vehicle's
        # effective brake: the human is free to brake in a limiter scenario and
        # the effective pedal would then be non-zero for a reason that has
        # nothing to do with the claim. A matcher that watched the effective
        # brake would be a trap for exactly the scenario a real driver
        # produces.
        if "function" not in must:
            return res(
                "skip",
                "must entry names no function -- a matcher that checks nothing must "
                "not report pass",
            )
        function = must["function"]
        brake_key = must.get("brake_key", f"{function}.brake_out")
        tol = float(must.get("tolerance", 0.001))
        min_frames = int(must.get("min_frames", 1))
        gated = _gated_frame_indices(frames, must)
        if len(gated) < min_frames:
            return res(
                "skip",
                f"only {len(gated)} frame(s) in time window (< min_frames={min_frames})",
            )
        measured = []  # (idx, brake)
        for i in gated:
            b = _adas_detail_float(_hvd_adas_record(frames[i], function), brake_key)
            if b is None:
                continue
            measured.append((i, b))
        if not measured:
            return res(
                "skip",
                f"{brake_key!r} never reported over {len(gated)} gated frame(s) -- an "
                "unwritten channel cannot evidence 'no brake was produced'",
            )
        offenders = [(i, b) for (i, b) in measured if b > tol]
        if offenders:
            i0, b0 = max(offenders, key=lambda r: r[1])
            detail = (
                f"{brake_key}={b0:.3f} (> tol {tol}) at t={frames[i0]['sim_time']:.2f} "
                f"(frame {i0}) -- the function braked; {len(offenders)}/{len(measured)} "
                "frame(s) non-zero"
            )
            return res("fail", detail, i0)
        detail = (
            f"{brake_key} stayed <= {tol} over all {len(measured)} reported frame(s)"
        )
        return res("pass", detail)

    if kind == "stop_hold_stationary":
        # req-vd-ad:REQ-AD-031 step a: once the vehicle has come to rest under
        # the function's stop hold, it does not creep forward until the human
        # triggers a restart.
        #
        #   max_displacement_m float : allowed travel while held, default 0.5
        #   stop_speed         float : speed at/below which "stopped" starts,
        #                              default 0.2 m/s
        #   function/hold_key  str   : optional -- when given, the hold window
        #                              is taken from that boolean custom_detail
        #                              key (e.g. gt.acc / gt.acc.stop_hold)
        #                              instead of from the speed alone
        #   after/before             : the usual sim_time window
        #
        # WHY hold_key MATTERS: "the car did not move" is also true of a car
        # nobody was holding. Anchoring the window on the function's own
        # stop_hold flag is what makes this a claim about the HOLD rather than
        # about the scenario happening to end at a standstill. Without it the
        # matcher still runs (speed-anchored), but it cannot tell the two
        # apart, so a run that never engaged the hold SKIPs rather than passes.
        max_disp = float(must.get("max_displacement_m", 0.5))
        stop_speed = float(must.get("stop_speed", 0.2))
        function = must.get("function")
        hold_key = must.get("hold_key")
        min_frames = int(must.get("min_frames", 2))
        gated = _gated_frame_indices(frames, must)
        if len(gated) < min_frames:
            return res(
                "skip",
                f"only {len(gated)} frame(s) in time window (< min_frames={min_frames})",
            )
        if hold_key is not None and function is None:
            return res(
                "skip", "hold_key requires `function` (which row to read it from)"
            )

        held = []  # (idx, x, y)
        src = None
        for i in gated:
            eg = _ego_state(frames[i])
            src = src or eg["_source"]
            if hold_key is not None:
                rec = _hvd_adas_record(frames[i], function)
                detail_map = (rec or {}).get("detail") or {}
                if str(detail_map.get(hold_key, "")).lower() != "true":
                    continue
            elif eg["speed"] > stop_speed:
                continue
            held.append((i, eg["x"], eg["y"]))
        if len(held) < 2:
            what = f"{hold_key!r} true" if hold_key else f"speed <= {stop_speed}"
            return res(
                "skip",
                f"only {len(held)} frame(s) with {what} -- the run never held a stop, "
                "so it cannot evidence 'no creep while held'",
                ego_source=src,
            )
        # Contiguity: the hold can legitimately engage more than once in a run
        # (a queue that stops twice). Measuring displacement across the whole
        # union of held frames would count the travel BETWEEN two holds as
        # creep. Measure per contiguous run and take the worst.
        runs: list[list[tuple[int, float, float]]] = [[held[0]]]
        for row in held[1:]:
            if row[0] == runs[-1][-1][0] + 1:
                runs[-1].append(row)
            else:
                runs.append([row])
        worst_disp, worst_idx = 0.0, held[-1][0]
        for run in runs:
            x0, y0 = run[0][1], run[0][2]
            for i, x, y in run:
                d = math.hypot(x - x0, y - y0)
                if d > worst_disp:
                    worst_disp, worst_idx = d, i
        if worst_disp > max_disp:
            detail = (
                f"moved {worst_disp:.3f} m while held (max {max_disp}) by "
                f"t={frames[worst_idx]['sim_time']:.2f} (frame {worst_idx}) -- creep"
            )
            return res("fail", detail, worst_idx, ego_source=src)
        detail = (
            f"max displacement {worst_disp:.3f} m over {len(held)} held frame(s) in "
            f"{len(runs)} hold(s) (max {max_disp})"
        )
        return res("pass", detail, ego_source=src)

    if kind == "restart_after_trigger":
        # req-vd-ad:REQ-AD-031 step a, the other half: after the human's
        # accelerator trigger the vehicle actually moves off again.
        #
        #   trigger_after float : sim_time of the accelerator pulse (required)
        #   min_speed     float : speed the ego must reach, default 1.0 m/s
        #   within_s      float : how long after the trigger it has to,
        #                         default 5.0
        #
        # The PRE-trigger half of the claim ("it did not move before") belongs
        # to stop_hold_stationary; keeping them apart means a run that never
        # stopped at all fails the right one of the two, instead of one
        # combined matcher reporting a single ambiguous red.
        if "trigger_after" not in must:
            return res(
                "skip",
                "must entry names no trigger_after -- a matcher that checks nothing "
                "must not report pass",
            )
        t_trigger = float(must["trigger_after"])
        min_speed = float(must.get("min_speed", 1.0))
        within_s = float(must.get("within_s", 5.0))
        gated = _gated_frame_indices(frames, must)
        if not gated:
            return res("skip", "no frames in time window")
        after = [
            i
            for i in gated
            if t_trigger <= frames[i]["sim_time"] <= t_trigger + within_s
        ]
        if not after:
            return res(
                "skip",
                f"no frame in [{t_trigger}, {t_trigger + within_s}] -- the run ended "
                "before the restart window",
            )
        src = _ego_state(frames[after[0]])["_source"]
        speeds = {i: _ego_state(frames[i])["speed"] for i in after}
        moving = [i for i in after if speeds[i] >= min_speed]
        if not moving:
            i0 = max(speeds, key=speeds.get)
            detail = (
                f"peak speed {speeds[i0]:.2f} within {within_s}s of the trigger at "
                f"t={t_trigger} (want >= {min_speed}) -- did not restart"
            )
            return res("fail", detail, i0, ego_source=src)
        i0 = moving[0]
        detail = (
            f"reached {speeds[i0]:.2f} m/s at t={frames[i0]['sim_time']:.2f} "
            f"({frames[i0]['sim_time'] - t_trigger:.2f}s after the trigger)"
        )
        return res("pass", detail, ego_source=src)

    if kind == "route_lane_plan_holds":
        # vd-func:FUNC-050 (レーンレベル経路計画) / RouteLanePlan.hpp conformance.
        # Reads telemetry.route_lane (VirtualDriverTelemetryJson.cpp's additive
        # "route_lane" block: valid/road_id/ego_lane/ego_lane_raw/target_lanes/
        # on_target_lane/dist_to_connection/deviation_count/
        # last_deviation_road_id/rerouted/diagnostic/reason -- see
        # docs/virtualdriver/design/route_lane_plan_design.md). Every expect_*
        # key below is independently OPTIONAL -- same "check only what the
        # caller asked for" contract as domain_split_holds above -- so a must
        # entry naming NONE of them is refused (skip, not pass): a matcher
        # that checks nothing must never report pass (this is the same
        # discipline as the "nothing evaluated is not a pass" F7 hardening on
        # assert_expectations, below, one layer further down / per-signal
        # rather than per-scenario).
        #
        #   expect_diagnostic     str  : every gated frame's diagnostic == this
        #   expect_rerouted       bool : every gated frame's rerouted == this
        #   expect_target_lanes   list : >=1 gated frame's target_lanes,
        #                                sorted, == sorted(this)
        #   expect_on_target_lane bool : >=1 gated frame's on_target_lane == this
        #   min_deviations        int  : LAST gated frame's deviation_count >= this
        #   max_deviations         int  : LAST gated frame's deviation_count <= this
        #                                (vd-func:FUNC-055 / lane_change_initiation
        #                                 design doc §7 -- the success-side
        #                                 counterpart to min_deviations; a value of 0
        #                                 asserts the ego reached its target lane band
        #                                 without ever crossing off-plan)
        #   window: [t0, t1]           : optional sim_time gate (default: all frames)
        window = must.get("window")
        if window is not None:
            t0, t1 = float(window[0]), float(window[1])
            gated = [i for i in range(len(frames)) if t0 <= frames[i]["sim_time"] <= t1]
        else:
            gated = list(range(len(frames)))
        if not gated:
            return res("skip", "no frames in time window")

        # route_lane is an additive telemetry block that postdates this
        # scenario's baseline captures: a frame dict simply omits the key on a
        # DLL built before RouteLanePlan was wired in. Absence must surface as
        # needs-review (via skip -> the assert_expectations rollup), never as
        # a silent pass -- the same "zero evaluated is not a pass" discipline
        # as the F7 hardening below, applied per-signal instead of
        # per-scenario.
        with_block = [i for i in gated if isinstance(frames[i].get("route_lane"), dict)]
        if not with_block:
            return res(
                "skip",
                "no frame in the gated window carries a route_lane block -- "
                "stale GT_esminiLib.dll (predates RouteLanePlan telemetry) or "
                "the feature is not wired into this run",
            )

        checks = [
            k
            for k in (
                "expect_diagnostic",
                "expect_rerouted",
                "expect_target_lanes",
                "expect_on_target_lane",
                "min_deviations",
                "max_deviations",
            )
            if k in must
        ]
        if not checks:
            return res(
                "skip",
                "must entry names none of expect_diagnostic/expect_rerouted/"
                "expect_target_lanes/expect_on_target_lane/min_deviations/"
                "max_deviations -- a matcher that checks nothing must not "
                "report pass",
            )

        # 1. diagnostic is plan-level and must hold on EVERY gated frame that
        # carries the block (it is a static property of the cached plan, not a
        # per-frame match outcome).
        if "expect_diagnostic" in must:
            want = must["expect_diagnostic"]
            offenders = [
                i
                for i in with_block
                if frames[i]["route_lane"].get("diagnostic") != want
            ]
            if offenders:
                i0 = offenders[0]
                got = frames[i0]["route_lane"].get("diagnostic")
                return res(
                    "fail",
                    f"diagnostic == {got!r} at t={frames[i0]['sim_time']:.2f} "
                    f"(want {want!r} on every frame)",
                    i0,
                )

        # 2. rerouted is likewise plan-level: every gated frame must agree.
        if "expect_rerouted" in must:
            want = bool(must["expect_rerouted"])
            offenders = [
                i
                for i in with_block
                if bool(frames[i]["route_lane"].get("rerouted")) != want
            ]
            if offenders:
                i0 = offenders[0]
                got = bool(frames[i0]["route_lane"].get("rerouted"))
                return res(
                    "fail",
                    f"rerouted == {got} at t={frames[i0]['sim_time']:.2f} "
                    f"(want {want} on every frame)",
                    i0,
                )

        # 3. target_lanes is the band for whichever road the ego is currently
        # matched against, so it legitimately varies frame to frame (a new
        # road -> a new band, or no band at all off-plan) -- an EXISTS check,
        # not an ALL check.
        if "expect_target_lanes" in must:
            want = sorted(must["expect_target_lanes"])
            found = any(
                sorted(frames[i]["route_lane"].get("target_lanes") or []) == want
                for i in with_block
            )
            if not found:
                i0 = with_block[-1]
                got = sorted(frames[i0]["route_lane"].get("target_lanes") or [])
                return res(
                    "fail",
                    f"no gated frame had target_lanes (sorted) == {want}; "
                    f"last observed {got} at t={frames[i0]['sim_time']:.2f}",
                    i0,
                )

        # 4. on_target_lane: likewise an EXISTS check (it tracks target_lanes,
        # which varies with the matched road).
        if "expect_on_target_lane" in must:
            want = bool(must["expect_on_target_lane"])
            found = any(
                bool(frames[i]["route_lane"].get("on_target_lane")) == want
                for i in with_block
            )
            if not found:
                i0 = with_block[-1]
                got = bool(frames[i0]["route_lane"].get("on_target_lane"))
                return res(
                    "fail",
                    f"no gated frame observed on_target_lane == {want} "
                    f"(last observed {got} at t={frames[i0]['sim_time']:.2f})",
                    i0,
                )

        # 5. deviation_count is a cumulative counter (ControllerVirtualDriver's
        # route_lane_deviations_) -- only the LAST gated frame's value is
        # meaningful; it is a running total, not a per-frame condition.
        if "min_deviations" in must:
            want = int(must["min_deviations"])
            i0 = with_block[-1]
            got = int(frames[i0]["route_lane"].get("deviation_count", 0))
            if got < want:
                return res(
                    "fail",
                    f"final deviation_count = {got} at t={frames[i0]['sim_time']:.2f} "
                    f"(want >= {want})",
                    i0,
                )

        # 6. max_deviations mirrors min_deviations (same cumulative counter,
        # same "only the LAST gated frame is meaningful" reasoning) with the
        # comparison reversed: this is the success-side assertion (§7 of
        # lane_change_initiation.md) -- e.g. max_deviations: 0 asserts the ego
        # never crossed off-plan before reaching its target lane band.
        if "max_deviations" in must:
            want = int(must["max_deviations"])
            i0 = with_block[-1]
            got = int(frames[i0]["route_lane"].get("deviation_count", 0))
            if got > want:
                return res(
                    "fail",
                    f"final deviation_count = {got} at t={frames[i0]['sim_time']:.2f} "
                    f"(want <= {want})",
                    i0,
                )

        detail = (
            f"route_lane_plan_holds: {len(checks)} check(s) held over "
            f"{len(with_block)}/{len(gated)} gated frame(s)"
        )
        return res("pass", detail)

    if kind == "indicator_leads_lane_change":
        # vd-func:FUNC-055 pre-signal timing (docs/virtualdriver/design/lane_change_initiation.md
        # section 11-9). Unlike route_lane_plan_holds just above -- which section 7 deliberately
        # did NOT extend with a new matcher because it added no new OBSERVED quantity -- this DOES
        # observe a new quantity (the indicator lamp, telemetry.indicator.left/right) alongside the
        # new telemetry.lane_change.signal_active (section 11-8), so section 11-9 calls for a
        # dedicated matcher rather than folding this into route_lane_plan_holds's route_lane-only
        # reads.
        #
        #   min_lead_s   float (required) : t_arm - t_sig must be >= this
        #   window: [t0, t1]     (optional): sim_time gate (default: all frames)
        if "min_lead_s" not in must:
            return res(
                "skip",
                "must entry names no min_lead_s -- a matcher that checks nothing must not "
                "report pass",
            )
        min_lead_s = float(must["min_lead_s"])

        window = must.get("window")
        if window is not None:
            t0, t1 = float(window[0]), float(window[1])
            gated = [i for i in range(len(frames)) if t0 <= frames[i]["sim_time"] <= t1]
        else:
            gated = list(range(len(frames)))
        if not gated:
            return res("skip", "no frames in time window")

        # Same "absence is needs-review, not silent pass" discipline as route_lane_plan_holds
        # above: a frame dict simply omits "lane_change" on a DLL built before LaneChangeInitiation
        # was wired in, and omits "signal_active" within it on a DLL built before section 11's
        # pre-signal telemetry landed even if lane_change itself is present.
        with_lc = [i for i in gated if isinstance(frames[i].get("lane_change"), dict)]
        if not with_lc:
            return res(
                "skip",
                "no frame in the gated window carries a lane_change block -- stale "
                "GT_esminiLib.dll (predates LaneChangeInitiation telemetry) or the feature is "
                "not wired into this run",
            )
        with_signal_key = [
            i for i in with_lc if "signal_active" in frames[i]["lane_change"]
        ]
        if not with_signal_key:
            return res(
                "skip",
                "no gated frame's lane_change block carries signal_active -- stale "
                "GT_esminiLib.dll (predates the design doc section 11 pre-signal telemetry)",
            )

        # 3. First frame signal_active goes true.
        sig_idx = next(
            (
                i
                for i in with_signal_key
                if frames[i]["lane_change"].get("signal_active")
            ),
            None,
        )
        if sig_idx is None:
            return res(
                "fail",
                "lane_change.signal_active never became true in the gated window -- the "
                "indicator never pre-signaled the lane change",
            )
        t_sig = frames[sig_idx]["sim_time"]

        # 4. First frame armed goes true (searched over the same with_lc set -- armed does not
        # require the signal_active key to be present, only lane_change itself).
        arm_idx = next(
            (i for i in with_lc if frames[i]["lane_change"].get("armed")), None
        )
        if arm_idx is None:
            return res(
                "fail",
                f"lane_change.armed never became true (signal_active first true at "
                f"t={t_sig:.2f}) -- the lane change never initiated, so the lead cannot be "
                f"measured",
            )
        t_arm = frames[arm_idx]["sim_time"]

        # 5. Lead requirement.
        lead = t_arm - t_sig
        if lead < min_lead_s:
            return res(
                "fail",
                f"indicator led the lane change by only {lead:.2f}s (t_sig={t_sig:.2f}, "
                f"t_arm={t_arm:.2f}; want >= {min_lead_s}s)",
                arm_idx,
            )

        # 6. Indicator lamp must stay lit (left or right) for the WHOLE [t_sig, t_arm] window --
        # the intent (signal_active) reaching the lamp (indicator.left/right), per section 11-8's
        # "matcher は両方を見る".
        lit_window = [i for i in gated if t_sig <= frames[i]["sim_time"] <= t_arm]
        with_indicator = [
            i for i in lit_window if isinstance(frames[i].get("indicator"), dict)
        ]
        if not with_indicator:
            return res(
                "skip",
                "no frame in [t_sig, t_arm] carries an indicator block -- stale "
                "GT_esminiLib.dll or the feature is not wired into this run",
            )
        dark = [
            i
            for i in with_indicator
            if not (
                frames[i]["indicator"].get("left")
                or frames[i]["indicator"].get("right")
            )
        ]
        if dark:
            i0 = dark[0]
            return res(
                "fail",
                f"indicator was dark at t={frames[i0]['sim_time']:.2f}, inside "
                f"[t_sig={t_sig:.2f}, t_arm={t_arm:.2f}] -- the pre-signal intent never reached "
                f"the lamp",
                i0,
            )

        # 7. Lit side must agree with the hop's direction at t_arm (lane_change.direction: +1 left
        # / -1 right, only meaningful once armed -- see VirtualDriverTypes.hpp).
        arm_direction = frames[arm_idx]["lane_change"].get("direction")
        arm_indicator = frames[arm_idx].get("indicator") or {}
        lit_left = bool(arm_indicator.get("left"))
        lit_right = bool(arm_indicator.get("right"))
        direction_ok = (arm_direction == 1 and lit_left) or (
            arm_direction == -1 and lit_right
        )
        if not direction_ok:
            return res(
                "fail",
                f"at t_arm={t_arm:.2f} lane_change.direction={arm_direction} but indicator "
                f"left={lit_left} right={lit_right} -- the lit side does not match the hop's "
                f"direction",
                arm_idx,
            )

        detail = (
            f"indicator led the lane change by {lead:.2f}s (t_sig={t_sig:.2f}, "
            f"t_arm={t_arm:.2f}, want >= {min_lead_s}s)"
        )
        return res("pass", detail)

    if kind == "indicator_leads_junction_turn":
        # req-vd-ad:REQ-AD-021 (docs/virtualdriver/design/junction_turn_signal.md section 4).
        # Sibling of indicator_leads_lane_change just above, same skip/fail discipline, but this
        # one measures a DISTANCE lead rather than a time lead: the legal requirement here
        # ("30 m before the junction") is distance-based (section 2-2), unlike the lane-change
        # side's 3-second rule. Reads the new telemetry.junction_turn block (section 3-4):
        # {dir: +1 left/-1 right/0 none, dist_to_entry_m, on_connector}, alongside the existing
        # telemetry.indicator.left/right lamp state.
        #
        #   min_distance_m float (required unless expect_dir == "none")
        #   expect_dir      "left" | "right" | "none" (required; "none" is the negative check
        #                   for a straight pass-through -- checks 1-5 below do not apply to it)
        #   window: [t0, t1]     (optional): sim_time gate (default: all frames)
        if "expect_dir" not in must:
            return res(
                "skip",
                "must entry names no expect_dir -- a matcher that checks nothing must not "
                "report pass",
            )
        expect_dir = str(must["expect_dir"]).lower()
        if expect_dir not in ("left", "right", "none"):
            return res(
                "skip", f"expect_dir must be left/right/none, got {expect_dir!r}"
            )

        window = must.get("window")
        if window is not None:
            t0, t1 = float(window[0]), float(window[1])
            gated = [i for i in range(len(frames)) if t0 <= frames[i]["sim_time"] <= t1]
        else:
            gated = list(range(len(frames)))
        if not gated:
            return res("skip", "no frames in time window")

        # Same "absence is needs-review, not silent pass" discipline as indicator_leads_lane_change:
        # a frame dict simply omits "junction_turn" on a DLL built before this telemetry block
        # landed (it is being added by a parallel change at the same time as this matcher).
        with_jt = [i for i in gated if isinstance(frames[i].get("junction_turn"), dict)]
        if not with_jt:
            return res(
                "skip",
                "no frame in the gated window carries a junction_turn block -- stale "
                "GT_esminiLib.dll (predates the junction turn signal telemetry) or the feature "
                "is not wired into this run",
            )

        # --- expect_dir == "none": negative check for a straight pass-through -----------------
        # Checked two ways, both independently sufficient to catch a regression: (a) the
        # geometry classifier itself must never call this a turn (dir stays 0), and (b) the
        # lamp must never light while junction_turn telemetry is present at all -- (b) does not
        # assume anything about how dir feeds the lamp condition internally, so it still catches
        # a lamp lit via some other path (e.g. on_connector alone) that (a) could miss.
        if expect_dir == "none":
            bad_dir = [
                i
                for i in with_jt
                if int(frames[i]["junction_turn"].get("dir") or 0) != 0
            ]
            if bad_dir:
                i0 = bad_dir[0]
                got = frames[i0]["junction_turn"].get("dir")
                return res(
                    "fail",
                    f"junction_turn.dir == {got} at t={frames[i0]['sim_time']:.2f} (frame {i0}), "
                    f"expected 0 throughout -- expect_dir: none asserts a straight pass-through "
                    f"is never classified as a turn",
                    i0,
                )
            with_indicator = [
                i for i in with_jt if isinstance(frames[i].get("indicator"), dict)
            ]
            lit = [
                i
                for i in with_indicator
                if frames[i]["indicator"].get("left")
                or frames[i]["indicator"].get("right")
            ]
            if lit:
                i0 = lit[0]
                return res(
                    "fail",
                    f"indicator lit at t={frames[i0]['sim_time']:.2f} (frame {i0}) while "
                    f"junction_turn telemetry was present -- expected dark throughout for a "
                    f"straight pass-through",
                    i0,
                )
            return res(
                "pass",
                f"junction_turn.dir stayed 0 and the indicator stayed dark over "
                f"{len(with_jt)} gated frame(s) -- no spurious turn-signal on a straight "
                f"pass-through",
            )

        # --- expect_dir in (left, right): positive distance-lead check ------------------------
        if "min_distance_m" not in must:
            return res(
                "skip",
                "must entry names no min_distance_m -- a matcher that checks nothing must not "
                "report pass",
            )
        min_distance_m = float(must["min_distance_m"])
        want_dir_sign = 1 if expect_dir == "left" else -1

        with_indicator = [
            i for i in with_jt if isinstance(frames[i].get("indicator"), dict)
        ]
        if not with_indicator:
            return res(
                "skip",
                "no frame in the gated window carries an indicator block -- stale "
                "GT_esminiLib.dll or the feature is not wired into this run",
            )

        def _lit_for_dir(i: int) -> bool:
            ind = frames[i]["indicator"]
            return (
                bool(ind.get("left")) if want_dir_sign == 1 else bool(ind.get("right"))
            )

        # 1. rising frame where the indicator is lit on the expected side WHILE junction_turn.dir
        # already reports that same side -- an indicator lit for some unrelated reason (e.g. a
        # concurrent lane change) must not be credited to this maneuver.
        sig_idx = next(
            (
                i
                for i in with_indicator
                if int(frames[i]["junction_turn"].get("dir") or 0) == want_dir_sign
                and _lit_for_dir(i)
            ),
            None,
        )
        if sig_idx is None:
            return res(
                "fail",
                f"indicator never lit for junction_turn.dir == {want_dir_sign} ({expect_dir}) "
                f"in the gated window -- the turn signal never anticipated the {expect_dir} "
                f"junction turn",
            )
        t_sig = frames[sig_idx]["sim_time"]

        # 2. first frame at/after sig_idx where junction_turn.on_connector becomes true.
        junc_idx = next(
            (
                i
                for i in with_jt
                if i >= sig_idx and bool(frames[i]["junction_turn"].get("on_connector"))
            ),
            None,
        )
        if junc_idx is None:
            return res(
                "fail",
                f"junction_turn.on_connector never became true after t_sig={t_sig:.2f} (frame "
                f"{sig_idx}) -- the ego never reached the connecting road, so the lead distance "
                f"cannot be measured",
                sig_idx,
            )
        t_junc = frames[junc_idx]["sim_time"]

        # 3. distance requirement -- frame-to-frame Euclidean sum over [sig_idx, junc_idx], the
        # same technique domain_split_holds uses above (robust to a captured-frame time-
        # duplicate at the batch tail, unlike speed * dt).
        span = [i for i in gated if sig_idx <= i <= junc_idx]
        dist = 0.0
        for a, b in zip(span, span[1:]):
            dist += math.hypot(
                float(frames[b]["ego"]["x"]) - float(frames[a]["ego"]["x"]),
                float(frames[b]["ego"]["y"]) - float(frames[a]["ego"]["y"]),
            )
        if dist < min_distance_m:
            return res(
                "fail",
                f"indicator led the junction turn by only {dist:.2f} m (t_sig={t_sig:.2f} frame "
                f"{sig_idx}, t_junc={t_junc:.2f} frame {junc_idx}; want >= {min_distance_m} m)",
                junc_idx,
            )

        # 4. no dark frame within [sig_idx, junc_idx] -- the pre-signal intent must hold all the
        # way to junction entry, not flicker.
        with_indicator_span = [
            i for i in span if isinstance(frames[i].get("indicator"), dict)
        ]
        dark = [
            i
            for i in with_indicator_span
            if not (
                frames[i]["indicator"].get("left")
                or frames[i]["indicator"].get("right")
            )
        ]
        if dark:
            i0 = dark[0]
            return res(
                "fail",
                f"indicator was dark at t={frames[i0]['sim_time']:.2f} (frame {i0}), inside "
                f"[t_sig={t_sig:.2f}, t_junc={t_junc:.2f}] -- the pre-signal did not hold "
                f"through junction entry",
                i0,
            )

        # 5. defect-3 regression guard (junction_turn_signal.md section 1, row 3: "曲り終わる前
        # に消える" -- extinguished before the turn finishes). The indicator must stay lit for
        # as long as junction_turn.on_connector stays true, i.e. through the frame right before
        # on_connector returns to false.
        on_connector_span = [i for i in gated if i >= junc_idx]
        off_idx = next(
            (
                i
                for i in on_connector_span
                if not bool(frames[i]["junction_turn"].get("on_connector"))
            ),
            None,
        )
        hold_span = (
            on_connector_span
            if off_idx is None
            else [i for i in on_connector_span if i < off_idx]
        )
        with_indicator_hold = [
            i for i in hold_span if isinstance(frames[i].get("indicator"), dict)
        ]
        dark_on_connector = [
            i
            for i in with_indicator_hold
            if not (
                frames[i]["indicator"].get("left")
                or frames[i]["indicator"].get("right")
            )
        ]
        if dark_on_connector:
            i0 = dark_on_connector[0]
            return res(
                "fail",
                f"indicator went dark at t={frames[i0]['sim_time']:.2f} (frame {i0}) while "
                f"junction_turn.on_connector was still true -- defect-3 regression (the signal "
                f"must stay lit until the maneuver is complete, not just for "
                f"indicator_min_on_time)",
                i0,
            )
        if off_idx is None:
            end_idx = hold_span[-1] if hold_span else junc_idx
            return res(
                "fail",
                f"junction_turn.on_connector never returned to false by the end of the gated "
                f"window (last checked frame {end_idx}, t={frames[end_idx]['sim_time']:.2f}) -- "
                f"cannot confirm the signal held through the whole maneuver",
                end_idx,
            )

        # 6. lit side agrees with expect_dir at junction entry (re-checked explicitly here, on
        # top of the sig_idx/junc_idx search already requiring it, for a clear failure message
        # if the lamp flipped sides mid-maneuver).
        junc_indicator = frames[junc_idx].get("indicator") or {}
        lit_left = bool(junc_indicator.get("left"))
        lit_right = bool(junc_indicator.get("right"))
        direction_ok = (want_dir_sign == 1 and lit_left) or (
            want_dir_sign == -1 and lit_right
        )
        if not direction_ok:
            return res(
                "fail",
                f"at t_junc={t_junc:.2f} (frame {junc_idx}) expected {expect_dir} "
                f"(junction_turn.dir={want_dir_sign}) but indicator left={lit_left} "
                f"right={lit_right}",
                junc_idx,
            )

        detail = (
            f"indicator led the junction turn ({expect_dir}) by {dist:.2f} m (t_sig={t_sig:.2f} "
            f"frame {sig_idx}, t_junc={t_junc:.2f} frame {junc_idx}, want >= {min_distance_m} m); "
            f"held lit through on_connector until t={frames[off_idx]['sim_time']:.2f} "
            f"(frame {off_idx})"
        )
        return res("pass", detail)

    if kind == "overtake_decision_holds":
        # vd-func:FUNC-056 (overtake maneuver, docs/virtualdriver/design/overtake_maneuver.md
        # section 9-2). Reads the new telemetry.overtake block (section 9-1, additive): phase /
        # considered / lead_id / delta_v_mps / t_pass_s / required_m / route_budget_m /
        # blocked_reason / cleared_lead. Unlike route_lane_plan_holds above (section 7 of
        # lane_change_initiation.md deliberately did NOT add a matcher there because no new
        # observed quantity existed) this DOES observe new quantities, hence a dedicated matcher
        # -- the design doc's own section 9-2 reasoning for why.
        #
        #   expect_considered    bool  : considered==true on >=1 gated frame (want True); on
        #                                EVERY gated frame when want False (mirrors the "never
        #                                even happened" negative check). This is the "偽PASS
        #                                検知の要" the design doc calls out: a scenario where the
        #                                overtake decision was never evaluated at all must never
        #                                be reported as "the guard held" -- see
        #                                overtake_declined_before_route_branch.expectations.yaml.
        #                                The want=False semantics ("never true on any gated
        #                                frame", mirrored below for expect_cleared_lead too) is
        #                                an interpretation this author chose because the design
        #                                doc only specified the want=True case explicitly --
        #                                CONFIRMED by the coordinating session (2026-08-04): this
        #                                is the correct, final semantics, not a placeholder.
        #   expect_blocked_reason str   : blocked_reason == this value on >=1 gated frame
        #   expect_phases         list  : this exact order of phase values must appear as a
        #                                SUBSEQUENCE of the gated frames' phase readings (not
        #                                necessarily contiguous -- a phase that persists across
        #                                many frames, e.g. a long PASS while gap-waiting, must
        #                                not break the match)
        #   forbid_phases         list  : none of these phase values ever appear on any gated
        #                                frame
        #   expect_cleared_lead   bool  : cleared_lead==true on >=1 gated frame (want True), or
        #                                on EVERY gated frame when want False (same "never
        #                                happened" mirroring as expect_considered)
        #   window: [t0, t1]            : optional sim_time gate (default: all frames)
        #
        # Same "checks nothing -> skip, not pass" discipline as route_lane_plan_holds /
        # indicator_leads_lane_change above, applied per-signal: a must entry naming none of the
        # five expect_*/forbid_phases keys is refused.
        checks = [
            k
            for k in (
                "expect_considered",
                "expect_blocked_reason",
                "expect_phases",
                "forbid_phases",
                "expect_cleared_lead",
            )
            if k in must
        ]
        if not checks:
            return res(
                "skip",
                "must entry names none of expect_considered/expect_blocked_reason/"
                "expect_phases/forbid_phases/expect_cleared_lead -- a matcher that checks "
                "nothing must not report pass",
            )

        window = must.get("window")
        if window is not None:
            t0, t1 = float(window[0]), float(window[1])
            gated = [i for i in range(len(frames)) if t0 <= frames[i]["sim_time"] <= t1]
        else:
            gated = list(range(len(frames)))
        if not gated:
            return res("skip", "no frames in time window")

        # overtake is an additive telemetry block that postdates this scenario's baseline
        # captures, same reasoning as route_lane / lane_change above: a frame dict simply omits
        # the key on a DLL built before OvertakeManeuver was wired in.
        with_block = [i for i in gated if isinstance(frames[i].get("overtake"), dict)]
        if not with_block:
            return res(
                "skip",
                "no frame in the gated window carries an overtake block -- stale "
                "GT_esminiLib.dll (predates OvertakeManeuver telemetry) or the feature is not "
                "wired into this run",
            )

        # 1. expect_considered.
        if "expect_considered" in must:
            want = bool(must["expect_considered"])
            considered_true = [
                i for i in with_block if bool(frames[i]["overtake"].get("considered"))
            ]
            if want and not considered_true:
                return res(
                    "fail",
                    f"overtake.considered never became true over {len(with_block)} gated "
                    f"frame(s) -- the overtake decision was never evaluated (a 'guard held' "
                    f"green here would be a false pass)",
                )
            if not want and considered_true:
                i0 = considered_true[0]
                return res(
                    "fail",
                    f"overtake.considered == true at t={frames[i0]['sim_time']:.2f} (frame "
                    f"{i0}) (want false on every gated frame)",
                    i0,
                )

        # 2. expect_blocked_reason: EXISTS check (>=1 gated frame).
        if "expect_blocked_reason" in must:
            want_reason = str(must["expect_blocked_reason"])
            found = [
                i
                for i in with_block
                if frames[i]["overtake"].get("blocked_reason") == want_reason
            ]
            if not found:
                i0 = with_block[-1]
                got = frames[i0]["overtake"].get("blocked_reason")
                return res(
                    "fail",
                    f"no gated frame had blocked_reason == {want_reason!r}; last observed "
                    f"{got!r} at t={frames[i0]['sim_time']:.2f} (frame {i0})",
                    i0,
                )

        # 3. expect_phases: SUBSEQUENCE match over the gated frames' phase readings, in time
        # order.
        if "expect_phases" in must:
            want_seq = list(must["expect_phases"])
            observed = [frames[i]["overtake"].get("phase") for i in with_block]
            pos = 0
            for ph in observed:
                if pos < len(want_seq) and ph == want_seq[pos]:
                    pos += 1
            if pos < len(want_seq):
                observed_preview = (
                    observed
                    if len(observed) <= 40
                    else (observed[:20] + ["..."] + observed[-20:])
                )
                return res(
                    "fail",
                    f"expect_phases {want_seq} not observed as a subsequence (matched "
                    f"{pos}/{len(want_seq)} entries); observed phase sequence: "
                    f"{observed_preview}",
                    with_block[-1],
                )

        # 4. forbid_phases: none of these ever observed.
        if "forbid_phases" in must:
            forbidden = set(must["forbid_phases"])
            offenders = [
                i for i in with_block if frames[i]["overtake"].get("phase") in forbidden
            ]
            if offenders:
                i0 = offenders[0]
                got = frames[i0]["overtake"].get("phase")
                return res(
                    "fail",
                    f"overtake.phase == {got!r} at t={frames[i0]['sim_time']:.2f} (frame {i0}) "
                    f"-- forbidden (forbid_phases={sorted(forbidden)})",
                    i0,
                )

        # 5. expect_cleared_lead.
        if "expect_cleared_lead" in must:
            want = bool(must["expect_cleared_lead"])
            cleared_true = [
                i for i in with_block if bool(frames[i]["overtake"].get("cleared_lead"))
            ]
            if want and not cleared_true:
                return res(
                    "fail",
                    f"overtake.cleared_lead never became true over {len(with_block)} gated "
                    f"frame(s)",
                )
            if not want and cleared_true:
                i0 = cleared_true[0]
                return res(
                    "fail",
                    f"overtake.cleared_lead == true at t={frames[i0]['sim_time']:.2f} (frame "
                    f"{i0}) (want false on every gated frame)",
                    i0,
                )

        detail = (
            f"overtake_decision_holds: {len(checks)} check(s) held over "
            f"{len(with_block)}/{len(gated)} gated frame(s)"
        )
        return res("pass", detail)

    return {
        "event": kind,
        "status": "skip",
        "detail": "unknown event type",
        "reason": reason,
    }


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
    # feature:F7 — NOTHING EVALUATED IS NOT A PASS.
    #
    # This chain used to end in a literal "pass", which is reached whenever
    # n_pass == n_fail == n_skip == 0 -- i.e. whenever there were no matchers to
    # run at all. Every one of these produces that state and used to come back
    # green:
    #   * the expectations file has no `must:` key,
    #   * `must:` is misspelled (`musts:`, `Must:` ...), so .get("must") misses,
    #   * `must: []`.
    # In each case the scenario was never checked against anything, and the
    # verdict said it passed.
    #
    # This is the same defect class as the 2026-07-27 gate incident, where all
    # 22 scenarios died on WinError 10013 and the run still printed
    # "REGRESSION GATE: PASS" -- fixed there at the batch level, still live here
    # one level down, per scenario. A deviation check over zero matchers is
    # exactly as meaningless as one over zero scenarios.
    #
    # needs-review rather than fail: an expectations file with no musts is not
    # a product failure, it is an UNVERIFIED scenario, and that is what
    # needs-review means. It still counts as "ran" for the gate's coverage
    # accounting and still deviates from a baseline that recorded "pass", so
    # the regression gate catches it either way.
    if not results:
        overall = "needs-review"
    else:
        overall = (
            "fail"
            if n_fail
            else (
                "pass"
                if n_pass and not n_skip
                else "needs-review" if n_skip else "pass"
            )
        )

    verdict = {
        "run": str(run_dir),
        "expectations": str(expectations),
        "scenario": spec.get("scenario") if isinstance(spec, dict) else None,
        "overall": overall,
        "summary": {"pass": n_pass, "fail": n_fail, "skip": n_skip},
        "results": results,
    }
    (run_dir / "verdict.json").write_text(
        json.dumps(verdict, indent=2), encoding="utf-8"
    )
    return verdict


# ---------------------------------------------------------------------------
# OSI UDP capture (multi-packet GroundTruth reassembly -> .osi file)
# ---------------------------------------------------------------------------


def capture_osi(
    out_osi: Path, proc: subprocess.Popen, port: int, idle_timeout: float
) -> int:
    """Reassemble multi-packet GroundTruth frames from UDP into a length-delimited
    .osi file. Stops once the process has exited and the stream is idle.
    Returns the number of complete frames written.

    feature:F7 gate hardening -- checks ``port`` is free immediately before
    binding it (require_udp_port_free). This is the ACTUAL lowest layer that
    binds an OSI capture port for BOTH callers: gt_sim_test.py's own OSI
    capture is a separate in-process implementation (_OsiCapture) with its
    own equivalent check, but services/vd_verify.py's generate_baseline()
    (launches GT_Sim.exe as a subprocess, the web backend's own VERIFY-panel
    baseline generation) calls straight into THIS function with no
    intermediate port check of its own -- an earlier port-hardening pass
    protected gt_sim_test.py's run()/batch() and missed this call site
    entirely, which is exactly backwards: this is closer to the real
    2026-07-27 incident party (a production web-backend code path) than the
    CLI gate script the fix originally targeted. Raising here means neither
    caller needs to remember to check first.
    """
    require_udp_port_free(port, "OSI ground-truth (about to bind for capture)")
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
                    if (
                        proc.poll() is not None
                        and (time.time() - last_data) > idle_timeout
                    ):
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
