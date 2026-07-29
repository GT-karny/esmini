"""
ACC (Adaptive Cruise Control) Controller Module

Provides longitudinal control with lead vehicle following capability.
API-compatible with LongitudinalController but adds ACC functionality.

Based on esmini's ControllerACC.cpp logic, adapted for OSI-only or
optional RoadManager-based operation.

Simple API:
    controller = ACCController(ego_id=0)
    controller.set_target_speed(30.0)  # 30 m/s cruise speed

    # In control loop - pass GroundTruth directly:
    output = controller.update(ground_truth, dt)
    throttle, brake = output.throttle, output.brake

With RoadManager (more accurate lane-based detection):
    controller = ACCController(ego_id=0, rm_lib=rm_lib)
"""

import math
from dataclasses import dataclass, replace
from typing import Optional, Tuple, TYPE_CHECKING

from .vehicle_state import VehicleState, VehicleStateExtractor
from .longitudinal_controller import LongitudinalOutput

if TYPE_CHECKING:
    from .rm_lib import EsminiRMLib


@dataclass
class ACCConfig:
    """
    Configuration for ACC controller.

    Adjust these values to tune the ACC behavior.
    Based on ControllerACC.cpp parameters.
    """

    # ACC-specific parameters (from ControllerACC.cpp)
    time_gap: float = 1.5  # Target headway time (seconds)
    lateral_dist: float = 5.0  # Lateral tolerance for lead detection (m)
    min_dist: float = 3.0  # Minimum following distance (m)
    acceleration_factor: float = 0.7  # Free-flow acceleration factor

    # Speed/acceleration limits
    max_acceleration: float = 3.0  # m/s^2
    max_deceleration: float = 5.0  # m/s^2

    # Lead vehicle detection
    lookahead_dist: float = 100.0  # Maximum lookahead distance (m)

    # Output conversion (acceleration -> throttle/brake)
    acc_to_throttle_gain: float = 0.3  # Scale positive acc to throttle [0,1]
    acc_to_brake_gain: float = 0.2  # Scale negative acc to brake [0,1]

    # Vehicle dimensions
    ego_half_length: float = 2.5  # Default vehicle half-length (m)


DEFAULT_ACC_CONFIG = ACCConfig()


@dataclass
class LeadVehicleInfo:
    """Information about detected lead vehicle."""

    obj_id: int  # Lead vehicle object ID
    gap_distance: float  # Gap between bounding boxes (m)
    relative_speed: float  # lead_speed - ego_speed (m/s), positive = lead faster
    lead_speed: float  # Absolute speed of lead vehicle (m/s)
    longitudinal_dist: float  # Raw longitudinal distance (before bbox adjustment)
    lateral_offset: float  # Lateral offset from ego centerline (m)


