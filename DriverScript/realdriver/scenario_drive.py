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


@dataclass
class SteeringConfig:
    """
    Steering controller tuning parameters.
    
    Adjust these values to tune the steering behavior.
    All parameters are collected here for easy modification.
    """
    # === Curvature Detection ===
    curvature_sample_dist: float = 10.0  # Distance ahead to sample curvature (m)
    
    # === Anticipatory Steering (Pre-steer before curves) ===
    anticipate_start_dist: float = 10.0  # Start looking for curves this far ahead (m)
    anticipate_end_dist: float = 0.0     # Stop anticipation at this distance (m), handoff to normal control
    anticipate_min_heading: float = 0.1  # Minimum heading change to trigger anticipation (rad, ~5.7°)
    anticipate_max_gain: float = 0.7    # Maximum anticipatory steering gain
    
    # === Lookahead Point ===
    lookahead_time: float = 0.6          # Base lookahead time (seconds ahead)
    lookahead_min_dist: float = 3.0      # Minimum lookahead distance (m)
    lookahead_curvature_scale: float = 10.0  # Curvature scaling factor for lookahead reduction
    lookahead_min_factor: float = 0.4    # Minimum lookahead factor (40% of base in tight curves)
    
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
    front_axle_offset: float = 3       # Distance from CG/rear axle to front axle (m)
                                          # Track path with front, not center
    
    # === Steering Smoothing ===
    smoothing_factor: float = 0.3        # Low-pass filter factor (0=none, 1=max)



