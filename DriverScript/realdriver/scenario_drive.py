"""
ScenarioDrive Controller Module

This module provides the ScenarioDriveController class that follows waypoints
and maintains target speed, similar to esmini's ControllerFollowRoute but
implemented in Python for external control.
"""

import math
import socket
import struct
from typing import List, Optional, Tuple
from enum import Enum
from dataclasses import dataclass

from .waypoint import Waypoint, WaypointManager, WaypointStatus
from .rm_lib import EsminiRMLib
from .gt_rm_lib import GTEsminiRMLib
from .simplified_router import SimplifiedRouter
from .pid_controller import PIDController

try:
    from osi3.osi_groundtruth_pb2 import GroundTruth
except ImportError:
    GroundTruth = None


class LaneChangeState(Enum):
    """Lane change state machine states."""
    LANE_KEEP = 0
    LANE_CHANGE_PREPARE = 1
    LANE_CHANGING = 2


@dataclass
class ControlOutput:
    """Control output values."""
    steering: float
    throttle: float
    brake: float
    indicator: int = 0  # 0=off, 1=left, 2=right

    @property
    def is_valid(self) -> bool:
        return not (math.isnan(self.steering) or
                   math.isnan(self.throttle) or
                   math.isnan(self.brake))


class ScenarioDriveController:
    """
    Scenario-driven autonomous controller that follows waypoints
    and maintains target speed, similar to ControllerFollowRoute.

    Features:
    - Waypoint following (lateral control)
    - Speed control (longitudinal control)
    - Explicit lane change handling
    - Multiple waypoint sources (user, calculated, UDP)
    """

    def __init__(self,
                 lib_path: str,
                 xodr_path: str,
                 ego_id: int = 0,
                 target_speed_port: int = 54995,
                 gt_lib_path: Optional[str] = None,
                 steering_pid: Tuple[float, float, float] = (1.0, 0.01, 0.1),
                 speed_pid: Tuple[float, float, float] = (0.5, 0.01, 0.05),
                 lane_change_time: float = 5.0,
                 lookahead_distance: float = 5.0):
        """
        Initialize ScenarioDriveController.

        Args:
            lib_path: Path to esminiRMLib.dll
            xodr_path: Path to OpenDRIVE map file (.xodr)
            ego_id: Object ID of the ego vehicle in OSI GroundTruth
            target_speed_port: UDP port for receiving target speed
            gt_lib_path: Path to GT_esminiLib.dll (optional, for routing)
            steering_pid: PID gains for steering (kp, ki, kd)
            speed_pid: PID gains for speed control (kp, ki, kd)
            lane_change_time: Time to complete a lane change (seconds)
            lookahead_distance: Lookahead distance for steering (meters)
        """
        # Initialize RoadManager
        self.rm_lib = EsminiRMLib(lib_path)
        if self.rm_lib.Init(xodr_path) < 0:
            raise RuntimeError(f"Failed to initialize RoadManager with map: {xodr_path}")

        # Initialize GT extension (optional)
        self.gt_rm_lib = None
        if gt_lib_path:
            try:
                self.gt_rm_lib = GTEsminiRMLib(gt_lib_path)
            except Exception as e:
                print(f"[WARN] ScenarioDrive: Failed to load GT_esminiRMLib: {e}")

        # Initialize router
        self.router = SimplifiedRouter(self.rm_lib, self.gt_rm_lib)

        # Initialize waypoint manager
        self.waypoint_mgr = WaypointManager()

        # Initialize PID controllers
        self.steering_pid = PIDController(
            kp=steering_pid[0], ki=steering_pid[1], kd=steering_pid[2],
            output_limits=(-1.0, 1.0), integral_limits=(-0.5, 0.5)
        )
        self.speed_pid = PIDController(
            kp=speed_pid[0], ki=speed_pid[1], kd=speed_pid[2],
            output_limits=(-1.0, 1.0), integral_limits=(-0.5, 0.5)
        )

        # Parameters
        self.ego_id = ego_id
        self.lane_change_time = lane_change_time
        self.lookahead_distance = lookahead_distance

        # State
        self.target_speed = 0.0
        self._last_speed = 0.0
        self._lane_change_state = LaneChangeState.LANE_KEEP
        self._lane_change_progress = 0.0
        self._lane_change_target = 0
        self._pos_handle = self.rm_lib.CreatePosition()
        self._no_route_warned = False

        # UDP receiver for target speed
        self._target_speed_sock = None
        self._setup_target_speed_receiver(target_speed_port)

    def _setup_target_speed_receiver(self, port: int):
        """Setup UDP receiver for target speed."""
        try:
            self._target_speed_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self._target_speed_sock.bind(("127.0.0.1", port))
            self._target_speed_sock.setblocking(False)
            print(f"[INFO] ScenarioDrive: Listening for target speed on port {port}")
        except Exception as e:
            print(f"[WARN] ScenarioDrive: Failed to setup target speed receiver: {e}")

    def set_waypoints(self, waypoints: List[Waypoint]) -> None:
        """
        Set user-specified waypoints to follow.

        Args:
            waypoints: List of waypoints
        """
        self.waypoint_mgr.set_waypoints(waypoints)
        self._lane_change_state = LaneChangeState.LANE_KEEP
        self._no_route_warned = False
        print(f"[INFO] ScenarioDrive: Set {len(waypoints)} user waypoints")

    def set_target(self, target: Waypoint) -> None:
        """
        Set target position and calculate route automatically.

        Args:
            target: Target waypoint
        """
        # Will calculate route on first update when we have current position
        self._pending_target = target
        self._no_route_warned = False
        print(f"[INFO] ScenarioDrive: Target set, route will be calculated")

    def set_target_speed(self, speed: float) -> None:
        """
        Set target speed manually.

        Args:
            speed: Target speed in m/s
        """
        self.target_speed = speed

    def _receive_target_speed(self):
        """Receive target speed from UDP (non-blocking)."""
        if self._target_speed_sock is None:
            return

        try:
            while True:
                data, _ = self._target_speed_sock.recvfrom(1024)
                if len(data) == 9 and data[0] == 1:
                    self.target_speed = struct.unpack('<d', data[1:9])[0]
        except BlockingIOError:
            pass  # No data available
        except Exception as e:
            pass  # Ignore errors

    def _extract_ego_from_ground_truth(self, ground_truth) -> Optional[Waypoint]:
        """Extract ego vehicle state from OSI GroundTruth."""
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

        pos = ego_obj.base.position
        ori = ego_obj.base.orientation
        vel = ego_obj.base.velocity

        speed = math.sqrt(vel.x**2 + vel.y**2)
        self._last_speed = speed

        return Waypoint(
            x=pos.x,
            y=pos.y,
            h=ori.yaw,
            road_id=-1,  # Will be determined by RMLib
            s=0.0,
            lane_id=0
        )

    def _get_position_data(self, wp: Waypoint):
        """Get road position data for a waypoint using RMLib."""
        if self._pos_handle < 0:
            return None

        res = self.rm_lib.SetWorldXYZHPosition(self._pos_handle, wp.x, wp.y, 0.0, wp.h)
        if res < 0:
            return None

        res, pos_data = self.rm_lib.GetPositionData(self._pos_handle)
        if res < 0:
            return None

        return pos_data

    def _calculate_steering(self, current_pos, current_pos_data, target_wp: Waypoint,
                            dt: float) -> float:
        """Calculate steering output toward target waypoint."""
        # Calculate heading error
        heading_to_target = current_pos.heading_to(target_wp)
        heading_error = heading_to_target - current_pos.h

        # Normalize to [-pi, pi]
        while heading_error > math.pi:
            heading_error -= 2 * math.pi
        while heading_error < -math.pi:
            heading_error += 2 * math.pi

        # Add lane offset correction if available
        lane_offset_error = 0.0
        if current_pos_data:
            # Lane offset: positive = left of lane center
            # We want to steer right (negative) if we're left of center
            lane_offset_error = -current_pos_data.laneOffset * 0.1  # Scaled contribution

        # Combined error with lookahead
        total_error = heading_error + lane_offset_error

        # Update PID
        steering = self.steering_pid.update(total_error, dt)

        return steering

    def _calculate_lane_change_steering(self, current_pos, target_lane: int,
                                         dt: float) -> Tuple[float, bool]:
        """
        Calculate steering for lane change maneuver.

        Returns:
            Tuple of (steering_offset, completed)
        """
        pos_data = self._get_position_data(current_pos)
        if pos_data is None:
            return 0.0, True

        current_lane = pos_data.laneId
        lane_offset = pos_data.laneOffset

        # Check if lane change is complete
        if current_lane == target_lane and abs(lane_offset) < 0.5:
            return 0.0, True

        # Calculate progress based on lane offset and time
        self._lane_change_progress += dt / self.lane_change_time
        self._lane_change_progress = min(1.0, self._lane_change_progress)

        # Sinusoidal steering curve
        # Positive offset = steer left, Negative offset = steer right
        direction = 1 if target_lane > current_lane else -1
        steering_offset = direction * 0.3 * math.sin(self._lane_change_progress * math.pi)

        completed = self._lane_change_progress >= 1.0
        return steering_offset, completed

    def _calculate_throttle_brake(self, current_speed: float, dt: float) -> Tuple[float, float]:
        """Calculate throttle and brake for speed control."""
        speed_error = self.target_speed - current_speed

        # PID output
        control = self.speed_pid.update(speed_error, dt)

        if control >= 0:
            # Accelerate
            throttle = min(1.0, control)
            brake = 0.0
        else:
            # Decelerate
            throttle = 0.0
            brake = min(1.0, -control)

        return throttle, brake

    def _check_lane_change_needed(self, current_pos, current_pos_data,
                                   next_wp: Waypoint) -> bool:
        """Check if a lane change is needed for the next waypoint."""
        if current_pos_data is None or next_wp.lane_id == 0:
            return False

        current_lane = current_pos_data.laneId

        # Check if target lane is different
        if next_wp.lane_id != current_lane:
            # Check distance to waypoint
            dist = current_pos.distance_to(next_wp)
            lane_change_dist = max(self.lane_change_time * self._last_speed, 25.0)

            if dist < lane_change_dist:
                return True

        return False

    def update(self, ground_truth, dt: float) -> Tuple[Optional[float], Optional[float], Optional[float]]:
        """
        Update the controller and calculate control outputs.

        Args:
            ground_truth: OSI GroundTruth protobuf message
            dt: Time step (seconds)

        Returns:
            Tuple of (steering, throttle, brake) or (None, None, None) if no route
        """
        if dt <= 0:
            return None, None, None

        # Receive target speed from UDP
        self._receive_target_speed()

        # Extract ego vehicle state
        current_pos = self._extract_ego_from_ground_truth(ground_truth)
        if current_pos is None:
            print("[WARN] ScenarioDrive: Ego vehicle not found in GroundTruth")
            return None, None, None

        # Get road position data
        current_pos_data = self._get_position_data(current_pos)
        if current_pos_data:
            current_pos.road_id = current_pos_data.roadId
            current_pos.s = current_pos_data.s
            current_pos.lane_id = current_pos_data.laneId

        # Handle pending target (auto-route calculation)
        if hasattr(self, '_pending_target') and self._pending_target:
            route = self.router.calculate_path(current_pos, self._pending_target)
            if route:
                self.waypoint_mgr.set_calculated_waypoints(route)
            self._pending_target = None

        # Check if we have waypoints
        if not self.waypoint_mgr.has_waypoints():
            if not self._no_route_warned:
                print("[WARN] ScenarioDrive: No route configured, skipping control output")
                self._no_route_warned = True
            return None, None, None

        # Get current target waypoint
        target_wp = self.waypoint_mgr.get_current_waypoint()
        if target_wp is None:
            print("[INFO] ScenarioDrive: All waypoints completed")
            return None, None, None

        # Check waypoint status
        status = self.waypoint_mgr.get_waypoint_status(current_pos, target_wp)

        if status == WaypointStatus.PASSED:
            # Advance to next waypoint
            if not self.waypoint_mgr.advance():
                print("[INFO] ScenarioDrive: Final waypoint reached")
                return None, None, None
            target_wp = self.waypoint_mgr.get_current_waypoint()
        elif status == WaypointStatus.MISSED:
            # Recalculate route if possible
            print("[WARN] ScenarioDrive: Waypoint missed, advancing")
            self.waypoint_mgr.advance()
            target_wp = self.waypoint_mgr.get_current_waypoint()

        if target_wp is None:
            return None, None, None

        # Calculate steering
        indicator = 0

        # Check if lane change is needed
        if self._lane_change_state == LaneChangeState.LANE_KEEP:
            if self._check_lane_change_needed(current_pos, current_pos_data, target_wp):
                self._lane_change_state = LaneChangeState.LANE_CHANGE_PREPARE
                self._lane_change_target = target_wp.lane_id
                self._lane_change_progress = 0.0
                # Set indicator
                if current_pos_data:
                    indicator = 1 if target_wp.lane_id > current_pos_data.laneId else 2

        # Handle lane change states
        if self._lane_change_state == LaneChangeState.LANE_CHANGE_PREPARE:
            # Brief preparation phase (could add delay here)
            self._lane_change_state = LaneChangeState.LANE_CHANGING
            indicator = 1 if self._lane_change_target > (current_pos_data.laneId if current_pos_data else 0) else 2

        if self._lane_change_state == LaneChangeState.LANE_CHANGING:
            # Calculate lane change steering
            lc_offset, completed = self._calculate_lane_change_steering(
                current_pos, self._lane_change_target, dt
            )
            base_steering = self._calculate_steering(current_pos, current_pos_data, target_wp, dt)
            steering = base_steering + lc_offset

            if completed:
                self._lane_change_state = LaneChangeState.LANE_KEEP
                indicator = 0
        else:
            # Normal waypoint following
            steering = self._calculate_steering(current_pos, current_pos_data, target_wp, dt)

        # Calculate throttle/brake
        throttle, brake = self._calculate_throttle_brake(self._last_speed, dt)

        return steering, throttle, brake

    def close(self):
        """Clean up resources."""
        if self._target_speed_sock:
            self._target_speed_sock.close()
        if hasattr(self, 'rm_lib') and self.rm_lib:
            try:
                self.rm_lib.Close()
            except Exception:
                pass

    def __del__(self):
        """Destructor."""
        self.close()
