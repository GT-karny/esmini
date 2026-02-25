"""Controller configuration API endpoints."""

from __future__ import annotations

from typing import Any

from fastapi import APIRouter

from GT_esmini.web.backend.config import (
    DEFAULT_CONTROLLER_CONFIG,
    load_settings,
    save_settings,
)

router = APIRouter(prefix="/api/controller-config", tags=["controller-config"])


@router.get("/presets")
async def get_presets() -> list[dict[str, Any]]:
    """Get predefined controller configuration presets."""
    return [
        {
            "name": "Default Controller",
            "description": "esmini built-in defaultController()",
            "config": {
                "controller_type": "default",
                "python": DEFAULT_CONTROLLER_CONFIG["python"],
            },
        },
        {
            "name": "Python Driver (Recommended)",
            "description": "PythonDriverController with EmbeddedController",
            "config": {
                "controller_type": "python",
                "python": {
                    "script": "DriverScript/pythondriver/scenario_drive_embedded.py",
                    "class": "EmbeddedController",
                    "python_home": "",
                    "trace_enabled": True,
                    "trace_dir": "",
                },
            },
        },
    ]


@router.get("/current")
async def get_current_config() -> dict[str, Any]:
    """Get current default controller configuration."""
    settings = load_settings()
    return settings.get("controller_config", DEFAULT_CONTROLLER_CONFIG)


@router.put("/current")
async def update_current_config(config: dict[str, Any]) -> dict[str, Any]:
    """Update default controller configuration."""
    settings = load_settings()
    settings["controller_config"] = config
    save_settings(settings)
    return config
