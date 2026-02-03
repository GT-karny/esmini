"""
Waypoint Module

This module provides Waypoint data structures and WaypointManager for
managing waypoints from multiple sources (user-specified, calculated, UDP).
"""

from dataclasses import dataclass, field
from typing import List, Optional, Tuple
from enum import Enum
import struct
import math


class WaypointStatus(Enum):
    """Status of a waypoint relative to current vehicle position."""
    NOT_REACHED = 0
    PASSED = 1
    MISSED = 2


@dataclass
class Waypoint:
    """
    Represents a waypoint in both world and road coordinates.

    Attributes:
        x: World X coordinate (meters)
        y: World Y coordinate (meters)
        h: Heading in radians (optional)
        road_id: OpenDRIVE road ID (-1 if unknown)
        s: Road s-coordinate (meters along road)
        lane_id: Lane ID (negative = right side, positive = left side)
        lane_offset: Offset from lane center (meters, positive = left)
    """
    x: float
    y: float
    h: float = 0.0
    road_id: int = -1
    s: float = 0.0
    lane_id: int = 0
    lane_offset: float = 0.0

    def distance_to(self, other: 'Waypoint') -> float:
        """Calculate Euclidean distance to another waypoint."""
        dx = self.x - other.x
        dy = self.y - other.y
        return math.sqrt(dx * dx + dy * dy)

    def heading_to(self, other: 'Waypoint') -> float:
        """Calculate heading angle to another waypoint (radians)."""
        dx = other.x - self.x
        dy = other.y - self.y
        return math.atan2(dy, dx)

    @classmethod
    def from_world_coords(cls, x: float, y: float, h: float = 0.0) -> 'Waypoint':
        """Create a waypoint from world coordinates only."""
        return cls(x=x, y=y, h=h, road_id=-1, s=0.0, lane_id=0)

    @classmethod
    def from_road_coords(cls, road_id: int, s: float, lane_id: int,
                         x: float = 0.0, y: float = 0.0, h: float = 0.0) -> 'Waypoint':
        """Create a waypoint from road coordinates (with optional world coords)."""
        return cls(x=x, y=y, h=h, road_id=road_id, s=s, lane_id=lane_id)


# Waypoint packet format for UDP
# [Type: uint8 = 2][CurrentIndex: uint32][Count: uint32][Waypoints...]
# Each waypoint: [x: double][y: double][h: double][roadId: uint32][PADDING: 4b][s: double][laneId: int32][PADDING: 4b][laneOffset: double]
# Note: C++ struct alignment inserts padding before doubles if previous members are 4-byte aligned.
WAYPOINT_PACKET_TYPE = 2
WAYPOINT_STRUCT_FORMAT = '<dddI4xdi4xd'  # x, y, h, roadId, pad, s, laneId, pad, laneOffset
WAYPOINT_STRUCT_SIZE = 56  # 48 + 8 bytes padding

def parse_waypoints_from_udp(data: bytes) -> Tuple[int, List[Waypoint]]:
    """
    Parse waypoints from UDP packet.

    Args:
        data: Raw UDP packet data

    Returns:
        Tuple of (current_waypoint_index, list of waypoints)

    Raises:
        ValueError: If packet format is invalid
    """
    if len(data) < 9:  # Minimum: type(1) + index(4) + count(4)
        raise ValueError(f"Packet too small: {len(data)} bytes")

    packet_type = data[0]
    if packet_type != WAYPOINT_PACKET_TYPE:
        raise ValueError(f"Invalid packet type: {packet_type}, expected {WAYPOINT_PACKET_TYPE}")

    current_index, count = struct.unpack('<II', data[1:9])

    expected_size = 9 + count * WAYPOINT_STRUCT_SIZE
    if len(data) < expected_size:
        raise ValueError(f"Packet size mismatch: got {len(data)}, expected {expected_size}")

    waypoints = []
    offset = 9
    for _ in range(count):
        # Only unpack the fields we need, ignore padding
        # Note: struct.unpack with 'x' skips bytes
        x, y, h, road_id, s, lane_id, lane_offset = struct.unpack(
            WAYPOINT_STRUCT_FORMAT,
            data[offset:offset + WAYPOINT_STRUCT_SIZE]
        )
        waypoints.append(Waypoint(
            x=x, y=y, h=h,
            road_id=road_id, s=s, lane_id=lane_id, lane_offset=lane_offset
        ))
        offset += WAYPOINT_STRUCT_SIZE

    return current_index, waypoints


