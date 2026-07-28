"""feature:F7 -- ManualDrive /config wire-shape (flat<->nested) round-trip.

The audit finding this file exists to close: db567aea fixed PUT's
sdl2->flat translation (gap #2) but GET never translated the other
direction. GET returned the raw on-disk FLAT shape
(``input.upshift_button``, ...); the frontend's pre-fill code
(SimulationRunForm.tsx) only ever reads the NESTED wire shape
(``config.sdl2.button_mapping.upshift``, ``config.input_network.*``,
``config.physics_network.*``, top-level ``config.vehicle_params_file``,
``config.override_cfg.*``). Every one of those reads was silently
``undefined`` against a real GET response, so "save a setting, reopen the
panel" always re-showed DEFAULT_MANUAL_CONFIG's hardcoded values --
including AUTO_RESUME, the field with an actual prior incident (gap #5:
unassigned on 100% of GUI-launched manual runs).

GET/PUT succeeding (422-free) is NOT the claim under test here -- that was
already covered by test_manual_drive_api.py before this fix existed, and
would stay green even with the shape bug (the endpoints never round-tripped
through EACH OTHER in that suite). The claim this file proves is the actual
audit demand: a value SAVED through PUT is what GET returns on the very
next call (same session or a fresh one -- GET does not know the
difference), AND that pre-filled value is what a subsequently launched run
actually receives, by chaining into
simulation_runner._write_manual_drive_config (the function that produces
the file ManualDriveConfig.cpp really parses).
"""

from __future__ import annotations

import asyncio
import json
from pathlib import Path
from typing import Any

import pytest

from GT_esmini.web.backend import config
from GT_esmini.web.backend.api import manual_drive_api
from GT_esmini.web.backend.models.simulation import (
    ControllerConfig,
    ManualDriveControllerConfig,
)
from GT_esmini.web.backend.services import simulation_runner

REAL_MANUAL_DRIVE_CONFIG = (
    Path(config.REPO_ROOT) / "GT_esmini" / "config" / "manual_drive.json"
)


def _run(coro):
    return asyncio.run(coro)


@pytest.fixture
def sandbox(tmp_path, monkeypatch):
    """Point BOTH the persistence API (manual_drive_api) and the per-run
    writer (simulation_runner) at the SAME tmp CONFIG_DIR, seeded from the
    real shipped config -- otherwise a PUT through one module and a read
    through the other would not actually share a file, and the end-to-end
    claim below would not be testing what it claims to."""
    cfg_dir = tmp_path / "config"
    cfg_dir.mkdir()
    (cfg_dir / "manual_drive.json").write_text(
        REAL_MANUAL_DRIVE_CONFIG.read_text(encoding="utf-8"), encoding="utf-8"
    )
    monkeypatch.setattr(manual_drive_api, "CONFIG_DIR", cfg_dir)
    monkeypatch.setattr(simulation_runner, "CONFIG_DIR", cfg_dir)
    assert manual_drive_api._config_path() == cfg_dir / "manual_drive.json"
    return cfg_dir


# --- unit-level: the translation functions are exact inverses --------------


def test_flat_to_wire_exposes_the_nested_shape_the_frontend_reads():
    flat = json.loads(REAL_MANUAL_DRIVE_CONFIG.read_text(encoding="utf-8"))
    wire = manual_drive_api._flat_to_wire_shape(flat)
    assert wire["sdl2"]["button_mapping"]["auto_resume"] == flat["input"]["auto_resume_button"]
    assert wire["sdl2"]["device_index"] == flat["input"]["device_index"]
    assert wire["input_network"]["port"] == flat["input"]["port"]
    assert wire["physics_network"]["host"] == flat["physics"]["host"]
    assert wire["vehicle_params_file"] == flat["physics"]["vehicle_params_file"]
    assert wire["override_cfg"] == flat["override"]


def test_wire_to_flat_is_the_inverse_of_flat_to_wire():
    flat = json.loads(REAL_MANUAL_DRIVE_CONFIG.read_text(encoding="utf-8"))
    wire = manual_drive_api._flat_to_wire_shape(flat)
    back_to_flat = manual_drive_api._wire_to_flat_shape(wire)
    for cpp_key in (
        "upshift_button", "downshift_button", "override_button",
        "indicator_left_button", "indicator_right_button", "headlight_button",
        "high_beam_button", "fog_light_button", "hazard_button", "auto_resume_button",
        "device_index", "deadzone", "transport_type", "port", "level",
    ):
        assert back_to_flat["input"][cpp_key] == flat["input"][cpp_key], cpp_key
    assert back_to_flat["physics"]["vehicle_params_file"] == flat["physics"]["vehicle_params_file"]
    assert back_to_flat["physics"]["host"] == flat["physics"]["host"]
    assert back_to_flat["override"] == flat["override"]


