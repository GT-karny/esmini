"""Lane-level route planning from map clicks.

Turns a list of world (x, y) points -- what the map UI produces when a user clicks a
start, an optional set of via points, and a goal -- into the Waypoint chain a
scenario needs.

Two libraries are involved and they do different jobs:

* ``esminiRMLib`` (EsminiRMLib) snaps each world point onto a lane, giving
  (road_id, lane_id, s). This is the same DLL road_geometry_service uses to build
  the polylines the user clicked on, so the snap agrees with what is on screen.
* ``GT_esminiLib`` (GtOdrMetadataLib) runs the lane-change-aware router
  (roadmanager::LaneIndependentRouter) between consecutive snapped points.

Why the lane-aware router and not the road-level RoadPath that Route::AddWaypoint
uses: RoadPath stops at ``nextRoad == targetRoad`` and never checks whether the
final hop's target lane is reachable, so it reports success for routes that need a
lane change it never verified. A UI that hands users a "click two points" affordance
cannot rely on that -- the user does not know the lane topology, so the planner has
to. See GT_esmini/docs/virtualdriver/design/route_lane_plan_design.md section 2.
"""

from __future__ import annotations

import logging
import math
from pathlib import Path

from GT_esmini.web.backend.config import ESMINI_RM_LIB, GT_ESMINI_LIB

# The SAME lock road_geometry_service takes. ctypes.CDLL hands back one loaded module
# per path, so both services drive a single process-global OpenDrive; separate locks
# would let one Init() another's map out from under it mid-request.
from GT_esmini.web.backend.services.road_geometry_service import (
    ESMINI_RM_LOCK,
    init_odr_cached,
)

logger = logging.getLogger(__name__)

# Lane types a route may be planned on. Deliberately narrower than road_geometry's
# _DRIVABLE_MASK, which includes shoulder/parking/biking so it can DRAW them --
# being drawable is not being routable, and snapping a click to a parking strip
# would produce a route no vehicle should drive.
_ROUTABLE_LANE_TYPES = (
    # RM_LANE_TYPE_DRIVING | ENTRY | EXIT | OFF_RAMP | ON_RAMP | CONNECTING_RAMP
    # | BIDIRECTIONAL -- i.e. rm_lib's RM_LANE_TYPE_ANY_DRIVING.
    (1 << 1)
    | (1 << 17)
    | (1 << 18)
    | (1 << 19)
    | (1 << 20)
    | (1 << 22)
    | (1 << 9)
)

_STRATEGY_NAMES = {
    "shortest": 0,
    "fastest": 1,
    "min_intersections": 2,
}


class RoutePlanError(Exception):
    """Raised when a route cannot be planned. Carries a machine-readable code."""

    def __init__(self, code: str, message: str, detail: dict | None = None):
        super().__init__(message)
        self.code = code
        self.detail = detail or {}


def _empty_plan(diagnostic: str) -> dict:
    return {
        "waypoints": [],
        "lane_changes": [],
        "length": -1.0,
        "diagnostic": diagnostic,
    }


