"""Tests for the results API HTTP-status translation (api/results.py),
feature:F7 audit #3: zero coverage, "パストラバーサル防止(relative_to->403)、
DatFormatUnsupported->409変換が無検証" specifically named.

result_service business logic is covered in test_result_service.py; here we
isolate the API layer's status-code translation by monkeypatching
simulation_runner.get_simulation_status and result_service, matching the
direct-function-call convention used across this test suite (no TestClient).
"""

from __future__ import annotations

from pathlib import Path

import pytest
from fastapi import HTTPException

from GT_esmini.web.backend.api import results
from GT_esmini.web.backend.models.simulation import SimulationStatus
from GT_esmini.web.backend.services import result_service, simulation_runner


def _sim(**overrides) -> SimulationStatus:
    base = dict(job_id="job-1", scenario_id="scn-1", status="completed", output_dir=None)
    base.update(overrides)
    return SimulationStatus(**base)


def _patch_sim(monkeypatch, sim):
    async def _get(job_id):
        return sim

    monkeypatch.setattr(simulation_runner, "get_simulation_status", _get)


# ---------------------------------------------------------------------------
# 404: unknown job / missing output_dir
# ---------------------------------------------------------------------------


async def test_get_result_meta_404_when_job_unknown(monkeypatch):
    async def _none(job_id):
        return None

    monkeypatch.setattr(simulation_runner, "get_simulation_status", _none)

    with pytest.raises(HTTPException) as exc_info:
        await results.get_result_meta("nope")

    assert exc_info.value.status_code == 404


async def test_get_result_meta_404_when_no_output_dir(monkeypatch):
    _patch_sim(monkeypatch, _sim(output_dir=None))

    with pytest.raises(HTTPException) as exc_info:
        await results.get_result_meta("job-1")

    assert exc_info.value.status_code == 404


async def test_download_file_404_when_file_missing(monkeypatch, tmp_path):
    _patch_sim(monkeypatch, _sim(output_dir=str(tmp_path)))

    with pytest.raises(HTTPException) as exc_info:
        await results.download_file("job-1", "missing.csv")

    assert exc_info.value.status_code == 404


# ---------------------------------------------------------------------------
# 403: path traversal out of output_dir
# ---------------------------------------------------------------------------


async def test_download_file_403_when_path_escapes_output_dir(monkeypatch, tmp_path):
    output_dir = tmp_path / "results" / "job-1"
    output_dir.mkdir(parents=True)
    secret = tmp_path / "secret.txt"
    secret.write_text("do not serve me")

    _patch_sim(monkeypatch, _sim(output_dir=str(output_dir)))

    with pytest.raises(HTTPException) as exc_info:
        await results.download_file("job-1", "../../secret.txt")

    assert exc_info.value.status_code == 403


async def test_download_file_serves_file_within_output_dir(monkeypatch, tmp_path):
    output_dir = tmp_path / "results" / "job-1"
    output_dir.mkdir(parents=True)
    (output_dir / "sim.csv").write_text("time,speed\n0,1\n")

    _patch_sim(monkeypatch, _sim(output_dir=str(output_dir)))

    resp = await results.download_file("job-1", "sim.csv")

    assert Path(resp.path) == output_dir / "sim.csv"


# ---------------------------------------------------------------------------
# metrics: not-yet-completed passthrough, 409 (DatFormatUnsupported), 404
# ---------------------------------------------------------------------------


async def test_get_metrics_returns_status_message_when_not_completed(monkeypatch, tmp_path):
    _patch_sim(monkeypatch, _sim(status="running", output_dir=str(tmp_path)))

    resp = await results.get_metrics("job-1")

    assert resp == {"status": "running", "message": "Simulation not yet completed"}


async def test_get_metrics_404_when_no_output_dir(monkeypatch):
    _patch_sim(monkeypatch, _sim(output_dir=None))

    with pytest.raises(HTTPException) as exc_info:
        await results.get_metrics("job-1")

    assert exc_info.value.status_code == 404


async def test_get_metrics_409_on_dat_format_unsupported(monkeypatch, tmp_path):
    _patch_sim(monkeypatch, _sim(status="completed", output_dir=str(tmp_path)))

    def _raise(output_dir):
        raise result_service.DatFormatUnsupported("sim.dat is DAT format v5")

    monkeypatch.setattr(result_service, "compute_metrics", _raise)

    with pytest.raises(HTTPException) as exc_info:
        await results.get_metrics("job-1")

    assert exc_info.value.status_code == 409


async def test_get_metrics_404_when_no_data(monkeypatch, tmp_path):
    _patch_sim(monkeypatch, _sim(status="completed", output_dir=str(tmp_path)))
    monkeypatch.setattr(result_service, "compute_metrics", lambda output_dir: None)

    with pytest.raises(HTTPException) as exc_info:
        await results.get_metrics("job-1")

    assert exc_info.value.status_code == 404


async def test_get_metrics_returns_computed_metrics(monkeypatch, tmp_path):
    _patch_sim(monkeypatch, _sim(status="completed", output_dir=str(tmp_path)))
    monkeypatch.setattr(
        result_service, "compute_metrics", lambda output_dir: {"summary": {"num_frames": 3}}
    )

    resp = await results.get_metrics("job-1")

    assert resp == {"summary": {"num_frames": 3}}


# ---------------------------------------------------------------------------
# timeseries: 409 (DatFormatUnsupported), field parsing
# ---------------------------------------------------------------------------


async def test_get_timeseries_409_on_dat_format_unsupported(monkeypatch, tmp_path):
    _patch_sim(monkeypatch, _sim(output_dir=str(tmp_path)))

    def _raise(output_dir, fields, entity):
        raise result_service.DatFormatUnsupported("sim.dat is DAT format v5")

    monkeypatch.setattr(result_service, "get_timeseries", _raise)

    with pytest.raises(HTTPException) as exc_info:
        await results.get_timeseries("job-1")

    assert exc_info.value.status_code == 409


async def test_get_timeseries_splits_comma_separated_fields(monkeypatch, tmp_path):
    _patch_sim(monkeypatch, _sim(output_dir=str(tmp_path)))
    captured = {}

    def _fake(output_dir, fields, entity):
        captured["fields"] = fields
        captured["entity"] = entity
        return [{"time": 0.0, "speed": 1.0}]

    monkeypatch.setattr(result_service, "get_timeseries", _fake)

    resp = await results.get_timeseries("job-1", fields="time,speed", entity="Ego")

    assert captured["fields"] == ["time", "speed"]
    assert captured["entity"] == "Ego"
    assert resp.fields == ["time", "speed"]
    assert resp.data == [{"time": 0.0, "speed": 1.0}]


async def test_get_timeseries_404_when_no_output_dir(monkeypatch):
    _patch_sim(monkeypatch, _sim(output_dir=None))

    with pytest.raises(HTTPException) as exc_info:
        await results.get_timeseries("job-1")

    assert exc_info.value.status_code == 404
