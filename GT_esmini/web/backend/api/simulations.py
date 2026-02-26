"""Simulation execution API endpoints."""

from __future__ import annotations

from fastapi import APIRouter, HTTPException

from GT_esmini.web.backend.models.simulation import (
    SimulationListResponse,
    SimulationRequest,
    SimulationStatus,
)
from GT_esmini.web.backend.services import scenario_service, simulation_runner

router = APIRouter(prefix="/api/simulations", tags=["simulations"])


@router.post("", response_model=SimulationStatus)
async def create_simulation(req: SimulationRequest):
    """Submit a new simulation job."""
    scenario_path = scenario_service.get_scenario_path(req.scenario_id)
    if scenario_path is None:
        raise HTTPException(
            status_code=404, detail=f"Scenario '{req.scenario_id}' not found"
        )

    job_id = await simulation_runner.submit_simulation(req, scenario_path)
    status = await simulation_runner.get_simulation_status(job_id)
    if status is None:
        raise HTTPException(status_code=500, detail="Failed to create simulation job")
    return status


@router.get("", response_model=SimulationListResponse)
async def list_simulations(
    status: str | None = None, limit: int = 20, offset: int = 0
):
    """List simulation jobs."""
    jobs, total = await simulation_runner.list_simulations(status, limit, offset)
    return SimulationListResponse(jobs=jobs, total=total)


@router.get("/{job_id}", response_model=SimulationStatus)
async def get_simulation(job_id: str):
    """Get simulation job status."""
    sim = await simulation_runner.get_simulation_status(job_id)
    if sim is None:
        raise HTTPException(status_code=404, detail=f"Job '{job_id}' not found")
    return sim


@router.delete("/{job_id}")
async def cancel_simulation(job_id: str):
    """Cancel a running simulation."""
    success = await simulation_runner.cancel_simulation(job_id)
    if not success:
        raise HTTPException(
            status_code=400, detail="Job not found or not in running state"
        )
    return {"job_id": job_id, "status": "cancelled"}
