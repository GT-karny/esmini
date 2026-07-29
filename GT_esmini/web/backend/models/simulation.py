"""Pydantic models for simulation-related endpoints."""

from __future__ import annotations

from typing import Any

from pydantic import BaseModel, Field

from GT_esmini.web.backend.config import DEFAULT_VD_INPUT_PORT


class PythonControllerConfig(BaseModel):
    script: str = "DriverScript/pythondriver/scenario_drive_embedded.py"
    python_class: str = Field(default="EmbeddedController", alias="class")
    python_home: str = ""
    trace_enabled: bool = True
    trace_dir: str = ""

    model_config = {"populate_by_name": True}


# feature:F7 gap #2 -- SINGLE SOURCE OF TRUTH for the SDL2 button mapping.
#
# There used to be two independent lists: ManualDriveButtonMapping's field
# names (this file) and a hand-written 9-line translation block in
# simulation_runner._write_manual_drive_config(). Adding a button meant
# remembering both. F7 added auto_resume to neither the model nor the runner's
# block, which is exactly how gap #5 (AUTO_RESUME unassigned on 100% of
# GUI-launched manual runs) came to exist.
#
# C++ reads FLAT keys under "input" (ManualDriveConfig.cpp: parse_int
# ("upshift_button", ...)), and its config reader is a line-wise substring
# scan with no notion of JSON scope -- so a nested "button_mapping": {"upshift":
# 4} block does not merely fail to register, it also collides with
# ManualDriveKeyboardConfig's identically-named "upshift" and overwrites the
# keyboard binding with a number. Everything that writes this mapping must go
# through the table below.
SDL2_BUTTON_KEY_MAP: dict[str, str] = {
    "upshift": "upshift_button",
    "downshift": "downshift_button",
    "override": "override_button",
    "indicator_left": "indicator_left_button",
    "indicator_right": "indicator_right_button",
    "headlight": "headlight_button",
    "high_beam": "high_beam_button",
    "fog_light": "fog_light_button",
    "hazard": "hazard_button",
    # Present in the shipped config and read by C++, but deliberately NOT a
    # typed field below yet -- see gap #6. extra="allow" lets it round-trip.
    "auto_resume": "auto_resume_button",
}


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
    # feature:F7 gap #6 -- exposed so the GUI can rebind it. Default is 3, the
    # value the shipped config/manual_drive.json carries, NOT -1: the other
    # defaults here also mirror the shipped config, and more importantly
    # _sdl2_button_entries() only falls back to the on-disk config for buttons
    # this model does NOT declare. Declaring it with -1 would make an omitted
    # field beat the shipped 3 and re-create gap #5 (AUTO_RESUME unassigned on
    # every GUI-launched manual run) through the very change meant to fix it.
    auto_resume: int = 3

    model_config = {"extra": "allow"}


class ManualDriveSDL2Config(BaseModel):
    device_index: int = 0
    deadzone: float = 0.05
    button_mapping: ManualDriveButtonMapping = ManualDriveButtonMapping()

    model_config = {"extra": "allow"}


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

    model_config = {"extra": "allow"}


class ManualDriveDomainConfig(BaseModel):
    lateral: str = "manual"
    longitudinal: str = "manual"

    model_config = {"extra": "allow"}


class ManualDriveNetworkInput(BaseModel):
    transport_type: str = "udp"
    port: int = DEFAULT_VD_INPUT_PORT
    level: str = "pedal_steer"

    model_config = {"extra": "allow"}


class ManualDriveNetworkPhysics(BaseModel):
    transport_type: str = "udp"
    host: str = "127.0.0.1"
    cmd_port: int = 9200
    state_port: int = 9201

    model_config = {"extra": "allow"}


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

    # F7b (target-tracking servo) and F7 (jerk-cap safety watchdog) added many
    # more ffb.* keys directly to config/manual_drive.json without extending
    # this model. Without extra="allow", pydantic v2's default extra='ignore'
    # silently dropped every key this model doesn't declare (e.g.
    # target_track_*, safety_*) the instant *any* manual-drive config was
    # saved through the API, including edits unrelated to FFB. See
    # ManualDriveControllerConfig below for the same fix applied uniformly.
    model_config = {"extra": "allow"}


class ManualDriveOverrideConfig(BaseModel):
    """feature:F7 gap #6 -- the takeover thresholds, finally reachable.

    Defaults mirror BOTH config/manual_drive.json and the C++ compile-time
    values (ManualDriveConfig.hpp:483-488); they coincide, which is why gap #4
    (the per-run writer replacing this whole block with {"enabled": True}) was
    invisible with a stock config.
    """

    enabled: bool = True
    steering_threshold: float = 0.05
    throttle_threshold: float = 0.1
    brake_threshold: float = 0.1
    auto_return_timeout: float = 0.0
    button_override: bool = True

    model_config = {"extra": "allow"}


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

    # feature:F7 gap #6 -- exposed so the GUI can edit them. Until now these
    # existed only in config/manual_drive.json with no control anywhere, so
    # "GUI から一切触れない" was the whole complaint. Defaults mirror the
    # shipped config AND ManualDriveConfig.hpp:483-488, so a request that
    # leaves them alone reproduces the previous behaviour exactly.
    #
    # None means "not stated by this request" -> the per-run writer falls back
    # to the on-disk config. That distinction matters: an older frontend build
    # that does not send these must not force C++ defaults over a user's
    # hand-edited file.
    override_cfg: ManualDriveOverrideConfig | None = None
    indicator_cancel_angle: float | None = None
    vehicle_params_file: str | None = None

    # Preserve any on-disk section this model doesn't model as a typed field
    # (e.g. legacy "input" / "physics" / "override" / "indicator_cancel_angle"
    # top-level keys that ManualDriveConfig.cpp's flat key-scan still reads).
    # Extra fields round-trip through model_dump() instead of being dropped.
    model_config = {"extra": "allow"}


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
    # F6: force-enable environment-driven headlights (night/tunnel low beam + auto
    # high beam) via GT_Sim's --autolight-headlights, regardless of the
    # auto_light.json headlight_enabled master switch. Self-sufficient in the C++
    # (implies the AutoLight master switch — commit 942c07c0).
    autolight_headlights: bool = False
    vehicle_physics: bool = True
    kinematic_mode: bool = False
    route_drive_mode: bool = False
    route_drive_timing: str = (
        "normal"  # RouteDrive lane-change Timing knob (late | normal | early)
    )
    route_drive_gap: str = (
        "normal"  # RouteDrive lane-change Gap knob (wide | normal | tight)
    )
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
    speed_factor: float = Field(
        ge=0.1, le=100.0, description="Speed multiplier (1.0 = realtime)"
    )


class DriveModeRequest(BaseModel):
    mode: str = Field(
        min_length=1,
        max_length=32,
        description="HVDEstimator drive mode (e.g. 'comfort', 'sport')",
    )
