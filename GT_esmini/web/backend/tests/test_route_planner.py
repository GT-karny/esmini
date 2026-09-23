"""Route planner tests (Track A1).

These require the built GT_esminiLib.dll / esminiRMLib.dll, so they skip on a
checkout without a Release build rather than failing -- the same posture the
integration ctests take. The routing assertions themselves are exact: the point of
this file is that a "route" that silently goes the wrong way down a lane, or that
reports no-route for a perfectly legal one, is caught.

Road facts these tests rely on (read from resources/xodr/fabriksgatan.xodr, and
independently corroborated by the connectingRoad13 finding recorded for issue #31):

    junction 4 is the SUCCESSOR end of roads 2 and 3,
              and the PREDECESSOR end of roads 0 and 1.

So the lane that APPROACHES the junction is -1 on roads 2/3 (drives +s toward
s_max) and +1 on roads 0/1 (drives -s toward s=0). That asymmetry is exactly what
makes these tests worth having: the pre-GT_RM_CalcRouteH API searched only the
successor end, so it answered roads 2/3 correctly and roads 0/1 wrongly.
"""

from __future__ import annotations

import math
from pathlib import Path

import pytest

from GT_esmini.web.backend.config import ESMINI_RM_LIB, GT_ESMINI_LIB, RESOURCES_DIR
from GT_esmini.web.backend.services.route_planner_service import (
    RoutePlanError,
    plan_route,
)

FABRIKSGATAN = RESOURCES_DIR / "xodr" / "fabriksgatan.xodr"
HIGHWAY = RESOURCES_DIR / "xodr" / "highway_example_with_merge_and_split.xodr"
MULTI_INTERSECTIONS = RESOURCES_DIR / "xodr" / "multi_intersections.xodr"

_libs_present = Path(ESMINI_RM_LIB).is_file() and Path(GT_ESMINI_LIB).is_file()
requires_libs = pytest.mark.skipif(
    not _libs_present,
    reason="needs a Release build (esminiRMLib.dll + GT_esminiLib.dll)",
)


def _world_point(xodr: Path, road_id: int, lane_id: int, s: float) -> dict:
    """World (x, y) of a lane centre -- stands in for a user's map click."""
    import sys

    from GT_esmini.web.backend.config import GT_SCRIPTS_DIR

    if str(GT_SCRIPTS_DIR) not in sys.path:
        sys.path.insert(0, str(GT_SCRIPTS_DIR))
    from rm_lib import EsminiRMLib  # type: ignore[attr-defined]

    from GT_esmini.web.backend.services.road_geometry_service import (
        ESMINI_RM_LOCK,
        init_odr_cached,
    )

    with ESMINI_RM_LOCK:
        rm = EsminiRMLib(str(ESMINI_RM_LIB))
        # Through the cache, not rm.Init: a raw Init loads a map the cache does
        # not know about, so the next plan_route skips its own Init and answers
        # from THIS map. That is the silent wrong-map failure, and this helper
        # reproduced it across tests before it went through here.
        assert init_odr_cached(rm, xodr, "rm") >= 0
        handle = rm.CreatePosition()
        try:
            rm.SetLanePosition(handle, road_id, lane_id, 0.0, s, True)
            _, data = rm.GetPositionData(handle)
            return {"x": float(data.x), "y": float(data.y)}
        finally:
            rm.DeletePosition(handle)


def _chain(plan: dict) -> list[tuple[int, int]]:
    return [(w["road_id"], w["lane_id"]) for w in plan["waypoints"]]


# ---------------------------------------------------------------------------
# Positive: routes that exist must be found, in BOTH junction orientations
# ---------------------------------------------------------------------------


@requires_libs
def test_route_leaving_via_successor_end():
    """road 3 -> road 2. Approach lane is -1 (drives +s into junction 4)."""
    plan = plan_route(
        FABRIKSGATAN,
        [
            _world_point(FABRIKSGATAN, 3, -1, 10.0),
            _world_point(FABRIKSGATAN, 2, 1, 50.0),
        ],
    )
    # connectingRoad 13 is the road-3 -> road-2 connector (issue #31 record).
    assert _chain(plan) == [(3, -1), (13, -1), (2, 1)]
    assert plan["diagnostic"] == "ok"
    assert plan["length"] > 0


