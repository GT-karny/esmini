"""Simulation execution service: launch GT_Sim.exe and manage jobs."""

from __future__ import annotations

import asyncio
import json
import logging
import os
import shutil
import signal
import subprocess
import sys
import threading
import uuid
from datetime import datetime, timezone
from pathlib import Path

import aiosqlite

from GT_esmini.web.backend.config import (
    CONFIG_DIR,
    DEFAULT_VD_INPUT_PORT,
    GT_SIM_EXE,
    REPO_ROOT,
    RESULTS_DIR,
)
from GT_esmini.web.backend.db.database import get_db
from GT_esmini.web.backend.models.simulation import (
    SDL2_BUTTON_KEY_MAP,
    ControllerConfig,
    ExecutionConfig,
    SimulationRequest,
    SimulationStatus,
)
from GT_esmini.web.backend.services.log_extract import extract_failure
from GT_esmini.web.backend.services.osi_bridge import start_bridge, stop_bridge
from GT_esmini.web.backend.services.sv_bridge import start_sv_bridge, stop_sv_bridge
from GT_esmini.web.backend.services import vd_recorder
from GT_esmini.web.backend.services.vd_metrics import require_udp_port_free
from GT_esmini.web.backend.services.xosc_paths import absolutize_scenario_paths

# In-memory tracking of job_id -> Popen (registered at subprocess start to avoid race)
_running_procs: dict[str, subprocess.Popen] = {}
_running_procs_lock = threading.Lock()

# In-memory tracking of control pipe names per job (for speed control)
_control_pipes: dict[str, str] = {}  # {job_id: pipe_name}
_pipes_lock = threading.Lock()


def _now_iso() -> str:
    return datetime.now(timezone.utc).isoformat()


def _build_output_dir(job_id: str) -> Path:
    output_dir = RESULTS_DIR / job_id
    output_dir.mkdir(parents=True, exist_ok=True)
    return output_dir


def _absolutize_xosc(variant_path: Path, source_dir: str) -> None:
    """Absolutize relative paths in XOSC so it works from any CWD."""
    import xml.etree.ElementTree as ET

    tree = ET.parse(variant_path)
    root = tree.getroot()
    absolutize_scenario_paths(root, source_dir)
    tree.write(variant_path, encoding="utf-8", xml_declaration=True)


def _apply_param_overrides(xosc_path: Path, overrides: dict[str, str]) -> None:
    """Update ParameterDeclaration default values in XOSC variant file.

    esmini resolves $parameter references during SE_Init() XOSC parsing,
    so overrides applied via SE_SetParameter*() after init have no effect.
    This function patches the XOSC before it is loaded by esmini.
    """
    import xml.etree.ElementTree as ET

    tree = ET.parse(xosc_path)
    root = tree.getroot()
    pd = root.find("ParameterDeclarations")
    if pd is None:
        return
    for decl in pd.findall("ParameterDeclaration"):
        name = decl.get("name", "")
        if name in overrides:
            decl.set("value", overrides[name])
    tree.write(xosc_path, encoding="utf-8", xml_declaration=True)


def _generate_manual_variant(
    baseline_xosc: Path,
    output_path: Path,
    controller: ControllerConfig,
) -> None:
    """Generate XOSC variant with ManualDriveController injected."""
    import xml.etree.ElementTree as ET

    tree = ET.parse(baseline_xosc)
    root = tree.getroot()

    # Remove existing ObjectController from first entity
    all_entities = root.findall(".//ScenarioObject")
    if not all_entities:
        # Fallback: just copy
        shutil.copy2(baseline_xosc, output_path)
        return

    entity = all_entities[0]
    existing_oc = entity.find("ObjectController")
    if existing_oc is not None:
        entity.remove(existing_oc)

    # Create ManualDriveController
    ctrl = ET.Element("Controller")
    ctrl.set("name", "ManualDriveController")

    props = ET.SubElement(ctrl, "Properties")
    p1 = ET.SubElement(props, "Property")
    p1.set("name", "esminiController")
    p1.set("value", "ManualDriveController")

    # ConfigFile property — absolute path to per-run config
    config_abs = str((output_path.parent / "manual_drive.json").resolve())
    p2 = ET.SubElement(props, "Property")
    p2.set("name", "ConfigFile")
    p2.set("value", config_abs)

    oc = ET.Element("ObjectController")
    oc.append(ctrl)

    # Insert after Vehicle/CatalogReference (same pattern as generate_python_variant)
    insert_pos = None
    for i, child in enumerate(entity):
        if child.tag in ("Vehicle", "CatalogReference"):
            insert_pos = i + 1
            break
    if insert_pos is not None:
        entity.insert(insert_pos, oc)
    else:
        entity.append(oc)

    # Add <ActivateControllerAction> in Init/Actions/Private for the ego entity
    ego_name = entity.get("name", "")
    for private in root.findall(".//Init/Actions/Private"):
        if private.get("entityRef") != ego_name:
            continue
        # Remove existing ActivateControllerAction
        for pa in private.findall("PrivateAction"):
            act = pa.find("ActivateControllerAction")
            if act is not None:
                private.remove(pa)
        # Add new ActivateControllerAction
        pa = ET.SubElement(private, "PrivateAction")
        act = ET.SubElement(pa, "ActivateControllerAction")
        act.set("longitudinal", "true")
        act.set("lateral", "true")

    output_path.parent.mkdir(parents=True, exist_ok=True)
    tree.write(output_path, encoding="utf-8", xml_declaration=True)


