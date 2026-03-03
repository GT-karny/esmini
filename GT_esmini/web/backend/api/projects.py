"""Project management API endpoints."""

from __future__ import annotations

import io
import zipfile

from fastapi import APIRouter, HTTPException, Request, UploadFile, File, Form
from fastapi.responses import FileResponse, Response, StreamingResponse

from GT_esmini.web.backend import config
from GT_esmini.web.backend.models.project import (
    ParameterPreset,
    PresetCreateRequest,
    PresetUpdateRequest,
    ProjectCreateRequest,
    ProjectDetail,
    ProjectFile,
    ProjectListItem,
    ProjectUpdateRequest,
    ScenarioInfo,
    ScenarioParam,
)
from GT_esmini.web.backend.services import project_service

router = APIRouter(prefix="/api/projects", tags=["projects"])


# ---------------------------------------------------------------------------
# Project template download (must be defined before /{project_id})
# ---------------------------------------------------------------------------

_TEMPLATE_README = """\
# GT-SIM Project Template

## Folder Structure

- `xosc/` — OpenSCENARIO scenario files (.xosc)
- `xodr/` — OpenDRIVE road network files (.xodr)
- `docs/` — Scenario documentation (Markdown). Name files to match scenarios (e.g., example.md for example.xosc)
- `docs/img/` — Images referenced from markdown
- `catalogs/` — Shared OpenSCENARIO catalogs (Vehicles, Controllers, etc.)
"""

_TEMPLATE_XOSC = """\
<?xml version="1.0" encoding="UTF-8"?>
<OpenSCENARIO>
  <FileHeader revMajor="1" revMinor="1" date="2024-01-01" description="Template scenario" author="GT-SIM" />
  <ParameterDeclarations />
  <CatalogLocations>
    <VehicleCatalog><Directory path="catalogs/Vehicles" /></VehicleCatalog>
  </CatalogLocations>
  <RoadNetwork><LogicFile filepath="xodr/your_road.xodr" /></RoadNetwork>
  <Entities />
  <Storyboard>
    <Init><Actions /></Init>
    <StopTrigger />
  </Storyboard>
</OpenSCENARIO>
"""

_TEMPLATE_EXAMPLE_MD = """\
# Example Scenario

Describe your scenario here.
"""


@router.get("/template/download")
async def download_project_template():
    """Generate and download a project template ZIP."""
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as zf:
        zf.writestr("my_project/README.md", _TEMPLATE_README)
        zf.writestr("my_project/xosc/example.xosc", _TEMPLATE_XOSC)
        # Empty directories: add entries with trailing slash
        zf.writestr("my_project/xodr/", "")
        zf.writestr("my_project/docs/img/", "")
        zf.writestr("my_project/docs/example.md", _TEMPLATE_EXAMPLE_MD)

        # Copy catalog files from resources
        catalogs_src = config.RESOURCES_DIR / "xosc" / "Catalogs"
        catalog_subdirs = [
            "Vehicles", "Controllers", "Environments",
            "Maneuvers", "MiscObjects", "Pedestrians", "Routes",
        ]
        for subdir in catalog_subdirs:
            src_dir = catalogs_src / subdir
            if not src_dir.is_dir():
                continue
            for xosc_file in src_dir.glob("*.xosc"):
                arc_path = f"my_project/catalogs/{subdir}/{xosc_file.name}"
                zf.write(str(xosc_file), arc_path)

    buf.seek(0)
    return StreamingResponse(
        buf,
        media_type="application/zip",
        headers={
            "Content-Disposition": "attachment; filename=gt_sim_project_template.zip",
        },
    )


# ---------------------------------------------------------------------------
# Project CRUD
# ---------------------------------------------------------------------------

@router.get("", response_model=list[ProjectListItem])
async def list_projects():
    """List all projects including built-in samples."""
    return await project_service.list_projects()


