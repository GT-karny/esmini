"""Tests for the Manual Drive controller config API (api/manual_drive_api.py).

Regression coverage for a data-loss bug: ``PUT /api/manual-drive/config``
validated the incoming payload via ``ManualDriveControllerConfig(**config)``
and persisted ``validated.model_dump()``. Because ``ManualDriveFFBConfig``
(and its sibling nested models) never declared every key that
``config/manual_drive.json`` actually carries -- F7/F7b added ``ffb.
target_track_*`` / ``ffb.safety_*`` tuning straight to the JSON file without
extending the model -- pydantic v2's default ``extra='ignore'`` silently
dropped every undeclared key the instant *any* manual-drive setting was
saved, including edits with nothing to do with FFB (e.g. a button mapping
change). The fix is two-layered:
  1. every nested model under ``ManualDriveControllerConfig`` now sets
     ``model_config = {"extra": "allow"}`` (models/simulation.py), so unknown
     keys survive validation instead of being discarded;
  2. ``update_config`` additionally persists by deep-merging the raw request
     onto the existing on-disk file (or schema defaults) rather than
     replacing it with ``model_dump()`` wholesale, so a request that itself
     omits a section (partial payload, a preset apply) cannot erase it.

We monkeypatch the module-level ``CONFIG_DIR`` to a tmp dir so the working
tree is never touched, mirroring test_auto_light_api / test_virtual_driver_api's
sandbox approach. Endpoints are plain async functions, driven directly via
``asyncio.run`` (no TestClient / app startup)."""

from __future__ import annotations

import asyncio
import json
import shutil
from pathlib import Path
from typing import Any

import pytest

from GT_esmini.web.backend import config
from GT_esmini.web.backend.api import manual_drive_api
from GT_esmini.web.backend.models.simulation import (
    ManualDriveControllerConfig,
    ManualDriveFFBConfig,
)

REAL_MANUAL_DRIVE_CONFIG = (
    Path(config.REPO_ROOT) / "GT_esmini" / "config" / "manual_drive.json"
)


def _run(coro):
    return asyncio.run(coro)


def _flat_keys(d: dict[str, Any], prefix: str = "") -> set[str]:
    """Flatten a (possibly nested) dict into a set of dotted key paths, so a
    key-set comparison doesn't care about intermediate nesting, only about
    which leaf/section names round-tripped. Deliberately does not hardcode a
    key count anywhere -- the config is expected to keep growing."""
    keys: set[str] = set()
    for k, v in d.items():
        full = f"{prefix}{k}"
        keys.add(full)
        if isinstance(v, dict):
            keys |= _flat_keys(v, full + ".")
    return keys


@pytest.fixture()
def sandbox(monkeypatch, tmp_path) -> Path:
    """Point the API at a tmp config dir; return the manual_drive.json path."""
    monkeypatch.setattr(manual_drive_api, "CONFIG_DIR", tmp_path)
    return tmp_path / manual_drive_api.MANUAL_DRIVE_CONFIG_FILE


@pytest.fixture()
def settings_sandbox(monkeypatch, tmp_path):
    """Point config.load_settings/save_settings (used by the presets
    endpoints) at a tmp repo root, so preset tests never touch the real
    GT_esmini/web/settings.json."""
    monkeypatch.setattr(config, "PACKAGED", False)
    monkeypatch.setattr(config, "REPO_ROOT", tmp_path)


# ---------------------------------------------------------------------------
# Core regression: the real repo config file must round-trip losslessly.
# ---------------------------------------------------------------------------


def test_real_config_put_roundtrip_loses_no_keys(sandbox):
    """The actual bug report: load the real committed manual_drive.json, PUT
    it back through the API with an edit unrelated to FFB (a button mapping
    tweak), and confirm every original key -- including ffb.target_track_* /
    ffb.safety_* -- survives. Before the fix this dropped ~30-59 keys."""
    assert REAL_MANUAL_DRIVE_CONFIG.is_file(), (
        f"{REAL_MANUAL_DRIVE_CONFIG} missing from checkout"
    )
    shutil.copy2(REAL_MANUAL_DRIVE_CONFIG, sandbox)

    before = json.loads(sandbox.read_text(encoding="utf-8"))
    before_keys = _flat_keys(before)
    assert before_keys, "real manual_drive.json parsed empty"

    # Simulate the panel: GET, tweak one unrelated field (button mapping),
    # PUT the whole object back -- exactly what the reported bug scenario did.
    edited = json.loads(sandbox.read_text(encoding="utf-8"))
    edited["input"]["upshift_button"] = 99

    written = _run(manual_drive_api.update_config(edited))

    after_keys = _flat_keys(written)
    lost = before_keys - after_keys
    assert not lost, f"round-trip dropped keys: {sorted(lost)}"
    assert after_keys == before_keys

    on_disk = json.loads(sandbox.read_text(encoding="utf-8"))
    assert _flat_keys(on_disk) == before_keys
    assert on_disk["input"]["upshift_button"] == 99
    # Spot-check a couple of the specific keys the bug report named.
    assert on_disk["ffb"]["target_track_override_residual_threshold"] == before["ffb"][
        "target_track_override_residual_threshold"
    ]
    assert (
        on_disk["ffb"]["safety_max_saturation_seconds"]
        == before["ffb"]["safety_max_saturation_seconds"]
    )