def _generate_virtual_driver_variant(
    baseline_xosc: Path,
    output_path: Path,
    enable_override: bool = True,
) -> None:
    """Generate XOSC variant with VirtualDriverController injected.

    Mirrors _generate_manual_variant but assigns the VirtualDriver controller
    (full-physics virtual driver) and activates it on both domains.

    When ``enable_override`` is set (default), a per-run virtual_driver.json is
    written next to the variant (see ``_write_virtual_driver_config`` for how
    input_type is chosen) and the controller's ``ConfigFile`` property points
    at it. With the default "network" input_type the web manual-override
    panel can inject pedal/steer/indicator commands over UDP at any time; with
    no input the NetworkInputBridge stays empty and the auto pipeline drives
    normally. Choosing "sdl2_wheel" instead hands input over to a physical
    wheel and the web override panel has nothing to drive.
    """
    import xml.etree.ElementTree as ET

    tree = ET.parse(baseline_xosc)
    root = tree.getroot()

    all_entities = root.findall(".//ScenarioObject")
    if not all_entities:
        shutil.copy2(baseline_xosc, output_path)
        return

    entity = all_entities[0]
    existing_oc = entity.find("ObjectController")
    # Per-scenario traffic-policy opt-in: a scenario may carry
    # <Property name="policies" value="traffic_light,stop_yield,lead"/> on its
    # VirtualDriverController. We read it BEFORE stripping the controller, then
    # enable exactly those policies in the per-run config. Scenarios without it
    # are unchanged (policies stay OFF). Used by the verification project so the
    # GUI shows signals/signs/lead-following actually taking effect.
    requested_policies: list[str] = []
    if existing_oc is not None:
        for prop in existing_oc.findall("./Controller/Properties/Property"):
            if prop.get("name") == "policies":
                requested_policies = [
                    p.strip() for p in (prop.get("value") or "").split(",") if p.strip()
                ]
        entity.remove(existing_oc)

    ctrl = ET.Element("Controller")
    ctrl.set("name", "VirtualDriverController")
    props = ET.SubElement(ctrl, "Properties")
    p1 = ET.SubElement(props, "Property")
    p1.set("name", "esminiController")
    p1.set("value", "VirtualDriverController")

    if enable_override:
        run_config = _write_virtual_driver_config(
            output_path.parent, requested_policies
        )
        p2 = ET.SubElement(props, "Property")
        p2.set("name", "ConfigFile")
        p2.set("value", str(run_config))

    oc = ET.Element("ObjectController")
    oc.append(ctrl)

    insert_pos = None
    for i, child in enumerate(entity):
        if child.tag in ("Vehicle", "CatalogReference"):
            insert_pos = i + 1
            break
    if insert_pos is not None:
        entity.insert(insert_pos, oc)
    else:
        entity.append(oc)

    # Add <ActivateControllerAction> (both domains) for the ego entity.
    ego_name = entity.get("name", "")
    for private in root.findall(".//Init/Actions/Private"):
        if private.get("entityRef") != ego_name:
            continue
        # Remove any existing ActivateControllerAction before adding ours.
        # Catch BOTH the bare form and the spec form nested in <ControllerAction>
        # (`.//` = descendant): a scenario that already embeds a controller carries
        # the nested form, and leaving it would double-activate the Longitudinal
        # domain, which esmini deactivates under OSC < v1.3 (controller goes dead).
        for pa in list(private.findall("PrivateAction")):
            if pa.find(".//ActivateControllerAction") is not None:
                private.remove(pa)
        pa = ET.SubElement(private, "PrivateAction")
        act = ET.SubElement(pa, "ActivateControllerAction")
        act.set("longitudinal", "true")
        act.set("lateral", "true")

    output_path.parent.mkdir(parents=True, exist_ok=True)
    tree.write(output_path, encoding="utf-8", xml_declaration=True)


_VD_POLICY_FLAG = {
    "lead": "policy_lead_enabled",
    "traffic_light": "policy_traffic_light_enabled",
    "stop_yield": "policy_stop_yield_enabled",
    "conflict": "policy_conflict_enabled",
    "crosswalk": "policy_crosswalk_enabled",
    "junction_priority": "policy_junction_priority_enabled",
    "aeb": "policy_aeb_enabled",
}


