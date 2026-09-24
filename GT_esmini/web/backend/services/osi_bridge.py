"""OSI Bridge: receives UDP OSI data from GT_Sim and distributes to subscribers."""

from __future__ import annotations

import asyncio
import logging
import struct
import uuid
from dataclasses import dataclass, field
from typing import Any

from osi3.osi_groundtruth_pb2 import GroundTruth
from osi3.osi_hostvehicledata_pb2 import HostVehicleData
from google.protobuf.message import DecodeError

from GT_esmini.web.backend.config import OSI_GT_PORT, OSI_HVD_PORT

logger = logging.getLogger(__name__)

# esmini UDP packet format: [counter: int32][size: uint32][data: bytes]
_HEADER_SIZE = 8  # 4 (counter) + 4 (size)
_MAX_PACKET_SIZE = 8208  # 8192 data + 8 header (contract with esmini)


@dataclass
class _StreamState:
    """Per-stream subscriber tracking and reassembly state."""

    subscribers: dict[str, asyncio.Queue[bytes]] = field(default_factory=dict)
    transport: asyncio.DatagramTransport | None = None

    # First complete message on this stream, kept verbatim.
    #
    # The static GroundTruth -- lane / lane_boundary / traffic_sign /
    # stationary_object and, since v0.18.0, reference_line / logical_lane /
    # logical_lane_boundary -- is transmitted EXACTLY ONCE, on the first frame
    # (OSIReporter::UpdateOSIGroundTruth's `!osi_initialized_` branch is the only
    # one that calls SerializeDynamicAndStaticData; every later frame takes the
    # DEFAULT static-report mode and serialises dynamic data only).
    #
    # The bridge starts before GT_Sim, so IT always sees that frame -- but a
    # WebSocket client that connects once the run is under way does not, and
    # nothing replays it. Keeping the frame here is what lets a late subscriber
    # still obtain the road network (see OSIBridge.static_frame).
    first_frame: bytes | None = None


class _OSIProtocol(asyncio.DatagramProtocol):
    """asyncio DatagramProtocol that reassembles multi-packet OSI messages."""

    def __init__(self, stream: _StreamState, label: str) -> None:
        self._stream = stream
        self._label = label
        self._buffer = b""
        self._next_index: int | None = None

    def connection_made(self, transport: asyncio.DatagramTransport) -> None:  # type: ignore[override]
        self._stream.transport = transport

    def datagram_received(self, data: bytes, addr: tuple[str, int]) -> None:
        if len(data) < _HEADER_SIZE:
            return

        counter, size = struct.unpack_from("iI", data)
        frame = data[_HEADER_SIZE:]

        if not (len(frame) == size == len(data) - _HEADER_SIZE):
            self._reset()
            return

        # counter == 0: single complete message (e.g. HostVehicleData)
        if counter == 0:
            self._dispatch(frame)
            self._reset()
            return

        # Multi-packet reassembly (GroundTruth): counter starts at 1,
        # last packet indicated by negative counter.
        index = abs(counter)
        if self._next_index is None:
            if index != 1:
                return
            self._next_index = 1
            self._buffer = b""

        if index == self._next_index:
            self._buffer += frame
            self._next_index += 1
            if counter < 0:  # negative counter = last packet
                self._dispatch(self._buffer)
                self._reset()
        else:
            self._reset()

    def _dispatch(self, complete_msg: bytes) -> None:
        """Push raw protobuf bytes to all subscribers."""
        # Keep the first frame verbatim: it is the only one carrying the static
        # ground truth (see _StreamState.first_frame). Cheap -- one assignment,
        # no parsing, and only on the very first message of the stream.
        if self._stream.first_frame is None:
            self._stream.first_frame = complete_msg

        for sub_id, queue in list(self._stream.subscribers.items()):
            try:
                queue.put_nowait(complete_msg)
            except asyncio.QueueFull:
                # Drop oldest to prevent backpressure stall
                try:
                    queue.get_nowait()
                    queue.put_nowait(complete_msg)
                except (asyncio.QueueEmpty, asyncio.QueueFull):
                    pass

    def _reset(self) -> None:
        self._buffer = b""
        self._next_index = None

    def error_received(self, exc: Exception) -> None:
        logger.warning("OSI %s UDP error: %s", self._label, exc)


