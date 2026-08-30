"""Scenario builder tests (Track A2).

Pure XML assembly -- no DLL needed, so these run everywhere. The behavioural proof
that the generated file actually drives (GT_Sim EXIT=0, and the RouteLanePlan
warning appearing with lane_change_initiation OFF and vanishing with it ON) lives
in the route_lane regression batch, not here.
"""

from __future__ import annotations

import xml.etree.ElementTree as ET

import pytest

from GT_esmini.web.backend.config import RESOURCES_DIR
from GT_esmini.web.backend.services.scenario_builder_service import (
    ScenarioBuildError,
    build_route_scenario,
)

XODR = RESOURCES_DIR / "xodr" / "highway_example_with_merge_and_split.xodr"

# The exit-ramp route as route_planner_service reports it: the router's first
# waypoint names lane -4 (the lane that must be REACHED on road 0), while the
# vehicle actually starts in lane -3 where the user clicked.
ROUTER_WAYPOINTS = [
    {"road_id": 0, "lane_id": -4, "s": 125.0, "junction_id": None, "h": 0.0},
    {"road_id": 4, "lane_id": -1, "s": 50.0, "junction_id": 1, "h": 0.0},
    {"road_id": 2, "lane_id": -1, "s": 40.0, "junction_id": None, "h": 0.0},
]
CLICKED_START = {"road_id": 0, "lane_id": -3, "s": 10.0, "h": 0.0}


def _parse(xml_str: str) -> ET.Element:
    return ET.fromstring(xml_str)


def _lane_positions(root: ET.Element, xpath: str) -> list[tuple[int, int]]:
    return [
        (int(lp.get("roadId")), int(lp.get("laneId")))
        for lp in root.findall(xpath + "//LanePosition")
    ]


@pytest.fixture
def xml_str() -> str:
    return build_route_scenario(
        XODR,
        ROUTER_WAYPOINTS,
        start=CLICKED_START,
        ego_speed=13.889,
        policies=["lane_change_initiation"],
        route_length=330.0,
    )


def test_teleports_to_the_clicked_start_not_the_first_waypoint(xml_str):
    """The regression this file exists for.

    Teleporting to waypoints[0] would put the vehicle in lane -4 -- already past
    the lane change the route needs -- so the manoeuvre under test would silently
    never happen and the scenario would still "pass". The start must be lane -3.
    """
    root = _parse(xml_str)
    teleport = root.find(".//TeleportAction//LanePosition")
    assert (int(teleport.get("roadId")), int(teleport.get("laneId"))) == (0, -3)


def test_route_starts_at_the_start_and_keeps_the_rest_of_the_chain(xml_str):
    """Route waypoints: clicked start, then the router's chain minus its own
    start-road entry. RouteLanePlan derives road 0's {-4} target band from lane
    connectivity to road 4, so naming -3 here still produces the lane change."""
    root = _parse(xml_str)
    route = _lane_positions(root, ".//AssignRouteAction/Route")
    assert route == [(0, -3), (4, -1), (2, -1)]


def test_policies_property_is_written(xml_str):
    """simulation_runner._generate_virtual_driver_variant reads this Property to
    build the per-run config; without it a default-OFF feature stays off."""
    root = _parse(xml_str)
    props = {
        p.get("name"): p.get("value")
        for p in root.findall(".//ObjectController/Controller/Properties/Property")
    }
    assert props["esminiController"] == "VirtualDriverController"
    assert props["policies"] == "lane_change_initiation"


def test_no_policies_property_when_none_requested():
    root = _parse(
        build_route_scenario(XODR, ROUTER_WAYPOINTS, start=CLICKED_START, policies=[])
    )
    names = [
        p.get("name")
        for p in root.findall(".//ObjectController/Controller/Properties/Property")
    ]
    assert "policies" not in names


def test_paths_are_absolute(xml_str):
    """Generated files live in TEMP_SCENARIOS_DIR, far from resources/, and
    absolutize_scenario_paths does not rewrite Vehicle model3d -- so relative
    paths here would resolve to nothing."""
    import os

    root = _parse(xml_str)
    logic = root.find("RoadNetwork/LogicFile").get("filepath")
    assert os.path.isabs(logic)
    model = root.find(".//Vehicle").get("model3d")
    if model is not None:
        assert os.path.isabs(model)


def test_stop_time_scales_with_route_length(xml_str):
    """A fixed stop time would cut long routes short, which looks identical to a
    vehicle that failed to arrive."""
    short = _parse(
        build_route_scenario(
            XODR, ROUTER_WAYPOINTS, start=CLICKED_START, route_length=100.0
        )
    )
    long = _parse(
        build_route_scenario(
            XODR, ROUTER_WAYPOINTS, start=CLICKED_START, route_length=2000.0
        )
    )

    def stop_seconds(root: ET.Element) -> float:
        cond = root.find("Storyboard/StopTrigger//SimulationTimeCondition")
        return float(cond.get("value"))

    assert stop_seconds(long) > stop_seconds(short)
    # 2000 m at 13.889 m/s is ~144 s of driving; the trigger must outlast it.
    assert stop_seconds(long) > 2000.0 / 13.889


def test_generated_xml_is_wellformed_and_declares_openscenario(xml_str):
    assert xml_str.startswith('<?xml version="1.0" encoding="UTF-8"?>')
    assert _parse(xml_str).tag == "OpenSCENARIO"


def test_start_defaults_to_first_waypoint_when_omitted():
    """Callers with no separate start still get a coherent file."""
    root = _parse(build_route_scenario(XODR, ROUTER_WAYPOINTS))
    teleport = root.find(".//TeleportAction//LanePosition")
    assert (int(teleport.get("roadId")), int(teleport.get("laneId"))) == (0, -4)


def test_empty_waypoints_rejected():
    with pytest.raises(ScenarioBuildError) as excinfo:
        build_route_scenario(XODR, [])
    assert excinfo.value.code == "no_waypoints"


def test_non_positive_speed_rejected():
    with pytest.raises(ScenarioBuildError) as excinfo:
        build_route_scenario(XODR, ROUTER_WAYPOINTS, ego_speed=0.0)
    assert excinfo.value.code == "bad_speed"


def test_missing_xodr_rejected(tmp_path):
    with pytest.raises(ScenarioBuildError) as excinfo:
        build_route_scenario(tmp_path / "nope.xodr", ROUTER_WAYPOINTS)
    assert excinfo.value.code == "xodr_not_found"