def _snap_one(rm, pos_handle, x: float, y: float) -> dict:
    """Snap one world point onto a lane. Never raises -- reports on_road instead.

    This is the shared primitive. Two callers want DIFFERENT failure behaviour and
    conflating them was a UI defect: the map needs to show where a single click
    landed the instant it happens (and say "that one missed" for one bad point),
    while route planning must refuse outright. So the primitive reports status and
    each caller decides what a miss means.
    """
    # Heading is unknown for a click. It does not affect which lane the point snaps
    # to (that is decided by the t offset), and a route's departure direction is
    # taken from the lane's own legal driving direction later -- not from this.
    rm.SetWorldXYHPosition(pos_handle, x, y, 0.0)
    res, data = rm.GetPositionData(pos_handle)

    if res != 0 or data.roadId == 0xFFFFFFFF:
        return {"on_road": False, "reason": "off_road", "x": x, "y": y}

    # GetInLaneType is the ONLY reliable "is this point actually on a lane?" signal.
    # Two things that look like they would work do not: the return codes (0 even for
    # a point 100 km off the map) and click-vs-snapped distance (always exactly 0,
    # because GetPositionData echoes back the coordinates given to it).
    in_lane_type = rm.GetInLaneType(pos_handle)
    if not (in_lane_type & _ROUTABLE_LANE_TYPES):
        return {
            "on_road": False,
            "reason": "not_routable",
            "x": x,
            "y": y,
            "road_id": int(data.roadId),
            "lane_id": int(data.laneId),
            "in_lane_type": int(in_lane_type),
        }

    road_id = int(data.roadId)
    lane_id = int(data.laneId)
    s_val = float(data.s)

    # Resolve the LANE-CENTRE world position by setting the lane position back.
    #
    # data.x/data.y must NOT be used here: GetPositionData echoes back the very
    # coordinates SetWorldXYHPosition was given, so using them returns the click
    # unchanged and the marker never visibly snaps. The road/lane/s it reports ARE
    # resolved -- only the world position is the input verbatim.
    #
    # This is easy to "verify" wrongly: probing with a point that already sits on
    # the lane centre makes the echo and a correct snap identical. Test off-centre.
    rm.SetLanePosition(pos_handle, road_id, lane_id, 0.0, s_val, True)
    lane_res, lane_data = rm.GetPositionData(pos_handle)
    if lane_res != 0:
        # Should not happen for a position we just resolved; fall back to the click
        # rather than dropping the point.
        return {
            "on_road": True,
            "road_id": road_id,
            "lane_id": lane_id,
            "s": s_val,
            "x": x,
            "y": y,
            "h": float(data.h),
        }

    return {
        "on_road": True,
        "road_id": road_id,
        "lane_id": lane_id,
        "s": s_val,
        "x": float(lane_data.x),
        "y": float(lane_data.y),
        "h": float(lane_data.h),
    }


def snap_points(xodr_path, points: list[dict]) -> list[dict]:
    """Snap world points onto lanes, reporting per-point status. Never raises for
    a point that missed -- the map shows that as a marker state, not as an error."""
    xodr_path = Path(xodr_path).resolve()
    if not xodr_path.is_file():
        raise RoutePlanError("xodr_not_found", f"xodr file not found: {xodr_path}")
    rm_lib_path = str(ESMINI_RM_LIB)
    if not Path(rm_lib_path).is_file():
        raise RoutePlanError(
            "library_unavailable", f"esminiRMLib not found: {rm_lib_path}"
        )

    from rm_lib import EsminiRMLib  # type: ignore[attr-defined]

    with ESMINI_RM_LOCK:
        rm = EsminiRMLib(rm_lib_path)
        if init_odr_cached(rm, xodr_path, "rm") < 0:
            raise RoutePlanError(
                "xodr_load_failed", f"esminiRMLib failed to load {xodr_path}"
            )
        handle = rm.CreatePosition()
        try:
            return [_snap_one(rm, handle, float(p["x"]), float(p["y"])) for p in points]
        finally:
            rm.DeletePosition(handle)