def test_real_config_edit_ffb_known_field_still_works(sandbox):
    """The fix must not turn this into a pure passthrough: a known field
    (sat_gain) must still actually change, and still coexist with the
    unmodeled keys."""
    shutil.copy2(REAL_MANUAL_DRIVE_CONFIG, sandbox)
    before = json.loads(sandbox.read_text(encoding="utf-8"))

    edited = json.loads(sandbox.read_text(encoding="utf-8"))
    edited["ffb"]["sat_gain"] = 0.42

    written = _run(manual_drive_api.update_config(edited))
    assert written["ffb"]["sat_gain"] == 0.42
    assert written["ffb"]["target_track_kp"] == before["ffb"]["target_track_kp"]


# ---------------------------------------------------------------------------
# Future-proofing: the mechanism must not depend on today's specific key
# names (the config is expected to keep growing).
# ---------------------------------------------------------------------------


def test_synthetic_unknown_keys_survive_at_every_nesting_level(sandbox):
    seed = ManualDriveControllerConfig().model_dump()
    seed["a_future_top_level_section"] = {"knob": 1}
    seed["ffb"]["a_future_ffb_knob_v99"] = 1.23
    seed["sdl2"]["a_future_sdl2_knob"] = "x"
    seed["sdl2"]["button_mapping"]["a_future_button"] = 3
    sandbox.parent.mkdir(parents=True, exist_ok=True)
    sandbox.write_text(json.dumps(seed, indent=4), encoding="utf-8")

    edited = json.loads(sandbox.read_text(encoding="utf-8"))
    edited["domain"]["lateral"] = "scenario"  # unrelated edit

    written = _run(manual_drive_api.update_config(edited))
    assert written["a_future_top_level_section"] == {"knob": 1}
    assert written["ffb"]["a_future_ffb_knob_v99"] == 1.23
    assert written["sdl2"]["a_future_sdl2_knob"] == "x"
    assert written["sdl2"]["button_mapping"]["a_future_button"] == 3
    assert written["domain"]["lateral"] == "scenario"


def test_ffb_model_extra_allow_directly(sandbox):
    """Unit-level guard on the model itself, independent of the API layer:
    ManualDriveFFBConfig must not be extra='ignore'."""
    m = ManualDriveFFBConfig(sat_gain=0.1, target_track_kp=9.9, some_new_key="z")
    dumped = m.model_dump()
    assert dumped["target_track_kp"] == 9.9
    assert dumped["some_new_key"] == "z"


# ---------------------------------------------------------------------------
# Robustness: a request that itself omits a section must not erase it
# (defense in depth beyond extra="allow" -- covers e.g. a preset apply).
# ---------------------------------------------------------------------------


def test_put_partial_payload_preserves_untouched_sections(sandbox):
    shutil.copy2(REAL_MANUAL_DRIVE_CONFIG, sandbox)
    before = json.loads(sandbox.read_text(encoding="utf-8"))

    # A minimal payload that only knows about a subset of sections (as a
    # built-in preset, or an older frontend build, might send).
    partial = {
        "input_type": "sdl2_wheel",
        "physics_type": "real_vehicle",
        "ffb_enabled": True,
        "domain": {"lateral": "manual", "longitudinal": "scenario"},
    }
    written = _run(manual_drive_api.update_config(partial))
    assert written["domain"]["longitudinal"] == "scenario"
    # Untouched sections/keys from the existing file must survive.
    assert written["ffb"]["target_track_kp"] == before["ffb"]["target_track_kp"]
    assert written["input"] == before["input"]
    assert written["override"] == before["override"]
    assert written["indicator_cancel_angle"] == before["indicator_cancel_angle"]


def test_get_config_missing_returns_defaults(sandbox):
    cfg = _run(manual_drive_api.get_config())
    assert cfg == ManualDriveControllerConfig().model_dump()
    assert not sandbox.exists()


# ---------------------------------------------------------------------------
# Presets (POST /presets) share the same models -- same hole, same fix.
# ---------------------------------------------------------------------------


def test_save_preset_preserves_unmodeled_ffb_keys(sandbox, settings_sandbox):
    real = json.loads(REAL_MANUAL_DRIVE_CONFIG.read_text(encoding="utf-8"))
    saved = _run(
        manual_drive_api.save_preset({"name": "My Tuning", "config": real})
    )
    assert (
        saved["config"]["ffb"]["target_track_override_residual_threshold"]
        == real["ffb"]["target_track_override_residual_threshold"]
    )

    presets = _run(manual_drive_api.get_presets())
    mine = next(p for p in presets if p["name"] == "My Tuning")
    assert (
        mine["config"]["ffb"]["safety_max_saturation_seconds"]
        == real["ffb"]["safety_max_saturation_seconds"]
    )
