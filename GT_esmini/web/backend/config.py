"""Application configuration with path resolution.

Supports two modes:
- Development: paths resolve relative to the repository root
- Packaged: paths resolve relative to the release package root
  (detected via sys.frozen or GT_SIM_WEB_PACKAGE_ROOT env var)
"""

from __future__ import annotations

import json
import os
import sys
from pathlib import Path
from typing import Any

import yaml


# ---------------------------------------------------------------------------
# Mode detection
# ---------------------------------------------------------------------------

def _is_packaged() -> bool:
    """Detect if running from a PyInstaller package."""
    return getattr(sys, "frozen", False) or "GT_SIM_WEB_PACKAGE_ROOT" in os.environ


def _find_package_root() -> Path:
    """Resolve root for packaged deployment."""
    env_root = os.environ.get("GT_SIM_WEB_PACKAGE_ROOT")
    if env_root:
        return Path(env_root).resolve()
    # Frozen exe is at server/gt_sim_web.exe → parent.parent = package root
    return Path(sys.executable).resolve().parent.parent


def _find_repo_root() -> Path:
    """Locate the repository root (contains EnvironmentSimulator/ and GT_esmini/)."""
    candidate = Path(__file__).resolve().parents[3]  # web/backend/ -> GT_esmini/ -> esmini/
    if (candidate / "GT_esmini").is_dir() and (candidate / "EnvironmentSimulator").is_dir():
        return candidate
    raise RuntimeError(f"Cannot locate repository root from {__file__}")


PACKAGED = _is_packaged()

# ---------------------------------------------------------------------------
# Path constants — branch on PACKAGED
# ---------------------------------------------------------------------------

if PACKAGED:
    PACKAGE_ROOT = _find_package_root()
    REPO_ROOT = PACKAGE_ROOT  # alias for downstream compatibility

    GT_SIM_EXE = PACKAGE_ROOT / "bin" / "GT_Sim.exe"
    ESMINI_RM_LIB = PACKAGE_ROOT / "bin" / "esminiRMLib.dll"
    SCENARIOS_DIR = PACKAGE_ROOT / "resources" / "xosc"
    DRIVERSCRIPT_DIR = PACKAGE_ROOT / "DriverScript"
    SCRIPTS_DIR = PACKAGE_ROOT / "scripts"
    CONFIG_DIR = PACKAGE_ROOT / "config"
    RESULTS_DIR = PACKAGE_ROOT / "data" / "results"
    PROJECTS_DIR = PACKAGE_ROOT / "data" / "projects"
    TEMP_SCENARIOS_DIR = PACKAGE_ROOT / "data" / "_temp_scenarios"
    TEMP_ROADS_DIR = PACKAGE_ROOT / "data" / "_temp_roads"
    DB_PATH = PACKAGE_ROOT / "data" / "gt_sim.db"
    RESOURCES_DIR = PACKAGE_ROOT / "resources"
else:
    REPO_ROOT = _find_repo_root()

    GT_SIM_EXE = REPO_ROOT / "build" / "GT_esmini" / "Release" / "GT_Sim.exe"
    ESMINI_RM_LIB = REPO_ROOT / "DriverScript" / "bin" / "esminiRMLib.dll"
    SCENARIOS_DIR = REPO_ROOT / "resources" / "xosc"
    DRIVERSCRIPT_DIR = REPO_ROOT / "DriverScript"
    SCRIPTS_DIR = REPO_ROOT / "scripts"
    CONFIG_DIR = REPO_ROOT / "GT_esmini" / "config"
    RESULTS_DIR = REPO_ROOT / "test_results" / "web"
    PROJECTS_DIR = REPO_ROOT / "test_results" / "web" / "projects"
    TEMP_SCENARIOS_DIR = REPO_ROOT / "test_results" / "web" / "_temp_scenarios"
    TEMP_ROADS_DIR = REPO_ROOT / "test_results" / "web" / "_temp_roads"
    DB_PATH = REPO_ROOT / "GT_esmini" / "web" / "gt_sim.db"
    RESOURCES_DIR = REPO_ROOT / "resources"

# Ensure scripts/ is importable
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))
if str(DRIVERSCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(DRIVERSCRIPT_DIR))

# Python script scan directories (relative to REPO_ROOT/PACKAGE_ROOT)
PYTHON_SCRIPT_DIRS = [
    "DriverScript/pythondriver",
    "DriverScript/examples",
    "DriverScript/realdriver",
]

TEMP_FILE_TTL_SECONDS = 3600  # 1 hour


def get_projects_dir() -> Path:
    """Return the active projects directory (from settings or default PROJECTS_DIR)."""
    settings = load_settings()
    custom_root = settings.get("projects_root")
    if custom_root:
        p = Path(custom_root)
        if p.is_dir():
            return p
    return PROJECTS_DIR

# Server ports (overridable via environment variables)
GRPC_PORT = int(os.environ.get("GT_SIM_GRPC_PORT", "50051"))
OSI_GT_PORT = int(os.environ.get("GT_SIM_OSI_GT_PORT", "48198"))
OSI_HVD_PORT = int(os.environ.get("GT_SIM_OSI_HVD_PORT", "48199"))
HTTP_PORT = int(os.environ.get("GT_SIM_HTTP_PORT", "8000"))
SV_LISTEN_PORT = int(os.environ.get("GT_SIM_SV_PORT", "48200"))
SV_MULTICAST_GROUP = os.environ.get("GT_SIM_SV_MULTICAST_GROUP", "239.0.0.1")
SV_MULTICAST_PORT = int(os.environ.get("GT_SIM_SV_MULTICAST_PORT", "48201"))

# Default execution parameters
DEFAULT_EXECUTION_PARAMS: dict[str, Any] = {
    "hz": 120,
    "headless": False,
    "record": False,
    "no_realtime": False,
    "timeout": 60,
    "osi": {"enabled": True, "ip": "127.0.0.1"},
    "autolight": True,
    "vehicle_physics": True,
    "kinematic_mode": False,
    "threads": True,
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


# ---------------------------------------------------------------------------
# Settings persistence
# ---------------------------------------------------------------------------

def _settings_path() -> Path:
    if PACKAGED:
        return PACKAGE_ROOT / "data" / "settings.json"
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
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(params, indent=2, ensure_ascii=False), encoding="utf-8")


def load_thresholds() -> dict[str, Any]:
    if PACKAGED:
        path = CONFIG_DIR / "comparison_thresholds.yaml"
    else:
        path = REPO_ROOT / "GT_esmini" / "test" / "comparison_thresholds.yaml"
    if path.exists():
        return yaml.safe_load(path.read_text(encoding="utf-8")) or {}
    return {}


def save_thresholds(data: dict[str, Any]) -> None:
    if PACKAGED:
        path = CONFIG_DIR / "comparison_thresholds.yaml"
    else:
        path = REPO_ROOT / "GT_esmini" / "test" / "comparison_thresholds.yaml"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        yaml.dump(
            data,
            allow_unicode=True,
            default_flow_style=False,
            width=float("inf"),
            sort_keys=False,
        ),
        encoding="utf-8",
    )
