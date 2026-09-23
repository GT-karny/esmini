"""Both-polarity unit tests for matcher:route_matches_plan.

spine-work:osi-logical-lane S5. This module IS the red-proof asset for the
matcher: a checker that has only ever seen data it accepts is not a checker, so
every GREEN case below has a RED sibling that perturbs exactly one thing, and
every RED assertion names the quantity in the returned `detail` rather than
settling for `status == "fail"`.

The vacuous-pass guards get their own tests, because this matcher has three
distinct ways to be handed nothing at all -- a run captured without `osi: true`
(no scene.logical_lanes), a run whose HostVehicleData reporter is off (no
hvd.route), and a `must` entry that names no expect_* key -- and all three must
come back `skip`, never `pass`.

Run:
    DriverScript/.venv/Scripts/python.exe -m pytest \
        GT_esmini/scripts/verification/test_route_matches_plan.py -v
"""

from __future__ import annotations

import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(
    0, str(REPO_ROOT / "GT_esmini" / "web" / "backend" / "services")
)  # vd_metrics, imported flat -- the same sys.path convention gt_sim_test.py
# and test_manualdrive_matchers.py already use for this config-free module.

from vd_metrics import eval_must  # noqa: E402

# ---------------------------------------------------------------------------
# fixtures
#
# A three-section route down one road: lanes 100/101 -> 200 -> 300, the shape
# gt_sim_test.py's _gt_to_scene writes into scene["logical_lanes"] and
# _hvd_to_dict writes into frame["hvd"]["route"].
# ---------------------------------------------------------------------------

TOPO = {
    "100": {
        "road_id": 1,
        "lane_id": -1,
        "pred": [],
        "succ": [200],
        "left": [],
        "right": [],
    },
    "101": {
        "road_id": 1,
        "lane_id": -2,
        "pred": [],
        "succ": [200],
        "left": [100],
        "right": [],
    },
    "200": {
        "road_id": 2,
        "lane_id": -1,
        "pred": [100, 101],
        "succ": [300],
        "left": [],
        "right": [],
    },
    "300": {
        "road_id": 3,
        "lane_id": -1,
        "pred": [200],
        "succ": [],
        "left": [],
        "right": [],
    },
}


def _seg(*ids: int) -> list[dict]:
    return [{"logical_lane_id": i, "start_s": 0.0, "end_s": 10.0} for i in ids]


def _frame(
    t: float,
    segments: list | None = None,
    *,
    topo: dict | None = TOPO,
    route_lane: dict | None = None,
    hvd: bool = True,
) -> dict:
    fr: dict = {"sim_time": t}
    if topo is not None:
        fr["scene"] = {"objects": [], "logical_lanes": topo}
    if hvd:
        route = None if segments is None else {"route_id": 7, "segments": segments}
        fr["hvd"] = {"inputs": {}, "adas": {}}
        if route is not None:
            fr["hvd"]["route"] = route
    if route_lane is not None:
        fr["route_lane"] = route_lane
    return fr


def _straight(**kw) -> list[dict]:
    """Two frames carrying the full 100/101 -> 200 -> 300 route."""
    return [
        _frame(0.1, [_seg(100, 101), _seg(200), _seg(300)], **kw),
        _frame(0.2, [_seg(100, 101), _seg(200), _seg(300)], **kw),
    ]


# ---------------------------------------------------------------------------
# presence
# ---------------------------------------------------------------------------


def test_route_present_green():
    r = eval_must(
        {"event": "route_matches_plan", "expect_route_present": True}, _straight()
    )
    assert r["status"] == "pass", r
    assert "route present on 2/2" in r["detail"]


def test_route_present_red_when_the_route_never_appears():
    frames = [_frame(0.1), _frame(0.2)]  # hvd, but no route key
    r = eval_must({"event": "route_matches_plan", "expect_route_present": True}, frames)
    assert r["status"] == "fail", r
    assert "carries segments on 0/2" in r["detail"]


def test_route_absent_is_assertable_in_the_other_direction():
    """expect_route_present: False is the negative control the flag-off run needs."""
    frames = [_frame(0.1), _frame(0.2)]
    r = eval_must(
        {"event": "route_matches_plan", "expect_route_present": False}, frames
    )
    assert r["status"] == "pass", r
    r_red = eval_must(
        {"event": "route_matches_plan", "expect_route_present": False}, _straight()
    )
    assert r_red["status"] == "fail", r_red
    assert "want none" in r_red["detail"]


# ---------------------------------------------------------------------------
# closure
# ---------------------------------------------------------------------------


def test_closure_green():
    r = eval_must({"event": "route_matches_plan", "expect_closure": True}, _straight())
    assert r["status"] == "pass", r
    assert "8 lane_segment id(s) all resolve" in r["detail"]


