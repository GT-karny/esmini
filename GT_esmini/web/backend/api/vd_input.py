"""WebSocket endpoint for live manual input → VirtualDriver (manual override).

Receives JSON commands {steering, throttle, brake, buttons} from the web UI and
forwards them to GT_Sim's NetworkInputBridge as the PedalSteer wire format
(magic 'PSTC' + 4 doubles + int32 gear + uint32 buttons = 44 bytes) over UDP.

The run must have been launched with manual override enabled (input_type=network
in the injected variant); the bridge listens on the configured input port
(default 9100)."""

from __future__ import annotations

import json
import logging
import socket
import struct

from fastapi import APIRouter, WebSocket, WebSocketDisconnect

from GT_esmini.web.backend.config import DEFAULT_VD_INPUT_PORT

logger = logging.getLogger(__name__)

router = APIRouter()

# Must match gt_esmini::NetworkInputBridge MAGIC_PEDAL_STEER ('PSTC' = 0x50535443)
# and the wire layout in NetworkInputBridge::Poll().
_MAGIC = 0x50535443
_WIRE = struct.Struct("<I4diI")  # magic, steering, throttle, brake, clutch, gear, buttons
_DEFAULT_INPUT_PORT = DEFAULT_VD_INPUT_PORT


def _pack(steering: float, throttle: float, brake: float, buttons: int,
          clutch: float = 0.0, gear: int = 0) -> bytes:
    return _WIRE.pack(_MAGIC, float(steering), float(throttle), float(brake),
                      float(clutch), int(gear), int(buttons) & 0xFFFFFFFF)


@router.websocket("/ws/input/{job_id}")
async def vd_input_websocket(websocket: WebSocket, job_id: str):
    """Forward manual pedal/steer/indicator commands to GT_Sim over UDP."""
    await websocket.accept()

    # Target port: ?port=<n> (the run's configured input port), default 9100.
    try:
        port = int(websocket.query_params.get("port", _DEFAULT_INPUT_PORT))
    except (TypeError, ValueError):
        port = _DEFAULT_INPUT_PORT

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    logger.info("WebSocket input client connected for job %s -> udp 127.0.0.1:%d", job_id, port)

    try:
        while True:
            raw = await websocket.receive_text()
            try:
                cmd = json.loads(raw)
            except json.JSONDecodeError:
                continue
            packet = _pack(
                steering=cmd.get("steering", 0.0),
                throttle=cmd.get("throttle", 0.0),
                brake=cmd.get("brake", 0.0),
                buttons=cmd.get("buttons", 0),
            )
            try:
                sock.sendto(packet, ("127.0.0.1", port))
            except OSError as e:
                logger.warning("input UDP send error for job %s: %s", job_id, e)

    except WebSocketDisconnect:
        logger.info("WebSocket input client disconnected for job %s", job_id)
    except Exception as e:
        logger.warning("WebSocket input error for job %s: %s", job_id, e)
    finally:
        sock.close()
        try:
            await websocket.close()
        except Exception:
            pass
