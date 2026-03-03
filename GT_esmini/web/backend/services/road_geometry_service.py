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
_lock = threading.Lock()

# Cache: xodr absolute path → geometry dict
_cache: dict[str, dict] = {}

# Lane type bitmask constants (from RoadManager.hpp)
_LANE_TYPE_NONE = 1 << 0
_LANE_TYPE_DRIVING = 1 << 1
_LANE_TYPE_STOP = 1 << 2
_LANE_TYPE_SHOULDER = 1 << 3
_LANE_TYPE_BIKING = 1 << 4
_LANE_TYPE_SIDEWALK = 1 << 5
_LANE_TYPE_BORDER = 1 << 6
_LANE_TYPE_RESTRICTED = 1 << 7
_LANE_TYPE_PARKING = 1 << 8
_LANE_TYPE_CURB = 1 << 21

# Types considered "drivable" (lane dividers between these are lane_divider)
_DRIVABLE_MASK = (
    _LANE_TYPE_DRIVING | _LANE_TYPE_STOP | _LANE_TYPE_SHOULDER
    | _LANE_TYPE_BIKING | _LANE_TYPE_RESTRICTED | _LANE_TYPE_PARKING
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
        if outer_type & _LANE_TYPE_SIDEWALK:
            return "sidewalk_edge"
        return "road_edge"

    # Both sides are drivable → lane divider
    if (inner_type & _DRIVABLE_MASK) and (outer_type & _DRIVABLE_MASK):
        return "lane_divider"

    # Transition from drivable to non-drivable
    if (inner_type & _DRIVABLE_MASK) and not (outer_type & _DRIVABLE_MASK):
        if outer_type & _LANE_TYPE_SIDEWALK:
            return "sidewalk_edge"
        return "road_edge"

    return "lane_divider"


def extract_road_geometry(xodr_path: str | Path) -> dict:
    """Extract lane boundary polylines from an OpenDRIVE file.

    Returns a dict with "boundaries" list ready for JSON serialisation.
    Results are cached per absolute xodr path.
    """
    xodr_path = Path(xodr_path).resolve()
    cache_key = str(xodr_path)

    if cache_key in _cache:
        return _cache[cache_key]

    if not xodr_path.is_file():
        logger.warning("xodr file not found: %s", xodr_path)
        return {"boundaries": []}

    lib_path = str(ESMINI_RM_LIB)
    if not Path(lib_path).is_file():
        logger.warning("esminiRMLib not found: %s", lib_path)
        return {"boundaries": []}

    # Import lazily to avoid import errors when DLL is missing
    from realdriver.rm_lib import EsminiRMLib

    boundaries: list[dict] = []

    with _lock:
        try:
            rm = EsminiRMLib(lib_path)
            ret = rm.Init(str(xodr_path))
            if ret < 0:
                logger.error("esminiRMLib.Init failed for %s (ret=%d)", xodr_path, ret)
                return {"boundaries": []}

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

            rm.DeletePosition(pos_handle)
            rm.Close()

        except Exception:
            logger.exception("Failed to extract road geometry from %s", xodr_path)
            try:
                rm.Close()
            except Exception:
                pass
            return {"boundaries": []}

    result = {"boundaries": boundaries}
    _cache[cache_key] = result
    logger.info(
        "Extracted %d boundaries from %s", len(boundaries), xodr_path.name
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
        out_boundaries.append({
            "road_id": road_id,
            "side": side,
            "rank": rank,
            "type": boundary_types.get(key, "lane_divider"),
            "points": points,
        })
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
        if (len(left_lanes) != prev_left_count
                or len(right_lanes) != prev_right_count):
            _flush_segment(boundary_points, boundary_types, road_id,
                           out_boundaries)
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
            rm.SetLanePosition(
                pos_handle, road_id, lid, w / 2.0, s_val, True
            )
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
            rm.SetLanePosition(
                pos_handle, road_id, lid, -(w / 2.0), s_val, True
            )
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


def clear_cache() -> None:
    """Clear all cached road geometry."""
    _cache.clear()
