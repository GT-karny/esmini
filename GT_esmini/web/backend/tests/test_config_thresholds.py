"""Tests for config.load_thresholds / save_thresholds (audit TST-6 hotspot:
GT_esmini/test/comparison_thresholds.yaml is live backend state).

The real repo file is copied into tmp_path and config.REPO_ROOT is
monkeypatched, so the tests never touch the working tree."""

from __future__ import annotations

import shutil
from pathlib import Path

from GT_esmini.web.backend import config

REAL_THRESHOLDS = (
    Path(config.REPO_ROOT) / "GT_esmini" / "test" / "comparison_thresholds.yaml"
)


def _sandbox(monkeypatch, tmp_path: Path) -> Path:
    """Point config at a tmp repo root; return the thresholds path inside it."""
    monkeypatch.setattr(config, "PACKAGED", False)
    monkeypatch.setattr(config, "REPO_ROOT", tmp_path)
    return tmp_path / "GT_esmini" / "test" / "comparison_thresholds.yaml"


def test_load_missing_returns_empty(monkeypatch, tmp_path):
    _sandbox(monkeypatch, tmp_path)
    assert config.load_thresholds() == {}


def test_load_real_repo_file_roundtrip(monkeypatch, tmp_path):
    assert REAL_THRESHOLDS.is_file(), f"{REAL_THRESHOLDS} missing from checkout"
    dest = _sandbox(monkeypatch, tmp_path)
    dest.parent.mkdir(parents=True)
    shutil.copy2(REAL_THRESHOLDS, dest)

    loaded = config.load_thresholds()
    assert isinstance(loaded, dict) and loaded, "real thresholds file parsed empty"

    # save -> load must be lossless
    config.save_thresholds(loaded)
    assert config.load_thresholds() == loaded


def test_save_creates_parent_dirs_and_persists(monkeypatch, tmp_path):
    dest = _sandbox(monkeypatch, tmp_path)
    assert not dest.parent.exists()

    data = {
        "default": {"xy_rmse_m": 0.5, "speed_rmse_mps": 1.0},
        "scenario_overrides": {"vd_basic": {"xy_rmse_m": 0.8}},
        "unicode_note": "しきい値",
    }
    config.save_thresholds(data)
    assert dest.is_file()
    assert config.load_thresholds() == data


def test_save_empty_dict_loads_back_empty(monkeypatch, tmp_path):
    _sandbox(monkeypatch, tmp_path)
    config.save_thresholds({})
    # yaml.dump({}) -> "{}"; safe_load gives {} (guarded by `or {}` for None)
    assert config.load_thresholds() == {}


def test_packaged_mode_uses_config_dir(monkeypatch, tmp_path):
    monkeypatch.setattr(config, "PACKAGED", True)
    monkeypatch.setattr(config, "CONFIG_DIR", tmp_path / "config")
    data = {"default": {"xy_rmse_m": 1.5}}
    config.save_thresholds(data)
    assert (tmp_path / "config" / "comparison_thresholds.yaml").is_file()
    assert config.load_thresholds() == data