class ACCController:
    """
    Adaptive Cruise Control longitudinal controller.

    API-compatible with LongitudinalController but adds:
    - Lead vehicle detection from OSI GroundTruth
    - Headway-based speed adaptation
    - Smooth acceleration/deceleration transitions

    Based on esmini's ControllerACC.cpp logic.

    Simple API (OSI-only):
        controller = ACCController(ego_id=0)
        controller.set_target_speed(30.0)  # 30 m/s

        # In control loop:
        output = controller.update(ground_truth, dt)

    With RoadManager (lane-based detection):
        controller = ACCController(ego_id=0, rm_lib=rm_lib)
    """

    def __init__(
        self,
        ego_id: int = 0,
        config: Optional[ACCConfig] = None,
        rm_lib: Optional["EsminiRMLib"] = None,
    ):
        """
        Initialize ACC controller.

        Args:
            ego_id: Object ID of the ego vehicle in OSI GroundTruth
            config: Controller tuning parameters. Uses defaults if None.
            rm_lib: Optional RoadManager library for lane-based detection.
                    If None, uses coordinate transformation (OSI-only mode).
        """
        self.config = config or DEFAULT_ACC_CONFIG
        self._rm_lib = rm_lib

        # Internal state extractor for GroundTruth parsing
        self._state_extractor = VehicleStateExtractor(ego_id)
        self._target_pos_handle: int = -1  # Position handle for target vehicles

        # State
        self._target_speed = 0.0
        self._last_speed = 0.0
        self._current_speed = 0.0
        self._last_acceleration = 0.0
        self._last_lead_info: Optional[LeadVehicleInfo] = None

        # Vehicle dimensions
        self._ego_half_length = self.config.ego_half_length

        # Debug
        self._debug_enabled = False
        self._log_counter = 0

    @property
    def target_speed(self) -> float:
        """Current target speed in m/s."""
        return self._target_speed

    @target_speed.setter
    def target_speed(self, value: float) -> None:
        """Set target speed in m/s."""
        self._target_speed = max(0.0, value)

    @property
    def last_speed(self) -> float:
        """Last processed vehicle speed (m/s)."""
        return self._last_speed

    @property
    def lead_vehicle(self) -> Optional[LeadVehicleInfo]:
        """Information about the currently detected lead vehicle, or None."""
        return self._last_lead_info

    @property
    def use_road_manager(self) -> bool:
        """Whether RoadManager is being used for lane-based detection."""
        return self._rm_lib is not None

    def set_target_speed(self, speed: float) -> None:
        """
        Set target speed.

        Args:
            speed: Target speed in m/s (clamped to >= 0)
        """
        self._target_speed = max(0.0, speed)

    def update(self, ground_truth, dt: float) -> LongitudinalOutput:
        """
        Calculate throttle/brake output from OSI GroundTruth.

        This is the recommended API - pass GroundTruth directly.

        Args:
            ground_truth: OSI GroundTruth protobuf message
            dt: Time step (seconds)

        Returns:
            LongitudinalOutput with throttle and brake in [0.0, 1.0]
        """
        if dt <= 0:
            return LongitudinalOutput(throttle=0.0, brake=0.0)

        # Extract ego state
        state = self._state_extractor.extract(ground_truth)
        if state is None:
            return LongitudinalOutput(throttle=0.0, brake=0.0)

        self._last_speed = state.speed

        # Detect lead vehicle
        if self._rm_lib is not None:
            lead_info = self._detect_lead_vehicle_with_rm(ground_truth, state)
        else:
            lead_info = self._detect_lead_vehicle_osi_only(ground_truth, state)

        self._last_lead_info = lead_info

        # Calculate acceleration
        if lead_info is not None:
            acc = self._calculate_acc_with_lead(state.speed, lead_info)
        else:
            acc = self._calculate_acc_free_flow(state.speed)

        self._last_acceleration = acc

        # Update internal speed estimate
        new_speed = state.speed + acc * dt
        new_speed = max(0.0, min(new_speed, self._target_speed))
        self._current_speed = new_speed

        # Debug logging
        if self._debug_enabled:
            self._log_counter += 1
            if self._log_counter % 20 == 0:
                lead_str = f"gap={lead_info.gap_distance:.1f}m" if lead_info else "none"
                print(
                    f"[DEBUG_ACC] target={self._target_speed:.1f}, "
                    f"current={state.speed:.2f}, acc={acc:.2f}, lead={lead_str}"
                )

        # Convert to throttle/brake
        return self._acc_to_output(acc)

    def update_from_speed(self, current_speed: float, dt: float) -> LongitudinalOutput:
        """
        Calculate throttle/brake output from speed value directly.

        Note: This method does NOT detect lead vehicles since it doesn't
        have access to GroundTruth. Use only for simple speed control.

        Args:
            current_speed: Current vehicle speed (m/s)
            dt: Time step (seconds)

        Returns:
            LongitudinalOutput with throttle and brake in [0.0, 1.0]
        """
        if dt <= 0:
            return LongitudinalOutput(throttle=0.0, brake=0.0)

        self._last_speed = current_speed
        self._last_lead_info = None  # No lead detection without GroundTruth

        # Calculate acceleration (free-flow only)
        acc = self._calculate_acc_free_flow(current_speed)
        self._last_acceleration = acc

        return self._acc_to_output(acc)

    def reset(self) -> None:
        """Reset controller state."""
        self._last_speed = 0.0
        self._current_speed = 0.0
        self._last_acceleration = 0.0
        self._last_lead_info = None

    def enable_debug(self, enabled: bool = True) -> None:
        """Enable/disable debug logging."""
        self._debug_enabled = enabled
        self._log_counter = 0

    # =========================================================================
    # Lead Vehicle Detection
    # =========================================================================

    def _transform_to_ego_frame(
        self, ego_x: float, ego_y: float, ego_h: float, target_x: float, target_y: float
    ) -> Tuple[float, float]:
        """
        Transform target position to ego's local coordinate frame.

        Args:
            ego_x, ego_y, ego_h: Ego position and heading
            target_x, target_y: Target world position

        Returns:
            (ds, dt): Longitudinal distance (positive=ahead),
                      Lateral offset (positive=left)
        """
        dx = target_x - ego_x
        dy = target_y - ego_y

        cos_h = math.cos(-ego_h)
        sin_h = math.sin(-ego_h)

        ds = dx * cos_h - dy * sin_h  # Longitudinal (positive = ahead)
        dt = dx * sin_h + dy * cos_h  # Lateral (positive = left)

        return ds, dt

    @staticmethod
    def _normalize_angle(angle: float) -> float:
        """Normalize angle to [-pi, pi]."""
        while angle > math.pi:
            angle -= 2 * math.pi
        while angle < -math.pi:
            angle += 2 * math.pi
        return angle

    def _detect_lead_vehicle_osi_only(
        self, ground_truth, ego_state: VehicleState
    ) -> Optional[LeadVehicleInfo]:
        """
        Detect lead vehicle using OSI-only coordinate transformation.

        Args:
            ground_truth: OSI GroundTruth message
            ego_state: Extracted ego vehicle state

        Returns:
            LeadVehicleInfo if lead vehicle found, None otherwise
        """
        cfg = self.config
        ego_id = self._state_extractor.ego_id
        ego_x, ego_y, ego_h = ego_state.x, ego_state.y, ego_state.h

        min_gap = float("inf")
        lead_info = None

        for obj in ground_truth.moving_object:
            if obj.id.value == ego_id:
                continue

            # Get target position and dimensions
            target_pos = obj.base.position
            target_ori = obj.base.orientation
            target_vel = obj.base.velocity
            target_dim = obj.base.dimension

            # Transform to ego frame
            ds, dt = self._transform_to_ego_frame(
                ego_x, ego_y, ego_h, target_pos.x, target_pos.y
            )

            # Filter: must be ahead and within lateral tolerance
            if ds <= 0 or abs(dt) > cfg.lateral_dist:
                continue

            # Skip if beyond lookahead
            if ds > cfg.lookahead_dist:
                continue

            # Calculate gap (adjust for bounding boxes)
            # Similar to ControllerACC.cpp logic
            heading_diff = abs(self._normalize_angle(ego_h - target_ori.yaw))
            target_half_length = (
                target_dim.length / 2.0 if target_dim.length > 0 else 2.0
            )

            if heading_diff < math.pi / 2:
                # Same direction: subtract front of ego to rear of target
                gap = ds - self._ego_half_length - target_half_length
            else:
                # Opposite direction: subtract front of ego to front of target
                gap = ds - self._ego_half_length - target_half_length

            # Only consider valid gaps
            if gap > 0 and gap < min_gap:
                min_gap = gap
                target_speed = math.sqrt(target_vel.x**2 + target_vel.y**2)
                lead_info = LeadVehicleInfo(
                    obj_id=obj.id.value,
                    gap_distance=gap,
                    relative_speed=target_speed - ego_state.speed,
                    lead_speed=target_speed,
                    longitudinal_dist=ds,
                    lateral_offset=dt,
                )

        return lead_info

    def _detect_lead_vehicle_with_rm(
        self, ground_truth, ego_state: VehicleState
    ) -> Optional[LeadVehicleInfo]:
        """
        Detect lead vehicle using RoadManager for lane-based detection.

        Args:
            ground_truth: OSI GroundTruth message
            ego_state: Extracted ego vehicle state

        Returns:
            LeadVehicleInfo if lead vehicle found, None otherwise
        """
        cfg = self.config
        rm_lib = self._rm_lib
        ego_id = self._state_extractor.ego_id

        # Enrich ego state with road data
        ego_enriched = self._state_extractor.enrich_with_road_data(ego_state, rm_lib)

        # Create position handle for target vehicles if needed
        if self._target_pos_handle < 0:
            self._target_pos_handle = rm_lib.CreatePosition()

        min_gap = float("inf")
        lead_info = None

        for obj in ground_truth.moving_object:
            if obj.id.value == ego_id:
                continue

            target_pos = obj.base.position
            target_ori = obj.base.orientation
            target_vel = obj.base.velocity
            target_dim = obj.base.dimension

            # Set target position in RoadManager
            rm_lib.SetWorldXYZHPosition(
                self._target_pos_handle,
                target_pos.x,
                target_pos.y,
                target_pos.z,
                target_ori.yaw,
            )

            # Get target road position
            result, target_road_data = rm_lib.GetPositionData(self._target_pos_handle)
            if result < 0:
                continue

            # Check if on same road
            if ego_enriched.road_id != target_road_data.roadId:
                # Could be on connected road - fall back to coordinate check
                ds, dt = self._transform_to_ego_frame(
                    ego_state.x, ego_state.y, ego_state.h, target_pos.x, target_pos.y
                )
                if ds <= 0 or abs(dt) > cfg.lateral_dist:
                    continue
                same_lane = False
            else:
                # Same road: use road coordinates
                ds = target_road_data.s - ego_enriched.s
                dt = target_road_data.laneOffset - ego_enriched.lane_offset
                same_lane = ego_enriched.lane_id == target_road_data.laneId

                # Must be ahead
                if ds <= 0:
                    continue

                # Lane-based filtering: same lane or within lateral tolerance
                if not same_lane and abs(dt) > cfg.lateral_dist:
                    continue

            # Skip if beyond lookahead
            if ds > cfg.lookahead_dist:
                continue

            # Calculate gap
            target_half_length = (
                target_dim.length / 2.0 if target_dim.length > 0 else 2.0
            )
            gap = ds - self._ego_half_length - target_half_length

            if gap > 0 and gap < min_gap:
                min_gap = gap
                target_speed = math.sqrt(target_vel.x**2 + target_vel.y**2)
                lead_info = LeadVehicleInfo(
                    obj_id=obj.id.value,
                    gap_distance=gap,
                    relative_speed=target_speed - ego_state.speed,
                    lead_speed=target_speed,
                    longitudinal_dist=ds,
                    lateral_offset=dt,
                )

        return lead_info

    # =========================================================================
    # ACC Control Law (from ControllerACC.cpp)
    # =========================================================================

    def _calculate_acc_with_lead(
        self, ego_speed: float, lead_info: LeadVehicleInfo
    ) -> float:
        """
        Calculate acceleration when following a lead vehicle.

        Based on ControllerACC.cpp lines 169-191.

        Args:
            ego_speed: Current ego vehicle speed (m/s)
            lead_info: Detected lead vehicle information

        Returns:
            Acceleration command (m/s^2)
        """
        cfg = self.config

        # Emergency stop if very close
        if lead_info.gap_distance < 1.0:
            return -cfg.max_deceleration

        # Calculate follow distance: minDist + timeGap * max(egoSpeed, leadSpeed)
        speed_for_gap = max(ego_speed, lead_info.lead_speed)
        follow_dist = cfg.min_dist + cfg.time_gap * abs(speed_for_gap)

        # Distance error (positive = we have margin, negative = too close)
        dist = lead_info.gap_distance - follow_dist

        # Distance factor [0, 1]: 0 = at follow distance, 1 = very far
        if follow_dist > 0:
            dist_factor = max(0.0, min(1.0, dist / follow_dist))
        else:
            dist_factor = 0.0

        # Speed differences
        dv_min = ego_speed - min(self._target_speed, lead_info.lead_speed)
        dv_set = ego_speed - self._target_speed

        # Weighted combination (from ControllerACC.cpp)
        acc = 2.5 * dist_factor - dist_factor * dv_set - (1 - dist_factor) * dv_min

        # Clamp acceleration
        acc = max(-cfg.max_deceleration, min(cfg.max_acceleration, acc))

        return acc

    def _calculate_acc_free_flow(self, ego_speed: float) -> float:
        """
        Calculate acceleration when no lead vehicle (cruise to set speed).

        Based on ControllerACC.cpp lines 199-213.

        Args:
            ego_speed: Current ego vehicle speed (m/s)

        Returns:
            Acceleration command (m/s^2)
        """
        cfg = self.config

        # acc = (setSpeed - currentSpeed) * accelerationFactor * maxAcceleration
        acc = (
            (self._target_speed - ego_speed)
            * cfg.acceleration_factor
            * cfg.max_acceleration
        )

        # Clamp: asymmetric limits
        acc = max(
            -cfg.max_deceleration,
            min(cfg.acceleration_factor * cfg.max_acceleration, acc),
        )

        return acc

    # =========================================================================
    # Output Conversion
    # =========================================================================

    def _acc_to_output(self, acc: float) -> LongitudinalOutput:
        """
        Convert acceleration command to throttle/brake values.

        Args:
            acc: Acceleration command (m/s^2)

        Returns:
            LongitudinalOutput with throttle and brake in [0.0, 1.0]
        """
        cfg = self.config

        if acc >= 0:
            # Positive acceleration -> throttle
            throttle = min(1.0, acc * cfg.acc_to_throttle_gain)
            brake = 0.0
        else:
            # Negative acceleration (deceleration) -> brake
            throttle = 0.0
            brake = min(1.0, -acc * cfg.acc_to_brake_gain)

        return LongitudinalOutput(throttle=throttle, brake=brake)