# --- the end-to-end claim: PUT -> GET pre-fill -> a launched run ----------


def test_saved_auto_resume_reassignment_prefills_on_the_next_get(sandbox):
    """The literal audit scenario: user opens the panel, GETs the current
    config, reassigns AUTO_RESUME, PUTs it back, closes the app. Next
    session: GET must show the reassignment, not the hardcoded default."""
    current = _run(manual_drive_api.get_config())
    assert current["sdl2"]["button_mapping"]["auto_resume"] == 3  # shipped default

    current["sdl2"]["button_mapping"]["auto_resume"] = 9
    _run(manual_drive_api.update_config(current))

    reopened = _run(manual_drive_api.get_config())
    assert reopened["sdl2"]["button_mapping"]["auto_resume"] == 9


def test_saved_reassignment_reaches_an_actual_launched_run(sandbox, tmp_path):
    """Full chain, per the audit's own standard ("GET/PUTが通ることと走行が
    その値で動くことは別"): save -> pre-fill -> build a run request from the
    pre-filled value (exactly what SimulationRunForm does, seeding
    manualDriveConfig from the GET response) -> the per-run writer -> the
    flat key ManualDriveConfig.cpp actually parses."""
    current = _run(manual_drive_api.get_config())
    current["sdl2"]["button_mapping"]["auto_resume"] = 9
    current["sdl2"]["button_mapping"]["upshift"] = 11
    _run(manual_drive_api.update_config(current))

    prefilled = _run(manual_drive_api.get_config())
    assert prefilled["sdl2"]["button_mapping"]["auto_resume"] == 9
    assert prefilled["sdl2"]["button_mapping"]["upshift"] == 11

    # What SimulationRunForm.tsx does with the GET response: use it (merged
    # onto its own defaults on the frontend, but every field here is present
    # so the merge is a no-op) as the manual_drive block of a new run request.
    controller = ControllerConfig(
        controller_type="manual",
        manual_drive=ManualDriveControllerConfig(**prefilled),
    )

    out_dir = tmp_path / "run"
    out_dir.mkdir()
    simulation_runner._write_manual_drive_config(out_dir, controller)
    run_config = json.loads((out_dir / "manual_drive.json").read_text(encoding="utf-8"))

    assert run_config["input"]["auto_resume_button"] == 9
    assert run_config["input"]["upshift_button"] == 11


def test_saved_override_threshold_and_vehicle_params_file_also_prefill(sandbox, tmp_path):
    """Same shape bug, different fields -- the fix is general, not
    auto_resume-specific. Values deliberately differ from both the shipped
    config and the C++ compile-time defaults (a stock-value test cannot
    distinguish "round-tripped" from "coincidentally still correct")."""
    current = _run(manual_drive_api.get_config())
    current["override_cfg"]["steering_threshold"] = 0.33
    current["vehicle_params_file"] = "custom_vehicle.json"
    _run(manual_drive_api.update_config(current))

    prefilled = _run(manual_drive_api.get_config())
    assert prefilled["override_cfg"]["steering_threshold"] == 0.33
    assert prefilled["vehicle_params_file"] == "custom_vehicle.json"

    controller = ControllerConfig(
        controller_type="manual",
        manual_drive=ManualDriveControllerConfig(**prefilled),
    )
    out_dir = tmp_path / "run"
    out_dir.mkdir()
    simulation_runner._write_manual_drive_config(out_dir, controller)
    run_config = json.loads((out_dir / "manual_drive.json").read_text(encoding="utf-8"))

    assert run_config["override"]["steering_threshold"] == 0.33
    assert run_config["physics"]["vehicle_params_file"] == "custom_vehicle.json"


def test_a_pre_fix_style_flat_only_put_still_works(sandbox):
    """Backward compatibility: a client that PUTs the flat shape directly
    (bypassing the nested wire translation entirely) must still work --
    _wire_to_flat_shape only translates keys it recognizes and passes
    everything else through unchanged."""
    _run(manual_drive_api.update_config({"input": {"upshift_button": 42}}))
    on_disk = json.loads((manual_drive_api._config_path()).read_text(encoding="utf-8"))
    assert on_disk["input"]["upshift_button"] == 42
    # Untouched sibling keys under "input" must survive the merge.
    assert on_disk["input"]["auto_resume_button"] == 3
