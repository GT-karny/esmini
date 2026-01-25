import math
from PIDController import PIDController
from esminiRMLib import EsminiRMLib

class LKASController:
    """
    Lateral Keep Assist System (LKAS) Controller using EsminiRMLib for lane detection.
    """
    def __init__(self, rm_lib: EsminiRMLib, model_type='ReferenceDriver', kp=0.5, ki=0.01, kd=0.1):
        """
        Initialize LKAS Controller.

        Args:
            rm_lib (EsminiRMLib): Initialized EsminiRMLib instance.
            model_type (str): 'ReferenceDriver' or others.
            kp, ki, kd (float): PID gains for lateral control.
        """
        self.rm_lib = rm_lib
        self.model_type = model_type
        
        # PID Controller for steering
        # Input: Lane Offset (error)
        # Output: Steering Angle (rad)
        # Assuming positive offset means vehicle is to the LEFT of center -> Need POSITIVE steering (Turn Left? Or Right?)
        # Convention:
        # Lane Offset > 0 (Left of center) -> Should steer RIGHT (Negative angle?)
        # Lane Offset < 0 (Right of center) -> Should steer LEFT (Positive angle?)
        # Check coordinate systems later. For now assume standard automotive:
        # Steering > 0 -> Left turn
        # Offset > 0 -> Left of center
        # Error = 0 - Offset = -Offset
        # PID Output -> Steering Angle
        
        self.pid = PIDController(kp=kp, ki=ki, kd=kd, output_limits=(-1.0, 1.0), integral_limits=(-0.5, 0.5))
        
        # Create a position handle for ego vehicle in RoadManager
        self.pos_handle = self.rm_lib.CreatePosition()
        if self.pos_handle < 0:
            raise RuntimeError("Failed to create position object in RoadManager")

    def update(self, x, y, z, h, dt):
        """
        Update LKAS controller.

        Args:
            x, y, z (float): Global position.
            h (float): Global heading (rad).
            dt (float): Time step (sec).

        Returns:
            float: Steering angle in radians.
        """
        if dt <= 0:
            return 0.0

        # Update position in RoadManager to get lane info
        # Note: z is important for multi-level roads, but if flat, 0 is ok if RM handles it.
        # Check if RM_SetWorldXYZHPosition is available and working
        res = self.rm_lib.SetWorldXYZHPosition(self.pos_handle, x, y, z, h)
        if res < 0:
            print(f"Warning: Failed to set world position in RM. Code: {res}")
            return 0.0

        # Get position data to retrieve lane offset and relative heading
        res, pos_data = self.rm_lib.GetPositionData(self.pos_handle)
        if res < 0:
            print("Warning: Failed to get position data from RM.")
            return 0.0

        # Calculate error
        # Target is 0 offset (Lane Center) and 0 relative heading (Aligned with lane)
        
        # Simple approach: PID on Lane Offset
        # A better approach would be: Steering = Kp * Offset_Error + Kd * Heading_Error
        # Lane Offset: pos_data.laneOffset
        # Relative Heading: pos_data.hRelative (Heading relative to lane tangent)
        
        # If vehicle is to the left (Offset > 0), we want to steer right (Negative)
        # If vehicle is pointing left (hRelative > 0), we want to steer right (Negative)
        
        lane_offset = pos_data.laneOffset
        heading_error = pos_data.hRelative 
        
        # Combined error term
        # Tuning required. Typically Lookahead error is used.
        # Error = Offset + Lookahead * sin(Heading_Error)
        lookahead = 5.0 # meters, tunable
        
        # Approximate projected offset error
        total_error = -(lane_offset + lookahead * math.sin(heading_error))

        # Update PID
        # We pass total_error. If total_error is positive (meaning we are to the right or pointing right),
        # we want positive steering (Left turn).
        steering_angle = self.pid.update(total_error, dt)
        
        return steering_angle

    def __del__(self):
        """Cleanup."""
        # Ideally we should close RM but this class doesn't own the RM lib instance entirely
        pass
