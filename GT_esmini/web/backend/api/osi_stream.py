"""WebSocket endpoint for streaming OSI data to the web frontend."""

from __future__ import annotations

import asyncio
import logging
import math

from fastapi import APIRouter, WebSocket, WebSocketDisconnect

from osi3.osi_groundtruth_pb2 import GroundTruth
from osi3.osi_hostvehicledata_pb2 import HostVehicleData
from google.protobuf.message import DecodeError

from GT_esmini.web.backend.services.osi_bridge import get_bridge

logger = logging.getLogger(__name__)

router = APIRouter()

# --- IndicatorState enum values (osi3.LightState.IndicatorState) ---
_INDICATOR_MAP = {0: "off", 1: "off", 2: "off", 3: "left", 4: "right", 5: "warning"}

# --- BrakeLightState enum values (osi3.LightState.BrakeLightState) ---
_BRAKE_LIGHT_MAP = {0: "off", 1: "off", 2: "off", 3: "normal", 4: "strong"}

# --- GenericLightState: 3 = ON, everything else = OFF ---
_GENERIC_LIGHT_ON = 3

# --- MovingObject.Type enum ---
_MOVING_TYPE_MAP = {
    0: "unknown",
    1: "other",
    2: "vehicle",
    3: "pedestrian",
    4: "animal",
}

# --- TrafficLight.Classification.Color enum (osi3) ---
_TL_COLOR_MAP = {
    0: "unknown",
    1: "other",
    2: "red",
    3: "yellow",
    4: "green",
    5: "blue",
    6: "white",
}

# --- TrafficLight.Classification.Mode enum (osi3) ---
_TL_MODE_MAP = {
    0: "unknown",
    1: "other",
    2: "off",
    3: "constant",
    4: "flashing",
    5: "counting",
}

# --- HostVehicleData.VehicleAutomatedDrivingFunction.State enum (osi3) ---
#
# req-vd-ad:REQ-AD-029 (driver-facing HMI). The three-value discipline the
# ManualDrive ADAS stack reports on (design manualdrive_adas_design.md sec8-2)
# is UNAVAILABLE / STANDBY / ACTIVE: "switched off or not owned", "watching but
# not triggered" and "intervening". The remaining values are carried through
# rather than folded into "unknown" -- collapsing them here would make a
# reporting bug (ERRORED) look like a quiet function on the dashboard.
_ADAS_STATE_MAP = {
    0: "unknown",
    1: "other",
    2: "errored",
    3: "unavailable",
    4: "available",
    5: "standby",
    6: "active",
}

# --- ...DriverOverride.Reason enum (osi3; only two values exist) ---
_ADAS_OVERRIDE_REASON_MAP = {0: "brake_pedal", 1: "steering_input"}

# --- VehicleClassification.Type enum ---
_VEHICLE_CLASS_MAP = {
    0: "unknown",
    1: "other",
    2: "small_car",
    3: "compact_car",
    4: "medium_car",
    5: "luxury_car",
    6: "delivery_van",
    7: "heavy_truck",
    8: "semitrailer",
    9: "trailer",
    10: "motorbike",
    11: "bicycle",
    12: "bus",
    13: "tram",
    14: "train",
    15: "wheelchair",
    16: "semitractor",
    17: "standup_scooter",
}


def _extract_entity_name(obj) -> str:
    """Extract OpenSCENARIO entity name from source_reference identifiers."""
    for ref in obj.source_reference:
        if ref.type == "net.asam.openscenario":
            for ident in ref.identifier:
                if ident.startswith("entity_name:"):
                    return ident[len("entity_name:") :]
    return ""


def _extract_lights(obj) -> dict:
    """Extract light state strings from a MovingObject's vehicle_classification."""
    head_light = "off"
    indicator = "off"
    brake_light = "off"

    if obj.HasField("vehicle_classification"):
        vc = obj.vehicle_classification
        if vc.HasField("light_state"):
            ls = vc.light_state
            head_light = "on" if ls.head_light == _GENERIC_LIGHT_ON else "off"
            indicator = _INDICATOR_MAP.get(ls.indicator_state, "off")
            brake_light = _BRAKE_LIGHT_MAP.get(ls.brake_light_state, "off")

    return {
        "head_light": head_light,
        "indicator": indicator,
        "brake_light": brake_light,
    }


