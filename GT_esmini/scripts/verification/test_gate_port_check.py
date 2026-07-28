"""feature:F7 gate hardening -- port-occupancy check, centralized at the
common path that actually binds/sends (run()/batch() in gt_sim_test.py), not
just run_regression_gate.ps1's own preflight.

The audited hole: run_regression_gate.ps1's Test-GatePortsFree only protects
invocations that go through the .ps1 itself. CI's workflow step calls
`python .../gt_sim_test.py batch ...` directly; a skill or an ad-hoc terminal
run does the same. None of those paths ever executed the .ps1 preflight, so
none of them were protected -- even though "the user leaves the packaged
GT_Sim.exe running while re-running a gate" is the NORMAL case here, not an
edge case to design around.

This file exercises check_gate_ports_free()/_require_gate_ports_free()
directly against disposable high ports (never the real 48198-and-friends
range), so it needs no build, no DLL, and cannot collide with anything real
on the machine running it.

Run:
  DriverScript/.venv/Scripts/python.exe -m pytest \
      GT_esmini/scripts/verification/test_gate_port_check.py -v
"""

from __future__ import annotations

import socket

import pytest

import gt_sim_test as gst

# Disposable ports for this test file only -- never gt_sim_test's real
# _GATE_UDP_PORTS/_GATE_TCP_LISTEN_PORTS range. Picked in the high dynamic
# range to avoid any well-known service.
_TEST_UDP_PORT = 51823
_TEST_TCP_PORT = 51824


def test_all_clear_when_nothing_is_using_the_test_ports():
    problems = gst.check_gate_ports_free(
        udp_ports={_TEST_UDP_PORT: ("test udp", "collision")},
        tcp_listen_ports={_TEST_TCP_PORT: ("test tcp", "collision")},
    )
    assert problems == []


def test_udp_collision_is_detected_when_the_port_is_already_bound():
    holder = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    holder.bind(("0.0.0.0", _TEST_UDP_PORT))
    try:
        problems = gst.check_gate_ports_free(
            udp_ports={_TEST_UDP_PORT: ("test udp", "collision")},
            tcp_listen_ports={},
        )
        assert len(problems) == 1
        assert str(_TEST_UDP_PORT) in problems[0]
        assert "collision" in problems[0]
    finally:
        holder.close()


def test_udp_contamination_port_is_also_flagged_when_bound():
    """Same mechanism, different label -- contamination ports are UDP too
    (something already owns the address), the distinction is only in the
    human-facing reason string."""
    holder = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    holder.bind(("0.0.0.0", _TEST_UDP_PORT))
    try:
        problems = gst.check_gate_ports_free(
            udp_ports={_TEST_UDP_PORT: ("test contamination target", "contamination")},
            tcp_listen_ports={},
        )
        assert len(problems) == 1
        assert "contamination" in problems[0]
    finally:
        holder.close()


def test_tcp_listen_collision_is_detected_when_something_is_listening():
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.bind(("127.0.0.1", _TEST_TCP_PORT))
    server.listen(1)
    try:
        problems = gst.check_gate_ports_free(
            udp_ports={},
            tcp_listen_ports={_TEST_TCP_PORT: ("test tcp", "collision")},
        )
        assert len(problems) == 1
        assert str(_TEST_TCP_PORT) in problems[0]
    finally:
        server.close()


def test_tcp_port_free_when_nothing_is_listening():
    # No listener bound -- connect() should fail/refuse, meaning "free".
    problems = gst.check_gate_ports_free(
        udp_ports={},
        tcp_listen_ports={_TEST_TCP_PORT: ("test tcp", "collision")},
    )
    assert problems == []


def test_require_raises_gate_ports_busy_error_with_all_problems_listed(monkeypatch):
    monkeypatch.setattr(
        gst._vd,
        "check_gate_ports_free",
        lambda: ["port 1 (a) [collision] already in use (UDP)",
                 "port 2 (b) [contamination] already in use (UDP)"],
    )
    with pytest.raises(gst.GatePortsBusyError) as exc_info:
        gst._require_gate_ports_free()
    msg = str(exc_info.value)
    assert "port 1" in msg
    assert "port 2" in msg


def test_require_is_a_noop_when_all_clear(monkeypatch):
    # feature:F7 2nd hardening round: check_gate_ports_free now lives in
    # vd_metrics.py (gst._vd); require_gate_ports_free() (aliased here as
    # gst._require_gate_ports_free) is ALSO defined there and resolves
    # check_gate_ports_free through vd_metrics's OWN module globals, not
    # gt_sim_test's -- monkeypatching gst.check_gate_ports_free (the
    # re-exported alias) does not affect that internal lookup. Patch the
    # real source instead.
    monkeypatch.setattr(gst._vd, "check_gate_ports_free", lambda: [])
    gst._require_gate_ports_free()  # must not raise


def test_run_refuses_to_start_when_a_port_is_busy(tmp_path, monkeypatch):
    """Wiring test: run() -- the single common choke point every invocation
    path (CI direct, a skill, batch()'s own per-scenario loop, an ad-hoc
    terminal run) funnels through -- must refuse BEFORE touching the DLL.
    GtLib is never imported/constructed here; if the check did not run
    first, this test would instead fail trying to load a (possibly absent)
    DLL, not with GatePortsBusyError."""
    monkeypatch.setattr(
        gst._vd, "check_gate_ports_free", lambda: ["port 9999 (x) [collision] already in use (UDP)"]
    )
    with pytest.raises(gst.GatePortsBusyError):
        gst.run(
            tmp_path / "does_not_matter.xosc",
            tmp_path / "out",
            0.05,
            1.0,
            1,
            None,
        )


def test_batch_refuses_to_start_when_a_port_is_busy_before_any_scenario_runs(
    tmp_path, monkeypatch
):
    """batch()'s own early check (outside the per-scenario try/except) must
    abort with ONE GatePortsBusyError, not run() being invoked at all (which
    would instead surface as a per-scenario 'error' -- still a FAIL overall,
    but noisier and not what this test pins)."""
    monkeypatch.setattr(
        gst._vd, "check_gate_ports_free", lambda: ["port 9999 (x) [collision] already in use (UDP)"]
    )

    def _must_not_be_called(*_args, **_kwargs):
        raise AssertionError("run() must not be reached when the early port check fails")

    monkeypatch.setattr(gst, "run", _must_not_be_called)

    manifest = tmp_path / "manifest.yaml"
    manifest.write_text(
        "name: port_probe\nscenarios:\n  - scenario: does/not/matter.xosc\n",
        encoding="utf-8",
    )

    with pytest.raises(gst.GatePortsBusyError):
        gst.batch(manifest, tmp_path / "out")