@requires_libs
def test_route_leaving_via_predecessor_end():
    """road 0 -> road 2. Approach lane is +1, which drives -s toward s=0.

    This is the case the +s-only GT_RM_CalcRoute could not solve: it reported
    "no route" (-2) even though this route is perfectly drivable.
    """
    plan = plan_route(
        FABRIKSGATAN,
        [
            _world_point(FABRIKSGATAN, 0, 1, 50.0),
            _world_point(FABRIKSGATAN, 2, 1, 50.0),
        ],
    )
    assert _chain(plan) == [(0, 1), (9, -1), (2, 1)]
    assert plan["diagnostic"] == "ok"


@requires_libs
def test_route_reports_required_lane_change():
    """The exit-ramp case: reaching road 2 needs a -3 -> -4 change on road 0.

    Same known answer the C-API smoke in commit 05ec5b48 recorded, now reached
    through the service.
    """
    plan = plan_route(
        HIGHWAY,
        [
            _world_point(HIGHWAY, 0, -3, 10.0),
            _world_point(HIGHWAY, 2, -1, 40.0),
        ],
    )
    assert [w["road_id"] for w in plan["waypoints"]] == [0, 4, 2]
    assert plan["lane_changes"] == [
        {"road_id": 0, "s": 0.0, "from_lane_id": -3, "to_lane_id": -4}
    ]


@requires_libs
def test_via_points_concatenate_without_seam_duplicates():
    """Multi-leg routes join correctly: no repeated waypoint, distances add up.

    fabriksgatan is a single four-arm junction, so a via point on a *third* arm
    would need a U-turn and is genuinely undrivable -- there is no via here that
    reroutes. What a via CAN do is split the same path into legs, which is what
    exercises the concatenation and seam dedup. Both vias below sit on the direct
    path, so the leg distances must sum back to the direct distance.
    """
    start = _world_point(FABRIKSGATAN, 3, -1, 10.0)
    goal = _world_point(FABRIKSGATAN, 2, 1, 50.0)
    direct = plan_route(FABRIKSGATAN, [start, goal])

    for label, via in (
        ("via further along the start road", _world_point(FABRIKSGATAN, 3, -1, 100.0)),
        ("via earlier on the target road", _world_point(FABRIKSGATAN, 2, 1, 250.0)),
    ):
        plan = plan_route(FABRIKSGATAN, [start, via, goal])
        assert _chain(plan) == _chain(direct), label
        # Leg distances must decompose exactly -- a seam counted twice (or dropped)
        # would show up here as a length that no longer matches the direct route.
        assert plan["length"] == pytest.approx(direct["length"], abs=1e-6), label
        roads = [w["road_id"] for w in plan["waypoints"]]
        assert all(a != b for a, b in zip(roads, roads[1:])), f"{label}: {roads}"


@requires_libs
def test_route_length_is_travel_distance():
    """length tracks start and target s, so it is a distance and not a road count.

    Pins the unit: moving the start 9 m along its lane must shorten the route by
    exactly 9 m. Verified against the hand sum on fabriksgatan:
    road3 (114.3-10) + connector 13 (14.9) + road2 (304.2-50) = 373.4 m.
    """
    near = plan_route(
        FABRIKSGATAN,
        [
            _world_point(FABRIKSGATAN, 3, -1, 1.0),
            _world_point(FABRIKSGATAN, 2, 1, 50.0),
        ],
    )
    far = plan_route(
        FABRIKSGATAN,
        [
            _world_point(FABRIKSGATAN, 3, -1, 10.0),
            _world_point(FABRIKSGATAN, 2, 1, 50.0),
        ],
    )
    assert near["length"] - far["length"] == pytest.approx(9.0, abs=0.05)
    assert far["length"] == pytest.approx(373.4, abs=0.5)


# ---------------------------------------------------------------------------
# Negative: the other polarity. Each of these must FAIL, and fail distinguishably.
# ---------------------------------------------------------------------------


