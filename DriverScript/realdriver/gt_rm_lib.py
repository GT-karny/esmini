"""
GT_esminiRMLib Python Wrapper

This module provides Python bindings for the GT_esminiRMLib C library,
which extends esminiRMLib with road connection query functions.
"""

import ctypes
import os
from dataclasses import dataclass
from typing import Optional, List, Tuple


# Constants matching GT_esminiRMLib.hpp
GT_RM_LINK_TYPE_PREDECESSOR = 0
GT_RM_LINK_TYPE_SUCCESSOR = 1

GT_RM_ELEMENT_TYPE_UNKNOWN = 0
GT_RM_ELEMENT_TYPE_ROAD = 1
GT_RM_ELEMENT_TYPE_JUNCTION = 2

GT_RM_CONTACT_POINT_UNKNOWN = 0
GT_RM_CONTACT_POINT_START = 1
GT_RM_CONTACT_POINT_END = 2


@dataclass
class RoadLinkInfo:
    """Road link information."""
    element_id: int
    element_type: int  # GT_RM_ELEMENT_TYPE_*
    contact_point: int  # GT_RM_CONTACT_POINT_*

    @property
    def is_road(self) -> bool:
        return self.element_type == GT_RM_ELEMENT_TYPE_ROAD

    @property
    def is_junction(self) -> bool:
        return self.element_type == GT_RM_ELEMENT_TYPE_JUNCTION

    @property
    def contact_point_name(self) -> str:
        if self.contact_point == GT_RM_CONTACT_POINT_START:
            return 'start'
        elif self.contact_point == GT_RM_CONTACT_POINT_END:
            return 'end'
        return 'unknown'


@dataclass
class JunctionConnection:
    """Junction connection information."""
    incoming_road_id: int
    connecting_road_id: int
    contact_point: int  # GT_RM_CONTACT_POINT_*


@dataclass
class RoadSignalInfo:
    """Road signal information."""
    id: int
    s: float
    t: float
    x: float
    y: float
    z: float
    h: float
    p: float
    r: float
    type: str
    subtype: str
    country: str
    value: float
    unit: str
    text: str
    is_dynamic: bool
    height: float
    width: float


# C structure definitions
class GT_RM_RoadLinkInfo(ctypes.Structure):
    _fields_ = [
        ("elementId", ctypes.c_uint32),
        ("elementType", ctypes.c_int),
        ("contactPoint", ctypes.c_int)
    ]


class GT_RM_JunctionConnection(ctypes.Structure):
    _fields_ = [
        ("incomingRoadId", ctypes.c_uint32),
        ("connectingRoadId", ctypes.c_uint32),
        ("contactPoint", ctypes.c_int)
    ]


class GT_RM_RoadSignalInfo(ctypes.Structure):
    _fields_ = [
        ("id", ctypes.c_int),
        ("s", ctypes.c_double),
        ("t", ctypes.c_double),
        ("x", ctypes.c_double),
        ("y", ctypes.c_double),
        ("z", ctypes.c_double),
        ("h", ctypes.c_double),
        ("p", ctypes.c_double),
        ("r", ctypes.c_double),
        ("type", ctypes.c_char * 64),
        ("subtype", ctypes.c_char * 64),
        ("country", ctypes.c_char * 64),
        ("value", ctypes.c_double),
        ("unit", ctypes.c_char * 64),
        ("text", ctypes.c_char * 128),
        ("isDynamic", ctypes.c_bool),
        ("height", ctypes.c_double),
        ("width", ctypes.c_double)
    ]


