"""feature:F7 acceptance check — do the proposed envelope limits ever clip a frame
across the FULL evaluated regression set (Phase 1: lc/curve/right_turn CSVs +
Phase 2: 12-scenario car_following_traffic_control_batch telemetry)?

Candidate limits under test (team-lead's x1.3 proposal):
    a_lat_max_steer = 4.3   m/s^2
    yaw_rate_max    = 1.0   rad/s
    steer_rate_max  = 1.0   rad/s

Per-scenario: max value per metric, % of limit used (headroom), and clip
count/pct at the candidate limits. No simulation run here -- reads the same
Phase-1 CSVs (scripts/ffb_spike/profiles/*.csv) and Phase-2 telemetry.jsonl
already produced.

Usage:
    DriverScript/.venv/Scripts/python.exe scripts/ffb_spike/f7_envelope_acceptance.py <phase2_out_root>
"""
from __future__ import annotations

import csv
import json
import math
import sys
from pathlib import Path

HERE = Path(__file__).parent
MAX_STEER_ANGLE = 0.61

LIMITS = {"a_lat": 4.3, "yaw": 1.0, "steer_rate": 1.0}

PHASE1_WB = {"lc": 5.04 * 0.6, "curve": 5.0 * 0.6, "right_turn": 5.0 * 0.6}
PHASE2_WB = 3.0


def series_from_phase1(name: str) -> dict:
    rows = []
    with (HERE / "profiles" / f"{name}.csv").open(encoding="utf-8") as fh:
        for r in csv.DictReader(fh):
            rows.append({k: float(v) for k, v in r.items()})
    wb = PHASE1_WB[name]
    deltas = [r["steer"] * MAX_STEER_ANGLE for r in rows]
    speeds = [r["speed"] for r in rows]
    kappas = [math.tan(d) / wb for d in deltas]
    a_lat = [abs(v * v * k) for v, k in zip(speeds, kappas)]
    yaw = [abs(v * k) for v, k in zip(speeds, kappas)]
    steer_rate = []
    for i in range(1, len(rows)):
        dt = rows[i]["t_s"] - rows[i - 1]["t_s"]
        if dt > 0:
            steer_rate.append(abs(deltas[i] - deltas[i - 1]) / dt)
    return {"a_lat": a_lat, "yaw": yaw, "steer_rate": steer_rate, "frames_meta": []}


def series_from_phase2(name: str, path: Path) -> dict:
    rows = [json.loads(l) for l in path.read_text(encoding="utf-8").splitlines() if l.strip()]
    active, prev_t = [], None
    for r in rows:
        t = r["sim_time"]
        if prev_t is not None and t <= prev_t:
            break
        active.append(r)
        prev_t = t
    deltas = [r["driver"]["steer"] * MAX_STEER_ANGLE for r in active]
    speeds = [r["ego"]["speed"] for r in active]
    kappas = [math.tan(d) / PHASE2_WB for d in deltas]
    a_lat = [abs(v * v * k) for v, k in zip(speeds, kappas)]
    yaw = [abs(v * k) for v, k in zip(speeds, kappas)]
    meta = {"a_lat": list(zip(a_lat, active)), "yaw": list(zip(yaw, active))}
    steer_rate, sr_meta = [], []
    for i in range(1, len(active)):
        dt = active[i]["sim_time"] - active[i - 1]["sim_time"]
        if dt > 0:
            steer_rate.append(abs(deltas[i] - deltas[i - 1]) / dt)
            sr_meta.append(active[i])
    return {"a_lat": a_lat, "yaw": yaw, "steer_rate": steer_rate,
            "active": active, "sr_meta": sr_meta}


