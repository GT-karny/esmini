"""Shared helpers for single-packet framed UDP fan-out bridges."""

from __future__ import annotations

import asyncio
import logging
import struct
import uuid
from dataclasses import dataclass, field
from typing import Callable, Generic, TypeVar

logger = logging.getLogger(__name__)

# GT_Sim UDP header: [counter: int32][size: uint32]
_HEADER_SIZE = 8


@dataclass
class _SubscriberState:
    subscribers: dict[str, asyncio.Queue[bytes]] = field(default_factory=dict)


class _FramedUdpProtocol(asyncio.DatagramProtocol):
    """Receives single-packet framed payloads and forwards valid frames."""

    # Rate-limit the size-mismatch diagnostic: log the first occurrence and then
    # every Nth after, so a persistent header/truncation mismatch stays visible
    # without flooding the log at datagram rate.
    _MISMATCH_LOG_EVERY = 500

    def __init__(self, label: str, on_payload: Callable[[bytes], None]) -> None:
        self._label = label
        self._on_payload = on_payload
        self._transport: asyncio.DatagramTransport | None = None
        self._mismatch_count = 0

    def connection_made(self, transport: asyncio.DatagramTransport) -> None:  # type: ignore[override]
        self._transport = transport

    def datagram_received(self, data: bytes, addr: tuple[str, int]) -> None:
        if len(data) < _HEADER_SIZE:
            return

        _counter, size = struct.unpack_from("iI", data)
        payload = data[_HEADER_SIZE:]

        if len(payload) != size:
            # A header-size/truncation mismatch drops the frame; without a log this
            # looks like a healthy connection delivering zero frames. Warn (rate-limited).
            self._mismatch_count += 1
            if (
                self._mismatch_count == 1
                or self._mismatch_count % self._MISMATCH_LOG_EVERY == 0
            ):
                logger.warning(
                    "%s dropped framed datagram: payload len=%d != header size=%d "
                    "(total=%d, mismatches so far=%d)",
                    self._label,
                    len(payload),
                    size,
                    len(data),
                    self._mismatch_count,
                )
            return

        self._on_payload(payload)

    def error_received(self, exc: Exception) -> None:
        logger.warning("%s UDP error: %s", self._label, exc)


class FramedUdpFanoutBridge:
    """UDP listener for GT_Sim single-packet framed payloads.

    The wire format is ``[counter: int32][size: uint32][payload]``. Valid payloads
    are optionally relayed by subclasses and then fanned out to WebSocket queues.
    """

    def __init__(
        self,
        *,
        label: str,
        listen_port: int,
        bind_ip: str = "127.0.0.1",
        max_queue_size: int = 16,
    ) -> None:
        self._label = label
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

        try:
            self._before_start()
            loop = asyncio.get_running_loop()
            transport, _protocol = await loop.create_datagram_endpoint(
                lambda: _FramedUdpProtocol(self._label, self._handle_payload),
                local_addr=(self._bind_ip, self._listen_port),
            )
        except Exception:
            self._after_stop()
            raise

        self._transport = transport
        self._running = True
        self._log_started()

    async def stop(self) -> None:
        if not self._running:
            return

        if self._transport is not None:
            self._transport.close()
            self._transport = None

        self._after_stop()
        self._subs.subscribers.clear()
        self._running = False
        logger.info("%s Bridge stopped", self._label)

    def subscribe(
        self, subscriber_id: str | None = None
    ) -> tuple[str, asyncio.Queue[bytes]]:
        sid = subscriber_id or uuid.uuid4().hex[:8]
        queue: asyncio.Queue[bytes] = asyncio.Queue(maxsize=self._max_queue_size)
        self._subs.subscribers[sid] = queue
        return sid, queue

    def unsubscribe(self, subscriber_id: str) -> None:
        self._subs.subscribers.pop(subscriber_id, None)

    def _before_start(self) -> None:
        pass

    def _after_stop(self) -> None:
        pass

    def _relay_payload(self, payload: bytes) -> None:
        pass

    def _log_started(self) -> None:
        logger.info(
            "%s Bridge started (listen=%s:%d)",
            self._label,
            self._bind_ip,
            self._listen_port,
        )

    def _handle_payload(self, payload: bytes) -> None:
        self._relay_payload(payload)

        for _sub_id, queue in list(self._subs.subscribers.items()):
            try:
                queue.put_nowait(payload)
            except asyncio.QueueFull:
                try:
                    queue.get_nowait()
                    queue.put_nowait(payload)
                except (asyncio.QueueEmpty, asyncio.QueueFull):
                    pass


TBridge = TypeVar("TBridge", bound=FramedUdpFanoutBridge)


class BridgeRegistry(Generic[TBridge]):
    """Global-plus-per-job registry for always-on telemetry bridges."""

    def __init__(self, label: str, factory: Callable[[int], TBridge]) -> None:
        self._label = label
        self._factory = factory
        self._global_bridge: TBridge | None = None
        self._job_bridges: dict[str, TBridge] = {}

    def get_global(self) -> TBridge | None:
        return self._global_bridge

    async def start_global(self, listen_port: int) -> TBridge:
        if self._global_bridge is not None and self._global_bridge.running:
            return self._global_bridge

        self._global_bridge = self._factory(listen_port)
        await self._global_bridge.start()
        logger.info("Global %s Bridge started", self._label)
        return self._global_bridge

    async def stop_global(self) -> None:
        if self._global_bridge is not None:
            await self._global_bridge.stop()
            self._global_bridge = None
            logger.info("Global %s Bridge stopped", self._label)

    def get_for_job(self, job_id: str) -> TBridge | None:
        if self._global_bridge is not None and self._global_bridge.running:
            return self._global_bridge
        return self._job_bridges.get(job_id)

    async def start_for_job(self, job_id: str, listen_port: int) -> TBridge:
        if self._global_bridge is not None and self._global_bridge.running:
            self._job_bridges[job_id] = self._global_bridge
            logger.info("%s Bridge: job %s using global bridge", self._label, job_id)
            return self._global_bridge

        for old_id in list(self._job_bridges.keys()):
            old = self._job_bridges.pop(old_id, None)
            if old is not None:
                try:
                    await old.stop()
                except Exception:
                    pass

        bridge = self._factory(listen_port)
        await bridge.start()
        self._job_bridges[job_id] = bridge
        logger.info("%s Bridge registered for job %s", self._label, job_id)
        return bridge

    async def stop_for_job(self, job_id: str) -> None:
        bridge = self._job_bridges.pop(job_id, None)
        if bridge is not None and bridge is not self._global_bridge:
            await bridge.stop()
            logger.info("%s Bridge removed for job %s", self._label, job_id)

    async def stop_all(self) -> int:
        count = 0
        for job_id in list(self._job_bridges.keys()):
            bridge = self._job_bridges.pop(job_id, None)
            if bridge is not None and bridge is not self._global_bridge:
                try:
                    await bridge.stop()
                    count += 1
                except Exception as e:
                    logger.warning(
                        "Error stopping %s bridge for %s: %s", self._label, job_id, e
                    )

        await self.stop_global()
        if self._global_bridge is None:
            count += 1
        return count
