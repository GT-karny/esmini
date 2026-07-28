"""Tests for services/grpc_server.py (feature:F7 audit "web backend APIの18
ファイル中14ファイルがテスト0件" — the 4th of the top-priority gaps: gRPC
OSI GroundTruth/HostVehicleData streaming had zero coverage: "bridge未起動時の
UNAVAILABLE返却、cancel検知等が無検証").

Uses a `grpc.aio.ServicerContext` stub (set_code/set_details/cancelled) and a
fake OSIBridge, per the audit's own suggested fix -- no real UDP/OSI bridge or
fixed-port gRPC listener involved for the servicer-level tests. The one
process-level test (`start_grpc_server`) binds port 0 (OS-assigned ephemeral)
rather than the real default 50051, to stay clear of this project's known
fixed-port contention class of bug.
"""

from __future__ import annotations

import asyncio

import grpc
import pytest
from google.protobuf import empty_pb2

from osi3.osi_groundtruth_pb2 import GroundTruth
from osi3.osi_hostvehicledata_pb2 import HostVehicleData

from GT_esmini.web.backend.services import grpc_server


@pytest.fixture(autouse=True)
def _clean_bridge_registry():
    grpc_server._bridges.clear()
    yield
    grpc_server._bridges.clear()


class _FakeQueue:
    """Replaces the real asyncio.Queue subscribed to a bridge stream: pops
    preset items, then simulates the real code's `asyncio.wait_for(..., 1.0)`
    timing out by raising TimeoutError immediately (no real 1s wait)."""

    def __init__(self, items=()):
        self._items = list(items)

    async def get(self):
        if self._items:
            return self._items.pop(0)
        raise asyncio.TimeoutError()


class _FakeBridge:
    def __init__(self, running=True, gt_queue=None, hvd_queue=None):
        self.running = running
        self._gt_queue = gt_queue if gt_queue is not None else _FakeQueue()
        self._hvd_queue = hvd_queue if hvd_queue is not None else _FakeQueue()
        self.subscribed_gt: list[str] = []
        self.unsubscribed_gt: list[str] = []
        self.subscribed_hvd: list[str] = []
        self.unsubscribed_hvd: list[str] = []

    def subscribe_gt(self, subscriber_id=None):
        sid = subscriber_id or "sub-gt"
        self.subscribed_gt.append(sid)
        return sid, self._gt_queue

    def unsubscribe_gt(self, sid):
        self.unsubscribed_gt.append(sid)

    def subscribe_hvd(self, subscriber_id=None):
        sid = subscriber_id or "sub-hvd"
        self.subscribed_hvd.append(sid)
        return sid, self._hvd_queue

    def unsubscribe_hvd(self, sid):
        self.unsubscribed_hvd.append(sid)


class _FlippingRunningBridge(_FakeBridge):
    """.running reads True exactly `true_reads` times, then False forever --
    simulates a bridge that was active when _get_active_bridge() picked it,
    then stopped while the stream loop was waiting on an empty queue."""

    def __init__(self, *, true_reads=1, **kwargs):
        super().__init__(running=True, **kwargs)
        self._true_reads = true_reads
        self._reads = 0

    @property
    def running(self):
        self._reads += 1
        return self._reads <= self._true_reads

    @running.setter
    def running(self, value):
        # _FakeBridge.__init__ assigns self.running = running; absorb it.
        pass


class _FakeContext:
    def __init__(self, cancelled=None):
        self.code = None
        self.details = None
        self._cancelled = cancelled or (lambda: False)

    def set_code(self, code):
        self.code = code

    def set_details(self, details):
        self.details = details

    def cancelled(self):
        return self._cancelled()


# ---------------------------------------------------------------------------
# _get_active_bridge
# ---------------------------------------------------------------------------


def test_get_active_bridge_returns_none_when_registry_empty():
    assert grpc_server._get_active_bridge() is None


def test_get_active_bridge_returns_none_when_no_bridge_is_running():
    grpc_server._bridges["job-1"] = _FakeBridge(running=False)
    assert grpc_server._get_active_bridge() is None


def test_get_active_bridge_returns_the_running_bridge():
    bridge = _FakeBridge(running=True)
    grpc_server._bridges["job-1"] = bridge
    assert grpc_server._get_active_bridge() is bridge


