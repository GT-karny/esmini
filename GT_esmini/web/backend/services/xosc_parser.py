"""Centralized XOSC parsing utilities.

Extracts entities, road file, controller info, and parameter declarations
from OpenSCENARIO XML.  Used by both project_service and scenario_service
to avoid duplicated parsing logic.
"""

from __future__ import annotations

import dataclasses
import xml.etree.ElementTree as ET
from pathlib import Path


@dataclasses.dataclass
class XoscEntityInfo:
    """A single ScenarioObject extracted from XOSC."""

    name: str
    vehicle_or_model: str | None = None
    controller: str | None = None


@dataclasses.dataclass
class XoscParamInfo:
    """A single ParameterDeclaration extracted from XOSC."""

    name: str
    type: str
    value: str


@dataclasses.dataclass
class XoscParseResult:
    """All metadata extracted from an XOSC file."""

    road_file: str | None = None
    entities: list[XoscEntityInfo] = dataclasses.field(default_factory=list)
    params: list[XoscParamInfo] = dataclasses.field(default_factory=list)
    has_controller: bool = False


def parse_xosc(xosc_path: Path) -> XoscParseResult | None:
    """Parse an XOSC file and extract metadata.

    Returns ``None`` on XML parse error.
    """
    try:
        tree = ET.parse(xosc_path)
        root = tree.getroot()
    except ET.ParseError:
        return None
    return parse_xosc_from_element(root)


def parse_xosc_from_element(root: ET.Element) -> XoscParseResult:
    """Extract metadata from an already-parsed XOSC Element tree."""

    # Road file
    road_file = None
    logic = root.find(".//RoadNetwork/LogicFile")
    if logic is not None:
        road_file = logic.get("filepath", "") or None

    # Entities
    entities: list[XoscEntityInfo] = []
    has_controller = False
    for obj in root.findall(".//ScenarioObject"):
        name = obj.get("name", "Unknown")
        vehicle_el = obj.find("Vehicle")
        catalog_ref = obj.find("CatalogReference")
        vehicle_or_model = None
        if vehicle_el is not None:
            vehicle_or_model = vehicle_el.get("name")
        elif catalog_ref is not None:
            vehicle_or_model = catalog_ref.get("entryName")

        controller = None
        obj_ctrl = obj.find("ObjectController")
        if obj_ctrl is not None:
            ctrl = obj_ctrl.find("Controller")
            if ctrl is not None:
                controller = ctrl.get("name")
                has_controller = True

        entities.append(
            XoscEntityInfo(
                name=name,
                vehicle_or_model=vehicle_or_model,
                controller=controller,
            )
        )

    # ParameterDeclarations (top-level only)
    params: list[XoscParamInfo] = []
    top_pd = root.find("ParameterDeclarations")
    if top_pd is not None:
        for param in top_pd.findall("ParameterDeclaration"):
            params.append(
                XoscParamInfo(
                    name=param.get("name", ""),
                    type=param.get("parameterType", "string"),
                    value=param.get("value", ""),
                )
            )

    return XoscParseResult(
        road_file=road_file,
        entities=entities,
        params=params,
        has_controller=has_controller,
    )
