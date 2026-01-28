"""
ScenarioDrive Controller Module

This module provides the ScenarioDriveController class that follows waypoints
and maintains target speed, similar to esmini's ControllerFollowRoute but
implemented in Python for external control.

This is a combined controller that wraps LateralController and LongitudinalController
for backward compatibility. For new code, consider using the individual controllers
directly for more flexibility.
"""

import math
from typing import List, Optional, Tuple
from dataclasses import dataclass

from .waypoint import Waypoint, WaypointManager
from .rm_lib import EsminiRMLib
from .gt_rm_lib import GTEsminiRMLib
from .simplified_router import SimplifiedRouter
from .vehicle_state import VehicleState, VehicleStateExtractor
from .lateral_controller import LateralController, LateralConfig, LaneChangeState
from .longitudinal_controller import LongitudinalController, LongitudinalConfig
from .udp_receivers import WaypointReceiver, TargetSpeedReceiver

try:
    from osi3.osi_groundtruth_pb2 import GroundTruth
except ImportError:
    GroundTruth = None


# Re-export for backward compatibility
SteeringConfig = LateralConfig
DEFAULT_STEERING_CONFIG = LateralConfig()


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

    This is a combined controller that wraps LateralController and
    LongitudinalController for backward compatibility.

    Features:
    - Waypoint following (lateral control)
    - Speed control (longitudinal control)
    - Explicit lane change handling
    - Multiple waypoint sources (user, calculated, UDP)

    For new code, consider using the individual controllers directly:

        from realdriver import LateralController, LongitudinalController

        lateral = LateralController(rm_lib=rm_lib)
        longitudinal = LongitudinalController()

        # In control loop:
        steering = lateral.update(state, dt)
        output = longitudinal.update(state.speed, dt)
    """

    def __init__(self,
                 lib_path: str,
                 xodr_path: str,
                 ego_id: int = 0,
                 target_speed_port: int = 54995,
                 waypoint_port: int = 54996,
                 gt_lib_path: Optional[str] = None,
                 steering_pid: Tuple[float, float, float] = (1.0, 0.01, 0.1),
                 speed_pid: Tuple[float, float, float] = (0.8, 0.02, 0.1),
                 lane_change_time: float = 5.0,
                 lookahead_distance: float = 5.0,
                 steering_config: Optional[LateralConfig] = None):
        """
        Initialize ScenarioDriveController.

        Args:
            lib_path: Path to esminiRMLib.dll
            xodr_path: Path to OpenDRIVE map file (.xodr)
            ego_id: Object ID of the ego vehicle in OSI GroundTruth
            target_speed_port: UDP port for receiving target speed
            waypoint_port: UDP port for receiving waypoints from esmini
            gt_lib_path: Path to GT_esminiLib.dll (optional, for routing)
            steering_pid: PID gains for steering (kp, ki, kd) - ignored, use steering_config
            speed_pid: PID gains for speed control (kp, ki, kd)
            lane_change_time: Time to complete a lane change (seconds)
            lookahead_distance: Lookahead distance for steering (meters) - ignored, use steering_config
            steering_config: Steering tuning parameters (uses defaults if None)
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
                if self.gt_rm_lib.init(xodr_path) < 0:
                    print(f"[WARN] ScenarioDrive: GT_esminiRMLib failed to load map")
                    self.gt_rm_lib = None
            except Exception as e:
                print(f"[WARN] ScenarioDrive: Failed to load GT_esminiRMLib: {e}")

        # Initialize router
        self.router = SimplifiedRouter(self.rm_lib, self.gt_rm_lib)

        # Vehicle state extractor
        self.state_extractor = VehicleStateExtractor(ego_id)

        # Lateral config with lane change time
        lateral_config = steering_config or LateralConfig()
        lateral_config.lane_change_time = lane_change_time

        # Initialize sub-controllers
        self.lateral = LateralController(
            rm_lib=self.rm_lib,
            config=lateral_config
        )

        self.longitudinal = LongitudinalController(
            config=LongitudinalConfig(
                pid_kp=speed_pid[0],
                pid_ki=speed_pid[1],
                pid_kd=speed_pid[2]
            )
        )

        # UDP receivers
        self._speed_receiver = TargetSpeedReceiver(target_speed_port)
        self._waypoint_receiver = WaypointReceiver(waypoint_port)

        # State
        self.ego_id = ego_id
        self.target_speed = 0.0
        self._last_speed = 0.0
        self._last_ego_pos: Optional[Waypoint] = None
        self._no_route_warned = False
        self._pending_target: Optional[Waypoint] = None

        # For backward compatibility
        self.waypoint_mgr = self.lateral.waypoint_mgr
        self.lookahead_distance = lookahead_distance
        self.lane_change_time = lane_change_time
        self.steer_cfg = lateral_config

    def set_waypoints(self, waypoints: List[Waypoint]) -> None:
        """
        Set user-specified waypoints to follow.

        Args:
            waypoints: List of waypoints
        """
        self.lateral.set_waypoints(waypoints)
        self._no_route_warned = False
        print(f"[INFO] ScenarioDrive: Set {len(waypoints)} user waypoints")

    def set_target(self, target: Waypoint) -> None:
        """
        Set target position and calculate route automatically.

        Args:
            target: Target waypoint
        """
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
        self.longitudinal.set_target_speed(speed)

    def _receive_target_speed(self) -> None:
        """Receive target speed from UDP."""
        speed = self._speed_receiver.receive_all()
        if speed is not None:
            if abs(speed - self.target_speed) > 0.1:
                print(f"[DEBUG_PY] Target speed CHANGED: {self.target_speed:.2f} -> {speed:.2f} m/s")
            self.target_speed = speed
            self.longitudinal.set_target_speed(speed)

    def _receive_waypoints(self) -> None:
        """Receive waypoints from UDP and generate dense route."""
        result = self._waypoint_receiver.receive()
        if result is None:
            return

        index, waypoints = result

        # Skip replanning if we already have a calculated dense route
        if self.waypoint_mgr.source == 'calculated' and len(self.waypoint_mgr.waypoints) > 50:
            return

        if self._last_ego_pos and waypoints:
            # Use UDP index to ignore passed waypoints
            future_wps = waypoints[index:]

            if future_wps:
                dense_route = self.router.calculate_route_from_waypoints(
                    self._last_ego_pos, future_wps, step_size=1.0
                )

                if dense_route:
                    print(f"[INFO] UDP: Generated dense route with {len(dense_route)} waypoints "
                          f"from {len(future_wps)} sparse WPs")
                    self.lateral.set_calculated_waypoints(dense_route)

                    # Find closest point to snap index
                    min_dist = float('inf')
                    closest_idx = 0
                    check_len = min(100, len(dense_route))
                    for i in range(check_len):
                        d = self._last_ego_pos.distance_to(dense_route[i])
                        if d < min_dist:
                            min_dist = d
                            closest_idx = i
                    self.waypoint_mgr.current_index = closest_idx
        else:
            # Fallback if no ego pos yet
            self.waypoint_mgr.receive_from_udp(self._waypoint_receiver._last_data or b'')

    def _ensure_dense_route(self) -> None:
        """Ensure dense route is generated if we have sparse UDP waypoints."""
        if self.waypoint_mgr.source == 'udp' and len(self.waypoint_mgr.waypoints) < 10:
            if self._last_ego_pos:
                print(f"[INFO] Late-triggering Dense Route with {len(self.waypoint_mgr.waypoints)} sparse WPs...")
                idx = self.waypoint_mgr.current_index
                future_wps = self.waypoint_mgr.waypoints[idx:]
                if future_wps:
                    dense_route = self.router.calculate_route_from_waypoints(
                        self._last_ego_pos, future_wps, step_size=1.0
                    )
                    if len(dense_route) > len(self.waypoint_mgr.waypoints):
                        print(f"[INFO] Successfully generated dense route ({len(dense_route)} pts)")
                        self.lateral.set_calculated_waypoints(dense_route)

                        # Sync index
                        min_dist = float('inf')
                        closest_idx = 0
                        for i, wp in enumerate(dense_route[:50]):
                            d = self._last_ego_pos.distance_to(wp)
                            if d < min_dist:
                                min_dist = d
                                closest_idx = i
                        self.waypoint_mgr.current_index = closest_idx

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

        # Receive UDP data
        self._receive_waypoints()
        self._receive_target_speed()

        # Extract vehicle state
        state = self.state_extractor.extract(ground_truth)
        if state is None:
            print("[WARN] ScenarioDrive: Ego vehicle not found in GroundTruth")
            return None, None, None

        # Enrich with road data
        state = self.state_extractor.enrich_with_road_data(state, self.rm_lib)

        # Cache for route planning
        self._last_speed = state.speed
        self._last_ego_pos = Waypoint(
            x=state.x, y=state.y, h=state.h,
            road_id=state.road_id, s=state.s, lane_id=state.lane_id
        )

        # Handle pending target (auto-route calculation)
        if self._pending_target:
            route = self.router.calculate_path(self._last_ego_pos, self._pending_target)
            if route:
                self.lateral.set_calculated_waypoints(route)
            self._pending_target = None

        # Ensure dense route is available
        self._ensure_dense_route()

        # Check if we have waypoints
        if not self.lateral.has_route:
            if not self._no_route_warned:
                print("[WARN] ScenarioDrive: No route configured, skipping control output")
                self._no_route_warned = True
            return None, None, None

        # Get current target waypoint for lane change check
        target_wp = self.waypoint_mgr.get_current_waypoint()
        if target_wp is None:
            print(f"[INFO] ScenarioDrive: All waypoints completed "
                  f"(index={self.waypoint_mgr.current_index}, total={len(self.waypoint_mgr.waypoints)})")
            return None, None, None

        # Calculate steering
        steering = self.lateral.update(state, dt)

        # Handle lane change (check and apply offset)
        lc_offset, _, indicator = self.lateral.update_lane_change(state, dt)
        if self.lateral.get_lane_change_state() == LaneChangeState.LANE_CHANGING:
            steering += lc_offset

        # Calculate throttle/brake
        lon_output = self.longitudinal.update(state.speed, dt)

        return steering, lon_output.throttle, lon_output.brake

    def close(self) -> None:
        """Clean up resources."""
        self._speed_receiver.close()
        self._waypoint_receiver.close()
        if hasattr(self, 'rm_lib') and self.rm_lib:
            try:
                self.rm_lib.Close()
            except Exception:
                pass

    def __del__(self):
        """Destructor."""
        self.close()

    # === Backward compatibility properties ===

    @property
    def steering_pid(self):
        """Backward compatibility: steering PID (not used)."""
        return None

    @property
    def speed_pid(self):
        """Backward compatibility: speed PID."""
        return self.longitudinal.pid

    @property
    def _lane_change_state(self):
        """Backward compatibility: lane change state."""
        return self.lateral.get_lane_change_state()
