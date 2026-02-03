"""
Lane Change Controller Module

Provides event-driven lane change control with TTC-based safety checking.
Integrates lateral (steering) and longitudinal (speed) control during lane changes.

Simple API:
    controller = LaneChangeController(rm_lib=rm_lib, ego_id=0)
    controller.set_base_speed(20.0)

    # Check safety before triggering
    safety = controller.check_safety(ground_truth, 'left')
    if safety.is_safe:
        controller.trigger_lane_change('left')

    # In control loop:
    output = controller.update(ground_truth, dt)
    if output.is_active:
        client.set_controls(output.throttle, output.brake, output.steering)
"""

import math
from dataclasses import dataclass, field
from enum import Enum
from typing import Optional, List, Tuple, TYPE_CHECKING

from .vehicle_state import VehicleState, VehicleStateExtractor
from .pid_controller import PIDController

if TYPE_CHECKING:
    from .rm_lib import EsminiRMLib


class LaneChangeState(Enum):
    """Lane change state machine states."""
    IDLE = 0        # Waiting, control delegated to other controllers
    CHECKING = 1    # Safety check in progress
    EXECUTING = 2   # Lane change executing
    COMPLETED = 3   # Lane change completed
    ABORTED = 4     # Lane change aborted (safety reason)


@dataclass
class LaneChangeConfig:
    """
    Configuration for lane change controller.

    Adjust these values to tune the lane change behavior.
    """
    # === Safety check parameters ===
    ttc_threshold: float = 3.0           # TTC threshold (seconds) - abort if lower
    min_gap_front: float = 15.0          # Minimum safe distance to front vehicle (m)
    min_gap_rear: float = 10.0           # Minimum safe distance to rear vehicle (m)

    # === Lane change execution parameters ===
    lane_change_duration: float = 4.0    # Time to complete lane change (seconds)
    steering_gain: float = 0.3           # Steering gain (max amplitude)

    # === Detection parameters ===
    detection_range_front: float = 100.0 # Front detection range (m)
    detection_range_rear: float = 50.0   # Rear detection range (m)
    lateral_tolerance: float = 2.0       # Lateral tolerance for adjacent lane detection (m)

    # === Longitudinal control ===
    speed_reduction_factor: float = 0.95 # Speed reduction during lane change
    min_speed_for_lc: float = 3.0        # Minimum speed to allow lane change (m/s)

    # === Vehicle dimensions ===
    ego_half_length: float = 2.5         # Ego vehicle half length (m)

    # === PID parameters for speed control ===
    pid_kp: float = 0.6
    pid_ki: float = 0.01
    pid_kd: float = 0.05


DEFAULT_LANE_CHANGE_CONFIG = LaneChangeConfig()


@dataclass
class LaneChangeOutput:
    """Output from lane change controller."""
    throttle: float       # [0.0, 1.0]
    brake: float          # [0.0, 1.0]
    steering: float       # [-1.0, 1.0]
    is_active: bool       # Whether controller is actively controlling
    completed: bool       # Whether lane change just completed
    aborted: bool         # Whether lane change was aborted
    indicator: int        # Turn indicator: 0=off, 1=left, 2=right
    state: LaneChangeState

    @property
    def acceleration_command(self) -> float:
        """Net acceleration command (positive = accelerate, negative = brake)."""
        return self.throttle - self.brake

    def __iter__(self):
        """Allow unpacking: throttle, brake, steering = output"""
        return iter((self.throttle, self.brake, self.steering))


@dataclass
class AdjacentVehicleInfo:
    """Information about a vehicle in adjacent lane."""
    obj_id: int
    longitudinal_dist: float  # Positive = front, negative = rear (m)
    lateral_dist: float       # Lateral offset (m)
    speed: float              # Absolute speed (m/s)
    relative_speed: float     # target_speed - ego_speed (m/s)
    ttc: float                # Time to collision (seconds), inf if no collision
    lane_id: int              # Lane ID