def _write_virtual_driver_config(
    output_dir: Path, policies: list[str] | None = None
) -> Path:
    """Write a per-run virtual_driver.json, respecting the user's input_type.

    Starts from the shipped config/virtual_driver.json (preserving tuned gains
    AND the input_type the user chose via PUT /api/virtual-driver/config —
    see virtual_driver_api.py's _STRING_ENUM_KEYS). We only default a base
    input_type of "stub" (the shipped, never-configured default) up to
    "network", so the web override panel (/ws/input) keeps driving the ego out
    of the box for anyone who has never touched the setting. An explicit
    "network" or "sdl2_wheel" choice passes through unmodified.

    Note: choosing "sdl2_wheel" means NetworkInputBridge is never constructed
    for the run (see ControllerVirtualDriver.cpp's input-source selection), so
    the /ws/input manual-override panel has nothing to drive — a physical
    SDL2 wheel owns lateral/longitudinal input instead. This is surfaced to
    the user in VirtualDriverPanel.tsx next to the input-source selector.

    ``policies`` enables the listed Phase-3 traffic policies (opt-in per
    scenario; default none). Returns the absolute path for the ConfigFile
    property.
    """
    base_config_path = CONFIG_DIR / "virtual_driver.json"
    base: dict = {}
    if base_config_path.exists():
        try:
            base = json.loads(base_config_path.read_text(encoding="utf-8"))
        except (json.JSONDecodeError, OSError):
            base = {}

    if base.get("input_type", "stub") == "stub":
        base["input_type"] = "network"
    base.setdefault("input_port", DEFAULT_VD_INPUT_PORT)
    base.setdefault("input_transport", "udp")

    for p in policies or []:
        flag = _VD_POLICY_FLAG.get(p)
        if flag:
            base[flag] = True

    output_dir.mkdir(parents=True, exist_ok=True)
    out_path = (output_dir / "virtual_driver.json").resolve()
    out_path.write_text(json.dumps(base, indent=2), encoding="utf-8")
    return out_path


def _sdl2_button_entries(mapping: Any, base: dict) -> dict[str, int]:
    """Build the flat C++ ``input.*_button`` entries from the shared table.

    feature:F7 gap #2/#5. Per button, in order of precedence:
      1. the request's own value, when the pydantic model carries that field;
      2. the existing on-disk config, for buttons the model does not model yet
         (``auto_resume`` today -- gap #6);
      3. -1 (C++'s "unassigned"), so a brand-new button can never silently
         inherit a stale value.

    Deriving this from SDL2_BUTTON_KEY_MAP is the whole point: the previous
    hand-written block is how auto_resume went missing on every GUI-launched
    manual run without anything failing.
    """
    base_input = base.get("input", {}) if isinstance(base.get("input"), dict) else {}
    entries: dict[str, int] = {}
    for field, cpp_key in SDL2_BUTTON_KEY_MAP.items():
        if hasattr(mapping, field):
            entries[cpp_key] = getattr(mapping, field)
        else:
            entries[cpp_key] = base_input.get(cpp_key, -1)
    return entries


# feature:F7 gap #4 -- mirrors ManualDriveConfig.hpp:483-488 (the C++
# compile-time defaults). Keep in sync with that struct: these are what a
# per-run config falls back to when the base config has no "override" section,
# and they are chosen so that behaviour is unchanged from the previous
# hardcoded {"enabled": True} whenever the base is silent.
_OVERRIDE_DEFAULTS: dict[str, Any] = {
    "enabled": True,
    "steering_threshold": 0.05,
    "throttle_threshold": 0.1,
    "brake_threshold": 0.1,
    "auto_return_timeout": 0.0,
    "button_override": True,
    "button_takeover": False,
}


def _write_manual_drive_config(output_dir: Path, controller: ControllerConfig) -> None:
    """Write manual_drive.json for per-run config override.

    Reads the existing config/manual_drive.json as a base so that
    user edits (especially FFB tuning) are preserved. Controller-level
    settings (domain, input type, etc.) from the request override the base.
    """
    # Read existing config as base (preserves user's FFB tuning etc.)
    base_config_path = CONFIG_DIR / "manual_drive.json"
    base: dict = {}
    if base_config_path.exists():
        try:
            base = json.loads(base_config_path.read_text(encoding="utf-8"))
        except (json.JSONDecodeError, OSError):
            pass

    md = controller.manual_drive
    config_data = {
        "input_type": md.input_type,
        "physics_type": md.physics_type,
        "ffb_enabled": md.ffb_enabled,
        "domain": md.domain.model_dump(),
        "input": {
            "device_index": md.sdl2.device_index,
            "deadzone": md.sdl2.deadzone,
            # feature:F7 gap #2 -- these nine lines used to be written out by
            # hand, which is the second of the two mapping tables that drifted
            # apart (see SDL2_BUTTON_KEY_MAP). Derived from the shared table
            # now, so adding a button in one place propagates here.
            # Includes auto_resume_button (gap #5). The first fix for #5 was a
            # standalone `"auto_resume_button": base.get(...)` line placed
            # AFTER this spread, which silently won over the table -- the same
            # last-writer-wins shape as the C++ config scanner that caused #2.
            # It was removed once gap #6 made auto_resume a typed field, so the
            # user's GUI edit now reaches the run instead of being overwritten
            # by the on-disk value.
            **_sdl2_button_entries(md.sdl2.button_mapping, base),
            "transport_type": md.input_network.transport_type,
            "port": md.input_network.port,
            "level": md.input_network.level,
        },
        "keyboard": md.keyboard.model_dump(),
        "physics": {
            # feature:F7 gap #3 -- was hardcoded "real_vehicle_params.json",
            # so a user who pointed config/manual_drive.json at a different
            # vehicle parameter file had it silently replaced on every
            # GUI-launched manual run. Invisible until now only because the
            # hardcoded string happens to equal the shipped value; a
            # default-vs-default test cannot see this bug at all.
            # gap #6: a GUI-stated value wins; None means "not stated" and
            # falls back to the on-disk config (gap #3's fix).
            "vehicle_params_file": (
                md.vehicle_params_file
                if md.vehicle_params_file is not None
                else base.get("physics", {}).get(
                    "vehicle_params_file", "real_vehicle_params.json"
                )
            ),
            "host": md.physics_network.host,
            "cmd_port": md.physics_network.cmd_port,
            "state_port": md.physics_network.state_port,
        },
        "indicator_cancel_angle": (
            md.indicator_cancel_angle
            if md.indicator_cancel_angle is not None
            else base.get("indicator_cancel_angle", 0.06)
        ),
        # NOTE: ffb still takes the on-disk value over the request, unlike the
        # gap #6 fields above and unlike the button mapping. That asymmetry
        # predates this work and changing it would alter behaviour beyond
        # gap #6's scope -- flagged as a follow-up rather than silently
        # rewritten here.
        "ffb": base.get("ffb", md.ffb.model_dump()),
        # feature:F7 gap #4 -- was {"enabled": True}, which dropped the other
        # five override settings entirely AND ignored the user's own
        # `enabled`. C++ then fell back to its compile-time defaults
        # (ManualDriveConfig.hpp:483-488), which coincide with the shipped
        # JSON values -- so the loss was invisible with a stock config and
        # would have surfaced the moment gap #6 exposed these in the GUI.
        #
        # Defaults below mirror those C++ compile-time values, so a base with
        # no "override" section produces exactly the previous behaviour. Base
        # wins per key. Nothing in the request model carries these yet (that
        # is gap #6), so the request deliberately contributes nothing here.
        # Precedence: C++ defaults < on-disk config < this request (gap #6).
        # The request layer is omitted entirely when override_cfg is None, so
        # a frontend that does not send it behaves exactly as before.
        "override": {
            **_OVERRIDE_DEFAULTS,
            **(base.get("override") if isinstance(base.get("override"), dict) else {}),
            **(md.override_cfg.model_dump() if md.override_cfg is not None else {}),
        },
    }
    config_path = output_dir / "manual_drive.json"
    config_path.write_text(
        json.dumps(config_data, indent=4, ensure_ascii=False),
        encoding="utf-8",
    )