class GTEsminiRMLib:
    """
    Python wrapper for GT_esminiRMLib.

    This class provides road connection query functions that extend
    the standard esminiRMLib functionality.

    Note: Requires that esminiRMLib is already initialized (RM_Init called)
    before using these functions.
    """

    def __init__(self, lib_path: str):
        """
        Initialize GTEsminiRMLib wrapper.

        Args:
            lib_path: Path to GT_esminiLib.dll (which includes GT_esminiRMLib functions)
        """
        if not os.path.exists(lib_path):
            raise FileNotFoundError(f"GT_esminiLib not found at: {lib_path}")

        try:
            self.lib = ctypes.CDLL(lib_path)
            self._setup_signatures()
        except OSError as e:
            print(f"Failed to load GT_esminiLib: {e}")
            raise

    def _setup_signatures(self):
        """Setup function signatures for ctypes."""
        # GT_RM_Init
        self.lib.GT_RM_Init.argtypes = [ctypes.c_char_p]
        self.lib.GT_RM_Init.restype = ctypes.c_int

        # GT_RM_Close
        self.lib.GT_RM_Close.argtypes = []
        self.lib.GT_RM_Close.restype = None

        # GT_RM_GetRoadSuccessor
        self.lib.GT_RM_GetRoadSuccessor.argtypes = [
            ctypes.c_uint32,
            ctypes.POINTER(GT_RM_RoadLinkInfo)
        ]
        self.lib.GT_RM_GetRoadSuccessor.restype = ctypes.c_int

        # GT_RM_GetRoadPredecessor
        self.lib.GT_RM_GetRoadPredecessor.argtypes = [
            ctypes.c_uint32,
            ctypes.POINTER(GT_RM_RoadLinkInfo)
        ]
        self.lib.GT_RM_GetRoadPredecessor.restype = ctypes.c_int

        # GT_RM_GetJunctionConnectionCount
        self.lib.GT_RM_GetJunctionConnectionCount.argtypes = [ctypes.c_uint32]
        self.lib.GT_RM_GetJunctionConnectionCount.restype = ctypes.c_int

        # GT_RM_GetJunctionConnection
        self.lib.GT_RM_GetJunctionConnection.argtypes = [
            ctypes.c_uint32,
            ctypes.c_int,
            ctypes.POINTER(GT_RM_JunctionConnection)
        ]
        self.lib.GT_RM_GetJunctionConnection.restype = ctypes.c_int

        # GT_RM_GetJunctionConnectionsFromRoad
        self.lib.GT_RM_GetJunctionConnectionsFromRoad.argtypes = [
            ctypes.c_uint32,
            ctypes.c_uint32
        ]
        self.lib.GT_RM_GetJunctionConnectionsFromRoad.restype = ctypes.c_int

        # GT_RM_GetJunctionConnectionFromRoadByIndex
        self.lib.GT_RM_GetJunctionConnectionFromRoadByIndex.argtypes = [
            ctypes.c_uint32,
            ctypes.c_uint32,
            ctypes.c_int,
            ctypes.POINTER(ctypes.c_uint32)
        ]
        self.lib.GT_RM_GetJunctionConnectionFromRoadByIndex.restype = ctypes.c_int

        # GT_RM_GetNumRoads
        self.lib.GT_RM_GetNumRoads.argtypes = []
        self.lib.GT_RM_GetNumRoads.restype = ctypes.c_int

        # GT_RM_GetRoadIdByIndex
        self.lib.GT_RM_GetRoadIdByIndex.argtypes = [ctypes.c_int]
        self.lib.GT_RM_GetRoadIdByIndex.restype = ctypes.c_uint32

        # GT_RM_GetRoadLength
        self.lib.GT_RM_GetRoadLength.argtypes = [ctypes.c_uint32]
        self.lib.GT_RM_GetRoadLength.restype = ctypes.c_double

        # GT_RM_GetRoadSignalCount
        self.lib.GT_RM_GetRoadSignalCount.argtypes = [ctypes.c_uint32]
        self.lib.GT_RM_GetRoadSignalCount.restype = ctypes.c_int

        # GT_RM_GetRoadSignal
        self.lib.GT_RM_GetRoadSignal.argtypes = [
            ctypes.c_uint32,
            ctypes.c_int,
            ctypes.POINTER(GT_RM_RoadSignalInfo)
        ]
        self.lib.GT_RM_GetRoadSignal.restype = ctypes.c_int

    def init(self, odr_path: str) -> int:
        """
        Initialize GT_esminiRMLib with an OpenDRIVE file.

        Args:
            odr_path: Path to the OpenDRIVE (.xodr) file

        Returns:
            0 on success, -1 on failure
        """
        result = self.lib.GT_RM_Init(odr_path.encode('utf-8'))
        if result == 0:
            print(f"[INFO] GT_esminiRMLib: Loaded {odr_path}")
        else:
            print(f"[ERROR] GT_esminiRMLib: Failed to load {odr_path}")
        return result

    def close(self):
        """Close GT_esminiRMLib and release resources."""
        self.lib.GT_RM_Close()

    def get_road_successor(self, road_id: int) -> Optional[RoadLinkInfo]:
        """
        Get the successor link of a road.

        Args:
            road_id: The road ID

        Returns:
            RoadLinkInfo if successor exists, None otherwise
        """
        link_info = GT_RM_RoadLinkInfo()
        result = self.lib.GT_RM_GetRoadSuccessor(road_id, ctypes.byref(link_info))

        if result == 0:
            return RoadLinkInfo(
                element_id=link_info.elementId,
                element_type=link_info.elementType,
                contact_point=link_info.contactPoint
            )
        return None

    def get_road_predecessor(self, road_id: int) -> Optional[RoadLinkInfo]:
        """
        Get the predecessor link of a road.

        Args:
            road_id: The road ID

        Returns:
            RoadLinkInfo if predecessor exists, None otherwise
        """
        link_info = GT_RM_RoadLinkInfo()
        result = self.lib.GT_RM_GetRoadPredecessor(road_id, ctypes.byref(link_info))

        if result == 0:
            return RoadLinkInfo(
                element_id=link_info.elementId,
                element_type=link_info.elementType,
                contact_point=link_info.contactPoint
            )
        return None

    def get_junction_connection_count(self, junction_id: int) -> int:
        """
        Get the number of connections in a junction.

        Args:
            junction_id: The junction ID

        Returns:
            Number of connections, or -1 if junction not found
        """
        return self.lib.GT_RM_GetJunctionConnectionCount(junction_id)

    def get_junction_connection(self, junction_id: int, index: int) -> Optional[JunctionConnection]:
        """
        Get a junction connection by index.

        Args:
            junction_id: The junction ID
            index: Connection index (0-based)

        Returns:
            JunctionConnection if found, None otherwise
        """
        conn = GT_RM_JunctionConnection()
        result = self.lib.GT_RM_GetJunctionConnection(junction_id, index, ctypes.byref(conn))

        if result == 0:
            return JunctionConnection(
                incoming_road_id=conn.incomingRoadId,
                connecting_road_id=conn.connectingRoadId,
                contact_point=conn.contactPoint
            )
        return None

    def get_junction_connections(self, junction_id: int) -> List[JunctionConnection]:
        """
        Get all connections in a junction.

        Args:
            junction_id: The junction ID

        Returns:
            List of JunctionConnection objects
        """
        count = self.get_junction_connection_count(junction_id)
        if count <= 0:
            return []

        connections = []
        for i in range(count):
            conn = self.get_junction_connection(junction_id, i)
            if conn:
                connections.append(conn)
        return connections

    def get_junction_connections_from_road(self, junction_id: int,
                                            incoming_road_id: int) -> List[int]:
        """
        Get all connecting road IDs from a specific incoming road through a junction.

        Args:
            junction_id: The junction ID
            incoming_road_id: The incoming road ID

        Returns:
            List of connecting road IDs
        """
        count = self.lib.GT_RM_GetJunctionConnectionsFromRoad(junction_id, incoming_road_id)
        if count <= 0:
            return []

        connecting_roads = []
        for i in range(count):
            road_id = ctypes.c_uint32()
            result = self.lib.GT_RM_GetJunctionConnectionFromRoadByIndex(
                junction_id, incoming_road_id, i, ctypes.byref(road_id)
            )
            if result == 0:
                connecting_roads.append(road_id.value)
        return connecting_roads

    def get_junction_connections_from_road_with_contact(self, junction_id: int,
                                                         incoming_road_id: int) -> List[Tuple[int, int]]:
        """
        Get all connecting road IDs and their contactPoints from a specific incoming road through a junction.

        Args:
            junction_id: The junction ID
            incoming_road_id: The incoming road ID

        Returns:
            List of (connecting_road_id, contact_point) tuples
            contact_point: GT_RM_CONTACT_POINT_START(1) or GT_RM_CONTACT_POINT_END(2)
        """
        # Use the full connection info to get contactPoint
        all_connections = self.get_junction_connections(junction_id)
        result = []
        for conn in all_connections:
            if conn.incoming_road_id == incoming_road_id:
                result.append((conn.connecting_road_id, conn.contact_point))
        return result

    def get_num_roads(self) -> int:
        """
        Get the number of roads in the loaded OpenDRIVE.

        Returns:
            Number of roads, or -1 if no map loaded
        """
        return self.lib.GT_RM_GetNumRoads()

    def get_road_id_by_index(self, index: int) -> Optional[int]:
        """
        Get road ID by index.

        Args:
            index: Road index (0-based)

        Returns:
            Road ID, or None if index out of range
        """
        road_id = self.lib.GT_RM_GetRoadIdByIndex(index)
        if road_id == 0xFFFFFFFF:
            return None
        return road_id

    def get_all_road_ids(self) -> List[int]:
        """
        Get all road IDs in the loaded OpenDRIVE.

        Returns:
            List of road IDs
        """
        count = self.get_num_roads()
        if count <= 0:
            return []

        road_ids = []
        for i in range(count):
            road_id = self.get_road_id_by_index(i)
            if road_id is not None:
                road_ids.append(road_id)
        return road_ids

    def get_road_length(self, road_id: int) -> float:
        """
        Get road length.

        Args:
            road_id: The road ID

        Returns:
            Road length in meters, or -1 if road not found
        """
        return self.lib.GT_RM_GetRoadLength(road_id)

    def get_road_signal_count(self, road_id: int) -> int:
        """
        Get the number of signals on a road.

        Args:
            road_id: The road ID

        Returns:
            Number of signals, or -1 if road not found
        """
        return self.lib.GT_RM_GetRoadSignalCount(road_id)

    def get_road_signal(self, road_id: int, index: int) -> Optional[RoadSignalInfo]:
        """
        Get signal information by index.

        Args:
            road_id: The road ID
            index: Signal index (0-based)

        Returns:
            RoadSignalInfo if found, None otherwise
        """
        signal_info = GT_RM_RoadSignalInfo()
        result = self.lib.GT_RM_GetRoadSignal(road_id, index, ctypes.byref(signal_info))

        if result == 0:
            return RoadSignalInfo(
                id=signal_info.id,
                s=signal_info.s,
                t=signal_info.t,
                x=signal_info.x,
                y=signal_info.y,
                z=signal_info.z,
                h=signal_info.h,
                p=signal_info.p,
                r=signal_info.r,
                type=signal_info.type.decode('utf-8', errors='replace'),
                subtype=signal_info.subtype.decode('utf-8', errors='replace'),
                country=signal_info.country.decode('utf-8', errors='replace'),
                value=signal_info.value,
                unit=signal_info.unit.decode('utf-8', errors='replace'),
                text=signal_info.text.decode('utf-8', errors='replace'),
                is_dynamic=signal_info.isDynamic,
                height=signal_info.height,
                width=signal_info.width
            )
        return None

    def get_all_road_signals(self, road_id: int) -> List[RoadSignalInfo]:
        """
        Get all signals on a road.

        Args:
            road_id: The road ID

        Returns:
            List of RoadSignalInfo objects
        """
        count = self.get_road_signal_count(road_id)
        if count <= 0:
            return []

        signals = []
        for i in range(count):
            signal = self.get_road_signal(road_id, i)
            if signal:
                signals.append(signal)
        return signals

    def get_connected_roads(self, road_id: int, direction: str = 'both') -> List[Tuple[int, str, int]]:
        """
        Get roads connected to a given road.

        Args:
            road_id: The road ID
            direction: 'successor', 'predecessor', or 'both'

        Returns:
            List of (connected_road_id, connection_type, contact_point) tuples
            contact_point: 0=unknown, 1=start, 2=end
        """
        connected = []

        if direction in ('successor', 'both'):
            succ = self.get_road_successor(road_id)
            if succ:
                if succ.is_road:
                    connected.append((succ.element_id, 'successor', succ.contact_point))
                elif succ.is_junction:
                    # Get all connecting roads through junction with contactPoint
                    for conn_road_id, contact_pt in self.get_junction_connections_from_road_with_contact(succ.element_id, road_id):
                        connected.append((conn_road_id, 'junction_successor', contact_pt))

        if direction in ('predecessor', 'both'):
            pred = self.get_road_predecessor(road_id)
            if pred:
                if pred.is_road:
                    connected.append((pred.element_id, 'predecessor', pred.contact_point))
                elif pred.is_junction:
                    # Get all connecting roads through junction with contactPoint
                    for conn_road_id, contact_pt in self.get_junction_connections_from_road_with_contact(pred.element_id, road_id):
                        connected.append((conn_road_id, 'junction_predecessor', contact_pt))

        return connected
