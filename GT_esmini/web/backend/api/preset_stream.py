"""WebSocket endpoint for live preset YAML change notifications."""

from __future__ import annotations

import asyncio
import logging

from fastapi import APIRouter, WebSocket, WebSocketDisconnect

from GT_esmini.web.backend.services.preset_watcher import get_preset_watcher_manager

logger = logging.getLogger(__name__)

router = APIRouter()


@router.websocket("/ws/presets/{project_id}")
async def preset_websocket(websocket: WebSocket, project_id: str):
    """Push presets_changed events to the browser when YAML files in
    ``<project_root>/presets/`` are modified externally."""
    await websocket.accept()
    manager = get_preset_watcher_manager()
    try:
        sub_id, queue = await manager.subscribe(project_id)
    except Exception as e:
        logger.warning("Failed to subscribe preset watcher for %s: %s", project_id, e)
        try:
            await websocket.send_json({"type": "error", "message": str(e)})
        finally:
            await websocket.close()
        return

    logger.info("Preset WS connected: project=%s sub=%s", project_id, sub_id)
    try:
        while True:
            try:
                message = await asyncio.wait_for(queue.get(), timeout=30.0)
            except asyncio.TimeoutError:
                # Heartbeat to keep proxies / Electron from reaping idle WS.
                await websocket.send_json({"type": "ping"})
                continue
            await websocket.send_json(message)
    except WebSocketDisconnect:
        logger.info("Preset WS disconnected: project=%s sub=%s", project_id, sub_id)
    except Exception as e:
        logger.warning("Preset WS error project=%s: %s", project_id, e)
    finally:
        await manager.unsubscribe(project_id, sub_id)
        try:
            await websocket.close()
        except Exception:
            pass
