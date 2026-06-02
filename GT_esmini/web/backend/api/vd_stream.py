"""WebSocket endpoint for streaming live VirtualDriver telemetry to web clients.

Mirrors sv_stream.py. The frame shape is identical to the recorded replay
(GT_GetVirtualDriverTelemetry / telemetry.jsonl), so the same overlay component
drives both live and replay."""

from __future__ import annotations

import asyncio
import json
import logging

from fastapi import APIRouter, WebSocket, WebSocketDisconnect

from GT_esmini.web.backend.services.vd_bridge import get_vd_bridge

logger = logging.getLogger(__name__)

router = APIRouter()


@router.websocket("/ws/vd/{job_id}")
async def vd_websocket(websocket: WebSocket, job_id: str):
    """Stream VirtualDriver telemetry as JSON to browser clients."""
    await websocket.accept()
    logger.info("WebSocket VD client connected for job %s", job_id)

    bridge = get_vd_bridge(job_id)
    if bridge is None or not bridge.running:
        await websocket.send_json({"error": "No active VD bridge for this job"})
        await websocket.close()
        return

    sub_id, queue = bridge.subscribe(f"ws-vd-{job_id}")

    try:
        while True:
            try:
                raw = await asyncio.wait_for(queue.get(), timeout=2.0)
            except asyncio.TimeoutError:
                if not bridge.running:
                    await websocket.send_json({"type": "end", "reason": "simulation_ended"})
                    break
                continue

            try:
                data = json.loads(raw)
            except (json.JSONDecodeError, UnicodeDecodeError):
                continue

            data["type"] = "virtual_driver_telemetry"
            await websocket.send_json(data)

    except WebSocketDisconnect:
        logger.info("WebSocket VD client disconnected for job %s", job_id)
    except Exception as e:
        logger.warning("WebSocket VD error for job %s: %s", job_id, e)
    finally:
        bridge.unsubscribe(sub_id)
        try:
            await websocket.close()
        except Exception:
            pass