def _gt_to_json(raw: bytes) -> dict | None:
    """Convert raw GroundTruth protobuf to a lightweight JSON dict for the frontend."""
    gt = GroundTruth()
    try:
        gt.ParseFromString(raw)
    except DecodeError:
        return None

    # Extract timestamp
    ts = gt.timestamp
    sim_time = ts.seconds + ts.nanos * 1e-9 if ts.seconds or ts.nanos else 0.0

    # Extract moving objects
    objects = []
    for obj in gt.moving_object:
        pos = obj.base.position
        ori = obj.base.orientation
        vel = obj.base.velocity
        speed = math.sqrt(vel.x**2 + vel.y**2 + vel.z**2)

        # Object type and vehicle classification
        obj_type = _MOVING_TYPE_MAP.get(obj.type, "unknown")
        vehicle_class = ""
        if obj.type == 2 and obj.HasField("vehicle_classification"):
            vehicle_class = _VEHICLE_CLASS_MAP.get(
                obj.vehicle_classification.type, "unknown"
            )

        # Bounding box dimensions
        dim = obj.base.dimension
        length = round(dim.length, 2) if dim.length > 0 else 4.0
        width = round(dim.width, 2) if dim.width > 0 else 2.0

        entry = {
            "id": obj.id.value,
            "name": _extract_entity_name(obj),
            "x": round(pos.x, 3),
            "y": round(pos.y, 3),
            "z": round(pos.z, 3),
            "h": round(ori.yaw, 4),
            "speed": round(speed, 3),
            "obj_type": obj_type,
            "vehicle_class": vehicle_class,
            "length": length,
            "width": width,
        }
        entry.update(_extract_lights(obj))

        # Everything else the engine populated on this object. The rule here is
        # "if it is emitted, forward it" -- a field withheld at this layer is
        # indistinguishable, from the browser, from a field the engine never
        # filled, and that ambiguity has already cost real debugging time.
        #
        # `speed` above stays the scalar it always was (consumers depend on it),
        # but the VECTOR is what carries approach vs. departure -- a magnitude
        # cannot express closing. Same reasoning as gt_sim_test's scene dict.
        entry["height"] = round(dim.height, 2) if dim.height > 0 else 1.5
        entry["vel"] = [round(vel.x, 3), round(vel.y, 3), round(vel.z, 3)]
        acc = obj.base.acceleration
        entry["acc"] = [round(acc.x, 3), round(acc.y, 3), round(acc.z, 3)]
        # Pitch and roll are modelled (control vehicles inside the controller,
        # traffic through VehiclePhysicsManager) but never reached the browser.
        entry["pitch"] = round(ori.pitch, 4)
        entry["roll"] = round(ori.roll, 4)
        orate = obj.base.orientation_rate
        entry["orientation_rate"] = [
            round(orate.roll, 4),
            round(orate.pitch, 4),
            round(orate.yaw, 4),
        ]
        oacc = obj.base.orientation_acceleration
        entry["orientation_acc"] = [
            round(oacc.roll, 4),
            round(oacc.pitch, 4),
            round(oacc.yaw, 4),
        ]

        # Physical lane assignment. Kept SEPARATE from logical_lanes below: inside
        # an OSI intersection this one is the junction's own id (the connecting
        # lanes are fused into one TYPE_INTERSECTION lane) while the logical one
        # names the individual driving path. Both are correct; folding them
        # together would destroy that distinction.
        phys = [i.value for i in obj.moving_object_classification.assigned_lane_id] or [
            i.value for i in obj.assigned_lane_id
        ]
        if phys:
            entry["assigned_lane_id"] = phys

        # Logical lane assignment (v0.18.0): one entry per lane the body overlaps
        # by more than 5 cm, so normally one and two while straddling. The lane
        # NETWORK those ids point into is static and comes from the REST endpoint,
        # not from here -- re-sending it every frame would be megabytes.
        assignments = [
            {
                "lane": a.assigned_lane_id.value,
                "s": round(a.s_position, 3),
                "t": round(a.t_position, 3),
                "angle": round(a.angle_to_lane, 4),
            }
            for a in obj.moving_object_classification.logical_lane_assignment
        ]
        if assignments:
            entry["logical_lanes"] = assignments

        # Reference-point geometry. Without it a consumer cannot convert between
        # base.position (bounding-box centre) and the axle-based points, which is
        # exactly the conversion that caused a 1.4 m error earlier in this work.
        va = obj.vehicle_attributes
        if obj.HasField("vehicle_attributes"):
            entry["bbcenter_to_rear"] = [
                round(va.bbcenter_to_rear.x, 3),
                round(va.bbcenter_to_rear.y, 3),
                round(va.bbcenter_to_rear.z, 3),
            ]
            entry["bbcenter_to_front"] = [
                round(va.bbcenter_to_front.x, 3),
                round(va.bbcenter_to_front.y, 3),
                round(va.bbcenter_to_front.z, 3),
            ]
            if va.number_wheels:
                entry["number_wheels"] = va.number_wheels

        if obj.HasField("vehicle_classification"):
            entry["role"] = obj.vehicle_classification.role
        if obj.HasField("color_description") and obj.color_description.HasField("rgb"):
            rgb = obj.color_description.rgb
            entry["rgb"] = [
                round(rgb.red, 3),
                round(rgb.green, 3),
                round(rgb.blue, 3),
            ]
        if obj.model_reference:
            entry["model_reference"] = obj.model_reference

        # The ego's planned path (signal:ego_planned_path). Host-only and capped
        # by the planner, so this does not scale with traffic density.
        if obj.future_trajectory:
            entry["future_trajectory"] = [
                [round(p.position.x, 3), round(p.position.y, 3), round(p.position.z, 3)]
                for p in obj.future_trajectory
            ]

        objects.append(entry)

    # Traffic lights: each lamp (red/yellow/green) is a separate osi3.TrafficLight.
    # dynamic_gt re-emits these every frame (live phase), so the WS stream is the
    # reliable source for signal colour over time. (Static signs/stop-lines come
    # from the road-geometry REST endpoint instead — see road_geometry_service.)
    traffic_lights = []
    for tl in gt.traffic_light:
        pos = tl.base.position
        cls = tl.classification
        traffic_lights.append(
            {
                "id": tl.id.value,
                "x": round(pos.x, 3),
                "y": round(pos.y, 3),
                "z": round(pos.z, 3),
                "h": round(tl.base.orientation.yaw, 4),
                "color": _TL_COLOR_MAP.get(cls.color, "unknown"),
                "mode": _TL_MODE_MAP.get(cls.mode, "unknown"),
                "icon": cls.icon,
            }
        )

    result = {
        "type": "ground_truth",
        "sim_time": round(sim_time, 3),
        "object_count": len(objects),
        "objects": objects,
    }
    # Which of the objects is the ego. Emitted by the engine on every frame but
    # never forwarded, so the browser had no way to identify the host from OSI.
    if gt.HasField("host_vehicle_id"):
        result["host_vehicle_id"] = gt.host_vehicle_id.value
    if traffic_lights:
        result["traffic_lights"] = traffic_lights
    return result


