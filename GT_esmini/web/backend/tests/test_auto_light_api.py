"""Tests for the AutoLight (F6) config API (api/auto_light_api.py).

The endpoints read/write ``CONFIG_DIR / auto_light.json``. We monkeypatch the
module-level ``CONFIG_DIR`` to a tmp dir so the working tree is never touched,
mirroring test_config_thresholds' sandbox approach. Endpoints are plain async
functions, driven directly via ``asyncio.run`` (no TestClient / app startup)."""

from __future__ import annotations

import asyncio
import json
from pathlib import Path

import pytest
from fastapi import HTTPException

from GT_esmini.web.backend.api import auto_light_api


def _run(coro):
    return asyncio.run(coro)


@pytest.fixture()
def sandbox(monkeypatch, tmp_path) -> Path:
    """Point the API at a tmp config dir; return the auto_light.json path."""
    monkeypatch.setattr(auto_light_api, "CONFIG_DIR", tmp_path)
    return tmp_path / auto_light_api.AUTO_LIGHT_CONFIG_FILE


def test_get_config_missing_returns_defaults(sandbox):
    cfg = _run(auto_light_api.get_config())
    assert cfg == auto_light_api.DEFAULT_AUTO_LIGHT_CONFIG
    # Fallback must not have created the file.
    assert not sandbox.exists()


def test_get_defaults_returns_shipping_values(sandbox):
    d = _run(auto_light_api.get_defaults())
    assert d == auto_light_api.DEFAULT_AUTO_LIGHT_CONFIG
    assert d["headlight_enabled"] is False
    assert d["highbeam_range_m"] == 120.0
    # Returns a copy — mutating it must not corrupt the module constant.
    d["headlight_enabled"] = True
    assert auto_light_api.DEFAULT_AUTO_LIGHT_CONFIG["headlight_enabled"] is False


def test_put_then_get_roundtrip(sandbox):
    patch = {"headlight_enabled": True, "highbeam_range_m": 90}
    written = _run(auto_light_api.update_config(patch))
    assert written["headlight_enabled"] is True
    assert written["highbeam_range_m"] == 90.0  # int coerced to float

    reread = _run(auto_light_api.get_config())
    assert reread["headlight_enabled"] is True
    assert reread["highbeam_range_m"] == 90.0
    # Untouched known keys keep their default values.
    assert reread["highbeam_range_hysteresis_m"] == 20.0


def test_put_preserves_comment_keys(sandbox):
    _run(auto_light_api.update_config({"headlight_enabled": True}))
    on_disk = json.loads(sandbox.read_text(encoding="utf-8"))
    for k in ("// F6", "// safety", "// enable", "// night", "// tunnel", "// highbeam"):
        assert k in on_disk, f"comment key {k} was dropped"
    assert on_disk["headlight_enabled"] is True


def test_put_ignores_incoming_comment_keys(sandbox):
    # A client-supplied comment key must not be written verbatim; server keeps its own.
    _run(auto_light_api.update_config({"// F6": "injected", "headlight_enabled": True}))
    on_disk = json.loads(sandbox.read_text(encoding="utf-8"))
    assert on_disk["// F6"] == auto_light_api.DEFAULT_AUTO_LIGHT_CONFIG["// F6"]


def test_put_rejects_unknown_key(sandbox):
    with pytest.raises(HTTPException) as exc:
        _run(auto_light_api.update_config({"headlight_evil": 1}))
    assert exc.value.status_code == 422
    assert not sandbox.exists()  # nothing persisted on rejection


def test_put_rejects_wrong_type_for_bool(sandbox):
    with pytest.raises(HTTPException) as exc:
        _run(auto_light_api.update_config({"headlight_enabled": 3000}))
    assert exc.value.status_code == 422


def test_put_rejects_wrong_type_for_number(sandbox):
    with pytest.raises(HTTPException) as exc:
        _run(auto_light_api.update_config({"highbeam_range_m": True}))
    assert exc.value.status_code == 422


def test_put_rejects_bool_string(sandbox):
    with pytest.raises(HTTPException) as exc:
        _run(auto_light_api.update_config({"headlight_enabled": "true"}))
    assert exc.value.status_code == 422


def test_put_accepts_all_known_keys(sandbox):
    patch = {
        "headlight_enabled": True,
        "headlight_illuminance_lux_threshold": 2500,
        "headlight_sun_elevation_deg": -1.5,
        "headlight_use_time_of_day": False,
        "headlight_dusk_hour": 18.5,
        "headlight_dawn_hour": 5.5,
        "headlight_tunnel_enabled": False,
        "highbeam_enabled": False,
        "highbeam_range_m": 100,
        "highbeam_range_hysteresis_m": 15,
        "highbeam_corridor_half_width_m": 5,
        "highbeam_on_delay_s": 1.0,
        "highbeam_off_delay_s": 0.2,
    }
    written = _run(auto_light_api.update_config(patch))
    for k, v in patch.items():
        expected = float(v) if not isinstance(v, bool) else v
        assert written[k] == expected


def test_put_reads_existing_file_as_base(sandbox):
    # Pre-seed a file that differs from defaults; a partial PUT must keep the rest.
    sandbox.parent.mkdir(parents=True, exist_ok=True)
    seed = dict(auto_light_api.DEFAULT_AUTO_LIGHT_CONFIG)
    seed["highbeam_corridor_half_width_m"] = 4.2
    sandbox.write_text(json.dumps(seed, indent=4), encoding="utf-8")

    _run(auto_light_api.update_config({"headlight_enabled": True}))
    reread = _run(auto_light_api.get_config())
    assert reread["headlight_enabled"] is True
    assert reread["highbeam_corridor_half_width_m"] == 4.2  # preserved from base
