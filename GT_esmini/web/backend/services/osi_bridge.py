"""OSI Bridge: receives UDP OSI data from GT_Sim and distributes to subscribers."""

from __future__ import annotations

import asyncio
import logging
import socket
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

# UDP receive buffer to ask the OS for. The static ground truth is one ~283 KB
# message (e6mini) sent as ~35 unpaced 8 KB datagrams, so anything near the
# ~64 KB default loses part of the burst and the frame never reassembles.
# 4 MB holds a burst many times over and costs nothing when idle.
_WANTED_RCVBUF = 4 * 1024 * 1024


@dataclass
class _StreamState:
    """Per-stream subscriber tracking and reassembly state."""

    subscribers: dict[str, asyncio.Queue[bytes]] = field(default_factory=dict)
    transport: asyncio.DatagramTransport | None = None

    # The message that actually CARRIES the static ground truth, kept verbatim.
    #
    # The static content -- lane / lane_boundary / traffic_sign /
    # stationary_object and, since v0.18.0, reference_line / logical_lane /
    # logical_lane_boundary -- is transmitted EXACTLY ONCE, on the first frame
    # (OSIReporter::UpdateOSIGroundTruth's `!osi_initialized_` branch is the only
    # one that calls SerializeDynamicAndStaticData; every later frame takes the
    # DEFAULT static-report mode and serialises dynamic data only).
    #
    # The bridge starts before GT_Sim, so it is listening when that frame is
    # sent -- but a WebSocket client that connects once the run is under way is
    # not, and nothing replays it. Keeping the frame here is what lets a late
    # subscriber still obtain the road network (see OSIBridge.static_frame).
    #
    # This deliberately is NOT "the first complete message". That was the first
    # implementation and it did not work: the static frame is by far the largest
    # (283 KB on e6mini = ~35 UDP packets) and therefore the likeliest to lose a
    # packet, and a lost packet resets reassembly -- so the first message to
    # COMPLETE is usually a later, dynamic-only one. Measured in the packaged
    # build: available=true with lane_count 0, which reads as "the road has no
    # lanes" rather than "the frame was dropped". Same failure shape as the UDP
    # drop that silently substituted an empty scene in the verification harness.
    static_frame: bytes | None = None

    # How many more frames to inspect before giving up on finding static content.
    # Bounded so a run whose static frame really was lost does not parse every
    # frame forever; `static_missing` then says so, which is actionable (raise
    # osi.static_reporting to 2 and every frame carries it).
    static_scan_left: int = 200
    static_missing: bool = False


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
        self._capture_static(complete_msg)

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

    def _capture_static(self, complete_msg: bytes) -> None:
        """Remember the frame that carries the static ground truth, if this is it.

        Parses, because "is this the static frame" cannot be answered from the
        byte length or the arrival order (see _StreamState.static_frame). Bounded
        by `static_scan_left`, and it stops entirely once found -- so the cost is
        a handful of small parses at the start of a run, not per frame forever.
        """
        st = self._stream
        if self._label != "GroundTruth":
            return  # HostVehicleData has no static content
        if st.static_frame is not None or st.static_scan_left <= 0:
            return

        st.static_scan_left -= 1
        if st.static_scan_left == 0:
            st.static_missing = True
            logger.warning(
                "OSI bridge saw no static ground truth in the first frames; the "
                "road network will be unavailable over REST. The static frame is "
                "sent once and is large enough to lose a UDP packet -- raise "
                "osi.static_reporting to 2 to have every frame carry it."
            )

        try:
            gt = GroundTruth()
            gt.ParseFromString(complete_msg)
        except Exception:  # noqa: BLE001 - a truncated frame is not fatal here
            return

        # Any of these means the static block travelled with this frame. `lane`
        # is the broadest test (every road network has lanes); the logical layer
        # is checked too so a build with GT_OSI_LOGICAL_LANE=0 still yields a
        # usable static payload.
        if gt.lane or gt.logical_lane or gt.stationary_object or gt.traffic_sign:
            st.static_frame = complete_msg
            st.static_missing = False
            logger.info(
                "OSI bridge captured the static ground truth (%d bytes, "
                "%d lanes, %d logical lanes)",
                len(complete_msg),
                len(gt.lane),
                len(gt.logical_lane),
            )

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
        self._rcvbuf = -1  # granted receive buffer, filled in by _make_socket

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
            sock=self._make_socket(self._gt_port),
        )
        _, hvd_protocol = await loop.create_datagram_endpoint(
            lambda: _OSIProtocol(self._hvd, "HostVehicleData"),
            sock=self._make_socket(self._hvd_port),
        )

        self._running = True
        logger.info(
            "OSI Bridge started (GT=%s:%d, HVD=%s:%d, rcvbuf=%d)",
            self._bind_ip,
            self._gt_port,
            self._bind_ip,
            self._hvd_port,
            self._rcvbuf,
        )

    def _make_socket(self, port: int) -> socket.socket:
        """A bound UDP socket with a receive buffer big enough for the static frame.

        The default receive buffer (~64 KB on Windows) cannot hold the static
        ground truth: it is ~283 KB on e6mini, sent as ~35 back-to-back 8 KB
        datagrams with no pacing. The buffer overflows mid-burst, a datagram is
        dropped, reassembly resets, and the frame is lost -- every time, so the
        road network simply never arrived over UDP. Measured in the packaged
        v0.18.1 build before this change: 200 frames scanned, zero carrying
        static content.

        Sized to hold a whole burst several times over. The kernel may clamp the
        request, which is why the granted size is logged rather than assumed.
        """
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, _WANTED_RCVBUF)
        except OSError as exc:  # pragma: no cover - platform dependent
            logger.warning("could not raise SO_RCVBUF: %s", exc)
        try:
            self._rcvbuf = s.getsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF)
        except OSError:  # pragma: no cover
            self._rcvbuf = -1
        if 0 <= self._rcvbuf < _WANTED_RCVBUF // 2:
            logger.warning(
                "the OS granted only %d bytes of UDP receive buffer (asked for %d); "
                "the static ground truth may still be lost -- raise "
                "osi.static_reporting to 2 if the road network does not appear",
                self._rcvbuf,
                _WANTED_RCVBUF,
            )
        s.bind((self._bind_ip, port))
        s.setblocking(False)
        return s

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
        return self._gt.static_frame

    @property
    def static_missing(self) -> bool:
        """True once the scan gave up without finding a static-bearing frame.

        Distinguishable from "not yet": this says the network is not coming, and
        why (see _StreamState.static_frame), so the caller can say something
        useful instead of leaving the user staring at an empty road.
        """
        return self._gt.static_missing

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
