"""Tests for config_api.py's GET endpoints + set_projects_root's error branch
(feature:F7 audit #3: "PUT側は深く検証済み(test_config_merge_api.py)。対応する
GETがほぼ0件。set_projects_rootの400分岐・sync_projects連鎖も無検証" --
「PUTは通るがGETで読み戻せるか」を検証していないという round3(c)の逆パターン).

Sandboxing follows test_config_merge_api.py's `store` fixture: patch the
module-level load/save helpers config_api.py actually calls (never touch the
real settings.json / vehicle params / thresholds files).
"""

from __future__ import annotations

import copy
from pathlib import Path
from typing import Any

import pytest
from fastapi import HTTPException

from GT_esmini.web.backend import config as config_module
from GT_esmini.web.backend.api import config_api
from GT_esmini.web.backend.config import DEFAULT_EXECUTION_PARAMS
from GT_esmini.web.backend.services import project_service


@pytest.fixture
def store(monkeypatch) -> dict[str, Any]:
    state: dict[str, Any] = {
        "settings": {"execution_defaults": copy.deepcopy(DEFAULT_EXECUTION_PARAMS)},
        "vehicle": {"mass": 1450, "drag": 0.0013},
        "thresholds": {"lat_err": 0.5},
    }
    monkeypatch.setattr(
        config_api, "load_settings", lambda: copy.deepcopy(state["settings"])
    )
    monkeypatch.setattr(
        config_api,
        "save_settings",
        lambda s: state.__setitem__("settings", copy.deepcopy(s)),
    )
    monkeypatch.setattr(
        config_api, "load_vehicle_params", lambda: dict(state["vehicle"])
    )
    monkeypatch.setattr(
        config_api, "load_thresholds", lambda: dict(state["thresholds"])
    )

    async def _noop_sync():
        state["sync_called"] = state.get("sync_called", 0) + 1

    monkeypatch.setattr(project_service, "sync_projects", _noop_sync)
    return state


# ---------------------------------------------------------------------------
# GET /execution-defaults
# ---------------------------------------------------------------------------


async def test_get_execution_defaults_returns_stored_values(store):
    result = await config_api.get_execution_defaults()
    assert result["hz"] == DEFAULT_EXECUTION_PARAMS["hz"]


async def test_get_execution_defaults_migrates_legacy_array_window(store):
    store["settings"]["execution_defaults"] = {
        **copy.deepcopy(DEFAULT_EXECUTION_PARAMS),
        "window": [10, 20, 800, 600],
    }

    result = await config_api.get_execution_defaults()

    assert result["window"] == {"x": 10, "y": 20, "w": 800, "h": 600}


async def test_get_execution_defaults_falls_back_to_module_default_when_unset(store):
    store["settings"] = {}
    result = await config_api.get_execution_defaults()
    assert result["hz"] == DEFAULT_EXECUTION_PARAMS["hz"]


# ---------------------------------------------------------------------------
# GET /vehicle-params, GET /thresholds
# ---------------------------------------------------------------------------


async def test_get_vehicle_params_returns_stored_values(store):
    result = await config_api.get_vehicle_params()
    assert result == {"mass": 1450, "drag": 0.0013}


async def test_get_thresholds_returns_stored_values(store):
    result = await config_api.get_thresholds()
    assert result == {"lat_err": 0.5}


# ---------------------------------------------------------------------------
# GET /projects-root + PUT /projects-root (400 branch + sync trigger)
# ---------------------------------------------------------------------------


async def test_get_projects_root_default_is_not_custom(store):
    result = await config_api.get_projects_root()
    assert result["is_custom"] is False
    assert result["projects_root"] is None


async def test_get_projects_root_reports_custom_when_set(store, tmp_path):
    store["settings"]["projects_root"] = str(tmp_path)
    result = await config_api.get_projects_root()
    assert result["is_custom"] is True
    assert result["projects_root"] == str(tmp_path)


async def test_set_projects_root_400_when_directory_does_not_exist(store):
    with pytest.raises(HTTPException) as exc_info:
        await config_api.set_projects_root(
            {"projects_root": "Z:/does/not/exist/at/all"}
        )

    assert exc_info.value.status_code == 400


async def test_set_projects_root_success_stores_resolved_absolute_path(store, tmp_path):
    result = await config_api.set_projects_root({"projects_root": str(tmp_path)})

    assert Path(result["projects_root"]) == tmp_path.resolve()
    assert store["settings"]["projects_root"] == str(tmp_path.resolve())


async def test_set_projects_root_none_clears_the_setting(store, tmp_path):
    store["settings"]["projects_root"] = str(tmp_path)

    result = await config_api.set_projects_root({"projects_root": None})

    assert result["projects_root"] is None
    assert "projects_root" not in store["settings"]


async def test_set_projects_root_triggers_sync_projects(store, tmp_path):
    await config_api.set_projects_root({"projects_root": str(tmp_path)})
    assert store.get("sync_called") == 1


async def test_set_projects_root_rejects_before_syncing(store):
    """A rejected (400) path change must not still trigger a sync against the
    old/invalid state."""
    with pytest.raises(HTTPException):
        await config_api.set_projects_root({"projects_root": "Z:/nope"})
    assert store.get("sync_called") is None


# ---------------------------------------------------------------------------
# GET /system
# ---------------------------------------------------------------------------


async def test_get_system_info_reports_expected_shape():
    info = await config_api.get_system_info()

    assert isinstance(info["gt_sim_exists"], bool)
    assert isinstance(info["scenarios_count"], int)
    assert info["gt_sim_path"] == str(config_module.GT_SIM_EXE)
    assert info["sv_multicast_port"] == config_module.SV_MULTICAST_PORT