def _snap_points(rm, pos_handle, points: list[dict]) -> list[dict]:
    """Strict snap for route planning: a point that missed is an error, named by
    index -- "no route" and "you clicked on grass" need different UI responses."""
    snapped = []
    for index, pt in enumerate(points):
        x = float(pt["x"])
        y = float(pt["y"])
        # Heading is unknown for a click. It does not affect which lane the point
        # snaps to (that is decided by the t offset), and the route's departure
        # direction is taken from the lane's own legal driving direction later --
        # not from this value.
        rm.SetWorldXYHPosition(pos_handle, x, y, 0.0)
        res, data = rm.GetPositionData(pos_handle)

        if res != 0 or data.roadId == 0xFFFFFFFF:
            raise RoutePlanError(
                "point_off_road",
                f"Point {index} ({x:.1f}, {y:.1f}) does not lie on any road.",
                {"index": index, "x": x, "y": y},
            )

        # GetInLaneType is the ONLY reliable "is this point actually on a lane?"
        # signal here. Two things that look like they would work do not:
        #   * the return codes -- SetWorldXYHPosition and GetPositionData both
        #     report 0 for a point 100 km off the map; esmini snaps to the nearest
        #     road unconditionally and calls that success.
        #   * comparing the click to the snapped position -- GetPositionData echoes
        #     back the x/y you supplied, so that distance is always exactly 0.
        # Measured on fabriksgatan road 0: sweeping perpendicular from the lane
        # centre gives DRIVING out to 5 m and NONE from 8 m, i.e. it flips at the
        # road edge, which is the behaviour a map click needs.
        in_lane_type = rm.GetInLaneType(pos_handle)
        if not (in_lane_type & _ROUTABLE_LANE_TYPES):
            raise RoutePlanError(
                "point_not_routable",
                f"Point {index} ({x:.1f}, {y:.1f}) is not on a drivable lane "
                f"(nearest: road {int(data.roadId)}, lane {int(data.laneId)}).",
                {
                    "index": index,
                    "x": x,
                    "y": y,
                    "road_id": int(data.roadId),
                    "lane_id": int(data.laneId),
                    "in_lane_type": int(in_lane_type),
                },
            )

        snapped.append(
            {
                "road_id": int(data.roadId),
                "lane_id": int(data.laneId),
                "s": float(data.s),
                "x": float(data.x),
                "y": float(data.y),
                "h": float(data.h),
            }
        )
    return snapped


def _drivable_lanes(rm, road_id: int, s: float) -> list[int]:
    """Drivable lane ids on a road at s (empty if the query fails)."""
    try:
        n = rm.GetRoadNumberOfDrivableLanes(road_id, s)
    except Exception:
        return []
    lanes: list[int] = []
    for i in range(n or 0):
        res = rm.GetDrivableLaneIdByIndex(road_id, i, s)
        lane_id = res[1] if isinstance(res, tuple) else res
        if lane_id is not None:
            lanes.append(int(lane_id))
    return lanes


def _finish_in_the_clicked_lane(
    rm, lib, goal: dict, arrived_lane: int, waypoints: list, lane_changes: list
) -> tuple[int, list, list]:
    """Turn a substituted destination lane into a lane change on the goal road.

    "The router could not END in that lane" is not "you cannot be in that lane".
    Entering road 209 from connector 206 puts you in lane -2 whatever you asked
    for, but -2 is a drop lane: it tapers to zero width at s=59. A goal clicked
    at s=67 IS lane -1, reachable by the ordinary lane change a driver makes --
    and the router emits exactly that change itself (road 209, -2 -> -1) when the
    route continues past 209 instead of ending on it. Ending on -2 instead put
    the destination in a lane that is not there.

    Only lanes running the SAME way qualify. The other carriageway is not a lane
    change, and that case stays a reported adjustment.
    """
    clicked = goal["lane_id"]
    if lib.GetLaneDrivingDirection(
        goal["road_id"], clicked, goal["s"]
    ) != lib.GetLaneDrivingDirection(goal["road_id"], arrived_lane, goal["s"]):
        return arrived_lane, waypoints, lane_changes
    if clicked not in _drivable_lanes(rm, goal["road_id"], goal["s"]):
        return arrived_lane, waypoints, lane_changes

    lane_changes = lane_changes + [
        {
            "road_id": int(goal["road_id"]),
            # s is a placeholder throughout -- the router does not locate its own
            # lane changes either (route_lanechange_util.hpp). Where the change is
            # DRAWN is decided from lane widths in _lane_change_windows.
            "s": 0.0,
            "from_lane_id": int(arrived_lane),
            "to_lane_id": int(clicked),
        }
    ]
    if waypoints and waypoints[-1]["road_id"] == goal["road_id"]:
        # The last waypoint is the destination, so it must name the lane the user
        # clicked -- it is what the generated xosc writes as its final
        # LanePosition, and what the map draws the goal marker on.
        waypoints = waypoints[:-1] + [
            {
                **waypoints[-1],
                "lane_id": int(clicked),
                "x": goal["x"],
                "y": goal["y"],
                "h": goal["h"],
            }
        ]
    return clicked, waypoints, lane_changes


