"""Tests for the simulation launch/cancel/control lifecycle in
services/simulation_runner.py + its SimulationConflictError (feature:F7 audit
"web backend APIの18ファイル中14ファイルがテスト0件" — the top-priority gap:
`api/simulations.py` + `services/simulation_runner.py` (1178 lines, GT_Sim.exe
launch lifecycle本体) had ZERO dedicated tests; only the per-run config writers
(_write_virtual_driver_config, _write_manual_drive_config) and the port-guard
call site were covered elsewhere.

Covers, with no real GT_Sim.exe / subprocess / named pipe involved
(subprocess.Popen, subprocess.run, and the module's own I/O helpers are
monkeypatched):
  - _build_cmd: the argv GT_Sim.exe actually receives, across the flag matrix
  - _generate_manual_variant / _apply_param_overrides: XOSC variant generation
  - SimulationConflictError + submit_simulation's single-instance conflict gate
  - submit_simulation happy path: DB row + scheduled _run_simulation
  - cancel_simulation: live-Popen kill path, PID-fallback path, not-running no-op
  - set_speed_factor / set_drive_mode: named-pipe command wire-up
"""

from __future__ import annotations

import asyncio
import subprocess
import xml.etree.ElementTree as ET
from pathlib import Path

import pytest

from GT_esmini.web.backend.db import database
from GT_esmini.web.backend.models.simulation import (
    ControllerConfig,
    ExecutionConfig,
    OsiConfig,
    SimulationRequest,
)
from GT_esmini.web.backend.services import annotation_store, simulation_runner as runner

_MINIMAL_XOSC = """<?xml version="1.0" encoding="UTF-8"?>
<OpenSCENARIO>
  <Entities>
    <ScenarioObject name="Ego">
      <Vehicle name="car" vehicleCategory="car"/>
      <ObjectController>
        <Controller name="OldController"/>
      </ObjectController>
    </ScenarioObject>
  </Entities>
  <Storyboard>
    <Init>
      <Actions>
        <Private entityRef="Ego">
          <PrivateAction>
            <ActivateControllerAction longitudinal="false" lateral="false"/>
          </PrivateAction>
        </Private>
      </Actions>
    </Init>
  </Storyboard>
</OpenSCENARIO>
"""

_NO_ENTITY_XOSC = """<?xml version="1.0" encoding="UTF-8"?>
<OpenSCENARIO>
  <Entities/>
</OpenSCENARIO>
"""

_PARAM_XOSC = """<?xml version="1.0" encoding="UTF-8"?>
<OpenSCENARIO>
  <ParameterDeclarations>
    <ParameterDeclaration name="speed" parameterType="double" value="10"/>
    <ParameterDeclaration name="untouched" parameterType="string" value="keep"/>
  </ParameterDeclarations>
</OpenSCENARIO>
"""

_NO_PARAMS_XOSC = """<?xml version="1.0" encoding="UTF-8"?>
<OpenSCENARIO>
  <Entities/>
</OpenSCENARIO>
"""


@pytest.fixture(autouse=True)
def _clean_module_state():
    """_running_procs / _control_pipes are module-level dicts mutated by the
    functions under test; scrub them before and after every test so tests
    cannot leak state into each other."""
    runner._running_procs.clear()
    runner._control_pipes.clear()
    yield
    runner._running_procs.clear()
    runner._control_pipes.clear()


class _FakeProc:
    def __init__(self, pid=4242):
        self.pid = pid
        self.killed = False

    def kill(self):
        self.killed = True


# ---------------------------------------------------------------------------
# _build_cmd
# ---------------------------------------------------------------------------


def _exec_config(**overrides) -> ExecutionConfig:
    return ExecutionConfig(**overrides)


def test_build_cmd_minimal_defaults(tmp_path):
    xosc = tmp_path / "scn.xosc"
    cmd = runner._build_cmd(xosc, _exec_config(), tmp_path)
    assert cmd[0] == str(runner.GT_SIM_EXE)
    assert "--osc" in cmd and str(xosc) in cmd
    assert "--logfile_path" in cmd and str(tmp_path / "log.txt") in cmd
    # defaults: not headless -> --window present; hz==100 -> no --hz;
    # record False -> no --record; drive_mode=="comfort" -> no --drive_mode
    assert "--headless" not in cmd
    assert "--hz" not in cmd
    assert "--record" not in cmd
    assert "--drive_mode" not in cmd
    assert "--window" in cmd


