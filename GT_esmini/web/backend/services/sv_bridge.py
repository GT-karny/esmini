"""SV Bridge: receives scenario variable JSON from GT_Sim via UDP unicast
and re-distributes to UDP multicast subscribers and WebSocket clients."""

from __future__ import annotations

import asyncio
import logging
import socket
import struct
import uuid
from dataclasses import dataclass, field

from GT_esmini.web.backend.config import SV_LISTEN_PORT, SV_MULTICAST_GROUP, SV_MULTICAST_PORT

logger = logging.getLogger(__name__)

# GT_Sim UDP header: [counter: int32][size: uint32]
_HEADER_SIZE = 8


@dataclass
class _SubscriberState:
    """WebSocket subscriber tracking."""

    subscribers: dict[str, asyncio.Queue[bytes]] = field(default_factory=dict)


class _SvProtocol(asyncio.DatagramProtocol):
    """Receives single-packet JSON from GT_Sim, dispatches to subscribers and multicast."""

    def __init__(
        self,
        subs: _SubscriberState,
        multicast_sock: socket.socket | None,
        multicast_group: str,
        multicast_port: int,
    ) -> None:
        self._subs = subs
        self._mc_sock = multicast_sock
        self._mc_group = multicast_group
        self._mc_port = multicast_port
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

        # 1. Forward raw JSON to multicast (UE / Unity / Python consumers)
        if self._mc_sock is not None:
            try:
                self._mc_sock.sendto(payload, (self._mc_group, self._mc_port))
            except OSError as e:
                logger.warning("SV multicast send error: %s", e)

        # 2. Push to WebSocket subscriber queues
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
        logger.warning("SV UDP error: %s", exc)


class SvBridge:
    """Receives scenario variable JSON via UDP unicast from GT_Sim,
    then fans out to UDP multicast and WebSocket subscribers."""

    def __init__(
        self,
        listen_port: int = SV_LISTEN_PORT,
        bind_ip: str = "127.0.0.1",
        multicast_group: str = SV_MULTICAST_GROUP,
        multicast_port: int = SV_MULTICAST_PORT,
        max_queue_size: int = 16,
    ) -> None:
        self._listen_port = listen_port
        self._bind_ip = bind_ip
        self._multicast_group = multicast_group
        self._multicast_port = multicast_port
        self._max_queue_size = max_queue_size

        self._subs = _SubscriberState()
        self._transport: asyncio.DatagramTransport | None = None
        self._mc_sock: socket.socket | None = None
        self._running = False

    @property
    def running(self) -> bool:
        return self._running

    async def start(self) -> None:
        if self._running:
            return

        # Create multicast sender socket
        try:
            mc = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
            mc.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 2)
            self._mc_sock = mc
        except OSError as e:
            logger.warning("SV Bridge: failed to create multicast socket: %s", e)
            self._mc_sock = None

        # Create unicast UDP listener (receives from GT_Sim)
        loop = asyncio.get_running_loop()
        transport, _protocol = await loop.create_datagram_endpoint(
            lambda: _SvProtocol(
                self._subs, self._mc_sock, self._multicast_group, self._multicast_port
            ),
            local_addr=(self._bind_ip, self._listen_port),
        )
        self._transport = transport

        self._running = True
        logger.info(
            "SV Bridge started (listen=%s:%d, multicast=%s:%d)",
            self._bind_ip,
            self._listen_port,
            self._multicast_group,
            self._multicast_port,
        )

    async def stop(self) -> None:
        if not self._running:
            return

        if self._transport is not None:
            self._transport.close()
            self._transport = None

        if self._mc_sock is not None:
            self._mc_sock.close()
            self._mc_sock = None

        self._subs.subscribers.clear()
        self._running = False
        logger.info("SV Bridge stopped")

    def subscribe(self, subscriber_id: str | None = None) -> tuple[str, asyncio.Queue[bytes]]:
        sid = subscriber_id or uuid.uuid4().hex[:8]
        queue: asyncio.Queue[bytes] = asyncio.Queue(maxsize=self._max_queue_size)
        self._subs.subscribers[sid] = queue
        return sid, queue

    def unsubscribe(self, subscriber_id: str) -> None:
        self._subs.subscribers.pop(subscriber_id, None)


# ---------------------------------------------------------------------------
# Global singleton + per-job registry
# ---------------------------------------------------------------------------

_global_bridge: SvBridge | None = None
_sv_bridges: dict[str, SvBridge] = {}


def get_global_sv_bridge() -> SvBridge | None:
    """Return the always-on SV bridge (started at server startup)."""
    return _global_bridge


async def start_global_sv_bridge(listen_port: int = SV_LISTEN_PORT) -> SvBridge:
    """Start the global SV bridge at server startup."""
    global _global_bridge  # noqa: PLW0603
    if _global_bridge is not None and _global_bridge.running:
        return _global_bridge
    _global_bridge = SvBridge(listen_port=listen_port)
    await _global_bridge.start()
    logger.info("Global SV Bridge started")
    return _global_bridge


async def stop_global_sv_bridge() -> None:
    """Stop the global SV bridge at server shutdown."""
    global _global_bridge  # noqa: PLW0603
    if _global_bridge is not None:
        await _global_bridge.stop()
        _global_bridge = None
        logger.info("Global SV Bridge stopped")


def get_sv_bridge(job_id: str) -> SvBridge | None:
    # Prefer the global always-on bridge
    if _global_bridge is not None and _global_bridge.running:
        return _global_bridge
    return _sv_bridges.get(job_id)


async def start_sv_bridge(
    job_id: str,
    listen_port: int = SV_LISTEN_PORT,
) -> SvBridge:
    # If global bridge is already running, just register the alias
    if _global_bridge is not None and _global_bridge.running:
        _sv_bridges[job_id] = _global_bridge
        logger.info("SV Bridge: job %s using global bridge", job_id)
        return _global_bridge

    # Fallback: start a per-job bridge (global bridge not available)
    for old_id in list(_sv_bridges.keys()):
        old = _sv_bridges.pop(old_id, None)
        if old is not None:
            try:
                await old.stop()
            except Exception:
                pass

    bridge = SvBridge(listen_port=listen_port)
    await bridge.start()
    _sv_bridges[job_id] = bridge
    logger.info("SV Bridge registered for job %s", job_id)
    return bridge


async def stop_sv_bridge(job_id: str) -> None:
    bridge = _sv_bridges.pop(job_id, None)
    # Don't stop the global bridge when a job ends
    if bridge is not None and bridge is not _global_bridge:
        await bridge.stop()
        logger.info("SV Bridge removed for job %s", job_id)


async def stop_all_sv_bridges() -> int:
    count = 0
    for job_id in list(_sv_bridges.keys()):
        bridge = _sv_bridges.pop(job_id, None)
        if bridge is not None and bridge is not _global_bridge:
            try:
                await bridge.stop()
                count += 1
            except Exception as e:
                logger.warning("Error stopping SV bridge for %s: %s", job_id, e)
    # Also stop the global bridge during full shutdown
    await stop_global_sv_bridge()
    if _global_bridge is None:
        count += 1
    return count
