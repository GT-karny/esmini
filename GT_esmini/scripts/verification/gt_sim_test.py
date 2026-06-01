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
import struct
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "scripts"))  # osi3 bindings

from gt_lib import GtLib  # noqa: E402  (local module, same dir)


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


def run(scenario: Path, out_dir: Path, dt: float, max_time: float,
        snapshots: int, dll: Path | None) -> dict:
    out_dir.mkdir(parents=True, exist_ok=True)
    jsonl_path = out_dir / "telemetry.jsonl"

    args = ["--osc", str(scenario), "--headless", "--fixed_timestep", str(dt)]
    lib = GtLib(dll) if dll else GtLib()

    frames: list[dict] = []
    grace = 0
    GRACE_MAX = int(round(1.0 / dt))  # ~1s of consecutive "no telemetry" = ended
    seen_valid = False

    with lib, open(jsonl_path, "w", encoding="utf-8") as f:
        rc = lib.init_with_args(args)
        if rc != 0:
            raise RuntimeError(f"GT_InitWithArgs failed (rc={rc}) for {scenario}")

        n_steps = int(round(max_time / dt))
        for _ in range(n_steps):
            lib.step(dt)
            tel = lib.get_vd_telemetry(-1)
            if tel is None:
                if seen_valid:
                    grace += 1
                    if grace >= GRACE_MAX:
                        break  # controller deactivated -> scenario ended
                continue
            seen_valid = True
            grace = 0
            f.write(json.dumps(tel, separators=(",", ":")) + "\n")
            frames.append(tel)

    duration = frames[-1]["sim_time"] if frames else 0.0
    meta = {
        "scenario": str(scenario.relative_to(REPO_ROOT)) if scenario.is_absolute()
        and str(scenario).startswith(str(REPO_ROOT)) else str(scenario),
        "controller": "VirtualDriver",
        "dt": dt,
        "frames": len(frames),
        "sim_duration_s": round(duration, 3),
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
    print(f"[compare] xy_rmse={result['xy_rmse_m']}m  speed_rmse={result['speed_rmse_mps']}m/s  "
          f"max_dev={result['xy_max_dev_m']}m -> {run_dir / 'compare.json'}")
    return result


# ---------------------------------------------------------------------------
# assert (expectations.yaml)
# ---------------------------------------------------------------------------

def _speed_series(frames: list[dict]) -> list[tuple[float, float]]:
    return [(fr["sim_time"], fr["ego"]["speed"]) for fr in frames]


def _time_window_ok(t: float, spec: dict) -> bool:
    """Honour optional after/before sim_time gates on a must entry."""
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
    speeds = _speed_series(frames)

    if kind in ("speed_above", "speed_below"):
        thr = float(must["threshold"])
        gated = [(t, s) for (t, s) in speeds if _time_window_ok(t, must)]
        if not gated:
            return {"event": kind, "status": "skip", "detail": "no frames in time window", "reason": reason}
        if kind == "speed_above":
            ok = any(s >= thr for _, s in gated)
            detail = f"max speed in window = {max(s for _, s in gated):.2f} (>= {thr}?)"
        else:
            ok = all(s <= thr for _, s in gated)
            worst = max(s for _, s in gated)
            detail = f"max speed in window = {worst:.2f} (<= {thr}?)"
        return {"event": kind, "status": "pass" if ok else "fail", "detail": detail, "reason": reason}

    # Lane events use the road-coordinate fields the telemetry exposes
    # (ego.lane / ego.track). These were added by the controller's telemetry
    # serializer; if a frame lacks them (older DLL) the event degrades to skip.
    def lane_series() -> list[tuple[float, int, int]]:
        out = []
        for fr in frames:
            ego = fr["ego"]
            if "lane" in ego and "track" in ego:
                out.append((fr["sim_time"], int(ego["track"]), int(ego["lane"])))
        return out

    if kind == "lane_keep":
        road_id = must.get("road_id")
        lane_id = must.get("lane_id")
        ls = [(t, trk, ln) for (t, trk, ln) in lane_series() if _time_window_ok(t, must)]
        if not ls:
            return {"event": kind, "status": "skip",
                    "detail": "no lane data in window (lane/track absent or empty window)", "reason": reason}
        bad = [(t, trk, ln) for (t, trk, ln) in ls
               if (road_id is not None and trk != road_id) or (lane_id is not None and ln != lane_id)]
        ok = not bad
        detail = (f"{len(ls)} frames in window on lane(s) "
                  f"{sorted(set(ln for _, _, ln in ls))} road(s) {sorted(set(trk for _, trk, _ in ls))}; "
                  f"expected road={road_id} lane={lane_id}")
        return {"event": kind, "status": "pass" if ok else "fail", "detail": detail, "reason": reason}

    if kind == "lane_change_count":
        expected = must.get("count")
        ls = lane_series()
        if not ls:
            return {"event": kind, "status": "skip", "detail": "no lane data", "reason": reason}
        changes = sum(1 for i in range(1, len(ls)) if ls[i][2] != ls[i - 1][2])
        ok = (expected is None) or (changes == expected)
        return {"event": kind, "status": "pass" if ok else "fail",
                "detail": f"observed {changes} lane change(s); expected {expected}", "reason": reason}

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

    pc = sub.add_parser("compare", help="compare run vs Default baseline (ego RMSE)")
    pc.add_argument("run_dir", type=Path)
    pc.add_argument("baseline", type=Path, help=".osi file or baselines/<name>/ dir")

    pa = sub.add_parser("assert", help="match run telemetry against expectations.yaml")
    pa.add_argument("run_dir", type=Path)
    pa.add_argument("--expectations", type=Path, required=True)

    args = p.parse_args(argv)

    if args.cmd == "run":
        if not args.scenario.is_file():
            print(f"ERROR: scenario not found: {args.scenario}", file=sys.stderr)
            return 2
        meta = run(args.scenario.resolve(), args.out.resolve(), args.dt,
                   args.max_time, args.snapshots, args.dll)
        return 0 if meta["frames"] > 0 else 1

    if args.cmd == "compare":
        compare(args.run_dir.resolve(), args.baseline.resolve())
        return 0

    if args.cmd == "assert":
        v = assert_expectations(args.run_dir.resolve(), args.expectations.resolve())
        return 0 if v["overall"] in ("pass", "needs-review") else 1

    return 2


if __name__ == "__main__":
    raise SystemExit(main())
