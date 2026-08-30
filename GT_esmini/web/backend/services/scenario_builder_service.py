"""Build a runnable OpenSCENARIO file from a planned route.

Takes what route_planner_service produced (a Waypoint chain over one xodr) and
emits the smallest scenario that drives it: one Ego, a TeleportAction at the start
waypoint, an AssignRouteAction carrying the chain, and a cruise SpeedAction.

Shape decisions worth knowing:

* Built with ``xml.etree.ElementTree``, not ``scenariogeneration``. The authoring
  toolchain in resources/scenario_authoring/ uses the latter, but its pins live in
  requirements-authoring.txt which is build-time only -- the web backend must not
  acquire a runtime dependency on it. simulation_runner already builds XOSC
  variants with ElementTree, so this follows the house style.

* Paths are written ABSOLUTE. Generated files land in TEMP_SCENARIOS_DIR, nowhere
  near resources/, and absolutize_scenario_paths only fixes up LogicFile /
  SceneGraphFile / CatalogLocations / Controller File -- not Vehicle ``model3d``.
  Writing absolute paths up front sidesteps the whole question for a file that is
  ephemeral anyway.

* The Ego DOES carry an <ObjectController> naming VirtualDriverController, with a
  ``policies`` Property. That is not redundant with simulation_runner's controller
  injection: _generate_virtual_driver_variant READS ``policies`` off the existing
  controller before stripping it, and turns those names into a per-run
  virtual_driver.json. It is the only way to switch on a default-OFF feature such
  as lane_change_initiation, which a route with required lane changes needs.
"""

from __future__ import annotations

import xml.etree.ElementTree as ET
from pathlib import Path

from GT_esmini.web.backend.config import RESOURCES_DIR

# Cruise ramp-up time for the initial SpeedAction [s]. Matches the verification
# scenarios in resources/xosc/verification/06_route_lane/.
_SPEED_RAMP_S = 3.0

# Slack applied when deriving the scenario's StopTrigger from the route length:
# the run must outlast the drive even if the vehicle is slowed by traffic, lights
# or a lane-change wait. Generous on purpose -- a scenario that ends early looks
# exactly like a vehicle that failed to reach its destination.
_STOP_TIME_FACTOR = 2.0
_STOP_TIME_MARGIN_S = 15.0


class ScenarioBuildError(Exception):
    """Raised when the requested scenario cannot be built."""

    def __init__(self, code: str, message: str):
        super().__init__(message)
        self.code = code


def _vehicle_element(parent: ET.Element) -> ET.Element:
    """Inline Ego vehicle (car_white geometry), mirroring the 06_route_lane assets.

    Inlined rather than a CatalogReference so the file carries no dependency on
    CatalogLocations resolution.
    """
    vehicle = ET.SubElement(
        parent, "Vehicle", {"name": "car_white", "vehicleCategory": "car"}
    )
    model = RESOURCES_DIR / "models" / "car_white.osgb"
    if model.is_file():
        vehicle.set("model3d", str(model))

    bbox = ET.SubElement(vehicle, "BoundingBox")
    ET.SubElement(bbox, "Center", {"x": "1.4", "y": "0.0", "z": "0.9"})
    ET.SubElement(
        bbox, "Dimensions", {"width": "2.0", "length": "5.0", "height": "1.8"}
    )
    ET.SubElement(
        vehicle,
        "Performance",
        {"maxSpeed": "69", "maxDeceleration": "30", "maxAcceleration": "10"},
    )
    axles = ET.SubElement(vehicle, "Axles")
    for tag, pos_x in (("FrontAxle", "2.98"), ("RearAxle", "0")):
        ET.SubElement(
            axles,
            tag,
            {
                "maxSteering": "30",
                "wheelDiameter": "0.8",
                "trackWidth": "1.68",
                "positionX": pos_x,
                "positionZ": "0.4",
            },
        )
    props = ET.SubElement(vehicle, "Properties")
    ET.SubElement(props, "Property", {"name": "model_id", "value": "0"})
    ET.SubElement(props, "Property", {"name": "scaleMode", "value": "ModelToBB"})
    return vehicle


def _lane_position(parent: ET.Element, wp: dict) -> None:
    position = ET.SubElement(parent, "Position")
    ET.SubElement(
        position,
        "LanePosition",
        {
            "roadId": str(wp["road_id"]),
            "laneId": str(wp["lane_id"]),
            "offset": "0",
            "s": f"{wp['s']:.6g}",
        },
    )