def _prepare_xosc(
    scenario_path: Path,
    controller: ControllerConfig,
    output_dir: Path,
    param_overrides: dict[str, str] | None = None,
) -> Path:
    """Generate XOSC variant based on controller configuration."""
    source_dir = str(scenario_path.parent)

    if controller.controller_type == "python":
        variant_path = output_dir / f"{scenario_path.stem}_python.xosc"
        try:
            from scenario_generator import generate_python_variant  # noqa: PLC0415
        except ImportError as exc:
            raise RuntimeError(
                "PythonDriverController tooling requires the frozen DriverScript/scripts "
                "checkout (dev-frozen since v0.8)"
            ) from exc
        generate_python_variant(
            baseline_xosc=scenario_path,
            output_path=variant_path,
            python_script=controller.python.script,
            python_class=controller.python.python_class,
            python_home=controller.python.python_home,
            python_trace=controller.python.trace_enabled,
            python_trace_dir=controller.python.trace_dir or str(output_dir),
        )
        _absolutize_xosc(variant_path, source_dir)
        if param_overrides:
            _apply_param_overrides(variant_path, param_overrides)
        return variant_path
    elif controller.controller_type == "manual":
        variant_path = output_dir / f"{scenario_path.stem}_manual.xosc"
        _generate_manual_variant(scenario_path, variant_path, controller)
        _absolutize_xosc(variant_path, source_dir)
        if param_overrides:
            _apply_param_overrides(variant_path, param_overrides)
        # Write per-run manual_drive.json alongside the variant
        _write_manual_drive_config(output_dir, controller)
        return variant_path
    elif controller.controller_type == "virtual_driver":
        variant_path = output_dir / f"{scenario_path.stem}_virtual_driver.xosc"
        _generate_virtual_driver_variant(scenario_path, variant_path)
        _absolutize_xosc(variant_path, source_dir)
        if param_overrides:
            _apply_param_overrides(variant_path, param_overrides)
        return variant_path
    elif controller.controller_type == "default":
        variant_path = output_dir / f"{scenario_path.stem}_default.xosc"
        variant_path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(scenario_path, variant_path)
        _absolutize_xosc(variant_path, source_dir)
        if param_overrides:
            _apply_param_overrides(variant_path, param_overrides)
        return variant_path
    else:
        return scenario_path


