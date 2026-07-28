"""Tests for the PER-RUN manual_drive.json writer (simulation_runner.py).

Why this file exists
--------------------
``test_manual_drive_api.py`` covers the PERSISTENCE API (PUT /api/manual-drive/
config). It does not touch ``_write_manual_drive_config()``, which is a
different function writing a different file -- and it is the per-run writer
that a GUI-launched run actually travels through. Every symptom the user hit
on 2026-07-27 came out of this path:

  * the input type being overwritten so the wheel was never used (ab4ecb10);
  * AUTO_RESUME arriving unassigned on 100% of GUI-launched manual runs
    (gap #5), which is the original F7 request ("let the button assignment be
    changeable from settings later");
  * override thresholds and vehicle_params_file replaced by hardcoded values
    (gaps #4 / #3).

The function was rewritten twice on 2026-07-27 (once to fix #5, once to route
the button mapping through the shared table for #2) with no committed test
over it at all. A green suite that says nothing about this path is exactly the
"tests pass but the user's symptom is unchanged" shape this project kept
hitting.

Method (same as the persistence-API tests)
------------------------------------------
Seed a sandbox CONFIG_DIR from the REAL config/manual_drive.json, plant
synthetic unknown keys at each nesting level, and check what survives into the
per-run file. Values are chosen to differ from BOTH the shipped config and the
C++ compile-time defaults -- with stock values, #3 in particular is invisible
because the hardcoded string happened to equal the shipped one.
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

import pytest

from GT_esmini.web.backend import config
from GT_esmini.web.backend.models.simulation import (
    SDL2_BUTTON_KEY_MAP,
    ControllerConfig,
)
from GT_esmini.web.backend.services import simulation_runner

REAL_MANUAL_DRIVE_CONFIG = (
    Path(config.REPO_ROOT) / "GT_esmini" / "config" / "manual_drive.json"
)


@pytest.fixture
def sandbox(tmp_path, monkeypatch) -> dict[str, Any]:
    """Sandbox CONFIG_DIR seeded from the real shipped config."""
    base = json.loads(REAL_MANUAL_DRIVE_CONFIG.read_text(encoding="utf-8"))
    cfg_dir = tmp_path / "config"
    cfg_dir.mkdir()
    monkeypatch.setattr(simulation_runner, "CONFIG_DIR", cfg_dir)
    assert simulation_runner.CONFIG_DIR == cfg_dir  # hook must take effect
    return {"base": base, "cfg_dir": cfg_dir, "out": tmp_path}


def _write(sandbox, base: dict, controller: ControllerConfig | None = None) -> dict:
    (sandbox["cfg_dir"] / "manual_drive.json").write_text(
        json.dumps(base, indent=2), encoding="utf-8"
    )
    out = sandbox["out"] / "run"
    out.mkdir(exist_ok=True)
    simulation_runner._write_manual_drive_config(
        out, controller or ControllerConfig(controller_type="manual")
    )
    return json.loads((out / "manual_drive.json").read_text(encoding="utf-8"))


# --- gap #5 / #6: AUTO_RESUME ---------------------------------------------


def test_auto_resume_button_reaches_the_run(sandbox):
    """The original F7 request. Was absent entirely -> C++ default -1."""
    run = _write(sandbox, sandbox["base"])
    assert run["input"]["auto_resume_button"] == sandbox["base"]["input"]["auto_resume_button"]
    assert run["input"]["auto_resume_button"] != -1


def test_auto_resume_honours_a_gui_reassignment(sandbox):
    cc = ControllerConfig(controller_type="manual")
    cc.manual_drive.sdl2.button_mapping.auto_resume = 9
    run = _write(sandbox, sandbox["base"], cc)
    assert run["input"]["auto_resume_button"] == 9


# --- gap #2: every mapped button reaches its C++ key ----------------------


def test_every_button_in_the_shared_table_is_written(sandbox):
    """Guards the drift that produced gap #5: a button added to the model but
    forgotten in the writer (or vice versa) fails here."""
    run = _write(sandbox, sandbox["base"])
    for cpp_key in SDL2_BUTTON_KEY_MAP.values():
        assert cpp_key in run["input"], f"{cpp_key} missing from per-run config"


# --- gap #3: vehicle_params_file ------------------------------------------


def test_vehicle_params_file_is_not_hardcoded(sandbox):
    """Uses a NON-default value: with the stock config the hardcoded string
    coincides with the shipped one and the bug is invisible."""
    base = json.loads(json.dumps(sandbox["base"]))
    base["physics"]["vehicle_params_file"] = "custom_vehicle.json"
    run = _write(sandbox, base)
    assert run["physics"]["vehicle_params_file"] == "custom_vehicle.json"


# --- gap #4: override block ------------------------------------------------


def test_override_block_carries_all_user_values(sandbox):
    base = json.loads(json.dumps(sandbox["base"]))
    base["override"] = {
        "enabled": False,
        "steering_threshold": 0.33,
        "throttle_threshold": 0.44,
        "brake_threshold": 0.55,
        "auto_return_timeout": 9.5,
        "button_override": False,
    }
    run = _write(sandbox, base)
    assert run["override"] == base["override"]


def test_override_falls_back_to_cpp_defaults_when_base_is_silent(sandbox):
    """A base with no override section must reproduce the previous behaviour."""
    base = json.loads(json.dumps(sandbox["base"]))
    base.pop("override", None)
    run = _write(sandbox, base)
    assert run["override"]["enabled"] is True
    assert run["override"]["steering_threshold"] == 0.05
    assert run["override"]["button_override"] is True


# --- the request must still own what the request owns ----------------------


def test_request_owned_fields_are_not_overridden_by_base(sandbox):
    """Symmetry check: the base must not start winning over the live request.
    ab4ecb10 was the mirror image of this -- the run ignored the requested
    input type and the wheel was never used."""
    base = json.loads(json.dumps(sandbox["base"]))
    base["input_type"] = "keyboard"
    base["input"]["deadzone"] = 0.99
    base["physics"]["cmd_port"] = 1
    cc = ControllerConfig(controller_type="manual")
    run = _write(sandbox, base, cc)
    md = cc.manual_drive
    assert run["input_type"] == md.input_type
    assert run["input"]["deadzone"] == md.sdl2.deadzone
    assert run["physics"]["cmd_port"] == md.physics_network.cmd_port


# --- unknown-key survival, per nesting level -------------------------------


def test_unknown_keys_inside_ffb_survive(sandbox):
    """ffb is taken wholesale from base, so F7/F7b tuning keys added straight
    to the JSON (as they have been, repeatedly) must reach the run."""
    base = json.loads(json.dumps(sandbox["base"]))
    base["ffb"]["some_future_tuning_key"] = 1.25
    run = _write(sandbox, base)
    assert run["ffb"]["some_future_tuning_key"] == 1.25


def test_unknown_keys_inside_override_survive(sandbox):
    base = json.loads(json.dumps(sandbox["base"]))
    base["override"]["some_future_override_key"] = 7
    run = _write(sandbox, base)
    assert run["override"]["some_future_override_key"] == 7


def test_indicator_cancel_angle_comes_from_base(sandbox):
    base = json.loads(json.dumps(sandbox["base"]))
    base["indicator_cancel_angle"] = 0.123
    run = _write(sandbox, base)
    assert run["indicator_cancel_angle"] == 0.123