class OSIBridge:
    """Receives OSI UDP from GT_Sim and distributes to gRPC / WebSocket subscribers.

    Each bridge instance manages two UDP streams (GroundTruth + HostVehicleData).
    Subscribers receive raw protobuf bytes via asyncio.Queue.
    """

    def __init__(
        self,
        gt_port: int = OSI_GT_PORT,
        hvd_port: int = OSI_HVD_PORT,
        bind_ip: str = "127.0.0.1",
        max_queue_size: int = 16,
    ) -> None:
        self._gt_port = gt_port
        self._hvd_port = hvd_port
        self._bind_ip = bind_ip
        self._max_queue_size = max_queue_size

        self._gt = _StreamState()
        self._hvd = _StreamState()
        self._running = False

    @property
    def running(self) -> bool:
        return self._running

    async def start(self) -> None:
        """Start listening for UDP OSI data."""
        if self._running:
            return

        loop = asyncio.get_running_loop()

        _, gt_protocol = await loop.create_datagram_endpoint(
            lambda: _OSIProtocol(self._gt, "GroundTruth"),
            local_addr=(self._bind_ip, self._gt_port),
        )
        _, hvd_protocol = await loop.create_datagram_endpoint(
            lambda: _OSIProtocol(self._hvd, "HostVehicleData"),
            local_addr=(self._bind_ip, self._hvd_port),
        )

        self._running = True
        logger.info(
            "OSI Bridge started (GT=%s:%d, HVD=%s:%d)",
            self._bind_ip,
            self._gt_port,
            self._bind_ip,
            self._hvd_port,
        )

    async def stop(self) -> None:
        """Stop listening and clean up."""
        if not self._running:
            return

        for stream in (self._gt, self._hvd):
            if stream.transport is not None:
                stream.transport.close()
                stream.transport = None
            stream.subscribers.clear()

        self._running = False
        logger.info("OSI Bridge stopped")

    @property
    def static_frame(self) -> bytes | None:
        """The first GroundTruth frame, or None if nothing has arrived yet.

        This is the only frame that carries the static ground truth (road
        network, signs, and the logical lane layer). Consumers that need the
        network but connected after the run started read it from here rather
        than waiting for a replay that never comes.
        """
        return self._gt.first_frame

    def subscribe_gt(
        self, subscriber_id: str | None = None
    ) -> tuple[str, asyncio.Queue[bytes]]:
        """Subscribe to GroundTruth stream. Returns (subscriber_id, queue)."""
        return self._subscribe(self._gt, subscriber_id)

    def subscribe_hvd(
        self, subscriber_id: str | None = None
    ) -> tuple[str, asyncio.Queue[bytes]]:
        """Subscribe to HostVehicleData stream. Returns (subscriber_id, queue)."""
        return self._subscribe(self._hvd, subscriber_id)

    def unsubscribe_gt(self, subscriber_id: str) -> None:
        self._gt.subscribers.pop(subscriber_id, None)

    def unsubscribe_hvd(self, subscriber_id: str) -> None:
        self._hvd.subscribers.pop(subscriber_id, None)

    def _subscribe(
        self, stream: _StreamState, subscriber_id: str | None
    ) -> tuple[str, asyncio.Queue[bytes]]:
        sid = subscriber_id or uuid.uuid4().hex[:8]
        queue: asyncio.Queue[bytes] = asyncio.Queue(maxsize=self._max_queue_size)
        stream.subscribers[sid] = queue
        return sid, queue


# Global bridge registry: job_id → OSIBridge
_bridges: dict[str, OSIBridge] = {}


def get_bridge(job_id: str) -> OSIBridge | None:
    """Get the OSI bridge for a running simulation."""
    return _bridges.get(job_id)


async def start_bridge(
    job_id: str,
    gt_port: int = OSI_GT_PORT,
    hvd_port: int = OSI_HVD_PORT,
) -> OSIBridge:
    """Create and start an OSI bridge for a simulation job."""
    # Stop any existing bridges to free ports
    for old_id in list(_bridges.keys()):
        old_bridge = _bridges.pop(old_id, None)
        if old_bridge is not None:
            try:
                await old_bridge.stop()
                logger.info("OSI Bridge stopped stale bridge for job %s", old_id)
            except Exception:
                pass

    bridge = OSIBridge(gt_port=gt_port, hvd_port=hvd_port)
    await bridge.start()
    _bridges[job_id] = bridge
    logger.info("OSI Bridge registered for job %s", job_id)
    return bridge


async def stop_bridge(job_id: str) -> None:
    """Stop and remove the OSI bridge for a simulation job."""
    bridge = _bridges.pop(job_id, None)
    if bridge is not None:
        await bridge.stop()
        logger.info("OSI Bridge removed for job %s", job_id)


async def stop_all_bridges() -> int:
    """Stop and remove all active OSI bridges.

    Called during server shutdown. Returns count of bridges stopped.
    """
    count = 0
    for job_id in list(_bridges.keys()):
        bridge = _bridges.pop(job_id, None)
        if bridge is not None:
            try:
                await bridge.stop()
                count += 1
                logger.info("Shutdown: stopped OSI bridge for job %s", job_id)
            except Exception as e:
                logger.warning(
                    "Error stopping bridge for %s during shutdown: %s", job_id, e
                )
    return count
