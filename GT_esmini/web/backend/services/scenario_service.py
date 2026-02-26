"""Scenario management: scan XOSC files and parse details."""

from __future__ import annotations

import xml.etree.ElementTree as ET
from datetime import datetime, timezone
from pathlib import Path

from GT_esmini.web.backend.config import REPO_ROOT, SCENARIOS_DIR
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
    """Resolve scenario ID to absolute path."""
    xosc_path = SCENARIOS_DIR / f"{scenario_id}.xosc"
    return xosc_path if xosc_path.exists() else None
