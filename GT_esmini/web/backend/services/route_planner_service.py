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


def _snap_points(rm, pos_handle, points: list[dict]) -> list[dict]:
    """Snap world (x, y) clicks onto lanes -> [{road_id, lane_id, s, x, y, h}].

    Raises RoutePlanError when a point lands nowhere routable, naming WHICH point --
    "no route" and "you clicked on grass" are different failures and the UI needs to
    say different things about them.
    """
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


def _plan_leg(lib, start: dict, goal: dict, strategy: int) -> tuple[list, list, float]:
    """Route one start->goal leg. Returns (waypoints, lane_changes, length)."""
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
        )

    rc = lib.CalcRouteInDrivingDirection(
        start["road_id"],
        start["lane_id"],
        start["s"],
        goal["road_id"],
        goal["lane_id"],
        goal["s"],
        strategy,
    )
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
    return lib.GetRouteWaypoints(), lib.GetLaneChanges(), lib.GetRouteLength()


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
        total_length = 0.0
        for leg_index in range(len(snapped) - 1):
            leg_wps, leg_lcs, leg_len = _plan_leg(
                lib, snapped[leg_index], snapped[leg_index + 1], strategy_id
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

    return {
        "waypoints": waypoints,
        "lane_changes": lane_changes,
        "length": total_length,
        "diagnostic": "ok",
        "snapped": snapped,
    }