@requires_libs
def test_wrong_way_route_is_refused():
    """road 3 lane +1 drives AWAY from junction 4, so it reaches nothing.

    The +s-only API answered this with a 3-waypoint route that drove up the lane
    against traffic. Refusing it is the fix; this test pins that down so a future
    change to the search direction cannot quietly reintroduce wrong-way routes.
    """
    with pytest.raises(RoutePlanError) as excinfo:
        plan_route(
            FABRIKSGATAN,
            [
                _world_point(FABRIKSGATAN, 3, 1, 100.0),
                _world_point(FABRIKSGATAN, 0, 1, 50.0),
            ],
        )
    assert excinfo.value.code == "no_route"


@requires_libs
def test_point_off_road_is_rejected():
    """A click on empty space must not snap to the nearest road 100 km away."""
    with pytest.raises(RoutePlanError) as excinfo:
        plan_route(
            FABRIKSGATAN,
            [
                {"x": 99999.0, "y": 99999.0},
                _world_point(FABRIKSGATAN, 2, 1, 50.0),
            ],
        )
    assert excinfo.value.code == "point_not_routable"
    assert excinfo.value.detail["index"] == 0


@requires_libs
def test_off_road_detection_flips_at_the_road_edge():
    """Both polarities of the on/off-lane discriminator, on one road.

    Guards against the two things that look like they would work here but are
    constants: the library return codes (always 0) and click-vs-snapped distance
    (always 0, because GetPositionData echoes the input coordinates).
    """
    import sys

    from GT_esmini.web.backend.config import GT_SCRIPTS_DIR

    if str(GT_SCRIPTS_DIR) not in sys.path:
        sys.path.insert(0, str(GT_SCRIPTS_DIR))
    from rm_lib import EsminiRMLib  # type: ignore[attr-defined]

    from GT_esmini.web.backend.services.road_geometry_service import (
        ESMINI_RM_LOCK,
        init_odr_cached,
    )

    with ESMINI_RM_LOCK:
        rm = EsminiRMLib(str(ESMINI_RM_LIB))
        assert init_odr_cached(rm, FABRIKSGATAN, "rm") >= 0
        handle = rm.CreatePosition()
        try:
            rm.SetLanePosition(handle, 0, 1, 0.0, 50.0, True)
            _, centre = rm.GetPositionData(handle)
            nx, ny = -math.sin(centre.h), math.cos(centre.h)

            rm.SetWorldXYHPosition(handle, centre.x, centre.y, 0.0)
            on_lane = rm.GetInLaneType(handle)

            far_x, far_y = centre.x + nx * 30.0, centre.y + ny * 30.0
            rm.SetWorldXYHPosition(handle, far_x, far_y, 0.0)
            off_lane = rm.GetInLaneType(handle)
        finally:
            rm.DeletePosition(handle)

    assert on_lane == 2, "lane centre should report DRIVING"
    assert off_lane == 1, "30 m off the road should report NONE"


@requires_libs
def test_too_few_points_is_rejected():
    with pytest.raises(RoutePlanError) as excinfo:
        plan_route(FABRIKSGATAN, [{"x": 0.0, "y": 0.0}])
    assert excinfo.value.code == "too_few_points"


@requires_libs
def test_unknown_strategy_is_rejected():
    with pytest.raises(RoutePlanError) as excinfo:
        plan_route(
            FABRIKSGATAN,
            [
                _world_point(FABRIKSGATAN, 3, -1, 10.0),
                _world_point(FABRIKSGATAN, 2, 1, 50.0),
            ],
            strategy="scenic",
        )
    assert excinfo.value.code == "bad_strategy"


# ---------------------------------------------------------------------------
# Snapping (the map's per-point feedback, independent of routing)
# ---------------------------------------------------------------------------


