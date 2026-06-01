"""VirtualDriver verification replay API.

Serves recorded gt_sim_test runs (results/<run_id>/) so the web frontend can
replay the VirtualDriver telemetry overlay (short-horizon trajectory, driver
errors) without a live socket transport from GT_Sim. Live streaming is added
later once the C++ telemetry emit is wired; the replay shape is identical, so
the same overlay component will drive both.
"""
from __future__ import annotations

import json
from pathlib import Path

from fastapi import APIRouter, HTTPException

from GT_esmini.web.backend.config import REPO_ROOT

router = APIRouter(prefix="/api/verification", tags=["verification"])

RESULTS_ROOT = (REPO_ROOT / "results").resolve()


def _safe_run_dir(run_id: str) -> Path:
    d = (RESULTS_ROOT / run_id).resolve()
    if d != RESULTS_ROOT and RESULTS_ROOT not in d.parents:
        raise HTTPException(status_code=400, detail="invalid run id")
    return d


def _read_json(path: Path):
    if path.is_file():
        try:
            return json.loads(path.read_text(encoding="utf-8"))
        except Exception:
            return None
    return None


@router.get("/runs")
async def list_runs():
    """List recorded runs (dirs under results/ that contain telemetry.jsonl)."""
    runs = []
    if RESULTS_ROOT.is_dir():
        for d in sorted(RESULTS_ROOT.iterdir()):
            if d.is_dir() and (d / "telemetry.jsonl").is_file():
                runs.append({
                    "id": d.name,
                    "meta": _read_json(d / "meta.json") or {},
                    "has_compare": (d / "compare.json").is_file(),
                    "has_verdict": (d / "verdict.json").is_file(),
                })
    return {"runs": runs}


@router.get("/runs/{run_id}/telemetry")
async def get_telemetry(run_id: str):
    """Return all recorded telemetry frames for a run, plus meta/compare/verdict."""
    d = _safe_run_dir(run_id)
    tj = d / "telemetry.jsonl"
    if not tj.is_file():
        raise HTTPException(status_code=404, detail=f"no telemetry for run '{run_id}'")

    frames = []
    for line in tj.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line:
            try:
                frames.append(json.loads(line))
            except json.JSONDecodeError:
                continue

    return {
        "id": run_id,
        "meta": _read_json(d / "meta.json") or {},
        "frames": frames,
        "compare": _read_json(d / "compare.json"),
        "verdict": _read_json(d / "verdict.json"),
    }
