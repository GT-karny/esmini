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


def _obb_separation(a: dict, b: dict) -> float:
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
                raise RuntimeError(f"GT_InitWithArgs failed (rc={rc}) for {scenario}")
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

    print(f"[run] {scenario.name}: {len(frames)} frames, {duration:.1f}s -> {jsonl_path}")
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
# telemetry / baseline trajectory extraction
# ---------------------------------------------------------------------------

def _load_telemetry(run_dir: Path) -> list[dict]:
    jsonl = run_dir / "telemetry.jsonl"
    if not jsonl.is_file():
        raise FileNotFoundError(f"{jsonl} not found - run `gt_sim_test run` first")
    out = []
    for line in jsonl.read_text(encoding="utf-8").splitlines():
        if line.strip():
            out.append(json.loads(line))
    return out


def _ego_track_from_telemetry(frames: list[dict]) -> list[tuple[float, float, float, float]]:
    """-> [(t, x, y, speed)]"""
    return [(fr["sim_time"], fr["ego"]["x"], fr["ego"]["y"], fr["ego"]["speed"]) for fr in frames]


def _ego_track_from_osi(osi_path: Path) -> list[tuple[float, float, float, float]]:
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


def _resolve_baseline_osi(baseline: Path) -> Path:
    if baseline.is_file():
        return baseline
    cand = baseline / "groundtruth.osi"
    if cand.is_file():
        return cand
    raise FileNotFoundError(f"No baseline .osi found at {baseline}")


# ---------------------------------------------------------------------------
# compare
# ---------------------------------------------------------------------------

def _interp(track: list[tuple[float, float, float, float]], t: float) -> tuple[float, float, float]:
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
    vd = _ego_track_from_telemetry(_load_telemetry(run_dir))
    base = _ego_track_from_osi(_resolve_baseline_osi(baseline))
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
        vx, vy, vs = _interp(vd, t)
        bx, by, bs = _interp(base, t)
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
        bx, by, bs = _interp(base, t)
        baseline_track.append({"t": round(t, 3), "x": round(bx, 3), "y": round(by, 3), "speed": round(bs, 3)})
    (run_dir / "baseline_track.json").write_text(
        json.dumps(baseline_track, separators=(",", ":")), encoding="utf-8")

    print(f"[compare] xy_rmse={result['xy_rmse_m']}m  speed_rmse={result['speed_rmse_mps']}m/s  "
          f"max_dev={result['xy_max_dev_m']}m -> {run_dir / 'compare.json'} (+baseline_track.json)")
    return result


# ---------------------------------------------------------------------------
# assert (expectations.yaml)
# ---------------------------------------------------------------------------

def _time_window_ok(t: float, spec: dict) -> bool:
    """Honour optional after/before sim_time gates on a must entry."""
    after = spec.get("after", {})
    before = spec.get("before", {})
    if "sim_time" in after and not (t >= after["sim_time"]):
        return False
    if "sim_time" in before and not (t <= before["sim_time"]):
        return False
    return True


def _speed_accel_jerk(frames: list[dict], smooth_window: int = 5) -> dict:
    """Derive (t, v, s, a, j) series from telemetry for the mid/long matchers.

    Telemetry carries no acceleration, so smooth the speed with a centered moving
    average (odd window, ~0.25 s at dt=0.05) then central-difference twice to get
    acceleration `a` [m/s^2] and jerk `j` [m/s^3]. Endpoints are clamped to their
    neighbours. Returns equal-length lists keyed t/v/s/a/j (n = len(frames))."""
    n = len(frames)
    t = [fr["sim_time"] for fr in frames]
    v_raw = [fr["ego"]["speed"] for fr in frames]
    s = [fr["ego"].get("s", 0.0) for fr in frames]

    w = max(1, int(smooth_window))
    if w % 2 == 0:
        w += 1
    half = w // 2
    v = []
    for i in range(n):
        seg = v_raw[max(0, i - half):min(n, i + half + 1)]
        v.append(sum(seg) / len(seg))

    def _central(y: list[float]) -> list[float]:
        d = [0.0] * n
        for i in range(1, n - 1):
            dt = t[i + 1] - t[i - 1]
            d[i] = (y[i + 1] - y[i - 1]) / dt if dt > 1e-9 else 0.0
        if n > 2:
            d[0], d[-1] = d[1], d[-2]
        return d

    a = _central(v)
    j = _central(a)
    return {"t": t, "v": v, "s": s, "a": a, "j": j}


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
        if not _time_window_ok(fr["sim_time"], must):
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