@requires_libs
def test_snap_moves_an_off_centre_point_to_the_lane_centre():
    """The regression this exists for: snap must MOVE the point.

    The first implementation returned GetPositionData's x/y as the snapped
    position. Those are the coordinates handed to SetWorldXYHPosition, echoed
    back verbatim, so every click came back unchanged and nothing ever visibly
    snapped -- while road/lane/s looked perfectly correct.

    It survived review because the probe used a point already ON the lane centre,
    where an echo and a correct snap are identical. So this test deliberately
    starts OFF centre, along the road normal, and asserts the distance moved.
    """
    import math
    import sys

    from GT_esmini.web.backend.config import ESMINI_RM_LIB, GT_SCRIPTS_DIR
    from GT_esmini.web.backend.services.road_geometry_service import (
        ESMINI_RM_LOCK,
        init_odr_cached,
    )
    from GT_esmini.web.backend.services.route_planner_service import snap_points

    if str(GT_SCRIPTS_DIR) not in sys.path:
        sys.path.insert(0, str(GT_SCRIPTS_DIR))
    from rm_lib import EsminiRMLib  # type: ignore[attr-defined]

    with ESMINI_RM_LOCK:
        rm = EsminiRMLib(str(ESMINI_RM_LIB))
        assert init_odr_cached(rm, FABRIKSGATAN, "rm") >= 0
        handle = rm.CreatePosition()
        try:
            rm.SetLanePosition(handle, 0, 1, 0.0, 50.0, True)
            _, centre = rm.GetPositionData(handle)
            cx, cy, ch = float(centre.x), float(centre.y), float(centre.h)
        finally:
            rm.DeletePosition(handle)

    nx, ny = -math.sin(ch), math.cos(ch)
    offset = 1.0
    probe = {"x": cx + nx * offset, "y": cy + ny * offset}
    result = snap_points(FABRIKSGATAN, [probe])[0]

    assert result["on_road"] is True
    assert result["lane_id"] == 1, "should stay in the lane it was nudged inside"
    moved = math.hypot(result["x"] - probe["x"], result["y"] - probe["y"])
    assert moved == pytest.approx(offset, abs=0.1), (
        f"snap moved the point {moved:.3f} m; expected ~{offset} m back to the "
        "lane centre. A value of 0 means the input coordinates were echoed."
    )
    # And it landed on the centre, not merely somewhere else.
    assert math.hypot(result["x"] - cx, result["y"] - cy) < 0.1


@requires_libs
def test_snap_reports_a_missed_point_instead_of_raising():
    """One bad point must not fail the whole request -- the map marks that point
    and keeps the others usable. plan_route is the strict one, not this."""
    from GT_esmini.web.backend.services.route_planner_service import snap_points

    good = _world_point(FABRIKSGATAN, 0, 1, 50.0)
    results = snap_points(FABRIKSGATAN, [good, {"x": 99999.0, "y": 99999.0}])
    assert results[0]["on_road"] is True
    assert results[1]["on_road"] is False
    assert results[1]["reason"] in {"off_road", "not_routable"}


# ---------------------------------------------------------------------------
# Drawn path: the line must follow the lane the route is IN, not only the lane
# it leaves each road on
# ---------------------------------------------------------------------------


def _centre_line_hits(xodr: Path, path: list[dict]) -> list[tuple[int, int, float]]:
    """Path points sitting on a two-way road's reference line == its centre line.

    A zero-width lane's centre IS the reference line, so a path drawn on a turn
    pocket upstream of its taper lands exactly on the centre line. This is the
    measurement that names that failure; the two-way test is road-level on
    purpose, so the very stretch where the pocket has collapsed to nothing --
    where a per-s test would see no opposing lane and stay quiet -- still counts.
    """
    import sys

    from GT_esmini.web.backend.config import GT_SCRIPTS_DIR

    if str(GT_SCRIPTS_DIR) not in sys.path:
        sys.path.insert(0, str(GT_SCRIPTS_DIR))
    from rm_lib import EsminiRMLib  # type: ignore[attr-defined]

    from GT_esmini.web.backend.services.road_geometry_service import (
        ESMINI_RM_LOCK,
        init_odr_cached,
    )

    hits: list[tuple[int, int, float]] = []
    twoway: dict[int, bool] = {}
    with ESMINI_RM_LOCK:
        rm = EsminiRMLib(str(ESMINI_RM_LIB))
        assert init_odr_cached(rm, xodr, "rm") >= 0
        handle = rm.CreatePosition()
        try:
            for index, point in enumerate(path):
                px, py = float(point["x"]), float(point["y"])
                rm.SetWorldXYHPosition(handle, px, py, 0.0)
                _, data = rm.GetPositionData(handle)
                road, s = int(data.roadId), float(data.s)
                if road not in twoway:
                    length = rm.GetRoadLength(road) or 0.0

                    def widest(lane_id: int, road: int = road, length: float = length):
                        return max(
                            rm.GetLaneWidthByRoadId(road, lane_id, length * f)[1]
                            for f in (0.0, 0.1, 0.25, 0.5, 0.75, 0.9, 0.999)
                        )

                    twoway[road] = widest(1) > 0.1 and widest(-1) > 0.1
                if not twoway[road]:
                    continue
                rm.SetLanePosition(handle, road, 0, 0.0, s, True)
                _, ref = rm.GetPositionData(handle)
                distance = math.hypot(px - float(ref.x), py - float(ref.y))
                if distance < 1.0:
                    hits.append((index, road, round(distance, 3)))
        finally:
            rm.DeletePosition(handle)
    return hits


