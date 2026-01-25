import math
import os
from .pid_controller import PIDController
from .rm_lib import EsminiRMLib

try:
    from osi3.osi_groundtruth_pb2 import GroundTruth
except ImportError:
    GroundTruth = None


class LKASController:
    """
    Lateral Keep Assist System (LKAS) Controller using EsminiRMLib for lane detection.

    This class is self-contained: it initializes and manages its own EsminiRMLib instance
    and accepts OSI GroundTruth messages directly.
    """

    def __init__(self, lib_path: str, xodr_path: str, ego_id: int = 0,
                 model_type: str = 'ReferenceDriver',
                 kp: float = 0.5, ki: float = 0.01, kd: float = 0.1):
        """
        Initialize LKAS Controller.

        Args:
            lib_path (str): Path to esminiRMLib.dll/.so
            xodr_path (str): Path to OpenDRIVE map file (.xodr)
            ego_id (int): Object ID of the ego vehicle in OSI GroundTruth
            model_type (str): 'ReferenceDriver' or others.
            kp, ki, kd (float): PID gains for lateral control.

        Raises:
            FileNotFoundError: If lib_path or xodr_path does not exist.
            RuntimeError: If RoadManager initialization fails.
        """
        # Validate paths
        if not os.path.exists(lib_path):
            raise FileNotFoundError(f"esminiRMLib not found at: {lib_path}")
        if not os.path.exists(xodr_path):
            raise FileNotFoundError(f"OpenDRIVE file not found at: {xodr_path}")

        self.ego_id = ego_id
        self.model_type = model_type
        self._last_ego_speed = 0.0

        # Initialize EsminiRMLib internally
        self.rm_lib = EsminiRMLib(lib_path)

        # Initialize RoadManager with the map
        if self.rm_lib.Init(xodr_path) < 0:
            raise RuntimeError(f"Failed to initialize RoadManager with map: {xodr_path}")

        # PID Controller for steering
        # Convention:
        # Lane Offset > 0 (Left of center) -> Should steer RIGHT (Negative angle)
        # Lane Offset < 0 (Right of center) -> Should steer LEFT (Positive angle)
        # Steering > 0 -> Left turn
        self.pid = PIDController(kp=kp, ki=ki, kd=kd, output_limits=(-1.0, 1.0), integral_limits=(-0.5, 0.5))

        # Create a position handle for ego vehicle in RoadManager
        self.pos_handle = self.rm_lib.CreatePosition()
        if self.pos_handle < 0:
            raise RuntimeError("Failed to create position object in RoadManager")

    def _extract_ego_from_ground_truth(self, ground_truth):
        """
        Extract ego vehicle state from OSI GroundTruth.

        Args:
            ground_truth: OSI GroundTruth protobuf message

        Returns:
            tuple: (x, y, z, h, speed) or None if ego not found
        """
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

        return self._parse_moving_object(ego_obj)

    def _parse_moving_object(self, obj):
        """
        Parse MovingObject to extract position and speed.

        Args:
            obj: OSI MovingObject

        Returns:
            tuple: (x, y, z, h, speed)
        """
        pos = obj.base.position
        ori = obj.base.orientation
        vel = obj.base.velocity
        speed = math.sqrt(vel.x**2 + vel.y**2)
        return (pos.x, pos.y, pos.z, ori.yaw, speed)

    @property
    def last_ego_speed(self) -> float:
        """Returns the speed from the last update() call."""
        return self._last_ego_speed

    def update(self, ground_truth, dt: float) -> float:
        """
        Update LKAS controller using OSI GroundTruth.

        Args:
            ground_truth: OSI GroundTruth protobuf message
            dt (float): Time step (sec).

        Returns:
            float: Steering angle in radians.

        Raises:
            ValueError: If ego vehicle not found in GroundTruth
        """
        if dt <= 0:
            return 0.0

        # Extract ego vehicle state from GroundTruth
        ego_state = self._extract_ego_from_ground_truth(ground_truth)
        if ego_state is None:
            raise ValueError(f"Ego vehicle with ID {self.ego_id} not found in GroundTruth")

        x, y, z, h, speed = ego_state
        self._last_ego_speed = speed

        # Update position in RoadManager to get lane info
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
        # Lane Offset: pos_data.laneOffset
        # Relative Heading: pos_data.hRelative (Heading relative to lane tangent)
        lane_offset = pos_data.laneOffset
        heading_error = pos_data.hRelative

        # Combined error term with lookahead
        lookahead = 5.0  # meters, tunable
        total_error = -(lane_offset + lookahead * math.sin(heading_error))

        # Update PID
        steering_angle = self.pid.update(total_error, dt)

        return steering_angle

    def __del__(self):
        """Cleanup RoadManager resources."""
        if hasattr(self, 'rm_lib') and self.rm_lib is not None:
            try:
                self.rm_lib.Close()
            except Exception:
                pass
