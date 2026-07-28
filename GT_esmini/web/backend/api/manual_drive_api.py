"""Manual Drive controller configuration API endpoints."""

from __future__ import annotations

import json
from typing import Any

from fastapi import APIRouter, HTTPException

from GT_esmini.web.backend.config import CONFIG_DIR, load_settings, save_settings
from GT_esmini.web.backend.models.simulation import (
    SDL2_BUTTON_KEY_MAP,
    ManualDriveControllerConfig,
)

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


def _normalize_sdl2_to_cpp_shape(config: dict[str, Any]) -> dict[str, Any]:
    """Translate the GUI's nested ``sdl2`` block into the flat shape C++ reads.

    feature:F7 gap #2. The frontend sends ``sdl2.button_mapping.upshift``;
    ManualDriveConfig.cpp reads ``upshift_button`` and does so with a line-wise
    substring scan that has no notion of JSON nesting. Deep-merging the nested
    request straight onto the flat on-disk file therefore did two bad things:

      - the remapped button never reached C++ (the shipped symptom: "I
        reassigned it in the GUI and nothing changed"); and
      - ``"button_mapping": {"upshift": 4}`` collides with
        ManualDriveKeyboardConfig's identically-named ``upshift``. Because the
        scan takes the LAST matching line in the file and the appended block
        sorts after ``keyboard``, the keyboard binding was overwritten with a
        number -- an invalid SDL scancode name. Eight bindings were exposed
        this way (upshift, downshift, indicator_left/right, headlight,
        high_beam, fog_light, hazard).

    Translating here keeps the persisted file in exactly the shape C++ expects
    and keeps the nested block out of it entirely, so neither failure can
    occur. Values already sent in flat form win over the nested ones (an
    explicit ``input.upshift_button`` is more specific than a translated one).
    """
    sdl2 = config.get("sdl2")
    if not isinstance(sdl2, dict):
        return config

    translated: dict[str, Any] = {}
    mapping = sdl2.get("button_mapping")
    if isinstance(mapping, dict):
        for field, value in mapping.items():
            cpp_key = SDL2_BUTTON_KEY_MAP.get(field)
            if cpp_key is not None:
                translated[cpp_key] = value
    for passthrough in ("device_index", "deadzone"):
        if passthrough in sdl2:
            translated[passthrough] = sdl2[passthrough]

    if not translated:
        return config

    out = {k: v for k, v in config.items() if k != "sdl2"}
    existing_input = out.get("input")
    merged_input = dict(existing_input) if isinstance(existing_input, dict) else {}
    # Flat wins: only fill keys the caller did not already state flatly.
    for k, v in translated.items():
        merged_input.setdefault(k, v)
    out["input"] = merged_input
    return out


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
    # feature:F7 gap #2 -- normalize BEFORE merging, so the nested sdl2 block
    # never lands in the file C++ parses. See _normalize_sdl2_to_cpp_shape.
    merged = _deep_merge(base, _normalize_sdl2_to_cpp_shape(config))
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