def test_build_cmd_headless_suppresses_window_flag(tmp_path):
    cmd = runner._build_cmd(tmp_path / "scn.xosc", _exec_config(headless=True), tmp_path)
    assert "--headless" in cmd
    assert "--window" not in cmd


def test_build_cmd_threads_only_added_when_not_headless(tmp_path):
    windowed = runner._build_cmd(
        tmp_path / "scn.xosc", _exec_config(threads=True, headless=False), tmp_path
    )
    headless = runner._build_cmd(
        tmp_path / "scn.xosc", _exec_config(threads=True, headless=True), tmp_path
    )
    assert "--threads" in windowed
    assert "--threads" not in headless


def test_build_cmd_record_writes_sim_dat_under_output_dir(tmp_path):
    cmd = runner._build_cmd(tmp_path / "scn.xosc", _exec_config(record=True), tmp_path)
    assert "--record" in cmd
    assert str(tmp_path / "sim.dat") in cmd


def test_build_cmd_hz_only_emitted_when_non_default(tmp_path):
    cmd = runner._build_cmd(tmp_path / "scn.xosc", _exec_config(hz=50), tmp_path)
    i = cmd.index("--hz")
    assert cmd[i + 1] == "50"


def test_build_cmd_osi_enabled_passes_ip(tmp_path):
    cmd = runner._build_cmd(
        tmp_path / "scn.xosc",
        _exec_config(osi=OsiConfig(enabled=True, ip="239.0.0.1")),
        tmp_path,
    )
    i = cmd.index("--osi")
    assert cmd[i + 1] == "239.0.0.1"


def test_build_cmd_osi_disabled_omits_flag(tmp_path):
    cmd = runner._build_cmd(tmp_path / "scn.xosc", _exec_config(), tmp_path)
    assert "--osi" not in cmd


def test_build_cmd_autolight_flags_are_independent(tmp_path):
    cmd = runner._build_cmd(
        tmp_path / "scn.xosc",
        _exec_config(autolight=False, autolight_headlights=True),
        tmp_path,
    )
    assert "--autolight" not in cmd
    assert "--autolight-headlights" in cmd


def test_build_cmd_route_drive_mode_adds_timing_and_gap(tmp_path):
    cmd = runner._build_cmd(
        tmp_path / "scn.xosc",
        _exec_config(route_drive_mode=True, route_drive_timing="early", route_drive_gap="tight"),
        tmp_path,
    )
    assert "--route-drive-mode" in cmd
    ti = cmd.index("--route-drive-timing")
    assert cmd[ti + 1] == "early"
    gi = cmd.index("--route-drive-gap")
    assert cmd[gi + 1] == "tight"


def test_build_cmd_drive_mode_sport_is_emitted_comfort_is_not(tmp_path):
    comfort = runner._build_cmd(tmp_path / "scn.xosc", _exec_config(drive_mode="comfort"), tmp_path)
    sport = runner._build_cmd(tmp_path / "scn.xosc", _exec_config(drive_mode="sport"), tmp_path)
    assert "--drive_mode" not in comfort
    si = sport.index("--drive_mode")
    assert sport[si + 1] == "sport"


def test_build_cmd_extra_args_appended_verbatim(tmp_path):
    cmd = runner._build_cmd(
        tmp_path / "scn.xosc", _exec_config(extra_args=["--foo", "bar"]), tmp_path
    )
    assert cmd[-2:] == ["--foo", "bar"]


def test_build_cmd_param_overrides_encoded_as_name_comma_value(tmp_path):
    cmd = runner._build_cmd(
        tmp_path / "scn.xosc",
        _exec_config(),
        tmp_path,
        param_overrides={"speed": "12.5"},
    )
    i = cmd.index("--param")
    assert cmd[i + 1] == "speed,12.5"


