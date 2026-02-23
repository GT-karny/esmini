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

from .waypoint import Waypoint
from .rm_lib import EsminiRMLib
from .gt_rm_lib import GTEsminiRMLib
from .simplified_router import SimplifiedRouter
from .vehicle_state import VehicleStateExtractor
from .lateral_controller import LateralController, LateralConfig, LaneChangeState
from .longitudinal_controller import LongitudinalController, LongitudinalConfig

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
    - Embedded frame waypoints and longitudinal profile handling

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
                 speed_pid: Optional[Tuple[float, float, float]] = None,
                 lane_change_time: float = 5.0,
                 lookahead_distance: float = 5.0,
                 steering_config: Optional[LateralConfig] = None,
                 longitudinal_config: Optional[LongitudinalConfig] = None,
                 allow_reverse_from_profile: bool = False,
                 mode: str = "embedded"):
        """
        Initialize ScenarioDriveController.

        Args:
            lib_path: Path to esminiRMLib.dll
            xodr_path: Path to OpenDRIVE map file (.xodr)
            ego_id: Object ID of the ego vehicle in OSI GroundTruth
            target_speed_port: Deprecated, ignored in embedded mode
            waypoint_port: Deprecated, ignored in embedded mode
            gt_lib_path: Path to GT_esminiLib.dll (optional, for routing)
            steering_pid: PID gains for steering (kp, ki, kd) - ignored, use steering_config
            speed_pid: Deprecated. Use longitudinal_config instead.
            lane_change_time: Time to complete a lane change (seconds)
            lookahead_distance: Lookahead distance for steering (meters) - ignored, use steering_config
            steering_config: Steering tuning parameters (uses defaults if None)
            longitudinal_config: Longitudinal tuning parameters including PID + feedforward (uses defaults if None)
        """
        if mode != "embedded":
            raise ValueError(
                f"Unsupported mode: {mode}. ScenarioDriveController is embedded-only."
            )
        self.mode = "embedded"

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

        # Longitudinal controller: use explicit config, or build from legacy speed_pid, or use defaults
        if longitudinal_config is not None:
            lon_config = longitudinal_config
        elif speed_pid is not None:
            lon_config = LongitudinalConfig(
                pid_kp=speed_pid[0],
                pid_ki=speed_pid[1],
                pid_kd=speed_pid[2]
            )
        else:
            lon_config = LongitudinalConfig()

        self.longitudinal = LongitudinalController(config=lon_config)

        # State
        self.ego_id = ego_id
        self.target_speed = 0.0
        self._last_speed = 0.0
        self._last_ego_pos: Optional[Waypoint] = None
        self._no_route_warned = False
        self._pending_target: Optional[Waypoint] = None
        self._last_embedded_waypoint_sig = None
        self._last_embedded_waypoint_generation_version: Optional[int] = None
        self.allow_reverse_from_profile = allow_reverse_from_profile
        self._embedded_frame_data = None

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
        """Receive longitudinal profile from embedded frame payload."""
        if not self._embedded_frame_data:
            return

        profile = self._embedded_frame_data.get("lon_profile", []) or []
        if profile:
            speed = profile[-1].get("v_target", self.target_speed)
            if not self.allow_reverse_from_profile and speed < 0.0:
                speed = 0.0
            self.target_speed = speed
            self.longitudinal.set_target_speed(speed)

    def _build_waypoint_signature(self, waypoints: List[Waypoint]) -> Tuple:
        if not waypoints:
            return tuple()
        sample_ids = [0, len(waypoints) // 4, len(waypoints) // 2, (3 * len(waypoints)) // 4, len(waypoints) - 1]
        sig = [len(waypoints)]
        for i in sample_ids:
            wp = waypoints[i]
            sig.append((
                round(wp.x, 2),
                round(wp.y, 2),
                round(wp.h, 3),
                int(wp.road_id),
                int(wp.lane_id),
                round(wp.s, 1),
                round(wp.lane_offset, 2),
            ))
        return tuple(sig)

    def _find_closest_index(self, ego_pos: Waypoint, waypoints: List[Waypoint], limit: int = 100) -> int:
        if not waypoints:
            return 0
        min_dist = float("inf")
        closest_idx = 0
        for i in range(min(limit, len(waypoints))):
            d = ego_pos.distance_to(waypoints[i])
            if d < min_dist:
                min_dist = d
                closest_idx = i
        return closest_idx

    def _receive_waypoints(self) -> None:
        """Receive waypoints from embedded frame payload and keep route dense."""
        if not self._embedded_frame_data:
            return

        frame_waypoints = self._embedded_frame_data.get("waypoints", []) or []
        if not frame_waypoints:
            return

        generation = self._embedded_frame_data.get("waypoint_generation", {}) or {}
        generation_version = int(generation.get("version", -1))
        incoming_index = int(self._embedded_frame_data.get("waypoint_index", 0))
        waypoints = [
            Waypoint(
                x=wp.get("x", 0.0),
                y=wp.get("y", 0.0),
                h=wp.get("h", 0.0),
                road_id=wp.get("road_id", -1),
                s=wp.get("s", 0.0),
                lane_id=wp.get("lane_id", 0),
                lane_offset=wp.get("lane_offset", 0.0),
            )
            for wp in frame_waypoints
        ]
        sig_tuple = self._build_waypoint_signature(waypoints)
        changed = (
            self._last_embedded_waypoint_generation_version != generation_version
            or self._last_embedded_waypoint_sig != sig_tuple
        )

        if changed:
            next_waypoints = waypoints
            next_index = max(0, min(incoming_index, max(0, len(waypoints) - 1)))

            # AssignRoute results can be sparse; densify to keep lateral stability.
            if self._last_ego_pos and len(waypoints) < 20:
                future_wps = waypoints[next_index:]
                if future_wps:
                    dense_route = self.router.calculate_route_from_waypoints(
                        self._last_ego_pos, future_wps, step_size=1.0
                    )
                    if dense_route and len(dense_route) > len(future_wps):
                        next_waypoints = dense_route
                        next_index = self._find_closest_index(self._last_ego_pos, next_waypoints)
                        print(
                            f"[INFO] Embedded: Generated dense route with {len(dense_route)} waypoints "
                            f"from {len(future_wps)} sparse WPs"
                        )

            self.lateral.set_calculated_waypoints(next_waypoints)
            self.waypoint_mgr.current_index = max(0, min(next_index, max(0, len(next_waypoints) - 1)))
            self._last_embedded_waypoint_sig = sig_tuple
            self._last_embedded_waypoint_generation_version = generation_version
            return

        max_index = max(0, len(self.waypoint_mgr.waypoints) - 1)
        self.waypoint_mgr.current_index = max(0, min(max(self.waypoint_mgr.current_index, incoming_index), max_index))

    def _ensure_dense_route(self) -> None:
        """Ensure sparse route fallback is densified once ego pose becomes available."""
        if not self._last_ego_pos:
            return
        if len(self.waypoint_mgr.waypoints) >= 10:
            return
        idx = max(0, min(self.waypoint_mgr.current_index, max(0, len(self.waypoint_mgr.waypoints) - 1)))
        future_wps = self.waypoint_mgr.waypoints[idx:]
        if not future_wps:
            return
        dense_route = self.router.calculate_route_from_waypoints(
            self._last_ego_pos, future_wps, step_size=1.0
        )
        if dense_route and len(dense_route) > len(future_wps):
            print(f"[INFO] Embedded: Late densification ({len(dense_route)} pts)")
            self.lateral.set_calculated_waypoints(dense_route)
            self.waypoint_mgr.current_index = self._find_closest_index(self._last_ego_pos, dense_route)

    def update(self, ground_truth, dt: float) -> Tuple[Optional[float], Optional[float], Optional[float]]:
        """
        Update the controller and calculate control outputs.

        Args:
            ground_truth: OSI GroundTruth protobuf message
            dt: Time step (seconds)

        Returns:
            Tuple of (steering, throttle, brake) or (None, None, None) if no route
        """
        # Always receive embedded frame data (even when dt=0) so that
        # waypoints and target speed are ready for the first real step.
        self._receive_waypoints()
        self._receive_target_speed()

        if dt <= 0:
            return None, None, None

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

        # Calculate steering (use _from_state since we already extracted state)
        steering = self.lateral.update_from_state(state, dt)

        # Handle lane change (check and apply offset)
        lc_offset, _, indicator = self.lateral.update_lane_change(state, dt)
        if self.lateral.get_lane_change_state() == LaneChangeState.LANE_CHANGING:
            steering += lc_offset

        # Calculate throttle/brake (use _from_speed since we have speed)
        lon_output = self.longitudinal.update_from_speed(state.speed, dt)

        return steering, lon_output.throttle, lon_output.brake

    def set_embedded_frame_data(self, frame_data: dict) -> None:
        """Provide frame payload from PythonDriverBridge in embedded mode."""
        self._embedded_frame_data = frame_data

    def close(self) -> None:
        """Clean up resources."""
        if hasattr(self, 'rm_lib') and self.rm_lib:
            try:
                self.rm_lib.Close()
            except Exception:
                pass

    def __del__(self):
        """Destructor."""
        try:
            self.close()
        except Exception:
            pass

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
