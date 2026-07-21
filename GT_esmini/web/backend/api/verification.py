"""VirtualDriver verification replay API.

Serves recorded gt_sim_test runs (results/<run_id>/) so the web frontend can
replay the VirtualDriver telemetry overlay (short-horizon trajectory, driver
errors) without a live socket transport from GT_Sim. Live streaming is added
later once the C++ telemetry emit is wired; the replay shape is identical, so
the same overlay component will drive both.
"""

from __future__ import annotations

import asyncio
import json
from pathlib import Path

from fastapi import APIRouter, HTTPException

from GT_esmini.web.backend.config import RESULTS_DIR
from GT_esmini.web.backend.services import vd_verify

router = APIRouter(prefix="/api/verification", tags=["verification"])

# Canonical results root = the same dir GUI simulations write to (config.RESULTS_DIR),
# so GUI VirtualDriver runs (recorded by vd_recorder) appear here.
RESULTS_ROOT = RESULTS_DIR.resolve()


def _a_sim_is_running() -> bool:
    """True if a GT_Sim subprocess is currently active (baseline generation would
    otherwise collide on the OSI UDP port)."""
    from GT_esmini.web.backend.services.simulation_runner import (
        _running_procs,
        _running_procs_lock,
    )

    with _running_procs_lock:
        return len(_running_procs) > 0


def _load_jsonl(path: Path) -> list[dict]:
    out: list[dict] = []
    if path.is_file():
        for line in path.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if line:
                try:
                    out.append(json.loads(line))
                except json.JSONDecodeError:
                    continue
    return out


def _safe_run_dir(run_id: str) -> Path:
    # Accept both top-level ids ('vd_basic') and batch-nested composite ids
    # ('batch/<batch_id>/<stem>' -> RESULTS_ROOT/<batch_id>/<stem>).
    parts = run_id.split("/")
    if ".." in parts:
        raise HTTPException(status_code=400, detail="invalid run id")
    rel = parts[1:] if parts and parts[0] == "batch" else parts
    d = RESULTS_ROOT.joinpath(*rel).resolve()
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
                runs.append(
                    {
                        "id": d.name,
                        "meta": _read_json(d / "meta.json") or {},
                        "has_compare": (d / "compare.json").is_file(),
                        "has_verdict": (d / "verdict.json").is_file(),
                    }
                )
    return {"runs": runs}


@router.get("/runs/{run_id:path}/telemetry")
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
        # Recorded scene (other traffic + signal phases) for full replay context.
        # Empty if the run had OSI streaming disabled.
        "scene": _load_jsonl(d / "scene.jsonl"),
        "compare": _read_json(d / "compare.json"),
        "verdict": _read_json(d / "verdict.json"),
        "baseline_track": _read_json(d / "baseline_track.json"),
    }


@router.post("/runs/{run_id}/baseline-compare")
async def baseline_compare(run_id: str):
    """Run the same scenario with the Default controller, then compare the recorded
    VirtualDriver run against it (ego XY / speed RMSE). Writes compare.json +
    baseline_track.json into the run dir so the replay UI can overlay the ghost."""
    run_dir = _safe_run_dir(run_id)
    if not (run_dir / "telemetry.jsonl").is_file():
        raise HTTPException(status_code=404, detail=f"no telemetry for run '{run_id}'")

    meta = _read_json(run_dir / "meta.json") or {}
    scenario_path = meta.get("scenario_path")
    if not scenario_path or not Path(scenario_path).is_file():
        raise HTTPException(
            status_code=404,
            detail="meta.json has no usable 'scenario_path'; cannot generate a Default baseline",
        )
    if _a_sim_is_running():
        raise HTTPException(
            status_code=409,
            detail="a simulation is running; baseline generation needs the OSI port — try again after it finishes",
        )

    scenario = Path(scenario_path)
    baseline_dir = (RESULTS_ROOT / "baselines" / scenario.stem).resolve()

    def _work():
        meta_b = vd_verify.generate_baseline(scenario, baseline_dir)
        if meta_b.get("frames", 0) == 0:
            raise RuntimeError(
                "baseline produced 0 OSI frames (is GT_Sim emitting OSI?)"
            )
        return vd_verify.compare(run_dir, baseline_dir)

    try:
        result = await asyncio.to_thread(_work)
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"baseline-compare failed: {e}")
    return result


@router.post("/runs/{run_id}/assert")
async def assert_run(run_id: str):
    """Evaluate the recorded run against an expectations.yaml (declarative must[]
    events). Looks for <scenario-stem>.expectations.yaml beside the scenario, then
    expectations.yaml in the run dir. Writes verdict.json."""
    run_dir = _safe_run_dir(run_id)
    if not (run_dir / "telemetry.jsonl").is_file():
        raise HTTPException(status_code=404, detail=f"no telemetry for run '{run_id}'")

    meta = _read_json(run_dir / "meta.json") or {}
    scenario_path = meta.get("scenario_path")

    exp: Path | None = None
    if scenario_path:
        sp = Path(scenario_path)
        cand = sp.parent / f"{sp.stem}.expectations.yaml"
        if cand.is_file():
            exp = cand
    if exp is None and (run_dir / "expectations.yaml").is_file():
        exp = run_dir / "expectations.yaml"
    if exp is None:
        raise HTTPException(
            status_code=404,
            detail="no expectations.yaml found (expected beside the scenario or in the run dir)",
        )

    try:
        verdict = await asyncio.to_thread(vd_verify.assert_run, run_dir, exp)
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"assert failed: {e}")
    return verdict
