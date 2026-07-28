"""Tests for the projects API HTTP-status translation (api/projects.py),
feature:F7 audit "web backend APIの18ファイル中14ファイルがテスト0件" #3:
`api/projects.py`(20エンドポイント) had zero coverage, and the audit named
"413/403/404/409分岐が丸ごと無検証" specifically.

Service-layer business logic (path traversal defense, CRUD, presets YAML) is
covered separately in test_project_service.py. Here we isolate the thin
HTTP-status-code translation this module is responsible for, by monkeypatching
project_service functions -- endpoints are plain async functions, driven
directly (no TestClient / app startup), matching test_manual_drive_api.py's
convention. HTTPException.status_code is real application-raised state (not a
route-decorator artifact), so it is fully observable this way; the few
decorator-only status codes (e.g. status_code=201 on creates) are out of scope
for a direct function call and are not what the audit flagged.
"""

from __future__ import annotations

from pathlib import Path
from types import SimpleNamespace

import pytest
from fastapi import HTTPException

from GT_esmini.web.backend.api import projects
from GT_esmini.web.backend.models.project import (
    PresetCreateRequest,
    PresetUpdateRequest,
    ProjectUpdateRequest,
)
from GT_esmini.web.backend.services import project_service


class _FakeUploadFile:
    def __init__(self, filename: str, data: bytes):
        self.filename = filename
        self._data = data

    async def read(self) -> bytes:
        return self._data


# ---------------------------------------------------------------------------
# project CRUD status codes
# ---------------------------------------------------------------------------


async def test_get_project_404_when_not_found(monkeypatch):
    async def _none(pid):
        return None

    monkeypatch.setattr(project_service, "get_project", _none)

    with pytest.raises(HTTPException) as exc_info:
        await projects.get_project("nope")

    assert exc_info.value.status_code == 404


async def test_update_project_403_when_service_rejects(monkeypatch):
    async def _reject(pid, name, desc):
        return False

    monkeypatch.setattr(project_service, "update_project", _reject)

    with pytest.raises(HTTPException) as exc_info:
        await projects.update_project("builtin", ProjectUpdateRequest(name="x"))

    assert exc_info.value.status_code == 403


async def test_delete_project_403_when_service_rejects(monkeypatch):
    async def _reject(pid):
        return False

    monkeypatch.setattr(project_service, "delete_project", _reject)

    with pytest.raises(HTTPException) as exc_info:
        await projects.delete_project("builtin")

    assert exc_info.value.status_code == 403


# ---------------------------------------------------------------------------
# ZIP upload: 400 (bad extension / bad zip content) + 413 (oversized)
# ---------------------------------------------------------------------------


async def test_upload_project_400_when_filename_not_zip():
    upload = _FakeUploadFile("scenario.xosc", b"not a zip")

    with pytest.raises(HTTPException) as exc_info:
        await projects.upload_project(file=upload, name="X", description="")

    assert exc_info.value.status_code == 400


async def test_upload_project_413_when_too_large():
    oversized = b"x" * (100 * 1024 * 1024 + 1)
    upload = _FakeUploadFile("project.zip", oversized)

    with pytest.raises(HTTPException) as exc_info:
        await projects.upload_project(file=upload, name="X", description="")

    assert exc_info.value.status_code == 413


async def test_upload_project_400_when_zip_content_invalid(monkeypatch):
    async def _raise_value_error(data, name, description=""):
        raise ValueError("Invalid ZIP file")

    monkeypatch.setattr(project_service, "create_project_from_zip", _raise_value_error)
    upload = _FakeUploadFile("project.zip", b"PK\x03\x04garbage")

    with pytest.raises(HTTPException) as exc_info:
        await projects.upload_project(file=upload, name="X", description="")

    assert exc_info.value.status_code == 400


# ---------------------------------------------------------------------------
# file management status codes (path-traversal / builtin rejection surfaces
# as a plain False from the service layer -- see test_project_service.py)
# ---------------------------------------------------------------------------


async def test_list_files_api_404_when_project_missing(monkeypatch):
    async def _none_files(pid):
        return None

    monkeypatch.setattr(project_service, "list_files", _none_files)

    with pytest.raises(HTTPException) as exc_info:
        await projects.list_files("nope")

    assert exc_info.value.status_code == 404


async def test_upload_file_api_403_when_service_rejects(monkeypatch):
    async def _reject(pid, path, data):
        return False

    monkeypatch.setattr(project_service, "upload_file", _reject)
    upload = _FakeUploadFile("a.xosc", b"<A/>")

    with pytest.raises(HTTPException) as exc_info:
        await projects.upload_file("builtin", file=upload, path="a.xosc")

    assert exc_info.value.status_code == 403


async def test_download_file_api_404_when_missing(monkeypatch):
    async def _none_path(pid, path):
        return None

    monkeypatch.setattr(project_service, "get_file_path", _none_path)

    with pytest.raises(HTTPException) as exc_info:
        await projects.download_file("p1", "missing.xosc")

    assert exc_info.value.status_code == 404


