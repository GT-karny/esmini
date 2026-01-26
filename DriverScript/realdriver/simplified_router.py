"""
Simplified Router Module

This module provides Python-based route calculation using esminiRMLib
and GT_esminiRMLib for road connection queries.
"""

from typing import List, Optional, Dict, Set, Tuple
from dataclasses import dataclass
import heapq
import math

from .waypoint import Waypoint
from .rm_lib import EsminiRMLib, RM_PositionData
from .gt_rm_lib import GTEsminiRMLib, GT_RM_ELEMENT_TYPE_JUNCTION


@dataclass
class RoadNode:
    """Node in the road network graph for pathfinding."""
    road_id: int
    lane_id: int
    s: float  # Position along road
    cost: float = 0.0
    heuristic: float = 0.0
    parent: Optional['RoadNode'] = None

    @property
    def total_cost(self) -> float:
        return self.cost + self.heuristic

    def __lt__(self, other: 'RoadNode') -> bool:
        return self.total_cost < other.total_cost

    def __eq__(self, other: object) -> bool:
        if not isinstance(other, RoadNode):
            return False
        return (self.road_id == other.road_id and
                self.lane_id == other.lane_id and
                abs(self.s - other.s) < 1.0)

    def __hash__(self) -> int:
        return hash((self.road_id, self.lane_id, int(self.s)))


