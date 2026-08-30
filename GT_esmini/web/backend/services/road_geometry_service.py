"""Road geometry extraction service using esminiRMLib.

Loads an OpenDRIVE (.xodr) file via esminiRMLib and samples lane boundary
positions to produce polylines suitable for 2D SVG rendering.
"""

from __future__ import annotations

import logging
import threading
from pathlib import Path

from GT_esmini.web.backend.config import ESMINI_RM_LIB

logger = logging.getLogger(__name__)

# esminiRMLib is a global singleton — only one xodr at a time.
#
# PUBLIC on purpose: ctypes.CDLL returns the SAME loaded module for a given path, so
# every service that constructs EsminiRMLib in this process shares one OpenDrive.
# Two of them Init()-ing different xodr files concurrently would silently answer from
# the wrong map. Any new caller must take THIS lock, not one of its own —
# route_planner_service does.
ESMINI_RM_LOCK = threading.Lock()
_lock = ESMINI_RM_LOCK  # backward-compatible alias for existing call sites

# Cache: xodr absolute path → geometry dict
_cache: dict[str, dict] = {}

# Lane type bitmask constants -- values mirror RoadManager.hpp Lane::LaneType.
# MUST stay in sync with the C++ enum; machine-checked by ctest OdrLaneTypeSync
# (GT_esmini/test/unit/road/test_OdrLaneTypeSync.cpp) -- plan P2.
_LANE_TYPE_NONE = 1 << 0
_LANE_TYPE_DRIVING = 1 << 1
_LANE_TYPE_STOP = 1 << 2
_LANE_TYPE_SHOULDER = 1 << 3
_LANE_TYPE_BIKING = 1 << 4
_LANE_TYPE_SIDEWALK = 1 << 5
_LANE_TYPE_BORDER = 1 << 6
_LANE_TYPE_RESTRICTED = 1 << 7
_LANE_TYPE_PARKING = 1 << 8
_LANE_TYPE_BIDIRECTIONAL = 1 << 9
_LANE_TYPE_CURB = 1 << 21
_LANE_TYPE_CONNECTING_RAMP = 1 << 22

# Types considered "drivable" (lane dividers between these are lane_divider).
# BIDIRECTIONAL/CONNECTING_RAMP cover ODR "shared"/"slipLane" lanes, which the
# [GT_ODR:lane-types] fork patch maps onto these enums (plan P2).
_DRIVABLE_MASK = (
    _LANE_TYPE_DRIVING
    | _LANE_TYPE_STOP
    | _LANE_TYPE_SHOULDER
    | _LANE_TYPE_BIKING
    | _LANE_TYPE_RESTRICTED
    | _LANE_TYPE_PARKING
    | _LANE_TYPE_BIDIRECTIONAL
    | _LANE_TYPE_CONNECTING_RAMP
)

# Sampling step in meters along the road
_SAMPLE_STEP = 2.0


def _classify_boundary(inner_type: int, outer_type: int, is_outermost: bool) -> str:
    """Classify a lane boundary based on adjacent lane types.

    Returns one of: "center_line", "lane_divider", "road_edge", "sidewalk_edge".
    """
    if inner_type == _LANE_TYPE_NONE:
        # boundary at center reference line
        return "center_line"

    if is_outermost:
        # Curb renders like a sidewalk edge in the 2D viewer
        if outer_type & (_LANE_TYPE_SIDEWALK | _LANE_TYPE_CURB):
            return "sidewalk_edge"
        return "road_edge"

    # Both sides are drivable → lane divider
    if (inner_type & _DRIVABLE_MASK) and (outer_type & _DRIVABLE_MASK):
        return "lane_divider"

    # Transition from drivable to non-drivable
    if (inner_type & _DRIVABLE_MASK) and not (outer_type & _DRIVABLE_MASK):
        if outer_type & (_LANE_TYPE_SIDEWALK | _LANE_TYPE_CURB):
            return "sidewalk_edge"
        return "road_edge"

    return "lane_divider"


