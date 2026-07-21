"""Rule-based similarity matching for VirtualDriver verification runs.

Given a target run, rank past *labeled* runs of the same scenario by how similar
their behavior is, so the annotation UI can suggest "you previously labelled a run
like this as PASS". Deliberately simple (Phase 3d will improve it from real usage).

Importable by the web backend (services/annotation_store.match_run) AND runnable
standalone:

    py annotation_match.py <run_dir> --against <dir-of-runs> [-k 5]

No import-time side effects (no DLL load) — telemetry helpers are inlined rather
than imported from gt_sim_test, which loads GT_esminiLib at import.
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
from typing import Any

# ---------------------------------------------------------------------------
# Telemetry helpers (inlined minimal copies of gt_sim_test internals)
# ---------------------------------------------------------------------------


def _load_telemetry(run_dir: Path) -> list[dict]:
    jsonl = run_dir / "telemetry.jsonl"
    if not jsonl.is_file():
        return []
    out: list[dict] = []
    for line in jsonl.read_text(encoding="utf-8").splitlines():
        if line.strip():
            try:
                out.append(json.loads(line))
            except json.JSONDecodeError:
                continue
    return out


def _read_json(path: Path) -> Any:
    if path.is_file():
        try:
            return json.loads(path.read_text(encoding="utf-8"))
        except Exception:
            return None
    return None


def _accel_jerk(frames: list[dict], smooth_window: int = 5) -> tuple[float, float]:
    """(max |accel|, max |jerk|) derived from smoothed speed via central difference."""
    n = len(frames)
    if n < 3:
        return 0.0, 0.0
    t = [fr["sim_time"] for fr in frames]
    v_raw = [fr["ego"]["speed"] for fr in frames]
    w = max(1, int(smooth_window))
    if w % 2 == 0:
        w += 1
    half = w // 2
    v = [
        sum(v_raw[max(0, i - half) : min(n, i + half + 1)])
        / len(v_raw[max(0, i - half) : min(n, i + half + 1)])
        for i in range(n)
    ]

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
    return max((abs(x) for x in a), default=0.0), max((abs(x) for x in j), default=0.0)


# ---------------------------------------------------------------------------
# Feature extraction
# ---------------------------------------------------------------------------


def extract_features(
    run_dir: Path | None = None,
    frames: list[dict] | None = None,
    verdict: dict | None = None,
    meta: dict | None = None,
) -> dict:
    """Scalar behavior fingerprint from a run's telemetry + verdict + meta."""
    if frames is None:
        frames = _load_telemetry(run_dir) if run_dir is not None else []
    meta = meta or {}
    verdict = verdict or {}

    scenario = meta.get("scenario") or meta.get("scenario_path")
    scenario_stem = Path(str(scenario)).stem if scenario else None

    speeds = [fr["ego"]["speed"] for fr in frames] if frames else []
    max_decel, max_jerk = _accel_jerk(frames)

    lane_changes = 0
    prev_lane = None
    has_lead = False
    for fr in frames:
        lane = fr.get("ego", {}).get("lane")
        if prev_lane is not None and lane is not None and lane != prev_lane:
            lane_changes += 1
        if lane is not None:
            prev_lane = lane
        scene = fr.get("scene")
        if scene and any(not o.get("is_host", False) for o in scene.get("objects", [])):
            has_lead = True

    failing = {
        r.get("event")
        for r in verdict.get("results", [])
        if r.get("status") == "fail" and r.get("event")
    }
    policy_sources: set[str] = set()
    for fr in frames:
        pol = fr.get("policy") or {}
        for c in pol.get("constraints", []):
            if c.get("source"):
                policy_sources.add(c["source"])

    return {
        "scenario_stem": scenario_stem,
        "frames": len(frames),
        "sim_duration": frames[-1]["sim_time"] if frames else 0.0,
        "min_speed": min(speeds) if speeds else 0.0,
        "mean_speed": (sum(speeds) / len(speeds)) if speeds else 0.0,
        "max_speed": max(speeds) if speeds else 0.0,
        "max_decel": max_decel,
        "max_jerk": max_jerk,
        "lane_change_count": lane_changes,
        "did_full_stop": bool(speeds) and min(speeds) < 0.3,
        "has_lead": has_lead,
        "verdict_overall": verdict.get("overall"),
        "failing_event_kinds": failing,
        "policy_sources": policy_sources,
    }


