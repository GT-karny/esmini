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

        # Logical lane assignment (v0.18.0). Per-frame and small: one entry per
        # lane the body overlaps by more than 5 cm, so normally one and two while
        # the object straddles a lane boundary. The lane NETWORK those ids point
        # into is static and comes from the REST endpoint below, not from here --
        # re-sending it every frame would be megabytes.
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

    return {
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

    return {
        "lane_count": len(lanes),
        "reference_line_count": len(reference_lines),
        "boundary_count": len(boundaries),
        "lanes": lanes,
        "reference_lines": reference_lines,
        "boundaries": boundaries,
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
        return {"error": "no ground truth received yet", "available": False}

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