def _empty_geometry() -> dict:
    return {"boundaries": [], "signs": [], "stop_lines": []}


def extract_road_geometry(xodr_path: str | Path) -> dict:
    """Extract lane boundary polylines + signs + stop lines from an OpenDRIVE file.

    Returns a dict with "boundaries", "signs" and "stop_lines" lists ready for
    JSON serialisation. Results are cached per absolute xodr path.

    Signs and stop lines are sourced here (not from the OSI stream) because the
    OSI GroundTruth re-emits traffic signs only on the first frame, so a late WS
    subscriber would miss them. This REST path is deterministic and timing-safe.
    Traffic-light *phase* still comes live from the OSI WS stream.
    """
    xodr_path = Path(xodr_path).resolve()
    cache_key = str(xodr_path)

    if cache_key in _cache:
        return _cache[cache_key]

    if not xodr_path.is_file():
        logger.warning("xodr file not found: %s", xodr_path)
        return _empty_geometry()

    lib_path = str(ESMINI_RM_LIB)
    if not Path(lib_path).is_file():
        logger.warning("esminiRMLib not found: %s", lib_path)
        return _empty_geometry()

    # Import lazily to avoid import errors when DLL is missing.
    # GT_SCRIPTS_DIR (GT_esmini/scripts/) is on sys.path via config.py; rm_lib.py
    # lives there now (relocated from DriverScript/realdriver/ -- audit SCR-7).
    from rm_lib import EsminiRMLib

    boundaries: list[dict] = []
    signs: list[dict] = []
    stop_lines: list[dict] = []

    with _lock:
        try:
            rm = EsminiRMLib(lib_path)
            ret = rm.Init(str(xodr_path))
            if ret < 0:
                logger.error("esminiRMLib.Init failed for %s (ret=%d)", xodr_path, ret)
                return _empty_geometry()

            pos_handle = rm.CreatePosition()
            num_roads = rm.GetNumberOfRoads()

            for road_idx in range(num_roads):
                road_id = rm.GetIdOfRoadFromIndex(road_idx)
                road_length = rm.GetRoadLength(road_id)
                if road_length <= 0:
                    continue

                _extract_road_boundaries(
                    rm, pos_handle, road_id, road_length, boundaries
                )
                _extract_road_signs(rm, pos_handle, road_id, signs, stop_lines)

            rm.DeletePosition(pos_handle)
            rm.Close()

        except Exception:
            logger.exception("Failed to extract road geometry from %s", xodr_path)
            try:
                rm.Close()
            except Exception:
                pass
            return _empty_geometry()

    result = {"boundaries": boundaries, "signs": signs, "stop_lines": stop_lines}
    _cache[cache_key] = result
    logger.info(
        "Extracted %d boundaries, %d signs, %d stop lines from %s",
        len(boundaries),
        len(signs),
        len(stop_lines),
        xodr_path.name,
    )
    return result


def _flush_segment(
    boundary_points: dict[tuple[str, int], list[list[float]]],
    boundary_types: dict[tuple[str, int], str],
    road_id: int,
    out_boundaries: list[dict],
) -> None:
    """Write accumulated boundary points as polylines and clear the dicts."""
    for key, points in boundary_points.items():
        if len(points) < 2:
            continue
        side, rank = key
        out_boundaries.append(
            {
                "road_id": road_id,
                "side": side,
                "rank": rank,
                "type": boundary_types.get(key, "lane_divider"),
                "points": points,
            }
        )
    boundary_points.clear()
    boundary_types.clear()


