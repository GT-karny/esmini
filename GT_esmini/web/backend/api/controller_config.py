"""Controller configuration API endpoints."""

from __future__ import annotations

import copy
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
    ]


@router.get("/current")
async def get_current_config() -> dict[str, Any]:
    """Get current default controller configuration."""
    settings = load_settings()
    return settings.get("controller_config", DEFAULT_CONTROLLER_CONFIG)


@router.put("/current")
async def update_current_config(config: dict[str, Any]) -> dict[str, Any]:
    """Update default controller configuration.

    feature:F7 gap #7 -- merged instead of replaced wholesale. The stored
    controller_config holds per-controller sub-objects (``python``, and
    whatever later controllers add); replacing the whole dict meant a PUT that
    only adjusted ``controller_type`` deleted every sub-object with it.

    NOTE the difference from /api/config/execution-defaults (gap #1): that one
    is a FLAT ``dict.update`` -- its nested blocks (``osi``, ``window``) are
    owned entirely by the settings panel, which always sends every sub-field,
    so merging into them would make it impossible to shrink them. Here the
    sub-objects are partial by nature (a PUT may touch only ``python.script``),
    so this merges ONE level deep. Two different endpoints, two different
    rules -- do not "unify" them without re-checking who owns each block.
    Deeper-than-one-level structures are still replaced.
    """
    settings = load_settings()
    stored = settings.get("controller_config")
    # deepcopy on the fallback: DEFAULT_CONTROLLER_CONFIG is a module-level
    # constant, and dict() is shallow -- the "python" sub-dict would be the
    # SAME object as the constant's. Nothing mutates it in place today, but a
    # future in-place edit here would silently rewrite the process-wide
    # default. The stored branch needs no deepcopy: load_settings() re-parses
    # the file (json.loads) on every call, so its sub-dicts are already fresh
    # and owned by nobody else.
    merged = (
        dict(stored)
        if isinstance(stored, dict)
        else copy.deepcopy(DEFAULT_CONTROLLER_CONFIG)
    )
    for key, value in config.items():
        existing = merged.get(key)
        if isinstance(value, dict) and isinstance(existing, dict):
            merged[key] = {**existing, **value}
        else:
            merged[key] = value
    settings["controller_config"] = merged
    save_settings(settings)
    return merged
