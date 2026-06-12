# Relocated from DriverScript/realdriver/rm_lib.py -- audit SCR-7; GT_esmini-managed now.
# Original source kept as a thin shim in DriverScript/realdriver/rm_lib.py for backward compatibility.

import ctypes
import os
import sys

# Define types
id_t = ctypes.c_uint32

RM_ID_UNDEFINED = 0xFFFFFFFF


class RM_PositionXYZ(ctypes.Structure):
    _fields_ = [
        ("x", ctypes.c_double),
        ("y", ctypes.c_double),
        ("z", ctypes.c_double)
    ]


class RM_PositionData(ctypes.Structure):
    _fields_ = [
        ("x", ctypes.c_double),
        ("y", ctypes.c_double),
        ("z", ctypes.c_double),
        ("h", ctypes.c_double),
        ("p", ctypes.c_double),
        ("r", ctypes.c_double),
        ("hRelative", ctypes.c_double),
        ("roadId", id_t),
        ("junctionId", id_t),
        ("laneId", ctypes.c_int),
        ("laneOffset", ctypes.c_double),
        ("s", ctypes.c_double)
    ]


class RM_RoadLaneInfo(ctypes.Structure):
    _fields_ = [
        ("pos", RM_PositionXYZ),
        ("heading", ctypes.c_double),
        ("pitch", ctypes.c_double),
        ("roll", ctypes.c_double),
        ("width", ctypes.c_double),
        ("curvature", ctypes.c_double),
        ("speed_limit", ctypes.c_double),
        ("roadId", id_t),
        ("junctionId", id_t),
        ("laneId", ctypes.c_int),
        ("laneOffset", ctypes.c_double),
        ("s", ctypes.c_double),
        ("t", ctypes.c_double),
        ("road_type", ctypes.c_int),
        ("road_rule", ctypes.c_int),
        ("lane_type", ctypes.c_int)
    ]


class RM_RoadProbeInfo(ctypes.Structure):
    _fields_ = [
        ("road_lane_info", RM_RoadLaneInfo),
        ("relative_pos", RM_PositionXYZ),
        ("relative_h", ctypes.c_double)
    ]


class RM_PositionDiff(ctypes.Structure):
    _fields_ = [
        ("ds", ctypes.c_double),
        ("dt", ctypes.c_double),
        ("dLaneId", ctypes.c_int)
    ]


class RM_RoadSign(ctypes.Structure):
    _fields_ = [
        ("id", ctypes.c_int),
        ("x", ctypes.c_double),
        ("y", ctypes.c_double),
        ("z", ctypes.c_double),
        ("z_offset", ctypes.c_double),
        ("h", ctypes.c_double),
        ("roadId", id_t),
        ("s", ctypes.c_double),
        ("t", ctypes.c_double),
        ("name", ctypes.c_char_p),
        ("orientation", ctypes.c_int),
        ("length", ctypes.c_double),
        ("height", ctypes.c_double),
        ("width", ctypes.c_double)
    ]


class RM_RoadObjValidity(ctypes.Structure):
    _fields_ = [
        ("fromLane", ctypes.c_int),
        ("toLane", ctypes.c_int)
    ]


class RM_GeoReference(ctypes.Structure):
    _fields_ = [
        ("a_", ctypes.c_double),
        ("axis_", ctypes.c_char_p),
        ("b_", ctypes.c_double),
        ("ellps_", ctypes.c_char_p),
        ("k_", ctypes.c_double),
        ("k_0_", ctypes.c_double),
        ("lat_0_", ctypes.c_double),
        ("lon_0_", ctypes.c_double),
        ("lon_wrap_", ctypes.c_double),
        ("over_", ctypes.c_double),
        ("pm_", ctypes.c_char_p),
        ("proj_", ctypes.c_char_p),
        ("units_", ctypes.c_char_p),
        ("vunits_", ctypes.c_char_p),
        ("x_0_", ctypes.c_double),
        ("y_0_", ctypes.c_double),
        ("datum_", ctypes.c_char_p),
        ("geo_id_grids_", ctypes.c_char_p),
        ("zone_", ctypes.c_double),
        ("towgs84_", ctypes.c_int),
        ("original_georef_str_", ctypes.c_char_p)
    ]


