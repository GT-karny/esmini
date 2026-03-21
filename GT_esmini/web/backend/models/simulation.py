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


class ManualDriveButtonMapping(BaseModel):
    upshift: int = 4
    downshift: int = 5
    override: int = 0
    indicator_left: int = 7
    indicator_right: int = 6


class ManualDriveSDL2Config(BaseModel):
    device_index: int = 0
    deadzone: float = 0.05
    button_mapping: ManualDriveButtonMapping = ManualDriveButtonMapping()


class ManualDriveDomainConfig(BaseModel):
    lateral: str = "manual"
    longitudinal: str = "manual"


class ManualDriveNetworkInput(BaseModel):
    transport_type: str = "udp"
    port: int = 9100
    level: str = "pedal_steer"


class ManualDriveNetworkPhysics(BaseModel):
    transport_type: str = "udp"
    host: str = "127.0.0.1"
    cmd_port: int = 9200
    state_port: int = 9201


class ManualDriveFFBConfig(BaseModel):
    spring_coefficient: float = 0.5
    damper_coefficient: float = 0.3
    constant_gain: float = 1.0
    max_force: float = 1.0


class ManualDriveControllerConfig(BaseModel):
    input_type: str = "sdl2_wheel"
    physics_type: str = "real_vehicle"
    ffb_enabled: bool = True
    domain: ManualDriveDomainConfig = ManualDriveDomainConfig()
    sdl2: ManualDriveSDL2Config = ManualDriveSDL2Config()
    input_network: ManualDriveNetworkInput = ManualDriveNetworkInput()
    physics_network: ManualDriveNetworkPhysics = ManualDriveNetworkPhysics()
    ffb: ManualDriveFFBConfig = ManualDriveFFBConfig()


class ControllerConfig(BaseModel):
    controller_type: str = "default"  # "default" | "python" | "manual"
    python: PythonControllerConfig = PythonControllerConfig()
    manual_drive: ManualDriveControllerConfig = ManualDriveControllerConfig()


class OsiConfig(BaseModel):
    enabled: bool = False
    ip: str = "127.0.0.1"


class WindowConfig(BaseModel):
    x: int = 60
    y: int = 60
    w: int = 1280
    h: int = 720


class ExecutionConfig(BaseModel):
    headless: bool = False
    record: bool = False
    hz: int = 100
    no_realtime: bool = True
    timeout: int = 60
    osi: OsiConfig = OsiConfig()
    autolight: bool = False
    vehicle_physics: bool = True
    threads: bool = False
    window: WindowConfig = WindowConfig()
    extra_args: list[str] = []


class SimulationRequest(BaseModel):
    scenario_id: str
    project_id: str | None = None
    controller: ControllerConfig = ControllerConfig()
    execution: ExecutionConfig = ExecutionConfig()
    param_overrides: dict[str, str] | None = None


class SimulationStatus(BaseModel):
    job_id: str
    scenario_id: str
    project_id: str | None = None
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