def _extract_road_boundaries(
    rm,
    pos_handle: int,
    road_id: int,
    road_length: float,
    out_boundaries: list[dict],
) -> None:
    """Sample lane boundaries for a single road and append to out_boundaries.

    Uses SetLanePosition instead of SetRoadPosition so that OpenDRIVE
    <laneOffset> is correctly accounted for at every sample point.
    """
    # Generate s-values
    s_values: list[float] = []
    s = 0.0
    while s < road_length:
        s_values.append(s)
        s += _SAMPLE_STEP
    if s_values[-1] < road_length:
        s_values.append(road_length)

    # Collect boundary points, keyed by (side, boundary_rank).
    # When lane topology changes (number of left/right lanes differs from
    # previous sample), flush accumulated polylines and start fresh so that
    # boundaries belonging to different lane sections are not merged.
    boundary_points: dict[tuple[str, int], list[list[float]]] = {}
    boundary_types: dict[tuple[str, int], str] = {}
    prev_left_count = -1
    prev_right_count = -1

    for s_val in s_values:
        # Get all lane IDs at this s
        num_lanes = rm.GetRoadNumberOfLanes(road_id, s_val, -1)
        lane_ids: list[int] = []
        for i in range(num_lanes):
            res, lid = rm.GetLaneIdByIndex(road_id, i, s_val, -1)
            if res == 0:
                lane_ids.append(lid)

        left_lanes = sorted([l for l in lane_ids if l > 0])
        right_lanes = sorted([l for l in lane_ids if l < 0], reverse=True)

        # Detect lane topology change → flush current segment
        if len(left_lanes) != prev_left_count or len(right_lanes) != prev_right_count:
            _flush_segment(boundary_points, boundary_types, road_id, out_boundaries)
        prev_left_count = len(left_lanes)
        prev_right_count = len(right_lanes)

        # --- Center line (lane 0 center, accounts for laneOffset) ---
        rm.SetLanePosition(pos_handle, road_id, 0, 0.0, s_val, True)
        res, data = rm.GetPositionData(pos_handle)
        if res == 0:
            key = ("center", 0)
            boundary_points.setdefault(key, []).append(
                [round(data.x, 2), round(data.y, 2)]
            )
            boundary_types[key] = "center_line"

        # --- Left side boundaries ---
        # For each left lane, place at outer edge: lane center + width/2
        for rank, lid in enumerate(left_lanes):
            _, w = rm.GetLaneWidthByRoadId(road_id, lid, s_val)
            rm.SetLanePosition(pos_handle, road_id, lid, w / 2.0, s_val, True)
            res, data = rm.GetPositionData(pos_handle)
            if res != 0:
                continue

            key = ("left", rank)
            boundary_points.setdefault(key, []).append(
                [round(data.x, 2), round(data.y, 2)]
            )

            # Classify this boundary
            if key not in boundary_types:
                inner_type = rm.GetLaneTypeByRoadId(road_id, lid, s_val)
                is_outermost = rank == len(left_lanes) - 1
                if is_outermost:
                    outer_type = 0
                else:
                    outer_type = rm.GetLaneTypeByRoadId(
                        road_id, left_lanes[rank + 1], s_val
                    )
                boundary_types[key] = _classify_boundary(
                    inner_type, outer_type, is_outermost
                )

        # --- Right side boundaries ---
        # For each right lane, place at outer edge: lane center - width/2
        for rank, lid in enumerate(right_lanes):
            _, w = rm.GetLaneWidthByRoadId(road_id, lid, s_val)
            rm.SetLanePosition(pos_handle, road_id, lid, -(w / 2.0), s_val, True)
            res, data = rm.GetPositionData(pos_handle)
            if res != 0:
                continue

            key = ("right", rank)
            boundary_points.setdefault(key, []).append(
                [round(data.x, 2), round(data.y, 2)]
            )

            if key not in boundary_types:
                inner_type = rm.GetLaneTypeByRoadId(road_id, lid, s_val)
                is_outermost = rank == len(right_lanes) - 1
                if is_outermost:
                    outer_type = 0
                else:
                    outer_type = rm.GetLaneTypeByRoadId(
                        road_id, right_lanes[rank + 1], s_val
                    )
                boundary_types[key] = _classify_boundary(
                    inner_type, outer_type, is_outermost
                )

    # Flush remaining segment
    _flush_segment(boundary_points, boundary_types, road_id, out_boundaries)


