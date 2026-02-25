"""Pydantic models for scenario-related endpoints."""

from __future__ import annotations

from pydantic import BaseModel


class ScenarioListItem(BaseModel):
    id: str
    filename: str
    path: str
    modified: str
    size: int


class ScenarioEntity(BaseModel):
    name: str
    vehicle: str | None = None
    controller: str | None = None


class ScenarioDetail(BaseModel):
    id: str
    filename: str
    path: str
    road_file: str | None = None
    entities: list[ScenarioEntity] = []
    has_controller: bool = False


class VariantRequest(BaseModel):
    variant_type: str  # "python" | "default"
    python_script: str = "DriverScript/pythondriver/scenario_drive_embedded.py"
    python_class: str = "EmbeddedController"
    python_home: str = ""
    python_trace: bool = True
    python_trace_dir: str = ""


class VariantResponse(BaseModel):
    variant_id: str
    path: str
    variant_type: str
