"""Simulation execution service: launch GT_Sim.exe and manage jobs."""

from __future__ import annotations

import asyncio
import json
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

# Import scenario_generator for XOSC variant generation
import sys
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))
from scenario_generator import generate_default_variant, generate_python_variant

# Import GTExecutionPlanner for path absolutization
sys.path.insert(0, str(REPO_ROOT / "DriverScript"))
from runtime_api import GTExecutionPlanner

_planner = GTExecutionPlanner()


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
        generate_default_variant(
            baseline_xosc=scenario_path,
            output_path=variant_path,
        )
        _absolutize_xosc(variant_path, source_dir)
        return variant_path
    else:
        # Use baseline as-is
        return scenario_path


def _build_cmd(
    xosc_path: Path,
    execution: ExecutionConfig,
    output_dir: Path,
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
    if not execution.headless:
        w = execution.window
        cmd.extend(["--window", str(w.x), str(w.y), str(w.w), str(w.h)])
    for arg in execution.extra_args:
        cmd.append(arg)

    return cmd


async def submit_simulation(req: SimulationRequest, scenario_path: Path) -> str:
    """Register a new simulation job and start execution."""
    job_id = uuid.uuid4().hex[:12]
    output_dir = _build_output_dir(job_id)

    # Prepare XOSC variant
    xosc_path = _prepare_xosc(scenario_path, req.controller, output_dir)

    # Build command
    cmd = _build_cmd(xosc_path, req.execution, output_dir)

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
               (job_id, scenario_id, status, controller_type, options_json, output_dir, started_at)
               VALUES (?, ?, 'running', ?, ?, ?, ?)""",
            (
                job_id,
                req.scenario_id,
                req.controller.controller_type,
                json.dumps(options, ensure_ascii=False),
                str(output_dir),
                _now_iso(),
            ),
        )
        await db.commit()
    finally:
        await db.close()

    # Launch GT_Sim asynchronously
    asyncio.create_task(_run_simulation(job_id, cmd, output_dir, req.execution.timeout))

    return job_id


async def _run_simulation(
    job_id: str,
    cmd: list[str],
    output_dir: Path,
    timeout: int,
) -> None:
    """Execute GT_Sim.exe and update job status on completion."""
    stdout_path = output_dir / "stdout.txt"
    stderr_path = output_dir / "stderr.txt"

    try:
        process = await asyncio.create_subprocess_exec(
            *cmd,
            cwd=str(REPO_ROOT),
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )

        # Update PID
        db = await get_db()
        try:
            await db.execute(
                "UPDATE simulations SET pid = ? WHERE job_id = ?",
                (process.pid, job_id),
            )
            await db.commit()
        finally:
            await db.close()

        # Wait for completion with timeout
        try:
            stdout_data, stderr_data = await asyncio.wait_for(
                process.communicate(), timeout=timeout
            )
            exit_code = process.returncode

            # Save output
            stdout_path.write_bytes(stdout_data)
            stderr_path.write_bytes(stderr_data)

            status = "completed" if exit_code == 0 else "failed"
            error_msg = None
            if exit_code != 0:
                error_msg = stderr_data.decode("utf-8", errors="replace")[:2000]

        except asyncio.TimeoutError:
            process.terminate()
            try:
                await asyncio.wait_for(process.wait(), timeout=5)
            except asyncio.TimeoutError:
                process.kill()
            status = "timeout"
            exit_code = -1
            error_msg = f"Process timed out after {timeout}s"

    except Exception as e:
        status = "failed"
        exit_code = -1
        error_msg = str(e)

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
    status: str | None = None, limit: int = 20, offset: int = 0
) -> tuple[list[SimulationStatus], int]:
    """List simulation jobs with optional filtering."""
    db = await get_db()
    try:
        where = ""
        params: list = []
        if status:
            where = "WHERE status = ?"
            params.append(status)

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
    import os
    import signal

    sim = await get_simulation_status(job_id)
    if sim is None or sim.status != "running":
        return False

    if sim.pid:
        try:
            os.kill(sim.pid, signal.SIGTERM)
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
