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

    from GT_esmini.web.backend.services.road_geometry_service import ESMINI_RM_LOCK

    with ESMINI_RM_LOCK:
        rm = EsminiRMLib(str(ESMINI_RM_LIB))
        assert rm.Init(str(xodr)) >= 0
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

    from GT_esmini.web.backend.services.road_geometry_service import ESMINI_RM_LOCK

    with ESMINI_RM_LOCK:
        rm = EsminiRMLib(str(ESMINI_RM_LIB))
        assert rm.Init(str(FABRIKSGATAN)) >= 0
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
