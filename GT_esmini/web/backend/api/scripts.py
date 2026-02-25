"""Python controller script management API endpoints."""

from __future__ import annotations

from fastapi import APIRouter, HTTPException

from GT_esmini.web.backend.models.script import ScriptInfo, ScriptListResponse
from GT_esmini.web.backend.services import script_service

router = APIRouter(prefix="/api/scripts", tags=["scripts"])


@router.get("", response_model=ScriptListResponse)
async def list_scripts():
    """List available Python controller scripts."""
    scripts = script_service.list_scripts()
    return ScriptListResponse(scripts=scripts)


@router.get("/{script_path:path}", response_model=ScriptInfo)
async def get_script(script_path: str):
    """Get details for a specific Python script."""
    info = script_service.get_script_detail(script_path)
    if info is None:
        raise HTTPException(status_code=404, detail=f"Script '{script_path}' not found")
    return info
