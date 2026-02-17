"""
Longitudinal Controller Module

Provides standalone longitudinal (speed/throttle/brake) control.
Does NOT require RoadManager - operates purely on speed error.

Simple API:
    controller = LongitudinalController(ego_id=0)
    controller.set_target_speed(10.0)
    output = controller.update(ground_truth, dt)

Advanced API (direct speed input):
    output = controller.update_from_speed(current_speed, dt)
"""

from dataclasses import dataclass
from typing import Optional, Tuple

from .pid_controller import PIDController
from .vehicle_state import VehicleStateExtractor


@dataclass
class LongitudinalConfig:
    """
    Configuration for longitudinal controller.

    Adjust these values to tune the speed control behavior.
    """
    pid_kp: float = 0.8
    pid_ki: float = 0.02
    pid_kd: float = 0.1
    output_limits: Tuple[float, float] = (-1.0, 1.0)
    integral_limits: Tuple[float, float] = (-0.5, 0.5)
    reverse_deadband_mps: float = 0.2


DEFAULT_LONGITUDINAL_CONFIG = LongitudinalConfig()


@dataclass
class LongitudinalOutput:
    """Output from longitudinal controller."""
    throttle: float
    brake: float

    @property
    def acceleration_command(self) -> float:
        """Net acceleration command (positive = accelerate, negative = brake)."""
        return self.throttle - self.brake

    def __iter__(self):
        """Allow unpacking: throttle, brake = output"""
        return iter((self.throttle, self.brake))


class LongitudinalController:
    """
    Longitudinal (speed/acceleration) controller.

    Simple PID-based speed controller that outputs throttle and brake commands.
    Does NOT require RoadManager - operates purely on speed.

    Simple API (recommended):
        controller = LongitudinalController(ego_id=0)
        controller.set_target_speed(10.0)  # 10 m/s

        # In control loop - pass GroundTruth directly:
        output = controller.update(ground_truth, dt)
        throttle, brake = output.throttle, output.brake

    Advanced API (direct speed input):
        output = controller.update_from_speed(current_speed, dt)
    """

    def __init__(self, ego_id: int = 0, config: Optional[LongitudinalConfig] = None):
        """
        Initialize longitudinal controller.

        Args:
            ego_id: Object ID of the ego vehicle in OSI GroundTruth
            config: Controller tuning parameters. Uses defaults if None.
        """
        self.config = config or DEFAULT_LONGITUDINAL_CONFIG

        # Internal state extractor for GroundTruth parsing
        self._state_extractor = VehicleStateExtractor(ego_id)
        self._last_speed = 0.0

        self.pid = PIDController(
            kp=self.config.pid_kp,
            ki=self.config.pid_ki,
            kd=self.config.pid_kd,
            output_limits=self.config.output_limits,
            integral_limits=self.config.integral_limits
        )

        self._target_speed = 0.0
        self._debug_enabled = False
        self._log_counter = 0

    @property
    def target_speed(self) -> float:
        """Current target speed in m/s."""
        return self._target_speed

    @target_speed.setter
    def target_speed(self, value: float) -> None:
        """Set target speed in m/s."""
        self._target_speed = float(value)

    def set_target_speed(self, speed: float) -> None:
        """
        Set target speed.

        Args:
            speed: Target speed in m/s
        """
        self._target_speed = float(speed)

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
        state = self._state_extractor.extract(ground_truth)
        if state is None:
            return LongitudinalOutput(throttle=0.0, brake=0.0)

        self._last_speed = state.speed
        return self.update_from_speed(state.speed, dt)

    def update_from_speed(self, current_speed: float, dt: float) -> LongitudinalOutput:
        """
        Calculate throttle/brake output from speed value directly.

        Advanced API for cases where you have speed from another source.

        Args:
            current_speed: Current vehicle speed (m/s)
            dt: Time step (seconds)

        Returns:
            LongitudinalOutput with throttle and brake in [0.0, 1.0]
        """
        if dt <= 0:
            return LongitudinalOutput(throttle=0.0, brake=0.0)

        self._last_speed = current_speed
        target_speed = self._target_speed
        deadband = max(0.0, self.config.reverse_deadband_mps)

        # Reverse mode compares speed magnitudes so controller can generate
        # positive throttle while transmission gear is set to reverse.
        if target_speed < -deadband:
            speed_error = abs(target_speed) - abs(current_speed)
        else:
            speed_error = target_speed - current_speed

        control = self.pid.update(speed_error, dt)

        if control >= 0:
            throttle = min(1.0, control)
            brake = 0.0
        else:
            throttle = 0.0
            brake = min(1.0, -control)

        # Debug logging
        if self._debug_enabled:
            self._log_counter += 1
            if self._log_counter % 20 == 0:
                print(f"[DEBUG_LON] dt={dt*1000:.1f}ms, target={self._target_speed:.2f}, "
                      f"current={current_speed:.2f}, error={speed_error:.2f}, "
                      f"PID={control:.3f} (P={self.pid.last_p:.3f}, I={self.pid.last_i:.3f}, "
                      f"D={self.pid.last_d:.3f}), thr={throttle:.2f}, brk={brake:.2f}")

        return LongitudinalOutput(throttle=throttle, brake=brake)

    @property
    def last_speed(self) -> float:
        """Last processed vehicle speed (m/s)."""
        return self._last_speed

    def reset(self) -> None:
        """Reset PID state (integral accumulator, derivative history)."""
        self.pid.reset()

    def enable_debug(self, enabled: bool = True) -> None:
        """Enable/disable debug logging."""
        self._debug_enabled = enabled
        self._log_counter = 0
