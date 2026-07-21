"""Simulation execution API endpoints."""

from __future__ import annotations

from fastapi import APIRouter, HTTPException

from GT_esmini.web.backend.models.simulation import (
    SimulationListResponse,
    SimulationRequest,
    SimulationStatus,
    SpeedRequest,
    DriveModeRequest,
)
from GT_esmini.web.backend.services import (
    project_service,
    scenario_service,
    simulation_runner,
)
from GT_esmini.web.backend.services.simulation_runner import SimulationConflictError

router = APIRouter(prefix="/api/simulations", tags=["simulations"])


@router.post("", response_model=SimulationStatus)
async def create_simulation(req: SimulationRequest):
    """Submit a new simulation job."""
    scenario_path = None

    # Resolve scenario path: project-based or legacy
    if req.project_id:
        proj = await project_service.get_project(req.project_id)
        if proj is None:
            raise HTTPException(
                status_code=404, detail=f"Project '{req.project_id}' not found"
            )
        from pathlib import Path

        candidate = Path(proj.root_path) / req.scenario_id
        if candidate.is_file():
            scenario_path = candidate

    if scenario_path is None:
        scenario_path = scenario_service.get_scenario_path(req.scenario_id)

    if scenario_path is None:
        raise HTTPException(
            status_code=404, detail=f"Scenario '{req.scenario_id}' not found"
        )

    try:
        job_id = await simulation_runner.submit_simulation(req, scenario_path)
    except SimulationConflictError as e:
        raise HTTPException(
            status_code=409,
            detail=f"A simulation is already running: {e.running_job_id}",
        )
    status = await simulation_runner.get_simulation_status(job_id)
    if status is None:
        raise HTTPException(status_code=500, detail="Failed to create simulation job")
    return status


@router.get("", response_model=SimulationListResponse)
async def list_simulations(
    status: str | None = None,
    project_id: str | None = None,
    scenario_id: str | None = None,
    limit: int = 20,
    offset: int = 0,
):
    """List simulation jobs."""
    jobs, total = await simulation_runner.list_simulations(
        status,
        project_id,
        limit,
        offset,
        scenario_id=scenario_id,
    )
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


@router.put("/{job_id}/speed")
async def set_simulation_speed(job_id: str, body: SpeedRequest):
    """Change the simulation speed of a running job."""
    sim = await simulation_runner.get_simulation_status(job_id)
    if sim is None:
        raise HTTPException(status_code=404, detail=f"Job '{job_id}' not found")
    if sim.status != "running":
        raise HTTPException(
            status_code=409,
            detail=f"Job is not running (status: {sim.status})",
        )

    success = await simulation_runner.set_speed_factor(job_id, body.speed_factor)
    if not success:
        raise HTTPException(
            status_code=500,
            detail="Failed to communicate with simulation process",
        )
    return {"job_id": job_id, "speed_factor": body.speed_factor}


@router.put("/{job_id}/drive_mode")
async def set_simulation_drive_mode(job_id: str, body: DriveModeRequest):
    """Change the HVDEstimator drive mode of a running job."""
    sim = await simulation_runner.get_simulation_status(job_id)
    if sim is None:
        raise HTTPException(status_code=404, detail=f"Job '{job_id}' not found")
    if sim.status != "running":
        raise HTTPException(
            status_code=409,
            detail=f"Job is not running (status: {sim.status})",
        )

    success = await simulation_runner.set_drive_mode(job_id, body.mode)
    if not success:
        raise HTTPException(
            status_code=500,
            detail="Failed to communicate with simulation process",
        )
    return {"job_id": job_id, "mode": body.mode}
