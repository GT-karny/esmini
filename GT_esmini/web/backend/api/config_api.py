"""Configuration management API endpoints."""

from __future__ import annotations

from typing import Any

from fastapi import APIRouter

from GT_esmini.web.backend.config import (
    DEFAULT_EXECUTION_PARAMS,
    GT_SIM_EXE,
    REPO_ROOT,
    SCENARIOS_DIR,
    load_settings,
    load_thresholds,
    load_vehicle_params,
    save_settings,
    save_thresholds,
    save_vehicle_params,
)

router = APIRouter(prefix="/api/config", tags=["config"])


@router.get("/execution-defaults")
async def get_execution_defaults() -> dict[str, Any]:
    """Get default execution parameters."""
    settings = load_settings()
    defaults = settings.get("execution_defaults", DEFAULT_EXECUTION_PARAMS)
    # Migrate old array-format window to dict format
    w = defaults.get("window")
    if isinstance(w, list) and len(w) == 4:
        defaults["window"] = {"x": w[0], "y": w[1], "w": w[2], "h": w[3]}
    return defaults


@router.put("/execution-defaults")
async def update_execution_defaults(params: dict[str, Any]) -> dict[str, Any]:
    """Update default execution parameters."""
    settings = load_settings()
    settings["execution_defaults"] = params
    save_settings(settings)
    return params


@router.get("/vehicle-params")
async def get_vehicle_params() -> dict[str, Any]:
    """Get vehicle parameters from real_vehicle_params.json."""
    return load_vehicle_params()


@router.put("/vehicle-params")
async def update_vehicle_params(params: dict[str, Any]) -> dict[str, Any]:
    """Update vehicle parameters."""
    save_vehicle_params(params)
    return params


@router.get("/thresholds")
async def get_thresholds() -> dict[str, Any]:
    """Get comparison thresholds."""
    return load_thresholds()


@router.put("/thresholds")
async def update_thresholds(data: dict[str, Any]) -> dict[str, Any]:
    """Update comparison thresholds."""
    save_thresholds(data)
    return data


@router.get("/system")
async def get_system_info() -> dict[str, Any]:
    """Get system information."""
    return {
        "gt_sim_path": str(GT_SIM_EXE),
        "gt_sim_exists": GT_SIM_EXE.exists(),
        "repo_root": str(REPO_ROOT),
        "scenarios_dir": str(SCENARIOS_DIR),
        "scenarios_count": len(list(SCENARIOS_DIR.glob("*.xosc"))) if SCENARIOS_DIR.is_dir() else 0,
    }
