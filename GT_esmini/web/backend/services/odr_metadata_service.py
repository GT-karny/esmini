"""OpenDRIVE side-model metadata extraction service using GT_esminiLib.

Loads an OpenDRIVE (.xodr) file via GT_esminiLib (the GT fork's metadata C API,
exposed through the GtOdrMetadataLib ctypes wrapper in rm_lib.py) and surfaces
the "side-model" metadata the GT ODR 1.6-1.9 fork records during parsing:

- parse/audit warnings (unsupported constructs, removed-in-1.6 hits),
- raw <userData> / <dataQuality> blobs,
- traffic-signal semantics (speeds, lane/priority types, prohibited, deps/refs),
- junction priority pairs,
- crosswalk (crossPath) synthesis records,
- railroad switches + stations (inert; stored only, no runtime effect).

Results are cached per (absolute xodr path, mtime). The DLL is a process-global
singleton (one xodr Init'd at a time), mirroring road_geometry_service.

The wrapper is imported lazily inside the function so a missing DLL / not-yet-
landed wrapper cannot crash the server at import time; callers get a structured
"unavailable" signal instead (raised as MetadataUnavailable → HTTP 503).
"""

from __future__ import annotations

import logging
import threading
from pathlib import Path

from GT_esmini.web.backend.config import GT_ESMINI_LIB

logger = logging.getLogger(__name__)

# GT_esminiLib holds one Init'd OpenDRIVE at a time — serialize access.
_lock = threading.Lock()

# Cache: xodr absolute path → (mtime, metadata dict)
_cache: dict[str, tuple[float, dict]] = {}


class MetadataUnavailable(RuntimeError):
    """Raised when the metadata DLL / wrapper is unavailable or Init failed.

    The API layer maps this to HTTP 503 with the message as detail.
    """


def _empty_metadata() -> dict:
    return {
        "warnings": {
            "version": {"rev_major": 0, "rev_minor": 0},
            "unsupported_elements": 0,
            "unsupported_attributes": 0,
            "removed16_hits": 0,
            "entries": [],
        },
        "user_data": [],
        "data_quality": [],
        "signals": [],
        "junction_priorities": [],
        "crosswalks": [],
        "railroad": {"switches": [], "stations": []},
        "lane_layers": {"mode": "permanent", "roads": []},
        "virtual_junctions": [],
    }


def _dedupe_user_data(items: list[dict]) -> list[dict]:
    """Dedupe userData/dataQuality rows on (owner_path, context_id, xml).

    Parse order may repeat identical blobs (dual-homed roads, re-visits); the
    UI wants each distinct blob once. Order-preserving.
    """
    seen: set[tuple] = set()
    out: list[dict] = []
    for it in items or []:
        key = (
            it.get("owner_path", ""),
            it.get("context_id", ""),
            it.get("xml", ""),
        )
        if key in seen:
            continue
        seen.add(key)
        out.append(it)
    return out


