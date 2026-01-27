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
    contact_point: int = 1  # 1=START (enter at s=0), 2=END (enter at s=road_length)
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

    def __repr__(self) -> str:
        return f"RoadNode(road={self.road_id}, lane={self.lane_id}, s={self.s:.1f})"


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
        
    def _is_in_junction(self, wp: Waypoint) -> bool:
        """Check if a waypoint is located within a junction."""
        pos_data = self._get_road_position(wp)
        if pos_data is None:
            return False
        # junctionId != -1 (or uint32 max) means it's in a junction
        # esmini likely returns -1 for non-junctions but as uint32 it's 4294967295
        # Also check for 0 depending on implementation, but typically -1/max
        return pos_data.junctionId != 0xFFFFFFFF and pos_data.junctionId != -1

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
        if start_pos.roadId == target_pos.roadId:
            # Simple case: generate waypoints along the same road
            return self._generate_waypoints_same_road(
                start_pos, target_pos, waypoint_spacing
            )

        # Different roads: use A* pathfinding
        if self.gt_rm is None:
            # Fallback for when GT lib is not available - try straight line?
            # Or just return start/target?
            # print("[WARN] SimplifiedRouter: GT_esminiRMLib not available for cross-road routing")
            return [start, target]

        road_path = self._find_road_path(start_pos, target_pos)
        if not road_path:
            # print("[WARN] SimplifiedRouter: No path found between roads")
            return []

        # Generate waypoints along the road path
        return self._generate_waypoints_along_path(
            road_path, start_pos, target_pos, waypoint_spacing
        )

    def calculate_route_from_waypoints(self, current_pos: Waypoint, waypoints: List[Waypoint], 
                                      step_size: float = 1.0) -> List[Waypoint]:
        """
        Calculate a dense route from a list of sparse waypoints.
        
        Args:
            current_pos: Current vehicle position
            waypoints: List of sparse waypoints (from UDP)
            step_size: Spacing for dense waypoints (meters)
            
        Returns:
            List of dense waypoints
        """
        if not waypoints:
            return []
            
        if not waypoints:
            return []
            
        dense_route = []
        
        # Add current pos as start waypoints? Not really needed for dense route 
        # but let's see. logic is: current -> WP[0] -> ...
        
        # 1. Segment: Current -> WP[0]
        # Only if WP[0] is somewhat far away (> 5m), otherwise skip to avoid jitter
        if current_pos.distance_to(waypoints[0]) > 5.0:
            segment = self.calculate_path(current_pos, waypoints[0], step_size)
            dense_route.extend(segment[:-1]) # Exclude end to avoid duplicate with next start
        
        # 2. Iteratively process waypoints with lookahead
        i = 0
        while i < len(waypoints) - 1:
            start_wp = waypoints[i]
            
            # Check for lookahead possibility: can we skip WP[i+1]?
            # Only if WP[i+1] is in a junction (as requested)
            # and we have a WP[i+2]
            lookahead_success = False
            
            if i < len(waypoints) - 2:
                next_wp = waypoints[i+1]
                target_wp = waypoints[i+2]
                
                # Check if next_wp is in a junction
                if self._is_in_junction(next_wp):
                    # Try to calculate path directly from start_wp to target_wp
                    print(f"[DEBUG] calc_route: Trying lookahead across junction at WP[{i+1}]")
                    direct_segment = self.calculate_path(start_wp, target_wp, step_size)
                    
                    if direct_segment:
                        # Check if this path passes close to next_wp
                        # "Close" means distance < threshold (e.g. 10m)
                        min_dist = float('inf')
                        for p in direct_segment:
                            d = p.distance_to(next_wp)
                            if d < min_dist:
                                min_dist = d
                        
                        if min_dist < 2.0: # 2.0 meters threshold to avoid wrong lane selection
                            print(f"[DEBUG] calc_route: Lookahead successful! Skipped WP[{i+1}] (min_dist={min_dist:.1f}m)")
                            
                            if i < len(waypoints) - 3:
                                dense_route.extend(direct_segment[:-1])
                            else:
                                dense_route.extend(direct_segment) # Include end for very last segment
                            
                            i += 2 # Skip next_wp
                            lookahead_success = True
                        else:
                            print(f"[DEBUG] calc_route: Lookahead path too far from WP[{i+1}] (min_dist={min_dist:.1f}m)")
                    else:
                        print(f"[DEBUG] calc_route: Lookahead path calculation failed")
            
            if not lookahead_success:
                # Standard segment: WP[i] -> WP[i+1]
                end_wp = waypoints[i+1]
                
                segment = self.calculate_path(start_wp, end_wp, step_size)
                if segment:
                    if i < len(waypoints) - 2:
                        dense_route.extend(segment[:-1])
                    else:
                        dense_route.extend(segment) # Include end for last segment
                else:
                    # Fallback if pathfinding fails: just add the point
                    dense_route.append(start_wp)
                
                i += 1
        
        # Add last waypoint if not included
        if dense_route and dense_route[-1] != waypoints[-1]:
             dense_route.append(waypoints[-1])
             
        # If route is empty (e.g. single WP close by), just return WPs
        if not dense_route:
            return list(waypoints)
            
        return dense_route

    def _get_road_position(self, wp: Waypoint) -> Optional[RM_PositionData]:
        """Get road position data for a waypoint."""
        pos_handle = self.rm.CreatePosition()
        if pos_handle < 0:
            return None

        try:
            # If waypoint already has valid road coordinates, use them directly
            if wp.road_id >= 0 and wp.s >= 0 and wp.lane_id != 0:
                # Use SetLanePosition to get position data for the specified road
                res = self.rm.SetLanePosition(pos_handle, wp.road_id, wp.lane_id, 0.0, wp.s, True)
                if res >= 0:
                    res, pos_data = self.rm.GetPositionData(pos_handle)
                    if res >= 0:
                        return pos_data
                # If SetLanePosition fails, fall back to world coordinates

            # Use world coordinates as fallback
            res = self.rm.SetWorldXYHPosition(pos_handle, wp.x, wp.y, wp.h)
            if res < 0:
                return None

            res, pos_data = self.rm.GetPositionData(pos_handle)
            if res < 0:
                return None

            return pos_data
        finally:
            pass  # Position handle cleanup if needed

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
            print("[DEBUG] _find_road_path: GT_RM not available")
            return []

        start_road = start_pos.roadId
        target_road = target_pos.roadId
        start_lane = start_pos.laneId
        target_lane = target_pos.laneId

        print(f"[DEBUG] _find_road_path: Finding path from road {start_road} lane {start_lane} "
              f"to road {target_road} lane {target_lane}")

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

        iterations = 0
        max_iterations = 1000  # Prevent infinite loop

        while open_set and iterations < max_iterations:
            iterations += 1
            current = heapq.heappop(open_set)

            if current.road_id == target_road:
                # Found the target road
                # [FIX] Keep the driving lane (lane=-1 for right-hand traffic)
                # instead of switching to target_lane which may be on the wrong side
                # The target_lane from sparse waypoint is just a destination hint,
                # not the lane we should actually drive in
                # current.lane_id = target_lane  # REMOVED - don't switch lanes
                path = self._reconstruct_path(current)
                print(f"[DEBUG] _find_road_path: Found path with {len(path)} roads: {path}")
                return path

            node_key = (current.road_id, current.lane_id)
            if node_key in closed_set:
                continue
            closed_set.add(node_key)

            # Expand neighbors
            connected = self.gt_rm.get_connected_roads(current.road_id)
            if iterations == 1:
                print(f"[DEBUG] _find_road_path: Road {current.road_id} connected to: {connected}")
            for neighbor_road, connection_type, contact_point in connected:
                # [FIX] Lane polarity depends on contactPoint:
                # - contactPoint == 1 (START): entering at s=0, travel Start→End, lane -1 is right
                # - contactPoint == 2 (END): entering at s=length, travel End→Start, lane +1 is right
                # Flip lane polarity when entering at END
                if contact_point == 2:  # GT_RM_CONTACT_POINT_END
                    neighbor_lane = -current.lane_id  # Flip: -1 → +1 or +1 → -1
                else:
                    neighbor_lane = current.lane_id  # Keep same polarity

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
                    s=0.0,  # Start of next road (simplified, ideally depends on connection)
                    cost=new_cost,
                    heuristic=heuristic,
                    contact_point=contact_point,  # Track how we enter this road
                    parent=current
                )

                heapq.heappush(open_set, neighbor_node)


        print(f"[DEBUG] _find_road_path: No path found after {iterations} iterations")
        return []  # No path found

    def _reconstruct_path(self, end_node: RoadNode) -> List[Tuple[int, int, int]]:
        """Reconstruct path from end node back to start.
        
        Returns:
            List of (road_id, lane_id, contact_point) tuples
        """
        path = []
        current = end_node
        while current is not None:
            path.append((current.road_id, current.lane_id, current.contact_point))
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

        road_id = start_pos.roadId
        lane_id = start_pos.laneId

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
            # Use SetLanePosition to get correct position for each s value
            res = self.rm.SetLanePosition(pos_handle, road_id, lane_id, 0.0, s, True)
            if res >= 0:
                res, pos_data = self.rm.GetPositionData(pos_handle)
                if res >= 0:
                    waypoints.append(Waypoint(
                        x=pos_data.x,
                        y=pos_data.y,
                        h=pos_data.h,
                        road_id=road_id,
                        s=s,
                        lane_id=lane_id
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

        print(f"[DEBUG] _generate_waypoints_same_road: Generated {len(waypoints)} waypoints "
              f"from s={s_start:.1f} to s={s_end:.1f} on road {road_id}")

        return waypoints

    def _generate_waypoints_along_path(self, road_path: List[Tuple[int, int, int]],
                                        start_pos: RM_PositionData,
                                        target_pos: RM_PositionData,
                                        spacing: float) -> List[Waypoint]:
        """Generate waypoints along a multi-road path.
        
        Args:
            road_path: List of (road_id, lane_id, contact_point) tuples
        """
        waypoints = []

        # Create a position handle for coordinate conversion
        pos_handle = self.rm.CreatePosition()
        if pos_handle < 0:
            # Fallback to just start/end
            return [
                Waypoint(x=start_pos.x, y=start_pos.y, h=start_pos.h,
                        road_id=start_pos.roadId, s=start_pos.s, lane_id=start_pos.laneId),
                Waypoint(x=target_pos.x, y=target_pos.y, h=target_pos.h,
                        road_id=target_pos.roadId, s=target_pos.s, lane_id=target_pos.laneId)
            ]

        # 1. Generate waypoints along the FIRST road (from start_pos.s to road end)
        first_road_id, first_lane_id, first_contact_point = road_path[0]
        first_road_length = self._get_road_length(first_road_id)
        
        # Determine direction on first road (towards successor, so s increases)
        s = start_pos.s
        while s < first_road_length:
            res = self.rm.SetLanePosition(pos_handle, first_road_id, first_lane_id, 0.0, s, True)
            if res >= 0:
                res, pos_data = self.rm.GetPositionData(pos_handle)
                if res >= 0:
                    waypoints.append(Waypoint(
                        x=pos_data.x, y=pos_data.y, h=pos_data.h,
                        road_id=first_road_id, s=s, lane_id=first_lane_id
                    ))
            s += spacing

        # 2. Generate waypoints for intermediate roads (full length)
        for road_id, lane_id, contact_point in road_path[1:-1]:
            road_length = self._get_road_length(road_id)
            
            # contact_point determines where we enter the road
            # contact_point=1 (START): enter at s=0, travel toward s=road_length
            # contact_point=2 (END): enter at s=road_length, travel toward s=0
            if contact_point == 2:  # Enter at END
                s = road_length
                while s > 0:
                    res = self.rm.SetLanePosition(pos_handle, road_id, lane_id, 0.0, s, True)
                    if res >= 0:
                        res, pos_data = self.rm.GetPositionData(pos_handle)
                        if res >= 0:
                            waypoints.append(Waypoint(
                                x=pos_data.x, y=pos_data.y, h=pos_data.h,
                                road_id=road_id, s=s, lane_id=lane_id
                            ))
                    s -= spacing
            else:  # Enter at START
                s = 0.0
                while s < road_length:
                    res = self.rm.SetLanePosition(pos_handle, road_id, lane_id, 0.0, s, True)
                    if res >= 0:
                        res, pos_data = self.rm.GetPositionData(pos_handle)
                        if res >= 0:
                            waypoints.append(Waypoint(
                                x=pos_data.x, y=pos_data.y, h=pos_data.h,
                                road_id=road_id, s=s, lane_id=lane_id
                            ))
                    s += spacing

        # 3. Generate waypoints along the LAST road (from entry point to target_pos.s)
        if len(road_path) > 1:
            last_road_id, last_lane_id, last_contact_point = road_path[-1]
            last_road_length = self._get_road_length(last_road_id)
            
            # contact_point determines where we enter the last road
            if last_contact_point == 2:  # Enter at END (s=road_length), travel toward target_pos.s
                s = last_road_length
                while s > target_pos.s:
                    res = self.rm.SetLanePosition(pos_handle, last_road_id, last_lane_id, 0.0, s, True)
                    if res >= 0:
                        res, pos_data = self.rm.GetPositionData(pos_handle)
                        if res >= 0:
                            waypoints.append(Waypoint(
                                x=pos_data.x, y=pos_data.y, h=pos_data.h,
                                road_id=last_road_id, s=s, lane_id=last_lane_id
                            ))
                    s -= spacing
            else:  # Enter at START (s=0), travel toward target_pos.s
                s = 0.0
                while s < target_pos.s:
                    res = self.rm.SetLanePosition(pos_handle, last_road_id, last_lane_id, 0.0, s, True)
                    if res >= 0:
                        res, pos_data = self.rm.GetPositionData(pos_handle)
                        if res >= 0:
                            waypoints.append(Waypoint(
                                x=pos_data.x, y=pos_data.y, h=pos_data.h,
                                road_id=last_road_id, s=s, lane_id=last_lane_id
                            ))
                    s += spacing


        # Add final target waypoint
        waypoints.append(Waypoint(
            x=target_pos.x, y=target_pos.y, h=target_pos.h,
            road_id=target_pos.roadId, s=target_pos.s, lane_id=target_pos.laneId
        ))

        print(f"[DEBUG] _generate_waypoints_along_path: Generated {len(waypoints)} waypoints "
              f"along {len(road_path)} roads")
        # Print first and last waypoints of each road for debugging
        if waypoints and len(road_path) > 1:
            print(f"[DEBUG] First WP: ({waypoints[0].x:.1f}, {waypoints[0].y:.1f}) road={waypoints[0].road_id}")
            for i, wp in enumerate(waypoints):
                if i > 0 and wp.road_id != waypoints[i-1].road_id:
                    print(f"[DEBUG] Transition at WP[{i}]: ({wp.x:.1f}, {wp.y:.1f}) road={waypoints[i-1].road_id}->{wp.road_id}")
            print(f"[DEBUG] Last WP: ({waypoints[-1].x:.1f}, {waypoints[-1].y:.1f}) road={waypoints[-1].road_id}")

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
