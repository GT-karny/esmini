"""The logical lane layer must survive the trip to the browser.

Three things stood between the engine emitting logical lanes (v0.18.0) and the
Electron GUI showing them, and each is pinned here:

1. the static ground truth is transmitted on the FIRST frame only, so a client
   that subscribes mid-run never sees it -> the bridge keeps that frame
2. nothing projected the layer into JSON -> the REST endpoint does
3. per-object assignments were dropped by _gt_to_json -> they are carried now

The negative controls matter as much as the positive ones: a frame with no
logical lanes must come back distinguishable from "no frame arrived", or the GUI
cannot tell a disabled layer from a run that has not started.
"""

from __future__ import annotations

import asyncio

import pytest

from osi3.osi_groundtruth_pb2 import GroundTruth

from GT_esmini.web.backend.api import osi_stream
from GT_esmini.web.backend.services import osi_bridge


def _gt_with_logical_lanes() -> GroundTruth:
    """A minimal but structurally honest static frame."""
    gt = GroundTruth()
    gt.timestamp.seconds = 0

    rl = gt.reference_line.add()
    rl.id.value = 900
    for i, (x, y, s) in enumerate([(0.0, 0.0, 0.0), (50.0, 0.0, 50.0)]):
        p = rl.poly_line.add()
        p.world_position.x, p.world_position.y, p.world_position.z = x, y, 0.0
        p.s_position = s
        p.t_axis_yaw = 1.5708  # +t points at +Y for a road running along +X

    for bid, t in ((910, 0.0), (911, 3.75)):
        lb = gt.logical_lane_boundary.add()
        lb.id.value = bid
        for x, s in ((0.0, 0.0), (50.0, 50.0)):
            p = lb.boundary_line.add()
            p.position.x, p.position.y, p.position.z = x, t, 0.0
            p.s_position = s
            p.t_position = t

    ll = gt.logical_lane.add()
    ll.id.value = 920
    ll.reference_line_id.value = 900
    ll.start_s, ll.end_s = 0.0, 50.0
    ll.right_boundary_id.add().value = 910
    ll.left_boundary_id.add().value = 911
    sr = ll.source_reference.add()
    sr.type = "net.asam.opendrive"
    sr.identifier.extend(["road_id:3", "road_s:0", "lane_id:-1"])
    succ = ll.successor_lane.add()
    succ.other_lane_id.value = 921
    succ.at_begin_of_other_lane = True

    mo = gt.moving_object.add()
    mo.id.value = 1
    mo.base.position.x, mo.base.position.y, mo.base.position.z = 10.0, 1.875, 0.75
    mo.base.dimension.length, mo.base.dimension.width = 4.0, 2.0
    mo.base.dimension.height = 1.5
    a = mo.moving_object_classification.logical_lane_assignment.add()
    a.assigned_lane_id.value = 920
    a.s_position, a.t_position, a.angle_to_lane = 10.0, 1.875, 0.0
    return gt


# --- 1. the bridge keeps the first frame -----------------------------------


def test_bridge_keeps_the_first_frame_for_late_subscribers():
    stream = osi_bridge._StreamState()
    proto = osi_bridge._OSIProtocol(stream, "GroundTruth")

    assert stream.first_frame is None, "nothing received yet"

    proto._dispatch(b"static-frame")
    proto._dispatch(b"later-frame")

    # The FIRST frame is the one kept -- that is where the static ground truth is.
    assert stream.first_frame == b"static-frame"


def test_bridge_caches_even_with_no_subscribers():
    """The gap this closes: the bridge starts before GT_Sim, the browser later."""
    stream = osi_bridge._StreamState()
    proto = osi_bridge._OSIProtocol(stream, "GroundTruth")
    assert not stream.subscribers

    proto._dispatch(b"static-frame")

    assert stream.first_frame == b"static-frame"


def test_static_frame_property_reads_the_gt_stream():
    bridge = osi_bridge.OSIBridge()
    assert bridge.static_frame is None
    osi_bridge._OSIProtocol(bridge._gt, "GroundTruth")._dispatch(b"gt")
    osi_bridge._OSIProtocol(bridge._hvd, "HostVehicleData")._dispatch(b"hvd")
    assert bridge.static_frame == b"gt", "must not pick up the HVD stream"


# --- 2. the REST projection -------------------------------------------------


def test_network_projection_carries_geometry_and_topology():
    net = osi_stream._logical_lane_network_to_json(
        _gt_with_logical_lanes().SerializeToString()
    )
    assert net is not None
    assert net["lane_count"] == 1
    assert net["reference_line_count"] == 1
    assert net["boundary_count"] == 2

    lane = net["lanes"][0]
    assert lane["id"] == 920
    assert lane["reference_line"] == 900
    assert lane["left_boundary"] == [911]
    assert lane["right_boundary"] == [910]
    # Topology, with the end it attaches to -- the bit osi_lane cannot express.
    assert lane["successor"] == [{"lane": 921, "at_begin": True}]
    # OpenDRIVE provenance, parsed from the prefixed source_reference strings.
    assert lane["odr"] == {"road_id": "3", "road_s": "0", "lane_id": "-1"}

    # Geometry is what makes the payload usable without the xodr: reference line
    # points carry (world, s), boundary points carry (world, s, t).
    rp = net["reference_lines"][0]["points"][0]
    assert {"x", "y", "z", "s", "t_axis_yaw"} <= set(rp)
    bp = net["boundaries"][0]["points"][0]
    assert {"x", "y", "z", "s", "t"} <= set(bp)