def _extract_road_signs(
    rm,
    pos_handle: int,
    road_id: int,
    out_signs: list[dict],
    out_stop_lines: list[dict],
) -> None:
    """Enumerate OpenDRIVE signals/signs on a road → marker + stop-line polyline.

    RM_RoadSign carries position/heading/dims and a `name` (OpenDRIVE signal
    name, usually the 3D-model key) but not the OSI type. We surface name +
    geometry; the frontend does best-effort classification from the name. For
    each sign a stop line is derived across its valid approach lanes at the
    sign's s. Per-sign failures are swallowed so one bad sign can't abort the
    whole extraction.
    """
    try:
        num_signs = rm.GetNumberOfRoadSigns(road_id)
    except Exception:
        return

    for i in range(num_signs):
        try:
            res, sign = rm.GetRoadSign(road_id, i)
            if res != 0:
                continue

            name = sign.name.decode("utf-8", "replace") if sign.name else ""
            out_signs.append(
                {
                    "id": sign.id,
                    "road_id": road_id,
                    "x": round(sign.x, 2),
                    "y": round(sign.y, 2),
                    "z": round(sign.z, 2),
                    "h": round(sign.h, 4),
                    "s": round(sign.s, 2),
                    "t": round(sign.t, 2),
                    "name": name,
                    "orientation": sign.orientation,
                    "height": round(sign.height, 2),
                    "width": round(sign.width, 2),
                }
            )

            # Valid lanes from validity records (fallback: lanes on the sign's side).
            valid: set[int] = set()
            try:
                nv = rm.GetNumberOfRoadSignValidityRecords(road_id, i)
                for vi in range(nv):
                    vres, val = rm.GetRoadSignValidityRecord(road_id, i, vi)
                    if vres == 0:
                        lo, hi = sorted((val.fromLane, val.toLane))
                        valid.update(range(lo, hi + 1))
                valid.discard(0)
            except Exception:
                valid = set()

            side = -1 if sign.t < 0 else 1
            pts = _stop_line_points(
                rm, pos_handle, road_id, sign.s, valid or None, side
            )
            if pts:
                out_stop_lines.append(
                    {
                        "road_id": road_id,
                        "sign_id": sign.id,
                        "points": pts,
                    }
                )
        except Exception:
            logger.debug(
                "sign extraction failed (road=%s idx=%s)", road_id, i, exc_info=True
            )
            continue


def _stop_line_points(
    rm,
    pos_handle: int,
    road_id: int,
    s: float,
    valid_lanes: set[int] | None,
    side: int,
) -> list[list[float]] | None:
    """Build a 2-point stop line across the approach lanes at (road_id, s).

    Endpoints: the road reference (lane 0) centre and the outer edge of the
    outermost approach lane — a perpendicular segment spanning the lanes the
    sign governs. Curvature within a single s is negligible, so 2 points suffice.
    """
    num = rm.GetRoadNumberOfLanes(road_id, s, -1)
    present: list[int] = []
    for i in range(num):
        res, lid = rm.GetLaneIdByIndex(road_id, i, s, -1)
        if res == 0 and lid != 0:
            present.append(lid)
    if not present:
        return None

    if valid_lanes:
        lanes = [l for l in present if l in valid_lanes]
    else:
        lanes = [l for l in present if (l > 0) == (side > 0)]
    if not lanes:
        lanes = present

    outer = max(lanes, key=lambda l: abs(l))

    pts: list[list[float]] = []

    rm.SetLanePosition(pos_handle, road_id, 0, 0.0, s, True)
    res, data = rm.GetPositionData(pos_handle)
    if res == 0:
        pts.append([round(data.x, 2), round(data.y, 2)])

    _, w = rm.GetLaneWidthByRoadId(road_id, outer, s)
    edge_off = (w / 2.0) if outer > 0 else -(w / 2.0)
    rm.SetLanePosition(pos_handle, road_id, outer, edge_off, s, True)
    res, data = rm.GetPositionData(pos_handle)
    if res == 0:
        pts.append([round(data.x, 2), round(data.y, 2)])

    return pts if len(pts) == 2 else None


def clear_cache() -> None:
    """Clear all cached road geometry."""
    _cache.clear()
