"""Application configuration with path resolution."""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any

import yaml


def _find_repo_root() -> Path:
    """Locate the repository root (contains EnvironmentSimulator/ and GT_esmini/)."""
    candidate = Path(__file__).resolve().parents[3]  # web/backend/ -> GT_esmini/ -> esmini/
    if (candidate / "GT_esmini").is_dir() and (candidate / "EnvironmentSimulator").is_dir():
        return candidate
    raise RuntimeError(f"Cannot locate repository root from {__file__}")


REPO_ROOT = _find_repo_root()

# Key paths
GT_SIM_EXE = REPO_ROOT / "build" / "GT_esmini" / "Release" / "GT_Sim.exe"
SCENARIOS_DIR = REPO_ROOT / "resources" / "xosc"
DRIVERSCRIPT_DIR = REPO_ROOT / "DriverScript"
SCRIPTS_DIR = REPO_ROOT / "scripts"
CONFIG_DIR = REPO_ROOT / "GT_esmini" / "config"
RESULTS_DIR = REPO_ROOT / "test_results" / "web"
DB_PATH = REPO_ROOT / "GT_esmini" / "web" / "gt_sim.db"

# Ensure scripts/ is importable
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))
if str(DRIVERSCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(DRIVERSCRIPT_DIR))

# Python script scan directories (relative to REPO_ROOT)
PYTHON_SCRIPT_DIRS = [
    "DriverScript/pythondriver",
    "DriverScript/examples",
    "DriverScript/realdriver",
]

# Default execution parameters
DEFAULT_EXECUTION_PARAMS: dict[str, Any] = {
    "hz": 120,
    "headless": True,
    "record": True,
    "no_realtime": False,
    "timeout": 60,
    "osi": {"enabled": True, "ip": "127.0.0.1"},
    "autolight": True,
    "window": {"x": 60, "y": 60, "w": 1280, "h": 720},
}

# Default controller configuration
DEFAULT_CONTROLLER_CONFIG: dict[str, Any] = {
    "controller_type": "default",
    "python": {
        "script": "DriverScript/pythondriver/scenario_drive_embedded.py",
        "class": "EmbeddedController",
        "python_home": "",
        "trace_enabled": True,
        "trace_dir": "",
    },
}


def _settings_path() -> Path:
    return REPO_ROOT / "GT_esmini" / "web" / "settings.json"


def load_settings() -> dict[str, Any]:
    """Load persisted settings (execution defaults + controller config)."""
    path = _settings_path()
    if path.exists():
        return json.loads(path.read_text(encoding="utf-8"))
    return {
        "execution_defaults": dict(DEFAULT_EXECUTION_PARAMS),
        "controller_config": dict(DEFAULT_CONTROLLER_CONFIG),
    }


def save_settings(settings: dict[str, Any]) -> None:
    """Persist settings to disk."""
    path = _settings_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(settings, indent=2, ensure_ascii=False), encoding="utf-8")


def load_vehicle_params() -> dict[str, Any]:
    path = CONFIG_DIR / "real_vehicle_params.json"
    if path.exists():
        return json.loads(path.read_text(encoding="utf-8"))
    return {}


def save_vehicle_params(params: dict[str, Any]) -> None:
    path = CONFIG_DIR / "real_vehicle_params.json"
    path.write_text(json.dumps(params, indent=2, ensure_ascii=False), encoding="utf-8")


def load_thresholds() -> dict[str, Any]:
    path = REPO_ROOT / "GT_esmini" / "test" / "comparison_thresholds.yaml"
    if path.exists():
        return yaml.safe_load(path.read_text(encoding="utf-8")) or {}
    return {}


def save_thresholds(data: dict[str, Any]) -> None:
    path = REPO_ROOT / "GT_esmini" / "test" / "comparison_thresholds.yaml"
    path.write_text(yaml.dump(data, allow_unicode=True, default_flow_style=False), encoding="utf-8")