# ---------------------------------------------------------------------------
# Similarity
# ---------------------------------------------------------------------------


def _gaussian_close(a: float, b: float, scale: float) -> float:
    """1.0 when equal, decaying with |a-b| over `scale`."""
    d = abs((a or 0.0) - (b or 0.0))
    return math.exp(-(d * d) / (2.0 * scale * scale)) if scale > 0 else 1.0


def _jaccard(a: set, b: set) -> float:
    if not a and not b:
        return 1.0
    u = a | b
    return len(a & b) / len(u) if u else 1.0


def similarity(a: dict, b: dict) -> tuple[float, list[str]]:
    """Weighted rule-based similarity in [0,1] with human-readable reasons.

    Same scenario_stem is a hard prefilter (returns 0 if they differ)."""
    reasons: list[str] = []
    if a.get("scenario_stem") != b.get("scenario_stem"):
        return 0.0, ["different scenario"]
    reasons.append(f"same scenario ({a.get('scenario_stem')})")

    score = 0.0
    # Same auto verdict (0.3)
    if a.get("verdict_overall") and a.get("verdict_overall") == b.get(
        "verdict_overall"
    ):
        score += 0.3
        reasons.append(f"both verdict={a['verdict_overall']}")
    # Overlap of failing event kinds (0.3)
    jac = _jaccard(
        a.get("failing_event_kinds", set()), b.get("failing_event_kinds", set())
    )
    score += 0.3 * jac
    shared = a.get("failing_event_kinds", set()) & b.get("failing_event_kinds", set())
    if shared:
        reasons.append("shared failing events: " + ", ".join(sorted(shared)))
    # Numeric profile closeness (0.4 total)
    c_decel = _gaussian_close(a.get("max_decel"), b.get("max_decel"), 1.5)
    c_jerk = _gaussian_close(a.get("max_jerk"), b.get("max_jerk"), 4.0)
    c_dur = _gaussian_close(a.get("sim_duration"), b.get("sim_duration"), 5.0)
    score += 0.4 * (0.4 * c_decel + 0.3 * c_jerk + 0.3 * c_dur)
    if c_decel > 0.8:
        reasons.append(f"similar peak decel (~{b.get('max_decel', 0):.1f} m/s^2)")
    if c_dur > 0.8:
        reasons.append(f"similar duration (~{b.get('sim_duration', 0):.1f} s)")

    return round(min(1.0, score), 4), reasons


def rank(
    target: dict, candidates: list[tuple[str, str, str, dict]], k: int = 5
) -> list[dict]:
    """candidates: [(run_id, label, comment, features)] -> top-k MatchOut dicts."""
    scored = []
    for run_id, label, comment, feat in candidates:
        score, reasons = similarity(target, feat)
        if score <= 0.0:
            continue
        scored.append(
            {
                "run_id": run_id,
                "label": label,
                "comment": comment,
                "score": score,
                "reasons": reasons,
            }
        )
    scored.sort(key=lambda m: m["score"], reverse=True)
    return scored[:k]


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def _features_for(run_dir: Path) -> dict:
    return extract_features(
        run_dir=run_dir,
        verdict=_read_json(run_dir / "verdict.json"),
        meta=_read_json(run_dir / "meta.json") or {},
    )


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    p.add_argument("run_dir", type=Path, help="the target run dir")
    p.add_argument(
        "--against",
        type=Path,
        required=True,
        help="dir whose immediate subdirs are candidate runs",
    )
    p.add_argument("-k", type=int, default=5)
    args = p.parse_args(argv)

    target = _features_for(args.run_dir)
    candidates: list[tuple[str, str, str, dict]] = []
    for d in sorted(args.against.iterdir()):
        if not d.is_dir() or d.resolve() == args.run_dir.resolve():
            continue
        if not (d / "telemetry.jsonl").is_file():
            continue
        # Standalone mode has no DB labels; tag with the auto verdict as a stand-in.
        verdict = _read_json(d / "verdict.json") or {}
        candidates.append((d.name, verdict.get("overall", "?"), "", _features_for(d)))

    matches = rank(target, candidates, args.k)
    print(
        json.dumps(
            {
                "target_dir": str(args.run_dir),
                "scenario_stem": target.get("scenario_stem"),
                "matches": matches,
            },
            indent=2,
            ensure_ascii=False,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
