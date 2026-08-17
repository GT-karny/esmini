"""feature:F8 -- wheel axis mapping: wire<->flat round trip, per-run delivery,
and the probe API's behaviour when the probe binary is absent.

The claim under test is the same one feature:F7's round-trip suite established
for buttons, because the failure mode is identical and was already paid for
once: a value the user saves in the GUI must (a) come back on the next GET, and
(b) reach the config file a launched run actually parses. A GET/PUT that merely
returns 200 proves neither.

The axis mapping raises the stakes over buttons: a wrong button index means one
control does nothing, while a wrong axis index means the brake pedal operates
the clutch -- silently, with the vehicle moving.
"""

from __future__ import annotations

import asyncio
import json
from pathlib import Path

import pytest
from fastapi import HTTPException

from GT_esmini.web.backend import config
from GT_esmini.web.backend.api import manual_drive_api, wheel_probe
from GT_esmini.web.backend.models.simulation import (
    SDL2_AXIS_KEY_MAP,
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
    cfg_dir = tmp_path / "config"
    cfg_dir.mkdir()
    (cfg_dir / "manual_drive.json").write_text(
        REAL_MANUAL_DRIVE_CONFIG.read_text(encoding="utf-8"), encoding="utf-8"
    )
    monkeypatch.setattr(manual_drive_api, "CONFIG_DIR", cfg_dir)
    monkeypatch.setattr(simulation_runner, "CONFIG_DIR", cfg_dir)
    return cfg_dir


# --- translation: exact inverses, and the shipped file is covered -----------


def test_shipped_config_exposes_every_axis_key_in_the_wire_shape():
    """If a key is added to the on-disk "input" block but not to
    SDL2_AXIS_KEY_MAP, the GUI silently stops seeing it -- the "saved but
    invisible" failure _flat_to_wire_shape exists to prevent. Asserting against
    the SHIPPED file (not a fixture) is what makes that regression impossible to
    introduce quietly."""
    flat = json.loads(REAL_MANUAL_DRIVE_CONFIG.read_text(encoding="utf-8"))
    wire = manual_drive_api._flat_to_wire_shape(flat)

    axis_mapping = wire["sdl2"]["axis_mapping"]
    for field, cpp_key in SDL2_AXIS_KEY_MAP.items():
        assert cpp_key in flat["input"], f"shipped config missing {cpp_key}"
        assert axis_mapping[field] == flat["input"][cpp_key], field


def test_axis_wire_to_flat_is_the_inverse_of_flat_to_wire():
    flat = json.loads(REAL_MANUAL_DRIVE_CONFIG.read_text(encoding="utf-8"))
    back = manual_drive_api._wire_to_flat_shape(
        manual_drive_api._flat_to_wire_shape(flat)
    )
    for cpp_key in SDL2_AXIS_KEY_MAP.values():
        assert back["input"][cpp_key] == flat["input"][cpp_key], cpp_key


def test_a_config_predating_f8_gets_no_axis_block_rather_than_a_partial_one(sandbox):
    """A file with no axis keys must not produce a half-filled axis_mapping:
    the frontend falls back to its defaults (the same G29 layout C++ defaults
    to), which is correct, whereas a block with three of thirteen keys would
    render as a mapping the user never chose."""
    (sandbox / "manual_drive.json").write_text(
        json.dumps({"input_type": "sdl2_wheel", "input": {"device_index": 0}}),
        encoding="utf-8",
    )
    wire = _run(manual_drive_api.get_config())
    assert "axis_mapping" not in wire.get("sdl2", {})


# --- the end-to-end claim: saved -> pre-filled -> reaches a run ------------


def test_saved_axis_reassignment_prefills_and_reaches_a_launched_run(sandbox, tmp_path):
    """The G923 scenario end to end: pedals in a different order than the G29,
    brake on a 0..32767 axis, steering reversed. Values are deliberately all
    different from the shipped ones -- a stock-value test cannot tell
    "round-tripped" from "coincidentally still correct"."""
    current = _run(manual_drive_api.get_config())
    assert current["sdl2"]["axis_mapping"]["throttle_axis"] == 1  # shipped G29

    current["sdl2"]["axis_mapping"].update(
        {
            "steer_axis": 0,
            "steer_raw_center": -100,
            "steer_raw_full": -30000,  # mirrored: full RIGHT below centre
            "throttle_axis": 2,
            "brake_axis": 1,
            "brake_raw_released": 0,
            "brake_raw_full": 32767,
            "clutch_axis": -1,
        }
    )
    _run(manual_drive_api.update_config(current))

    prefilled = _run(manual_drive_api.get_config())
    axes = prefilled["sdl2"]["axis_mapping"]
    assert axes["throttle_axis"] == 2
    assert axes["brake_axis"] == 1
    assert axes["brake_raw_released"] == 0
    assert axes["steer_raw_full"] == -30000  # the mirrored calibration survives
    assert axes["steer_raw_center"] == -100
    assert axes["clutch_axis"] == -1

    controller = ControllerConfig(
        controller_type="manual",
        manual_drive=ManualDriveControllerConfig(**prefilled),
    )
    out_dir = tmp_path / "run"
    out_dir.mkdir()
    simulation_runner._write_manual_drive_config(out_dir, controller)
    run_config = json.loads((out_dir / "manual_drive.json").read_text(encoding="utf-8"))

    # These are the exact flat keys ManualDriveConfig.cpp's scanner parses.
    assert run_config["input"]["throttle_axis"] == 2
    assert run_config["input"]["brake_axis"] == 1
    assert run_config["input"]["brake_raw_released"] == 0
    assert run_config["input"]["brake_raw_full"] == 32767
    assert run_config["input"]["steer_raw_full"] == -30000
    assert run_config["input"]["steer_raw_center"] == -100
    assert run_config["input"]["clutch_axis"] == -1


def test_a_run_request_without_an_axis_block_still_gets_the_g29_defaults(tmp_path):
    """Negative control for the per-run writer: an older frontend that sends no
    axis_mapping must produce the pre-F8 layout, not -1 (which for a raw-range
    key would be a nonsense calibration rather than "unassigned")."""
    controller = ControllerConfig(
        controller_type="manual", manual_drive=ManualDriveControllerConfig()
    )
    out_dir = tmp_path / "run"
    out_dir.mkdir()
    simulation_runner._write_manual_drive_config(out_dir, controller)
    run_config = json.loads((out_dir / "manual_drive.json").read_text(encoding="utf-8"))

    assert run_config["input"]["steer_axis"] == 0
    assert run_config["input"]["throttle_axis"] == 1
    assert run_config["input"]["brake_axis"] == 2
    assert run_config["input"]["clutch_axis"] == 3
    assert run_config["input"]["throttle_raw_released"] == 32767
    assert run_config["input"]["throttle_raw_full"] == -32768
    assert run_config["input"]["steer_raw_center"] == 0
    assert run_config["input"]["steer_raw_full"] == 32767


# --- probe API ------------------------------------------------------------


def test_mapping_args_translates_every_key_and_the_invert_flag():
    flags = wheel_probe._mapping_args(
        {
            "steer_axis": 0,
            "steer_raw_center": -100,
            "steer_raw_full": -30000,  # mirrored: full RIGHT below centre
            "throttle_axis": 2,
            "throttle_raw_released": 32767,
            "throttle_raw_full": -32768,
            "brake_axis": 1,
            "brake_raw_released": 0,
            "brake_raw_full": 32767,
            "clutch_axis": -1,
            "clutch_raw_released": 32767,
            "clutch_raw_full": -32768,
        }
    )
    assert flags[flags.index("--throttle-axis") + 1] == "2"
    assert flags[flags.index("--brake-raw-released") + 1] == "0"
    assert flags[flags.index("--clutch-axis") + 1] == "-1"
    # Every mapping key must reach the probe: a key that silently fails to
    # translate would make the GUI preview disagree with what a run does.
    for key in SDL2_AXIS_KEY_MAP:
        assert "--" + key.replace("_", "-") in flags, key


def test_mapping_args_never_forwards_a_retired_invert_key():
    # feature:F8 -- the flag was removed in favour of the calibration order, and
    # GT_WheelProbe rejects --steer-invert with exit 2. A stale client that still
    # sends the key must therefore not have it translated (which would kill the
    # live readout), and must not have it silently honoured either (that would
    # restore two representations of one fact).
    flags = wheel_probe._mapping_args({"steer_axis": 0, "steer_invert": True})
    assert "--steer-invert" not in flags
    assert flags == ["--steer-axis", "0"]


def test_mapping_args_drops_a_malformed_value_instead_of_poisoning_the_command():
    flags = wheel_probe._mapping_args({"throttle_axis": "not-a-number"})
    assert flags == []


def test_probe_endpoints_report_a_missing_binary_as_a_condition_not_a_crash(
    monkeypatch, tmp_path
):
    """GT_ENABLE_SDL2 defaults OFF, so a build without the probe is normal. The
    status endpoint must say so, and /devices must 503 with an explanation the
    panel can show verbatim -- not raise."""
    monkeypatch.setattr(wheel_probe, "GT_WHEEL_PROBE_EXE", tmp_path / "nope.exe")

    status = _run(wheel_probe.probe_status())
    assert status["available"] is False
    assert "GT_ENABLE_SDL2" in status["message"]

    with pytest.raises(HTTPException) as excinfo:
        _run(wheel_probe.list_devices())
    assert excinfo.value.status_code == 503


def test_probe_status_reports_available_when_the_binary_exists(monkeypatch, tmp_path):
    # Both polarities of the availability check; without this the test above
    # passes for a status endpoint hardwired to False.
    fake = tmp_path / "GT_WheelProbe.exe"
    fake.write_bytes(b"")
    monkeypatch.setattr(wheel_probe, "GT_WHEEL_PROBE_EXE", fake)

    status = _run(wheel_probe.probe_status())
    assert status["available"] is True
    assert status["message"] is None