def _plan_leg(
    rm, lib, start: dict, goal: dict, strategy: int
) -> tuple[list, list, float, int]:
    """Route one start->goal leg. Returns (waypoints, lane_changes, length, goal_lane).

    THE DESTINATION LANE IS NEGOTIABLE; THE START LANE IS NOT.
    ----------------------------------------------------------
    LaneIndependentRouter matches the target lane EXACTLY on the final hop: it
    will not accept "arrive on the destination road in a neighbouring lane and
    change lanes there". A click that lands one lane off therefore does not give a
    slightly different route -- it gives a completely different one.

    Measured on multi_intersections, road 197 lane +1 -> road 209:
        -> lane -2 :   3 roads,  168 m   (the right turn a driver would make;
                                          junction 146's connector 206 delivers
                                          197/+1 into 209/-2 and nowhere else)
        -> lane -1 :  13 roads, 1086 m   (a loop, because nothing connects INTO -1)
        -> lane +1 :  11 roads,  913 m
    The user clicked lane -1 and got the 1086 m loop, which reads as a broken
    router. It is not: it is an exact-match constraint meeting a click whose lane
    was incidental.

    So each leg's GOAL lane is relaxed to any drivable lane of that road and the
    shortest wins. Arriving elsewhere is then the LAST resort, not the answer:
    _finish_in_the_clicked_lane turns the substitution back into a lane change on
    the goal road whenever the clicked lane runs the same way, which is what the
    router itself emits for the identical geometry when the route continues past
    that road instead of ending on it. Only a lane no lane change can reach --
    the other carriageway -- is reported as an adjustment.

    The START lane is left alone -- that is where the vehicle physically is, and
    moving it would put the car somewhere the user did not ask for.
    """
    same_road = start["road_id"] == goal["road_id"]
    same_lane = start["lane_id"] == goal["lane_id"]
    if same_road and same_lane:
        # LaneIndependentRouter rejects this outright ("start pos and target pos on
        # same road and lane"), but it is a perfectly ordinary request: drive along
        # this lane. Answer it directly instead of surfacing a router error.
        return (
            [
                {
                    "road_id": start["road_id"],
                    "junction_id": None,
                    "lane_id": start["lane_id"],
                    "s": goal["s"],
                    "x": goal["x"],
                    "y": goal["y"],
                    "z": 0.0,
                    "h": goal["h"],
                }
            ],
            [],
            abs(goal["s"] - start["s"]),
            start["lane_id"],
        )

    # Clicked lane first so an exact tie keeps the user's choice.
    #
    # The opposite carriageway stays a candidate. Restricting substitutes to the
    # clicked lane's own direction was tried and is worse: on fabriksgatan
    # 3/-1 -> 0/+1 it turns a 134 m route into NO ROUTE, and on
    # multi_intersections 202/-1 -> 217/+1 it costs 215.7 m -> 1567.6 m. Which
    # carriageway a click lands on is decided by a few centimetres the user
    # cannot aim at, so honouring it at that price serves nobody. What the user
    # genuinely cannot see is WHICH side the route ends up on -- so that is
    # reported (see lane_adjustments / opposite_direction) rather than legislated.
    candidates = [goal["lane_id"]] + [
        ln
        for ln in _drivable_lanes(rm, goal["road_id"], goal["s"])
        if ln != goal["lane_id"]
    ]

    best: tuple[float, int, list, list] | None = None
    for lane_id in candidates:
        rc_try = lib.CalcRouteInDrivingDirection(
            start["road_id"],
            start["lane_id"],
            start["s"],
            goal["road_id"],
            lane_id,
            goal["s"],
            strategy,
        )
        if rc_try < 0:
            continue
        length = lib.GetRouteLength()
        if length < 0:
            continue
        if best is None or length < best[0] - 1e-6:
            best = (length, lane_id, lib.GetRouteWaypoints(), lib.GetLaneChanges())

    if best is not None:
        length, lane_id, wps, lcs = best
        if lane_id != goal["lane_id"]:
            lane_id, wps, lcs = _finish_in_the_clicked_lane(
                rm, lib, goal, lane_id, wps, lcs
            )
        return wps, lcs, length, lane_id

    rc = -2
    if rc < 0:
        # NOTE: the header documents -1 for bad args and -2 for "no route", but in
        # practice invalid road/lane ids also come back as -2 (only a missing map
        # yields -1). Inputs are validated during the snap above, so by this point
        # any negative code means the same thing: no lane-connected path exists.
        raise RoutePlanError(
            "no_route",
            f"No drivable route from road {start['road_id']} lane {start['lane_id']} "
            f"to road {goal['road_id']} lane {goal['lane_id']}.",
            {"rc": int(rc), "start": start, "goal": goal},
        )
    return (
        lib.GetRouteWaypoints(),
        lib.GetLaneChanges(),
        lib.GetRouteLength(),
        goal["lane_id"],
    )