@router.get("/{project_id}", response_model=ProjectDetail)
async def get_project(project_id: str):
    """Get project details."""
    proj = await project_service.get_project(project_id)
    if proj is None:
        raise HTTPException(status_code=404, detail=f"Project '{project_id}' not found")
    return proj


@router.post("", response_model=ProjectDetail, status_code=201)
async def create_project(req: ProjectCreateRequest):
    """Create a new empty project."""
    return await project_service.create_project(req)


@router.post("/upload", response_model=ProjectDetail, status_code=201)
async def upload_project(
    file: UploadFile = File(...),
    name: str = Form(...),
    description: str = Form(""),
):
    """Create a project from a ZIP file upload."""
    if not file.filename or not file.filename.lower().endswith(".zip"):
        raise HTTPException(status_code=400, detail="File must be a .zip archive")

    data = await file.read()
    if len(data) > 100 * 1024 * 1024:  # 100MB limit
        raise HTTPException(status_code=413, detail="ZIP file too large (max 100MB)")

    try:
        return await project_service.create_project_from_zip(data, name, description)
    except ValueError as e:
        raise HTTPException(status_code=400, detail=str(e))


@router.put("/{project_id}", response_model=dict)
async def update_project(project_id: str, req: ProjectUpdateRequest):
    """Update project metadata (name, description)."""
    success = await project_service.update_project(project_id, req.name, req.description)
    if not success:
        raise HTTPException(
            status_code=403,
            detail="Project not found or is read-only (built-in)",
        )
    return {"status": "updated"}


@router.delete("/{project_id}")
async def delete_project(project_id: str):
    """Delete a project and all its files."""
    success = await project_service.delete_project(project_id)
    if not success:
        raise HTTPException(
            status_code=403,
            detail="Project not found or is read-only (built-in)",
        )
    return {"status": "deleted"}


# ---------------------------------------------------------------------------
# File management
# ---------------------------------------------------------------------------

@router.get("/{project_id}/files", response_model=list[ProjectFile])
async def list_files(project_id: str):
    """List all files in a project."""
    files = await project_service.list_files(project_id)
    if files is None:
        raise HTTPException(status_code=404, detail=f"Project '{project_id}' not found")
    return files


@router.post("/{project_id}/files")
async def upload_file(
    project_id: str,
    file: UploadFile = File(...),
    path: str = Form(""),
):
    """Upload a file to a project (add or replace)."""
    file_path = path if path else (file.filename or "uploaded_file")
    data = await file.read()
    success = await project_service.upload_file(project_id, file_path, data)
    if not success:
        raise HTTPException(
            status_code=403,
            detail="Project not found, is read-only (built-in), or path is invalid",
        )
    return {"status": "uploaded", "path": file_path}


@router.get("/{project_id}/files/{file_path:path}")
async def download_file(project_id: str, file_path: str):
    """Download a file from a project."""
    abs_path = await project_service.get_file_path(project_id, file_path)
    if abs_path is None:
        raise HTTPException(status_code=404, detail="File not found")
    return FileResponse(str(abs_path), filename=abs_path.name)


@router.delete("/{project_id}/files/{file_path:path}")
async def delete_file(project_id: str, file_path: str):
    """Delete a file from a project."""
    success = await project_service.delete_file(project_id, file_path)
    if not success:
        raise HTTPException(
            status_code=403,
            detail="Project not found, is read-only (built-in), or file not found",
        )
    return {"status": "deleted"}


# ---------------------------------------------------------------------------
# Scenarios
# ---------------------------------------------------------------------------

@router.get("/{project_id}/scenarios", response_model=list[ScenarioInfo])
async def list_scenarios(project_id: str):
    """List all xosc scenarios in a project with parsed details."""
    scenarios = await project_service.list_scenarios(project_id)
    if scenarios is None:
        raise HTTPException(status_code=404, detail=f"Project '{project_id}' not found")
    return scenarios


