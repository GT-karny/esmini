"""
Vehicle State Module

Provides shared utilities for extracting vehicle state from OSI GroundTruth
and enriching it with road coordinate data from RoadManager.
"""

import math
from dataclasses import dataclass, replace
from typing import Optional, TYPE_CHECKING

if TYPE_CHECKING:
    from .rm_lib import EsminiRMLib

try:
    from osi3.osi_groundtruth_pb2 import GroundTruth
except ImportError:
    GroundTruth = None


@dataclass
class VehicleState:
    """
    Immutable snapshot of vehicle state.

    Contains both world coordinates (x, y, z, h) and road coordinates
    (road_id, lane_id, s, lane_offset) when enriched with RoadManager data.
    """
    x: float
    y: float
    z: float
    h: float              # heading (yaw) in radians
    speed: float          # m/s

    # Road coordinates (populated by enrich_with_road_data)
    road_id: int = -1
    lane_id: int = 0
    s: float = 0.0
    lane_offset: float = 0.0
    h_relative: float = 0.0  # heading relative to road

    def distance_to(self, other: 'VehicleState') -> float:
        """Calculate Euclidean distance to another state."""
        return math.sqrt((self.x - other.x)**2 + (self.y - other.y)**2)

    def heading_to(self, other: 'VehicleState') -> float:
        """Calculate heading angle to another state."""
        return math.atan2(other.y - self.y, other.x - self.x)


class VehicleStateExtractor:
    """
    Extract vehicle state from OSI GroundTruth messages.

    This class provides a reusable utility for parsing OSI GroundTruth
    and extracting ego vehicle position, orientation, and velocity.
    """

    def __init__(self, ego_id: int = 0):
        """
        Initialize the extractor.

        Args:
            ego_id: Object ID of the ego vehicle in OSI GroundTruth.
                    Falls back to host_vehicle_id if not found.
        """
        self.ego_id = ego_id
        self._pos_handle: int = -1

    def extract(self, ground_truth) -> Optional[VehicleState]:
        """
        Extract ego vehicle state from OSI GroundTruth.

        Args:
            ground_truth: OSI GroundTruth protobuf message

        Returns:
            VehicleState if ego vehicle found, None otherwise
        """
        if ground_truth is None:
            return None

        ego_obj = None

        # 1. Try specified ego_id
        for obj in ground_truth.moving_object:
            if obj.id.value == self.ego_id:
                ego_obj = obj
                break

        # 2. Fallback to host_vehicle_id
        if ego_obj is None and ground_truth.HasField('host_vehicle_id'):
            host_id = ground_truth.host_vehicle_id.value
            for obj in ground_truth.moving_object:
                if obj.id.value == host_id:
                    ego_obj = obj
                    break

        if ego_obj is None:
            return None

        # Extract position, orientation, velocity
        pos = ego_obj.base.position
        ori = ego_obj.base.orientation
        vel = ego_obj.base.velocity

        speed = math.sqrt(vel.x**2 + vel.y**2)

        return VehicleState(
            x=pos.x,
            y=pos.y,
            z=pos.z,
            h=ori.yaw,
            speed=speed
        )

    def enrich_with_road_data(self, state: VehicleState,
                               rm_lib: 'EsminiRMLib') -> VehicleState:
        """
        Add road coordinate data to vehicle state using RoadManager.

        Args:
            state: VehicleState to enrich
            rm_lib: EsminiRMLib instance with loaded map

        Returns:
            New VehicleState with road coordinates populated
        """
        if state is None:
            return None

        # Create position handle if needed
        if self._pos_handle < 0:
            self._pos_handle = rm_lib.CreatePosition()
            if self._pos_handle < 0:
                return state

        # Set world position
        res = rm_lib.SetWorldXYZHPosition(
            self._pos_handle, state.x, state.y, state.z, state.h
        )
        if res < 0:
            return state

        # Get position data
        res, pos_data = rm_lib.GetPositionData(self._pos_handle)
        if res < 0:
            return state

        # Return new state with road data (immutable)
        return replace(
            state,
            road_id=pos_data.roadId,
            lane_id=pos_data.laneId,
            s=pos_data.s,
            lane_offset=pos_data.laneOffset,
            h_relative=pos_data.hRelative
        )
