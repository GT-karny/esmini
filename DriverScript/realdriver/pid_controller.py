"""
PID Controller Module

This module provides a generic PID controller implementation.
"""

class PIDController:
    """
    Generic PID Controller implementation.
    """
    def __init__(self, kp, ki, kd, output_limits=(None, None), integral_limits=(None, None)):
        """
        Initialize the PID controller.

        Args:
            kp (float): Proportional gain.
            ki (float): Integral gain.
            kd (float): Derivative gain.
            output_limits (tuple): Output limits (min, max). None for no limit.
            integral_limits (tuple): Integral term limits (min, max). None for no limit.
        """
        self.kp = kp
        self.ki = ki
        self.kd = kd

        self.min_output, self.max_output = output_limits
        self.min_integral, self.max_integral = integral_limits

        self.reset()

    def reset(self):
        """Reset the internal state of the controller."""
        self._integral = 0.0
        self._last_error = 0.0
        self._last_time = None
        
        # Debug values
        self.last_p = 0.0
        self.last_i = 0.0
        self.last_d = 0.0

    def update(self, error, dt):
        """
        Update the PID controller.

        Args:
            error (float): The current error (setpoint - measured_value).
            dt (float): Time step in seconds.

        Returns:
            float: Control output.
        """
        if dt <= 0.0:
            return 0.0

        # Proportional term
        p_term = self.kp * error

        # Integral term
        self._integral += error * dt

        # Anti-windup for integral term
        if self.min_integral is not None:
            self._integral = max(self.min_integral, self._integral)
        if self.max_integral is not None:
            self._integral = min(self.max_integral, self._integral)

        i_term = self.ki * self._integral

        # Derivative term
        d_term = 0.0
        if self._last_time is not None:
             d_term = self.kd * (error - self._last_error) / dt

        # Store debug values
        self.last_p = p_term
        self.last_i = i_term
        self.last_d = d_term

        # Store state for next update
        self._last_error = error
        self._last_time = True # Just a flag to indicate we have a history

        # Calculate total output
        output = p_term + i_term + d_term

        # Clamp output
        if self.min_output is not None:
            output = max(self.min_output, output)
        if self.max_output is not None:
            output = min(self.max_output, output)

        return output
