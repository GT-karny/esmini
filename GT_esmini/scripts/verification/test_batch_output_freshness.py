"""feature:F7 gate hardening -- batch() output-directory freshness.

Proves the mechanical guarantee _reset_batch_output_dir() gives: a stale
batch_verdict.json left over from a PREVIOUS invocation can never survive to
be misread as THIS run's result -- even if THIS run is interrupted before it
would otherwise have written anything (a native crash inside the DLL, the
process getting killed, Ctrl+C mid-batch). check_regression_baseline.py /
run_regression_gate.ps1 read batch_verdict.json purely by "does it exist and
parse"; neither can tell a fresh result from yesterday's leftover green.

Companion to run_regression_gate.ps1's port preflight (commit 8b006cff),
which closed the sibling "0 scenarios ran but the gate still printed PASS"
hole. That fix covers a process that keeps running long enough to record a
per-scenario error. This file covers the other half: a process that dies
before it ever gets that far.

Entirely offline -- no build, no DLL, no GT_Sim required. gt_sim_test.run()
is monkeypatched at the exact call site batch() uses, so the DLL is never
touched.

Run:
  DriverScript/.venv/Scripts/python.exe -m pytest \
      GT_esmini/scripts/verification/test_batch_output_freshness.py -v
"""

from __future__ import annotations

import json
from pathlib import Path

import pytest

import gt_sim_test as gst


@pytest.fixture(autouse=True)
def _no_real_port_checks(monkeypatch):
    """This file tests output-directory freshness, not port availability
    (that is test_gate_port_check.py's job) -- batch() now also calls
    _require_gate_ports_free() (feature:F7 gate hardening, port-check
    centralization), which would otherwise make these tests depend on
    whatever happens to be listening on the real 48198-and-friends range on
    the machine running them. Neutralize it here so a busy port on the test
    runner cannot make a freshness test fail for an unrelated reason.

    Patches gst._vd (vd_metrics.py, the real home of the check as of the
    2nd gate-hardening round) rather than the gst.check_gate_ports_free
    alias -- _require_gate_ports_free() is ALSO defined in vd_metrics.py and
    resolves check_gate_ports_free through THAT module's own globals, not
    gt_sim_test's, so patching the alias here would silently not apply."""
    monkeypatch.setattr(gst._vd, "check_gate_ports_free", lambda: [])


def _seed_stale_verdict(out_root: Path) -> None:
    """Plant exactly the kind of artifact a real previous green run would
    leave: a plausible overall=pass batch_verdict.json, its companion
    summary, and a per-scenario run_dir with its own verdict.json."""
    out_root.mkdir(parents=True, exist_ok=True)
    (out_root / "batch_verdict.json").write_text(
        json.dumps(
            {
                "name": "stale-from-a-previous-run",
                "overall": "pass",
                "summary": {"pass": 99, "fail": 0, "needs-review": 0, "error": 0},
                "scenarios": [],
            }
        ),
        encoding="utf-8",
    )
    (out_root / "batch_summary.md").write_text("stale\n", encoding="utf-8")
    stale_scenario_dir = out_root / "some_scenario"
    stale_scenario_dir.mkdir(parents=True, exist_ok=True)
    (stale_scenario_dir / "verdict.json").write_text(
        json.dumps({"overall": "pass"}), encoding="utf-8"
    )


def test_reset_removes_stale_verdict_and_run_dirs(tmp_path):
    out_root = tmp_path / "out"
    _seed_stale_verdict(out_root)
    assert (out_root / "batch_verdict.json").exists()
    assert (out_root / "some_scenario" / "verdict.json").exists()

    gst._reset_batch_output_dir(out_root)

    assert out_root.exists()
    assert list(out_root.iterdir()) == []


def test_reset_is_a_noop_on_a_directory_that_does_not_exist_yet(tmp_path):
    out_root = tmp_path / "never_created"
    assert not out_root.exists()
    gst._reset_batch_output_dir(out_root)
    assert out_root.exists()
    assert list(out_root.iterdir()) == []


class _SimulatedProcessDeath(BaseException):
    """Deliberately NOT an Exception subclass. batch()'s per-scenario loop
    only does `except Exception`, so this reaches the caller exactly like a
    real interpreter-ending crash would (a native access violation in the
    DLL, SIGKILL, Ctrl+C) -- none of which are catchable Python Exceptions
    either. This is the failure mode _reset_batch_output_dir exists for."""


def test_stale_pass_verdict_does_not_survive_a_crash_mid_batch(tmp_path, monkeypatch):
    """The claim this file exists to prove: seed a stale, plausible PASS
    verdict; run a batch() that dies before writing anything; confirm
    nothing readable as a result is left where the stale PASS used to be."""
    out_root = tmp_path / "out"
    _seed_stale_verdict(out_root)

    manifest = tmp_path / "manifest.yaml"
    manifest.write_text(
        "name: crash_probe\nscenarios:\n  - scenario: does/not/matter.xosc\n",
        encoding="utf-8",
    )

    def _boom(*_args, **_kwargs):
        raise _SimulatedProcessDeath("simulated native crash before any write")

    monkeypatch.setattr(gst, "run", _boom)

    with pytest.raises(_SimulatedProcessDeath):
        gst.batch(manifest, out_root)

    # The crash happened before batch() could write a fresh verdict -- but
    # the stale one must be gone regardless, not merely "not yet updated".
    assert not (out_root / "batch_verdict.json").exists()
    assert not (out_root / "batch_summary.md").exists()
    assert not (out_root / "some_scenario").exists()


def test_a_caught_per_scenario_error_still_produces_a_fresh_non_stale_verdict(
    tmp_path, monkeypatch
):
    """Contrast case: a per-scenario failure that IS caught (the ordinary,
    already-hardened-since-8b006cff path) must also produce a genuinely
    fresh batch_verdict.json -- proving the new freshness guarantee composes
    with the existing error-accounting hardening rather than only covering
    the crash case."""
    out_root = tmp_path / "out"
    _seed_stale_verdict(out_root)

    manifest = tmp_path / "manifest.yaml"
    manifest.write_text(
        "name: caught_error_probe\nscenarios:\n  - scenario: does/not/matter.xosc\n",
        encoding="utf-8",
    )

    def _fails_normally(*_args, **_kwargs):
        raise RuntimeError("GT_InitWithArgs failed (rc=1): simulated")

    monkeypatch.setattr(gst, "run", _fails_normally)

    agg = gst.batch(manifest, out_root)

    assert agg["overall"] == "fail"
    assert agg["summary"]["error"] == 1
    assert agg["name"] == "caught_error_probe"
    on_disk = json.loads((out_root / "batch_verdict.json").read_text(encoding="utf-8"))
    assert on_disk["overall"] == "fail"
    assert on_disk["summary"]["pass"] != 99  # not the stale sentinel value
    assert on_disk["name"] == "caught_error_probe"  # not "stale-from-a-previous-run"
