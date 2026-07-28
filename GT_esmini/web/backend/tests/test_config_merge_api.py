"""Tests for the settings/config PUT endpoints (config_api.py, controller_config.py).

feature:F7 gap #1 / #7. All four of these endpoints used to REPLACE the stored
object with the request body:

    settings["execution_defaults"] = params      # config_api
    save_vehicle_params(params)                  # config_api
    save_thresholds(data)                        # config_api
    settings["controller_config"] = config       # controller_config

A client that renders only some of the fields therefore deleted every key it
did not send. That is not hypothetical: SettingsPanel.tsx's handleSave() builds
its payload from the fields it renders, and ``route_drive_timing`` /
``route_drive_gap`` are not among them -- so saving anything at all in the
settings panel silently reset them to "normal" on the next run (gap #1).

Two DIFFERENT merge rules are correct here and the tests pin both:
  * execution-defaults / vehicle-params / thresholds -> FLAT merge. Their
    nested blocks (``osi``, ``window``) are owned wholesale by the panel, which
    always sends every sub-field; merging into them would make shrinking them
    impossible.
  * controller-config -> ONE LEVEL deep. Its sub-objects (``python``) are
    partial by nature; a PUT may legitimately touch only ``python.script``.

There were no tests over any of these endpoints before. Sandboxing follows
test_manual_drive_api.py: patch the module-level helpers the endpoints
actually call, and assert the patch took, so the real settings.json is never
written (an earlier verification script patched a CONSTANT instead of the
function that resolves the path, and overwrote the user's real file).
"""

from __future__ import annotations

import asyncio
import copy
from typing import Any

import pytest

from GT_esmini.web.backend.config import (
    DEFAULT_CONTROLLER_CONFIG,
    DEFAULT_EXECUTION_PARAMS,
)
from GT_esmini.web.backend.api import config_api, controller_config


def _run(coro):
    return asyncio.run(coro)


@pytest.fixture
def store(monkeypatch) -> dict[str, Any]:
    """In-memory stand-in for the persisted files."""
    state: dict[str, Any] = {
        "settings": {
            "execution_defaults": copy.deepcopy(DEFAULT_EXECUTION_PARAMS),
            "controller_config": copy.deepcopy(DEFAULT_CONTROLLER_CONFIG),
        },
        "vehicle": {"mass": 1450, "drag": 0.0013, "max_acc": 4.0},
        "thresholds": {"lat_err": 0.5, "lon_err": 1.0, "heading_err": 0.05},
    }
    for mod in (config_api, controller_config):
        monkeypatch.setattr(mod, "load_settings", lambda: copy.deepcopy(state["settings"]))
        monkeypatch.setattr(
            mod, "save_settings", lambda s: state.__setitem__("settings", copy.deepcopy(s))
        )
    monkeypatch.setattr(config_api, "load_vehicle_params", lambda: dict(state["vehicle"]))
    monkeypatch.setattr(
        config_api, "save_vehicle_params", lambda d: state.__setitem__("vehicle", dict(d))
    )
    monkeypatch.setattr(config_api, "load_thresholds", lambda: dict(state["thresholds"]))
    monkeypatch.setattr(
        config_api, "save_thresholds", lambda d: state.__setitem__("thresholds", dict(d))
    )
    return state


# --- execution-defaults (gap #1) -------------------------------------------


def test_execution_defaults_keeps_keys_the_client_never_sends(store):
    """The exact shape SettingsPanel.tsx sends: no route_drive_timing/gap."""
    _run(config_api.update_execution_defaults({"hz": 90, "headless": True}))
    ed = store["settings"]["execution_defaults"]
    assert ed["route_drive_timing"] == "normal"
    assert ed["route_drive_gap"] == "normal"
    assert ed["hz"] == 90
    assert ed["headless"] is True


def test_execution_defaults_survives_repeated_saves(store):
    for _ in range(3):
        _run(config_api.update_execution_defaults({"hz": 90}))
    ed = store["settings"]["execution_defaults"]
    assert ed["route_drive_timing"] == "normal"
    assert ed["route_drive_gap"] == "normal"


def test_execution_defaults_returns_what_was_stored(store):
    """Response must equal the persisted state, not the request body."""
    returned = _run(config_api.update_execution_defaults({"hz": 90}))
    assert returned == store["settings"]["execution_defaults"]
    assert "route_drive_timing" in returned


def test_execution_defaults_does_not_alias_module_default(store):
    """Fallback path must not hand out the module constant's sub-dicts."""
    store["settings"] = {}  # nothing stored yet
    before = copy.deepcopy(DEFAULT_EXECUTION_PARAMS)
    _run(config_api.update_execution_defaults({"osi": {"enabled": False, "ip": "10.0.0.1"}}))
    assert DEFAULT_EXECUTION_PARAMS == before, "module default was mutated"


# --- vehicle-params / thresholds (gap #7) ----------------------------------


def test_vehicle_params_partial_put_keeps_the_rest(store):
    _run(config_api.update_vehicle_params({"mass": 1600}))
    assert store["vehicle"] == {"mass": 1600, "drag": 0.0013, "max_acc": 4.0}


def test_thresholds_partial_put_keeps_the_rest(store):
    returned = _run(config_api.update_thresholds({"lat_err": 0.25}))
    assert store["thresholds"]["lat_err"] == 0.25
    assert store["thresholds"]["heading_err"] == 0.05
    assert returned == store["thresholds"]


# --- controller-config (gap #7, one-level merge) ---------------------------


def test_controller_config_partial_put_keeps_sibling_sections(store):
    _run(controller_config.update_current_config({"controller_type": "manual"}))
    cc = store["settings"]["controller_config"]
    assert cc["controller_type"] == "manual"
    assert isinstance(cc.get("python"), dict)
    assert cc["python"]["script"] == DEFAULT_CONTROLLER_CONFIG["python"]["script"]


def test_controller_config_merges_one_level_into_sub_objects(store):
    _run(controller_config.update_current_config({"python": {"script": "b.py"}}))
    py = store["settings"]["controller_config"]["python"]
    assert py["script"] == "b.py"
    # Sibling keys inside the same sub-object survive a partial sub-object PUT.
    assert py["class"] == DEFAULT_CONTROLLER_CONFIG["python"]["class"]


def test_controller_config_does_not_alias_module_default(store):
    store["settings"] = {}
    before = copy.deepcopy(DEFAULT_CONTROLLER_CONFIG)
    _run(controller_config.update_current_config({"python": {"script": "x.py"}}))
    assert DEFAULT_CONTROLLER_CONFIG == before, "module default was mutated"