# RM_PositionMode enum values
RM_Z_SET = 1
RM_Z_DEFAULT = 1
RM_Z_ABS = 3
RM_Z_REL = 7
RM_Z_MASK = 7
RM_H_SET = RM_Z_SET << 4
RM_H_DEFAULT = RM_Z_DEFAULT << 4
RM_H_ABS = RM_Z_ABS << 4
RM_H_REL = RM_Z_REL << 4
RM_H_MASK = RM_Z_MASK << 4
RM_P_SET = RM_Z_SET << 8
RM_P_DEFAULT = RM_Z_DEFAULT << 8
RM_P_ABS = RM_Z_ABS << 8
RM_P_REL = RM_Z_REL << 8
RM_P_MASK = RM_Z_MASK << 8
RM_R_SET = RM_Z_SET << 12
RM_R_DEFAULT = RM_Z_DEFAULT << 12
RM_R_ABS = RM_Z_ABS << 12
RM_R_REL = RM_Z_REL << 12
RM_R_MASK = RM_Z_MASK << 12


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
        # --- Initialization / Management ---

        # int RM_Init(const char* odrFilename);
        self.lib.RM_Init.argtypes = [ctypes.c_char_p]
        self.lib.RM_Init.restype = ctypes.c_int

        # int RM_InitWithString(const char* odrAsXMLString);
        self.lib.RM_InitWithString.argtypes = [ctypes.c_char_p]
        self.lib.RM_InitWithString.restype = ctypes.c_int

        # int RM_Close();
        self.lib.RM_Close.argtypes = []
        self.lib.RM_Close.restype = ctypes.c_int

        # void RM_SetLogFilePath(const char* logFilePath);
        self.lib.RM_SetLogFilePath.argtypes = [ctypes.c_char_p]
        self.lib.RM_SetLogFilePath.restype = None

        # int RM_CreatePosition();
        self.lib.RM_CreatePosition.argtypes = []
        self.lib.RM_CreatePosition.restype = ctypes.c_int

        # int RM_GetNrOfPositions();
        self.lib.RM_GetNrOfPositions.argtypes = []
        self.lib.RM_GetNrOfPositions.restype = ctypes.c_int

        # int RM_DeletePosition(int handle);
        self.lib.RM_DeletePosition.argtypes = [ctypes.c_int]
        self.lib.RM_DeletePosition.restype = ctypes.c_int

        # int RM_CopyPosition(int handle);
        self.lib.RM_CopyPosition.argtypes = [ctypes.c_int]
        self.lib.RM_CopyPosition.restype = ctypes.c_int

        # --- Position Mode ---

        # void RM_SetObjectPositionMode(int handle, int type, int mode);
        self.lib.RM_SetObjectPositionMode.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int]
        self.lib.RM_SetObjectPositionMode.restype = None

        # void RM_SetObjectPositionModeDefault(int handle, int type);
        self.lib.RM_SetObjectPositionModeDefault.argtypes = [ctypes.c_int, ctypes.c_int]
        self.lib.RM_SetObjectPositionModeDefault.restype = None

        # int RM_SetSnapLaneTypes(int handle, int laneTypes);
        self.lib.RM_SetSnapLaneTypes.argtypes = [ctypes.c_int, ctypes.c_int]
        self.lib.RM_SetSnapLaneTypes.restype = ctypes.c_int

        # int RM_SetLockOnLane(int handle, bool mode);
        self.lib.RM_SetLockOnLane.argtypes = [ctypes.c_int, ctypes.c_bool]
        self.lib.RM_SetLockOnLane.restype = ctypes.c_int

        # --- Road Info ---

        # int RM_GetNumberOfRoads();
        self.lib.RM_GetNumberOfRoads.argtypes = []
        self.lib.RM_GetNumberOfRoads.restype = ctypes.c_int

        # int RM_GetSpeedUnit();
        self.lib.RM_GetSpeedUnit.argtypes = []
        self.lib.RM_GetSpeedUnit.restype = ctypes.c_int

        # id_t RM_GetIdOfRoadFromIndex(unsigned int index);
        self.lib.RM_GetIdOfRoadFromIndex.argtypes = [ctypes.c_uint]
        self.lib.RM_GetIdOfRoadFromIndex.restype = id_t

        # float RM_GetRoadLength(id_t id);
        self.lib.RM_GetRoadLength.argtypes = [id_t]
        self.lib.RM_GetRoadLength.restype = ctypes.c_double

        # const char* RM_GetRoadIdString(id_t road_id);
        self.lib.RM_GetRoadIdString.argtypes = [id_t]
        self.lib.RM_GetRoadIdString.restype = ctypes.c_char_p

        # id_t RM_GetRoadIdFromString(const char* road_id_str);
        self.lib.RM_GetRoadIdFromString.argtypes = [ctypes.c_char_p]
        self.lib.RM_GetRoadIdFromString.restype = id_t

        # const char* RM_GetJunctionIdString(id_t junction_id);
        self.lib.RM_GetJunctionIdString.argtypes = [id_t]
        self.lib.RM_GetJunctionIdString.restype = ctypes.c_char_p

        # id_t RM_GetJunctionIdFromString(const char* junction_id_str);
        self.lib.RM_GetJunctionIdFromString.argtypes = [ctypes.c_char_p]
        self.lib.RM_GetJunctionIdFromString.restype = id_t

        # --- Lane Info ---

        # int RM_GetRoadNumberOfLanes(id_t roadId, float s, int type_mask);
        self.lib.RM_GetRoadNumberOfLanes.argtypes = [id_t, ctypes.c_double, ctypes.c_int]
        self.lib.RM_GetRoadNumberOfLanes.restype = ctypes.c_int

        # int RM_GetLaneIdByIndex(id_t roadId, int laneIndex, float s, int type_mask, int* lane_id);
        self.lib.RM_GetLaneIdByIndex.argtypes = [id_t, ctypes.c_int, ctypes.c_double, ctypes.c_int, ctypes.POINTER(ctypes.c_int)]
        self.lib.RM_GetLaneIdByIndex.restype = ctypes.c_int

        # int RM_GetRoadNumberOfDrivableLanes(id_t roadId, float s);
        self.lib.RM_GetRoadNumberOfDrivableLanes.argtypes = [id_t, ctypes.c_double]
        self.lib.RM_GetRoadNumberOfDrivableLanes.restype = ctypes.c_int

        # int RM_GetDrivableLaneIdByIndex(id_t roadId, int laneIndex, float s, int* lane_id);
        self.lib.RM_GetDrivableLaneIdByIndex.argtypes = [id_t, ctypes.c_int, ctypes.c_double, ctypes.POINTER(ctypes.c_int)]
        self.lib.RM_GetDrivableLaneIdByIndex.restype = ctypes.c_int

        # int RM_GetNumberOfRoadsOverlapping(int handle);
        self.lib.RM_GetNumberOfRoadsOverlapping.argtypes = [ctypes.c_int]
        self.lib.RM_GetNumberOfRoadsOverlapping.restype = ctypes.c_int

        # id_t RM_GetOverlappingRoadId(int handle, unsigned int index);
        self.lib.RM_GetOverlappingRoadId.argtypes = [ctypes.c_int, ctypes.c_uint]
        self.lib.RM_GetOverlappingRoadId.restype = id_t

        # --- Position Setting ---

        # int RM_SetLanePosition(int handle, id_t roadId, int laneId, float laneOffset, float s, bool align);
        self.lib.RM_SetLanePosition.argtypes = [ctypes.c_int, id_t, ctypes.c_int, ctypes.c_double, ctypes.c_double, ctypes.c_bool]
        self.lib.RM_SetLanePosition.restype = ctypes.c_int

        # int RM_SetRoadPosition(int handle, id_t roadId, float s, float t, bool align);
        self.lib.RM_SetRoadPosition.argtypes = [ctypes.c_int, id_t, ctypes.c_double, ctypes.c_double, ctypes.c_bool]
        self.lib.RM_SetRoadPosition.restype = ctypes.c_int

        # int RM_SetS(int handle, float s);
        self.lib.RM_SetS.argtypes = [ctypes.c_int, ctypes.c_double]
        self.lib.RM_SetS.restype = ctypes.c_int

        # int RM_SetWorldPosition(int handle, float x, float y, float z, float h, float p, float r);
        self.lib.RM_SetWorldPosition.argtypes = [ctypes.c_int, ctypes.c_double, ctypes.c_double, ctypes.c_double, ctypes.c_double, ctypes.c_double, ctypes.c_double]
        self.lib.RM_SetWorldPosition.restype = ctypes.c_int

        # int RM_SetWorldXYHPosition(int handle, float x, float y, float h);
        self.lib.RM_SetWorldXYHPosition.argtypes = [ctypes.c_int, ctypes.c_double, ctypes.c_double, ctypes.c_double]
        self.lib.RM_SetWorldXYHPosition.restype = ctypes.c_int

        # int RM_SetWorldXYZHPosition(int handle, float x, float y, float z, float h);
        self.lib.RM_SetWorldXYZHPosition.argtypes = [ctypes.c_int, ctypes.c_double, ctypes.c_double, ctypes.c_double, ctypes.c_double]
        self.lib.RM_SetWorldXYZHPosition.restype = ctypes.c_int

        # int RM_SetWorldPositionMode(int handle, float x, float y, float z, float h, float p, float r, int mode);
        self.lib.RM_SetWorldPositionMode.argtypes = [ctypes.c_int, ctypes.c_double, ctypes.c_double, ctypes.c_double, ctypes.c_double, ctypes.c_double, ctypes.c_double, ctypes.c_int]
        self.lib.RM_SetWorldPositionMode.restype = ctypes.c_int

        # int RM_SetH(int handle, float h);
        self.lib.RM_SetH.argtypes = [ctypes.c_int, ctypes.c_double]
        self.lib.RM_SetH.restype = ctypes.c_int

        # int RM_SetHMode(int handle, float h, int mode);
        self.lib.RM_SetHMode.argtypes = [ctypes.c_int, ctypes.c_double, ctypes.c_int]
        self.lib.RM_SetHMode.restype = ctypes.c_int

        # int RM_SetRoadId(int handle, id_t roadId);
        self.lib.RM_SetRoadId.argtypes = [ctypes.c_int, id_t]
        self.lib.RM_SetRoadId.restype = ctypes.c_int

        # int RM_PositionMoveForward(int handle, float dist, float junctionSelectorAngle);
        self.lib.RM_PositionMoveForward.argtypes = [ctypes.c_int, ctypes.c_double, ctypes.c_double]
        self.lib.RM_PositionMoveForward.restype = ctypes.c_int

        # --- Query ---

        # int RM_GetPositionData(int handle, RM_PositionData* data);
        self.lib.RM_GetPositionData.argtypes = [ctypes.c_int, ctypes.POINTER(RM_PositionData)]
        self.lib.RM_GetPositionData.restype = ctypes.c_int

        # float RM_GetSpeedLimit(int handle);
        self.lib.RM_GetSpeedLimit.argtypes = [ctypes.c_int]
        self.lib.RM_GetSpeedLimit.restype = ctypes.c_double

        # int RM_GetLaneInfo(int handle, float lookahead_distance, RM_RoadLaneInfo* data, int lookAheadMode, bool inRoadDrivingDirection);
        self.lib.RM_GetLaneInfo.argtypes = [ctypes.c_int, ctypes.c_double, ctypes.POINTER(RM_RoadLaneInfo), ctypes.c_int, ctypes.c_bool]
        self.lib.RM_GetLaneInfo.restype = ctypes.c_int

        # int RM_GetProbeInfo(int handle, float lookahead_distance, RM_RoadProbeInfo* data, int lookAheadMode, bool inRoadDrivingDirection);
        self.lib.RM_GetProbeInfo.argtypes = [ctypes.c_int, ctypes.c_double, ctypes.POINTER(RM_RoadProbeInfo), ctypes.c_int, ctypes.c_bool]
        self.lib.RM_GetProbeInfo.restype = ctypes.c_int

        # int RM_GetLaneWidth(int handle, int lane_id, float* width);
        self.lib.RM_GetLaneWidth.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.POINTER(ctypes.c_double)]
        self.lib.RM_GetLaneWidth.restype = ctypes.c_int

        # int RM_GetLaneWidthByRoadId(id_t road_id, int lane_id, float s, float* width);
        self.lib.RM_GetLaneWidthByRoadId.argtypes = [id_t, ctypes.c_int, ctypes.c_double, ctypes.POINTER(ctypes.c_double)]
        self.lib.RM_GetLaneWidthByRoadId.restype = ctypes.c_int

        # int RM_GetLaneType(int handle, int lane_id);
        self.lib.RM_GetLaneType.argtypes = [ctypes.c_int, ctypes.c_int]
        self.lib.RM_GetLaneType.restype = ctypes.c_int

        # int RM_GetInLaneType(int handle);
        self.lib.RM_GetInLaneType.argtypes = [ctypes.c_int]
        self.lib.RM_GetInLaneType.restype = ctypes.c_int

        # int RM_GetLaneTypeByRoadId(id_t road_id, int lane_id, float s);
        self.lib.RM_GetLaneTypeByRoadId.argtypes = [id_t, ctypes.c_int, ctypes.c_double]
        self.lib.RM_GetLaneTypeByRoadId.restype = ctypes.c_int

        # int RM_SubtractAFromB(int handleA, int handleB, RM_PositionDiff* pos_diff);
        self.lib.RM_SubtractAFromB.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.POINTER(RM_PositionDiff)]
        self.lib.RM_SubtractAFromB.restype = ctypes.c_int

        # --- Road Signs ---

        # int RM_GetNumberOfRoadSigns(id_t road_id);
        self.lib.RM_GetNumberOfRoadSigns.argtypes = [id_t]
        self.lib.RM_GetNumberOfRoadSigns.restype = ctypes.c_int

        # int RM_GetRoadSign(id_t road_id, unsigned int index, RM_RoadSign* road_sign);
        self.lib.RM_GetRoadSign.argtypes = [id_t, ctypes.c_uint, ctypes.POINTER(RM_RoadSign)]
        self.lib.RM_GetRoadSign.restype = ctypes.c_int

        # int RM_GetNumberOfRoadSignValidityRecords(id_t road_id, unsigned int index);
        self.lib.RM_GetNumberOfRoadSignValidityRecords.argtypes = [id_t, ctypes.c_uint]
        self.lib.RM_GetNumberOfRoadSignValidityRecords.restype = ctypes.c_int

        # int RM_GetRoadSignValidityRecord(id_t road_id, unsigned int signIndex, unsigned int validityIndex, RM_RoadObjValidity* validity);
        self.lib.RM_GetRoadSignValidityRecord.argtypes = [id_t, ctypes.c_uint, ctypes.c_uint, ctypes.POINTER(RM_RoadObjValidity)]
        self.lib.RM_GetRoadSignValidityRecord.restype = ctypes.c_int

        # --- GeoReference ---

        # int RM_GetOpenDriveGeoReference(RM_GeoReference* rmGeoReference);
        self.lib.RM_GetOpenDriveGeoReference.argtypes = [ctypes.POINTER(RM_GeoReference)]
        self.lib.RM_GetOpenDriveGeoReference.restype = ctypes.c_int

        # --- Options ---

        # int RM_SetOption(const char* name);
        self.lib.RM_SetOption.argtypes = [ctypes.c_char_p]
        self.lib.RM_SetOption.restype = ctypes.c_int

        # int RM_UnsetOption(const char* name);
        self.lib.RM_UnsetOption.argtypes = [ctypes.c_char_p]
        self.lib.RM_UnsetOption.restype = ctypes.c_int

        # int RM_SetOptionValue(const char* name, const char* value);
        self.lib.RM_SetOptionValue.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
        self.lib.RM_SetOptionValue.restype = ctypes.c_int

        # int RM_SetOptionPersistent(const char* name);
        self.lib.RM_SetOptionPersistent.argtypes = [ctypes.c_char_p]
        self.lib.RM_SetOptionPersistent.restype = ctypes.c_int

        # int RM_SetOptionValuePersistent(const char* name, const char* value);
        self.lib.RM_SetOptionValuePersistent.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
        self.lib.RM_SetOptionValuePersistent.restype = ctypes.c_int

        # const char* RM_GetOptionValue(const char* name);
        self.lib.RM_GetOptionValue.argtypes = [ctypes.c_char_p]
        self.lib.RM_GetOptionValue.restype = ctypes.c_char_p

        # bool RM_GetOptionSet(const char* name);
        self.lib.RM_GetOptionSet.argtypes = [ctypes.c_char_p]
        self.lib.RM_GetOptionSet.restype = ctypes.c_bool

    # =========================================================================
    # Initialization / Management
    # =========================================================================

    def Init(self, odr_filename):
        """Initialize RoadManager with ODR file."""
        return self.lib.RM_Init(odr_filename.encode('utf-8'))

    def InitWithString(self, odr_xml_string):
        """Initialize RoadManager with OpenDRIVE XML string."""
        return self.lib.RM_InitWithString(odr_xml_string.encode('utf-8'))

    def Close(self):
        """Close RoadManager."""
        return self.lib.RM_Close()

    def SetLogFilePath(self, log_file_path):
        """Set log file path. Must be called before Init()."""
        self.lib.RM_SetLogFilePath(log_file_path.encode('utf-8'))

    def CreatePosition(self):
        """Create a position object. Returns handle >= 0 or -1 on error."""
        return self.lib.RM_CreatePosition()

    def GetNrOfPositions(self):
        """Get the number of created position objects."""
        return self.lib.RM_GetNrOfPositions()

    def DeletePosition(self, handle):
        """Delete position object. Set handle=-1 to delete all."""
        return self.lib.RM_DeletePosition(handle)

    def CopyPosition(self, handle):
        """Copy a position object. Returns handle to new position or -1."""
        return self.lib.RM_CopyPosition(handle)

    # =========================================================================
    # Position Mode
    # =========================================================================

    def SetObjectPositionMode(self, handle, pos_mode_type, mode):
        """
        Set how position object aligns to road (Z, H, P, R).

        Args:
            handle: Position handle
            pos_mode_type: SET(0) or UPDATE(1)
            mode: Bitmask from RM_PositionMode (e.g. RM_Z_REL | RM_H_ABS)
        """
        self.lib.RM_SetObjectPositionMode(handle, pos_mode_type, mode)

    def SetObjectPositionModeDefault(self, handle, pos_mode_type):
        """
        Reset position mode to default.

        Args:
            handle: Position handle
            pos_mode_type: SET(0) or UPDATE(1)
        """
        self.lib.RM_SetObjectPositionModeDefault(handle, pos_mode_type)

    def SetSnapLaneTypes(self, handle, lane_types):
        """
        Specify which lane types the position object snaps to.

        Args:
            handle: Position handle
            lane_types: Bitmask (e.g. ANY_DRIVING=1966594, ANY_ROAD=1966734, ANY=-1)
        """
        return self.lib.RM_SetSnapLaneTypes(handle, lane_types)

    def SetLockOnLane(self, handle, mode):
        """
        Keep lane ID regardless of lateral position (True) or snap to closest (False).

        Args:
            handle: Position handle
            mode: True=keep lane, False=snap to closest
        """
        return self.lib.RM_SetLockOnLane(handle, mode)

    # =========================================================================
    # Road Info
    # =========================================================================

    def GetNumberOfRoads(self):
        """Get total number of roads in the loaded OpenDRIVE."""
        return self.lib.RM_GetNumberOfRoads()

    def GetSpeedUnit(self):
        """
        Get the speed unit from OpenDRIVE road type.

        Returns:
            -1=Error, 0=Undefined, 1=km/h, 2=m/s, 3=mph
        """
        return self.lib.RM_GetSpeedUnit()

    def GetIdOfRoadFromIndex(self, index):
        """Get road ID by index."""
        return self.lib.RM_GetIdOfRoadFromIndex(index)

    def GetRoadLength(self, road_id):
        """Get road length in meters. Returns 0.0 if not found."""
        return self.lib.RM_GetRoadLength(road_id)

    def GetRoadIdString(self, road_id):
        """Get original string ID for a road."""
        result = self.lib.RM_GetRoadIdString(road_id)
        return result.decode('utf-8') if result else ""

    def GetRoadIdFromString(self, road_id_str):
        """Get integer road ID from string ID. Returns -1 if not found."""
        return self.lib.RM_GetRoadIdFromString(road_id_str.encode('utf-8'))

    def GetJunctionIdString(self, junction_id):
        """Get original string ID for a junction."""
        result = self.lib.RM_GetJunctionIdString(junction_id)
        return result.decode('utf-8') if result else ""

    def GetJunctionIdFromString(self, junction_id_str):
        """Get integer junction ID from string ID. Returns -1 if not found."""
        return self.lib.RM_GetJunctionIdFromString(junction_id_str.encode('utf-8'))

    # =========================================================================
    # Lane Info
    # =========================================================================

    def GetRoadNumberOfLanes(self, road_id, s, type_mask=-1):
        """
        Get number of lanes of given type at specified road position.

        Args:
            road_id: Road ID
            s: Distance along road
            type_mask: Lane type bitmask (-1=any, 1966594=any drivable)
        """
        return self.lib.RM_GetRoadNumberOfLanes(road_id, s, type_mask)

    def GetLaneIdByIndex(self, road_id, lane_index, s, type_mask=-1):
        """
        Get lane ID by index and type.

        Returns:
            (result, lane_id): result=0 on success
        """
        lane_id = ctypes.c_int()
        res = self.lib.RM_GetLaneIdByIndex(road_id, lane_index, s, type_mask, ctypes.byref(lane_id))
        return res, lane_id.value

    def GetRoadNumberOfDrivableLanes(self, road_id, s):
        """Get number of drivable lanes at specified road position."""
        return self.lib.RM_GetRoadNumberOfDrivableLanes(road_id, s)

    def GetDrivableLaneIdByIndex(self, road_id, lane_index, s):
        """
        Get drivable lane ID by index.

        Returns:
            (result, lane_id): result=0 on success
        """
        lane_id = ctypes.c_int()
        res = self.lib.RM_GetDrivableLaneIdByIndex(road_id, lane_index, s, ctypes.byref(lane_id))
        return res, lane_id.value

    def GetNumberOfRoadsOverlapping(self, handle):
        """Get number of roads overlapping the given position."""
        return self.lib.RM_GetNumberOfRoadsOverlapping(handle)

    def GetOverlappingRoadId(self, handle, index):
        """Get overlapping road ID by index."""
        return self.lib.RM_GetOverlappingRoadId(handle, index)

    def GetLaneWidth(self, handle, lane_id):
        """
        Get width of lane at current longitudinal position.

        Returns:
            (result, width): result=0 on success
        """
        width = ctypes.c_double()
        res = self.lib.RM_GetLaneWidth(handle, lane_id, ctypes.byref(width))
        return res, width.value

    def GetLaneWidthByRoadId(self, road_id, lane_id, s):
        """
        Get width of lane at specified road and position.

        Returns:
            (result, width): result=0 on success
        """
        width = ctypes.c_double()
        res = self.lib.RM_GetLaneWidthByRoadId(road_id, lane_id, s, ctypes.byref(width))
        return res, width.value

    def GetLaneType(self, handle, lane_id):
        """Get type of lane at current longitudinal position."""
        return self.lib.RM_GetLaneType(handle, lane_id)

    def GetInLaneType(self, handle):
        """Get lane type that the position object is currently in."""
        return self.lib.RM_GetInLaneType(handle)

    def GetLaneTypeByRoadId(self, road_id, lane_id, s):
        """Get lane type at specified road and position."""
        return self.lib.RM_GetLaneTypeByRoadId(road_id, lane_id, s)

    # =========================================================================
    # Position Setting
    # =========================================================================

    def SetLanePosition(self, handle, road_id, lane_id, lane_offset, s, align=True):
        """
        Set position from road coordinates (lane-based).

        Args:
            handle: Position handle
            road_id: Road ID
            lane_id: Lane ID
            lane_offset: Lateral offset from lane center (meters)
            s: Distance along road (meters)
            align: Align to road direction
        """
        return self.lib.RM_SetLanePosition(handle, road_id, lane_id, lane_offset, s, align)

    def SetRoadPosition(self, handle, road_id, s, t, align=True):
        """
        Set position from road s/t coordinates.

        Args:
            handle: Position handle
            road_id: Road ID
            s: Distance along road
            t: Lateral position from reference line
            align: Align to road direction
        """
        return self.lib.RM_SetRoadPosition(handle, road_id, s, t, align)

    def SetS(self, handle, s):
        """Set s (distance) part of lane position."""
        return self.lib.RM_SetS(handle, s)

    def SetWorldPosition(self, handle, x, y, z, h, p, r):
        """
        Set position from world coordinates (x, y, z, heading, pitch, roll).
        Use float('nan') to skip a parameter.
        """
        return self.lib.RM_SetWorldPosition(handle, x, y, z, h, p, r)

    def SetWorldXYHPosition(self, handle, x, y, h):
        """Set position from world X, Y and Heading."""
        return self.lib.RM_SetWorldXYHPosition(handle, x, y, h)

    def SetWorldXYZHPosition(self, handle, x, y, z, h):
        """Set position from world X, Y, Z and Heading."""
        return self.lib.RM_SetWorldXYZHPosition(handle, x, y, z, h)

    def SetWorldPositionMode(self, handle, x, y, z, h, p, r, mode):
        """
        Set position from world coordinates with explicit position mode.

        Args:
            handle: Position handle
            x, y, z: World coordinates
            h, p, r: Heading, pitch, roll
            mode: Bitmask from RM_PositionMode
        """
        return self.lib.RM_SetWorldPositionMode(handle, x, y, z, h, p, r, mode)

    def SetH(self, handle, h):
        """Set heading, mode given by current setting."""
        return self.lib.RM_SetH(handle, h)

    def SetHMode(self, handle, h, mode):
        """
        Set heading with explicit mode.

        Args:
            handle: Position handle
            h: Heading value
            mode: RM_H_ABS or RM_H_REL
        """
        return self.lib.RM_SetHMode(handle, h, mode)

    def SetRoadId(self, handle, road_id):
        """Change road belonging, keeping actual x,y location."""
        return self.lib.RM_SetRoadId(handle, road_id)

    def PositionMoveForward(self, handle, dist, junction_selector_angle=-1.0):
        """
        Move position forward along road.

        Args:
            handle: Position handle
            dist: Distance (meters) to move
            junction_selector_angle: Direction at junction (0=straight, pi/2=right, pi=U-turn, 3pi/2=left, -1=random)
        """
        return self.lib.RM_PositionMoveForward(handle, dist, junction_selector_angle)

    # =========================================================================
    # Query
    # =========================================================================

    def GetPositionData(self, handle):
        """Get position data."""
        data = RM_PositionData()
        res = self.lib.RM_GetPositionData(handle, ctypes.byref(data))
        return res, data

    def GetSpeedLimit(self, handle):
        """Get current speed limit (m/s) at position."""
        return self.lib.RM_GetSpeedLimit(handle)

    def GetLaneInfo(self, handle, lookahead_distance=0.0, look_ahead_mode=0, in_road_driving_direction=True):
        """
        Get lane info (including offset).
        look_ahead_mode: 0=LaneCenter, 1=RoadCenter, 2=CurrentOffset
        """
        data = RM_RoadLaneInfo()
        res = self.lib.RM_GetLaneInfo(handle, lookahead_distance, ctypes.byref(data), look_ahead_mode, in_road_driving_direction)
        return res, data

    def GetProbeInfo(self, handle, lookahead_distance=0.0, look_ahead_mode=0, in_road_driving_direction=True):
        """
        Get road probe info (lane info + relative position from current position).
        look_ahead_mode: 0=LaneCenter, 1=RoadCenter, 2=CurrentOffset
        """
        data = RM_RoadProbeInfo()
        res = self.lib.RM_GetProbeInfo(handle, lookahead_distance, ctypes.byref(data), look_ahead_mode, in_road_driving_direction)
        return res, data

    def SubtractAFromB(self, handle_a, handle_b):
        """
        Find difference between two positions (delta s, delta t, delta laneId).

        Returns:
            (result, pos_diff): result=0 on success, -2 if no route found
        """
        pos_diff = RM_PositionDiff()
        res = self.lib.RM_SubtractAFromB(handle_a, handle_b, ctypes.byref(pos_diff))
        return res, pos_diff

    # =========================================================================
    # Road Signs
    # =========================================================================

    def GetNumberOfRoadSigns(self, road_id):
        """Get number of road signs along specified road."""
        return self.lib.RM_GetNumberOfRoadSigns(road_id)

    def GetRoadSign(self, road_id, index):
        """
        Get road sign information by index.

        Returns:
            (result, road_sign): result=0 on success
        """
        road_sign = RM_RoadSign()
        res = self.lib.RM_GetRoadSign(road_id, index, ctypes.byref(road_sign))
        return res, road_sign

    def GetNumberOfRoadSignValidityRecords(self, road_id, index):
        """Get number of lane validity records of specified road sign."""
        return self.lib.RM_GetNumberOfRoadSignValidityRecords(road_id, index)

    def GetRoadSignValidityRecord(self, road_id, sign_index, validity_index):
        """
        Get validity record of specified road sign.

        Returns:
            (result, validity): result=0 on success
        """
        validity = RM_RoadObjValidity()
        res = self.lib.RM_GetRoadSignValidityRecord(road_id, sign_index, validity_index, ctypes.byref(validity))
        return res, validity

    # =========================================================================
    # GeoReference
    # =========================================================================

    def GetOpenDriveGeoReference(self):
        """
        Get the OpenDRIVE geo reference.

        Returns:
            (result, geo_ref): result=0 on success
        """
        geo_ref = RM_GeoReference()
        res = self.lib.RM_GetOpenDriveGeoReference(ctypes.byref(geo_ref))
        return res, geo_ref

    # =========================================================================
    # Options
    # =========================================================================

    def SetOption(self, name):
        """Set option (unset on next scenario run)."""
        return self.lib.RM_SetOption(name.encode('utf-8'))

    def UnsetOption(self, name):
        """Unset option."""
        return self.lib.RM_UnsetOption(name.encode('utf-8'))

    def SetOptionValue(self, name, value):
        """Set option value (unset on next scenario run)."""
        return self.lib.RM_SetOptionValue(name.encode('utf-8'), value.encode('utf-8'))

    def SetOptionPersistent(self, name):
        """Set option persistently (remains until lib is reloaded)."""
        return self.lib.RM_SetOptionPersistent(name.encode('utf-8'))

    def SetOptionValuePersistent(self, name, value):
        """Set option value persistently (remains until lib is reloaded)."""
        return self.lib.RM_SetOptionValuePersistent(name.encode('utf-8'), value.encode('utf-8'))

    def GetOptionValue(self, name):
        """Get option value."""
        result = self.lib.RM_GetOptionValue(name.encode('utf-8'))
        return result.decode('utf-8') if result else ""

    def GetOptionSet(self, name):
        """Check if option is set."""
        return self.lib.RM_GetOptionSet(name.encode('utf-8'))