@requires_libs
def test_drawn_path_avoids_the_centre_line_where_a_turn_pocket_has_not_opened():
    """road 222 -> road 196 through road 202, whose lane +1 is a turn pocket.

    multi_intersections road 202 declares lane +1 with three width records: full
    3.75 m up to s=33.5, a taper to s=59, then flat ZERO. The route arrives on
    road 202 at s=109 and must leave on lane +1, so for the first ~50 m the lane
    it leaves on does not physically exist. Drawing the whole road on that lane
    put 32 of 130 points exactly on the road's reference line -- the centre line.
    """
    start = _world_point(MULTI_INTERSECTIONS, 222, -1, 10.0)
    goal = _world_point(MULTI_INTERSECTIONS, 196, -1, 31.0)
    plan = plan_route(MULTI_INTERSECTIONS, [start, goal])

    # Preconditions: this is the case with the pocket, and a change into it.
    assert (202, 1) in _chain(plan)
    assert [
        (lc["road_id"], lc["from_lane_id"], lc["to_lane_id"])
        for lc in plan["lane_changes"]
    ] == [(202, 2, 1)]

    assert _centre_line_hits(MULTI_INTERSECTIONS, plan["path"]) == []


@requires_libs
def test_centre_line_check_fires_when_the_lane_chain_is_ignored(monkeypatch):
    """Negative control for the test above.

    Without it, `== []` would also pass if `_centre_line_hits` never measured
    anything. Restoring the old rule -- draw every road on the waypoint's exit
    lane -- must put points back on the centre line.
    """
    from GT_esmini.web.backend.services import route_planner_service

    monkeypatch.setattr(
        route_planner_service,
        "_lane_chain",
        lambda road_id, exit_lane_id, lane_changes: [exit_lane_id],
    )
    start = _world_point(MULTI_INTERSECTIONS, 222, -1, 10.0)
    goal = _world_point(MULTI_INTERSECTIONS, 196, -1, 31.0)
    plan = route_planner_service.plan_route(MULTI_INTERSECTIONS, [start, goal])

    hits = _centre_line_hits(MULTI_INTERSECTIONS, plan["path"])
    assert len(hits) > 10, f"expected the old rule to hit the centre line, got {hits}"
    assert all(road == 202 for _, road, _ in hits)


