"""Self-contained VirtualDriver verification helpers (compare / baseline / assert).

Ported from the offline CLI (GT_esmini/scripts/verification/gt_sim_test.py +
generate_baseline.py) into the backend so the VERIFY panel works in BOTH the dev
tree and the packaged distribution — the packaged build does not ship the
verification scripts, and the previous import-from-scripts approach 500'd there.

Dependencies are all bundled in the backend already: osi3 (gRPC/OSI bridge),
PyYAML (config.py), and bin/GT_Sim.exe (config.GT_SIM_EXE). The Default baseline
is produced by launching GT_Sim on the original (controller-less) scenario the
same way simulation_runner does — no embedded-python assumptions."""

from __future__ import annotations

import json
import math
import os
import socket
import struct
import subprocess
import time
from pathlib import Path

from GT_esmini.web.backend.config import GT_SIM_EXE, OSI_GT_PORT, REPO_ROOT

_OSI_BUFFER_SIZE = 8208  # max OSI UDP payload + 8-byte header (esmini contract)


# ---------------------------------------------------------------------------
# trajectory extraction
# ---------------------------------------------------------------------------

def _load_telemetry(run_dir: Path) -> list[dict]:
    jsonl = run_dir / "telemetry.jsonl"
    if not jsonl.is_file():
        raise FileNotFoundError(f"{jsonl} not found")
    out = []
    for line in jsonl.read_text(encoding="utf-8").splitlines():
        if line.strip():
            out.append(json.loads(line))
    return out


def _ego_track_from_telemetry(frames: list[dict]) -> list[tuple[float, float, float, float]]:
    return [(f["sim_time"], f["ego"]["x"], f["ego"]["y"], f["ego"]["speed"]) for f in frames]


def _ego_track_from_osi(osi_path: Path) -> list[tuple[float, float, float, float]]:
    """Extract host/ego trajectory from a length-delimited .osi trace
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


def _interp(track, t):
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


# ---------------------------------------------------------------------------
# Default baseline generation (launch GT_Sim, capture OSI GroundTruth)
# ---------------------------------------------------------------------------

def _capture_osi(out_osi: Path, proc: subprocess.Popen, port: int, idle_timeout: float) -> int:
    """Reassemble multi-packet GroundTruth frames from UDP into a length-delimited
    .osi file. Stops once GT_Sim has exited and the stream is idle."""
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
                    msg, _ = sock.recvfrom(_OSI_BUFFER_SIZE)
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


def generate_baseline(scenario_path: Path, baseline_dir: Path,
                      hz: float = 100.0, idle_timeout: float = 3.0) -> dict:
    """Run the (controller-less) scenario with the Default controller and record
    its OSI GroundTruth to baseline_dir/groundtruth.osi. Blocking — call via
    asyncio.to_thread. Launches GT_Sim like simulation_runner (cwd=REPO_ROOT)."""
    baseline_dir.mkdir(parents=True, exist_ok=True)
    out_osi = baseline_dir / "groundtruth.osi"

    cmd = [
        str(GT_SIM_EXE), "--osc", str(scenario_path),
        "--headless", "--osi", "127.0.0.1", "--hz", str(hz), "--no_realtime",
    ]
    # Inherit env; ensure the exe's own dir is on PATH so its sibling DLLs resolve.
    env = dict(os.environ)
    env["PATH"] = os.pathsep.join([str(Path(GT_SIM_EXE).parent), env.get("PATH", "")])

    with open(baseline_dir / "stdout.txt", "w") as so, open(baseline_dir / "stderr.txt", "w") as se:
        proc = subprocess.Popen(cmd, cwd=str(REPO_ROOT), env=env, stdout=so, stderr=se)
        frames = _capture_osi(out_osi, proc, OSI_GT_PORT, idle_timeout)
        proc.wait()

    return {"frames": frames, "osi_file": str(out_osi)}


# ---------------------------------------------------------------------------
# compare (run vs Default baseline)
# ---------------------------------------------------------------------------

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

    baseline_track = []
    for (t, _x, _y, _s) in vd:
        bx, by, bs = _interp(base, t)
        baseline_track.append({"t": round(t, 3), "x": round(bx, 3), "y": round(by, 3), "speed": round(bs, 3)})
    (run_dir / "baseline_track.json").write_text(
        json.dumps(baseline_track, separators=(",", ":")), encoding="utf-8")
    return result


# ---------------------------------------------------------------------------
# assert (expectations.yaml)
# ---------------------------------------------------------------------------

def _time_window_ok(t: float, spec: dict) -> bool:
    after = spec.get("after", {})
    before = spec.get("before", {})
    if "sim_time" in after and not (t >= after["sim_time"]):
        return False
    if "sim_time" in before and not (t <= before["sim_time"]):
        return False
    return True


def _eval_must(must: dict, frames: list[dict]) -> dict:
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
            worst_i = max(gated, key=lambda p: p[1])[0]
            detail = f"max speed in window = {frames[worst_i]['ego']['speed']:.2f} (>= {thr}?)"
            return res("pass" if ok else "fail", detail, None if ok else worst_i)
        offenders = [i for i, s in gated if s > thr]
        ok = not offenders
        worst = max(s for _, s in gated)
        detail = f"max speed in window = {worst:.2f} (<= {thr}?)"
        return res("pass" if ok else "fail", detail, None if ok else offenders[0])

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
            return res("skip", "no lane data in window")
        bad = [i for (i, trk, ln) in ls
               if (road_id is not None and trk != road_id) or (lane_id is not None and ln != lane_id)]
        detail = (f"{len(ls)} frames on lane(s) {sorted(set(ln for _, _, ln in ls))} "
                  f"road(s) {sorted(set(trk for _, trk, _ in ls))}; expected road={road_id} lane={lane_id}")
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

    return {"event": kind, "status": "skip", "detail": "unknown event type", "reason": reason}


def assert_run(run_dir: Path, expectations: Path) -> dict:
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
    return verdict
