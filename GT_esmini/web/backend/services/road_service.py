"""Road file management: temporary upload and cleanup."""

from __future__ import annotations

import shutil
import uuid
from datetime import datetime, timedelta, timezone
from pathlib import Path

from GT_esmini.web.backend.config import (
    RESOURCES_DIR,
    TEMP_FILE_TTL_SECONDS,
    TEMP_ROADS_DIR,
)

# Catalog roads shipped with the repo/package. Temp uploads live in TEMP_ROADS_DIR
# and are addressed by their "tmp_road_*" id; catalog roads use their file stem.
CATALOG_ROADS_DIR = RESOURCES_DIR / "xodr"

# Background traffic (feature:F9) needs a matching SUMO config. Committed ones
# live here; generated ones land beside the road under TEMP_ROADS_DIR. A road
# without one simply cannot host traffic -- the UI greys the option out rather
# than failing at run time, because the reason ("this road has no SUMO network")
# is actionable and a mid-run SUMO load failure is not.
SUMO_INPUTS_DIR = RESOURCES_DIR / "sumo_inputs"


def find_sumocfg(road_id: str) -> Path | None:
    """The .sumocfg for a road, or None. Generated configs win over committed ones."""
    for candidate in (
        TEMP_ROADS_DIR / f"{road_id}.sumocfg",
        SUMO_INPUTS_DIR / f"{road_id}.sumocfg",
    ):
        if candidate.is_file():
            return candidate
    return None


def list_roads() -> list[dict]:
    """All selectable OpenDRIVE files: catalog roads first, then temp uploads."""
    roads: list[dict] = []
    for source, directory in (
        ("catalog", CATALOG_ROADS_DIR),
        ("upload", TEMP_ROADS_DIR),
    ):
        if not directory.is_dir():
            continue
        for path in sorted(directory.glob("*.xodr")):
            stat = path.stat()
            roads.append(
                {
                    "road_id": path.stem,
                    "name": path.name,
                    "source": source,
                    "path": str(path),
                    "size": stat.st_size,
                    "sumocfg": (str(cfg) if (cfg := find_sumocfg(path.stem)) else None),
                }
            )
    return roads


def resolve_road_path(road_id: str) -> Path | None:
    """Map a road_id back to its .xodr, or None when it does not exist.

    Rejects anything with path separators so a road_id from a request body cannot
    walk out of the two directories we serve.
    """
    if not road_id or "/" in road_id or "\\" in road_id or road_id.startswith("."):
        return None
    for directory in (CATALOG_ROADS_DIR, TEMP_ROADS_DIR):
        candidate = directory / f"{road_id}.xodr"
        if candidate.is_file():
            return candidate
    return None


def save_temp_road(xml_content: str) -> dict:
    """Save uploaded XODR XML as a temporary road file.

    Returns dict with road_id and road_path.
    """
    road_id = f"tmp_road_{uuid.uuid4().hex[:12]}"
    TEMP_ROADS_DIR.mkdir(parents=True, exist_ok=True)
    road_path = TEMP_ROADS_DIR / f"{road_id}.xodr"
    road_path.write_text(xml_content, encoding="utf-8")

    return {
        "road_id": road_id,
        "road_path": str(road_path),
    }


def delete_temp_road(road_id: str) -> bool:
    """Delete a temporary road file by ID."""
    if not road_id.startswith("tmp_road_"):
        return False
    road_path = TEMP_ROADS_DIR / f"{road_id}.xodr"
    if road_path.is_file():
        road_path.unlink()
        return True
    return False


def cleanup_expired_roads() -> int:
    """Remove temp road files older than TTL. Returns count deleted."""
    if not TEMP_ROADS_DIR.is_dir():
        return 0
    now = datetime.now(timezone.utc)
    count = 0
    for entry in TEMP_ROADS_DIR.iterdir():
        if not entry.is_file() or not entry.name.endswith(".xodr"):
            continue
        created = datetime.fromtimestamp(entry.stat().st_ctime, tz=timezone.utc)
        if (now - created).total_seconds() > TEMP_FILE_TTL_SECONDS:
            entry.unlink(missing_ok=True)
            count += 1
    return count
