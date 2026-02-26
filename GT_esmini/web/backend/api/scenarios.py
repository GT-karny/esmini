"""Scenario management API endpoints."""

from __future__ import annotations

from fastapi import APIRouter, HTTPException

from GT_esmini.web.backend.models.scenario import (
    ScenarioDetail,
    ScenarioListItem,
    VariantRequest,
    VariantResponse,
)
from GT_esmini.web.backend.services import scenario_service

router = APIRouter(prefix="/api/scenarios", tags=["scenarios"])


@router.get("", response_model=list[ScenarioListItem])
async def list_scenarios(search: str | None = None):
    """List all available XOSC scenarios."""
    return scenario_service.list_scenarios(search=search)


@router.get("/{scenario_id}", response_model=ScenarioDetail)
async def get_scenario(scenario_id: str):
    """Get scenario details with parsed XOSC info."""
    detail = scenario_service.get_scenario_detail(scenario_id)
    if detail is None:
        raise HTTPException(status_code=404, detail=f"Scenario '{scenario_id}' not found")
    return detail