def test_build_cmd_registers_control_pipe_when_job_id_given(tmp_path):
    cmd = runner._build_cmd(tmp_path / "scn.xosc", _exec_config(), tmp_path, job_id="job-abc")
    assert "--control_pipe" in cmd
    i = cmd.index("--control_pipe")
    assert cmd[i + 1] == "gt_sim_job-abc"
    assert runner._control_pipes.get("job-abc") == "gt_sim_job-abc"


def test_build_cmd_no_control_pipe_without_job_id(tmp_path):
    cmd = runner._build_cmd(tmp_path / "scn.xosc", _exec_config(), tmp_path)
    assert "--control_pipe" not in cmd


# ---------------------------------------------------------------------------
# _generate_manual_variant
# ---------------------------------------------------------------------------


def test_generate_manual_variant_replaces_controller_and_activates_both_domains(tmp_path):
    baseline = tmp_path / "base.xosc"
    baseline.write_text(_MINIMAL_XOSC, encoding="utf-8")
    out = tmp_path / "run" / "base_manual.xosc"

    runner._generate_manual_variant(baseline, out, ControllerConfig())

    root = ET.parse(out).getroot()
    entity = root.find(".//ScenarioObject")
    oc = entity.find("ObjectController")
    assert oc is not None
    ctrl = oc.find("Controller")
    assert ctrl.get("name") == "ManualDriveController"

    props = {
        p.get("name"): p.get("value") for p in ctrl.findall("Properties/Property")
    }
    assert props["esminiController"] == "ManualDriveController"
    assert Path(props["ConfigFile"]).name == "manual_drive.json"
    assert Path(props["ConfigFile"]).is_absolute()

    # old controller gone, new one inserted right after <Vehicle>
    children = list(entity)
    assert children[0].tag == "Vehicle"
    assert children[1] is oc
    assert all(c.get("name") != "OldController" for c in ctrl.iter("Controller"))

    act = root.find(".//Init/Actions/Private/PrivateAction/ActivateControllerAction")
    assert act.get("longitudinal") == "true"
    assert act.get("lateral") == "true"
    # exactly one PrivateAction survives under this Private (the stale
    # false/false one must have been removed, not left alongside the new one)
    private = root.find(".//Init/Actions/Private")
    assert len(private.findall("PrivateAction")) == 1


def test_generate_manual_variant_falls_back_to_plain_copy_when_no_entities(tmp_path):
    baseline = tmp_path / "base.xosc"
    baseline.write_text(_NO_ENTITY_XOSC, encoding="utf-8")
    out = tmp_path / "base_manual.xosc"

    runner._generate_manual_variant(baseline, out, ControllerConfig())

    assert out.read_text(encoding="utf-8") == _NO_ENTITY_XOSC


# ---------------------------------------------------------------------------
# _apply_param_overrides
# ---------------------------------------------------------------------------


def test_apply_param_overrides_updates_only_named_matches(tmp_path):
    xosc = tmp_path / "p.xosc"
    xosc.write_text(_PARAM_XOSC, encoding="utf-8")

    runner._apply_param_overrides(xosc, {"speed": "42"})

    root = ET.parse(xosc).getroot()
    decls = {
        d.get("name"): d.get("value")
        for d in root.findall("ParameterDeclarations/ParameterDeclaration")
    }
    assert decls["speed"] == "42"
    assert decls["untouched"] == "keep"  # not in overrides -> unchanged


def test_apply_param_overrides_is_a_noop_without_parameter_declarations(tmp_path):
    xosc = tmp_path / "noparams.xosc"
    xosc.write_text(_NO_PARAMS_XOSC, encoding="utf-8")

    runner._apply_param_overrides(xosc, {"speed": "42"})  # must not raise

    assert xosc.read_text(encoding="utf-8") == _NO_PARAMS_XOSC


# ---------------------------------------------------------------------------
# SimulationConflictError + submit_simulation
# ---------------------------------------------------------------------------


def test_simulation_conflict_error_carries_running_job_id():
    err = runner.SimulationConflictError("job-999")
    assert err.running_job_id == "job-999"
    assert "job-999" in str(err)
    assert "already running" in str(err)


