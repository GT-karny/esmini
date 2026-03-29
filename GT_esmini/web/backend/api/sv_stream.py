"""WebSocket endpoint for streaming scenario variables to web clients."""

from __future__ import annotations

import asyncio
import json
import logging

from fastapi import APIRouter, WebSocket, WebSocketDisconnect

from GT_esmini.web.backend.services.sv_bridge import get_sv_bridge

logger = logging.getLogger(__name__)

router = APIRouter()


@router.websocket("/ws/sv/{job_id}")
async def sv_websocket(websocket: WebSocket, job_id: str):
    """Stream scenario variables as JSON to browser clients."""
    await websocket.accept()
    logger.info("WebSocket SV client connected for job %s", job_id)

    bridge = get_sv_bridge(job_id)
    if bridge is None or not bridge.running:
        await websocket.send_json({"error": "No active SV bridge for this job"})
        await websocket.close()
        return

    sub_id, queue = bridge.subscribe(f"ws-sv-{job_id}")

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

            data["type"] = "scenario_variables"
            await websocket.send_json(data)

    except WebSocketDisconnect:
        logger.info("WebSocket SV client disconnected for job %s", job_id)
    except Exception as e:
        logger.warning("WebSocket SV error for job %s: %s", job_id, e)
    finally:
        bridge.unsubscribe(sub_id)
        try:
            await websocket.close()
        except Exception:
            pass