@requires_libs
def test_lane_change_is_drawn_where_the_pocket_opens_not_at_the_road_entry():
    """The change into road 202's lane +1 belongs at the taper, not at s=109.

    Placing it at the road entry would be the other way to keep the line off the
    centre line, and it would be wrong: it draws the car sitting in a lane that
    has not started yet.
    """
    import sys

    from GT_esmini.web.backend.config import GT_SCRIPTS_DIR

    start = _world_point(MULTI_INTERSECTIONS, 222, -1, 10.0)
    goal = _world_point(MULTI_INTERSECTIONS, 196, -1, 31.0)
    plan = plan_route(MULTI_INTERSECTIONS, [start, goal])

    if str(GT_SCRIPTS_DIR) not in sys.path:
        sys.path.insert(0, str(GT_SCRIPTS_DIR))
    from rm_lib import EsminiRMLib  # type: ignore[attr-defined]

    from GT_esmini.web.backend.services.road_geometry_service import (
        ESMINI_RM_LOCK,
        init_odr_cached,
    )

    with ESMINI_RM_LOCK:
        rm = EsminiRMLib(str(ESMINI_RM_LIB))
        assert init_odr_cached(rm, MULTI_INTERSECTIONS, "rm") >= 0
        handle = rm.CreatePosition()
        try:
            # Highest s at which the drawn line is still off lane +2's centre.
            # Travel on road 202 is -s, so that is where the change STARTS.
            departed_at = None
            for point in plan["path"]:
                px, py = float(point["x"]), float(point["y"])
                rm.SetWorldXYHPosition(handle, px, py, 0.0)
                _, data = rm.GetPositionData(handle)
                if int(data.roadId) != 202:
                    continue
                s = float(data.s)
                rm.SetLanePosition(handle, 202, 2, 0.0, s, True)
                _, lane2 = rm.GetPositionData(handle)
                if math.hypot(px - float(lane2.x), py - float(lane2.y)) > 0.2:
                    departed_at = s if departed_at is None else max(departed_at, s)
        finally:
            rm.DeletePosition(handle)

    assert departed_at is not None, "the line never left lane +2"
    # The pocket is zero-width above s=59, so the change must start below it --
    # and not be deferred until after the taper has fully opened (s=33.5).
    assert 30.0 < departed_at < 59.0, f"lane change started at s={departed_at}"


# ---------------------------------------------------------------------------
# Goal lane: the router matches the target lane exactly on the final hop, so a
# click on a lane the connector does not feed must not cost the whole turn
# ---------------------------------------------------------------------------


@requires_libs
def test_turn_is_not_routed_the_long_way_when_the_clicked_goal_lane_is_unreachable():
    """road 197 -> road 209 on multi_intersections: a turn, ~168 m.

    Connector 206 delivers road 197/+1 into road 209 on lane -2 only. The router
    matches the target lane EXACTLY on the final hop, so a click on lane -1 --
    the outer lane, 3.75 m from the one the connector feeds, and the one a user
    naturally aims at -- made the turn unroutable and sent the route round the
    block instead. The arrival lane is relaxed to the shortest reachable lane on
    the goal road, and the substitution is reported rather than hidden.
    """
    start = _world_point(MULTI_INTERSECTIONS, 197, 1, 69.0)
    goal = _world_point(MULTI_INTERSECTIONS, 209, -1, 83.0)
    plan = plan_route(MULTI_INTERSECTIONS, [start, goal])

    assert plan["length"] < 300.0, f"route is a detour: {plan['length']:.1f} m"
    # The route enters 209 on -2 because that is all connector 206 feeds, then
    # changes into the lane that was clicked -- so the goal is the clicked lane
    # and there is nothing to report as an adjustment.
    assert _chain(plan) == [(197, 1), (206, -1), (209, -1)]
    assert [
        (lc["road_id"], lc["from_lane_id"], lc["to_lane_id"])
        for lc in plan["lane_changes"]
    ] == [(209, -2, -1)]
    assert plan["lane_adjustments"] == []


@requires_libs
def test_detour_check_fires_without_goal_lane_relaxation(monkeypatch):
    """Negative control: with only the clicked lane tried, the detour comes back.

    `_drivable_lanes` supplies the alternative arrival lanes, so emptying it
    leaves `_plan_leg` with the clicked lane alone -- the pre-fix behaviour.
    """
    from GT_esmini.web.backend.services import route_planner_service

    start = _world_point(MULTI_INTERSECTIONS, 197, 1, 69.0)
    goal = _world_point(MULTI_INTERSECTIONS, 209, -1, 83.0)
    monkeypatch.setattr(
        route_planner_service, "_drivable_lanes", lambda rm, road_id, s: []
    )
    plan = route_planner_service.plan_route(MULTI_INTERSECTIONS, [start, goal])

    assert plan["length"] > 900.0, f"expected the detour back, got {plan['length']:.1f}"
    assert plan["lane_adjustments"] == []


