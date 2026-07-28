"""Tests for services/osi_bridge.py's `_OSIProtocol` -- the multi-packet OSI
GroundTruth/HostVehicleData UDP reassembly state machine (feature:F7 audit
"web backend APIの18ファイル中14ファイルがテスト0件": osi_bridge.py was named as
the single most complex untested piece, and specifically flagged as a false-
coverage trap -- test_framed_udp_bridge.py exists and covers VdBridge/SvBridge,
which makes it easy to assume the sibling OSI bridge is covered too; it is not,
`_OSIProtocol` is a separate implementation).

Wire contract (osi_bridge.py's own header comment, `_HEADER_SIZE`/`_MAX_PACKET_SIZE`):
    [counter: int32 LE][size: uint32 LE][data: bytes]
    counter == 0        -> single complete message (e.g. HostVehicleData)
    counter == 1..N      -> GroundTruth fragment, ascending index
    counter negative     -> this fragment is the LAST one (abs(counter) is its index)

Also covers the OSIBridge/start_bridge/stop_bridge registry layer with
`loop.create_datagram_endpoint` monkeypatched -- no real UDP sockets are bound
(OSI_GT_PORT/OSI_HVD_PORT are fixed ports; binding them for real in a test
would risk exactly the port-contention class of bug this project has been
bitten by before).
"""

from __future__ import annotations

import asyncio
import struct

import pytest

from GT_esmini.web.backend.services import osi_bridge


def _packet(counter: int, payload: bytes, *, size_override: int | None = None) -> bytes:
    size = len(payload) if size_override is None else size_override
    return struct.pack("iI", counter, size) + payload


@pytest.fixture(autouse=True)
def _clean_bridge_registry():
    osi_bridge._bridges.clear()
    yield
    osi_bridge._bridges.clear()


def _new_protocol(label="Test"):
    stream = osi_bridge._StreamState()
    return stream, osi_bridge._OSIProtocol(stream, label)


async def _subscribe(stream: osi_bridge._StreamState, sid="sub", maxsize=16):
    queue: asyncio.Queue[bytes] = asyncio.Queue(maxsize=maxsize)
    stream.subscribers[sid] = queue
    return queue


# ---------------------------------------------------------------------------
# single-packet (counter == 0) messages, e.g. HostVehicleData
# ---------------------------------------------------------------------------


async def test_counter_zero_dispatches_immediately_as_one_message():
    stream, proto = _new_protocol()
    queue = await _subscribe(stream)

    proto.datagram_received(_packet(0, b"hvd-payload"), ("127.0.0.1", 1))

    assert queue.get_nowait() == b"hvd-payload"
    assert queue.empty()
    # state stays reset, ready for the next message
    assert proto._buffer == b""
    assert proto._next_index is None


# ---------------------------------------------------------------------------
# multi-packet (GroundTruth) reassembly
# ---------------------------------------------------------------------------


async def test_two_fragment_message_reassembles_in_order():
    stream, proto = _new_protocol()
    queue = await _subscribe(stream)

    proto.datagram_received(_packet(1, b"AAA"), ("127.0.0.1", 1))
    assert queue.empty()  # not dispatched yet -- still waiting on the last fragment
    proto.datagram_received(_packet(-2, b"BBB"), ("127.0.0.1", 1))

    assert queue.get_nowait() == b"AAABBB"
    assert proto._buffer == b""
    assert proto._next_index is None


async def test_three_fragment_message_reassembles_in_order():
    stream, proto = _new_protocol()
    queue = await _subscribe(stream)

    proto.datagram_received(_packet(1, b"one-"), ("127.0.0.1", 1))
    proto.datagram_received(_packet(2, b"two-"), ("127.0.0.1", 1))
    proto.datagram_received(_packet(-3, b"three"), ("127.0.0.1", 1))

    assert queue.get_nowait() == b"one-two-three"


async def test_single_fragment_multipacket_message_dispatches_on_first_negative_counter():
    """counter=-1 on the very first fragment: a GroundTruth message that
    happens to fit in one UDP packet still uses the multi-packet framing, so
    it must dispatch immediately rather than wait for a fragment 2 that will
    never come."""
    stream, proto = _new_protocol()
    queue = await _subscribe(stream)

    proto.datagram_received(_packet(-1, b"solo"), ("127.0.0.1", 1))

    assert queue.get_nowait() == b"solo"
    assert proto._next_index is None


async def test_reassembly_survives_back_to_back_messages():
    """After one message completes, the very next fragment=1 must start a
    fresh reassembly, not be treated as a stray continuation."""
    stream, proto = _new_protocol()
    queue = await _subscribe(stream)

    proto.datagram_received(_packet(1, b"first-"), ("127.0.0.1", 1))
    proto.datagram_received(_packet(-2, b"msg"), ("127.0.0.1", 1))
    assert queue.get_nowait() == b"first-msg"

    proto.datagram_received(_packet(1, b"second-"), ("127.0.0.1", 1))
    proto.datagram_received(_packet(-2, b"msg"), ("127.0.0.1", 1))
    assert queue.get_nowait() == b"second-msg"


# ---------------------------------------------------------------------------
# malformed / out-of-order input
# ---------------------------------------------------------------------------


async def test_runt_packet_below_header_size_is_ignored_without_disturbing_reassembly():
    stream, proto = _new_protocol()
    queue = await _subscribe(stream)

    proto.datagram_received(_packet(1, b"keep-"), ("127.0.0.1", 1))
    proto.datagram_received(b"\x00\x01", ("127.0.0.1", 1))  # 2 bytes, < _HEADER_SIZE
    proto.datagram_received(_packet(-2, b"going"), ("127.0.0.1", 1))

    assert queue.get_nowait() == b"keep-going"