def _adas_functions_to_json(hvd: HostVehicleData) -> list[dict]:
    """Project vehicle_automated_driving_function[] for the driver-facing HMI.

    req-vd-ad:REQ-AD-029 steps a/b (state, settings, warnings) and step c (the
    supply is the EXISTING HVD wiring -- UDP 48199 -> OSIBridge -> this
    projection -> the /ws/osi WebSocket; no display-only channel is dug).

    The shape deliberately MIRRORS the verification harness's own projection
    (gt_sim_test.py `_hvd_to_dict`, verification plan sec7-3) -- same keys, same
    enum spellings -- so face 3 (matchers) and the dashboard are reading the
    same thing under the same names. A row is emitted for every function the
    vehicle reports, in report order; the ManualDrive stack writes
    gt.aeb / gt.fcw / gt.acc / gt.lka / gt.ldw / gt.msl (design sec8-2), other
    controllers write their own rows and are unaffected.

    `driver_override.present` is kept distinct from `active` for the same
    reason face 3 keeps it (design sec8-3): an absent submessage means "nobody
    wrote this channel", an explicit active=false means "evaluated, no
    override". Rendering them the same would make a dead populate path look
    like a driver who is not overriding anything.
    """
    rows: list[dict] = []
    for func in hvd.vehicle_automated_driving_function:
        override_present = func.HasField("driver_override")
        rows.append(
            {
                "key": func.custom_name,
                "name": func.name,
                "state": func.state,
                "state_name": _ADAS_STATE_MAP.get(func.state, "unknown"),
                "detail": {kv.key: kv.value for kv in func.custom_detail},
                "driver_override": {
                    "present": override_present,
                    "active": (
                        bool(func.driver_override.active) if override_present else False
                    ),
                    "reasons": (
                        [
                            _ADAS_OVERRIDE_REASON_MAP.get(r, "unknown")
                            for r in func.driver_override.override_reason
                        ]
                        if override_present
                        else []
                    ),
                },
                "custom_state": func.custom_state,
            }
        )
    return rows


