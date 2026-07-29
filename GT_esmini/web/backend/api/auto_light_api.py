"""AutoLight (F6 environment-driven headlights) configuration API endpoints.

Mirrors ``manual_drive_api`` (audit WEB-*): reads/writes the shipped config file
``CONFIG_DIR / auto_light.json`` — the same file GT_Sim.exe reads at runtime via
``ConfigLoader::ResolveConfigPath`` (``<exe_dir>/../config/``). In the packaged
distribution ``CONFIG_DIR`` IS that directory, so GUI edits take effect directly;
in a dev build the CMake POST_BUILD step syncs ``GT_esmini/config`` →
``build/GT_esmini/config`` on the next rebuild (identical to manual_drive.json /
real_vehicle_params.json). There is no per-run ConfigFile override for AutoLight
in the C++ (unlike ManualDrive/VirtualDriver), so this endpoint edits the one
shared config; the ``--autolight-headlights`` CLI flag force-enables at run time.
"""

from __future__ import annotations

import json
from typing import Any

from fastapi import APIRouter, HTTPException

from GT_esmini.web.backend.config import CONFIG_DIR

router = APIRouter(prefix="/api/auto-light", tags=["auto-light"])

AUTO_LIGHT_CONFIG_FILE = "auto_light.json"

# Known editable keys, split by expected JSON type. ``bool`` is validated before
# ``number`` because in Python ``bool`` is a subclass of ``int`` — a boolean must
# never satisfy a numeric field, nor vice versa.
_BOOL_KEYS = frozenset(
    {
        "headlight_enabled",
        "headlight_use_time_of_day",
        "headlight_tunnel_enabled",
        "highbeam_enabled",
    }
)
_NUMBER_KEYS = frozenset(
    {
        "headlight_illuminance_lux_threshold",
        "headlight_sun_elevation_deg",
        "headlight_dusk_hour",
        "headlight_dawn_hour",
        "highbeam_range_m",
        "highbeam_range_hysteresis_m",
        "highbeam_corridor_half_width_m",
        "highbeam_on_delay_s",
        "highbeam_off_delay_s",
    }
)
KNOWN_KEYS = _BOOL_KEYS | _NUMBER_KEYS

# Shipping defaults — mirror GT_esmini/config/auto_light.json, comment ("// ")
# keys included so a freshly-written file stays self-documenting. Single source of
# truth for both the GET /config fallback (file absent) and GET /defaults (reset).
DEFAULT_AUTO_LIGHT_CONFIG: dict[str, Any] = {
    "// F6": "Environment-driven headlights (night / tunnel low beam + auto high beam).",
    "// safety": "headlight_enabled defaults false so existing AutoLight behaviour is unchanged.",
    "// enable": "Set headlight_enabled true here, or pass --autolight-headlights on the CLI.",
    "headlight_enabled": False,
    "// night": "Low beam ON when it is night. Priority: illuminance > sun elevation > time-of-day.",
    "headlight_illuminance_lux_threshold": 3000.0,
    "headlight_sun_elevation_deg": 0.0,
    "headlight_use_time_of_day": True,
    "headlight_dusk_hour": 19.0,
    "headlight_dawn_hour": 6.0,
    "// tunnel": "Low beam ON while the vehicle s-position is inside an OpenDRIVE <tunnel>.",
    "headlight_tunnel_enabled": True,
    "// highbeam": "High beam while low beam is on and no vehicle is ahead within range (hysteresis).",
    "highbeam_enabled": True,
    "highbeam_range_m": 120.0,
    "highbeam_range_hysteresis_m": 20.0,
    "highbeam_corridor_half_width_m": 6.0,
    "highbeam_on_delay_s": 1.5,
    "highbeam_off_delay_s": 0.3,
}


def _config_path():
    return CONFIG_DIR / AUTO_LIGHT_CONFIG_FILE


def _read_config() -> dict[str, Any]:
    """Return the on-disk config (comments included), or the shipped defaults."""
    path = _config_path()
    if path.exists():
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
            if isinstance(data, dict):
                return data
        except (json.JSONDecodeError, OSError):
            pass
    return dict(DEFAULT_AUTO_LIGHT_CONFIG)


def _coerce(key: str, value: Any) -> Any:
    """Type-check a single known key; return the value to persist.

    bool keys accept only JSON booleans; number keys accept int/float but reject
    booleans. Raises HTTPException(422) on mismatch.
    """
    if key in _BOOL_KEYS:
        if not isinstance(value, bool):
            raise HTTPException(status_code=422, detail=f"'{key}' must be a boolean")
        return value
    # number key
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise HTTPException(status_code=422, detail=f"'{key}' must be a number")
    return float(value)


@router.get("/config")
async def get_config() -> dict[str, Any]:
    """Read current auto_light.json (F6 headlight rule) configuration.

    Includes the "// ..." comment keys (spec documentation). Falls back to the
    shipped defaults when the file is absent.
    """
    return _read_config()


@router.get("/defaults")
async def get_defaults() -> dict[str, Any]:
    """Return the factory-default auto_light.json values (for the Reset button)."""
    return dict(DEFAULT_AUTO_LIGHT_CONFIG)


@router.put("/config")
async def update_config(patch: dict[str, Any]) -> dict[str, Any]:
    """Write auto_light.json.

    - Only known ``headlight_*`` / ``highbeam_*`` keys are accepted; any other
      non-comment key is rejected (422).
    - Each value is type-checked (bool / number).
    - "// ..." comment keys are preserved: we start from the existing file (or the
      shipped defaults) and overwrite only the supplied known keys, so the
      self-documenting comments survive the round-trip. Incoming comment keys are
      ignored (the server keeps its own), so a client cannot inject arbitrary text.
    """
    for key in patch:
        if key.startswith("// "):
            continue
        if key not in KNOWN_KEYS:
            raise HTTPException(status_code=422, detail=f"Unknown key: '{key}'")

    merged = _read_config()
    for key, value in patch.items():
        if key.startswith("// "):
            continue
        merged[key] = _coerce(key, value)

    path = _config_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(merged, indent=4, ensure_ascii=False),
        encoding="utf-8",
    )
    return merged
