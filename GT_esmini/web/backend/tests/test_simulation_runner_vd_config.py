"""Regression tests for ``_write_virtual_driver_config``
(services/simulation_runner.py).

Bug: the per-run writer unconditionally set ``base["input_type"] = "network"``
after loading the shipped ``config/virtual_driver.json`` as its base, so a
user who chose a different input source (e.g. ``sdl2_wheel``, persisted via
``PUT /api/virtual-driver/config`` -- see test_virtual_driver_api.py) never
had it take effect: every GUI-launched run silently used "network" instead,
and the physical wheel input source was never constructed
(ControllerVirtualDriver.cpp's ``input_type == "sdl2_wheel"`` branch never
ran). The fix only *defaults* a base ``input_type`` of "stub" (the shipped,
never-configured value) up to "network" -- preserving the existing "web
override panel works out of the box" behavior -- and passes any other
explicit choice through unmodified.

We monkeypatch the module-level ``CONFIG_DIR`` to a tmp dir so the working
tree is never touched, mirroring test_auto_light_api / test_virtual_driver_api's
sandbox approach.
"""

from __future__ import annotations

import json
from pathlib import Path

import pytest

from GT_esmini.web.backend.services import simulation_runner


@pytest.fixture()
def config_dir(monkeypatch, tmp_path) -> Path:
    """Point the runner at a tmp config dir; return it."""
    d = tmp_path / "config"
    d.mkdir()
    monkeypatch.setattr(simulation_runner, "CONFIG_DIR", d)
    return d


def _seed_base_config(config_dir: Path, **overrides) -> None:
    base = {
        "_comment": "test fixture",
        "idm_min_gap": 2.0,
        "input_type": "stub",
        "input_port": 9100,
        "input_transport": "udp",
    }
    base.update(overrides)
    (config_dir / "virtual_driver.json").write_text(
        json.dumps(base, indent=2), encoding="utf-8"
    )


def _read_written(output_dir: Path) -> dict:
    return json.loads((output_dir / "virtual_driver.json").read_text(encoding="utf-8"))


def test_defaults_to_network_when_no_base_file(config_dir, tmp_path):
    # No config/virtual_driver.json at all -- historical behavior: default to
    # network so the web override panel works out of the box.
    out_dir = tmp_path / "run"
    simulation_runner._write_virtual_driver_config(out_dir)
    assert _read_written(out_dir)["input_type"] == "network"


def test_defaults_to_network_when_base_is_stub(config_dir, tmp_path):
    # Shipped config/virtual_driver.json ships input_type="stub" and nothing
    # ever changed it (never configured via the GUI) -- still defaults up to
    # "network" so existing installs keep the override panel working.
    _seed_base_config(config_dir, input_type="stub")
    out_dir = tmp_path / "run"
    simulation_runner._write_virtual_driver_config(out_dir)
    assert _read_written(out_dir)["input_type"] == "network"


def test_respects_explicit_sdl2_wheel_choice(config_dir, tmp_path):
    # THE regression this bug is about: a user who picked "sdl2_wheel" via
    # PUT /api/virtual-driver/config must have it reach the run's config,
    # not get silently overwritten back to "network".
    _seed_base_config(config_dir, input_type="sdl2_wheel")
    out_dir = tmp_path / "run"
    simulation_runner._write_virtual_driver_config(out_dir)
    assert _read_written(out_dir)["input_type"] == "sdl2_wheel"


def test_respects_explicit_network_choice(config_dir, tmp_path):
    # An explicit "network" choice is a no-op through the defaulting logic.
    _seed_base_config(config_dir, input_type="network")
    out_dir = tmp_path / "run"
    simulation_runner._write_virtual_driver_config(out_dir)
    assert _read_written(out_dir)["input_type"] == "network"


def test_preserves_tuned_gains_alongside_input_type(config_dir, tmp_path):
    # The writer must still start from the shipped base (preserving tuned
    # gains) regardless of which input_type branch is taken.
    _seed_base_config(config_dir, input_type="sdl2_wheel", idm_min_gap=4.2)
    out_dir = tmp_path / "run"
    simulation_runner._write_virtual_driver_config(out_dir)
    written = _read_written(out_dir)
    assert written["idm_min_gap"] == 4.2
    assert written["input_type"] == "sdl2_wheel"


def test_input_port_and_transport_still_defaulted(config_dir, tmp_path):
    # input_port / input_transport remain runner-defaulted (setdefault),
    # unaffected by the input_type fix.
    _seed_base_config(config_dir, input_type="sdl2_wheel")
    (config_dir / "virtual_driver.json").write_text(
        json.dumps({"input_type": "sdl2_wheel"}), encoding="utf-8"
    )
    out_dir = tmp_path / "run"
    simulation_runner._write_virtual_driver_config(out_dir)
    written = _read_written(out_dir)
    assert written["input_port"] == simulation_runner.DEFAULT_VD_INPUT_PORT
    assert written["input_transport"] == "udp"


def test_policies_still_applied_alongside_input_type_fix(config_dir, tmp_path):
    _seed_base_config(config_dir, input_type="sdl2_wheel")
    out_dir = tmp_path / "run"
    simulation_runner._write_virtual_driver_config(out_dir, ["lead", "aeb"])
    written = _read_written(out_dir)
    assert written["policy_lead_enabled"] is True
    assert written["policy_aeb_enabled"] is True
    assert written["input_type"] == "sdl2_wheel"
