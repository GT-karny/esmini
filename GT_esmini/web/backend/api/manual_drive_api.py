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


def _deep_merge(base: dict[str, Any], overlay: dict[str, Any]) -> dict[str, Any]:
    """Recursively merge ``overlay`` onto ``base``.

    Keys present in ``overlay`` win (recursing when both sides hold a dict at
    that key); keys present only in ``base`` are preserved untouched. Used so
    a PUT that only touches e.g. button mapping doesn't erase unrelated
    sections (or unmodeled keys within them) that happen not to be re-sent.
    """
    merged = dict(base)
    for key, value in overlay.items():
        existing = merged.get(key)
        if isinstance(value, dict) and isinstance(existing, dict):
            merged[key] = _deep_merge(existing, value)
        else:
            merged[key] = value
    return merged


@router.get("/config")
async def get_config() -> dict[str, Any]:
    """Read current manual_drive.json configuration."""
    path = _config_path()
    if path.exists():
        return json.loads(path.read_text(encoding="utf-8"))
    return ManualDriveControllerConfig().model_dump()


@router.put("/config")
async def update_config(config: dict[str, Any]) -> dict[str, Any]:
    """Write manual_drive.json configuration.

    Shape/type-validated via pydantic, but persisted by deep-merging the raw
    request onto the existing on-disk file (or schema defaults if none exists
    yet) rather than replacing the file with ``validated.model_dump()``
    wholesale. Two reasons this matters, both belt-and-suspenders with the
    models' own ``extra="allow"`` (models/simulation.py):
      - a request that itself omits some on-disk keys (e.g. applying a
        built-in preset, or an older frontend build) must not delete them;
      - the FFB/target-track keys are numerous and evolve independently of
        this endpoint, so "unknown to the model" must never mean "discarded".
    """
    ManualDriveControllerConfig(**config)  # shape/type validation; raises on bad input
    path = _config_path()
    base = (
        json.loads(path.read_text(encoding="utf-8"))
        if path.exists()
        else ManualDriveControllerConfig().model_dump()
    )
    merged = _deep_merge(base, config)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(merged, indent=4, ensure_ascii=False),
        encoding="utf-8",
    )
    return merged


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