async def test_delete_file_api_403_when_service_rejects(monkeypatch):
    async def _reject(pid, path):
        return False

    monkeypatch.setattr(project_service, "delete_file", _reject)

    with pytest.raises(HTTPException) as exc_info:
        await projects.delete_file("builtin", "a.xosc")

    assert exc_info.value.status_code == 403


# ---------------------------------------------------------------------------
# scenarios
# ---------------------------------------------------------------------------


async def test_list_scenarios_api_404_when_project_missing(monkeypatch):
    async def _none_scen(pid):
        return None

    monkeypatch.setattr(project_service, "list_scenarios", _none_scen)

    with pytest.raises(HTTPException) as exc_info:
        await projects.list_scenarios("nope")

    assert exc_info.value.status_code == 404


async def test_get_scenario_params_api_404_when_missing(monkeypatch):
    async def _none_params(pid, scen):
        return None

    monkeypatch.setattr(project_service, "get_scenario_params", _none_params)

    with pytest.raises(HTTPException) as exc_info:
        await projects.get_scenario_params("p1", "missing.xosc")

    assert exc_info.value.status_code == 404


async def test_get_scenario_docs_204_when_no_docs_file(monkeypatch, tmp_path):
    async def _fake_get_project(pid):
        return SimpleNamespace(root_path=str(tmp_path))

    monkeypatch.setattr(project_service, "get_project", _fake_get_project)

    resp = await projects.get_scenario_docs("p1", "scene.xosc")

    assert resp.status_code == 204


async def test_get_scenario_docs_404_when_project_missing(monkeypatch):
    async def _none(pid):
        return None

    monkeypatch.setattr(project_service, "get_project", _none)

    with pytest.raises(HTTPException) as exc_info:
        await projects.get_scenario_docs("nope", "scene.xosc")

    assert exc_info.value.status_code == 404


# ---------------------------------------------------------------------------
# presets: 409 (corrupted file / name conflict) + 404 (unknown preset)
# ---------------------------------------------------------------------------


def _corrupted_error():
    return project_service.PresetFileCorruptedError(Path("x.yaml"), ValueError("bad"))


async def test_list_presets_api_409_on_corrupted_file(monkeypatch):
    async def _raise(pid, scen):
        raise _corrupted_error()

    monkeypatch.setattr(project_service, "list_presets", _raise)

    with pytest.raises(HTTPException) as exc_info:
        await projects.list_presets("p1", "scene.xosc")

    assert exc_info.value.status_code == 409
    assert exc_info.value.detail["code"] == "preset_file_corrupted"


async def test_create_preset_api_409_on_corrupted_file(monkeypatch):
    async def _raise(pid, scen, name, values, description=""):
        raise _corrupted_error()

    monkeypatch.setattr(project_service, "create_preset", _raise)

    with pytest.raises(HTTPException) as exc_info:
        await projects.create_preset(
            "p1", "scene.xosc", PresetCreateRequest(name="x", values={})
        )

    assert exc_info.value.status_code == 409


async def test_update_preset_api_409_on_name_conflict(monkeypatch):
    async def _raise(pid, scen, preset_id, name=None, values=None, description=None):
        raise project_service.PresetNameConflictError("taken")

    monkeypatch.setattr(project_service, "update_preset", _raise)

    with pytest.raises(HTTPException) as exc_info:
        await projects.update_preset(
            "p1", "scene.xosc", "a", PresetUpdateRequest(name="taken")
        )

    assert exc_info.value.status_code == 409
    assert exc_info.value.detail["code"] == "preset_name_conflict"


async def test_update_preset_api_404_when_not_found(monkeypatch):
    async def _false(pid, scen, preset_id, name=None, values=None, description=None):
        return False

    monkeypatch.setattr(project_service, "update_preset", _false)

    with pytest.raises(HTTPException) as exc_info:
        await projects.update_preset(
            "p1", "scene.xosc", "nope", PresetUpdateRequest(name="x")
        )

    assert exc_info.value.status_code == 404


async def test_delete_preset_api_404_when_not_found(monkeypatch):
    async def _false(pid, scen, preset_id):
        return False

    monkeypatch.setattr(project_service, "delete_preset", _false)

    with pytest.raises(HTTPException) as exc_info:
        await projects.delete_preset("p1", "scene.xosc", "nope")

    assert exc_info.value.status_code == 404


async def test_delete_preset_api_409_on_corrupted_file(monkeypatch):
    async def _raise(pid, scen, preset_id):
        raise _corrupted_error()

    monkeypatch.setattr(project_service, "delete_preset", _raise)

    with pytest.raises(HTTPException) as exc_info:
        await projects.delete_preset("p1", "scene.xosc", "a")

    assert exc_info.value.status_code == 409
