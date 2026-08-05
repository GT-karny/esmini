"""Wheel axis probe API (feature:F8).

Spawns bin/GT_WheelProbe.exe -- a read-only SDL2 joystick reader -- and relays
its JSON lines. This is what lets the axis-mapping panel answer "which axis is
the brake on THIS wheel?", and it has to be answered in the SAME SDL index space
SDL2WheelInput reads at runtime.

WHY NOT THE BROWSER GAMEPAD API. The frontend could read axes directly with
navigator.getGamepads(), and the existing BUTTON learn-mode does exactly that.
But the browser exposes its own index space (remapped per device by the
browser), so "assign the axis the user just moved" built on it can confidently
write an index the simulator will read as a different function. The probe exists
to remove that failure mode, not to look sophisticated.

The probe process is short-lived and per-connection: one WebSocket = one child.
It never opens SDL_Haptic, so nothing here can energise a wheel.
"""

from __future__ import annotations

import asyncio
import json
import logging
from typing import Any

from fastapi import APIRouter, HTTPException, WebSocket, WebSocketDisconnect

from GT_esmini.web.backend.config import GT_WHEEL_PROBE_EXE

logger = logging.getLogger(__name__)

router = APIRouter(prefix="/api/wheel-probe", tags=["wheel-probe"])

# The probe is built only with GT_ENABLE_SDL2=ON (OFF by default), so "not
# present" is a normal build configuration rather than an error, and the API
# says so in a way the panel can show verbatim.
_MISSING_MSG = (
    "GT_WheelProbe.exe not found. It is built only when the project is "
    "configured with -DGT_ENABLE_SDL2=ON (the wheel input path). The axis "
    "mapping can still be edited by hand without it, but there will be no live "
    "axis readout."
)

_LIST_TIMEOUT_S = 5.0
# Upper bound on how long a single probe frame may take to arrive before the
# relay gives up. Generous: the probe emits at its own --hz, and a device that
# has stopped reporting is a condition the UI should see (via the per-axis
# `reported` flags), not a connection the server silently drops.
_FRAME_TIMEOUT_S = 15.0


def _probe_available() -> bool:
    return GT_WHEEL_PROBE_EXE.is_file()


def _mapping_args(mapping: dict[str, Any] | None) -> list[str]:
    """Translate an axis-mapping dict (the wire shape's sdl2.axis_mapping) into
    GT_WheelProbe CLI flags.

    Passing the mapping per-connection rather than letting the probe read
    manual_drive.json is deliberate: the panel needs to preview values for a
    mapping the user has edited but NOT yet saved, and it must preview them
    through the real C++ normalizer rather than a JS re-implementation of it.
    """
    if not mapping:
        return []
    flags: list[str] = []
    int_flags = {
        "steer_axis": "--steer-axis",
        "steer_raw_center": "--steer-raw-center",
        "steer_raw_full": "--steer-raw-full",
        "throttle_axis": "--throttle-axis",
        "throttle_raw_released": "--throttle-raw-released",
        "throttle_raw_full": "--throttle-raw-full",
        "brake_axis": "--brake-axis",
        "brake_raw_released": "--brake-raw-released",
        "brake_raw_full": "--brake-raw-full",
        "clutch_axis": "--clutch-axis",
        "clutch_raw_released": "--clutch-raw-released",
        "clutch_raw_full": "--clutch-raw-full",
    }
    for key, flag in int_flags.items():
        value = mapping.get(key)
        if value is None:
            continue
        try:
            flags += [flag, str(int(value))]
        except (TypeError, ValueError):
            # A malformed value is dropped rather than forwarded: the probe
            # rejects a non-integer flag value with exit code 2, which would
            # turn one bad field into "no live readout at all".
            logger.warning("wheel-probe: ignoring non-integer %s=%r", key, value)
    if mapping.get("steer_invert"):
        flags.append("--steer-invert")
    return flags


@router.get("/devices")
async def list_devices() -> dict[str, Any]:
    """Enumerate connected joysticks (name / axis count / button count)."""
    if not _probe_available():
        raise HTTPException(status_code=503, detail=_MISSING_MSG)

    proc = await asyncio.create_subprocess_exec(
        str(GT_WHEEL_PROBE_EXE),
        "--list",
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.PIPE,
    )
    try:
        stdout, stderr = await asyncio.wait_for(proc.communicate(), _LIST_TIMEOUT_S)
    except asyncio.TimeoutError:
        proc.kill()
        raise HTTPException(
            status_code=504, detail="GT_WheelProbe --list did not return in time"
        ) from None

    text = stdout.decode("utf-8", errors="replace").strip()
    if proc.returncode != 0 or not text:
        detail = (
            stderr.decode("utf-8", errors="replace").strip() or text or "unknown error"
        )
        raise HTTPException(
            status_code=500,
            detail=f"GT_WheelProbe --list failed (exit {proc.returncode}): {detail}",
        )
    # The probe prints exactly one JSON object for --list. Parse it here rather
    # than passing the text through, so a malformed response fails loudly at the
    # boundary instead of inside the frontend.
    try:
        return json.loads(text.splitlines()[-1])
    except json.JSONDecodeError as exc:
        raise HTTPException(
            status_code=500,
            detail=f"GT_WheelProbe --list returned non-JSON: {text[:200]}",
        ) from exc