# Default steering configuration - modify this for tuning
DEFAULT_STEERING_CONFIG = SteeringConfig()



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
                 waypoint_port: int = 54996,
                 gt_lib_path: Optional[str] = None,
                 steering_pid: Tuple[float, float, float] = (1.0, 0.01, 0.1),
                 speed_pid: Tuple[float, float, float] = (0.8, 0.02, 0.1),  # Increased gains for better braking
                 lane_change_time: float = 5.0,
                 lookahead_distance: float = 5.0,
                 steering_config: Optional[SteeringConfig] = None):
        """
        Initialize ScenarioDriveController.

        Args:
            lib_path: Path to esminiRMLib.dll
            xodr_path: Path to OpenDRIVE map file (.xodr)
            ego_id: Object ID of the ego vehicle in OSI GroundTruth
            target_speed_port: UDP port for receiving target speed
            waypoint_port: UDP port for receiving waypoints from esmini
            gt_lib_path: Path to GT_esminiLib.dll (optional, for routing)
            steering_pid: PID gains for steering (kp, ki, kd)
            speed_pid: PID gains for speed control (kp, ki, kd)
            lane_change_time: Time to complete a lane change (seconds)
            lookahead_distance: Lookahead distance for steering (meters)
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
                # Initialize with the same OpenDRIVE map
                if self.gt_rm_lib.init(xodr_path) < 0:
                    print(f"[WARN] ScenarioDrive: GT_esminiRMLib failed to load map")
                    self.gt_rm_lib = None
            except Exception as e:
                print(f"[WARN] ScenarioDrive: Failed to load GT_esminiRMLib: {e}")

        # Initialize router
        self.router = SimplifiedRouter(self.rm_lib, self.gt_rm_lib)

        # Initialize waypoint manager
        self.waypoint_mgr = WaypointManager()

        # Initialize PID controllers
        # Initialize PID controllers
        # [TUNED] Adjusted gains for intersection handling
        self.steering_pid = PIDController(
            kp=0.8, ki=0.02, kd=0.1,  # Increased for faster response at intersections
            output_limits=(-1.0, 1.0), integral_limits=(-0.3, 0.3)
        )
        self.speed_pid = PIDController(
            kp=speed_pid[0], ki=speed_pid[1], kd=speed_pid[2],
            output_limits=(-1.0, 1.0), integral_limits=(-0.5, 0.5)
        )

        # Steering configuration (tuning parameters)
        self.steer_cfg = steering_config if steering_config else DEFAULT_STEERING_CONFIG

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

        # UDP receiver for waypoints (from C++ ControllerRealDriver)
        self._waypoint_sock = None
        self._setup_waypoint_receiver(waypoint_port)

    def _setup_waypoint_receiver(self, port: int):
        """Setup UDP receiver for waypoints."""
        try:
            self._waypoint_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self._waypoint_sock.bind(("127.0.0.1", port))
            self._waypoint_sock.setblocking(False)
            print(f"[INFO] ScenarioDrive: Listening for waypoints on port {port}")
        except Exception as e:
            print(f"[WARN] ScenarioDrive: Failed to setup waypoint receiver: {e}")

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
                    new_target = struct.unpack('<d', data[1:9])[0]
                    # [DEBUG] Log when target speed changes significantly
                    if abs(new_target - self.target_speed) > 0.1:
                        print(f"[DEBUG_PY] Target speed CHANGED: {self.target_speed:.2f} -> {new_target:.2f} m/s")
                    self.target_speed = new_target
        except BlockingIOError:
            pass  # No data available
        except Exception as e:
            pass  # Ignore errors

    def _receive_waypoints(self):
        """Receive waypoints from UDP (non-blocking)."""
        if self._waypoint_sock is None:
            return

        try:
            while True:
                data, _ = self._waypoint_sock.recvfrom(8192)  # Larger buffer for waypoints
                # Check if data changed to avoid relpanning every frame
                if hasattr(self, '_last_udp_data') and self._last_udp_data == data:
                     continue
                self._last_udp_data = data
                
                # Parse
                from .waypoint import parse_waypoints_from_udp
                try:
                    index, waypoints = parse_waypoints_from_udp(data)
                    
                # [PLANNING] Generate Dense Route
                    # Only replan if we have enough waypoints and position is valid
                    # [FIX] Skip replanning if we already have a calculated dense route
                    # This prevents issues when vehicle is in junction and position detection is wrong
                    if self.waypoint_mgr.source == 'calculated' and len(self.waypoint_mgr.waypoints) > 50:
                        # Already have a dense route, don't replan
                        continue
                    
                    current_pos = None
                    
                    start_pos = getattr(self, '_last_ego_pos', None)
                    if start_pos and waypoints:
                        # [FIX] Use UDP index to ignore passed waypoints.
                        # Otherwise, we plan a path back to WP[0] (U-Turn).
                        future_wps = waypoints[index:]
                        
                        if future_wps:
                            # print(f"[INFO] Replanning route with {len(future_wps)} future sparse waypoints...")
                            dense_route = self.router.calculate_route_from_waypoints(start_pos, future_wps, step_size=1.0)
                            # print(f"[INFO] Generated dense route with {len(dense_route)} points")
                            
                            if dense_route:
                                print(f"[INFO] UDP: Generated dense route with {len(dense_route)} waypoints from {len(future_wps)} sparse WPs")
                                self.waypoint_mgr.set_calculated_waypoints(dense_route)
                                
                                # Find closest point to snap index
                                min_dist = float('inf')
                                closest_idx = 0
                                # Search window can be small since we start from ego pos
                                check_len = min(100, len(dense_route))
                                for i in range(check_len):
                                    d = start_pos.distance_to(dense_route[i])
                                    if d < min_dist:
                                        min_dist = d
                                        closest_idx = i
                                self.waypoint_mgr.current_index = closest_idx
                        else:
                            # Completed or no future WPs
                            pass
                    else:
                        # Fallback if no ego pos yet
                        self.waypoint_mgr.receive_from_udp(data)
                        
                except ValueError as e:
                    print(f"[WARN] ScenarioDrive: Failed to parse UDP waypoints: {e}")

        except BlockingIOError:
            pass  # No data available
        except Exception as e:
            print(f"[WARN] ScenarioDrive: Error receiving waypoints: {e}")

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
        
        wp = Waypoint(
            x=pos.x,
            y=pos.y,
            h=ori.yaw,
            road_id=-1,  # Will be determined by RMLib
            s=0.0,
            lane_id=0
        )
        self._last_ego_pos = wp # Cache for replanning
        return wp

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

    def _calculate_cross_track_error(self, current_pos: Waypoint, waypoints: List[Waypoint], index: int) -> float:
        """
        Calculate Cross Track Error (XTE) relative to the path segment being traversed.
        Segment is defined as Waypoint[index-1] -> Waypoint[index].
        Positive XTE = Left of path.
        """
        if index == 0:
            # Special case: approaching first waypoint.
            # Use checks to see if we have enough points
            if len(waypoints) < 2:
                return 0.0
            p1 = waypoints[0]
            p2 = waypoints[1] # Use first segment direction
            # If we are travelling TO WP[0], and we assume path starts at WP[0]... 
            # Actually, typically index=0 means we are going TO WP[0].
            # But in Dense Route, WP[0] is usually Current Pos/Start.
            # So we pass it immediately and go to index=1.
            # If we are effectively at index=0, XTE isn't well defined unless we have a 'previous'.
            # Let's fallback to 0 or use first segment.
            # Using first segment (0->1) is safer for orientation.
        else:
            p1 = waypoints[index - 1]
            p2 = waypoints[index]

        # Path vector
        dx = p2.x - p1.x
        dy = p2.y - p1.y
        len_sq = dx * dx + dy * dy
        
        if len_sq < 0.001:
            return 0.0

        # Vector from p1 to car
        cx = current_pos.x - p1.x
        cy = current_pos.y - p1.y

        # Cross product in 2D: cp = dx * cy - dy * cx
        cross_prod = dx * cy - dy * cx
        xte = cross_prod / math.sqrt(len_sq)
        
        return xte

    def _calculate_steering(self, current_pos, current_pos_data, target_wp: Waypoint,
                            dt: float) -> float:
        """
        Calculate steering output toward target waypoint.
        
        NEAREST-NEIGHBOR + LOOKAHEAD APPROACH:
        1. Find the closest waypoint on the Dense Route
        2. Look ahead from that point by a distance based on speed
        3. Steer toward the lookahead point's heading
        
        This eliminates complex waypoint pass detection logic.
        """
        wps = self.waypoint_mgr.waypoints
        
        if len(wps) < 2:
            return 0.0
        
        # Get config
        cfg = self.steer_cfg
        
        # === Project tracking point to front axle ===
        # Instead of tracking with vehicle center, track with front axle
        # This prevents the front corner from cutting inside on turns
        front_x = current_pos.x + cfg.front_axle_offset * math.cos(current_pos.h)
        front_y = current_pos.y + cfg.front_axle_offset * math.sin(current_pos.h)
        
        # Create a temporary position object for front axle tracking
        class FrontPos:
            def __init__(self, x, y, h):
                self.x, self.y, self.h = x, y, h
            def distance_to(self, wp):
                return math.sqrt((self.x - wp.x)**2 + (self.y - wp.y)**2)
        
        front_pos = FrontPos(front_x, front_y, current_pos.h)
        
        # === STEP 1: Find nearest waypoint to FRONT AXLE ===
        # Search in a window around current index for efficiency
        current_idx = getattr(self, '_last_nearest_idx', 0)
        search_start = max(0, current_idx - 10)
        search_end = min(len(wps), current_idx + 100)  # Look mostly forward
        
        min_dist = float('inf')
        nearest_idx = current_idx
        for i in range(search_start, search_end):
            d = front_pos.distance_to(wps[i])
            if d < min_dist:
                min_dist = d
                nearest_idx = i

        
        # Cache for next frame
        self._last_nearest_idx = nearest_idx
        
        # Also update waypoint_mgr's index for debugging/logging purposes
        self.waypoint_mgr.current_index = nearest_idx
        
        # === STEP 2: Calculate path curvature ahead ===
        # Look at heading changes in upcoming waypoints to detect curves
        current_speed = getattr(self, '_last_speed', 5.0)
        
        # Calculate curvature by measuring heading change over distance (for lookahead adjustment)
        cfg = self.steer_cfg
        curvature = 0.0
        curvature_sample_idx = nearest_idx
        sample_dist = 0.0
        
        for i in range(nearest_idx, min(nearest_idx + 20, len(wps) - 1)):
            sample_dist += wps[i].distance_to(wps[i + 1])
            if sample_dist >= cfg.curvature_sample_dist:
                curvature_sample_idx = i + 1
                break
            curvature_sample_idx = i + 1
        
        if curvature_sample_idx > nearest_idx and sample_dist > 0:
            # Calculate heading change
            heading_diff = wps[curvature_sample_idx].h - wps[nearest_idx].h
            # Normalize
            while heading_diff > math.pi: heading_diff -= 2 * math.pi
            while heading_diff < -math.pi: heading_diff += 2 * math.pi
            # Curvature = heading change per meter
            curvature = abs(heading_diff) / sample_dist
        
        # === STEP 2.5: Anticipatory Steering - Look further ahead for curves ===
        # Scan all waypoints within anticipation window to find where curve begins
        # This fixes the issue where we only checked heading at 25m and missed curves
        
        curve_start_dist = None  # Distance to where curve begins
        curve_heading_diff = 0.0  # Total heading change through the curve
        prev_heading = wps[nearest_idx].h
        scan_dist = 0.0
        
        for i in range(nearest_idx, min(nearest_idx + 60, len(wps) - 1)):
            scan_dist += wps[i].distance_to(wps[i + 1])
            if scan_dist > cfg.anticipate_start_dist:
                break  # Stop scanning beyond anticipation distance
            
            # Check heading change from previous waypoint
            curr_heading = wps[i + 1].h
            delta_h = curr_heading - prev_heading
            while delta_h > math.pi: delta_h -= 2 * math.pi
            while delta_h < -math.pi: delta_h += 2 * math.pi
            
            # Detect curve start (significant heading change rate)
            segment_len = wps[i].distance_to(wps[i + 1])
            if segment_len > 0.001:
                heading_rate = abs(delta_h) / segment_len
                # Threshold: ~1.1 degrees per meter indicates curve start (lowered from 2°/m)
                if heading_rate > 0.02 and curve_start_dist is None:
                    curve_start_dist = scan_dist
                
                # Accumulate total heading change in curve
                if curve_start_dist is not None:
                    curve_heading_diff += delta_h
            
            prev_heading = curr_heading
        
        # Calculate anticipatory steering
        # Only activate if we detected a curve within anticipation window
        if curve_start_dist is not None and abs(curve_heading_diff) > cfg.anticipate_min_heading:
            # Blend based on distance to curve: stronger when closer
            # At 25m -> 0%, at 8m -> 100%
            if curve_start_dist <= cfg.anticipate_end_dist:
                blend_factor = 1.0  # Already at curve entry, max anticipation
            elif curve_start_dist >= cfg.anticipate_start_dist:
                blend_factor = 0.0  # Too far, no anticipation
            else:
                blend_factor = (cfg.anticipate_start_dist - curve_start_dist) / \
                              (cfg.anticipate_start_dist - cfg.anticipate_end_dist)
            
            # Apply anticipatory gain
            anticipate_gain = cfg.anticipate_max_gain * blend_factor
            self._anticipate_steering = curve_heading_diff * anticipate_gain
            self._curve_detected = True  # Flag for adaptive smoothing
        else:
            self._anticipate_steering = 0.0
            self._curve_detected = False

        
        # === STEP 3: Find lookahead point (curvature-aware) ===
        # Reduce lookahead in curves for tighter tracking
        base_lookahead = current_speed * cfg.lookahead_time
        
        # Curvature-based reduction: high curvature = shorter lookahead
        curvature_factor = max(cfg.lookahead_min_factor, 
                               1.0 - curvature * cfg.lookahead_curvature_scale)
        lookahead_dist = max(cfg.lookahead_min_dist, base_lookahead * curvature_factor)
        
        # Walk along path from nearest point until we reach lookahead distance
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
        
        # Store curvature for reference
        self._current_curvature = curvature



        # [FIX] Lane Keeping at the end of route
        # If the target is the very last waypoint, and we are close to it
        if target_idx >= len(wps) - 1:
            dist_to_end = current_pos.distance_to(wps[-1])
            if dist_to_end < 10.0:  # Start LK slightly before end
                # Use RoadManager to find lane info
                pos_handle = self.rm_lib.CreatePosition()
                # Set position (use current z if available)
                self.rm_lib.SetWorldXYHPosition(pos_handle, current_pos.x, current_pos.y, current_pos.h)
                
                # Get lane info 5m ahead
                res, info = self.rm_lib.GetLaneInfo(pos_handle, lookahead_distance=5.0, look_ahead_mode=0)
                
                if res == 0:
                    # Lane Keeping Logic
                    # Target heading is the road heading at lookahead
                    target_heading = info.heading
                    
                    # XTE is the lateral offset from lane center
                    # info.laneOffset seems to be offset OF the lane, not valid here?
                    # Actually GetLaneInfo with mode 0 returns info about lane center.
                    # We can use the position returned by GetLaneInfo as a target point
                    
                    lk_target_x = info.pos.x
                    lk_target_y = info.pos.y
                    
                    # Calculate vector to LK target
                    dx = lk_target_x - current_pos.x
                    dy = lk_target_y - current_pos.y
                    
                    # Calculate target heading from vector
                    if math.hypot(dx, dy) > 0.1:
                        target_heading = math.atan2(dy, dx)
                    
                    heading_error = target_heading - current_pos.h
                     # Normalize
                    while heading_error > math.pi: heading_error -= 2 * math.pi
                    while heading_error < -math.pi: heading_error += 2 * math.pi
                    
                    # Calculate XTE (distance to target line)
                    # Simplified: just steer towards target point (Pure Pursuit-ish)
                    # Use existing gains
                    
                    # We can reuse the steering calculation logic below if we construct a pseudo-target
                    # But let's just do a simple P-controller for heading here
                    
                    # Note: We need to invert steering for esmini (caller inverts it again, so we output + for Left turn)
                    # Left turn needed if heading_error > 0
                    
                    heading_gain = 0.8  # [FIX] Define gain locally
                    
                    # Reuse gains
                    steering = -heading_gain * heading_error  # Same sign logic as fixed before
                    
                    # Limit steering
                    steering = max(-1.0, min(1.0, steering))
                    return steering
                    
                # If RM fails, just go straight
                return 0.0
        
        # === STEP 3: Calculate steering ===
        # Use the PATH VECTOR HEADING instead of waypoint's intrinsic heading
        # This avoids issues where OpenDRIVE road heading might be opposite to travel direction (e.g. lane > 0)
        # Calculate heading from nearest_wp to target_wp
        dx = target_wp.x - nearest_wp.x
        dy = target_wp.y - nearest_wp.y
        if math.hypot(dx, dy) > 0.1:
            target_heading = math.atan2(dy, dx)
        else:
            # Fallback if points are too close
            target_heading = target_wp.h
        
        # Calculate heading error
        heading_error = target_heading - current_pos.h
        
        # Normalize to [-pi, pi]
        while heading_error > math.pi:
            heading_error -= 2 * math.pi
        while heading_error < -math.pi:
            heading_error += 2 * math.pi
        
        # === STEP 4: Calculate Cross Track Error (XTE) ===
        # XTE = perpendicular distance from car to path
        # Positive XTE = car is to the LEFT of path (need to steer RIGHT)
        # Use the nearest waypoint and next waypoint to define path direction
        next_idx = min(nearest_idx + 1, len(wps) - 1)
        next_wp = wps[next_idx]
        
        # Vector from nearest_wp to next_wp (path direction)
        path_dx = next_wp.x - nearest_wp.x
        path_dy = next_wp.y - nearest_wp.y
        path_len = math.sqrt(path_dx * path_dx + path_dy * path_dy)
        
        if path_len > 0.01:
            # Vector from nearest_wp to FRONT AXLE (not car center)
            car_dx = front_pos.x - nearest_wp.x
            car_dy = front_pos.y - nearest_wp.y
            
            # Cross product gives signed perpendicular distance
            # (path × car) > 0 means car is to the LEFT of path
            xte = (path_dx * car_dy - path_dy * car_dx) / path_len
        else:
            xte = 0.0
        
        # === STEP 5: Combine heading error and XTE ===
        # Stanley-like formulation: steer = heading_error + atan(k * xte / speed)
        # Simplified: steer = heading_error + xte_gain * xte
        # Note: The caller (scenario_drive_example.py) inverts the steering sign!
        # So we need to produce POSITIVE output when we want to turn LEFT (final < 0).
        # Heading: Error > 0 (Left target) -> Output > 0 -> Final < 0 (Left). OK.
        # XTE: XTE < 0 (Right position) -> Want Left -> Output SHOULD BE > 0 -> Final < 0.
        # So XTE gain must be negative.
        
        # [IMPROVED] Stronger XTE gain with speed-based scaling for faster lane centering
        # At high speed, reduce XTE correction to avoid oscillation
        # At low speed or after tight turns, apply stronger correction
        speed_factor = max(cfg.xte_speed_min_factor, 
                          min(cfg.xte_speed_max_factor, 
                              cfg.xte_speed_reference / max(current_speed, 1.0)))
        xte_gain = cfg.xte_base_gain * speed_factor
        
        # Apply non-linear XTE correction: stronger when far from center
        if abs(xte) > cfg.xte_nonlinear_threshold:
            # Extra correction when significantly off-center
            xte_correction = xte_gain * xte * cfg.xte_nonlinear_multiplier
        else:
            xte_correction = xte_gain * xte
        
        # Get anticipatory steering (calculated earlier)
        anticipate_steering = getattr(self, '_anticipate_steering', 0.0)
        
        # Combine: heading error + XTE correction + anticipatory steering
        steering = cfg.heading_gain * heading_error + xte_correction + anticipate_steering
        
        # === STEP 6: Steering Smoothing ===
        # Apply low-pass filter to reduce oscillations, especially at path transitions
        # REDUCE smoothing during curves for faster response
        last_steering = getattr(self, '_last_steering', steering)
        curve_detected = getattr(self, '_curve_detected', False)
        
        # Use less smoothing during curves for more responsive steering
        if curve_detected or curvature > 0.02 or abs(anticipate_steering) > 0.05:
            effective_smoothing = cfg.smoothing_factor * 0.3  # 30% of normal smoothing in curves
        else:
            effective_smoothing = cfg.smoothing_factor
        
        steering = steering * (1 - effective_smoothing) + last_steering * effective_smoothing
        self._last_steering = steering

        
        # Clamp to valid range
        steering = max(-1.0, min(1.0, steering))




        
        # [DEBUG]
        if not hasattr(self, '_steering_log_counter'):
            self._steering_log_counter = 0
        self._steering_log_counter += 1
        if self._steering_log_counter % 20 == 0:  # ~Every 0.4s
             tgt_info = f"{target_wp.road_id}/{target_wp.lane_id}" if target_wp else "End"
             cur_info = f"{current_pos_data.roadId}/{current_pos_data.laneId}" if current_pos_data else "None"
             antic_str = f"Antic={anticipate_steering:.3f}" if anticipate_steering != 0 else ""
             print(f"[DEBUG_STEER] NearIdx={nearest_idx}, TgtIdx={target_idx}, "
                   f"TgtH={target_heading:.3f}, CarH={current_pos.h:.3f}, "
                   f"HeadErr={heading_error:.3f}, XTE={xte:.2f}, Steer={steering:.3f}, "
                   f"Tgt={tgt_info}, Cur={cur_info} {antic_str}")
        
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

        # [DEBUG] Log speed control values every 20 frames
        if not hasattr(self, '_speed_log_counter'):
            self._speed_log_counter = 0
        self._speed_log_counter += 1
        if self._speed_log_counter % 20 == 0:
            print(f"[DEBUG_SPEED] dt={dt*1000:.1f}ms, target={self.target_speed:.2f}, current={current_speed:.2f}, "
                  f"error={speed_error:.2f}, PID={control:.3f} (P={self.speed_pid.last_p:.3f}, I={self.speed_pid.last_i:.3f}, D={self.speed_pid.last_d:.3f}), "
                  f"thr={throttle:.2f}, brk={brake:.2f}")

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
            lc_dist = current_pos.distance_to(next_wp)
            lane_change_dist = max(self.lane_change_time * self._last_speed, 15.0)  # Reduced from 25m for better intersection handling

            if lc_dist < lane_change_dist:
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

        # Receive waypoints from UDP (before target speed)
        self._receive_waypoints()

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

        # [FIX] Ensure Dense Route is generated if we have Sparse UDP waypoints
        # The UDP callback might miss this if Ego Pos wasn't ready.
        # Check if we are using UDP source and have very few waypoints (Sparse)
        if self.waypoint_mgr.source == 'udp' and len(self.waypoint_mgr.waypoints) < 10:
             # Try to densify
             if hasattr(self, '_last_ego_pos') and self._last_ego_pos:
                 print(f"[INFO] Late-triggering Dense Route with {len(self.waypoint_mgr.waypoints)} sparse WPs...")
                 # [FIX] Use current index to slice passed waypoints for late trigger too.
                 idx = self.waypoint_mgr.current_index
                 future_wps = self.waypoint_mgr.waypoints[idx:]
                 if future_wps:
                     dense_route = self.router.calculate_route_from_waypoints(self._last_ego_pos, future_wps, step_size=1.0)
                     if len(dense_route) > len(self.waypoint_mgr.waypoints):
                         print(f"[INFO] Successfully generated dense route ({len(dense_route)} pts)")
                         self.waypoint_mgr.set_calculated_waypoints(dense_route) # This changes source to 'calculated'
                         
                         # Sync index
                         min_dist = float('inf')
                         closest_idx = 0
                         for i, wp in enumerate(dense_route[:50]):
                             d = self._last_ego_pos.distance_to(wp)
                             if d < min_dist:
                                 min_dist = d
                                 closest_idx = i
                         self.waypoint_mgr.current_index = closest_idx

        # Get current target waypoint
        target_wp = self.waypoint_mgr.get_current_waypoint()
        if target_wp is None:
            print(f"[INFO] ScenarioDrive: All waypoints completed (index={self.waypoint_mgr.current_index}, total={len(self.waypoint_mgr.waypoints)})")
            return None, None, None

        # [DISABLED] Old waypoint status check - not needed with nearest-neighbor steering
        # The nearest-neighbor approach in _calculate_steering handles waypoint progression
        # status = self.waypoint_mgr.get_waypoint_status(current_pos, target_wp)
        # ... old PASSED/MISSED handling removed ...

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
        if self._waypoint_sock:
            self._waypoint_sock.close()
        if hasattr(self, 'rm_lib') and self.rm_lib:
            try:
                self.rm_lib.Close()
            except Exception:
                pass

    def __del__(self):
        """Destructor."""
        self.close()