def build_route_scenario(
    xodr_path,
    waypoints: list[dict],
    start: dict | None = None,
    ego_speed: float = 13.889,
    policies: list[str] | None = None,
    route_length: float | None = None,
    description: str = "GT_Sim route-plan scenario",
    sumocfg: str | Path | None = None,
) -> str:
    """Render a route scenario as an OpenSCENARIO XML string.

    Args:
        xodr_path: the OpenDRIVE the waypoints were planned on.
        waypoints: route_planner_service output; each needs road_id/lane_id/s.
        start: where the vehicle actually begins -- route_planner_service's
            ``snapped[0]``, i.e. the point the user clicked. MUST be passed when the
            route needs a lane change, and here is why: the router's first waypoint
            reports the lane the vehicle has to REACH by the end of that road, not
            the one it starts in. On the exit-ramp case it comes back as road 0 lane
            -4 while the click was lane -3. Teleporting to the waypoint would place
            the vehicle post-lane-change and quietly delete the manoeuvre the route
            exists to exercise. Defaults to waypoints[0] only for callers that have
            no separate start.
        ego_speed: cruise target [m/s].
        policies: VirtualDriver opt-ins (e.g. ["lane_change_initiation"]) written
            as the controller's ``policies`` Property for simulation_runner to read.
        route_length: planned route distance [m], used to size the StopTrigger.
        description: FileHeader description.
        sumocfg: path to a .sumocfg to run background traffic from. Generate one
            with scripts/xodr_to_sumo_net.py --demand N. Omit for an empty road.

    Raises:
        ScenarioBuildError: no waypoints, or the xodr is missing.
    """
    if not waypoints:
        raise ScenarioBuildError("no_waypoints", "A route scenario needs waypoints.")
    if ego_speed <= 0:
        raise ScenarioBuildError("bad_speed", "ego_speed must be positive.")

    xodr_path = Path(xodr_path).resolve()
    if not xodr_path.is_file():
        raise ScenarioBuildError("xodr_not_found", f"xodr not found: {xodr_path}")

    start = start or waypoints[0]

    # Route waypoint list: the start position, then the router's chain minus its
    # own first entry when that describes the same road (it is the start road, and
    # we have just written a more accurate entry for it). RouteLanePlan derives each
    # road's target-lane band from lane CONNECTIVITY to the next road, not from the
    # waypoint's lane id, so naming the start lane here still yields the {-4} band
    # that drives the lane change -- this is the shape the committed
    # 06_route_lane/*.xosc assets use.
    route_waypoints = [start]
    for wp in waypoints:
        if wp["road_id"] == start["road_id"] and len(route_waypoints) == 1:
            continue
        route_waypoints.append(wp)

    root = ET.Element("OpenSCENARIO")
    ET.SubElement(
        root,
        "FileHeader",
        {
            "revMajor": "1",
            "revMinor": "1",
            # Fixed date: a timestamp here would make otherwise-identical
            # generated scenarios differ byte-for-byte on every request.
            "date": "2026-01-01T00:00:00",
            "description": description,
            "author": "GT_Sim route planner",
        },
    )
    ET.SubElement(root, "ParameterDeclarations")
    ET.SubElement(root, "CatalogLocations")

    road_network = ET.SubElement(root, "RoadNetwork")
    ET.SubElement(road_network, "LogicFile", {"filepath": str(xodr_path)})

    entities = ET.SubElement(root, "Entities")
    ego = ET.SubElement(entities, "ScenarioObject", {"name": "Ego"})
    _vehicle_element(ego)

    obj_controller = ET.SubElement(ego, "ObjectController")
    controller = ET.SubElement(
        obj_controller, "Controller", {"name": "VirtualDriverController"}
    )
    ctrl_props = ET.SubElement(controller, "Properties")
    ET.SubElement(
        ctrl_props,
        "Property",
        {"name": "esminiController", "value": "VirtualDriverController"},
    )
    if policies:
        ET.SubElement(
            ctrl_props,
            "Property",
            {"name": "policies", "value": ",".join(policies)},
        )

    if sumocfg:
        # Background traffic host. It is NOT a participant: the controller removes
        # it at Init and uses it only as the 3D template for spawned vehicles.
        #
        # The controller is declared INLINE, never via a CatalogReference: GT_Sim
        # absolutizes only filepath/path attributes inside the xosc itself, not
        # inside catalog files, so a catalog-referenced SUMO controller dies with
        # "Failed to localize controller file" (exit 1). Measured in
        # GT_esmini/docs/features/sumo_background_traffic.md section 5.
        traffic = ET.SubElement(entities, "ScenarioObject", {"name": "SumoVehicles"})
        _vehicle_element(traffic)
        traffic_oc = ET.SubElement(traffic, "ObjectController")
        traffic_ctrl = ET.SubElement(
            traffic_oc, "Controller", {"name": "gtSumoTraffic"}
        )
        traffic_props = ET.SubElement(traffic_ctrl, "Properties")
        ET.SubElement(
            traffic_props,
            "Property",
            {"name": "esminiController", "value": "GTSumoTrafficController"},
        )
        ET.SubElement(
            traffic_props,
            "Property",
            {"name": "overrideVehicleScaleMode", "value": "BBToModel"},
        )
        ET.SubElement(traffic_props, "File", {"filepath": str(Path(sumocfg).resolve())})

    storyboard = ET.SubElement(root, "Storyboard")
    init = ET.SubElement(storyboard, "Init")
    init_actions = ET.SubElement(init, "Actions")
    private = ET.SubElement(init_actions, "Private", {"entityRef": "Ego"})

    teleport_action = ET.SubElement(private, "PrivateAction")
    teleport = ET.SubElement(teleport_action, "TeleportAction")
    _lane_position(teleport, start)

    routing_action = ET.SubElement(private, "PrivateAction")
    routing = ET.SubElement(routing_action, "RoutingAction")
    assign = ET.SubElement(routing, "AssignRouteAction")
    route = ET.SubElement(assign, "Route", {"name": "ego_route", "closed": "false"})
    for wp in route_waypoints:
        waypoint = ET.SubElement(route, "Waypoint", {"routeStrategy": "shortest"})
        _lane_position(waypoint, wp)

    activate_action = ET.SubElement(private, "PrivateAction")
    controller_action = ET.SubElement(activate_action, "ControllerAction")
    ET.SubElement(
        controller_action,
        "ActivateControllerAction",
        {"lateral": "true", "longitudinal": "true"},
    )

    story = ET.SubElement(storyboard, "Story", {"name": "RouteStory"})
    act = ET.SubElement(story, "Act", {"name": "RouteAct"})
    group = ET.SubElement(
        act, "ManeuverGroup", {"maximumExecutionCount": "1", "name": "EgoSequence"}
    )
    actors = ET.SubElement(group, "Actors", {"selectTriggeringEntities": "false"})
    ET.SubElement(actors, "EntityRef", {"entityRef": "Ego"})
    maneuver = ET.SubElement(group, "Maneuver", {"name": "CruiseManeuver"})
    event = ET.SubElement(
        maneuver, "Event", {"name": "Cruise", "priority": "overwrite"}
    )
    action = ET.SubElement(event, "Action", {"name": "CruiseAction"})
    action_private = ET.SubElement(action, "PrivateAction")
    longitudinal = ET.SubElement(action_private, "LongitudinalAction")
    speed_action = ET.SubElement(longitudinal, "SpeedAction")
    ET.SubElement(
        speed_action,
        "SpeedActionDynamics",
        {
            "dynamicsShape": "linear",
            "value": str(_SPEED_RAMP_S),
            "dynamicsDimension": "time",
        },
    )
    speed_target = ET.SubElement(speed_action, "SpeedActionTarget")
    ET.SubElement(speed_target, "AbsoluteTargetSpeed", {"value": f"{ego_speed:.6g}"})

    _simulation_time_trigger(event, "StartTrigger", "CruiseStart", 0.0)
    _simulation_time_trigger(act, "StartTrigger", "ActStart", 0.0)

    stop_seconds = _STOP_TIME_MARGIN_S
    if route_length and route_length > 0:
        stop_seconds += (route_length / ego_speed) * _STOP_TIME_FACTOR
    _simulation_time_trigger(
        storyboard, "StopTrigger", "StopCondition", stop_seconds, edge="rising"
    )

    ET.indent(root, space="   ")
    return '<?xml version="1.0" encoding="UTF-8"?>\n' + ET.tostring(
        root, encoding="unicode"
    )


def _simulation_time_trigger(
    parent: ET.Element,
    tag: str,
    name: str,
    value: float,
    edge: str = "none",
) -> None:
    trigger = ET.SubElement(parent, tag)
    group = ET.SubElement(trigger, "ConditionGroup")
    condition = ET.SubElement(
        group, "Condition", {"name": name, "delay": "0", "conditionEdge": edge}
    )
    by_value = ET.SubElement(condition, "ByValueCondition")
    ET.SubElement(
        by_value,
        "SimulationTimeCondition",
        {"value": f"{value:.6g}", "rule": "greaterThan"},
    )
