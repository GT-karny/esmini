"""WebSocket endpoint for streaming OSI data to the web frontend."""

from __future__ import annotations

import asyncio
import logging
import math

from fastapi import APIRouter, WebSocket, WebSocketDisconnect

from osi3.osi_groundtruth_pb2 import GroundTruth
from google.protobuf.message import DecodeError

from GT_esmini.web.backend.services.osi_bridge import get_bridge

logger = logging.getLogger(__name__)

router = APIRouter()


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

        objects.append({
            "id": obj.id.value,
            "x": round(pos.x, 3),
            "y": round(pos.y, 3),
            "z": round(pos.z, 3),
            "h": round(ori.yaw, 4),
            "speed": round(speed, 3),
        })

    return {
        "type": "ground_truth",
        "sim_time": round(sim_time, 3),
        "object_count": len(objects),
        "objects": objects,
    }


@router.websocket("/ws/osi/{job_id}")
async def osi_websocket(websocket: WebSocket, job_id: str):
    """Stream OSI GroundTruth data as JSON to browser clients."""
    await websocket.accept()
    logger.info("WebSocket OSI client connected for job %s", job_id)

    bridge = get_bridge(job_id)
    if bridge is None or not bridge.running:
        await websocket.send_json({"error": "No active OSI bridge for this job"})
        await websocket.close()
        return

    sub_id, queue = bridge.subscribe_gt(f"ws-{job_id}")
    try:
        while True:
            try:
                raw = await asyncio.wait_for(queue.get(), timeout=2.0)
            except asyncio.TimeoutError:
                if not bridge.running:
                    await websocket.send_json({"type": "end", "reason": "simulation_ended"})
                    break
                continue

            data = _gt_to_json(raw)
            if data is not None:
                await websocket.send_json(data)

    except WebSocketDisconnect:
        logger.info("WebSocket OSI client disconnected for job %s", job_id)
    except Exception as e:
        logger.warning("WebSocket OSI error for job %s: %s", job_id, e)
    finally:
        bridge.unsubscribe_gt(sub_id)
        try:
            await websocket.close()
        except Exception:
            pass