def _hvd_to_json(raw: bytes) -> dict | None:
    """Convert raw HostVehicleData protobuf to a lightweight JSON dict for the frontend."""
    hvd = HostVehicleData()
    try:
        hvd.ParseFromString(raw)
    except DecodeError:
        return None

    ts = hvd.timestamp
    sim_time = ts.seconds + ts.nanos * 1e-9 if ts.seconds or ts.nanos else 0.0

    throttle = (
        hvd.vehicle_powertrain.pedal_position_acceleration
        if hvd.HasField("vehicle_powertrain")
        else 0.0
    )
    brake = (
        hvd.vehicle_brake_system.pedal_position_brake
        if hvd.HasField("vehicle_brake_system")
        else 0.0
    )

    steering_angle = 0.0
    if hvd.HasField("vehicle_steering") and hvd.vehicle_steering.HasField(
        "vehicle_steering_wheel"
    ):
        steering_angle = hvd.vehicle_steering.vehicle_steering_wheel.angle

    gear = (
        hvd.vehicle_powertrain.gear_transmission
        if hvd.HasField("vehicle_powertrain")
        else 0
    )
    rpm = 0.0
    torque = 0.0
    if hvd.HasField("vehicle_powertrain") and len(hvd.vehicle_powertrain.motor) > 0:
        rpm = hvd.vehicle_powertrain.motor[0].rpm
        torque = hvd.vehicle_powertrain.motor[0].torque

    # C++ GT_HostVehicleReporter writes velocity to the deprecated location field
    speed = 0.0
    if hvd.HasField("location") and hvd.location.HasField("velocity"):
        vel = hvd.location.velocity
        speed = math.sqrt(vel.x**2 + vel.y**2 + vel.z**2)
    elif hvd.HasField("vehicle_motion") and hvd.vehicle_motion.HasField("velocity"):
        vel = hvd.vehicle_motion.velocity
        speed = math.sqrt(vel.x**2 + vel.y**2 + vel.z**2)

    out = {
        "type": "host_vehicle_data",
        "sim_time": round(sim_time, 3),
        "throttle": round(throttle, 4),
        "brake": round(brake, 4),
        "steering_angle": round(steering_angle, 4),
        "gear": gear,
        "rpm": round(rpm, 1),
        "torque": round(torque, 1),
        "speed": round(speed, 3),
        "adas_functions": _adas_functions_to_json(hvd),
    }

    # The rest of what the reporter populates. Until 2026-09-25 only the ten keys
    # above survived, so state the engine already computes -- the whole motion
    # block, the localisation block, and the route filled in v0.18.0 -- simply
    # never reached the browser.
    def _vec(v):
        return [round(v.x, 4), round(v.y, 4), round(v.z, 4)]

    def _ori(o):
        return [round(o.roll, 4), round(o.pitch, 4), round(o.yaw, 4)]

    if hvd.HasField("host_vehicle_id"):
        out["host_vehicle_id"] = hvd.host_vehicle_id.value

    if hvd.HasField("location"):
        loc = hvd.location
        out["location"] = {
            "position": _vec(loc.position),
            "orientation": _ori(loc.orientation),
            "velocity": _vec(loc.velocity),
            "acceleration": _vec(loc.acceleration),
        }

    # vehicle_motion is referenced to the middle of the rear axle, NOT the
    # bounding-box centre that `location` uses -- carry both rather than pick.
    if hvd.HasField("vehicle_motion"):
        vm = hvd.vehicle_motion
        out["vehicle_motion"] = {
            "position": _vec(vm.position),
            "orientation": _ori(vm.orientation),
            "velocity": _vec(vm.velocity),
            "acceleration": _vec(vm.acceleration),
        }

    if hvd.HasField("vehicle_localization"):
        vl = hvd.vehicle_localization
        out["vehicle_localization"] = {
            "position": _vec(vl.position),
            "orientation": _ori(vl.orientation),
        }

    if hvd.HasField("vehicle_basics"):
        out["operating_state"] = hvd.vehicle_basics.operating_state

    # HostVehicleData.route (v0.18.0): the lanes the ego intends to drive, as
    # ordered segments. `logical_lane_id` indexes the network served by the
    # static REST endpoint; start_s > end_s means "traverse against the
    # reference line direction", which is the ordinary case for a right-hand
    # negative lane -- do not normalise it away.
    if hvd.HasField("route"):
        out["route"] = {
            "route_id": hvd.route.route_id.value,
            "segments": [
                [
                    {
                        "lane": ls.logical_lane_id.value,
                        "start_s": round(ls.start_s, 3),
                        "end_s": round(ls.end_s, 3),
                    }
                    for ls in seg.lane_segment
                ]
                for seg in hvd.route.route_segment
            ],
        }

    return out


