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
    headlight: int = -1
    high_beam: int = -1
    fog_light: int = -1
    hazard: int = -1


class ManualDriveSDL2Config(BaseModel):
    device_index: int = 0
    deadzone: float = 0.05
    button_mapping: ManualDriveButtonMapping = ManualDriveButtonMapping()


class ManualDriveKeyboardConfig(BaseModel):
    steer_left: str = "A"
    steer_right: str = "D"
    throttle: str = "W"
    brake: str = "S"
    clutch: str = "LShift"
    upshift: str = "E"
    downshift: str = "Q"
    override_key: str = "O"
    indicator_left: str = "Z"
    indicator_right: str = "X"
    headlight: str = "L"
    high_beam: str = "K"
    fog_light: str = "F"
    hazard: str = "H"
    steer_rate: float = 2.0
    centering_rate: float = 3.0
    pedal_press_rate: float = 4.0
    pedal_release_rate: float = 6.0


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
    sat_gain: float = 0.08
    sat_centering_gain: float = 1.50
    friction_base: float = 0.12
    friction_speed_gain: float = 0.04
    damper_base: float = 0.02
    damper_speed_gain: float = 0.06
    soft_stop_gain: float = 0.5
    lock_angle: float = 0.7
    assist_low_speed: float = 0.90
    assist_high_speed: float = 0.20
    max_force: float = 1.0
    disable_non_realtime: bool = True


class ManualDriveControllerConfig(BaseModel):
    input_type: str = "sdl2_wheel"
    physics_type: str = "real_vehicle"
    ffb_enabled: bool = True
    domain: ManualDriveDomainConfig = ManualDriveDomainConfig()
    sdl2: ManualDriveSDL2Config = ManualDriveSDL2Config()
    keyboard: ManualDriveKeyboardConfig = ManualDriveKeyboardConfig()
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
    kinematic_mode: bool = False
    route_drive_mode: bool = False
    route_drive_timing: str = "normal"  # RouteDrive lane-change Timing knob (late | normal | early)
    route_drive_gap: str = "normal"     # RouteDrive lane-change Gap knob (wide | normal | tight)
    threads: bool = False
    window: WindowConfig = WindowConfig()
    extra_args: list[str] = []
    drive_mode: str = "comfort"  # HVDEstimator drive mode (comfort | sport)


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


class DriveModeRequest(BaseModel):
    mode: str = Field(min_length=1, max_length=32, description="HVDEstimator drive mode (e.g. 'comfort', 'sport')")