@requires_libs
def test_arrival_on_the_other_carriageway_is_reported_as_such():
    """A near-miss and a different destination must not look the same.

    Both are 3.75 m on the map and neither shows in the drawn line at map zoom,
    but one ends the route facing the way the user pointed and the other does
    not. multi_intersections 196/-1 -> 222/+1 can only be reached on the far
    carriageway, so it is reported; 197/+1 -> 209/-1 is one lane over on the
    same carriageway, so it is simply driven to.
    """
    across = plan_route(
        MULTI_INTERSECTIONS,
        [
            _world_point(MULTI_INTERSECTIONS, 196, -1, 10.0),
            _world_point(MULTI_INTERSECTIONS, 222, 1, 40.0),
        ],
    )
    alongside = plan_route(
        MULTI_INTERSECTIONS,
        [
            _world_point(MULTI_INTERSECTIONS, 197, 1, 69.0),
            _world_point(MULTI_INTERSECTIONS, 209, -1, 83.0),
        ],
    )

    # Other carriageway: cannot be reached by changing lanes, so it is reported.
    assert [a["opposite_direction"] for a in across["lane_adjustments"]] == [True]
    # Same carriageway: reached by changing lanes, so there is nothing to report.
    assert alongside["lane_adjustments"] == []
    assert [
        (lc["road_id"], lc["from_lane_id"], lc["to_lane_id"])
        for lc in alongside["lane_changes"]
    ] == [(209, -2, -1)]


@requires_libs
def test_the_other_carriageway_stays_a_candidate_when_nothing_else_reaches():
    """Pins the decision NOT to restrict substitutes to the clicked direction.

    Doing so reads as the safe choice and is not: on fabriksgatan 3/-1 -> 0/+1
    it leaves no route at all, and on multi_intersections 202/-1 -> 217/+1 it
    costs 215.7 m -> 1567.6 m. Which carriageway a click lands on is decided by
    a few centimetres, so it is reported (above), not enforced.
    """
    plan = plan_route(
        FABRIKSGATAN,
        [
            _world_point(FABRIKSGATAN, 3, -1, 20.0),
            _world_point(FABRIKSGATAN, 0, 1, 30.0),
        ],
    )

    assert plan["length"] < 200.0, f"expected a short route, got {plan['length']:.1f} m"
    assert [a["opposite_direction"] for a in plan["lane_adjustments"]] == [True]


# ---------------------------------------------------------------------------
# The loaded-map cache: it must never answer from the wrong xodr
# ---------------------------------------------------------------------------


@requires_libs
def test_alternating_between_maps_answers_from_the_right_one():
    """Init() is cached per DLL, so the failure mode is silent, not loud.

    Both DLLs hold ONE OpenDrive. Skipping a re-Init when the map has changed
    would answer road ids from the previous map -- plausible numbers, wrong map,
    no error anywhere. Alternating catches it; planning each map once does not.
    """
    fab = [
        _world_point(FABRIKSGATAN, 3, -1, 20.0),
        _world_point(FABRIKSGATAN, 2, -1, 30.0),
    ]
    multi = [
        _world_point(MULTI_INTERSECTIONS, 222, -1, 10.0),
        _world_point(MULTI_INTERSECTIONS, 196, -1, 31.0),
    ]

    first_fab = _chain(plan_route(FABRIKSGATAN, fab))
    first_multi = _chain(plan_route(MULTI_INTERSECTIONS, multi))
    second_fab = _chain(plan_route(FABRIKSGATAN, fab))
    second_multi = _chain(plan_route(MULTI_INTERSECTIONS, multi))

    assert first_fab == second_fab
    assert first_multi == second_multi
    # And the two maps really are distinguishable, so the assertions above are
    # not comparing two copies of the same answer.
    assert first_fab != first_multi
    assert {road for road, _ in first_multi} & {222, 196}


@requires_libs
def test_a_map_replaced_at_the_same_path_is_re_read(tmp_path):
    """The cache key is (path, mtime, size), not path.

    A road re-uploaded under its old name is the case a path-only key gets
    wrong, and the web UI does exactly that.
    """
    import shutil

    from GT_esmini.web.backend.services import road_geometry_service

    road = tmp_path / "road.xodr"
    shutil.copy(FABRIKSGATAN, road)
    before = _chain(
        plan_route(
            road,
            [
                _world_point(road, 3, -1, 20.0),
                _world_point(road, 2, -1, 30.0),
            ],
        )
    )

    shutil.copy(MULTI_INTERSECTIONS, road)
    after = _chain(
        plan_route(
            road,
            [
                _world_point(road, 222, -1, 10.0),
                _world_point(road, 196, -1, 31.0),
            ],
        )
    )

    assert before != after
    assert {r for r, _ in after} & {222, 196}
    assert road_geometry_service._ODR_LOADED["rm"] is not None