@router.get("/status")
async def probe_status() -> dict[str, Any]:
    """Whether the live readout is available at all, for the panel to branch on
    before it offers a 'detect' button that could not work."""
    return {
        "available": _probe_available(),
        "path": str(GT_WHEEL_PROBE_EXE),
        "message": None if _probe_available() else _MISSING_MSG,
    }


@router.websocket("/stream")
async def wheel_probe_websocket(websocket: WebSocket) -> None:
    """Stream live axis/button state (ws://<host>/api/wheel-probe/stream).

    Query parameters: ``device`` (index, default 0), ``hz`` (default 30).
    The client may also send a JSON object at any time to change the mapping
    used for the normalized preview; the child is restarted with the new flags
    (the probe takes its mapping at startup, and a restart is ~50 ms).
    """
    await websocket.accept()

    if not _probe_available():
        await websocket.send_json({"type": "error", "message": _MISSING_MSG})
        await websocket.close()
        return

    def _query_int(name: str, default: int) -> int:
        raw = websocket.query_params.get(name)
        if raw is None:
            return default
        try:
            return int(raw)
        except ValueError:
            return default

    device = _query_int("device", 0)
    hz = _query_int("hz", 30)
    mapping: dict[str, Any] | None = None

    proc: asyncio.subprocess.Process | None = None

    async def start_probe() -> asyncio.subprocess.Process:
        return await asyncio.create_subprocess_exec(
            str(GT_WHEEL_PROBE_EXE),
            "--device",
            str(device),
            "--hz",
            str(hz),
            *_mapping_args(mapping),
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.DEVNULL,
        )

    async def stop_probe(p: asyncio.subprocess.Process | None) -> None:
        if p is None or p.returncode is not None:
            return
        p.kill()
        try:
            await asyncio.wait_for(p.wait(), 2.0)
        except asyncio.TimeoutError:
            logger.warning("wheel-probe: child %s did not exit after kill", p.pid)

    async def relay(p: asyncio.subprocess.Process) -> None:
        """Forward the child's JSON lines to the socket until it stops."""
        assert p.stdout is not None
        while True:
            line = await asyncio.wait_for(p.stdout.readline(), _FRAME_TIMEOUT_S)
            if not line:
                return
            text = line.decode("utf-8", errors="replace").strip()
            if not text:
                continue
            try:
                await websocket.send_text(text)
            except (WebSocketDisconnect, RuntimeError):
                return

    async def next_mapping_request() -> dict[str, Any]:
        """Wait for the client to ask for a different preview mapping.

        Returns the requested mapping; raises WebSocketDisconnect when the
        client goes away. Restarting the child is the CALLER's job -- an earlier
        draft did it here and had to reach back into the relay task to cancel
        it, which is the kind of two-owners-one-process arrangement that leaks
        children.
        """
        while True:
            payload = await websocket.receive_json()
            if isinstance(payload, dict):
                requested = payload.get("axis_mapping")
                if isinstance(requested, dict):
                    return requested

    try:
        proc = await start_probe()
        while True:
            relay_task = asyncio.create_task(relay(proc))
            recv_task = asyncio.create_task(next_mapping_request())
            done, pending = await asyncio.wait(
                {relay_task, recv_task}, return_when=asyncio.FIRST_COMPLETED
            )
            for task in pending:
                task.cancel()

            if recv_task in done and recv_task.exception() is None:
                # Mapping edited in the GUI: restart the child with new flags.
                # The probe fixes its mapping at startup, and a restart is ~50 ms
                # -- cheaper than teaching the probe a control channel.
                mapping = recv_task.result()
                await stop_probe(proc)
                proc = await start_probe()
                continue

            # Otherwise we are done: either the child exited (device unplugged,
            # bad index) or the socket closed / sent something unusable.
            break
    except (WebSocketDisconnect, asyncio.TimeoutError):
        pass
    except Exception:  # noqa: BLE001 - a probe failure must not take the server down
        logger.exception("wheel-probe: relay failed")
    finally:
        await stop_probe(proc)
        try:
            await websocket.close()
        except RuntimeError:
            pass