@pytest.fixture
def tmp_db(monkeypatch, tmp_path):
    """Redirect DB_PATH to a per-test file and disable the best-effort
    verification-registry warm scan (it walks the real results dir on disk --
    see test_database.py's identical fixture)."""
    db_path = tmp_path / "data" / "gt_sim.db"
    monkeypatch.setattr(database, "DB_PATH", db_path)

    async def _noop_scan(force: bool = False):
        return None

    monkeypatch.setattr(annotation_store, "scan_registry", _noop_scan)
    return db_path


async def test_submit_simulation_raises_conflict_when_a_job_is_already_running(
    tmp_db, tmp_path, monkeypatch
):
    await database.init_db()
    db = await database.get_db()
    try:
        await db.execute(
            "INSERT INTO simulations (job_id, scenario_id, status) VALUES (?, 's0', 'running')",
            ("existing-job",),
        )
        await db.commit()
    finally:
        await db.close()

    def _must_not_run(*a, **kw):
        raise AssertionError("_prepare_xosc must not run when a job is already active")

    monkeypatch.setattr(runner, "_prepare_xosc", _must_not_run)

    req = SimulationRequest(scenario_id="scn-new")
    with pytest.raises(runner.SimulationConflictError) as exc_info:
        await runner.submit_simulation(req, tmp_path / "scn.xosc")
    assert exc_info.value.running_job_id == "existing-job"


async def test_submit_simulation_conflict_also_triggers_on_queued_status(
    tmp_db, tmp_path, monkeypatch
):
    await database.init_db()
    db = await database.get_db()
    try:
        await db.execute(
            "INSERT INTO simulations (job_id, scenario_id, status) VALUES (?, 's0', 'queued')",
            ("queued-job",),
        )
        await db.commit()
    finally:
        await db.close()

    req = SimulationRequest(scenario_id="scn-new")
    with pytest.raises(runner.SimulationConflictError) as exc_info:
        await runner.submit_simulation(req, tmp_path / "scn.xosc")
    assert exc_info.value.running_job_id == "queued-job"


async def test_submit_simulation_happy_path_inserts_running_row_and_schedules_run(
    tmp_db, tmp_path, monkeypatch
):
    await database.init_db()

    scheduled = {}

    async def _fake_run_simulation(job_id, cmd, output_dir, timeout, **kwargs):
        scheduled["job_id"] = job_id
        scheduled["cmd"] = cmd

    monkeypatch.setattr(runner, "_run_simulation", _fake_run_simulation)
    monkeypatch.setattr(runner, "RESULTS_DIR", tmp_path / "results")

    scenario_path = tmp_path / "scenario.xosc"
    scenario_path.write_text(_NO_ENTITY_XOSC, encoding="utf-8")

    req = SimulationRequest(
        scenario_id="scn-1",
        controller=ControllerConfig(controller_type="default"),
        execution=ExecutionConfig(headless=True),
    )
    job_id = await runner.submit_simulation(req, scenario_path)

    assert isinstance(job_id, str) and len(job_id) == 12

    # submit_simulation schedules _run_simulation via asyncio.create_task and
    # returns immediately; yield once so the scheduled task actually runs.
    await asyncio.sleep(0)
    assert scheduled["job_id"] == job_id
    assert str(runner.GT_SIM_EXE) in scheduled["cmd"]

    db = await database.get_db()
    try:
        cur = await db.execute(
            "SELECT status, scenario_id FROM simulations WHERE job_id = ?", (job_id,)
        )
        row = await cur.fetchone()
        assert row["status"] == "running"
        assert row["scenario_id"] == "scn-1"
    finally:
        await db.close()


# ---------------------------------------------------------------------------
# cancel_simulation
# ---------------------------------------------------------------------------


async def test_cancel_simulation_returns_false_when_job_unknown(tmp_db):
    await database.init_db()
    assert await runner.cancel_simulation("does-not-exist") is False


async def test_cancel_simulation_returns_false_when_not_running(tmp_db):
    await database.init_db()
    db = await database.get_db()
    try:
        await db.execute(
            "INSERT INTO simulations (job_id, scenario_id, status) VALUES ('done-job', 's', 'completed')"
        )
        await db.commit()
    finally:
        await db.close()
    assert await runner.cancel_simulation("done-job") is False


