"""Simulation execution service: launch GT_Sim.exe and manage jobs."""

from __future__ import annotations

import asyncio
import json
import logging
import os
import shutil
import signal
import subprocess
import threading
import uuid
from datetime import datetime, timezone
from pathlib import Path

import aiosqlite

from GT_esmini.web.backend.config import GT_SIM_EXE, REPO_ROOT, RESULTS_DIR, SCRIPTS_DIR
from GT_esmini.web.backend.db.database import get_db
from GT_esmini.web.backend.models.simulation import (
    ControllerConfig,
    ExecutionConfig,
    SimulationRequest,
    SimulationStatus,
)
from GT_esmini.web.backend.services.osi_bridge import start_bridge, stop_bridge

# Import scenario_generator for XOSC variant generation
import sys
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))
from scenario_generator import generate_python_variant

# Import GTExecutionPlanner for path absolutization
sys.path.insert(0, str(REPO_ROOT / "DriverScript"))
from runtime_api import GTExecutionPlanner

_planner = GTExecutionPlanner()

# In-memory tracking of running GT_Sim subprocess PIDs
_running_pids: set[int] = set()
_running_pids_lock = threading.Lock()

# In-memory tracking of job_id -> PID (available immediately on subprocess start)
_running_jobs: dict[str, int] = {}
_running_jobs_lock = threading.Lock()

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
    _planner.absolutize_scenario_paths(root, source_dir)
    tree.write(variant_path, encoding="utf-8", xml_declaration=True)


def _prepare_xosc(
    scenario_path: Path,
    controller: ControllerConfig,
    output_dir: Path,
) -> Path:
    """Generate XOSC variant based on controller configuration."""
    source_dir = str(scenario_path.parent)

    if controller.controller_type == "python":
        variant_path = output_dir / f"{scenario_path.stem}_python.xosc"
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
        return variant_path
    elif controller.controller_type == "default":
        variant_path = output_dir / f"{scenario_path.stem}_default.xosc"
        # 元のXOSCをそのままコピー（コントローラを削除しない）
        variant_path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(scenario_path, variant_path)
        _absolutize_xosc(variant_path, source_dir)
        return variant_path
    else:
        # Use baseline as-is
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
    if execution.threads and not execution.headless:
        cmd.append("--threads")
    if not execution.headless:
        w = execution.window
        cmd.extend(["--window", str(w.x), str(w.y), str(w.w), str(w.h)])
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


async def submit_simulation(req: SimulationRequest, scenario_path: Path) -> str:
    """Register a new simulation job and start execution."""
    job_id = uuid.uuid4().hex[:12]
    output_dir = _build_output_dir(job_id)

    # Prepare XOSC variant
    xosc_path = _prepare_xosc(scenario_path, req.controller, output_dir)

    # Build command
    cmd = _build_cmd(
        xosc_path, req.execution, output_dir,
        job_id=job_id, param_overrides=req.param_overrides,
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
                json.dumps(req.param_overrides, ensure_ascii=False) if req.param_overrides else None,
            ),
        )
        await db.commit()
    finally:
        await db.close()

    # Launch GT_Sim asynchronously
    asyncio.create_task(
        _run_simulation(
            job_id, cmd, output_dir, req.execution.timeout,
            osi_enabled=req.execution.osi.enabled,
        )
    )

    return job_id


_logger = logging.getLogger(__name__)