class WaypointManager:
    """
    Manages waypoints from multiple sources.

    Priority order:
    1. User-specified waypoints (set via set_waypoints)
    2. Calculated waypoints (from SimplifiedRouter)
    3. UDP-received waypoints (from C++ ControllerRealDriver)
    """

    def __init__(self):
        self._user_waypoints: List[Waypoint] = []
        self._calculated_waypoints: List[Waypoint] = []
        self._udp_waypoints: List[Waypoint] = []
        self._current_index: int = 0
        self._source: str = 'none'

    def set_waypoints(self, waypoints: List[Waypoint]) -> None:
        """
        Set user-specified waypoints (highest priority).

        Args:
            waypoints: List of waypoints to follow
        """
        self._user_waypoints = list(waypoints)
        self._current_index = 0
        self._source = 'user'

    def set_calculated_waypoints(self, waypoints: List[Waypoint]) -> None:
        """
        Set calculated waypoints (from SimplifiedRouter).

        Args:
            waypoints: List of waypoints calculated by router
        """
        self._calculated_waypoints = list(waypoints)
        if not self._user_waypoints:
            self._current_index = 0
            self._source = 'calculated'

    def receive_from_udp(self, data: bytes) -> bool:
        """
        Parse and store waypoints from UDP packet.

        Args:
            data: Raw UDP packet data

        Returns:
            True if parsing succeeded, False otherwise
        """
        try:
            index, waypoints = parse_waypoints_from_udp(data)
            self._udp_waypoints = waypoints
            if not self._user_waypoints and not self._calculated_waypoints:
                self._current_index = index
                self._source = 'udp'

            # Debug: Log received waypoints (first time only)
            if not hasattr(self, '_udp_logged') or not self._udp_logged:
                print(f"[UDP] Received {len(waypoints)} waypoints, currentIndex={index}")
                for i, wp in enumerate(waypoints):
                    marker = ">>>" if i == index else "   "
                    print(f"  {marker}WP[{i}]: x={wp.x:.2f}, y={wp.y:.2f}, road={wp.road_id}, lane={wp.lane_id}")
                self._udp_logged = True

            return True
        except ValueError as e:
            print(f"[WARN] WaypointManager: Failed to parse UDP waypoints: {e}")
            return False

    def clear(self) -> None:
        """Clear all waypoints."""
        self._user_waypoints = []
        self._calculated_waypoints = []
        self._udp_waypoints = []
        self._current_index = 0
        self._source = 'none'

    def clear_user_waypoints(self) -> None:
        """Clear user-specified waypoints only."""
        self._user_waypoints = []
        if self._calculated_waypoints:
            self._source = 'calculated'
        elif self._udp_waypoints:
            self._source = 'udp'
        else:
            self._source = 'none'

    @property
    def waypoints(self) -> List[Waypoint]:
        """Get active waypoints (based on priority)."""
        if self._user_waypoints:
            return self._user_waypoints
        elif self._calculated_waypoints:
            return self._calculated_waypoints
        elif self._udp_waypoints:
            return self._udp_waypoints
        return []

    @property
    def current_index(self) -> int:
        """Get current waypoint index."""
        return self._current_index

    @current_index.setter
    def current_index(self, value: int) -> None:
        """Set current waypoint index."""
        self._current_index = max(0, min(value, len(self.waypoints) - 1))

    @property
    def source(self) -> str:
        """Get current waypoint source ('user', 'calculated', 'udp', 'none')."""
        return self._source

    def has_waypoints(self) -> bool:
        """Check if any waypoints are available."""
        return len(self.waypoints) > 0

    def get_current_waypoint(self) -> Optional[Waypoint]:
        """Get the current target waypoint."""
        wps = self.waypoints
        if not wps or self._current_index >= len(wps):
            return None
        return wps[self._current_index]

    def get_next_waypoint(self) -> Optional[Waypoint]:
        """Get the next waypoint after current."""
        wps = self.waypoints
        next_idx = self._current_index + 1
        if not wps or next_idx >= len(wps):
            return None
        return wps[next_idx]

    def advance(self) -> bool:
        """
        Advance to the next waypoint.

        Returns:
            True if advanced, False if already at last waypoint
        """
        wps = self.waypoints
        if self._current_index < len(wps) - 1:
            self._current_index += 1
            return True
        return False

    def is_complete(self) -> bool:
        """Check if all waypoints have been passed."""
        wps = self.waypoints
        return not wps or self._current_index >= len(wps) - 1

    def get_waypoint_status(self, current_pos: Waypoint, waypoint: Waypoint,
                            driving_direction: int = 1) -> WaypointStatus:
        """
        Check if a waypoint has been passed, missed, or not reached.

        Args:
            current_pos: Current vehicle position
            waypoint: Target waypoint
            driving_direction: 1 for positive s direction, -1 for negative

        Returns:
            WaypointStatus enum value
        """
        # Calculate distance
        dist = current_pos.distance_to(waypoint)

        # Debug logging (periodic)
        if not hasattr(self, '_wp_status_log_counter'):
            self._wp_status_log_counter = 0
        self._wp_status_log_counter += 1
        log_now = (self._wp_status_log_counter % 100 == 0)
        
        if log_now:
            print(f"[DEBUG_WP_STATUS] cur_road={current_pos.road_id}, wp_road={waypoint.road_id}, "
                  f"cur_s={current_pos.s:.1f}, wp_s={waypoint.s:.1f}, "
                  f"cur_lane={current_pos.lane_id}, wp_lane={waypoint.lane_id}, dist={dist:.1f}")

        # Check if on same road
        if current_pos.road_id != waypoint.road_id or current_pos.road_id < 0:
            # Different road or unknown
            
            # [FIX] Track minimum distance to detect when we've passed the waypoint
            # This is crucial for junction transitions where road IDs differ
            if not hasattr(self, '_min_dist_to_wp'):
                self._min_dist_to_wp = {}
            
            wp_key = (waypoint.x, waypoint.y)  # Use position as key
            
            if wp_key not in self._min_dist_to_wp:
                self._min_dist_to_wp[wp_key] = dist
            else:
                # Update minimum distance
                if dist < self._min_dist_to_wp[wp_key]:
                    self._min_dist_to_wp[wp_key] = dist
            
            # Check if we've passed the waypoint by monitoring distance increase
            # If we got close and are now moving away, we've passed it
            min_dist = self._min_dist_to_wp.get(wp_key, dist)
            # [FIX] Relaxed threshold: 10m instead of 5m for junction areas
            if min_dist < 10.0 and dist > min_dist + 3.0:
                # We got within 10m and are now moving away by 3m - consider it passed
                del self._min_dist_to_wp[wp_key]  # Clean up
                return WaypointStatus.PASSED
            
            # [FIX] Also check if waypoint is clearly behind us based on heading
            # Calculate angle from car to waypoint
            dx = waypoint.x - current_pos.x
            dy = waypoint.y - current_pos.y
            angle_to_wp = math.atan2(dy, dx)
            angle_diff = angle_to_wp - current_pos.h
            # Normalize to [-pi, pi]
            while angle_diff > math.pi:
                angle_diff -= 2 * math.pi
            while angle_diff < -math.pi:
                angle_diff += 2 * math.pi
            
            # If waypoint is more than 90 degrees behind us and we're more than 5m away, skip it
            if abs(angle_diff) > math.pi / 2 and dist > 5.0:
                if wp_key in self._min_dist_to_wp:
                    del self._min_dist_to_wp[wp_key]
                return WaypointStatus.PASSED
            
            # Original close proximity check
            if dist < 1.5:  # [FIX] Tighter threshold for Dense Route (1m spacing) matches
                # Check lane
                if current_pos.lane_id == waypoint.lane_id or waypoint.lane_id == 0:
                    return WaypointStatus.PASSED
                else:
                    return WaypointStatus.MISSED
            return WaypointStatus.NOT_REACHED

        # Same road - use s coordinate
        if driving_direction > 0:
            passed = current_pos.s > waypoint.s - 1.0  # 1m tolerance
        else:
            passed = current_pos.s < waypoint.s + 1.0

        if log_now:
            print(f"[DEBUG_WP_STATUS] Same road check: passed={passed}")

        if passed:
            # Check lane
            if current_pos.lane_id == waypoint.lane_id or waypoint.lane_id == 0:
                return WaypointStatus.PASSED
            else:
                return WaypointStatus.MISSED

        return WaypointStatus.NOT_REACHED