_PATH_STEP_M = 2.0
_SEAM_EPS_M = 0.01
# The width at which a lane counts as having started. Only has to clear the
# floating-point noise of a taper's first metre -- see _lane_change_windows for
# why the change begins there and not where the lane is wide enough to sit in.
_LANE_OPEN_EPS_M = 0.10
# How long the drawn lane change takes. Drawing it as an instant sideways jump
# reads as a glitch; a real change takes a few seconds at road speed.
_LANE_CHANGE_BLEND_M = 25.0


def _lane_width(rm, road_id: int, lane_id: int, s: float) -> float:
    """Width of one lane at one s, or 0.0 if it cannot be had."""
    try:
        res = rm.GetLaneWidthByRoadId(road_id, lane_id, s)
    except Exception:  # pragma: no cover - defensive, ctypes-level failure
        return 0.0
    if not res:
        return 0.0
    rc, width = res
    return float(width) if rc == 0 else 0.0


def _lane_chain(road_id: int, exit_lane_id: int, lane_changes: list[dict]) -> list[int]:
    """The lanes the route occupies on one road, in travel order.

    A waypoint names the lane the route LEAVES a road on. When the route also
    has to change lanes on that road, the lane it ARRIVES on is only in the
    lane-change list -- drawing the whole road on the waypoint's lane is what
    put the line on the centre line (see _sample_route_path).
    """
    changes = [lc for lc in lane_changes if int(lc["road_id"]) == int(road_id)]
    if not changes:
        return [exit_lane_id]
    chain = [int(changes[0]["from_lane_id"])]
    for lc in changes:
        chain.append(int(lc["to_lane_id"]))
    if chain[-1] != exit_lane_id:
        chain.append(exit_lane_id)
    return chain


def _lane_change_windows(rm, pos_handle, road_id, chain, start, end):
    """Place each lane change of one road along the travelled span.

    A change is drawn where the target lane is actually there to move into: a
    turn pocket that opens before a junction is entered where it opens, not at
    the far end of the road where its width is still zero. Widths are read from
    the road, so this needs no per-map tuning.

    The change spans the TAPER, from the first metre the target lane exists to
    where it reaches full width. Waiting until it is wide enough to sit in is
    what makes the drawn route wiggle: a lane appearing between the through lane
    and the centre line pushes the through lane's centre sideways, so a line that
    stays on it until halfway up the taper swings out and then cuts back. Nobody
    drives that. Blending across the whole taper keeps the line where the car
    already is while the new lane opens beneath it.

    Returns [(s_from, s_to, from_lane, to_lane), ...] in travel order.
    """
    if len(chain) < 2:
        return []
    direction = 1.0 if end >= start else -1.0
    span = abs(end - start)
    probes = [
        start + direction * min(k * _PATH_STEP_M, span)
        for k in range(int(span / _PATH_STEP_M) + 2)
    ]

    windows = []
    cursor = start
    for lane_a, lane_b in zip(chain, chain[1:]):
        widths = [(s, _lane_width(rm, road_id, lane_b, s)) for s in probes]
        ahead = [(s, w) for s, w in widths if (s - cursor) * direction >= 0]
        widest = max((w for _, w in ahead), default=0.0)

        # Where the lane starts to exist, and where it has finished opening. A
        # lane that is full width throughout gives s_open == s_full == cursor,
        # and then the change simply takes _LANE_CHANGE_BLEND_M.
        s_open = next((s for s, w in ahead if w > _LANE_OPEN_EPS_M), cursor)
        s_full = next(
            (
                s
                for s, w in ahead
                if (s - s_open) * direction >= 0 and w >= 0.95 * widest
            ),
            s_open,
        )
        # An opening lane sets the length: the change IS the taper. The fixed
        # minimum is for the ordinary case where the target lane is simply there.
        taper = abs(s_full - s_open)
        s_done = s_open + direction * (
            taper if taper > _PATH_STEP_M else _LANE_CHANGE_BLEND_M
        )
        if (s_done - end) * direction > 0:
            s_done = end
        t_from = _lane_offset(rm, pos_handle, road_id, lane_a, s_open)
        t_to = _lane_offset(rm, pos_handle, road_id, lane_b, s_done)
        if t_from is None or t_to is None:
            # Without both ends the offset blend has nothing to pin; skipping the
            # window leaves the line on lane_a, which is wrong but not silent --
            # the route simply does not show the change.
            continue
        windows.append((s_open, s_done, lane_a, lane_b, t_from, t_to))
        cursor = s_done
    return windows