def _start_subprocess(cmd: list[str], cwd: str) -> subprocess.Popen:
    """Start subprocess and return immediately (called in thread)."""
    proc = subprocess.Popen(
        cmd,
        cwd=cwd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    with _running_pids_lock:
        _running_pids.add(proc.pid)
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
    finally:
        with _running_pids_lock:
            _running_pids.discard(pid)


async def _run_simulation(
    job_id: str,
    cmd: list[str],
    output_dir: Path,
    timeout: int,
    osi_enabled: bool = False,
) -> None:
    """Execute GT_Sim.exe and update job status on completion."""
    stdout_path = output_dir / "stdout.txt"
    stderr_path = output_dir / "stderr.txt"

    # Start OSI bridge before GT_Sim so UDP listener is ready
    if osi_enabled:
        try:
            await start_bridge(job_id)
        except Exception as e:
            _logger.warning("Failed to start OSI bridge for %s: %s", job_id, e)

    _logger.info("Launching simulation %s: %s", job_id, " ".join(cmd))
    try:
        # Phase 1: Start the subprocess (returns immediately with Popen object)
        proc = await asyncio.to_thread(_start_subprocess, cmd, str(REPO_ROOT))
        pid = proc.pid

        # Register job_id -> PID mapping immediately (for cancel_simulation)
        with _running_jobs_lock:
            _running_jobs[job_id] = pid

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
            _wait_subprocess, proc, timeout,
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
            error_msg = stderr_data.decode("utf-8", errors="replace")[:2000]

    except Exception as e:
        status = "failed"
        exit_code = -1
        error_msg = f"{type(e).__name__}: {e}" if str(e) else f"{type(e).__name__} (no message)"
        _logger.error("Simulation %s failed: %s", job_id, error_msg, exc_info=True)
    finally:
        with _running_jobs_lock:
            _running_jobs.pop(job_id, None)

    # Stop OSI bridge
    if osi_enabled:
        await stop_bridge(job_id)

    # Clean up control pipe tracking
    with _pipes_lock:
        _control_pipes.pop(job_id, None)

    # Update DB
    db = await get_db()
    try:
        await db.execute(
            """UPDATE simulations
               SET status = ?, exit_code = ?, completed_at = ?, error_message = ?, pid = NULL
               WHERE job_id = ?""",
            (status, exit_code, _now_iso(), error_msg, job_id),
        )
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

        cursor = await db.execute(
            f"SELECT COUNT(*) FROM simulations {where}", params
        )
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

    # Get PID: prefer in-memory dict (always available while running) over DB
    pid = None
    with _running_jobs_lock:
        pid = _running_jobs.get(job_id)
    if pid is None:
        pid = sim.pid  # fallback to DB

    if pid:
        try:
            if sys.platform == "win32":
                # Use taskkill to kill the process tree on Windows
                subprocess.run(
                    ["taskkill", "/F", "/T", "/PID", str(pid)],
                    capture_output=True,
                )
            else:
                os.kill(pid, signal.SIGTERM)
        except (OSError, ProcessLookupError):
            pass

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
    with _running_pids_lock:
        pids = list(_running_pids)
    for pid in pids:
        try:
            if sys.platform == "win32":
                subprocess.run(
                    ["taskkill", "/F", "/T", "/PID", str(pid)],
                    capture_output=True,
                )
            else:
                os.kill(pid, signal.SIGTERM)
            killed += 1
            _logger.info("Killed GT_Sim subprocess PID %d", pid)
        except (OSError, ProcessLookupError):
            pass
    with _running_pids_lock:
        _running_pids.clear()
    with _running_jobs_lock:
        _running_jobs.clear()
    return killed


async def set_speed_factor(job_id: str, factor: float) -> bool:
    """Send speed change command to running GT_Sim via Named Pipe."""
    with _pipes_lock:
        pipe_name = _control_pipes.get(job_id)
    if pipe_name is None:
        return False

    def _write_pipe():
        import ctypes
        import ctypes.wintypes as wintypes

        pipe_path = f"\\\\.\\pipe\\{pipe_name}"
        command = f"SPEED:{factor}\n".encode("utf-8")

        GENERIC_WRITE = 0x40000000
        OPEN_EXISTING = 3

        kernel32 = ctypes.windll.kernel32
        handle = kernel32.CreateFileW(
            pipe_path,
            GENERIC_WRITE,
            0,  # no sharing
            None,
            OPEN_EXISTING,
            0,
            None,
        )
        if handle == -1:  # INVALID_HANDLE_VALUE
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

    return await asyncio.to_thread(_write_pipe)