def _eval_must(must: dict, frames: list[dict]) -> dict:
    """Evaluate one must[] entry. Fail results carry the first offending frame's
    `t` and `idx` so the UI can jump straight to the failure."""
    kind = must.get("event")
    reason = must.get("reason", "")

    def res(status, detail, fail_idx=None):
        out = {"event": kind, "status": status, "detail": detail, "reason": reason}
        if status == "fail" and fail_idx is not None:
            out["idx"] = fail_idx
            out["t"] = round(frames[fail_idx]["sim_time"], 3)
        return out

    if kind in ("speed_above", "speed_below"):
        thr = float(must["threshold"])
        gated = [(i, frames[i]["ego"]["speed"]) for i in range(len(frames))
                 if _time_window_ok(frames[i]["sim_time"], must)]
        if not gated:
            return res("skip", "no frames in time window")
        if kind == "speed_above":
            ok = any(s >= thr for _, s in gated)
            worst_i = max(gated, key=lambda p: p[1])[0]  # closest attempt
            detail = f"max speed in window = {frames[worst_i]['ego']['speed']:.2f} (>= {thr}?)"
            return res("pass" if ok else "fail", detail, None if ok else worst_i)
        else:
            offenders = [i for i, s in gated if s > thr]
            ok = not offenders
            worst = max(s for _, s in gated)
            detail = f"max speed in window = {worst:.2f} (<= {thr}?)"
            return res("pass" if ok else "fail", detail, None if ok else offenders[0])

    if kind == "min_speed_above":
        # Lowest speed in the window must stay above threshold: a "do not slow
        # down here" assertion (e.g. crossing a junction the ego drives straight
        # through). Optional road_id confines the window to one road/connector.
        thr = float(must["threshold"])
        road_id = must.get("road_id")
        gated = [i for i in range(len(frames))
                 if _time_window_ok(frames[i]["sim_time"], must)
                 and (road_id is None or int(frames[i]["ego"].get("track", -10 ** 9)) == road_id)]
        if not gated:
            where = f" on road {road_id}" if road_id is not None else ""
            return res("skip", f"no frames in time window{where}")
        worst_i = min(gated, key=lambda i: frames[i]["ego"]["speed"])
        v_min = frames[worst_i]["ego"]["speed"]
        ok = v_min >= thr
        detail = f"min speed in window = {v_min:.2f} (>= {thr}?)"
        return res("pass" if ok else "fail", detail, None if ok else worst_i)

    if kind == "no_constraint_kind":
        # Assert the mid/long planner never raises a constraint of the given kind
        # in the window. Reads telemetry midlong.constraints[].kind. Used to prove
        # a straight pass-through junction does not emit a "junction" constraint.
        target = must.get("kind")
        if target is None:
            return res("skip", "kind is required")
        gated = [i for i in range(len(frames)) if _time_window_ok(frames[i]["sim_time"], must)]
        if not gated:
            return res("skip", "no frames in time window")
        offenders = [i for i in gated
                     if any(c.get("kind") == target
                            for c in frames[i].get("midlong", {}).get("constraints", []))]
        ok = not offenders
        detail = (f"{len(gated)} frames checked; {len(offenders)} raised a "
                  f"'{target}' constraint")
        return res("pass" if ok else "fail", detail, None if ok else offenders[0])

    # Lane events use the road-coordinate fields the telemetry exposes
    # (ego.lane / ego.track). If a frame lacks them (older DLL) the event skips.
    def lane_series() -> list[tuple[int, int, int]]:
        out = []
        for i, fr in enumerate(frames):
            ego = fr["ego"]
            if "lane" in ego and "track" in ego:
                out.append((i, int(ego["track"]), int(ego["lane"])))
        return out

    if kind == "lane_keep":
        road_id = must.get("road_id")
        lane_id = must.get("lane_id")
        ls = [(i, trk, ln) for (i, trk, ln) in lane_series()
              if _time_window_ok(frames[i]["sim_time"], must)]
        if not ls:
            return res("skip", "no lane data in window (lane/track absent or empty window)")
        bad = [i for (i, trk, ln) in ls
               if (road_id is not None and trk != road_id) or (lane_id is not None and ln != lane_id)]
        detail = (f"{len(ls)} frames in window on lane(s) "
                  f"{sorted(set(ln for _, _, ln in ls))} road(s) {sorted(set(trk for _, trk, _ in ls))}; "
                  f"expected road={road_id} lane={lane_id}")
        return res("pass" if not bad else "fail", detail, None if not bad else bad[0])

    if kind == "lane_change_count":
        expected = must.get("count")
        ls = lane_series()
        if not ls:
            return res("skip", "no lane data")
        changes = [ls[i][0] for i in range(1, len(ls)) if ls[i][2] != ls[i - 1][2]]
        ok = (expected is None) or (len(changes) == expected)
        detail = f"observed {len(changes)} lane change(s); expected {expected}"
        return res("pass" if ok else "fail", detail, None if ok else (changes[0] if changes else None))

    # --- mid/long anticipation matchers (V2) ---------------------------------
    # "Physically plausible deceleration" judged from VirtualDriver telemetry
    # alone (position equivalence vs Default is meaningless for the mid/long case).

    if kind == "deceleration_profile_smooth":
        gated = [i for i in range(len(frames)) if _time_window_ok(frames[i]["sim_time"], must)]
        if len(gated) < 3:
            return res("skip", "fewer than 3 frames in time window")
        prof = _speed_accel_jerk(frames, int(must.get("smooth_window", 5)))

        # Scope jerk/decel to the deceleration approach: [onset -> landmark
        # passage]. This excludes the launch-from-rest spike, the post-landmark
        # re-acceleration, and the recording-tail boundary artifact, none of
        # which are part of the deceleration profile being judged. (Reaching the
        # target speed by the landmark is asserted separately by
        # speed_reduction_before_landmark.)
        road_id = must.get("road_id")
        landmark_s = must.get("landmark_s")
        eval_idx, win = gated, ""
        if landmark_s is not None:
            lm = next((i for i in gated
                       if (road_id is None or int(frames[i]["ego"].get("track", -10 ** 9)) == road_id)
                       and prof["s"][i] >= float(landmark_s)), None)
            if lm is not None:
                onset = next((i for i in gated if i <= lm and prof["a"][i] < -0.3), None)
                if onset is not None and lm - onset >= 2:
                    eval_idx = [i for i in gated if onset <= i <= lm]
                    win = (f" [decel phase t{frames[onset]['sim_time']:.1f}-"
                           f"{frames[lm]['sim_time']:.1f}s]")

        max_jerk = must.get("max_jerk")
        if max_jerk is not None:
            worst = max(eval_idx, key=lambda i: abs(prof["j"][i]))
            if abs(prof["j"][worst]) > float(max_jerk):
                return res("fail",
                           f"max |jerk| = {abs(prof['j'][worst]):.2f} m/s^3 (<= {max_jerk}?){win}",
                           worst)

        max_decel = must.get("max_decel")
        if max_decel is not None:
            worst = min(eval_idx, key=lambda i: prof["a"][i])  # most negative accel
            if -prof["a"][worst] > float(max_decel):
                return res("fail",
                           f"max deceleration = {-prof['a'][worst]:.2f} m/s^2 (<= {max_decel}?){win}",
                           worst)

        wj = max(eval_idx, key=lambda i: abs(prof["j"][i]))
        wa = min(eval_idx, key=lambda i: prof["a"][i])
        return res("pass", f"max |jerk|={abs(prof['j'][wj]):.2f} m/s^3, "
                           f"max decel={-prof['a'][wa]:.2f} m/s^2 within bounds{win}")

    if kind == "speed_reduction_before_landmark":
        landmark_s = must.get("landmark_s")
        target_speed = must.get("target_speed")
        if landmark_s is None or target_speed is None:
            return res("skip", "landmark_s and target_speed are required")
        tol = float(must.get("tolerance", 0.5))
        road_id = must.get("road_id")
        hit = None
        for i in range(len(frames)):
            if not _time_window_ok(frames[i]["sim_time"], must):
                continue
            ego = frames[i]["ego"]
            if "s" not in ego:
                continue
            if road_id is not None and int(ego.get("track", -10 ** 9)) != road_id:
                continue
            if ego["s"] >= float(landmark_s):
                hit = i
                break
        if hit is None:
            where = f" on road {road_id}" if road_id is not None else ""
            return res("skip", f"landmark s={landmark_s}{where} not reached")
        v_hit = frames[hit]["ego"]["speed"]
        ok = v_hit <= float(target_speed) + tol
        detail = f"speed at landmark s={landmark_s} = {v_hit:.2f} m/s (<= {target_speed}+{tol}?)"
        return res("pass" if ok else "fail", detail, None if ok else hit)

    if kind == "steer_not_saturated":
        thr = float(must.get("threshold", 0.98))
        gated = [i for i in range(len(frames))
                 if _time_window_ok(frames[i]["sim_time"], must)
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

        # stopped_at_signal: optionally confirm the signal was red at stop onset
        # using the captured OSI scene. OSI traffic-light id<->signal id mapping
        # can vary, so this is best-effort: fail only when reds exist but none
        # match; skip the sub-check entirely if no scene was captured.
        if ok and kind == "stopped_at_signal" and must.get("require_red", True):
            scene = frames[start_i].get("scene")
            if scene is not None:
                tls = scene.get("traffic_lights", [])
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
        thws = []
        for i, fr in enumerate(frames):
            if not _time_window_ok(fr["sim_time"], must):
                continue
            scene = fr.get("scene")
            if not scene:
                continue
            ego = fr["ego"]
            v = ego["speed"]
            if v < stop_speed:
                continue  # standstill -> THW undefined
            ch, sh = math.cos(ego["h"]), math.sin(ego["h"])
            ego_len = next((o["length"] for o in scene["objects"] if o.get("is_host")), 5.0)
            best = None  # (forward, lead_len)
            for o in scene["objects"]:
                if o.get("is_host"):
                    continue
                if target_id is not None and o["id"] != target_id:
                    continue
                dx, dy = o["x"] - ego["x"], o["y"] - ego["y"]
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
        return res("pass" if (lo_ok and hi_ok) else "fail", detail)

    if kind == "min_separation_above":
        # Anti-collision gate: over the window, the minimum center-to-center
        # distance between the ego (is_host) and EVERY other scene object must stay
        # >= threshold. Requires capture_osi (telemetry.scene). Unlike a speed proxy
        # this cannot false-positive: it measures the actual closing distance, so it
        # catches a collision regardless of how the ego moves.
        thr = float(must["threshold"])
        worst_sep = None  # (sep, frame_idx)
        for i, fr in enumerate(frames):
            if not _time_window_ok(fr["sim_time"], must):
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
            if not _time_window_ok(fr["sim_time"], must):
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
                sep = _obb_separation(ego, o)
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

    return {"event": kind, "status": "skip", "detail": "unknown event type", "reason": reason}


def assert_expectations(run_dir: Path, expectations: Path) -> dict:
    import yaml
    frames = _load_telemetry(run_dir)
    spec = yaml.safe_load(expectations.read_text(encoding="utf-8"))
    musts = spec.get("must", []) if isinstance(spec, dict) else []

    results = [_eval_must(m, frames) for m in musts]
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
    print(f"[assert] overall={overall}  pass={n_pass} fail={n_fail} skip={n_skip} "
          f"-> {run_dir / 'verdict.json'}")
    for r in results:
        print(f"   [{r['status']:4}] {r['event']}: {r['detail']}")
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
    print(f"[report] -> {out_path}")


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
          f"-> {out_root / 'batch_verdict.json'} (+batch_summary.md)")
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
        compare(args.run_dir.resolve(), args.baseline.resolve())
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