@router.get("/{project_id}/scenarios/{scenario_file:path}/road-geometry")
async def get_road_geometry(project_id: str, scenario_file: str):
    """Extract lane boundary polylines from the scenario's OpenDRIVE file."""
    import asyncio
    from pathlib import Path

    from GT_esmini.web.backend.services import road_geometry_service

    proj = await project_service.get_project(project_id)
    if proj is None:
        raise HTTPException(status_code=404, detail=f"Project '{project_id}' not found")

    root = Path(proj.root_path)
    xosc_path = root / scenario_file
    if not xosc_path.is_file():
        raise HTTPException(status_code=404, detail=f"Scenario file not found: {scenario_file}")

    # Parse xosc to find the road file reference
    import xml.etree.ElementTree as ET
    try:
        tree = ET.parse(xosc_path)
    except ET.ParseError:
        raise HTTPException(status_code=400, detail="Failed to parse XOSC")

    logic = tree.getroot().find(".//RoadNetwork/LogicFile")
    if logic is None:
        raise HTTPException(status_code=404, detail="No road file in scenario")

    road_filepath = logic.get("filepath", "")
    if not road_filepath:
        raise HTTPException(status_code=404, detail="Empty road file path")

    # Resolve relative to xosc parent directory
    road_path = Path(road_filepath)
    if not road_path.is_absolute():
        road_path = (xosc_path.parent / road_filepath).resolve()
    if not road_path.is_file():
        raise HTTPException(status_code=404, detail=f"Road file not found: {road_filepath}")

    geometry = await asyncio.to_thread(
        road_geometry_service.extract_road_geometry, road_path
    )
    return geometry


@router.get("/{project_id}/scenarios/{scenario_file:path}/docs")
async def get_scenario_docs(project_id: str, scenario_file: str):
    """Get markdown documentation for a scenario file."""
    from pathlib import Path, PurePosixPath

    proj = await project_service.get_project(project_id)
    if proj is None:
        raise HTTPException(status_code=404, detail=f"Project '{project_id}' not found")

    stem = PurePosixPath(scenario_file).stem
    doc_path = Path(proj.root_path) / "docs" / f"{stem}.md"

    if not doc_path.is_file():
        raise HTTPException(status_code=404, detail=f"Documentation not found for '{stem}'")

    content = doc_path.read_text(encoding="utf-8")
    return Response(content=content, media_type="text/markdown")


@router.get("/{project_id}/scenarios/{scenario_file:path}/params", response_model=list[ScenarioParam])
async def get_scenario_params(project_id: str, scenario_file: str):
    """Get ParameterDeclarations from a scenario file."""
    params = await project_service.get_scenario_params(project_id, scenario_file)
    if params is None:
        raise HTTPException(status_code=404, detail="Project or scenario not found")
    return params


# ---------------------------------------------------------------------------
# Parameter presets
# ---------------------------------------------------------------------------

@router.get("/{project_id}/scenarios/{scenario_file:path}/presets", response_model=list[ParameterPreset])
async def list_presets(project_id: str, scenario_file: str):
    """List parameter presets for a scenario."""
    return await project_service.list_presets(project_id, scenario_file)


@router.post("/{project_id}/scenarios/{scenario_file:path}/presets", response_model=ParameterPreset, status_code=201)
async def create_preset(project_id: str, scenario_file: str, req: PresetCreateRequest):
    """Create a parameter preset for a scenario."""
    return await project_service.create_preset(project_id, scenario_file, req.name, req.values)


@router.put("/{project_id}/presets/{preset_id}", response_model=dict)
async def update_preset(project_id: str, preset_id: str, req: PresetUpdateRequest):
    """Update a parameter preset."""
    success = await project_service.update_preset(preset_id, req.name, req.values)
    if not success:
        raise HTTPException(status_code=404, detail="Preset not found")
    return {"status": "updated"}


@router.delete("/{project_id}/presets/{preset_id}")
async def delete_preset(project_id: str, preset_id: str):
    """Delete a parameter preset."""
    success = await project_service.delete_preset(preset_id)
    if not success:
        raise HTTPException(status_code=404, detail="Preset not found")
    return {"status": "deleted"}
