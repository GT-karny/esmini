"""
Lateral Controller Module

Provides standalone lateral (steering) control for waypoint following.
Implements curvature-aware Pure Pursuit with anticipatory steering.
"""

import math
from dataclasses import dataclass
from enum import Enum
from typing import Optional, List, Tuple, TYPE_CHECKING

from .waypoint import Waypoint, WaypointManager
from .vehicle_state import VehicleState

if TYPE_CHECKING:
    from .rm_lib import EsminiRMLib


class LaneChangeState(Enum):
    """Lane change state machine states."""
    LANE_KEEP = 0
    LANE_CHANGE_PREPARE = 1
    LANE_CHANGING = 2


@dataclass
class LateralConfig:
    """
    Lateral controller tuning parameters.

    Adjust these values to tune the steering behavior.
    All parameters are collected here for easy modification.
    """
    # === Curvature Detection ===
    curvature_sample_dist: float = 10.0  # Distance ahead to sample curvature (m)

    # === Anticipatory Steering (Pre-steer before curves) ===
    anticipate_start_dist: float = 10.0  # Start looking for curves this far ahead (m)
    anticipate_end_dist: float = 0.0     # Stop anticipation at this distance (m)
    anticipate_min_heading: float = 0.1  # Minimum heading change to trigger anticipation (rad)
    anticipate_max_gain: float = 0.7     # Maximum anticipatory steering gain

    # === Lookahead Point ===
    lookahead_time: float = 0.6          # Base lookahead time (seconds ahead)
    lookahead_min_dist: float = 3.0      # Minimum lookahead distance (m)
    lookahead_curvature_scale: float = 10.0  # Curvature scaling factor for lookahead
    lookahead_min_factor: float = 0.4    # Minimum lookahead factor in tight curves

    # === Heading Control ===
    heading_gain: float = 0.8            # Proportional gain for heading error

    # === Cross-Track Error (XTE) ===
    xte_base_gain: float = -0.35         # Base XTE gain (negative for correct direction)
    xte_speed_min_factor: float = 0.5    # Minimum speed factor for XTE scaling
    xte_speed_max_factor: float = 1.5    # Maximum speed factor for XTE scaling
    xte_speed_reference: float = 10.0    # Reference speed for XTE scaling (m/s)
    xte_nonlinear_threshold: float = 1.0 # XTE threshold for non-linear correction (m)
    xte_nonlinear_multiplier: float = 1.5  # Extra correction when XTE > threshold

    # === Vehicle Geometry ===
    front_axle_offset: float = 3.0       # Distance from CG to front axle (m)

    # === Steering Smoothing ===
    smoothing_factor: float = 0.3        # Low-pass filter factor (0=none, 1=max)

    # === Lane Change ===
    lane_change_time: float = 5.0        # Time to complete lane change (seconds)


DEFAULT_LATERAL_CONFIG = LateralConfig()