async def test_size_field_mismatch_resets_without_dispatch():
    stream, proto = _new_protocol()
    queue = await _subscribe(stream)

    # declared size (99) does not match the actual payload length
    bad = _packet(1, b"AAA", size_override=99)
    proto.datagram_received(bad, ("127.0.0.1", 1))

    assert queue.empty()
    assert proto._buffer == b""
    assert proto._next_index is None

    # must recover cleanly on the next well-formed message
    proto.datagram_received(_packet(0, b"next-ok"), ("127.0.0.1", 1))
    assert queue.get_nowait() == b"next-ok"


async def test_out_of_order_fragment_resets_and_drops_the_partial_message():
    stream, proto = _new_protocol()
    queue = await _subscribe(stream)

    proto.datagram_received(_packet(1, b"AAA"), ("127.0.0.1", 1))
    proto.datagram_received(_packet(-3, b"CCC"), ("127.0.0.1", 1))  # skips index 2

    assert (
        queue.empty()
    ), "a gap in the fragment sequence must not dispatch a corrupt message"
    assert proto._next_index is None

    # recovers on the next fresh sequence
    proto.datagram_received(_packet(1, b"fresh-"), ("127.0.0.1", 1))
    proto.datagram_received(_packet(-2, b"data"), ("127.0.0.1", 1))
    assert queue.get_nowait() == b"fresh-data"


async def test_first_fragment_must_start_at_index_1():
    """If fragment 1 was dropped upstream and we first see index 2, we cannot
    reassemble the message; it must be dropped, not mis-assembled from a
    truncated buffer."""
    stream, proto = _new_protocol()
    queue = await _subscribe(stream)

    proto.datagram_received(_packet(2, b"orphan"), ("127.0.0.1", 1))
    assert queue.empty()
    assert proto._next_index is None

    # a correct sequence afterward still works
    proto.datagram_received(_packet(1, b"ok-"), ("127.0.0.1", 1))
    proto.datagram_received(_packet(-2, b"now"), ("127.0.0.1", 1))
    assert queue.get_nowait() == b"ok-now"


# ---------------------------------------------------------------------------
# fan-out / backpressure
# ---------------------------------------------------------------------------


async def test_dispatch_fans_out_to_every_subscriber():
    stream, proto = _new_protocol()
    q1 = await _subscribe(stream, "s1")
    q2 = await _subscribe(stream, "s2")

    proto.datagram_received(_packet(0, b"broadcast"), ("127.0.0.1", 1))

    assert q1.get_nowait() == b"broadcast"
    assert q2.get_nowait() == b"broadcast"


async def test_dispatch_drops_oldest_when_a_subscriber_queue_is_full():
    stream, proto = _new_protocol()
    queue = await _subscribe(stream, maxsize=1)
    queue.put_nowait(b"stale")

    proto.datagram_received(_packet(0, b"fresh"), ("127.0.0.1", 1))

    assert queue.qsize() == 1
    assert queue.get_nowait() == b"fresh"  # oldest evicted, newest kept


# ---------------------------------------------------------------------------
# OSIBridge / start_bridge / stop_bridge registry layer
# (no real UDP sockets: create_datagram_endpoint is monkeypatched)
# ---------------------------------------------------------------------------


class _FakeTransport:
    def __init__(self):
        self.closed = False

    def close(self):
        self.closed = True


def _patch_datagram_endpoint(monkeypatch):
    """Patch create_datagram_endpoint on the CURRENT running loop instance
    (not the AbstractEventLoop class -- BaseEventLoop defines its own
    concrete override, so a class-level patch on AbstractEventLoop is shadowed
    by MRO and never actually intercepts the real call)."""
    created = []

    async def _fake_create_datagram_endpoint(protocol_factory, local_addr=None):
        protocol = protocol_factory()
        transport = _FakeTransport()
        protocol.connection_made(transport)
        created.append((local_addr, transport))
        return transport, protocol

    loop = asyncio.get_running_loop()
    monkeypatch.setattr(
        loop, "create_datagram_endpoint", _fake_create_datagram_endpoint
    )
    return created


async def test_start_bridge_registers_and_marks_running(monkeypatch):
    _patch_datagram_endpoint(monkeypatch)

    bridge = await osi_bridge.start_bridge("job-1", gt_port=48198, hvd_port=48199)

    assert bridge.running is True
    assert osi_bridge.get_bridge("job-1") is bridge


async def test_start_bridge_stops_any_stale_bridge_first(monkeypatch):
    _patch_datagram_endpoint(monkeypatch)

    first = await osi_bridge.start_bridge("job-1", gt_port=48198, hvd_port=48199)
    second = await osi_bridge.start_bridge("job-2", gt_port=48198, hvd_port=48199)

    assert first.running is False, "the stale bridge for job-1 must be stopped"
    assert second.running is True
    assert osi_bridge.get_bridge("job-1") is None
    assert osi_bridge.get_bridge("job-2") is second


async def test_stop_bridge_removes_from_registry(monkeypatch):
    _patch_datagram_endpoint(monkeypatch)

    bridge = await osi_bridge.start_bridge("job-3", gt_port=48198, hvd_port=48199)
    await osi_bridge.stop_bridge("job-3")

    assert bridge.running is False
    assert osi_bridge.get_bridge("job-3") is None


async def test_stop_bridge_on_unknown_job_is_a_noop():
    await osi_bridge.stop_bridge("never-existed")  # must not raise
