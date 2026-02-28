"""Pydantic models for simulation-related endpoints."""

from __future__ import annotations

from typing import Any

from pydantic import BaseModel, Field


class PythonControllerConfig(BaseModel):
    script: str = "DriverScript/pythondriver/scenario_drive_embedded.py"
    python_class: str = Field(default="EmbeddedController", alias="class")
    python_home: str = ""
    trace_enabled: bool = True
    trace_dir: str = ""

    model_config = {"populate_by_name": True}


class ControllerConfig(BaseModel):
    controller_type: str = "default"  # "default" | "python"
    python: PythonControllerConfig = PythonControllerConfig()


class OsiConfig(BaseModel):
    enabled: bool = False
    ip: str = "127.0.0.1"


class WindowConfig(BaseModel):
    x: int = 60
    y: int = 60
    w: int = 1280
    h: int = 720


class ExecutionConfig(BaseModel):
    headless: bool = True
    record: bool = True
    hz: int = 100
    no_realtime: bool = True
    timeout: int = 60
    osi: OsiConfig = OsiConfig()
    autolight: bool = False
    threads: bool = False
    window: WindowConfig = WindowConfig()
    extra_args: list[str] = []


class SimulationRequest(BaseModel):
    scenario_id: str
    controller: ControllerConfig = ControllerConfig()
    execution: ExecutionConfig = ExecutionConfig()


class SimulationStatus(BaseModel):
    job_id: str
    scenario_id: str
    status: str  # queued | running | completed | failed | cancelled | timeout
    controller_type: str = "default"
    progress_pct: int = 0
    pid: int | None = None
    exit_code: int | None = None
    output_dir: str | None = None
    started_at: str | None = None
    completed_at: str | None = None
    error_message: str | None = None
    options: dict[str, Any] = {}


class SimulationListResponse(BaseModel):
    jobs: list[SimulationStatus]
    total: int


class SpeedRequest(BaseModel):
    speed_factor: float = Field(ge=0.1, le=100.0, description="Speed multiplier (1.0 = realtime)")