def _sample_route_path(
    rm, lib, pos_handle, waypoints: list[dict], lane_changes: list[dict]
) -> list[dict]:
    """Sample the route along lane centres so it can be DRAWN as the road runs.

    Joining waypoints with straight lines looks fine on a straight road and lies
    everywhere else: with three waypoints a junction turn renders as two chords
    cutting the corner, which reads as "the router picked a silly route" when the
    route is correct and only the drawing is wrong.

    Each waypoint owns exactly one road, so every road is sampled once:
      * the first waypoint starts at its own s, later ones at the road's entry
      * the last waypoint ends at its own s, earlier ones at the road's exit
    Entry/exit depend on the lane's legal driving direction, not on lane-id sign
    (which is inverted under left-hand traffic).

    Within a road the route may change lanes, and then the waypoint's lane is
    only the lane it LEAVES on. Sampling that lane for the whole road draws the
    line on a lane that is not there yet: a turn pocket has width 0 upstream of
    its taper, and a zero-width lane's centre IS the road reference line -- the
    centre line. So the lane chain is followed, and each change is drawn where
    the target lane opens.
    """
    path: list[dict] = []
    last = len(waypoints) - 1
    for i, wp in enumerate(waypoints):
        road_id = wp["road_id"]
        lane_id = wp["lane_id"]
        length = rm.GetRoadLength(road_id)
        if length is None or length <= 0:
            continue
        direction = lib.GetLaneDrivingDirection(road_id, lane_id, wp["s"]) or 1
        entry = 0.0 if direction > 0 else length
        exit_ = length if direction > 0 else 0.0
        start = wp["s"] if i == 0 else entry
        end = wp["s"] if i == last else exit_

        chain = _lane_chain(road_id, lane_id, lane_changes)
        windows = _lane_change_windows(rm, pos_handle, road_id, chain, start, end)
        travel = 1.0 if end >= start else -1.0

        span = abs(end - start)
        steps = max(1, int(span / _PATH_STEP_M))
        for k in range(steps + 1):
            s_val = start + (end - start) * (k / steps)
            # Clamp: s slightly past either end is rejected and would silently
            # drop a sample, leaving a visible notch at every road boundary.
            # Nudge off the exact boundary. At s == 0 or s == length the position
            # sits on the seam between two roads and lane identity is ambiguous --
            # the sample can resolve onto a NEIGHBOURING connector, putting one
            # point of the line off the lane at every junction.
            s_val = min(max(s_val, _SEAM_EPS_M), max(length - _SEAM_EPS_M, 0.0))

            lane_here = chain[0]
            blend_to = None
            frac = 0.0
            t_from = t_to = 0.0
            for s_open, s_done, lane_a, lane_b, w_from, w_to in windows:
                if (s_val - s_open) * travel < 0:
                    break
                if (s_val - s_done) * travel >= 0:
                    lane_here = lane_b
                    continue
                lane_here = lane_a
                blend_to = lane_b
                t_from, t_to = w_from, w_to
                frac = (s_val - s_open) / (s_done - s_open) if s_done != s_open else 1.0
                break

            if blend_to is None:
                point = _lane_centre_xy(rm, pos_handle, road_id, lane_here, s_val)
            else:
                # Interpolate the LATERAL OFFSET between the two lane centres
                # taken at the ends of the window -- not between the two lane
                # centres at this s, which are both still moving.
                #
                # Where a turn pocket opens, the through lane's centre slides
                # outward by a full lane width and then the route crosses back
                # into the pocket: interpolating moving centres traces that whole
                # excursion, so the drawn line bulges out and returns (2.4 m on
                # multi_intersections road 202) where a driver simply carries
                # straight on as the pocket opens beside them. With the ends
                # pinned, the same window draws the straight line, and on a road
                # of constant width the two formulations agree exactly.
                offset = t_from + (t_to - t_from) * frac
                point = _offset_point(rm, pos_handle, road_id, s_val, offset)
                if point is None:
                    point = _lane_centre_xy(rm, pos_handle, road_id, lane_here, s_val)
            if point is None:
                continue
            path.append(point)
    return path