# ---------------------------------------------------------------------------
# Where a lane opens beside the route, the drawn line must not swing out and
# come back
# ---------------------------------------------------------------------------


def _lateral_offsets(xodr: Path, path: list[dict], road_id: int) -> list[float]:
    """Signed-magnitude offset from the road's reference line, per path point."""
    import sys

    from GT_esmini.web.backend.config import GT_SCRIPTS_DIR

    if str(GT_SCRIPTS_DIR) not in sys.path:
        sys.path.insert(0, str(GT_SCRIPTS_DIR))
    from rm_lib import EsminiRMLib  # type: ignore[attr-defined]

    from GT_esmini.web.backend.services.road_geometry_service import (
        ESMINI_RM_LOCK,
        init_odr_cached,
    )

    offsets: list[float] = []
    with ESMINI_RM_LOCK:
        rm = EsminiRMLib(str(ESMINI_RM_LIB))
        assert init_odr_cached(rm, xodr, "rm") >= 0
        handle = rm.CreatePosition()
        try:
            for point in path:
                px, py = float(point["x"]), float(point["y"])
                rm.SetWorldXYHPosition(handle, px, py, 0.0)
                _, data = rm.GetPositionData(handle)
                if int(data.roadId) != road_id:
                    continue
                rm.SetLanePosition(handle, road_id, 0, 0.0, float(data.s), True)
                _, ref = rm.GetPositionData(handle)
                offsets.append(math.hypot(px - float(ref.x), py - float(ref.y)))
        finally:
            rm.DeletePosition(handle)
    return offsets


@requires_libs
def test_route_holds_its_line_where_a_turn_pocket_opens_beside_it():
    """road 222 -> road 196. On road 202 a left-turn pocket opens at s=59.

    A lane appearing between the through lane and the centre line pushes the
    through lane's centre a full width outward. A line that follows it and then
    crosses back into the pocket traces a 2.4 m excursion -- nobody drives that;
    the pocket opens where the car already is, so the car carries straight on.
    """
    plan = plan_route(
        MULTI_INTERSECTIONS,
        [
            _world_point(MULTI_INTERSECTIONS, 222, -1, 65.0),
            _world_point(MULTI_INTERSECTIONS, 196, -1, 30.0),
        ],
    )
    assert [
        (lc["road_id"], lc["from_lane_id"], lc["to_lane_id"])
        for lc in plan["lane_changes"]
    ] == [(202, 2, 1)]

    offsets = _lateral_offsets(MULTI_INTERSECTIONS, plan["path"], 202)
    assert (
        len(offsets) > 20
    ), f"expected the road-202 stretch, got {len(offsets)} points"
    swing = max(offsets) - min(offsets)
    assert swing < 1.0, f"the line swings {swing:.2f} m across road 202"


@requires_libs
def test_swing_check_fires_without_the_offset_blend(monkeypatch):
    """Negative control: drop the offset blend and the excursion comes back.

    `_offset_point` is what draws the change as a lateral move between pinned
    offsets. With it unavailable the sampler falls back to the lane centres,
    which is exactly the shape this test exists to keep out.
    """
    from GT_esmini.web.backend.services import route_planner_service

    monkeypatch.setattr(
        route_planner_service,
        "_offset_point",
        lambda rm, handle, road_id, s, offset: None,
    )
    plan = route_planner_service.plan_route(
        MULTI_INTERSECTIONS,
        [
            _world_point(MULTI_INTERSECTIONS, 222, -1, 65.0),
            _world_point(MULTI_INTERSECTIONS, 196, -1, 30.0),
        ],
    )
    offsets = _lateral_offsets(MULTI_INTERSECTIONS, plan["path"], 202)
    swing = max(offsets) - min(offsets)
    assert swing > 2.0, f"expected the excursion back, got {swing:.2f} m"