def main() -> int:
    """Returns a PROCESS EXIT CODE, and that is the point.

    This file is named "acceptance" and its whole job is to answer "do the
    candidate envelope limits ever clip a frame across the evaluated set". It
    used to be declared `-> None` and called bare from __main__, so it could
    not report a verdict to anything: it printed a clip count and exited 0
    whether that count was 0 or 4000. Every commit that cited it as evidence
    was citing a program that is structurally incapable of failing.

    Exit codes:
      0 - no frame clipped any candidate limit
      1 - at least one frame clipped (the answer this check exists to give)
      2 - nothing was evaluated (no series found), which is not a pass either
    """
    # Accepts one or more roots; each is searched recursively (rglob) for
    # telemetry.jsonl so both flat (phase2: root/<scenario>/telemetry.jsonl)
    # and nested (junction batches: root/<manifest>/<scenario>/telemetry.jsonl)
    # layouts work without a flag. Optional --steer-rate-max=X overrides the
    # steer_rate candidate limit (for comparing 1.0 vs 1.5 without editing code).
    args = sys.argv[1:]
    for a in list(args):
        if a.startswith("--steer-rate-max="):
            LIMITS["steer_rate"] = float(a.split("=", 1)[1])
            args.remove(a)
    extra_roots = [Path(a) for a in args]
    all_series: dict[str, dict] = {}
    for name in PHASE1_WB:
        all_series[f"phase1/{name}"] = series_from_phase1(name)
    for root in extra_roots:
        label = root.name
        for p in sorted(root.rglob("telemetry.jsonl")):
            scen_name = p.parent.name
            all_series[f"{label}/{scen_name}"] = series_from_phase2(scen_name, p)

    print(f"{'scenario':28s} {'a_lat max(%lim)':>18s} {'yaw max(%lim)':>16s} "
          f"{'steer_rate max(%lim)':>22s} {'clip(a_lat/yaw/sr)':>20s}")
    total_clips = {"a_lat": 0, "yaw": 0, "steer_rate": 0}
    total_n = {"a_lat": 0, "yaw": 0, "steer_rate": 0}
    clip_detail = []
    for scen, s in all_series.items():
        cells = []
        clipcounts = []
        for m in ("a_lat", "yaw", "steer_rate"):
            vals = s[m]
            mx = max(vals) if vals else 0.0
            lim = LIMITS[m]
            pct = 100.0 * mx / lim
            cells.append(f"{mx:7.3f} ({pct:5.1f}%)")
            clipped = [v for v in vals if v > lim]
            clipcounts.append(len(clipped))
            total_clips[m] += len(clipped)
            total_n[m] += len(vals)
            if clipped and "phase2" in scen:
                # find offending frame(s) for reporting
                src = s.get("active" if m != "steer_rate" else "sr_meta", [])
                pairs = zip(s[m], src) if m != "steer_rate" else zip(s[m], s["sr_meta"])
                for v, fr in pairs:
                    if v > lim:
                        clip_detail.append((scen, m, v, fr["sim_time"], fr["ego"]["speed"], fr["driver"]["steer"]))
        print(f"{scen:28s} {cells[0]:>18s} {cells[1]:>16s} {cells[2]:>22s} "
              f"{str(clipcounts):>20s}")

    print(f"\n=== TOTAL across {len(all_series)} scenarios ===")
    for m in ("a_lat", "yaw", "steer_rate"):
        print(f"  {m}: clipped={total_clips[m]}/{total_n[m]} "
              f"({100*total_clips[m]/max(1,total_n[m]):.3f}%)")

    if clip_detail:
        print("\n=== clip detail (phase2) ===")
        seen = set()
        for scen, m, v, t, spd, steer in clip_detail:
            key = (scen, round(t, 2))
            if key in seen:
                continue
            seen.add(key)
            print(f"  {scen} t={t:.2f}s metric={m} value={v:.3f} v={spd:.2f}m/s steer_norm={steer:.4f}")

    # --- verdict ---------------------------------------------------------
    evaluated = sum(total_n.values())
    if evaluated == 0:
        print("\nRESULT: NOT EVALUATED — no series were found. "
              "Zero frames checked is not an acceptance.")
        return 2
    clipped_total = sum(total_clips.values())
    if clipped_total:
        print(f"\nRESULT: FAIL — {clipped_total} frame-metric(s) exceeded a candidate "
              f"limit across {evaluated} evaluated frame-metrics.")
        return 1
    print(f"\nRESULT: PASS — no frame exceeded any candidate limit "
          f"({evaluated} frame-metrics evaluated).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
