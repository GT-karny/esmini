"""Road file management API endpoints."""

from __future__ import annotations

from fastapi import APIRouter, HTTPException, Request
from pydantic import BaseModel, Field

from GT_esmini.web.backend.models.scenario import RoadUploadResponse
from GT_esmini.web.backend.services import road_geometry_service, road_service
from GT_esmini.web.backend.services.route_planner_service import (
    RoutePlanError,
    plan_route,
)

router = APIRouter(prefix="/api/roads", tags=["roads"])


class RoutePoint(BaseModel):
    x: float
    y: float


class RoutePlanRequest(BaseModel):
    road_id: str
    # Travel order: start, any via points, goal. The map UI appends as the user clicks.
    points: list[RoutePoint] = Field(min_length=2)
    strategy: str = "shortest"


@router.get("")
async def list_roads():
    """List selectable OpenDRIVE files (catalog + temporary uploads)."""
    return road_service.list_roads()


@router.get("/{road_id}/geometry")
async def road_geometry(road_id: str):
    """Lane-boundary polylines for a road, for the 2D map view.

    Same payload the scenario-scoped endpoint returns, keyed by road instead --
    the route planner starts from a road, not from an existing scenario.
    """
    xodr_path = road_service.resolve_road_path(road_id)
    if xodr_path is None:
        raise HTTPException(status_code=404, detail=f"Road '{road_id}' not found")
    return road_geometry_service.extract_road_geometry(xodr_path)


@router.post("/route-plan")
async def route_plan(req: RoutePlanRequest):
    """Plan a lane-level route through clicked world points.

    Returns the Waypoint chain plus the lane changes the route requires, which is
    what scenario_builder_service turns into an AssignRouteAction.
    """
    xodr_path = road_service.resolve_road_path(req.road_id)
    if xodr_path is None:
        raise HTTPException(status_code=404, detail=f"Road '{req.road_id}' not found")

    try:
        return plan_route(
            xodr_path,
            [{"x": p.x, "y": p.y} for p in req.points],
            strategy=req.strategy,
        )
    except RoutePlanError as exc:
        # 422: the map and the libraries are fine, the requested route is not.
        # 503: the build cannot answer at all (missing/outdated DLL) -- an operator
        # problem, not something the user can fix by clicking elsewhere.
        unavailable = {
            "library_unavailable",
            "route_api_missing",
            "route_direction_api_missing",
        }
        status = 503 if exc.code in unavailable else 422
        raise HTTPException(
            status_code=status,
            detail={"code": exc.code, "message": str(exc), **exc.detail},
        ) from exc


@router.post("/upload", response_model=RoadUploadResponse, status_code=201)
async def upload_road(request: Request):
    """Upload XODR XML to create a temporary road file."""
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

    result = road_service.save_temp_road(xml_str)
    return result


@router.delete("/upload/{road_id}")
async def delete_uploaded_road(road_id: str):
    """Delete a previously uploaded temporary road file."""
    if not road_id.startswith("tmp_road_"):
        raise HTTPException(
            status_code=400, detail="Can only delete temporary road files"
        )
    success = road_service.delete_temp_road(road_id)
    if not success:
        raise HTTPException(status_code=404, detail=f"Temp road '{road_id}' not found")
    return {"deleted": road_id}
