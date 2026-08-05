"""Manual Drive controller configuration API endpoints."""

from __future__ import annotations

import json
from typing import Any

from fastapi import APIRouter, HTTPException

from GT_esmini.web.backend.config import CONFIG_DIR, load_settings, save_settings
from GT_esmini.web.backend.models.simulation import (
    SDL2_AXIS_KEY_MAP,
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


# feature:F7 -- the wire (GET response / PUT request) shape for this endpoint
# is the NESTED ManualDriveControllerConfig shape (pydantic model / the
# frontend's ManualDriveConfig TS type): sdl2.{device_index,deadzone,
# button_mapping.*}, input_network.{transport_type,port,level},
# physics_network.{host,cmd_port,state_port}, top-level vehicle_params_file,
# override_cfg.*. The ON-DISK file (config/manual_drive.json,
# ManualDriveConfig.cpp's flat line-scanning parser) is FLAT:
# input.{device_index,deadzone,*_button,transport_type,port,level},
# physics.{vehicle_params_file,host,cmd_port,state_port}, override.*.
#
# _wire_to_flat_shape / _flat_to_wire_shape are exact inverses and are the
# ONLY place this translation happens; PUT must call the former before
# writing and GET must call the latter before returning, or the two
# endpoints silently disagree about which shape is truth (see the audit that
# found this: PUT had gap #2's sdl2-only translation but GET had none at
# all, so the "Save" button in ManualDrivePanel.tsx wrote sdl2 correctly but
# every OTHER nested field -- input_network / physics_network /
# vehicle_params_file / override_cfg -- landed as orphan top-level keys C++
# never reads, AND nothing the user saved ever pre-filled on the next
# session: GET returned flat, the frontend only ever reads
# config.sdl2.button_mapping.* / config.input_network.* / etc, found them
# undefined, and silently fell back to DEFAULT_MANUAL_CONFIG every time).
_WIRE_TO_FLAT_TOP_KEYS = (
    "sdl2",
    "input_network",
    "physics_network",
    "vehicle_params_file",
    "override_cfg",
)


def _wire_to_flat_shape(wire: dict[str, Any]) -> dict[str, Any]:
    """Translate an incoming PUT body (nested wire shape) into the flat shape
    the on-disk file needs. Mirrors simulation_runner._write_manual_drive_config,
    which independently reconstructs the same flat shape for the per-run
    config for the same reason (that function cannot reuse this one: it
    builds a whole new file from a full request + base fallback per field,
    this one translates an arbitrary -- possibly partial -- PUT body).

    Flat values already present under "input"/"physics"/"override" win over
    their nested-shape counterparts (an explicit ``input.upshift_button`` is
    more specific than a translated ``sdl2.button_mapping.upshift``) --
    same precedence rule gap #2's original sdl2-only version used.
    """
    out = {k: v for k, v in wire.items() if k not in _WIRE_TO_FLAT_TOP_KEYS}

    input_block: dict[str, Any] = (
        dict(out["input"]) if isinstance(out.get("input"), dict) else {}
    )
    sdl2 = wire.get("sdl2")
    if isinstance(sdl2, dict):
        mapping = sdl2.get("button_mapping")
        if isinstance(mapping, dict):
            for field, value in mapping.items():
                cpp_key = SDL2_BUTTON_KEY_MAP.get(field)
                if cpp_key is not None:
                    input_block.setdefault(cpp_key, value)
        # feature:F8 -- axis mapping, same translation rule as the buttons above
        # (flat keys under "input", derived from the shared table so the key
        # names cannot drift from the model's).
        axis_mapping = sdl2.get("axis_mapping")
        if isinstance(axis_mapping, dict):
            for field, value in axis_mapping.items():
                cpp_key = SDL2_AXIS_KEY_MAP.get(field)
                if cpp_key is not None:
                    input_block.setdefault(cpp_key, value)
        for passthrough in ("device_index", "deadzone"):
            if passthrough in sdl2:
                input_block.setdefault(passthrough, sdl2[passthrough])
    input_network = wire.get("input_network")
    if isinstance(input_network, dict):
        for k in ("transport_type", "port", "level"):
            if k in input_network:
                input_block.setdefault(k, input_network[k])
    if input_block:
        out["input"] = input_block

    physics_block: dict[str, Any] = (
        dict(out["physics"]) if isinstance(out.get("physics"), dict) else {}
    )
    if "vehicle_params_file" in wire:
        physics_block.setdefault("vehicle_params_file", wire["vehicle_params_file"])
    physics_network = wire.get("physics_network")
    if isinstance(physics_network, dict):
        for k in ("transport_type", "host", "cmd_port", "state_port"):
            if k in physics_network:
                physics_block.setdefault(k, physics_network[k])
    if physics_block:
        out["physics"] = physics_block

    override_cfg = wire.get("override_cfg")
    if isinstance(override_cfg, dict):
        merged_override = (
            dict(out["override"]) if isinstance(out.get("override"), dict) else {}
        )
        for k, v in override_cfg.items():
            merged_override.setdefault(k, v)
        out["override"] = merged_override

    return out


def _flat_to_wire_shape(flat: dict[str, Any]) -> dict[str, Any]:
    """Inverse of _wire_to_flat_shape: translate the on-disk flat config into
    the nested wire shape GET returns.

    The source blocks ("input" / "physics" / "override") are DROPPED from the
    output once translated, not kept alongside the nested result. Earlier
    drafts kept both "for safety" -- that was itself a bug: a client that
    GETs, edits ONLY the nested copy, and PUTs the object back would carry
    the untouched flat copy along too, and _wire_to_flat_shape's "explicit
    flat wins over translated-from-nested" precedence (deliberate, so a
    request that states a flat key by hand is never silently overridden by
    an unrelated nested default) would then make the stale flat value win
    over the caller's actual edit. Dropping the source blocks here removes
    the only way that staleness could arise from an ordinary GET->edit->PUT
    round trip.

    Every key currently in "input"/"physics"/"override" is covered by the
    translation below (device_index/deadzone/10 buttons/13 axis keys
    (feature:F8)/transport_type/port/level for "input";
    vehicle_params_file/host/cmd_port/state_port for "physics"; all 6 override
    keys copied verbatim). If a key is EVER added to one of those on-disk blocks
    that isn't one of the names below, add it to the matching translation here
    too -- silently dropping it from the wire shape would be exactly the "saved
    but the GUI can't see it" failure this function exists to prevent, just
    moved one level over.
    """
    out = {k: v for k, v in flat.items() if k not in ("input", "physics", "override")}

    input_block = flat.get("input")
    if isinstance(input_block, dict):
        sdl2: dict[str, Any] = {}
        if "device_index" in input_block:
            sdl2["device_index"] = input_block["device_index"]
        if "deadzone" in input_block:
            sdl2["deadzone"] = input_block["deadzone"]
        button_mapping = {
            field: input_block[cpp_key]
            for field, cpp_key in SDL2_BUTTON_KEY_MAP.items()
            if cpp_key in input_block
        }
        if button_mapping:
            sdl2["button_mapping"] = button_mapping
        # feature:F8 -- inverse of the axis translation in _wire_to_flat_shape.
        # Omitted entirely when the on-disk file predates F8, so the GUI falls
        # back to the model defaults (which are the G29 layout C++ also defaults
        # to) rather than showing a half-populated block.
        axis_mapping = {
            field: input_block[cpp_key]
            for field, cpp_key in SDL2_AXIS_KEY_MAP.items()
            if cpp_key in input_block
        }
        if axis_mapping:
            sdl2["axis_mapping"] = axis_mapping
        if sdl2:
            out["sdl2"] = sdl2

        input_network = {
            k: input_block[k]
            for k in ("transport_type", "port", "level")
            if k in input_block
        }
        if input_network:
            out["input_network"] = input_network

    physics_block = flat.get("physics")
    if isinstance(physics_block, dict):
        if "vehicle_params_file" in physics_block:
            out["vehicle_params_file"] = physics_block["vehicle_params_file"]
        physics_network = {
            k: physics_block[k]
            for k in ("transport_type", "host", "cmd_port", "state_port")
            if k in physics_block
        }
        if physics_network:
            out["physics_network"] = physics_network

    override_block = flat.get("override")
    if isinstance(override_block, dict):
        out["override_cfg"] = dict(override_block)

    return out


@router.get("/config")
async def get_config() -> dict[str, Any]:
    """Read current manual_drive.json configuration, translated to the
    nested wire shape (see _flat_to_wire_shape) so a value the user saved
    actually pre-fills the GUI on the next session instead of silently
    falling back to hardcoded defaults."""
    path = _config_path()
    if path.exists():
        return _flat_to_wire_shape(json.loads(path.read_text(encoding="utf-8")))
    return ManualDriveControllerConfig().model_dump()


@router.put("/config")
async def update_config(config: dict[str, Any]) -> dict[str, Any]:
    """Write manual_drive.json configuration.

    Shape/type-validated via pydantic, then translated to the flat on-disk
    shape (_wire_to_flat_shape) and persisted by deep-merging onto the
    existing on-disk file (or schema defaults if none exists yet) rather
    than replacing the file wholesale. Two reasons the merge matters, both
    belt-and-suspenders with the models' own ``extra="allow"``
    (models/simulation.py):
      - a request that itself omits some on-disk keys (e.g. applying a
        built-in preset, or an older frontend build) must not delete them;
      - the FFB/target-track keys are numerous and evolve independently of
        this endpoint, so "unknown to the model" must never mean "discarded".

    Returns the flat on-disk shape that was actually written (not
    translated back to wire shape) -- callers that want the wire shape back
    should GET again; this keeps the return value an exact reflection of
    what is now on disk, which is what a caller debugging a
    "did my write actually happen" question needs.
    """
    ManualDriveControllerConfig(**config)  # shape/type validation; raises on bad input
    path = _config_path()
    base = (
        json.loads(path.read_text(encoding="utf-8"))
        if path.exists()
        else ManualDriveControllerConfig().model_dump()
    )
    merged = _deep_merge(base, _wire_to_flat_shape(config))
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
