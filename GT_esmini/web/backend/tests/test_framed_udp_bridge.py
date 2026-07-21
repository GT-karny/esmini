"""Tests for services/framed_udp_bridge.py — the shared fan-out bridge core that
vd_bridge / sv_bridge were unified onto (WEB-3)."""

from __future__ import annotations

import asyncio
import socket
import struct

from GT_esmini.web.backend.services.framed_udp_bridge import (
    BridgeRegistry,
    FramedUdpFanoutBridge,
    _FramedUdpProtocol,
)
from GT_esmini.web.backend.services.sv_bridge import SvBridge
from GT_esmini.web.backend.services.vd_bridge import VdBridge


def _dgram(counter: int, payload: bytes, size: int | None = None) -> bytes:
    return struct.pack("iI", counter, len(payload) if size is None else size) + payload


# ---------------------------------------------------------------------------
# _FramedUdpProtocol framing
# ---------------------------------------------------------------------------


def test_protocol_forwards_valid_frame():
    got = []
    proto = _FramedUdpProtocol("T", got.append)
    proto.datagram_received(_dgram(1, b'{"a":1}'), ("127.0.0.1", 1))
    assert got == [b'{"a":1}']


def test_protocol_ignores_short_datagram():
    got = []
    proto = _FramedUdpProtocol("T", got.append)
    proto.datagram_received(b"\x00\x01\x02", ("127.0.0.1", 1))
    assert got == []


def test_protocol_drops_size_mismatch():
    got = []
    proto = _FramedUdpProtocol("T", got.append)
    proto.datagram_received(_dgram(1, b"abc", size=99), ("127.0.0.1", 1))
    assert got == []
    assert proto._mismatch_count == 1


# ---------------------------------------------------------------------------
# fan-out semantics
# ---------------------------------------------------------------------------


def test_fanout_delivers_to_all_subscribers():
    bridge = FramedUdpFanoutBridge(label="T", listen_port=0)
    _, q1 = bridge.subscribe("a")
    _, q2 = bridge.subscribe("b")
    bridge._handle_payload(b"x")
    assert q1.get_nowait() == b"x"
    assert q2.get_nowait() == b"x"


def test_fanout_drops_oldest_on_full_queue():
    bridge = FramedUdpFanoutBridge(label="T", listen_port=0, max_queue_size=2)
    _, q = bridge.subscribe("slow")
    for payload in (b"1", b"2", b"3"):
        bridge._handle_payload(payload)
    # queue kept the two NEWEST frames
    assert q.get_nowait() == b"2"
    assert q.get_nowait() == b"3"


def test_unsubscribe_stops_delivery():
    bridge = FramedUdpFanoutBridge(label="T", listen_port=0)
    sid, q = bridge.subscribe()
    bridge.unsubscribe(sid)
    bridge._handle_payload(b"x")
    assert q.empty()


# ---------------------------------------------------------------------------
# real UDP end-to-end (loopback, ephemeral port)
# ---------------------------------------------------------------------------


def _free_udp_port() -> int:
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


async def test_bridge_receives_framed_udp():
    port = _free_udp_port()
    bridge = VdBridge(listen_port=port)
    await bridge.start()
    try:
        assert bridge.running
        _, q = bridge.subscribe()
        payload = b'{"sim_time": 0.5}'
        tx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        tx.sendto(_dgram(1, payload), ("127.0.0.1", port))
        tx.close()
        assert await asyncio.wait_for(q.get(), timeout=2.0) == payload
    finally:
        await bridge.stop()
    assert not bridge.running


async def test_start_is_idempotent_and_stop_clears_subscribers():
    port = _free_udp_port()
    bridge = SvBridge(listen_port=port)
    await bridge.start()
    try:
        await bridge.start()  # second start must be a no-op, not EADDRINUSE
        bridge.subscribe("x")
    finally:
        await bridge.stop()
    assert bridge._subs.subscribers == {}


# ---------------------------------------------------------------------------
# BridgeRegistry semantics (global-first, per-job fallback)
# ---------------------------------------------------------------------------


class StubBridge(FramedUdpFanoutBridge):
    def __init__(self, listen_port: int) -> None:
        super().__init__(label="STUB", listen_port=listen_port)
        self.n_started = 0
        self.n_stopped = 0

    async def start(self) -> None:
        self._running = True
        self.n_started += 1

    async def stop(self) -> None:
        self._running = False
        self.n_stopped += 1


def _registry() -> BridgeRegistry[StubBridge]:
    return BridgeRegistry("STUB", lambda port: StubBridge(listen_port=port))


async def test_global_bridge_started_once_and_reused():
    reg = _registry()
    b1 = await reg.start_global(1111)
    b2 = await reg.start_global(1111)
    assert b1 is b2
    assert b1.n_started == 1
    assert reg.get_global() is b1


async def test_jobs_ride_the_global_bridge():
    reg = _registry()
    g = await reg.start_global(1111)
    j = await reg.start_for_job("job-1", 2222)
    assert j is g  # no second socket
    assert reg.get_for_job("job-1") is g
    assert reg.get_for_job("unknown-job") is g  # global serves everyone

    await reg.stop_for_job("job-1")
    assert g.n_stopped == 0  # global must survive job teardown


async def test_per_job_fallback_without_global():
    reg = _registry()
    b1 = await reg.start_for_job("job-1", 2222)
    assert reg.get_for_job("job-1") is b1
    assert reg.get_for_job("other") is None

    # starting the next job replaces (stops) the previous per-job bridge
    b2 = await reg.start_for_job("job-2", 2223)
    assert b1.n_stopped == 1
    assert reg.get_for_job("job-2") is b2

    await reg.stop_for_job("job-2")
    assert b2.n_stopped == 1


async def test_stop_all_stops_jobs_and_global():
    reg = _registry()
    g = await reg.start_global(1111)
    await reg.start_for_job("job-1", 2222)  # rides global
    count = await reg.stop_all()
    assert g.n_stopped == 1
    assert reg.get_global() is None
    assert count >= 1