def _logical_lane_network_to_json(raw: bytes) -> dict | None:
    """Project the STATIC logical lane layer out of a GroundTruth frame.

    Only the first frame of a run carries this (see osi_bridge._StreamState),
    which is why it is served over REST instead of the WebSocket: a client that
    connects mid-run has already missed it on the stream.

    Geometry is included because it is what makes the layer usable without the
    xodr -- ReferenceLine points carry (world XYZ, s) and boundary points carry
    (world XYZ, s, t), so a consumer can turn a (lane, s, t) assignment from the
    WebSocket into world coordinates with nothing but this payload. Measured
    round-trip on v0.18.0: 24.9 mm lateral worst case, inside the 5 cm the
    standard allows for the polyline approximation.
    """
    gt = GroundTruth()
    try:
        gt.ParseFromString(raw)
    except DecodeError:
        return None

    reference_lines = [
        {
            "id": rl.id.value,
            "points": [
                {
                    "x": round(p.world_position.x, 3),
                    "y": round(p.world_position.y, 3),
                    "z": round(p.world_position.z, 3),
                    "s": round(p.s_position, 3),
                    "t_axis_yaw": round(p.t_axis_yaw, 5),
                }
                for p in rl.poly_line
            ],
        }
        for rl in gt.reference_line
    ]

    boundaries = [
        {
            "id": lb.id.value,
            "passing_rule": lb.passing_rule,
            "points": [
                {
                    "x": round(p.position.x, 3),
                    "y": round(p.position.y, 3),
                    "z": round(p.position.z, 3),
                    "s": round(p.s_position, 3),
                    "t": round(p.t_position, 3),
                }
                for p in lb.boundary_line
            ],
        }
        for lb in gt.logical_lane_boundary
    ]

    lanes = []
    for ll in gt.logical_lane:
        # source_reference carries the OpenDRIVE provenance as prefixed strings
        # ("road_id:3" / "road_s:0" / "lane_id:-1"), the same convention the
        # physical Lane uses, so one parser serves both.
        odr = {}
        for sr in ll.source_reference:
            for ident in sr.identifier:
                key, _, val = ident.partition(":")
                if key and val:
                    odr[key] = val
        lanes.append(
            {
                "id": ll.id.value,
                "type": ll.type,
                "reference_line": ll.reference_line_id.value,
                "start_s": round(ll.start_s, 3),
                "end_s": round(ll.end_s, 3),
                "move_direction": ll.move_direction,
                "left_boundary": [i.value for i in ll.left_boundary_id],
                "right_boundary": [i.value for i in ll.right_boundary_id],
                "predecessor": [
                    {
                        "lane": c.other_lane_id.value,
                        "at_begin": c.at_begin_of_other_lane,
                    }
                    for c in ll.predecessor_lane
                ],
                "successor": [
                    {
                        "lane": c.other_lane_id.value,
                        "at_begin": c.at_begin_of_other_lane,
                    }
                    for c in ll.successor_lane
                ],
                "left_adjacent": [r.other_lane_id.value for r in ll.left_adjacent_lane],
                "right_adjacent": [
                    r.other_lane_id.value for r in ll.right_adjacent_lane
                ],
                "odr": odr,
            }
        )

    # --- the rest of the static ground truth -------------------------------
    #
    # Physical lanes, their boundaries, stationary objects, signs and the geo
    # reference are all emitted on this same frame and were all discarded. The
    # frontend reads road geometry out of the xodr instead, which works but means
    # two sources of truth for the same road -- and leaves anything the engine
    # knows but the xodr does not (OSI lane typing, the intersection fusion,
    # synthesised junction boundaries) unreachable.

    phys_lanes = [
        {
            "id": ln.id.value,
            "type": ln.classification.type,
            "subtype": ln.classification.subtype,
            "centerline_is_driving_direction": (
                ln.classification.centerline_is_driving_direction
            ),
            "centerline": [
                [round(p.x, 3), round(p.y, 3), round(p.z, 3)]
                for p in ln.classification.centerline
            ],
            "left_adjacent": [i.value for i in ln.classification.left_adjacent_lane_id],
            "right_adjacent": [
                i.value for i in ln.classification.right_adjacent_lane_id
            ],
            "lane_pairing": [
                {
                    "antecessor": (
                        lp.antecessor_lane_id.value
                        if lp.HasField("antecessor_lane_id")
                        else None
                    ),
                    "successor": (
                        lp.successor_lane_id.value
                        if lp.HasField("successor_lane_id")
                        else None
                    ),
                }
                for lp in ln.classification.lane_pairing
            ],
            "left_lane_boundary": [
                i.value for i in ln.classification.left_lane_boundary_id
            ],
            "right_lane_boundary": [
                i.value for i in ln.classification.right_lane_boundary_id
            ],
            "free_lane_boundary": [
                i.value for i in ln.classification.free_lane_boundary_id
            ],
            "source_reference": [list(sr.identifier) for sr in ln.source_reference],
        }
        for ln in gt.lane
    ]

    phys_boundaries = [
        {
            "id": lb.id.value,
            "type": lb.classification.type,
            "color": lb.classification.color,
            "points": [
                [round(p.position.x, 3), round(p.position.y, 3), round(p.position.z, 3)]
                for p in lb.boundary_line
            ],
        }
        for lb in gt.lane_boundary
    ]

    stationary = [
        {
            "id": so.id.value,
            "type": so.classification.type,
            "material": so.classification.material,
            "x": round(so.base.position.x, 3),
            "y": round(so.base.position.y, 3),
            "z": round(so.base.position.z, 3),
            "yaw": round(so.base.orientation.yaw, 4),
            "length": round(so.base.dimension.length, 3),
            "width": round(so.base.dimension.width, 3),
            "height": round(so.base.dimension.height, 3),
            "base_polygon": [
                [round(p.x, 3), round(p.y, 3)] for p in so.base.base_polygon
            ],
            "source_reference": [list(sr.identifier) for sr in so.source_reference],
        }
        for so in gt.stationary_object
    ]

    signs = [
        {
            "id": ts.id.value,
            "type": ts.main_sign.classification.type,
            "value": ts.main_sign.classification.value.value,
            "value_unit": ts.main_sign.classification.value.value_unit,
            "x": round(ts.main_sign.base.position.x, 3),
            "y": round(ts.main_sign.base.position.y, 3),
            "z": round(ts.main_sign.base.position.z, 3),
            "yaw": round(ts.main_sign.base.orientation.yaw, 4),
            "assigned_lane_id": [
                i.value for i in ts.main_sign.classification.assigned_lane_id
            ],
        }
        for ts in gt.traffic_sign
    ]

    return {
        "lane_count": len(lanes),
        "reference_line_count": len(reference_lines),
        "boundary_count": len(boundaries),
        "lanes": lanes,
        "reference_lines": reference_lines,
        "boundaries": boundaries,
        # --- physical layer + scenery + georeferencing ---
        "physical_lane_count": len(phys_lanes),
        "physical_lanes": phys_lanes,
        "physical_boundaries": phys_boundaries,
        "stationary_objects": stationary,
        "traffic_signs": signs,
        "host_vehicle_id": (
            gt.host_vehicle_id.value if gt.HasField("host_vehicle_id") else None
        ),
        # proj_string is the OpenDRIVE <geoReference> verbatim (a PROJ string),
        # which is what lets a consumer place the scene on a real map. Empty for
        # assets that carry no geoReference -- roughly half of ours.
        "proj_string": gt.proj_string,
        "map_reference": gt.map_reference,
        "model_reference": gt.model_reference,
        "osi_version": (
            "%d.%d.%d"
            % (
                gt.version.version_major,
                gt.version.version_minor,
                gt.version.version_patch,
            )
            if gt.HasField("version")
            else None
        ),
    }