def _road_frame(rm, pos_handle, road_id: int, s: float):
    """(x, y, nx, ny) of the road's reference line at s; n is the +t normal."""
    rm.SetLanePosition(pos_handle, road_id, 0, 0.0, s, True)
    res, data = rm.GetPositionData(pos_handle)
    if res != 0:
        return None
    heading = float(data.h)
    return (float(data.x), float(data.y), -math.sin(heading), math.cos(heading))


def _lane_offset(rm, pos_handle, road_id: int, lane_id: int, s: float):
    """Signed lateral offset of a lane's centre from the reference line at s."""
    frame = _road_frame(rm, pos_handle, road_id, s)
    centre = _lane_centre_xy(rm, pos_handle, road_id, lane_id, s)
    if frame is None or centre is None:
        return None
    x, y, nx, ny = frame
    return (centre["x"] - x) * nx + (centre["y"] - y) * ny


def _offset_point(rm, pos_handle, road_id: int, s: float, offset: float):
    """World point at a signed lateral offset from the reference line at s."""
    frame = _road_frame(rm, pos_handle, road_id, s)
    if frame is None:
        return None
    x, y, nx, ny = frame
    return {"x": x + offset * nx, "y": y + offset * ny}


def _lane_centre_xy(rm, pos_handle, road_id: int, lane_id: int, s: float):
    rm.SetLanePosition(pos_handle, road_id, lane_id, 0.0, s, True)
    res, data = rm.GetPositionData(pos_handle)
    if res != 0:
        return None
    return {"x": float(data.x), "y": float(data.y)}


