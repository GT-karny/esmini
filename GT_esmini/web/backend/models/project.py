"""Pydantic models for project-related endpoints."""

from __future__ import annotations

from pydantic import BaseModel


class ProjectListItem(BaseModel):
    project_id: str
    name: str
    description: str
    is_builtin: bool
    scenario_count: int = 0
    road_count: int = 0
    file_count: int = 0
    created_at: str
    updated_at: str


class ProjectDetail(BaseModel):
    project_id: str
    name: str
    description: str
    is_builtin: bool
    root_path: str
    scenario_count: int = 0
    road_count: int = 0
    file_count: int = 0
    created_at: str
    updated_at: str


class ProjectCreateRequest(BaseModel):
    name: str
    description: str = ""


class ProjectUpdateRequest(BaseModel):
    name: str | None = None
    description: str | None = None


class ProjectFile(BaseModel):
    path: str
    name: str
    type: str  # xosc | xodr | catalog | model | config | other
    size: int
    modified: str
    is_dir: bool = False


class ScenarioParam(BaseModel):
    name: str
    type: str  # double | integer | string | boolean
    value: str


class ScenarioInfo(BaseModel):
    file: str
    filename: str
    road_file: str | None = None
    entities: list[dict] = []
    params: list[ScenarioParam] = []
    has_controller: bool = False


class ParameterPreset(BaseModel):
    preset_id: str  # = YAML top-level key (preset name)
    name: str
    description: str = ""
    values: dict[str, str]


class PresetCreateRequest(BaseModel):
    name: str
    values: dict[str, str]
    description: str = ""


class PresetUpdateRequest(BaseModel):
    name: str | None = None
    values: dict[str, str] | None = None
    description: str | None = None
