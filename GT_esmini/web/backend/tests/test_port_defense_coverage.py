"""feature:F7 gate hardening, 2nd round -- mechanical enumeration of every
real entry point that binds a socket or spawns GT_Sim.exe, proving each one
is actually guarded by the port-occupancy check (vd_metrics.require_udp_port_free
/ require_gate_ports_free), not merely documented as guarded.

Why this file exists: the 1st hardening round (commit b3938b83) protected
gt_sim_test.py's run()/batch() and claimed that was "the common path every
launch route passes through". An audit found that claim false -- the actual
2026-07-27 incident party, services/vd_verify.py's generate_baseline() (a
production web-backend code path), never went near gt_sim_test.py and had no
defense at all. This file is the fix for that CLASS of mistake, not just the
specific instance: it lists every known socket-binding / subprocess-spawning
site and asserts, mechanically, that the guard runs first at each one -- so
the next entry point added without a guard fails a test here instead of
waiting for the next audit.

Known entry points (keep this list in sync with reality; a new one added
without a matching test here is exactly the gap this file exists to catch):
  1. gt_sim_test.py run()   -> covered by test_gate_port_check.py
  2. gt_sim_test.py batch() -> covered by test_gate_port_check.py
  3. vd_metrics.capture_osi()            -> covered below
  4. simulation_runner._start_subprocess() -> covered below
     (services/vd_verify.py's generate_baseline() calls straight into
     capture_osi() with no bind of its own, so #3 covers it transitively --
     see capture_osi's docstring.)
"""

from __future__ import annotations

import socket
import subprocess
from pathlib import Path

import pytest

from GT_esmini.web.backend.services import simulation_runner, vd_metrics


def test_capture_osi_calls_the_guard_before_binding(monkeypatch, tmp_path):
    calls: list[str] = []

    def _fake_require(port, what):
        calls.append("require")

    def _fake_bind(self, addr):
        calls.append("bind")

    monkeypatch.setattr(vd_metrics, "require_udp_port_free", _fake_require)
    monkeypatch.setattr(socket.socket, "bind", _fake_bind)

    class _FakeProc:
        def poll(self):
            return 0  # already exited

    vd_metrics.capture_osi(tmp_path / "out.osi", _FakeProc(), 48198, idle_timeout=0.0)

    assert calls[0] == "require", "require_udp_port_free must run BEFORE sock.bind()"
    assert "bind" in calls


def test_capture_osi_refuses_when_the_guard_raises(monkeypatch, tmp_path):
    """The guard's failure must actually stop capture_osi, not just be
    called-and-ignored."""

    def _always_busy(port, what):
        raise vd_metrics.GatePortsBusyError(f"port {port} busy (test)")

    monkeypatch.setattr(vd_metrics, "require_udp_port_free", _always_busy)

    class _FakeProc:
        def poll(self):
            return 0

    with pytest.raises(vd_metrics.GatePortsBusyError):
        vd_metrics.capture_osi(tmp_path / "out.osi", _FakeProc(), 48198, idle_timeout=0.0)


def test_start_subprocess_calls_the_guard_before_popen(monkeypatch):
    calls: list[str] = []

    def _fake_require(port, what):
        calls.append(f"require:{port}")

    class _FakeProc:
        pid = 12345

    def _fake_popen(cmd, **kwargs):
        calls.append("popen")
        return _FakeProc()

    monkeypatch.setattr(simulation_runner, "require_udp_port_free", _fake_require)
    monkeypatch.setattr(subprocess, "Popen", _fake_popen)

    simulation_runner._start_subprocess(["GT_Sim.exe"], ".", "job-1")

    assert calls[0] == f"require:{simulation_runner.DEFAULT_VD_INPUT_PORT}", (
        "require_udp_port_free must run BEFORE subprocess.Popen()"
    )
    assert "popen" in calls
    # Clean up: _start_subprocess registers the fake proc in module state.
    with simulation_runner._running_procs_lock:
        simulation_runner._running_procs.pop("job-1", None)


def test_start_subprocess_refuses_when_the_guard_raises(monkeypatch):
    def _always_busy(port, what):
        raise vd_metrics.GatePortsBusyError(f"port {port} busy (test)")

    monkeypatch.setattr(simulation_runner, "require_udp_port_free", _always_busy)

    def _must_not_be_called(cmd, **kwargs):
        raise AssertionError("subprocess.Popen must not run when the guard refuses")

    monkeypatch.setattr(subprocess, "Popen", _must_not_be_called)

    with pytest.raises(vd_metrics.GatePortsBusyError):
        simulation_runner._start_subprocess(["GT_Sim.exe"], ".", "job-2")


def test_generate_baseline_has_no_bind_of_its_own_between_popen_and_capture_osi():
    """Documents (mechanically, via source inspection) why vd_verify.py needs
    no guard call of its own: it launches the subprocess then calls straight
    into capture_osi(), which is fully covered above. If this ever stops
    being true (e.g. a future edit adds a direct sock.bind() in vd_verify.py
    itself, bypassing capture_osi), this test's source-level check catches
    the drift without needing a live subprocess."""
    from GT_esmini.web.backend.services import vd_verify

    src = Path(vd_verify.__file__).read_text(encoding="utf-8")
    assert "capture_osi(" in src, "generate_baseline must still route OSI capture through vd_metrics.capture_osi"
    assert ".bind(" not in src, (
        "vd_verify.py must not bind a socket directly -- that would bypass "
        "capture_osi()'s guard and need its own require_udp_port_free() call"
    )
