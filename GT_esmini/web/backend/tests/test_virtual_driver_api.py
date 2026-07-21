"""Tests for the VirtualDriver runtime config API (api/virtual_driver_api.py,
GitHub issue #33).

The endpoints read/write ``CONFIG_DIR / virtual_driver.json``. We monkeypatch the
module-level ``CONFIG_DIR`` to a tmp dir so the working tree is never touched,
mirroring test_auto_light_api's sandbox approach. Endpoints are plain async
functions, driven directly via ``asyncio.run`` (no TestClient / app startup)."""

from __future__ import annotations

import asyncio
import json
from pathlib import Path

import pytest
from fastapi import HTTPException

from GT_esmini.web.backend.api import virtual_driver_api


def _run(coro):
    return asyncio.run(coro)


@pytest.fixture()
def sandbox(monkeypatch, tmp_path) -> Path:
    """Point the API at a tmp config dir; return the virtual_driver.json path."""
    monkeypatch.setattr(virtual_driver_api, "CONFIG_DIR", tmp_path)
    return tmp_path / virtual_driver_api.VIRTUAL_DRIVER_CONFIG_FILE


def test_get_config_missing_returns_defaults(sandbox):
    cfg = _run(virtual_driver_api.get_config())
    assert cfg == virtual_driver_api.DEFAULT_VIRTUAL_DRIVER_CONFIG
    # Fallback must not have created the file.
    assert not sandbox.exists()


def test_get_defaults_returns_shipping_values(sandbox):
    d = _run(virtual_driver_api.get_defaults())
    assert d == virtual_driver_api.DEFAULT_VIRTUAL_DRIVER_CONFIG
    assert d["policy_lead_enabled"] is False
    assert d["idm_desired_speed"] == 50.0
    # Returns a copy — mutating it must not corrupt the module constant.
    d["policy_lead_enabled"] = True
    assert (
        virtual_driver_api.DEFAULT_VIRTUAL_DRIVER_CONFIG["policy_lead_enabled"] is False
    )


def test_put_roundtrips_policy_toggle_and_numeric_param(sandbox):
    patch = {"policy_lead_enabled": True, "idm_desired_speed": 30}
    written = _run(virtual_driver_api.update_config(patch))
    assert written["policy_lead_enabled"] is True
    assert written["idm_desired_speed"] == 30.0  # int coerced to float

    reread = _run(virtual_driver_api.get_config())
    assert reread["policy_lead_enabled"] is True
    assert reread["idm_desired_speed"] == 30.0
    # Untouched known keys keep their default values.
    assert reread["idm_time_headway"] == 1.5


def test_put_rejects_unknown_key(sandbox):
    with pytest.raises(HTTPException) as exc:
        _run(virtual_driver_api.update_config({"policy_evil_enabled": True}))
    assert exc.value.status_code == 422
    assert not sandbox.exists()  # nothing persisted on rejection


def test_put_rejects_bool_key_given_number(sandbox):
    with pytest.raises(HTTPException) as exc:
        _run(virtual_driver_api.update_config({"policy_lead_enabled": 1}))
    assert exc.value.status_code == 422


def test_put_rejects_number_key_given_bool(sandbox):
    with pytest.raises(HTTPException) as exc:
        _run(virtual_driver_api.update_config({"idm_desired_speed": True}))
    assert exc.value.status_code == 422


def test_put_rejects_invalid_enum_value(sandbox):
    with pytest.raises(HTTPException) as exc:
        _run(virtual_driver_api.update_config({"override_lateral": "auto"}))
    assert exc.value.status_code == 422


def test_put_accepts_valid_enum_values(sandbox):
    written = _run(
        virtual_driver_api.update_config(
            {
                "override_lateral": "scenario",
                "override_longitudinal": "manual",
            }
        )
    )
    assert written["override_lateral"] == "scenario"
    assert written["override_longitudinal"] == "manual"


def test_put_preserves_comment_keys(sandbox):
    _run(virtual_driver_api.update_config({"policy_lead_enabled": True}))
    on_disk = json.loads(sandbox.read_text(encoding="utf-8"))
    for k in (
        "_comment",
        "_planner",
        "_midlong",
        "_driver",
        "_control_point",
        "_indicator",
        "_policies",
        "_policy_lead",
        "_override",
        "_input",
    ):
        assert k in on_disk, f"comment key {k} was dropped"
    assert on_disk["policy_lead_enabled"] is True


def test_put_ignores_incoming_comment_keys(sandbox):
    # A client-supplied comment key must not be written verbatim; server keeps its own.
    _run(
        virtual_driver_api.update_config(
            {"_comment": "injected", "policy_lead_enabled": True}
        )
    )
    on_disk = json.loads(sandbox.read_text(encoding="utf-8"))
    assert (
        on_disk["_comment"]
        == virtual_driver_api.DEFAULT_VIRTUAL_DRIVER_CONFIG["_comment"]
    )


def test_get_defaults_matches_factory_dict(sandbox):
    d = _run(virtual_driver_api.get_defaults())
    assert d == virtual_driver_api.DEFAULT_VIRTUAL_DRIVER_CONFIG


def test_put_rejects_excluded_runner_owned_key(sandbox):
    # input_type / input_port / input_transport / vehicle_params_file are owned by
    # _write_virtual_driver_config (simulation_runner.py) — a GUI edit must not
    # fight the per-run writer.
    with pytest.raises(HTTPException) as exc:
        _run(virtual_driver_api.update_config({"input_type": "network"}))
    assert exc.value.status_code == 422
    assert not sandbox.exists()


def test_put_reads_existing_file_as_base(sandbox):
    # Pre-seed a file that differs from defaults; a partial PUT must keep the rest,
    # including the runner-owned keys untouched by this endpoint.
    sandbox.parent.mkdir(parents=True, exist_ok=True)
    seed = dict(virtual_driver_api.DEFAULT_VIRTUAL_DRIVER_CONFIG)
    seed["idm_min_gap"] = 4.2
    seed["input_type"] = "network"
    sandbox.write_text(json.dumps(seed, indent=4), encoding="utf-8")

    _run(virtual_driver_api.update_config({"policy_lead_enabled": True}))
    reread = _run(virtual_driver_api.get_config())
    assert reread["policy_lead_enabled"] is True
    assert reread["idm_min_gap"] == 4.2  # preserved from base
    assert reread["input_type"] == "network"  # runner-owned key untouched