def test_get_active_bridge_skips_stopped_bridges_to_find_a_running_one():
    stopped = _FakeBridge(running=False)
    running = _FakeBridge(running=True)
    grpc_server._bridges["job-stopped"] = stopped
    grpc_server._bridges["job-running"] = running
    assert grpc_server._get_active_bridge() is running


# ---------------------------------------------------------------------------
# GroundTruthServiceImpl.StreamGroundTruth
# ---------------------------------------------------------------------------


async def test_stream_groundtruth_sets_unavailable_when_no_active_bridge():
    svc = grpc_server.GroundTruthServiceImpl()
    ctx = _FakeContext()

    results = [msg async for msg in svc.StreamGroundTruth(empty_pb2.Empty(), ctx)]

    assert results == []
    assert ctx.code == grpc.StatusCode.UNAVAILABLE
    assert ctx.details == "No active simulation with OSI output"


async def test_stream_groundtruth_yields_parsed_message_then_stops_on_cancel():
    gt_msg = GroundTruth()
    gt_msg.timestamp.seconds = 42
    bridge = _FakeBridge(gt_queue=_FakeQueue([gt_msg.SerializeToString()]))
    grpc_server._bridges["job-1"] = bridge

    cancelled = {"flag": False}
    ctx = _FakeContext(cancelled=lambda: cancelled["flag"])
    svc = grpc_server.GroundTruthServiceImpl()
    agen = svc.StreamGroundTruth(empty_pb2.Empty(), ctx)

    first = await agen.__anext__()
    assert isinstance(first, GroundTruth)
    assert first.timestamp.seconds == 42

    # signal cancellation before the generator loops back to check it
    cancelled["flag"] = True
    with pytest.raises(StopAsyncIteration):
        await agen.__anext__()

    assert bridge.subscribed_gt == ["sub-gt"]
    assert bridge.unsubscribed_gt == ["sub-gt"], "must unsubscribe in the finally block"


async def test_stream_groundtruth_ends_when_bridge_stops_running_while_waiting():
    bridge = _FlippingRunningBridge(true_reads=1, gt_queue=_FakeQueue([]))
    grpc_server._bridges["job-1"] = bridge

    ctx = _FakeContext()  # never cancelled
    svc = grpc_server.GroundTruthServiceImpl()

    results = [msg async for msg in svc.StreamGroundTruth(empty_pb2.Empty(), ctx)]

    assert results == []
    assert bridge.unsubscribed_gt == ["sub-gt"]


# ---------------------------------------------------------------------------
# HostVehicleDataServiceImpl.StreamHostVehicleData
# ---------------------------------------------------------------------------


async def test_stream_hostvehicledata_sets_unavailable_when_no_active_bridge():
    svc = grpc_server.HostVehicleDataServiceImpl()
    ctx = _FakeContext()

    results = [msg async for msg in svc.StreamHostVehicleData(empty_pb2.Empty(), ctx)]

    assert results == []
    assert ctx.code == grpc.StatusCode.UNAVAILABLE
    assert ctx.details == "No active simulation with OSI output"


async def test_stream_hostvehicledata_yields_parsed_message_then_stops_on_cancel():
    hvd_msg = HostVehicleData()
    bridge = _FakeBridge(hvd_queue=_FakeQueue([hvd_msg.SerializeToString()]))
    grpc_server._bridges["job-1"] = bridge

    cancelled = {"flag": False}
    ctx = _FakeContext(cancelled=lambda: cancelled["flag"])
    svc = grpc_server.HostVehicleDataServiceImpl()
    agen = svc.StreamHostVehicleData(empty_pb2.Empty(), ctx)

    first = await agen.__anext__()
    assert isinstance(first, HostVehicleData)

    cancelled["flag"] = True
    with pytest.raises(StopAsyncIteration):
        await agen.__anext__()

    assert bridge.subscribed_hvd == ["sub-hvd"]
    assert bridge.unsubscribed_hvd == ["sub-hvd"]


# ---------------------------------------------------------------------------
# start_grpc_server (process-level smoke test; ephemeral port 0, not the real
# default 50051, to avoid any fixed-port contention)
# ---------------------------------------------------------------------------


async def test_start_grpc_server_starts_and_can_be_stopped():
    server = await grpc_server.start_grpc_server(port=0)
    try:
        assert isinstance(server, grpc.aio.Server)
    finally:
        await server.stop(grace=None)
