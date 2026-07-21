"""Manual Drive controller configuration API endpoints."""

from __future__ import annotations

import json
from typing import Any

from fastapi import APIRouter, HTTPException

from GT_esmini.web.backend.config import CONFIG_DIR, load_settings, save_settings
from GT_esmini.web.backend.models.simulation import ManualDriveControllerConfig

router = APIRouter(prefix="/api/manual-drive", tags=["manual-drive"])

MANUAL_DRIVE_CONFIG_FILE = "manual_drive.json"

# Built-in presets (not deletable)
BUILTIN_PRESETS: list[dict[str, Any]] = [
    {
        "name": "Full Manual",
        "builtin": True,
        "config": ManualDriveControllerConfig(
            input_type="sdl2_wheel",
            physics_type="real_vehicle",
            ffb_enabled=True,
        ).model_dump(),
    },
    {
        "name": "Steer Only",
        "builtin": True,
        "config": ManualDriveControllerConfig(
            input_type="sdl2_wheel",
            physics_type="real_vehicle",
            ffb_enabled=True,
            domain={"lateral": "manual", "longitudinal": "scenario"},
        ).model_dump(),
    },
    {
        "name": "Network Input",
        "builtin": True,
        "config": ManualDriveControllerConfig(
            input_type="network",
            physics_type="real_vehicle",
        ).model_dump(),
    },
    {
        "name": "External Sim",
        "builtin": True,
        "config": ManualDriveControllerConfig(
            input_type="network",
            physics_type="network",
        ).model_dump(),
    },
]


def _config_path():
    return CONFIG_DIR / MANUAL_DRIVE_CONFIG_FILE


@router.get("/config")
async def get_config() -> dict[str, Any]:
    """Read current manual_drive.json configuration."""
    path = _config_path()
    if path.exists():
        return json.loads(path.read_text(encoding="utf-8"))
    return ManualDriveControllerConfig().model_dump()


@router.put("/config")
async def update_config(config: dict[str, Any]) -> dict[str, Any]:
    """Write manual_drive.json configuration."""
    # Validate via pydantic
    validated = ManualDriveControllerConfig(**config)
    path = _config_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(validated.model_dump(), indent=4, ensure_ascii=False),
        encoding="utf-8",
    )
    return validated.model_dump()


@router.get("/presets")
async def get_presets() -> list[dict[str, Any]]:
    """Get built-in + user-saved presets."""
    settings = load_settings()
    user_presets = settings.get("manual_drive_presets", [])
    return BUILTIN_PRESETS + [{**p, "builtin": False} for p in user_presets]


@router.post("/presets")
async def save_preset(body: dict[str, Any]) -> dict[str, Any]:
    """Save a user preset."""
    name = body.get("name", "").strip()
    config = body.get("config")
    if not name or not config:
        raise HTTPException(status_code=400, detail="name and config required")

    # Validate config
    validated = ManualDriveControllerConfig(**config)

    settings = load_settings()
    presets = settings.get("manual_drive_presets", [])

    # Replace if same name exists
    presets = [p for p in presets if p.get("name") != name]
    presets.append({"name": name, "config": validated.model_dump()})

    settings["manual_drive_presets"] = presets
    save_settings(settings)
    return {"name": name, "config": validated.model_dump()}


@router.delete("/presets/{name}")
async def delete_preset(name: str) -> dict[str, str]:
    """Delete a user preset."""
    settings = load_settings()
    presets = settings.get("manual_drive_presets", [])
    new_presets = [p for p in presets if p.get("name") != name]
    if len(new_presets) == len(presets):
        raise HTTPException(status_code=404, detail=f"Preset '{name}' not found")
    settings["manual_drive_presets"] = new_presets
    save_settings(settings)
    return {"status": "deleted"}
