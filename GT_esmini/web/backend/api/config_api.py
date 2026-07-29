"""Configuration management API endpoints."""

from __future__ import annotations

import copy
from typing import Any

from fastapi import APIRouter, HTTPException

from GT_esmini.web.backend.config import (
    DEFAULT_EXECUTION_PARAMS,
    GT_SIM_EXE,
    REPO_ROOT,
    SCENARIOS_DIR,
    SV_MULTICAST_GROUP,
    SV_MULTICAST_PORT,
    get_projects_dir,
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
    """Update default execution parameters.

    feature:F7 gap #1 -- this used to do ``settings["execution_defaults"] =
    params``, replacing the stored dict wholesale. SettingsPanel.tsx's
    handleSave() builds its payload from the fields it renders, so any key it
    does not render was deleted on every save. ``route_drive_timing`` and
    ``route_drive_gap`` are exactly that case: present in
    DEFAULT_EXECUTION_PARAMS and consumed by _build_cmd()
    (--route-drive-timing / --route-drive-gap), but absent from the panel.
    Saving anything at all in the settings panel dropped them, silently, and
    the next run fell back to "normal"/"normal".

    Merging instead of replacing fixes the whole class: a client that does not
    know about a key can no longer delete it. Keys the client DOES send still
    win, so editing behaviour is unchanged. Nested dicts (``osi``, ``window``)
    are still replaced wholesale by design -- the panel owns them completely
    and sends every sub-field, and a deep merge there would make it impossible
    to ever shrink them.
    """
    settings = load_settings()
    stored = settings.get("execution_defaults")
    # deepcopy on the fallback only -- DEFAULT_EXECUTION_PARAMS is a module
    # constant whose "osi"/"window" sub-dicts a shallow dict() would alias.
    # The stored branch is already fresh (load_settings re-parses the file).
    merged = (
        dict(stored)
        if isinstance(stored, dict)
        else copy.deepcopy(DEFAULT_EXECUTION_PARAMS)
    )
    merged.update(params)
    settings["execution_defaults"] = merged
    save_settings(settings)
    return merged


@router.get("/vehicle-params")
async def get_vehicle_params() -> dict[str, Any]:
    """Get vehicle parameters from real_vehicle_params.json."""
    return load_vehicle_params()


@router.put("/vehicle-params")
async def update_vehicle_params(params: dict[str, Any]) -> dict[str, Any]:
    """Update vehicle parameters.

    feature:F7 gap #7 -- merged onto the stored file rather than replacing it,
    for the same reason as /execution-defaults (gap #1): a client that does not
    know about a key must not be able to delete it. real_vehicle_params.json is
    read by C++ (RealVehicle) and carries far more keys than any current UI
    renders, so a partial PUT used to silently drop the rest.

    No UI calls this today (verified 2026-07-27: zero call sites in client.ts /
    the .tsx components), which is exactly why it is worth fixing now -- the
    damage would start the moment a UI is wired up, and would look like the
    vehicle "forgetting" its tuning.
    """
    merged = {**load_vehicle_params(), **params}
    save_vehicle_params(merged)
    return merged


@router.get("/thresholds")
async def get_thresholds() -> dict[str, Any]:
    """Get comparison thresholds."""
    return load_thresholds()


@router.put("/thresholds")
async def update_thresholds(data: dict[str, Any]) -> dict[str, Any]:
    """Update comparison thresholds.

    feature:F7 gap #7 -- merged, not replaced (see update_vehicle_params).
    comparison_thresholds.yaml is shared with the verification tooling, so a
    partial PUT from a future UI must not delete the thresholds it does not
    render.
    """
    merged = {**load_thresholds(), **data}
    save_thresholds(merged)
    return merged


@router.get("/projects-root")
async def get_projects_root() -> dict[str, Any]:
    """Get the current projects root directory setting."""
    settings = load_settings()
    custom = settings.get("projects_root")
    return {
        "projects_root": custom,
        "effective_dir": str(get_projects_dir()),
        "is_custom": custom is not None,
    }


@router.put("/projects-root")
async def set_projects_root(body: dict[str, Any]) -> dict[str, Any]:
    """Set or clear the projects root directory."""
    from pathlib import Path

    from GT_esmini.web.backend.services.project_service import sync_projects

    new_root = body.get("projects_root")
    settings = load_settings()

    if new_root is None:
        settings.pop("projects_root", None)
    else:
        p = Path(new_root)
        if not p.is_dir():
            raise HTTPException(
                status_code=400, detail=f"Directory does not exist: {new_root}"
            )
        settings["projects_root"] = str(p.resolve())

    save_settings(settings)

    # Trigger sync after changing root
    await sync_projects()

    return {
        "projects_root": settings.get("projects_root"),
        "effective_dir": str(get_projects_dir()),
    }


@router.get("/system")
async def get_system_info() -> dict[str, Any]:
    """Get system information."""
    return {
        "gt_sim_path": str(GT_SIM_EXE),
        "gt_sim_exists": GT_SIM_EXE.exists(),
        "repo_root": str(REPO_ROOT),
        "scenarios_dir": str(SCENARIOS_DIR),
        "scenarios_count": (
            len(list(SCENARIOS_DIR.glob("*.xosc"))) if SCENARIOS_DIR.is_dir() else 0
        ),
        "sv_multicast_group": SV_MULTICAST_GROUP,
        "sv_multicast_port": SV_MULTICAST_PORT,
    }