def plan_route(xodr_path, points: list[dict], strategy: str = "shortest") -> dict:
    """Plan a lane-level route through the given world points.

    Args:
        xodr_path: OpenDRIVE file the points were clicked on.
        points: >= 2 dicts with "x"/"y" world coordinates, in travel order.
        strategy: "shortest" | "fastest" | "min_intersections".

    Returns:
        {"waypoints": [...], "lane_changes": [...], "length": float,
         "diagnostic": "ok", "snapped": [...]}

    Raises:
        RoutePlanError: bad input, missing library, or no drivable route.
    """
    if len(points) < 2:
        raise RoutePlanError(
            "too_few_points", "A route needs at least a start and a goal point."
        )
    if strategy not in _STRATEGY_NAMES:
        raise RoutePlanError(
            "bad_strategy",
            f"Unknown route strategy '{strategy}'; "
            f"expected one of {sorted(_STRATEGY_NAMES)}.",
        )
    strategy_id = _STRATEGY_NAMES[strategy]

    xodr_path = Path(xodr_path).resolve()
    if not xodr_path.is_file():
        raise RoutePlanError("xodr_not_found", f"xodr file not found: {xodr_path}")

    rm_lib_path = str(ESMINI_RM_LIB)
    gt_lib_path = str(GT_ESMINI_LIB)
    for label, path in (("esminiRMLib", rm_lib_path), ("GT_esminiLib", gt_lib_path)):
        if not Path(path).is_file():
            raise RoutePlanError("library_unavailable", f"{label} not found: {path}")

    # Lazy import: GT_SCRIPTS_DIR is on sys.path via config.py.
    from rm_lib import EsminiRMLib, GtOdrMetadataLib  # type: ignore[attr-defined]

    with ESMINI_RM_LOCK:
        rm = EsminiRMLib(rm_lib_path)
        if init_odr_cached(rm, xodr_path, "rm") < 0:
            raise RoutePlanError(
                "xodr_load_failed", f"esminiRMLib failed to load {xodr_path}"
            )
        pos_handle = rm.CreatePosition()
        try:
            snapped = _snap_points(rm, pos_handle, points)
        finally:
            rm.DeletePosition(pos_handle)

        lib = GtOdrMetadataLib(gt_lib_path)
        if not lib.HasRouteApi():
            raise RoutePlanError(
                "route_api_missing",
                "GT_esminiLib lacks the route exports; rebuild it (Protocol A).",
            )
        if not lib.HasRouteDirectionApi():
            # Refuse rather than silently fall back to the +s-only path: that build
            # cannot find routes leaving a road's predecessor end AND reports
            # wrong-way routes as valid, so a UI on top of it would mislead.
            raise RoutePlanError(
                "route_direction_api_missing",
                "GT_esminiLib predates GT_RM_CalcRouteH; rebuild it (Protocol A) "
                "so routes respect each lane's legal driving direction.",
            )
        if init_odr_cached(lib, xodr_path, "gt", ok=lambda rc: rc == 0) != 0:
            raise RoutePlanError(
                "xodr_load_failed", f"GT_esminiLib failed to load {xodr_path}"
            )

        waypoints: list[dict] = []
        lane_changes: list[dict] = []
        lane_adjustments: list[dict] = []
        total_length = 0.0
        for leg_index in range(len(snapped) - 1):
            leg_goal = snapped[leg_index + 1]
            leg_wps, leg_lcs, leg_len, arrived_lane = _plan_leg(
                rm, lib, snapped[leg_index], leg_goal, strategy_id
            )
            if arrived_lane != leg_goal["lane_id"]:
                # Report, never hide: the route is shorter but it does not end in
                # the lane that was clicked. Whether it ended on the OPPOSITE
                # carriageway is the part of that a map click cannot express and
                # the drawn line does not show at map zoom -- an adjacent lane is
                # a near-miss, the other side of the centre line is a different
                # destination, and both look like 3.75 m on screen.
                clicked_direction = lib.GetLaneDrivingDirection(
                    leg_goal["road_id"], leg_goal["lane_id"], leg_goal["s"]
                )
                arrived_direction = lib.GetLaneDrivingDirection(
                    leg_goal["road_id"], arrived_lane, leg_goal["s"]
                )
                lane_adjustments.append(
                    {
                        "index": leg_index + 1,
                        "road_id": leg_goal["road_id"],
                        "clicked_lane": leg_goal["lane_id"],
                        "arrived_lane": arrived_lane,
                        "opposite_direction": bool(
                            clicked_direction
                            and arrived_direction
                            and clicked_direction != arrived_direction
                        ),
                    }
                )
            if waypoints and leg_wps:
                # Seam: each leg reports its own start road, so leg N's last waypoint
                # and leg N+1's first describe the same place. Drop the duplicate,
                # but only when it really is the same road+lane -- a via point that
                # forced a lane change legitimately differs.
                prev = waypoints[-1]
                head = leg_wps[0]
                if (
                    prev["road_id"] == head["road_id"]
                    and prev["lane_id"] == head["lane_id"]
                ):
                    waypoints.pop()
            waypoints.extend(leg_wps)
            lane_changes.extend(leg_lcs)
            if leg_len > 0:
                total_length += leg_len

        # Drawn path. A separate handle: the snapping one above is already closed,
        # and sampling must happen inside this lock because it drives the same
        # process-global OpenDrive.
        path_handle = rm.CreatePosition()
        try:
            path = _sample_route_path(rm, lib, path_handle, waypoints, lane_changes)
        finally:
            rm.DeletePosition(path_handle)

    return {
        "waypoints": waypoints,
        "path": path,
        "lane_changes": lane_changes,
        "lane_adjustments": lane_adjustments,
        "length": total_length,
        "diagnostic": "ok",
        "snapped": snapped,
    }