@dataclass
class SafetyCheckResult:
    """Result of lane change safety check."""
    is_safe: bool                                     # Whether lane change is safe
    reason: str                                       # Reason string ("OK" or error description)
    front_vehicles: List[AdjacentVehicleInfo] = field(default_factory=list)
    rear_vehicles: List[AdjacentVehicleInfo] = field(default_factory=list)
    min_ttc_front: float = float('inf')              # Minimum TTC to front vehicles
    min_ttc_rear: float = float('inf')               # Minimum TTC to rear vehicles
    min_gap_front: float = float('inf')              # Minimum gap to front vehicles
    min_gap_rear: float = float('inf')               # Minimum gap to rear vehicles


class LaneChangeController:
    """
    Lane change controller with TTC-based safety checking.

    Provides event-driven lane change control that integrates:
    - Safety checking before and during lane change
    - Lateral control (steering) with sinusoidal profile
    - Longitudinal control (speed) with PID

    Requires RoadManager for accurate lane detection.

    Simple API:
        controller = LaneChangeController(rm_lib=rm_lib, ego_id=0)
        controller.set_base_speed(20.0)  # 20 m/s

        # Check safety first
        safety = controller.check_safety(ground_truth, 'left')
        if safety.is_safe:
            controller.trigger_lane_change('left')

        # In control loop:
        output = controller.update(ground_truth, dt)
        if output.is_active:
            client.set_controls(output.throttle, output.brake, output.steering)
    """

    def __init__(
        self,
        rm_lib: 'EsminiRMLib',
        ego_id: int = 0,
        config: Optional[LaneChangeConfig] = None
    ):
        """
        Initialize lane change controller.

        Args:
            rm_lib: RoadManager library instance (required)
            ego_id: Object ID of the ego vehicle in OSI GroundTruth
            config: Controller tuning parameters. Uses defaults if None.

        Raises:
            ValueError: If rm_lib is None
        """
        if rm_lib is None:
            raise ValueError("RoadManager (rm_lib) is required for LaneChangeController")

        self.rm_lib = rm_lib
        self.config = config or DEFAULT_LANE_CHANGE_CONFIG

        # State extractor for OSI GroundTruth
        self._state_extractor = VehicleStateExtractor(ego_id)

        # Position handles for RoadManager queries
        self._ego_pos_handle = rm_lib.CreatePosition()
        self._target_pos_handle = rm_lib.CreatePosition()

        # State management
        self._state = LaneChangeState.IDLE
        self._direction: Optional[str] = None  # 'left' or 'right'
        self._target_lane_id: Optional[int] = None
        self._progress: float = 0.0

        # Last state cache
        self._last_ego_state: Optional[VehicleState] = None
        self._last_safety_check: Optional[SafetyCheckResult] = None
        self._adjacent_vehicles: List[AdjacentVehicleInfo] = []

        # Longitudinal control
        self._speed_pid = PIDController(
            kp=self.config.pid_kp,
            ki=self.config.pid_ki,
            kd=self.config.pid_kd,
            output_limits=(-1.0, 1.0),
            integral_limits=(-0.3, 0.3)
        )
        self._base_target_speed: float = 0.0

        # Debug
        self._debug_enabled = False
        self._log_counter = 0

    # =========================================================================
    # Public API
    # =========================================================================

    def check_safety(self, ground_truth, direction: str) -> SafetyCheckResult:
        """
        Check if lane change is safe without triggering it.

        Args:
            ground_truth: OSI GroundTruth protobuf message
            direction: 'left' or 'right'

        Returns:
            SafetyCheckResult with detailed safety information
        """
        # Extract ego state
        ego_state = self._state_extractor.extract(ground_truth)
        if ego_state is None:
            return SafetyCheckResult(
                is_safe=False,
                reason="Failed to extract ego state from GroundTruth"
            )

        # Enrich with road data
        ego_state = self._state_extractor.enrich_with_road_data(ego_state, self.rm_lib)
        self._last_ego_state = ego_state

        # Get target lane
        target_lane = self._get_adjacent_lane_id(ego_state.lane_id, direction)
        if target_lane is None:
            return SafetyCheckResult(
                is_safe=False,
                reason=f"No adjacent lane found for direction '{direction}'"
            )

        # Detect adjacent vehicles
        adjacent = self._detect_adjacent_vehicles(ground_truth, ego_state, target_lane)

        # Separate front and rear vehicles
        front_vehicles = [v for v in adjacent if v.longitudinal_dist > 0]
        rear_vehicles = [v for v in adjacent if v.longitudinal_dist <= 0]

        # Calculate minimums
        min_ttc_front = min((v.ttc for v in front_vehicles), default=float('inf'))
        min_ttc_rear = min((v.ttc for v in rear_vehicles), default=float('inf'))
        min_gap_front = min((v.longitudinal_dist for v in front_vehicles), default=float('inf'))
        min_gap_rear = min((abs(v.longitudinal_dist) for v in rear_vehicles), default=float('inf'))

        # Check safety conditions
        cfg = self.config
        is_safe = True
        reason = "OK"

        # Speed check
        if ego_state.speed < cfg.min_speed_for_lc:
            is_safe = False
            reason = f"Speed too low: {ego_state.speed:.1f} < {cfg.min_speed_for_lc} m/s"

        # Front vehicle checks
        elif min_gap_front < cfg.min_gap_front:
            is_safe = False
            reason = f"Front gap too small: {min_gap_front:.1f} < {cfg.min_gap_front} m"
        elif min_ttc_front < cfg.ttc_threshold:
            is_safe = False
            reason = f"Front TTC too low: {min_ttc_front:.1f} < {cfg.ttc_threshold} s"

        # Rear vehicle checks
        elif min_gap_rear < cfg.min_gap_rear:
            is_safe = False
            reason = f"Rear gap too small: {min_gap_rear:.1f} < {cfg.min_gap_rear} m"
        elif min_ttc_rear < cfg.ttc_threshold:
            is_safe = False
            reason = f"Rear TTC too low: {min_ttc_rear:.1f} < {cfg.ttc_threshold} s"

        result = SafetyCheckResult(
            is_safe=is_safe,
            reason=reason,
            front_vehicles=front_vehicles,
            rear_vehicles=rear_vehicles,
            min_ttc_front=min_ttc_front,
            min_ttc_rear=min_ttc_rear,
            min_gap_front=min_gap_front,
            min_gap_rear=min_gap_rear
        )
        self._last_safety_check = result
        return result

    def get_adjacent_vehicles(self, ground_truth, direction: str) -> List[AdjacentVehicleInfo]:
        """
        Get list of vehicles in the adjacent lane.

        Args:
            ground_truth: OSI GroundTruth protobuf message
            direction: 'left' or 'right'

        Returns:
            List of AdjacentVehicleInfo for vehicles in the target lane
        """
        ego_state = self._state_extractor.extract(ground_truth)
        if ego_state is None:
            return []

        ego_state = self._state_extractor.enrich_with_road_data(ego_state, self.rm_lib)
        target_lane = self._get_adjacent_lane_id(ego_state.lane_id, direction)
        if target_lane is None:
            return []

        return self._detect_adjacent_vehicles(ground_truth, ego_state, target_lane)

    def trigger_lane_change(self, direction: str) -> bool:
        """
        Trigger a lane change.

        Args:
            direction: 'left' or 'right'

        Returns:
            True if accepted, False if rejected (already in progress or invalid direction)
        """
        if direction not in ('left', 'right'):
            return False

        if self._state not in (LaneChangeState.IDLE, LaneChangeState.COMPLETED, LaneChangeState.ABORTED):
            return False

        self._direction = direction
        self._state = LaneChangeState.CHECKING
        self._progress = 0.0
        self._target_lane_id = None
        return True

    def cancel_lane_change(self) -> None:
        """Cancel the current lane change."""
        if self._state != LaneChangeState.IDLE:
            self._state = LaneChangeState.ABORTED

    def set_base_speed(self, speed: float) -> None:
        """
        Set the base target speed.

        During lane change, speed will be reduced by speed_reduction_factor.

        Args:
            speed: Target speed in m/s
        """
        self._base_target_speed = max(0.0, speed)

    def update(self, ground_truth, dt: float) -> LaneChangeOutput:
        """
        Update controller and get control output.

        Args:
            ground_truth: OSI GroundTruth protobuf message
            dt: Time step (seconds)

        Returns:
            LaneChangeOutput with throttle, brake, steering, and status flags
        """
        # Default idle output
        idle_output = LaneChangeOutput(
            throttle=0.0,
            brake=0.0,
            steering=0.0,
            is_active=False,
            completed=False,
            aborted=False,
            indicator=0,
            state=self._state
        )

        if dt <= 0:
            return idle_output

        # Handle COMPLETED/ABORTED -> IDLE transition
        if self._state == LaneChangeState.COMPLETED:
            self._state = LaneChangeState.IDLE
            return LaneChangeOutput(
                throttle=0.0, brake=0.0, steering=0.0,
                is_active=False, completed=True, aborted=False,
                indicator=0, state=LaneChangeState.COMPLETED
            )

        if self._state == LaneChangeState.ABORTED:
            self._state = LaneChangeState.IDLE
            return LaneChangeOutput(
                throttle=0.0, brake=0.0, steering=0.0,
                is_active=False, completed=False, aborted=True,
                indicator=0, state=LaneChangeState.ABORTED
            )

        # IDLE state - no control
        if self._state == LaneChangeState.IDLE:
            return idle_output

        # Extract ego state
        ego_state = self._state_extractor.extract(ground_truth)
        if ego_state is None:
            return idle_output

        ego_state = self._state_extractor.enrich_with_road_data(ego_state, self.rm_lib)
        self._last_ego_state = ego_state

        # Get indicator
        indicator = 1 if self._direction == 'left' else 2

        # CHECKING state - perform safety check
        if self._state == LaneChangeState.CHECKING:
            # Get target lane
            target_lane = self._get_adjacent_lane_id(ego_state.lane_id, self._direction)
            if target_lane is None:
                self._state = LaneChangeState.ABORTED
                return LaneChangeOutput(
                    throttle=0.0, brake=0.0, steering=0.0,
                    is_active=False, completed=False, aborted=True,
                    indicator=0, state=LaneChangeState.ABORTED
                )

            self._target_lane_id = target_lane

            # Perform safety check
            safety = self.check_safety(ground_truth, self._direction)
            if not safety.is_safe:
                if self._debug_enabled:
                    print(f"[DEBUG_LC] Safety check failed: {safety.reason}")
                self._state = LaneChangeState.ABORTED
                return LaneChangeOutput(
                    throttle=0.0, brake=0.0, steering=0.0,
                    is_active=False, completed=False, aborted=True,
                    indicator=indicator, state=LaneChangeState.ABORTED
                )

            # Safety OK - start executing
            self._state = LaneChangeState.EXECUTING
            self._progress = 0.0
            if self._debug_enabled:
                print(f"[DEBUG_LC] Lane change started: {self._direction}, target_lane={target_lane}")

        # EXECUTING state - perform lane change
        if self._state == LaneChangeState.EXECUTING:
            # Update adjacent vehicles for continuous safety monitoring
            self._adjacent_vehicles = self._detect_adjacent_vehicles(
                ground_truth, ego_state, self._target_lane_id
            )

            # Optional: Check for danger during execution
            # (simplified - just check TTC)
            cfg = self.config
            for veh in self._adjacent_vehicles:
                if veh.ttc < cfg.ttc_threshold * 0.5:  # Stricter during execution
                    if self._debug_enabled:
                        print(f"[DEBUG_LC] Danger detected during execution: TTC={veh.ttc:.1f}s")
                    self._state = LaneChangeState.ABORTED
                    return LaneChangeOutput(
                        throttle=0.0, brake=0.0, steering=0.0,
                        is_active=False, completed=False, aborted=True,
                        indicator=indicator, state=LaneChangeState.ABORTED
                    )

            # Calculate steering
            steering = self._calculate_steering(ego_state, dt)

            # Calculate longitudinal control
            throttle, brake = self._calculate_longitudinal(ego_state, dt)

            # Check completion
            if self._progress >= 1.0:
                # Additional check: verify we're in target lane
                if ego_state.lane_id == self._target_lane_id:
                    self._state = LaneChangeState.COMPLETED
                    if self._debug_enabled:
                        print(f"[DEBUG_LC] Lane change completed")
                else:
                    # Not yet in target lane - extend execution
                    pass

            # Debug logging
            if self._debug_enabled:
                self._log_counter += 1
                if self._log_counter % 20 == 0:
                    print(f"[DEBUG_LC] Progress={self._progress:.2f}, Steer={steering:.3f}, "
                          f"Lane={ego_state.lane_id}, Target={self._target_lane_id}")

            return LaneChangeOutput(
                throttle=throttle,
                brake=brake,
                steering=steering,
                is_active=True,
                completed=False,
                aborted=False,
                indicator=indicator,
                state=self._state
            )

        return idle_output

    # =========================================================================
    # Properties
    # =========================================================================

    @property
    def state(self) -> LaneChangeState:
        """Current state of the controller."""
        return self._state

    @property
    def is_active(self) -> bool:
        """Whether the controller is actively controlling."""
        return self._state in (LaneChangeState.CHECKING, LaneChangeState.EXECUTING)

    @property
    def direction(self) -> Optional[str]:
        """Current lane change direction."""
        return self._direction

    @property
    def progress(self) -> float:
        """Current progress (0.0 to 1.0)."""
        return self._progress

    @property
    def last_safety_check(self) -> Optional[SafetyCheckResult]:
        """Last safety check result."""
        return self._last_safety_check

    @property
    def base_target_speed(self) -> float:
        """Base target speed in m/s."""
        return self._base_target_speed

    # =========================================================================
    # Control Methods
    # =========================================================================

    def reset(self) -> None:
        """Reset controller state."""
        self._state = LaneChangeState.IDLE
        self._direction = None
        self._target_lane_id = None
        self._progress = 0.0
        self._last_ego_state = None
        self._last_safety_check = None
        self._adjacent_vehicles = []
        self._speed_pid.reset()

    def enable_debug(self, enabled: bool = True) -> None:
        """Enable/disable debug logging."""
        self._debug_enabled = enabled
        self._log_counter = 0

    # =========================================================================
    # Internal Methods
    # =========================================================================

    def _get_adjacent_lane_id(self, current_lane_id: int, direction: str) -> Optional[int]:
        """
        Get the lane ID of the adjacent lane.

        In OpenDRIVE, lane IDs are:
        - Negative for right side of road center (driving direction)
        - Positive for left side
        - Adjacent lanes differ by 1

        Args:
            current_lane_id: Current lane ID
            direction: 'left' or 'right'

        Returns:
            Adjacent lane ID, or None if not available
        """
        if direction == 'left':
            # Moving left = increasing lane ID (towards positive)
            if current_lane_id < 0:
                return current_lane_id + 1  # e.g., -2 -> -1
            else:
                return current_lane_id + 1  # e.g., 1 -> 2
        else:  # right
            # Moving right = decreasing lane ID (towards negative)
            if current_lane_id > 0:
                return current_lane_id - 1  # e.g., 2 -> 1
            else:
                return current_lane_id - 1  # e.g., -1 -> -2

    def _detect_adjacent_vehicles(
        self,
        ground_truth,
        ego_state: VehicleState,
        target_lane_id: int
    ) -> List[AdjacentVehicleInfo]:
        """
        Detect vehicles in the target lane.

        Args:
            ground_truth: OSI GroundTruth
            ego_state: Current ego vehicle state
            target_lane_id: Target lane ID to check

        Returns:
            List of AdjacentVehicleInfo
        """
        cfg = self.config
        adjacent = []
        ego_id = self._state_extractor.ego_id

        for obj in ground_truth.moving_object:
            if obj.id.value == ego_id:
                continue

            # Get target vehicle position in road coordinates
            target_pos = obj.base.position
            target_ori = obj.base.orientation
            target_vel = obj.base.velocity

            self.rm_lib.SetWorldXYZHPosition(
                self._target_pos_handle,
                target_pos.x, target_pos.y, target_pos.z,
                target_ori.yaw
            )
            result, pos_data = self.rm_lib.GetPositionData(self._target_pos_handle)

            if result != 0:
                continue

            # Check if on same road
            if pos_data.roadId != ego_state.road_id:
                continue

            # Check if in target lane (with tolerance)
            if pos_data.laneId != target_lane_id:
                # Also check adjacent tolerance
                lateral_diff = abs(pos_data.laneOffset - ego_state.lane_offset)
                if lateral_diff > cfg.lateral_tolerance * 2:
                    continue

            # Calculate longitudinal distance
            ds = pos_data.s - ego_state.s

            # Check range
            if ds > cfg.detection_range_front or ds < -cfg.detection_range_rear:
                continue

            # Calculate speed and relative speed
            target_speed = math.sqrt(target_vel.x**2 + target_vel.y**2)
            relative_speed = target_speed - ego_state.speed

            # Calculate TTC
            ttc = self._calculate_ttc(ego_state.speed, ds, relative_speed)

            adjacent.append(AdjacentVehicleInfo(
                obj_id=obj.id.value,
                longitudinal_dist=ds,
                lateral_dist=pos_data.laneOffset,
                speed=target_speed,
                relative_speed=relative_speed,
                ttc=ttc,
                lane_id=pos_data.laneId
            ))

        return adjacent

    def _calculate_ttc(
        self,
        ego_speed: float,
        target_dist: float,
        relative_speed: float
    ) -> float:
        """
        Calculate Time-To-Collision.

        Args:
            ego_speed: Ego vehicle speed (m/s)
            target_dist: Distance to target (positive=front, negative=rear)
            relative_speed: target_speed - ego_speed

        Returns:
            TTC in seconds, float('inf') if no collision expected
        """
        # Front vehicle
        if target_dist > 0:
            # Collision possible only if ego is faster (closing)
            closing_speed = -relative_speed  # ego faster = positive closing
            if closing_speed > 0.1:  # Small threshold to avoid division issues
                return target_dist / closing_speed
            else:
                return float('inf')

        # Rear vehicle
        else:
            # Collision possible only if rear vehicle is faster
            if relative_speed > 0.1:  # Rear vehicle faster
                return abs(target_dist) / relative_speed
            else:
                return float('inf')

    def _calculate_steering(self, ego_state: VehicleState, dt: float) -> float:
        """
        Calculate steering command for lane change.

        Uses sinusoidal profile for smooth lane change.

        Args:
            ego_state: Current ego vehicle state
            dt: Time step

        Returns:
            Steering value [-1.0, 1.0]
        """
        cfg = self.config

        # Update progress
        self._progress += dt / cfg.lane_change_duration
        self._progress = min(1.0, self._progress)

        # Direction sign
        direction_sign = 1.0 if self._direction == 'left' else -1.0

        # Sinusoidal profile: 0 -> peak -> 0
        steering = direction_sign * cfg.steering_gain * math.sin(self._progress * math.pi)

        return max(-1.0, min(1.0, steering))

    def _calculate_longitudinal(
        self,
        ego_state: VehicleState,
        dt: float
    ) -> Tuple[float, float]:
        """
        Calculate longitudinal control (throttle/brake).

        Args:
            ego_state: Current ego vehicle state
            dt: Time step

        Returns:
            Tuple of (throttle, brake) in [0.0, 1.0]
        """
        cfg = self.config

        # Reduce speed during lane change
        target_speed = self._base_target_speed * cfg.speed_reduction_factor

        # Check for front vehicles and adjust target speed
        front_vehicles = [v for v in self._adjacent_vehicles if v.longitudinal_dist > 0]
        if front_vehicles:
            closest = min(front_vehicles, key=lambda v: v.longitudinal_dist)
            # Follow at slightly lower speed than front vehicle
            safe_speed = closest.speed - 1.0
            target_speed = min(target_speed, max(0.0, safe_speed))

        # PID control
        speed_error = target_speed - ego_state.speed
        control = self._speed_pid.update(speed_error, dt)

        if control >= 0:
            return min(1.0, control), 0.0
        else:
            return 0.0, min(1.0, -control)
