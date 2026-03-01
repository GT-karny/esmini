"""Road file management API endpoints."""

from __future__ import annotations

from fastapi import APIRouter, HTTPException, Request

from GT_esmini.web.backend.models.scenario import RoadUploadResponse
from GT_esmini.web.backend.services import road_service

router = APIRouter(prefix="/api/roads", tags=["roads"])


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
        raise HTTPException(status_code=400, detail="Can only delete temporary road files")
    success = road_service.delete_temp_road(road_id)
    if not success:
        raise HTTPException(status_code=404, detail=f"Temp road '{road_id}' not found")
    return {"deleted": road_id}
