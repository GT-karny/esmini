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
# Each waypoint: [x: double][y: double][h: double][roadId: uint32][s: double][laneId: int32][laneOffset: double]
WAYPOINT_PACKET_TYPE = 2
WAYPOINT_STRUCT_FORMAT = '<dddIdid'  # x, y, h, roadId, s, laneId, laneOffset
WAYPOINT_STRUCT_SIZE = 48  # 8+8+8+4+8+4+8 bytes


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
        x, y, h, road_id, s, lane_id, lane_offset = struct.unpack(
            '<dddIdid',
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
        # Check if on same road
        if current_pos.road_id != waypoint.road_id or current_pos.road_id < 0:
            # Different road or unknown - use distance check
            dist = current_pos.distance_to(waypoint)
            if dist < 5.0:  # Close enough threshold for waypoint detection
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

        if passed:
            # Check lane
            if current_pos.lane_id == waypoint.lane_id or waypoint.lane_id == 0:
                return WaypointStatus.PASSED
            else:
                return WaypointStatus.MISSED

        return WaypointStatus.NOT_REACHED