@router.get("/api/osi/{job_id}/logical-lanes")
async def get_logical_lane_network(job_id: str):
    """The static logical lane network for a running job.

    Returns 404 while no frame has arrived yet (the run has not started, or OSI
    is disabled for it), and an empty network when the engine emitted none --
    those are different situations and the payload says which.
    """
    bridge = get_bridge(job_id)
    if bridge is None:
        return {"error": "no OSI bridge for this job", "available": False}

    raw = bridge.static_frame
    if raw is None:
        # "not yet" and "not coming" are different answers and the caller acts on
        # them differently -- keep polling, versus tell the user to raise
        # osi.static_reporting. Collapsing both into "unavailable" is what made
        # the first version of this endpoint report an empty road network.
        if bridge.static_missing:
            return {
                "available": False,
                "static_missing": True,
                "error": (
                    "the static ground truth never arrived. It is sent once and is "
                    "large enough to lose a UDP packet; set osi.static_reporting=2 "
                    "to have every frame carry it."
                ),
            }
        return {
            "available": False,
            "static_missing": False,
            "error": "no static ground truth received yet",
        }

    network = _logical_lane_network_to_json(raw)
    if network is None:
        return {"error": "could not decode the ground truth frame", "available": False}

    if network["lane_count"] == 0:
        # Distinguishable from "not available": the frame arrived and simply had
        # no logical lanes (GT_OSI_LOGICAL_LANE=0, or a build without the layer).
        network["note"] = "the ground truth carried no logical lanes"
    network["available"] = True
    network["job_id"] = job_id
    return network


