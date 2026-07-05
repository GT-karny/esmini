"""Result retrieval API endpoints."""

from __future__ import annotations

from pathlib import Path
from typing import Any

from fastapi import APIRouter, HTTPException
from fastapi.responses import FileResponse

from GT_esmini.web.backend.models.result import ResultMeta, TimeseriesResponse
from GT_esmini.web.backend.services import result_service, simulation_runner

router = APIRouter(prefix="/api/results", tags=["results"])


async def _get_job_or_404(job_id: str):
    sim = await simulation_runner.get_simulation_status(job_id)
    if sim is None:
        raise HTTPException(status_code=404, detail=f"Job '{job_id}' not found")
    return sim


@router.get("/{job_id}", response_model=ResultMeta)
async def get_result_meta(job_id: str):
    """Get result metadata and file listing."""
    sim = await _get_job_or_404(job_id)
    if not sim.output_dir:
        raise HTTPException(status_code=404, detail="No output directory for this job")
    return result_service.get_result_meta(job_id, sim.scenario_id, sim.output_dir)


@router.get("/{job_id}/files/{filename}")
async def download_file(job_id: str, filename: str):
    """Download a result file."""
    sim = await _get_job_or_404(job_id)
    if not sim.output_dir:
        raise HTTPException(status_code=404, detail="No output directory")

    file_path = Path(sim.output_dir) / filename
    if not file_path.exists() or not file_path.is_file():
        raise HTTPException(status_code=404, detail=f"File '{filename}' not found")

    # Security: ensure file is within output_dir
    try:
        file_path.resolve().relative_to(Path(sim.output_dir).resolve())
    except ValueError:
        raise HTTPException(status_code=403, detail="Access denied")

    return FileResponse(path=str(file_path), filename=filename)


@router.get("/{job_id}/metrics")
async def get_metrics(job_id: str) -> dict[str, Any]:
    """Compute and return simulation metrics."""
    sim = await _get_job_or_404(job_id)
    if not sim.output_dir:
        raise HTTPException(status_code=404, detail="No output directory")
    if sim.status not in ("completed", "failed", "timeout"):
        return {"status": sim.status, "message": "Simulation not yet completed"}

    try:
        metrics = result_service.compute_metrics(sim.output_dir)
    except result_service.DatFormatUnsupported as exc:
        # P9b: explicit signal instead of a silent empty result (dat.py reads DAT v4
        # only; esmini >= v3.4.0 records v5 -- see the known-debt ledger).
        raise HTTPException(status_code=409, detail=str(exc))
    if metrics is None:
        raise HTTPException(status_code=404, detail="No simulation data found")
    return metrics


@router.get("/{job_id}/timeseries", response_model=TimeseriesResponse)
async def get_timeseries(
    job_id: str,
    fields: str | None = None,
    entity: str = "Ego",
):
    """Get timeseries data for charting."""
    sim = await _get_job_or_404(job_id)
    if not sim.output_dir:
        raise HTTPException(status_code=404, detail="No output directory")

    field_list = fields.split(",") if fields else None
    try:
        data = result_service.get_timeseries(sim.output_dir, field_list, entity)
    except result_service.DatFormatUnsupported as exc:
        raise HTTPException(status_code=409, detail=str(exc))
    actual_fields = list(data[0].keys()) if data else []
    return TimeseriesResponse(data=data, entity=entity, fields=actual_fields)
