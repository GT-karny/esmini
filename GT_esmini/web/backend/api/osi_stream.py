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

        entry = {
            "id": obj.id.value,
            "x": round(pos.x, 3),
            "y": round(pos.y, 3),
            "z": round(pos.z, 3),
            "h": round(ori.yaw, 4),
            "speed": round(speed, 3),
        }
        entry.update(_extract_lights(obj))
        objects.append(entry)

    return {
        "type": "ground_truth",
        "sim_time": round(sim_time, 3),
        "object_count": len(objects),
        "objects": objects,
    }


def _hvd_to_json(raw: bytes) -> dict | None:
    """Convert raw HostVehicleData protobuf to a lightweight JSON dict for the frontend."""
    hvd = HostVehicleData()
    try:
        hvd.ParseFromString(raw)
    except DecodeError:
        return None

    ts = hvd.timestamp
    sim_time = ts.seconds + ts.nanos * 1e-9 if ts.seconds or ts.nanos else 0.0

    throttle = hvd.vehicle_powertrain.pedal_position_acceleration if hvd.HasField("vehicle_powertrain") else 0.0
    brake = hvd.vehicle_brake_system.pedal_position_brake if hvd.HasField("vehicle_brake_system") else 0.0

    steering_angle = 0.0
    if hvd.HasField("vehicle_steering") and hvd.vehicle_steering.HasField("vehicle_steering_wheel"):
        steering_angle = hvd.vehicle_steering.vehicle_steering_wheel.angle

    gear = hvd.vehicle_powertrain.gear_transmission if hvd.HasField("vehicle_powertrain") else 0
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
    }


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
                    await websocket.send_json({"type": "end", "reason": "simulation_ended"})
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