def test_closure_red_on_a_dangling_id():
    frames = [_frame(0.1, [_seg(100), _seg(999)])]
    r = eval_must({"event": "route_matches_plan", "expect_closure": True}, frames)
    assert r["status"] == "fail", r
    assert "999" in r["detail"] and "do not exist" in r["detail"]


# ---------------------------------------------------------------------------
# connectivity -- the check that actually reads S2
# ---------------------------------------------------------------------------


def test_segments_connected_green():
    r = eval_must(
        {"event": "route_matches_plan", "expect_segments_connected": True}, _straight()
    )
    assert r["status"] == "pass", r
    assert "4 segment seam(s) joined" in r["detail"]


def test_segments_connected_red_when_the_graph_has_no_edge():
    """The route is unchanged; only the topology loses its successors. This is
    exactly the failure a '>0 segments' threshold would wave through."""
    topo = {k: dict(v, succ=[], pred=[]) for k, v in TOPO.items()}
    r = eval_must(
        {"event": "route_matches_plan", "expect_segments_connected": True},
        _straight(topo=topo),
    )
    assert r["status"] == "fail", r
    assert "not joined by any predecessor/successor" in r["detail"]


def test_segments_connected_red_when_the_route_skips_a_section():
    """Well-formed route, intact topology, but section 1 jumps straight to
    section 3 -- no edge exists between them."""
    frames = [_frame(0.1, [_seg(100), _seg(300)])]
    r = eval_must(
        {"event": "route_matches_plan", "expect_segments_connected": True}, frames
    )
    assert r["status"] == "fail", r
    assert "[100] -> [300]" in r["detail"]


def test_segments_connected_skips_a_single_segment_route():
    """One segment means no seam was ever examined -- that must not be a pass."""
    frames = [_frame(0.1, [_seg(100)])]
    r = eval_must(
        {"event": "route_matches_plan", "expect_segments_connected": True}, frames
    )
    assert r["status"] == "skip", r
    assert "single segment" in r["detail"]


# ---------------------------------------------------------------------------
# agreement with the VD's own plan
# ---------------------------------------------------------------------------


def test_lanes_match_plan_green():
    rl = {"valid": True, "road_id": 1, "target_lanes": [-1, -2]}
    r = eval_must(
        {"event": "route_matches_plan", "expect_lanes_match_plan": True},
        _straight(route_lane=rl),
    )
    assert r["status"] == "pass", r
    assert "matched target_lanes on 2 frame(s)" in r["detail"]


def test_lanes_match_plan_red_when_the_two_faces_disagree():
    rl = {"valid": True, "road_id": 1, "target_lanes": [-1]}
    r = eval_must(
        {"event": "route_matches_plan", "expect_lanes_match_plan": True},
        _straight(route_lane=rl),
    )
    assert r["status"] == "fail", r
    assert "[-2, -1]" in r["detail"] and "[-1]" in r["detail"]


def test_lanes_match_plan_skips_when_nothing_could_be_compared():
    """The ego's road is not in the route at all -- no comparison happened, so
    the only honest answer is skip."""
    rl = {"valid": True, "road_id": 9, "target_lanes": [-1]}
    r = eval_must(
        {"event": "route_matches_plan", "expect_lanes_match_plan": True},
        _straight(route_lane=rl),
    )
    assert r["status"] == "skip", r
    assert "nothing was compared" in r["detail"]


# ---------------------------------------------------------------------------
# vacuous-pass guards
# ---------------------------------------------------------------------------


def test_skip_when_must_names_no_check():
    r = eval_must({"event": "route_matches_plan"}, _straight())
    assert r["status"] == "skip", r
    assert "checks nothing must not report pass" in r["detail"]


def test_skip_without_scene_logical_lanes():
    """A batch entry without `osi: true` has no topology, so every id would be
    'not found' -- which must read as unmeasured, not as a violation."""
    frames = [_frame(0.1, [_seg(100), _seg(200)], topo=None)]
    r = eval_must({"event": "route_matches_plan", "expect_closure": True}, frames)
    assert r["status"] == "skip", r
    assert "scene.logical_lanes" in r["detail"]


def test_skip_without_hvd():
    frames = [_frame(0.1, hvd=False)]
    r = eval_must({"event": "route_matches_plan", "expect_closure": True}, frames)
    assert r["status"] == "skip", r
    assert "hvd.route" in r["detail"]


def test_skip_on_empty_window():
    r = eval_must(
        {"event": "route_matches_plan", "expect_closure": True, "window": [50.0, 60.0]},
        _straight(),
    )
    assert r["status"] == "skip", r
    assert "no frames in time window" in r["detail"]


def test_skip_when_hvd_is_present_but_carries_no_segment():
    frames = [_frame(0.1, []), _frame(0.2, [])]
    r = eval_must({"event": "route_matches_plan", "expect_closure": True}, frames)
    assert r["status"] == "skip", r
    assert "no segment on any gated frame" in r["detail"]