class LateralController:
    """
    Lateral (steering) controller for waypoint following.

    Implements a curvature-aware Pure Pursuit algorithm with:
    - Nearest-neighbor waypoint tracking
    - Anticipatory steering for upcoming curves
    - Cross-track error correction
    - Adaptive lookahead based on speed and curvature
    - Steering smoothing

    Example:
        controller = LateralController(rm_lib=rm_lib)
        controller.set_waypoints(waypoints)

        # In control loop:
        steering = controller.update(vehicle_state, dt)
    """

    def __init__(self,
                 rm_lib: Optional['EsminiRMLib'] = None,
                 config: Optional[LateralConfig] = None,
                 waypoint_mgr: Optional[WaypointManager] = None):
        """
        Initialize lateral controller.

        Args:
            rm_lib: RoadManager instance (optional, for road-aware features)
            config: Steering tuning parameters
            waypoint_mgr: Waypoint manager (created internally if None)
        """
        self.rm_lib = rm_lib
        self.config = config or DEFAULT_LATERAL_CONFIG
        self.waypoint_mgr = waypoint_mgr or WaypointManager()

        # Position handle for RM queries
        self._pos_handle = -1
        if rm_lib:
            self._pos_handle = rm_lib.CreatePosition()

        # Internal state
        self._last_steering = 0.0
        self._last_nearest_idx = 0
        self._current_curvature = 0.0
        self._anticipate_steering = 0.0
        self._curve_detected = False

        # Lane change state
        self._lane_change_state = LaneChangeState.LANE_KEEP
        self._lane_change_progress = 0.0
        self._lane_change_target = 0

        # Debug
        self._debug_enabled = False
        self._log_counter = 0

    def set_waypoints(self, waypoints: List[Waypoint]) -> None:
        """
        Set user-specified waypoints.

        Args:
            waypoints: List of waypoints to follow
        """
        self.waypoint_mgr.set_waypoints(waypoints)
        self._lane_change_state = LaneChangeState.LANE_KEEP
        self._last_nearest_idx = 0

    def set_calculated_waypoints(self, waypoints: List[Waypoint]) -> None:
        """
        Set calculated (dense) waypoints.

        Args:
            waypoints: List of calculated waypoints
        """
        self.waypoint_mgr.set_calculated_waypoints(waypoints)
        self._lane_change_state = LaneChangeState.LANE_KEEP
        self._last_nearest_idx = 0

    @property
    def has_route(self) -> bool:
        """Check if a route is available."""
        return self.waypoint_mgr.has_waypoints()

    @property
    def current_waypoint_index(self) -> int:
        """Current waypoint index being tracked."""
        return self.waypoint_mgr.current_index

    @current_waypoint_index.setter
    def current_waypoint_index(self, value: int) -> None:
        """Set current waypoint index."""
        self.waypoint_mgr.current_index = value

    @property
    def waypoints(self) -> List[Waypoint]:
        """Current waypoint list."""
        return self.waypoint_mgr.waypoints

    @property
    def curvature(self) -> float:
        """Current detected path curvature."""
        return self._current_curvature

    def update(self, state: VehicleState, dt: float) -> float:
        """
        Calculate steering output.

        Args:
            state: Current vehicle state
            dt: Time step (seconds)

        Returns:
            Steering value in range [-1.0, 1.0]
        """
        if dt <= 0:
            return 0.0

        wps = self.waypoint_mgr.waypoints
        if len(wps) < 2:
            return 0.0

        cfg = self.config

        # === Project tracking point to front axle ===
        front_x = state.x + cfg.front_axle_offset * math.cos(state.h)
        front_y = state.y + cfg.front_axle_offset * math.sin(state.h)

        # === STEP 1: Find nearest waypoint to front axle ===
        current_idx = self._last_nearest_idx
        search_start = max(0, current_idx - 10)
        search_end = min(len(wps), current_idx + 100)

        min_dist = float('inf')
        nearest_idx = current_idx
        for i in range(search_start, search_end):
            dx = front_x - wps[i].x
            dy = front_y - wps[i].y
            d = math.sqrt(dx * dx + dy * dy)
            if d < min_dist:
                min_dist = d
                nearest_idx = i

        self._last_nearest_idx = nearest_idx
        self.waypoint_mgr.current_index = nearest_idx

        # === STEP 2: Calculate path curvature ahead ===
        curvature = self._calculate_curvature(wps, nearest_idx, cfg.curvature_sample_dist)
        self._current_curvature = curvature

        # === STEP 2.5: Anticipatory steering ===
        self._calculate_anticipatory_steering(wps, nearest_idx, state.speed, cfg)

        # === STEP 3: Find lookahead point ===
        base_lookahead = state.speed * cfg.lookahead_time
        curvature_factor = max(cfg.lookahead_min_factor,
                               1.0 - curvature * cfg.lookahead_curvature_scale)
        lookahead_dist = max(cfg.lookahead_min_dist, base_lookahead * curvature_factor)

        target_idx = nearest_idx
        accumulated_dist = 0.0
        for i in range(nearest_idx, min(nearest_idx + 50, len(wps) - 1)):
            segment_dist = wps[i].distance_to(wps[i + 1])
            accumulated_dist += segment_dist
            if accumulated_dist >= lookahead_dist:
                target_idx = i + 1
                break
            target_idx = i + 1

        target_wp = wps[min(target_idx, len(wps) - 1)]
        nearest_wp = wps[nearest_idx]

        # === Check for route end ===
        if target_idx >= len(wps) - 1:
            dist_to_end = math.sqrt((state.x - wps[-1].x)**2 + (state.y - wps[-1].y)**2)
            if dist_to_end < 10.0 and self.rm_lib:
                steering = self._lane_keep_steering(state, cfg)
                if steering is not None:
                    return steering

        # === STEP 4: Calculate target heading ===
        dx = target_wp.x - nearest_wp.x
        dy = target_wp.y - nearest_wp.y
        if math.hypot(dx, dy) > 0.1:
            target_heading = math.atan2(dy, dx)
        else:
            target_heading = target_wp.h

        # Calculate heading error
        heading_error = target_heading - state.h
        while heading_error > math.pi:
            heading_error -= 2 * math.pi
        while heading_error < -math.pi:
            heading_error += 2 * math.pi

        # === STEP 5: Calculate Cross Track Error (XTE) ===
        next_idx = min(nearest_idx + 1, len(wps) - 1)
        next_wp = wps[next_idx]

        path_dx = next_wp.x - nearest_wp.x
        path_dy = next_wp.y - nearest_wp.y
        path_len = math.sqrt(path_dx * path_dx + path_dy * path_dy)

        if path_len > 0.01:
            car_dx = front_x - nearest_wp.x
            car_dy = front_y - nearest_wp.y
            xte = (path_dx * car_dy - path_dy * car_dx) / path_len
        else:
            xte = 0.0

        # === STEP 6: Combine steering components ===
        speed_factor = max(cfg.xte_speed_min_factor,
                          min(cfg.xte_speed_max_factor,
                              cfg.xte_speed_reference / max(state.speed, 1.0)))
        xte_gain = cfg.xte_base_gain * speed_factor

        if abs(xte) > cfg.xte_nonlinear_threshold:
            xte_correction = xte_gain * xte * cfg.xte_nonlinear_multiplier
        else:
            xte_correction = xte_gain * xte

        anticipate_steering = self._anticipate_steering
        steering = cfg.heading_gain * heading_error + xte_correction + anticipate_steering

        # === STEP 7: Apply smoothing ===
        curve_detected = self._curve_detected
        if curve_detected or curvature > 0.02 or abs(anticipate_steering) > 0.05:
            effective_smoothing = cfg.smoothing_factor * 0.3
        else:
            effective_smoothing = cfg.smoothing_factor

        steering = steering * (1 - effective_smoothing) + self._last_steering * effective_smoothing
        self._last_steering = steering

        # Clamp to valid range
        steering = max(-1.0, min(1.0, steering))

        # Debug logging
        if self._debug_enabled:
            self._log_counter += 1
            if self._log_counter % 20 == 0:
                antic_str = f"Antic={anticipate_steering:.3f}" if anticipate_steering != 0 else ""
                print(f"[DEBUG_LAT] NearIdx={nearest_idx}, TgtIdx={target_idx}, "
                      f"TgtH={target_heading:.3f}, CarH={state.h:.3f}, "
                      f"HeadErr={heading_error:.3f}, XTE={xte:.2f}, Steer={steering:.3f} {antic_str}")

        return steering

    def _calculate_curvature(self, wps: List[Waypoint], start_idx: int,
                             sample_dist: float) -> float:
        """Calculate path curvature by measuring heading change."""
        sample_idx = start_idx
        accumulated_dist = 0.0

        for i in range(start_idx, min(start_idx + 20, len(wps) - 1)):
            accumulated_dist += wps[i].distance_to(wps[i + 1])
            if accumulated_dist >= sample_dist:
                sample_idx = i + 1
                break
            sample_idx = i + 1

        if sample_idx > start_idx and accumulated_dist > 0:
            heading_diff = wps[sample_idx].h - wps[start_idx].h
            while heading_diff > math.pi:
                heading_diff -= 2 * math.pi
            while heading_diff < -math.pi:
                heading_diff += 2 * math.pi
            return abs(heading_diff) / accumulated_dist

        return 0.0

    def _calculate_anticipatory_steering(self, wps: List[Waypoint], nearest_idx: int,
                                          speed: float, cfg: LateralConfig) -> None:
        """Calculate anticipatory steering for upcoming curves."""
        curve_start_dist = None
        curve_heading_diff = 0.0
        prev_heading = wps[nearest_idx].h
        scan_dist = 0.0

        for i in range(nearest_idx, min(nearest_idx + 60, len(wps) - 1)):
            scan_dist += wps[i].distance_to(wps[i + 1])
            if scan_dist > cfg.anticipate_start_dist:
                break

            curr_heading = wps[i + 1].h
            delta_h = curr_heading - prev_heading
            while delta_h > math.pi:
                delta_h -= 2 * math.pi
            while delta_h < -math.pi:
                delta_h += 2 * math.pi

            segment_len = wps[i].distance_to(wps[i + 1])
            if segment_len > 0.001:
                heading_rate = abs(delta_h) / segment_len
                if heading_rate > 0.02 and curve_start_dist is None:
                    curve_start_dist = scan_dist

                if curve_start_dist is not None:
                    curve_heading_diff += delta_h

            prev_heading = curr_heading

        if curve_start_dist is not None and abs(curve_heading_diff) > cfg.anticipate_min_heading:
            if curve_start_dist <= cfg.anticipate_end_dist:
                blend_factor = 1.0
            elif curve_start_dist >= cfg.anticipate_start_dist:
                blend_factor = 0.0
            else:
                blend_factor = (cfg.anticipate_start_dist - curve_start_dist) / \
                              (cfg.anticipate_start_dist - cfg.anticipate_end_dist)

            anticipate_gain = cfg.anticipate_max_gain * blend_factor
            self._anticipate_steering = curve_heading_diff * anticipate_gain
            self._curve_detected = True
        else:
            self._anticipate_steering = 0.0
            self._curve_detected = False

    def _lane_keep_steering(self, state: VehicleState, cfg: LateralConfig) -> Optional[float]:
        """Lane keeping steering when near route end."""
        if not self.rm_lib or self._pos_handle < 0:
            return None

        self.rm_lib.SetWorldXYHPosition(self._pos_handle, state.x, state.y, state.h)
        res, info = self.rm_lib.GetLaneInfo(self._pos_handle, lookahead_distance=5.0, look_ahead_mode=0)

        if res != 0:
            return None

        target_heading = info.heading
        lk_target_x = info.pos.x
        lk_target_y = info.pos.y

        dx = lk_target_x - state.x
        dy = lk_target_y - state.y
        if math.hypot(dx, dy) > 0.1:
            target_heading = math.atan2(dy, dx)

        heading_error = target_heading - state.h
        while heading_error > math.pi:
            heading_error -= 2 * math.pi
        while heading_error < -math.pi:
            heading_error += 2 * math.pi

        heading_gain = 0.8
        steering = -heading_gain * heading_error
        return max(-1.0, min(1.0, steering))

    def start_lane_change(self, target_lane: int) -> None:
        """
        Start a lane change maneuver.

        Args:
            target_lane: Target lane ID
        """
        self._lane_change_state = LaneChangeState.LANE_CHANGE_PREPARE
        self._lane_change_target = target_lane
        self._lane_change_progress = 0.0

    def get_lane_change_state(self) -> LaneChangeState:
        """Get current lane change state."""
        return self._lane_change_state

    def update_lane_change(self, state: VehicleState, dt: float) -> Tuple[float, bool, int]:
        """
        Update lane change maneuver.

        Args:
            state: Current vehicle state
            dt: Time step

        Returns:
            Tuple of (steering_offset, completed, indicator)
            indicator: 0=off, 1=left, 2=right
        """
        if self._lane_change_state == LaneChangeState.LANE_KEEP:
            return 0.0, True, 0

        if self._lane_change_state == LaneChangeState.LANE_CHANGE_PREPARE:
            self._lane_change_state = LaneChangeState.LANE_CHANGING

        # Get current lane from road manager
        current_lane = state.lane_id

        # Check completion
        if current_lane == self._lane_change_target and abs(state.lane_offset) < 0.5:
            self._lane_change_state = LaneChangeState.LANE_KEEP
            return 0.0, True, 0

        # Update progress
        self._lane_change_progress += dt / self.config.lane_change_time
        self._lane_change_progress = min(1.0, self._lane_change_progress)

        # Sinusoidal steering curve
        direction = 1 if self._lane_change_target > current_lane else -1
        steering_offset = direction * 0.3 * math.sin(self._lane_change_progress * math.pi)

        # Indicator
        indicator = 1 if direction > 0 else 2

        completed = self._lane_change_progress >= 1.0
        if completed:
            self._lane_change_state = LaneChangeState.LANE_KEEP

        return steering_offset, completed, indicator

    def reset(self) -> None:
        """Reset controller state."""
        self._last_steering = 0.0
        self._last_nearest_idx = 0
        self._current_curvature = 0.0
        self._anticipate_steering = 0.0
        self._curve_detected = False
        self._lane_change_state = LaneChangeState.LANE_KEEP
        self._lane_change_progress = 0.0

    def enable_debug(self, enabled: bool = True) -> None:
        """Enable/disable debug logging."""
        self._debug_enabled = enabled
        self._log_counter = 0
