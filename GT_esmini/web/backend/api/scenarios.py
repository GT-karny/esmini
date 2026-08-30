"""Scenario management API endpoints."""

from __future__ import annotations

import xml.etree.ElementTree as ET
from pathlib import Path

from fastapi import APIRouter, HTTPException, Request
from pydantic import BaseModel, Field

from GT_esmini.web.backend.models.scenario import (
    ScenarioDetail,
    ScenarioListItem,
    ScenarioUploadResponse,
    VariantRequest,
    VariantResponse,
)
from GT_esmini.web.backend.config import SCENARIOS_DIR
from GT_esmini.web.backend.services import scenario_service
from GT_esmini.web.backend.services import road_geometry_service
from GT_esmini.web.backend.services import road_service
from GT_esmini.web.backend.services.route_planner_service import (
    RoutePlanError,
    plan_route,
)
from GT_esmini.web.backend.services.scenario_builder_service import (
    ScenarioBuildError,
    build_route_scenario,
)

router = APIRouter(prefix="/api/scenarios", tags=["scenarios"])


class BuildFromRoutePoint(BaseModel):
    x: float
    y: float


class BuildFromRouteRequest(BaseModel):
    road_id: str
    points: list[BuildFromRoutePoint] = Field(min_length=2)
    ego_speed: float = 13.889
    strategy: str = "shortest"
    # VirtualDriver opt-ins. "lane_change_initiation" is what lets the vehicle
    # actually move into the lane the route requires -- without it the run still
    # works, it just records the deviation instead of correcting it.
    policies: list[str] = Field(default_factory=lambda: ["lane_change_initiation"])
    description: str = "GT_Sim route-plan scenario"


@router.post("/build-from-route", status_code=201)
async def build_from_route(req: BuildFromRouteRequest):
    """Plan a route through clicked points and save it as a temporary scenario.

    The result is an ordinary temp scenario id, so everything downstream (the
    VirtualDriver variant path, the run launcher, the viewer) treats it exactly
    like an uploaded file -- no second execution path.
    """
    xodr_path = road_service.resolve_road_path(req.road_id)
    if xodr_path is None:
        raise HTTPException(status_code=404, detail=f"Road '{req.road_id}' not found")

    try:
        plan = plan_route(
            xodr_path,
            [{"x": p.x, "y": p.y} for p in req.points],
            strategy=req.strategy,
        )
    except RoutePlanError as exc:
        unavailable = {
            "library_unavailable",
            "route_api_missing",
            "route_direction_api_missing",
        }
        raise HTTPException(
            status_code=503 if exc.code in unavailable else 422,
            detail={"code": exc.code, "message": str(exc), **exc.detail},
        ) from exc

    try:
        xml_str = build_route_scenario(
            xodr_path,
            plan["waypoints"],
            start=plan["snapped"][0],
            ego_speed=req.ego_speed,
            policies=req.policies,
            route_length=plan["length"],
            description=req.description,
        )
    except ScenarioBuildError as exc:
        raise HTTPException(
            status_code=422, detail={"code": exc.code, "message": str(exc)}
        ) from exc

    saved = scenario_service.save_temp_scenario(xml_str)
    return {**saved, "route": plan}


@router.get("", response_model=list[ScenarioListItem])
async def list_scenarios(search: str | None = None):
    """List all available XOSC scenarios."""
    return scenario_service.list_scenarios(search=search)


@router.get("/{scenario_id}", response_model=ScenarioDetail)
async def get_scenario(scenario_id: str):
    """Get scenario details with parsed XOSC info."""
    detail = scenario_service.get_scenario_detail(scenario_id)
    if detail is None:
        raise HTTPException(
            status_code=404, detail=f"Scenario '{scenario_id}' not found"
        )
    return detail


@router.get("/{scenario_id}/road-geometry")
async def get_road_geometry(scenario_id: str):
    """Extract lane boundary polylines from the scenario's OpenDRIVE file."""
    detail = scenario_service.get_scenario_detail(scenario_id)
    if detail is None:
        raise HTTPException(
            status_code=404, detail=f"Scenario '{scenario_id}' not found"
        )
    if not detail.road_file:
        raise HTTPException(status_code=404, detail="Scenario has no road file")

    # Resolve xodr path (may be relative to SCENARIOS_DIR)
    road_path = Path(detail.road_file)
    if not road_path.is_absolute():
        road_path = (SCENARIOS_DIR / detail.road_file).resolve()
    if not road_path.is_file():
        raise HTTPException(
            status_code=404, detail=f"Road file not found: {detail.road_file}"
        )

    import asyncio

    geometry = await asyncio.to_thread(
        road_geometry_service.extract_road_geometry, road_path
    )
    return geometry


@router.post("/upload", response_model=ScenarioUploadResponse, status_code=201)
async def upload_scenario(request: Request):
    """Upload XOSC XML to create a temporary scenario."""
    content_type = request.headers.get("content-type", "")
    if not any(t in content_type for t in ("xml", "text/plain", "octet-stream")):
        raise HTTPException(
            status_code=415,
            detail="Expected XML content (text/xml, application/xml, or text/plain)",
        )

    body = await request.body()
    try:
        xml_str = body.decode("utf-8")
    except UnicodeDecodeError:
        raise HTTPException(status_code=400, detail="Failed to decode body as UTF-8")

    try:
        result = scenario_service.save_temp_scenario(xml_str)
    except ET.ParseError as e:
        raise HTTPException(status_code=400, detail=f"Invalid XML: {e}")

    return result


@router.delete("/upload/{scenario_id}")
async def delete_uploaded_scenario(scenario_id: str):
    """Delete a previously uploaded temporary scenario."""
    if not scenario_id.startswith("tmp_"):
        raise HTTPException(
            status_code=400, detail="Can only delete temporary scenarios"
        )
    success = scenario_service.delete_temp_scenario(scenario_id)
    if not success:
        raise HTTPException(
            status_code=404, detail=f"Temp scenario '{scenario_id}' not found"
        )
    return {"deleted": scenario_id}