def test_network_projection_survives_a_frame_with_no_logical_lanes():
    """GT_OSI_LOGICAL_LANE=0 must project cleanly, not blow up."""
    gt = GroundTruth()
    gt.moving_object.add().id.value = 1
    net = osi_stream._logical_lane_network_to_json(gt.SerializeToString())
    assert net is not None
    assert net["lane_count"] == 0
    assert net["lanes"] == []


def test_network_projection_rejects_garbage():
    assert osi_stream._logical_lane_network_to_json(b"\xff\xff\xff\xff") is None


# --- 3. per-frame assignments on the WebSocket ------------------------------


def test_gt_to_json_carries_logical_lane_assignments():
    data = osi_stream._gt_to_json(_gt_with_logical_lanes().SerializeToString())
    assert data is not None
    obj = data["objects"][0]
    assert obj["logical_lanes"] == [{"lane": 920, "s": 10.0, "t": 1.875, "angle": 0.0}]


def test_gt_to_json_omits_the_key_when_there_is_no_assignment():
    """Absent, not empty -- so a consumer can tell "none" from "not emitted"."""
    gt = _gt_with_logical_lanes()
    del gt.moving_object[0].moving_object_classification.logical_lane_assignment[:]
    data = osi_stream._gt_to_json(gt.SerializeToString())
    assert "logical_lanes" not in data["objects"][0]


# --- the endpoint's three "not available" shapes ---------------------------


def _run(coro):
    return asyncio.get_event_loop_policy().new_event_loop().run_until_complete(coro)


def test_endpoint_reports_no_bridge(monkeypatch):
    monkeypatch.setattr(osi_stream, "get_bridge", lambda job_id: None)
    out = _run(osi_stream.get_logical_lane_network("job-1"))
    assert out["available"] is False


def test_endpoint_reports_no_frame_yet(monkeypatch):
    monkeypatch.setattr(osi_stream, "get_bridge", lambda job_id: osi_bridge.OSIBridge())
    out = _run(osi_stream.get_logical_lane_network("job-1"))
    assert out["available"] is False
    assert "yet" in out["error"]


def test_endpoint_serves_the_cached_static_frame(monkeypatch):
    bridge = osi_bridge.OSIBridge()
    osi_bridge._OSIProtocol(bridge._gt, "GroundTruth")._dispatch(
        _gt_with_logical_lanes().SerializeToString()
    )
    monkeypatch.setattr(osi_stream, "get_bridge", lambda job_id: bridge)

    out = _run(osi_stream.get_logical_lane_network("job-1"))

    assert out["available"] is True
    assert out["lane_count"] == 1
    assert out["job_id"] == "job-1"


def test_endpoint_distinguishes_empty_network_from_unavailable(monkeypatch):
    gt = GroundTruth()
    gt.moving_object.add().id.value = 1
    bridge = osi_bridge.OSIBridge()
    osi_bridge._OSIProtocol(bridge._gt, "GroundTruth")._dispatch(gt.SerializeToString())
    monkeypatch.setattr(osi_stream, "get_bridge", lambda job_id: bridge)

    out = _run(osi_stream.get_logical_lane_network("job-1"))

    assert out["available"] is True, "a frame DID arrive"
    assert out["lane_count"] == 0
    assert "note" in out, "and the payload says the layer was empty"


# --- the "always emit static" mode ------------------------------------------


def test_static_reporting_is_off_by_default_and_omits_the_flag(tmp_path):
    """Default stays 0: the REST cache solves late attachment without paying
    ~556 KB per frame (multi_intersections) for a self-contained stream."""
    from GT_esmini.web.backend.models.simulation import ExecutionConfig
    from GT_esmini.web.backend.services import simulation_runner as runner

    cmd = runner._build_cmd(tmp_path / "s.xosc", ExecutionConfig(), tmp_path)
    assert "--osi" in cmd
    assert "--osi_static_reporting" not in cmd


@pytest.mark.parametrize("mode", [1, 2])
def test_static_reporting_reaches_the_command_line(tmp_path, mode):
    from GT_esmini.web.backend.models.simulation import ExecutionConfig, OsiConfig
    from GT_esmini.web.backend.services import simulation_runner as runner

    cmd = runner._build_cmd(
        tmp_path / "s.xosc",
        ExecutionConfig(osi=OsiConfig(static_reporting=mode)),
        tmp_path,
    )
    i = cmd.index("--osi_static_reporting")
    assert cmd[i + 1] == str(mode)


def test_static_reporting_is_not_passed_when_osi_is_off(tmp_path):
    """The flag is meaningless without --osi, so it must not leak out alone."""
    from GT_esmini.web.backend.models.simulation import ExecutionConfig, OsiConfig
    from GT_esmini.web.backend.services import simulation_runner as runner

    cmd = runner._build_cmd(
        tmp_path / "s.xosc",
        ExecutionConfig(osi=OsiConfig(enabled=False, static_reporting=2)),
        tmp_path,
    )
    assert "--osi_static_reporting" not in cmd
