"""Road file management: temporary upload and cleanup."""

from __future__ import annotations

import shutil
import uuid
from datetime import datetime, timedelta, timezone
from pathlib import Path

from GT_esmini.web.backend.config import TEMP_FILE_TTL_SECONDS, TEMP_ROADS_DIR


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
