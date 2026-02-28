"""Scenario management: scan XOSC files and parse details."""

from __future__ import annotations

import shutil
import uuid
import xml.etree.ElementTree as ET
from datetime import datetime, timedelta, timezone
from pathlib import Path

from GT_esmini.web.backend.config import REPO_ROOT, SCENARIOS_DIR, TEMP_FILE_TTL_SECONDS, TEMP_SCENARIOS_DIR
from GT_esmini.web.backend.models.scenario import (
    ScenarioDetail,
    ScenarioEntity,
    ScenarioListItem,
)


def list_scenarios(search: str | None = None) -> list[ScenarioListItem]:
    """Scan resources/xosc/ for XOSC files."""
    results: list[ScenarioListItem] = []
    if not SCENARIOS_DIR.is_dir():
        return results

    for xosc in sorted(SCENARIOS_DIR.glob("*.xosc")):
        if xosc.name.endswith(".temp.xosc"):
            continue
        if search and search.lower() not in xosc.stem.lower():
            continue
        stat = xosc.stat()
        results.append(
            ScenarioListItem(
                id=xosc.stem,
                filename=xosc.name,
                path=str(xosc.relative_to(REPO_ROOT)),
                modified=datetime.fromtimestamp(stat.st_mtime, tz=timezone.utc).isoformat(),
                size=stat.st_size,
            )
        )
    return results


def get_scenario_detail(scenario_id: str) -> ScenarioDetail | None:
    """Parse XOSC to extract entities, road file, and controller info."""
    xosc_path = SCENARIOS_DIR / f"{scenario_id}.xosc"
    if not xosc_path.exists():
        return None

    try:
        tree = ET.parse(xosc_path)
        root = tree.getroot()
    except ET.ParseError:
        return ScenarioDetail(
            id=scenario_id,
            filename=xosc_path.name,
            path=str(xosc_path.relative_to(REPO_ROOT)),
        )

    # Extract road file
    road_file = None
    logic = root.find(".//RoadNetwork/LogicFile")
    if logic is not None:
        road_file = logic.get("filepath", "")

    # Extract entities
    entities: list[ScenarioEntity] = []
    has_controller = False
    for obj in root.findall(".//ScenarioObject"):
        name = obj.get("name", "Unknown")
        vehicle_el = obj.find("Vehicle")
        catalog_ref = obj.find("CatalogReference")
        vehicle = None
        if vehicle_el is not None:
            vehicle = vehicle_el.get("name")
        elif catalog_ref is not None:
            vehicle = catalog_ref.get("entryName")

        controller = None
        obj_ctrl = obj.find("ObjectController")
        if obj_ctrl is not None:
            ctrl = obj_ctrl.find("Controller")
            if ctrl is not None:
                controller = ctrl.get("name")
                has_controller = True

        entities.append(ScenarioEntity(name=name, vehicle=vehicle, controller=controller))

    return ScenarioDetail(
        id=scenario_id,
        filename=xosc_path.name,
        path=str(xosc_path.relative_to(REPO_ROOT)),
        road_file=road_file,
        entities=entities,
        has_controller=has_controller,
    )


def get_scenario_path(scenario_id: str) -> Path | None:
    """Resolve scenario ID to absolute path, including temp uploads."""
    if scenario_id.startswith("tmp_"):
        xosc_path = TEMP_SCENARIOS_DIR / scenario_id / f"{scenario_id}.xosc"
        return xosc_path if xosc_path.exists() else None
    xosc_path = SCENARIOS_DIR / f"{scenario_id}.xosc"
    return xosc_path if xosc_path.exists() else None


def save_temp_scenario(xml_content: str) -> dict:
    """Save uploaded XOSC XML as a temporary scenario.

    Returns dict with scenario_id, entities, road_file, expires_at.
    """
    scenario_id = f"tmp_{uuid.uuid4().hex[:12]}"
    scenario_dir = TEMP_SCENARIOS_DIR / scenario_id
    scenario_dir.mkdir(parents=True, exist_ok=True)
    xosc_path = scenario_dir / f"{scenario_id}.xosc"

    # Parse XML to extract entities and road file, and absolutize paths
    root = ET.fromstring(xml_content)

    # Absolutize RoadNetwork/LogicFile path relative to SCENARIOS_DIR
    # (since the temp directory won't have the correct relative path structure)
    logic = root.find(".//RoadNetwork/LogicFile")
    road_file = None
    if logic is not None:
        filepath = logic.get("filepath", "")
        if filepath:
            road_file = filepath
            # If relative path, make absolute relative to SCENARIOS_DIR
            road_path = Path(filepath)
            if not road_path.is_absolute():
                abs_path = (SCENARIOS_DIR / filepath).resolve()
                logic.set("filepath", str(abs_path))

    # Extract entities
    entities = []
    for obj in root.findall(".//ScenarioObject"):
        name = obj.get("name", "Unknown")
        vehicle_el = obj.find("Vehicle")
        catalog_ref = obj.find("CatalogReference")
        model = None
        if vehicle_el is not None:
            model = vehicle_el.get("name")
        elif catalog_ref is not None:
            model = catalog_ref.get("entryName")
        entities.append({"name": name, "model": model})

    # Also absolutize CatalogLocations paths
    for catalog_dir in root.findall(".//CatalogLocations/*/Directory"):
        dirpath = catalog_dir.get("path", "")
        if dirpath and not Path(dirpath).is_absolute():
            abs_path = (SCENARIOS_DIR / dirpath).resolve()
            catalog_dir.set("path", str(abs_path))

    # Write the (possibly path-modified) XML
    tree = ET.ElementTree(root)
    tree.write(str(xosc_path), encoding="unicode", xml_declaration=True)

    expires_at = (datetime.now(timezone.utc) + timedelta(seconds=TEMP_FILE_TTL_SECONDS)).isoformat()

    return {
        "scenario_id": scenario_id,
        "entities": entities,
        "road_file": road_file,
        "expires_at": expires_at,
    }


def delete_temp_scenario(scenario_id: str) -> bool:
    """Delete a temporary scenario by ID."""
    if not scenario_id.startswith("tmp_"):
        return False
    scenario_dir = TEMP_SCENARIOS_DIR / scenario_id
    if scenario_dir.is_dir():
        shutil.rmtree(scenario_dir, ignore_errors=True)
        return True
    return False


def cleanup_expired_scenarios() -> int:
    """Remove temp scenarios older than TTL. Returns count deleted."""
    if not TEMP_SCENARIOS_DIR.is_dir():
        return 0
    now = datetime.now(timezone.utc)
    count = 0
    for entry in TEMP_SCENARIOS_DIR.iterdir():
        if not entry.is_dir():
            continue
        # Use directory creation time
        created = datetime.fromtimestamp(entry.stat().st_ctime, tz=timezone.utc)
        if (now - created).total_seconds() > TEMP_FILE_TTL_SECONDS:
            shutil.rmtree(entry, ignore_errors=True)
            count += 1
    return count