def _build_cmd(
    xosc_path: Path,
    execution: ExecutionConfig,
    output_dir: Path,
    job_id: str | None = None,
    param_overrides: dict[str, str] | None = None,
) -> list[str]:
    """Build GT_Sim command line arguments."""
    cmd = [str(GT_SIM_EXE), "--osc", str(xosc_path)]

    # Pin esmini's file log to this job's results dir (same dir as stdout/stderr
    # capture below) so the real failure cause is preserved per-job instead of
    # overwriting a single cwd/log.txt (audit WEB-2). GT_Sim passes core options
    # through, so --logfile_path is honored.
    cmd.extend(["--logfile_path", str(output_dir / "log.txt")])

    if execution.headless:
        cmd.append("--headless")
    if execution.record:
        cmd.extend(["--record", str(output_dir / "sim.dat")])
    if execution.no_realtime:
        cmd.append("--no_realtime")
    if execution.hz != 100:
        cmd.extend(["--hz", str(execution.hz)])
    if execution.osi.enabled:
        cmd.extend(["--osi", execution.osi.ip])
    if execution.autolight:
        cmd.append("--autolight")
    # F6: environment-driven headlights. --autolight-headlights is self-sufficient
    # in GT_Sim (implies the AutoLight master switch — commit 942c07c0), so it is
    # emitted independently of --autolight.
    if execution.autolight_headlights:
        cmd.append("--autolight-headlights")
    if execution.vehicle_physics:
        cmd.append("--vehicle-physics")
    if execution.kinematic_mode:
        cmd.append("--kinematic-mode")
    if execution.route_drive_mode:
        cmd.append("--route-drive-mode")
        cmd.extend(["--route-drive-timing", execution.route_drive_timing])
        cmd.extend(["--route-drive-gap", execution.route_drive_gap])
    if execution.threads and not execution.headless:
        cmd.append("--threads")
    if not execution.headless:
        w = execution.window
        cmd.extend(["--window", str(w.x), str(w.y), str(w.w), str(w.h)])
    if execution.drive_mode and execution.drive_mode != "comfort":
        cmd.extend(["--drive_mode", execution.drive_mode])
    for arg in execution.extra_args:
        cmd.append(arg)

    # Add parameter overrides
    if param_overrides:
        for name, value in param_overrides.items():
            cmd.extend(["--param", f"{name},{value}"])

    # Add control pipe for runtime speed control (Windows only)
    if job_id and os.name == "nt":
        pipe_name = f"gt_sim_{job_id}"
        cmd.extend(["--control_pipe", pipe_name])
        with _pipes_lock:
            _control_pipes[job_id] = pipe_name

    return cmd


class SimulationConflictError(Exception):
    """Raised when a simulation is already running."""

    def __init__(self, running_job_id: str):
        self.running_job_id = running_job_id
        super().__init__(f"A simulation is already running: {running_job_id}")


async def submit_simulation(req: SimulationRequest, scenario_path: Path) -> str:
    """Register a new simulation job and start execution."""
    # Enforce single-instance: reject if another job is already running
    db = await get_db()
    try:
        cursor = await db.execute(
            "SELECT job_id FROM simulations WHERE status IN ('running', 'queued') LIMIT 1"
        )
        row = await cursor.fetchone()
        if row:
            raise SimulationConflictError(row["job_id"])
    finally:
        await db.close()

    job_id = uuid.uuid4().hex[:12]
    output_dir = _build_output_dir(job_id)

    # Prepare XOSC variant
    xosc_path = _prepare_xosc(
        scenario_path, req.controller, output_dir, req.param_overrides
    )

    # Build command
    cmd = _build_cmd(
        xosc_path,
        req.execution,
        output_dir,
        job_id=job_id,
        param_overrides=req.param_overrides,
    )

    # Store job in DB
    options = {
        "controller": req.controller.model_dump(),
        "execution": req.execution.model_dump(),
        "xosc_path": str(xosc_path),
        "cmd": cmd,
    }

    db = await get_db()
    try:
        await db.execute(
            """INSERT INTO simulations
               (job_id, scenario_id, project_id, status, controller_type, options_json, output_dir, started_at, param_overrides)
               VALUES (?, ?, ?, 'running', ?, ?, ?, ?, ?)""",
            (
                job_id,
                req.scenario_id,
                req.project_id,
                req.controller.controller_type,
                json.dumps(options, ensure_ascii=False),
                str(output_dir),
                _now_iso(),
                (
                    json.dumps(req.param_overrides, ensure_ascii=False)
                    if req.param_overrides
                    else None
                ),
            ),
        )
        await db.commit()
    finally:
        await db.close()

    # Launch GT_Sim asynchronously. VirtualDriver runs are recorded for the
    # verification (VERIFY) panel: the live telemetry stream is teed to
    # results/<job_id>/telemetry.jsonl so the run shows up as a replayable
    # "past run" with the same shape as an offline gt_sim_test recording.
    record_vd = req.controller.controller_type == "virtual_driver"
    record_meta = (
        {
            "scenario": req.scenario_id,
            "scenario_path": str(scenario_path),
            "project_id": req.project_id,
            "scenario_file": req.scenario_id,
        }
        if record_vd
        else None
    )
    # Record the OSI scene (other traffic + signal phases) only when OSI streams.
    record_scene = record_vd and req.execution.osi.enabled

    asyncio.create_task(
        _run_simulation(
            job_id,
            cmd,
            output_dir,
            req.execution.timeout,
            osi_enabled=req.execution.osi.enabled,
            record_vd=record_vd,
            record_meta=record_meta,
            record_scene=record_scene,
        )
    )

    return job_id


_logger = logging.getLogger(__name__)


def _compose_error(cause: str, warnings: list[str]) -> str:
    """Combine the extracted failure cause with collected job warnings (WEB-4)."""
    if not warnings:
        return cause
    return cause + "\n\nWarnings:\n" + "\n".join(f"  {w}" for w in warnings)