class SimplifiedRouter:
    """
    Python-based route calculation using esminiRMLib.

    This router uses A* pathfinding on the road network to find
    a path between two positions. It generates waypoints along
    the path for the ScenarioDriveController to follow.
    """

    def __init__(self, rm_lib: EsminiRMLib, gt_rm_lib: Optional[GTEsminiRMLib] = None):
        """
        Initialize the router.

        Args:
            rm_lib: EsminiRMLib instance (already initialized with map)
            gt_rm_lib: GTEsminiRMLib instance for road connection queries
        """
        self.rm = rm_lib
        self.gt_rm = gt_rm_lib
        self._road_cache: Dict[int, float] = {}  # road_id -> length

    def calculate_path(self, start: Waypoint, target: Waypoint,
                       waypoint_spacing: float = 20.0) -> List[Waypoint]:
        """
        Calculate a path from start to target position.

        Args:
            start: Starting waypoint (with road coordinates if known)
            target: Target waypoint
            waypoint_spacing: Distance between generated waypoints (meters)

        Returns:
            List of waypoints forming the path, or empty list if no path found
        """
        # Get road positions for start and target
        start_pos = self._get_road_position(start)
        target_pos = self._get_road_position(target)

        if start_pos is None or target_pos is None:
            print("[WARN] SimplifiedRouter: Could not get road position for start or target")
            return []

        # Check if start and target are on the same road
        if start_pos.road_id == target_pos.road_id:
            # Simple case: generate waypoints along the same road
            return self._generate_waypoints_same_road(
                start_pos, target_pos, waypoint_spacing
            )

        # Different roads: use A* pathfinding
        if self.gt_rm is None:
            print("[WARN] SimplifiedRouter: GT_esminiRMLib not available for cross-road routing")
            # Fallback: just return start and target
            return [start, target]

        road_path = self._find_road_path(start_pos, target_pos)
        if not road_path:
            print("[WARN] SimplifiedRouter: No path found between roads")
            return []

        # Generate waypoints along the road path
        return self._generate_waypoints_along_path(
            road_path, start_pos, target_pos, waypoint_spacing
        )

    def _get_road_position(self, wp: Waypoint) -> Optional[RM_PositionData]:
        """Get road position data for a waypoint."""
        if wp.road_id >= 0 and wp.s >= 0:
            # Already have road coordinates, just need to verify
            pos_handle = self.rm.CreatePosition()
            if pos_handle < 0:
                return None

            try:
                # Set position using road coordinates
                # Note: This would require RM_SetLanePosition which may not be available
                # For now, use world coordinates
                res = self.rm.SetWorldXYHPosition(pos_handle, wp.x, wp.y, wp.h)
                if res < 0:
                    return None

                res, pos_data = self.rm.GetPositionData(pos_handle)
                if res < 0:
                    return None

                return pos_data
            finally:
                pass  # Position handle cleanup if needed

        # Use world coordinates
        pos_handle = self.rm.CreatePosition()
        if pos_handle < 0:
            return None

        try:
            res = self.rm.SetWorldXYHPosition(pos_handle, wp.x, wp.y, wp.h)
            if res < 0:
                return None

            res, pos_data = self.rm.GetPositionData(pos_handle)
            if res < 0:
                return None

            return pos_data
        finally:
            pass

    def _get_road_length(self, road_id: int) -> float:
        """Get cached road length."""
        if road_id not in self._road_cache:
            if self.gt_rm:
                self._road_cache[road_id] = self.gt_rm.get_road_length(road_id)
            else:
                self._road_cache[road_id] = 100.0  # Default fallback
        return self._road_cache[road_id]

    def _find_road_path(self, start_pos: RM_PositionData,
                        target_pos: RM_PositionData) -> List[Tuple[int, int]]:
        """
        Find a path of roads from start to target using A*.

        Returns:
            List of (road_id, lane_id) tuples forming the path
        """
        if self.gt_rm is None:
            return []

        start_road = start_pos.roadId
        target_road = target_pos.roadId
        start_lane = start_pos.laneId
        target_lane = target_pos.laneId

        # A* pathfinding
        open_set: List[RoadNode] = []
        closed_set: Set[Tuple[int, int]] = set()

        start_node = RoadNode(
            road_id=start_road,
            lane_id=start_lane,
            s=start_pos.s,
            cost=0.0,
            heuristic=self._estimate_distance(start_pos, target_pos)
        )

        heapq.heappush(open_set, start_node)

        while open_set:
            current = heapq.heappop(open_set)

            if current.road_id == target_road:
                # Found the target road
                return self._reconstruct_path(current)

            node_key = (current.road_id, current.lane_id)
            if node_key in closed_set:
                continue
            closed_set.add(node_key)

            # Expand neighbors
            for neighbor_road, connection_type in self.gt_rm.get_connected_roads(current.road_id):
                neighbor_lane = current.lane_id  # Assume same lane continues

                neighbor_key = (neighbor_road, neighbor_lane)
                if neighbor_key in closed_set:
                    continue

                # Calculate cost
                road_length = self._get_road_length(neighbor_road)
                new_cost = current.cost + road_length

                # Heuristic: straight-line distance to target
                # (simplified - would need position lookup for accuracy)
                heuristic = road_length  # Simplified heuristic

                neighbor_node = RoadNode(
                    road_id=neighbor_road,
                    lane_id=neighbor_lane,
                    s=0.0,  # Start of next road
                    cost=new_cost,
                    heuristic=heuristic,
                    parent=current
                )

                heapq.heappush(open_set, neighbor_node)

        return []  # No path found

    def _reconstruct_path(self, end_node: RoadNode) -> List[Tuple[int, int]]:
        """Reconstruct path from end node back to start."""
        path = []
        current = end_node
        while current is not None:
            path.append((current.road_id, current.lane_id))
            current = current.parent
        path.reverse()
        return path

    def _estimate_distance(self, pos1: RM_PositionData, pos2: RM_PositionData) -> float:
        """Estimate distance between two positions."""
        dx = pos2.x - pos1.x
        dy = pos2.y - pos1.y
        return math.sqrt(dx * dx + dy * dy)

    def _generate_waypoints_same_road(self, start_pos: RM_PositionData,
                                       target_pos: RM_PositionData,
                                       spacing: float) -> List[Waypoint]:
        """Generate waypoints along a single road."""
        waypoints = []

        # Determine direction
        if target_pos.s > start_pos.s:
            direction = 1
            s_start = start_pos.s
            s_end = target_pos.s
        else:
            direction = -1
            s_start = start_pos.s
            s_end = target_pos.s

        # Generate waypoints
        pos_handle = self.rm.CreatePosition()
        if pos_handle < 0:
            return [Waypoint(x=target_pos.x, y=target_pos.y, h=target_pos.h,
                            road_id=target_pos.roadId, s=target_pos.s,
                            lane_id=target_pos.laneId)]

        s = s_start
        while (direction > 0 and s < s_end) or (direction < 0 and s > s_end):
            # Move position to this s value
            res = self.rm.SetWorldXYHPosition(pos_handle, start_pos.x, start_pos.y, start_pos.h)
            if res >= 0:
                res, pos_data = self.rm.GetPositionData(pos_handle)
                if res >= 0:
                    waypoints.append(Waypoint(
                        x=pos_data.x,
                        y=pos_data.y,
                        h=pos_data.h,
                        road_id=pos_data.roadId,
                        s=pos_data.s,
                        lane_id=pos_data.laneId
                    ))

            s += direction * spacing

        # Add final target waypoint
        waypoints.append(Waypoint(
            x=target_pos.x,
            y=target_pos.y,
            h=target_pos.h,
            road_id=target_pos.roadId,
            s=target_pos.s,
            lane_id=target_pos.laneId
        ))

        return waypoints

    def _generate_waypoints_along_path(self, road_path: List[Tuple[int, int]],
                                        start_pos: RM_PositionData,
                                        target_pos: RM_PositionData,
                                        spacing: float) -> List[Waypoint]:
        """Generate waypoints along a multi-road path."""
        waypoints = []

        # Start waypoint
        waypoints.append(Waypoint(
            x=start_pos.x,
            y=start_pos.y,
            h=start_pos.h,
            road_id=start_pos.roadId,
            s=start_pos.s,
            lane_id=start_pos.laneId
        ))

        # Generate waypoints for intermediate roads
        for i, (road_id, lane_id) in enumerate(road_path[1:-1], start=1):
            road_length = self._get_road_length(road_id)

            # Generate waypoints along this road
            num_waypoints = max(1, int(road_length / spacing))
            for j in range(1, num_waypoints + 1):
                s = (j / num_waypoints) * road_length
                # Note: We would need to use RM_SetLanePosition to get accurate world coords
                # For now, we just store road coordinates
                waypoints.append(Waypoint(
                    x=0.0,  # Would need lookup
                    y=0.0,
                    h=0.0,
                    road_id=road_id,
                    s=s,
                    lane_id=lane_id
                ))

        # End waypoint
        waypoints.append(Waypoint(
            x=target_pos.x,
            y=target_pos.y,
            h=target_pos.h,
            road_id=target_pos.roadId,
            s=target_pos.s,
            lane_id=target_pos.laneId
        ))

        return waypoints

    def get_next_roads(self, road_id: int, direction: str = 'successor') -> List[int]:
        """
        Get the next roads from a given road.

        Args:
            road_id: Current road ID
            direction: 'successor' or 'predecessor'

        Returns:
            List of connected road IDs
        """
        if self.gt_rm is None:
            return []

        connected = self.gt_rm.get_connected_roads(road_id, direction)
        return [road_id for road_id, _ in connected]
