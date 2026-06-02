"""VD Bridge: receives live VirtualDriver telemetry JSON from GT_Sim via UDP
unicast (port 48202) and fans it out to WebSocket subscribers.

Mirrors sv_bridge.py (the Scenario-Variables bridge) but without the multicast
relay — VirtualDriver telemetry is web-only. The GT_Sim wire format is identical:
``[counter: int32][size: uint32][json_bytes]`` per single-packet message.
Multiple WebSocket clients can subscribe to the same telemetry (e.g. the embedded
panel and a popped-out window), so the dispatch fans out to all queues."""

from __future__ import annotations

import asyncio
import logging
import struct
import uuid
from dataclasses import dataclass, field

from GT_esmini.web.backend.config import VD_LISTEN_PORT

logger = logging.getLogger(__name__)

# GT_Sim UDP header: [counter: int32][size: uint32]
_HEADER_SIZE = 8


@dataclass
class _SubscriberState:
    """WebSocket subscriber tracking."""

    subscribers: dict[str, asyncio.Queue[bytes]] = field(default_factory=dict)


class _VdProtocol(asyncio.DatagramProtocol):
    """Receives single-packet telemetry JSON from GT_Sim, dispatches to subscribers."""

    def __init__(self, subs: _SubscriberState) -> None:
        self._subs = subs
        self._transport: asyncio.DatagramTransport | None = None

    def connection_made(self, transport: asyncio.DatagramTransport) -> None:  # type: ignore[override]
        self._transport = transport

    def datagram_received(self, data: bytes, addr: tuple[str, int]) -> None:
        if len(data) < _HEADER_SIZE:
            return

        _counter, size = struct.unpack_from("iI", data)
        payload = data[_HEADER_SIZE:]

        if len(payload) != size:
            return

        # Push to WebSocket subscriber queues (drop-oldest on overflow)
        for _sub_id, queue in list(self._subs.subscribers.items()):
            try:
                queue.put_nowait(payload)
            except asyncio.QueueFull:
                try:
                    queue.get_nowait()
                    queue.put_nowait(payload)
                except (asyncio.QueueEmpty, asyncio.QueueFull):
                    pass

    def error_received(self, exc: Exception) -> None:
        logger.warning("VD UDP error: %s", exc)


class VdBridge:
    """Receives VirtualDriver telemetry JSON via UDP unicast from GT_Sim,
    then fans out to WebSocket subscribers."""

    def __init__(
        self,
        listen_port: int = VD_LISTEN_PORT,
        bind_ip: str = "127.0.0.1",
        max_queue_size: int = 16,
    ) -> None:
        self._listen_port = listen_port
        self._bind_ip = bind_ip
        self._max_queue_size = max_queue_size

        self._subs = _SubscriberState()
        self._transport: asyncio.DatagramTransport | None = None
        self._running = False

    @property
    def running(self) -> bool:
        return self._running

    async def start(self) -> None:
        if self._running:
            return

        loop = asyncio.get_running_loop()
        transport, _protocol = await loop.create_datagram_endpoint(
            lambda: _VdProtocol(self._subs),
            local_addr=(self._bind_ip, self._listen_port),
        )
        self._transport = transport

        self._running = True
        logger.info("VD Bridge started (listen=%s:%d)", self._bind_ip, self._listen_port)

    async def stop(self) -> None:
        if not self._running:
            return

        if self._transport is not None:
            self._transport.close()
            self._transport = None

        self._subs.subscribers.clear()
        self._running = False
        logger.info("VD Bridge stopped")

    def subscribe(self, subscriber_id: str | None = None) -> tuple[str, asyncio.Queue[bytes]]:
        sid = subscriber_id or uuid.uuid4().hex[:8]
        queue: asyncio.Queue[bytes] = asyncio.Queue(maxsize=self._max_queue_size)
        self._subs.subscribers[sid] = queue
        return sid, queue

    def unsubscribe(self, subscriber_id: str) -> None:
        self._subs.subscribers.pop(subscriber_id, None)


# ---------------------------------------------------------------------------
# Global singleton + per-job registry (mirrors sv_bridge)
# ---------------------------------------------------------------------------

_global_bridge: VdBridge | None = None
_vd_bridges: dict[str, VdBridge] = {}


def get_global_vd_bridge() -> VdBridge | None:
    """Return the always-on VD bridge (started at server startup)."""
    return _global_bridge


async def start_global_vd_bridge(listen_port: int = VD_LISTEN_PORT) -> VdBridge:
    """Start the global VD bridge at server startup."""
    global _global_bridge  # noqa: PLW0603
    if _global_bridge is not None and _global_bridge.running:
        return _global_bridge
    _global_bridge = VdBridge(listen_port=listen_port)
    await _global_bridge.start()
    logger.info("Global VD Bridge started")
    return _global_bridge


async def stop_global_vd_bridge() -> None:
    """Stop the global VD bridge at server shutdown."""
    global _global_bridge  # noqa: PLW0603
    if _global_bridge is not None:
        await _global_bridge.stop()
        _global_bridge = None
        logger.info("Global VD Bridge stopped")


def get_vd_bridge(job_id: str) -> VdBridge | None:
    # Prefer the global always-on bridge
    if _global_bridge is not None and _global_bridge.running:
        return _global_bridge
    return _vd_bridges.get(job_id)


async def start_vd_bridge(job_id: str, listen_port: int = VD_LISTEN_PORT) -> VdBridge:
    # If the global bridge is running, just register the alias
    if _global_bridge is not None and _global_bridge.running:
        _vd_bridges[job_id] = _global_bridge
        logger.info("VD Bridge: job %s using global bridge", job_id)
        return _global_bridge

    # Fallback: start a per-job bridge (global bridge not available)
    for old_id in list(_vd_bridges.keys()):
        old = _vd_bridges.pop(old_id, None)
        if old is not None:
            try:
                await old.stop()
            except Exception:
                pass

    bridge = VdBridge(listen_port=listen_port)
    await bridge.start()
    _vd_bridges[job_id] = bridge
    logger.info("VD Bridge registered for job %s", job_id)
    return bridge


async def stop_vd_bridge(job_id: str) -> None:
    bridge = _vd_bridges.pop(job_id, None)
    # Don't stop the global bridge when a job ends
    if bridge is not None and bridge is not _global_bridge:
        await bridge.stop()
        logger.info("VD Bridge removed for job %s", job_id)


async def stop_all_vd_bridges() -> int:
    count = 0
    for job_id in list(_vd_bridges.keys()):
        bridge = _vd_bridges.pop(job_id, None)
        if bridge is not None and bridge is not _global_bridge:
            try:
                await bridge.stop()
                count += 1
            except Exception as e:
                logger.warning("Error stopping VD bridge for %s: %s", job_id, e)
    # Also stop the global bridge during full shutdown
    await stop_global_vd_bridge()
    if _global_bridge is None:
        count += 1
    return count