def _start_subprocess(cmd: list[str], cwd: str, job_id: str) -> subprocess.Popen:
    """Start subprocess and register in _running_procs atomically (called in thread).

    feature:F7 gate hardening, 2nd round -- this is the ONE function every
    production simulation launch funnels through (the sole call site is
    _run_simulation, via asyncio.to_thread). An audit found that the
    port-occupancy defense built for the verification CLI (gt_sim_test.py)
    never covered this path at all, even though this IS the code that
    actually ran during the 2026-07-27 incident class (a user-facing "Run
    Simulation" launch, not a gate script) -- "the production web-backend
    execution path is undefended" was the literal finding.

    Scope, stated honestly (do not read this as "every port is covered"):
    only DEFAULT_VD_INPUT_PORT (9100, manual-drive/VD network input) is
    checked, unconditionally, regardless of whether THIS run's controller
    actually uses input_type="network" -- narrower context-aware checking
    would need this function to know the resolved controller config, which
    it does not receive today (only the built ``cmd`` argv). 9100 is
    specifically the one port documented as a real, understood collision
    (test_results/f7_audit_port_contention.md P-2: the shipped default in
    BOTH config/manual_drive.json and config/virtual_driver.json, and
    hardcoded in 10 headless verification harnesses that import
    vd_resume_transient) with NO existing defense anywhere in this file.
    48198/48199/48200/48202 are deliberately NOT checked here: this process
    (the web backend) legitimately OWNS those as its own per-job OSI/SV
    bridge listeners (osi_bridge.py/sv_bridge.py, started by _run_simulation
    just before this function runs) for a run that requests them, so
    checking "is 48199 already bound" from inside the very process that
    binds it for itself would be checking a tautology, not a real hazard.
    Port 8000 is this same process's own HTTP listen port -- same tautology.
    A GatePortsBusyError raised here propagates out of _start_subprocess,
    through the asyncio.to_thread() call in _run_simulation, and is caught
    by that function's existing `except Exception` -- the job is marked
    'failed' with this error's message, not silently mis-reported as
    'completed'.
    """
    require_udp_port_free(DEFAULT_VD_INPUT_PORT, "manual-drive / VD network input")
    proc = subprocess.Popen(
        cmd,
        cwd=cwd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    with _running_procs_lock:
        _running_procs[job_id] = proc
    return proc


def _wait_subprocess(proc: subprocess.Popen, timeout: int):
    """Wait for an already-started subprocess to complete (called in thread)."""
    pid = proc.pid
    try:
        stdout, stderr = proc.communicate(timeout=timeout)
        return pid, proc.returncode, stdout, stderr, None
    except subprocess.TimeoutExpired:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
        return pid, -1, b"", b"", f"Process timed out after {timeout}s"


async def _run_simulation(
    job_id: str,
    cmd: list[str],
    output_dir: Path,
    timeout: int,
    osi_enabled: bool = False,
    record_vd: bool = False,
    record_meta: dict | None = None,
    record_scene: bool = False,
) -> None:
    """Execute GT_Sim.exe and update job status on completion."""
    stdout_path = output_dir / "stdout.txt"
    stderr_path = output_dir / "stderr.txt"
    log_path = output_dir / "log.txt"

    # Bridge/recorder failures (audit WEB-4) only reach the server log today; we
    # collect them so a failed job can surface them alongside its cause.
    # TODO(WEB-4): surface these on *successful* jobs too — needs a `warnings`
    # column on the simulations table (no schema change in Phase 1).
    job_warnings: list[str] = []

    # Start OSI bridge before GT_Sim so UDP listener is ready
    if osi_enabled:
        try:
            await start_bridge(job_id)
        except Exception as e:
            _logger.warning("Failed to start OSI bridge for %s: %s", job_id, e)
            job_warnings.append(f"OSI bridge start failed: {e}")

    # Start SV bridge (scenario variables, always enabled when OSI is)
    if osi_enabled:
        try:
            await start_sv_bridge(job_id)
        except Exception as e:
            _logger.warning("Failed to start SV bridge for %s: %s", job_id, e)
            job_warnings.append(f"SV bridge start failed: {e}")

    _logger.info("Launching simulation %s: %s", job_id, " ".join(cmd))
    try:
        # Phase 1: Start the subprocess (registers in _running_procs atomically)
        proc = await asyncio.to_thread(_start_subprocess, cmd, str(REPO_ROOT), job_id)
        pid = proc.pid

        # Tee live VirtualDriver telemetry (and the OSI scene) to file for VERIFY.
        if record_vd:
            try:
                vd_recorder.start(job_id, output_dir, record_scene=record_scene)
            except Exception as e:
                _logger.warning("VD recorder failed to start for %s: %s", job_id, e)
                job_warnings.append(f"VD recorder start failed: {e}")

        # Write PID to DB immediately (while process is still running)
        db = await get_db()
        try:
            await db.execute(
                "UPDATE simulations SET pid = ? WHERE job_id = ?",
                (pid, job_id),
            )
            await db.commit()
        finally:
            await db.close()

        # Phase 2: Wait for completion (blocking, runs in thread)
        pid, exit_code, stdout_data, stderr_data, timeout_msg = await asyncio.to_thread(
            _wait_subprocess,
            proc,
            timeout,
        )

        # Save output
        stdout_path.write_bytes(stdout_data)
        stderr_path.write_bytes(stderr_data)

        if timeout_msg:
            status = "timeout"
            error_msg = timeout_msg
        elif exit_code == 0:
            status = "completed"
            error_msg = None
        else:
            status = "failed"
            # The real cause is in this job's log.txt, not stderr (audit CORE-1);
            # mine it (dedup + noise-ranking) and translate DLL-missing exits.
            extracted = extract_failure(log_path, stdout_path, exit_code=exit_code)
            error_msg = _compose_error(extracted.as_message(), job_warnings)

    except Exception as e:
        status = "failed"
        exit_code = -1
        error_msg = (
            f"{type(e).__name__}: {e}" if str(e) else f"{type(e).__name__} (no message)"
        )
        _logger.error("Simulation %s failed: %s", job_id, error_msg, exc_info=True)
    finally:
        with _running_procs_lock:
            _running_procs.pop(job_id, None)
        if record_vd:
            try:
                await vd_recorder.stop(job_id, record_meta)
            except Exception as e:
                _logger.warning("VD recorder failed to stop for %s: %s", job_id, e)
                if status == "failed" and error_msg:
                    error_msg = _compose_error(
                        error_msg, [f"VD recorder stop failed: {e}"]
                    )

    # Stop OSI bridge
    if osi_enabled:
        await stop_bridge(job_id)
        await stop_sv_bridge(job_id)

    # Clean up control pipe tracking
    with _pipes_lock:
        _control_pipes.pop(job_id, None)

    # Update DB (guard: do not overwrite if already cancelled)
    db = await get_db()
    try:
        cursor = await db.execute(
            """UPDATE simulations
               SET status = ?, exit_code = ?, completed_at = ?, error_message = ?, pid = NULL
               WHERE job_id = ? AND status NOT IN ('cancelled')""",
            (status, exit_code, _now_iso(), error_msg, job_id),
        )
        if cursor.rowcount == 0:
            _logger.info(
                "Simulation %s: skipped update to '%s' (already cancelled)",
                job_id,
                status,
            )
        else:
            _logger.info("Simulation %s finished with status '%s'", job_id, status)
        await db.commit()
    finally:
        await db.close()


async def get_simulation_status(job_id: str) -> SimulationStatus | None:
    """Retrieve job status from DB."""
    db = await get_db()
    try:
        cursor = await db.execute(
            "SELECT * FROM simulations WHERE job_id = ?", (job_id,)
        )
        row = await cursor.fetchone()
        if row is None:
            return None

        options = json.loads(row["options_json"]) if row["options_json"] else {}
        return SimulationStatus(
            job_id=row["job_id"],
            scenario_id=row["scenario_id"],
            project_id=row["project_id"],
            status=row["status"],
            controller_type=row["controller_type"],
            pid=row["pid"],
            exit_code=row["exit_code"],
            output_dir=row["output_dir"],
            started_at=row["started_at"],
            completed_at=row["completed_at"],
            error_message=row["error_message"],
            options=options,
        )
    finally:
        await db.close()


async def list_simulations(
    status: str | None = None,
    project_id: str | None = None,
    limit: int = 20,
    offset: int = 0,
    scenario_id: str | None = None,
) -> tuple[list[SimulationStatus], int]:
    """List simulation jobs with optional filtering."""
    db = await get_db()
    try:
        conditions = []
        params: list = []
        if status:
            conditions.append("status = ?")
            params.append(status)
        if project_id:
            conditions.append("project_id = ?")
            params.append(project_id)
        if scenario_id:
            conditions.append("scenario_id = ?")
            params.append(scenario_id)

        where = f"WHERE {' AND '.join(conditions)}" if conditions else ""

        cursor = await db.execute(f"SELECT COUNT(*) FROM simulations {where}", params)
        total_row = await cursor.fetchone()
        total = total_row[0] if total_row else 0

        cursor = await db.execute(
            f"SELECT * FROM simulations {where} ORDER BY created_at DESC LIMIT ? OFFSET ?",
            params + [limit, offset],
        )
        rows = await cursor.fetchall()

        jobs = []
        for row in rows:
            options = json.loads(row["options_json"]) if row["options_json"] else {}
            jobs.append(
                SimulationStatus(
                    job_id=row["job_id"],
                    scenario_id=row["scenario_id"],
                    project_id=row["project_id"],
                    status=row["status"],
                    controller_type=row["controller_type"],
                    pid=row["pid"],
                    exit_code=row["exit_code"],
                    output_dir=row["output_dir"],
                    started_at=row["started_at"],
                    completed_at=row["completed_at"],
                    error_message=row["error_message"],
                    options=options,
                )
            )
        return jobs, total
    finally:
        await db.close()


async def cancel_simulation(job_id: str) -> bool:
    """Cancel a running simulation."""
    sim = await get_simulation_status(job_id)
    if sim is None or sim.status != "running":
        return False

    # Get Popen object (preferred) or fall back to PID from DB
    proc = None
    with _running_procs_lock:
        proc = _running_procs.get(job_id)

    if proc is not None:
        pid = proc.pid

        # Step 1: Send QUIT via control pipe for graceful shutdown (FFB release)
        pipe_name = None
        with _pipes_lock:
            pipe_name = _control_pipes.get(job_id)
        if pipe_name and sys.platform == "win32":
            try:
                pipe_path = f"\\\\.\\pipe\\{pipe_name}"
                with open(pipe_path, "wb") as pf:
                    pf.write(b"QUIT\n")
                    pf.flush()
                _logger.info(
                    "Sent QUIT to simulation %s via pipe %s", job_id, pipe_name
                )
                # Give GT_Sim time to run GT_Close() and release FFB
                await asyncio.sleep(1.0)
            except OSError as exc:
                _logger.debug("Could not send QUIT via pipe for %s: %s", job_id, exc)

        # Step 2: Force kill if still running
        try:
            proc.kill()
            _logger.info("Killed simulation %s (PID %d) via proc.kill()", job_id, pid)
        except OSError as exc:
            _logger.warning("proc.kill() failed for %s (PID %d): %s", job_id, pid, exc)

        # Step 3: taskkill /T to also kill child process tree (Windows)
        if sys.platform == "win32":
            result = await asyncio.to_thread(
                subprocess.run,
                ["taskkill", "/F", "/T", "/PID", str(pid)],
                capture_output=True,
                text=True,
            )
            if result.returncode != 0:
                _logger.debug(
                    "taskkill tree cleanup for %s (PID %d): %s",
                    job_id,
                    pid,
                    result.stderr.strip(),
                )
    elif sim.pid:
        # Fallback: kill via PID from DB
        pid = sim.pid
        _logger.warning("No Popen for %s, falling back to DB pid %d", job_id, pid)
        try:
            if sys.platform == "win32":
                result = await asyncio.to_thread(
                    subprocess.run,
                    ["taskkill", "/F", "/T", "/PID", str(pid)],
                    capture_output=True,
                    text=True,
                )
                if result.returncode != 0:
                    _logger.warning(
                        "taskkill fallback failed for %s (PID %d): %s",
                        job_id,
                        pid,
                        result.stderr.strip(),
                    )
                else:
                    _logger.info(
                        "Killed simulation %s (PID %d) via taskkill fallback",
                        job_id,
                        pid,
                    )
            else:
                os.kill(pid, signal.SIGTERM)
                _logger.info("Sent SIGTERM to simulation %s (PID %d)", job_id, pid)
        except (OSError, ProcessLookupError) as exc:
            _logger.warning(
                "Failed to kill simulation %s (PID %d): %s", job_id, pid, exc
            )
    else:
        _logger.error("No PID available to kill simulation %s", job_id)

    db = await get_db()
    try:
        await db.execute(
            """UPDATE simulations
               SET status = 'cancelled', completed_at = ?, pid = NULL
               WHERE job_id = ? AND status = 'running'""",
            (_now_iso(), job_id),
        )
        await db.commit()
    finally:
        await db.close()

    return True


def kill_all_running() -> int:
    """Kill all tracked GT_Sim subprocesses. Returns count killed.

    Synchronous function — safe to call from atexit or via asyncio.to_thread.
    """
    killed = 0

    with _running_procs_lock:
        procs = list(_running_procs.items())

    for job_id, proc in procs:
        pid = proc.pid
        try:
            proc.kill()
            _logger.info(
                "Killed GT_Sim subprocess %s (PID %d) via proc.kill()", job_id, pid
            )
            killed += 1
        except OSError as exc:
            _logger.warning("proc.kill() failed for %s (PID %d): %s", job_id, pid, exc)
        # Also kill child process tree on Windows
        if sys.platform == "win32":
            try:
                subprocess.run(
                    ["taskkill", "/F", "/T", "/PID", str(pid)],
                    capture_output=True,
                )
            except OSError:
                pass

    with _running_procs_lock:
        _running_procs.clear()
    return killed


def _send_pipe_command_sync(pipe_name: str, command: bytes) -> bool:
    import ctypes
    import ctypes.wintypes as wintypes

    pipe_path = f"\\\\.\\pipe\\{pipe_name}"

    GENERIC_WRITE = 0x40000000
    OPEN_EXISTING = 3

    kernel32 = ctypes.windll.kernel32
    handle = kernel32.CreateFileW(
        pipe_path,
        GENERIC_WRITE,
        0,
        None,
        OPEN_EXISTING,
        0,
        None,
    )
    if handle == -1:
        return False

    bytes_written = wintypes.DWORD(0)
    success = kernel32.WriteFile(
        handle,
        command,
        len(command),
        ctypes.byref(bytes_written),
        None,
    )
    kernel32.CloseHandle(handle)
    return bool(success)


async def set_speed_factor(job_id: str, factor: float) -> bool:
    """Send speed change command to running GT_Sim via Named Pipe."""
    with _pipes_lock:
        pipe_name = _control_pipes.get(job_id)
    if pipe_name is None:
        return False

    command = f"SPEED:{factor}\n".encode("utf-8")
    return await asyncio.to_thread(_send_pipe_command_sync, pipe_name, command)


async def set_drive_mode(job_id: str, mode: str) -> bool:
    """Send drive mode change command to running GT_Sim via Named Pipe."""
    with _pipes_lock:
        pipe_name = _control_pipes.get(job_id)
    if pipe_name is None:
        return False

    command = f"DRIVE_MODE:{mode}\n".encode("utf-8")
    return await asyncio.to_thread(_send_pipe_command_sync, pipe_name, command)