async def test_cancel_simulation_kills_live_popen_and_marks_cancelled(
    tmp_db, monkeypatch
):
    await database.init_db()
    db = await database.get_db()
    try:
        await db.execute(
            "INSERT INTO simulations (job_id, scenario_id, status, pid) "
            "VALUES ('live-job', 's', 'running', 4242)"
        )
        await db.commit()
    finally:
        await db.close()

    fake_proc = _FakeProc(pid=4242)
    runner._running_procs["live-job"] = fake_proc

    taskkill_calls = []

    def _fake_run(cmd, **kwargs):
        taskkill_calls.append(cmd)

        class _Result:
            returncode = 0
            stderr = ""

        return _Result()

    monkeypatch.setattr(subprocess, "run", _fake_run)

    result = await runner.cancel_simulation("live-job")

    assert result is True
    assert fake_proc.killed is True
    assert taskkill_calls and taskkill_calls[0][:4] == ["taskkill", "/F", "/T", "/PID"]
    assert taskkill_calls[0][4] == "4242"

    db = await database.get_db()
    try:
        cur = await db.execute("SELECT status, pid FROM simulations WHERE job_id='live-job'")
        row = await cur.fetchone()
        assert row["status"] == "cancelled"
        assert row["pid"] is None
    finally:
        await db.close()


async def test_cancel_simulation_falls_back_to_db_pid_when_no_popen_registered(
    tmp_db, monkeypatch
):
    """No Popen in _running_procs (e.g. server restarted) -- must still kill
    via the PID persisted in the DB, not silently no-op."""
    await database.init_db()
    db = await database.get_db()
    try:
        await db.execute(
            "INSERT INTO simulations (job_id, scenario_id, status, pid) "
            "VALUES ('orphan-job', 's', 'running', 7777)"
        )
        await db.commit()
    finally:
        await db.close()

    taskkill_calls = []

    def _fake_run(cmd, **kwargs):
        taskkill_calls.append(cmd)

        class _Result:
            returncode = 0
            stderr = ""

        return _Result()

    monkeypatch.setattr(subprocess, "run", _fake_run)

    result = await runner.cancel_simulation("orphan-job")

    assert result is True
    assert taskkill_calls and taskkill_calls[0][4] == "7777"


# ---------------------------------------------------------------------------
# set_speed_factor / set_drive_mode
# ---------------------------------------------------------------------------


async def test_set_speed_factor_returns_false_without_a_registered_pipe(monkeypatch):
    def _must_not_be_called(pipe_name, command):
        raise AssertionError("must not send a pipe command with no pipe registered")

    monkeypatch.setattr(runner, "_send_pipe_command_sync", _must_not_be_called)
    assert await runner.set_speed_factor("no-such-job", 1.5) is False


async def test_set_speed_factor_sends_correctly_formatted_command(monkeypatch):
    runner._control_pipes["job-x"] = "gt_sim_job-x"
    sent = {}

    def _fake_send(pipe_name, command):
        sent["pipe_name"] = pipe_name
        sent["command"] = command
        return True

    monkeypatch.setattr(runner, "_send_pipe_command_sync", _fake_send)

    ok = await runner.set_speed_factor("job-x", 1.5)

    assert ok is True
    assert sent["pipe_name"] == "gt_sim_job-x"
    assert sent["command"] == b"SPEED:1.5\n"


async def test_set_drive_mode_returns_false_without_a_registered_pipe(monkeypatch):
    def _must_not_be_called(pipe_name, command):
        raise AssertionError("must not send a pipe command with no pipe registered")

    monkeypatch.setattr(runner, "_send_pipe_command_sync", _must_not_be_called)
    assert await runner.set_drive_mode("no-such-job", "sport") is False


async def test_set_drive_mode_sends_correctly_formatted_command(monkeypatch):
    runner._control_pipes["job-y"] = "gt_sim_job-y"
    sent = {}

    def _fake_send(pipe_name, command):
        sent["pipe_name"] = pipe_name
        sent["command"] = command
        return True

    monkeypatch.setattr(runner, "_send_pipe_command_sync", _fake_send)

    ok = await runner.set_drive_mode("job-y", "sport")

    assert ok is True
    assert sent["pipe_name"] == "gt_sim_job-y"
    assert sent["command"] == b"DRIVE_MODE:sport\n"
