import ctypes
import os
import sys

# Define types
id_t = ctypes.c_uint32

class RM_PositionXYZ(ctypes.Structure):
    _fields_ = [
        ("x", ctypes.c_float),
        ("y", ctypes.c_float),
        ("z", ctypes.c_float)
    ]

class RM_PositionData(ctypes.Structure):
    _fields_ = [
        ("x", ctypes.c_float),
        ("y", ctypes.c_float),
        ("z", ctypes.c_float),
        ("h", ctypes.c_float),
        ("p", ctypes.c_float),
        ("r", ctypes.c_float),
        ("hRelative", ctypes.c_float),
        ("roadId", id_t),
        ("junctionId", id_t),
        ("laneId", ctypes.c_int),
        ("laneOffset", ctypes.c_float),
        ("s", ctypes.c_float)
    ]

class RM_RoadLaneInfo(ctypes.Structure):
    _fields_ = [
        ("pos", RM_PositionXYZ),
        ("heading", ctypes.c_float),
        ("pitch", ctypes.c_float),
        ("roll", ctypes.c_float),
        ("width", ctypes.c_float),
        ("curvature", ctypes.c_float),
        ("speed_limit", ctypes.c_float),
        ("roadId", id_t),
        ("junctionId", id_t),
        ("laneId", ctypes.c_int),
        ("laneOffset", ctypes.c_float),
        ("s", ctypes.c_float),
        ("t", ctypes.c_float),
        ("road_type", ctypes.c_int),
        ("road_rule", ctypes.c_int),
        ("lane_type", ctypes.c_int)
    ]

class RM_RoadProbeInfo(ctypes.Structure):
    _fields_ = [
        ("road_lane_info", RM_RoadLaneInfo),
        ("relative_pos", RM_PositionXYZ),
        ("relative_h", ctypes.c_float)
    ]

class EsminiRMLib:
    def __init__(self, lib_path):
        """
        Initialize EsminiRMLib wrapper.

        Args:
            lib_path (str): Path to esminiRMLib.dll
        """
        if not os.path.exists(lib_path):
            raise FileNotFoundError(f"esminiRMLib not found at: {lib_path}")

        try:
            self.lib = ctypes.CDLL(lib_path)
            self._setup_signatures()
        except OSError as e:
            print(f"Failed to load library: {e}")
            raise

    def _setup_signatures(self):
        # int RM_Init(const char* odrFilename);
        self.lib.RM_Init.argtypes = [ctypes.c_char_p]
        self.lib.RM_Init.restype = ctypes.c_int

        # int RM_Close();
        self.lib.RM_Close.argtypes = []
        self.lib.RM_Close.restype = ctypes.c_int

        # int RM_CreatePosition();
        self.lib.RM_CreatePosition.argtypes = []
        self.lib.RM_CreatePosition.restype = ctypes.c_int

        # int RM_SetWorldXYHPosition(int handle, float x, float y, float h);
        self.lib.RM_SetWorldXYHPosition.argtypes = [ctypes.c_int, ctypes.c_float, ctypes.c_float, ctypes.c_float]
        self.lib.RM_SetWorldXYHPosition.restype = ctypes.c_int

        # int RM_SetWorldXYZHPosition(int handle, float x, float y, float z, float h);
        self.lib.RM_SetWorldXYZHPosition.argtypes = [ctypes.c_int, ctypes.c_float, ctypes.c_float, ctypes.c_float, ctypes.c_float]
        self.lib.RM_SetWorldXYZHPosition.restype = ctypes.c_int

        # int RM_GetPositionData(int handle, RM_PositionData* data);
        self.lib.RM_GetPositionData.argtypes = [ctypes.c_int, ctypes.POINTER(RM_PositionData)]
        self.lib.RM_GetPositionData.restype = ctypes.c_int

        # int RM_GetLaneInfo(int handle, float lookahead_distance, RM_RoadLaneInfo* data, int lookAheadMode, bool inRoadDrivingDirection);
        self.lib.RM_GetLaneInfo.argtypes = [ctypes.c_int, ctypes.c_float, ctypes.POINTER(RM_RoadLaneInfo), ctypes.c_int, ctypes.c_bool]
        self.lib.RM_GetLaneInfo.restype = ctypes.c_int

    def Init(self, odr_filename):
        """Initialize RoadManager with ODR file."""
        return self.lib.RM_Init(odr_filename.encode('utf-8'))

    def Close(self):
        """Close RoadManager."""
        return self.lib.RM_Close()

    def CreatePosition(self):
        """Create a position object."""
        return self.lib.RM_CreatePosition()

    def SetWorldXYHPosition(self, handle, x, y, h):
        """Set position from world X, Y and Heading."""
        return self.lib.RM_SetWorldXYHPosition(handle, x, y, h)

    def SetWorldXYZHPosition(self, handle, x, y, z, h):
        """Set position from world X, Y, Z and Heading."""
        return self.lib.RM_SetWorldXYZHPosition(handle, x, y, z, h)

    def GetPositionData(self, handle):
        """Get position data."""
        data = RM_PositionData()
        res = self.lib.RM_GetPositionData(handle, ctypes.byref(data))
        return res, data

    def GetLaneInfo(self, handle, lookahead_distance=0.0, look_ahead_mode=0, in_road_driving_direction=True):
        """
        Get lane info (including offset).
        look_ahead_mode: 0=LaneCenter, 1=RoadCenter, 2=CurrentOffset
        """
        data = RM_RoadLaneInfo()
        res = self.lib.RM_GetLaneInfo(handle, lookahead_distance, ctypes.byref(data), look_ahead_mode, in_road_driving_direction)
        return res, data
