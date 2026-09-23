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
from pathlib import Path

from GT_esmini.web.backend.config import ESMINI_RM_LIB, GT_ESMINI_LIB

# The SAME lock road_geometry_service takes. ctypes.CDLL hands back one loaded module
# per path, so both services drive a single process-global OpenDrive; separate locks
# would let one Init() another's map out from under it mid-request.
from GT_esmini.web.backend.services.road_geometry_service import ESMINI_RM_LOCK

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
        if rm.Init(str(xodr_path)) < 0:
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

    So each leg's GOAL lane is relaxed to any drivable lane of that road, shortest
    wins, and the lane actually arrived in is reported back so the UI can say so.
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
# A lane narrower than this is a taper stub, not somewhere a car sits. Used to
# decide WHERE along a road a lane change gets drawn (see _lane_change_windows).
_LANE_MIN_USABLE_M = 2.0
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


def _lane_change_windows(rm, road_id, chain, start, end):
    """Place each lane change of one road along the travelled span.

    A change is drawn where the target lane is actually there to move into: a
    turn pocket that opens 50 m before a junction is entered where it opens,
    not at the far end of the road where its width is still zero. Widths are
    read from the road, so this needs no per-map tuning.

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
        widest = max((w for _, w in widths), default=0.0)
        # Half of the lane's own maximum, so a uniform lane opens immediately
        # and a taper opens halfway up. The floor keeps a lane that is narrow
        # everywhere from being declared absent.
        threshold = max(0.5, min(_LANE_MIN_USABLE_M, 0.5 * widest))
        s_open = cursor
        for s, w in widths:
            if (s - cursor) * direction < 0:
                continue
            if w >= threshold:
                s_open = s
                break
        s_done = s_open + direction * _LANE_CHANGE_BLEND_M
        if (s_done - end) * direction > 0:
            s_done = end
        windows.append((s_open, s_done, lane_a, lane_b))
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
        windows = _lane_change_windows(rm, road_id, chain, start, end)
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
            for s_open, s_done, lane_a, lane_b in windows:
                if (s_val - s_open) * travel < 0:
                    break
                if (s_val - s_done) * travel >= 0:
                    lane_here = lane_b
                    continue
                lane_here = lane_a
                blend_to = lane_b
                frac = (s_val - s_open) / (s_done - s_open) if s_done != s_open else 1.0
                break

            point = _lane_centre_xy(rm, pos_handle, road_id, lane_here, s_val)
            if point is None:
                continue
            if blend_to is not None:
                target = _lane_centre_xy(rm, pos_handle, road_id, blend_to, s_val)
                if target is not None:
                    point = {
                        "x": point["x"] + (target["x"] - point["x"]) * frac,
                        "y": point["y"] + (target["y"] - point["y"]) * frac,
                    }
            path.append(point)
    return path


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
        if rm.Init(str(xodr_path)) < 0:
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
        if lib.Init(str(xodr_path)) != 0:
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
                # the lane that was clicked.
                lane_adjustments.append(
                    {
                        "index": leg_index + 1,
                        "road_id": leg_goal["road_id"],
                        "clicked_lane": leg_goal["lane_id"],
                        "arrived_lane": arrived_lane,
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