def extract_odr_metadata(xodr_path: str | Path) -> dict:
    """Extract ODR side-model metadata from an OpenDRIVE file.

    Returns a dict with keys: warnings, user_data, data_quality, signals,
    junction_priorities, crosswalks, railroad. Cached per (abs path, mtime).

    Raises MetadataUnavailable if the DLL/wrapper is missing or Init fails —
    the endpoint translates that into HTTP 503 so the caller can degrade
    gracefully rather than see a bogus "empty" result.
    """
    xodr_path = Path(xodr_path).resolve()
    cache_key = str(xodr_path)

    if not xodr_path.is_file():
        # Missing input is a client error, not a DLL problem — surface empty.
        logger.warning("xodr file not found: %s", xodr_path)
        return _empty_metadata()

    mtime = xodr_path.stat().st_mtime
    cached = _cache.get(cache_key)
    if cached is not None and cached[0] == mtime:
        return cached[1]

    lib_path = str(GT_ESMINI_LIB)
    if not Path(lib_path).is_file():
        logger.warning("GT_esminiLib not found: %s", lib_path)
        raise MetadataUnavailable(
            f"GT_esminiLib.dll not available ({lib_path}); "
            "ODR metadata cannot be extracted."
        )

    # Import lazily to avoid import errors when the DLL / wrapper is missing.
    # GT_SCRIPTS_DIR (GT_esmini/scripts/) is on sys.path via config.py; rm_lib.py
    # lives there and (per the P9a contract) exposes GtOdrMetadataLib.
    try:
        from rm_lib import GtOdrMetadataLib  # type: ignore[attr-defined]
    except Exception as exc:  # ImportError or AttributeError (wrapper not landed)
        logger.warning("GtOdrMetadataLib wrapper unavailable: %s", exc)
        raise MetadataUnavailable(
            "GtOdrMetadataLib wrapper is not available yet "
            f"(rm_lib import failed: {exc})."
        ) from exc

    with _lock:
        lib = None
        try:
            lib = GtOdrMetadataLib(lib_path)
            rc = lib.Init(str(xodr_path))
            if rc != 0:
                logger.error(
                    "GtOdrMetadataLib.Init failed for %s (rc=%s)", xodr_path, rc
                )
                raise MetadataUnavailable(
                    f"Failed to load OpenDRIVE via GT_esminiLib (rc={rc})."
                )

            audit = _safe_call(lib, "GetOdrAuditJson", {})
            user_json = _safe_call(lib, "GetUserDataJson", {})
            signals = _safe_call(lib, "GetSignalSemanticsJson", {})
            junctions = _safe_call(lib, "GetJunctionPrioritiesJson", {})
            crosswalks = _safe_call(lib, "GetCrosswalksJson", {})
            railroad = _safe_call(lib, "GetRailroadJson", {})
            lane_layers = _safe_call(lib, "GetLaneLayersJson", {})
            virtual_junctions = _safe_call(lib, "GetVirtualJunctionsJson", {})

            result = {
                "warnings": audit or _empty_metadata()["warnings"],
                "user_data": _dedupe_user_data(user_json.get("user_data", [])),
                "data_quality": _dedupe_user_data(user_json.get("data_quality", [])),
                "signals": signals.get("signals", []),
                "junction_priorities": junctions.get("junctions", []),
                "crosswalks": crosswalks.get("cross_paths", []),
                "railroad": {
                    "switches": railroad.get("switches", []),
                    "stations": railroad.get("stations", []),
                },
                # P9b: 1.9 lane layers (P8 shadow storage + process mode latch)
                # and virtual-junction metadata (P6). Both degrade to empty when
                # the DLL predates the exports (_safe_call / wrapper {}-fallback).
                "lane_layers": {
                    "mode": lane_layers.get("mode", "permanent"),
                    "roads": lane_layers.get("roads", []),
                },
                "virtual_junctions": virtual_junctions.get("virtual_junctions", []),
            }
        except MetadataUnavailable:
            raise
        except Exception as exc:
            logger.exception("Failed to extract ODR metadata from %s", xodr_path)
            raise MetadataUnavailable(f"ODR metadata extraction failed: {exc}") from exc
        finally:
            if lib is not None:
                try:
                    lib.Close()
                except Exception:
                    pass

    _cache[cache_key] = (mtime, result)
    logger.info(
        "Extracted ODR metadata from %s: %d warnings, %d userData, %d signals, "
        "%d junctions, %d crosswalks, %d switches",
        xodr_path.name,
        len(result["warnings"].get("entries", [])),
        len(result["user_data"]),
        len(result["signals"]),
        len(result["junction_priorities"]),
        len(result["crosswalks"]),
        len(result["railroad"]["switches"]),
    )
    return result


def _safe_call(lib, method_name: str, default: dict) -> dict:
    """Call an optional wrapper accessor, tolerating a missing method.

    Individual accessors may not all be landed simultaneously; a missing one
    degrades that section to empty rather than failing the whole request.
    """
    fn = getattr(lib, method_name, None)
    if fn is None:
        logger.debug("GtOdrMetadataLib.%s not available", method_name)
        return default
    try:
        val = fn()
        return val if isinstance(val, dict) else default
    except Exception:
        logger.debug("GtOdrMetadataLib.%s raised", method_name, exc_info=True)
        return default


def clear_cache() -> None:
    """Clear all cached ODR metadata."""
    _cache.clear()