@router.websocket("/ws/osi/{job_id}")
async def osi_websocket(websocket: WebSocket, job_id: str):
    """Stream OSI GroundTruth + HostVehicleData as JSON to browser clients."""
    await websocket.accept()
    logger.info("WebSocket OSI client connected for job %s", job_id)

    bridge = get_bridge(job_id)
    if bridge is None or not bridge.running:
        await websocket.send_json({"error": "No active OSI bridge for this job"})
        await websocket.close()
        return

    gt_sub_id, gt_queue = bridge.subscribe_gt(f"ws-gt-{job_id}")
    hvd_sub_id, hvd_queue = bridge.subscribe_hvd(f"ws-hvd-{job_id}")

    try:
        while True:
            gt_task = asyncio.ensure_future(gt_queue.get())
            hvd_task = asyncio.ensure_future(hvd_queue.get())

            done, pending = await asyncio.wait(
                {gt_task, hvd_task},
                timeout=2.0,
                return_when=asyncio.FIRST_COMPLETED,
            )

            for task in pending:
                task.cancel()
                try:
                    await task
                except (asyncio.CancelledError, Exception):
                    pass

            if not done:
                if not bridge.running:
                    await websocket.send_json(
                        {"type": "end", "reason": "simulation_ended"}
                    )
                    break
                continue

            for task in done:
                raw = task.result()
                if task is gt_task:
                    data = _gt_to_json(raw)
                else:
                    data = _hvd_to_json(raw)
                if data is not None:
                    await websocket.send_json(data)

    except WebSocketDisconnect:
        logger.info("WebSocket OSI client disconnected for job %s", job_id)
    except Exception as e:
        logger.warning("WebSocket OSI error for job %s: %s", job_id, e)
    finally:
        bridge.unsubscribe_gt(gt_sub_id)
        bridge.unsubscribe_hvd(hvd_sub_id)
        try:
            await websocket.close()
        except Exception:
            pass
