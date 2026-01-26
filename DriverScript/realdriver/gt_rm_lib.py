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

    def get_connected_roads(self, road_id: int, direction: str = 'both') -> List[Tuple[int, str]]:
        """
        Get roads connected to a given road.

        Args:
            road_id: The road ID
            direction: 'successor', 'predecessor', or 'both'

        Returns:
            List of (connected_road_id, connection_type) tuples
        """
        connected = []

        if direction in ('successor', 'both'):
            succ = self.get_road_successor(road_id)
            if succ:
                if succ.is_road:
                    connected.append((succ.element_id, 'successor'))
                elif succ.is_junction:
                    # Get all connecting roads through junction
                    for conn_road_id in self.get_junction_connections_from_road(succ.element_id, road_id):
                        connected.append((conn_road_id, 'junction_successor'))

        if direction in ('predecessor', 'both'):
            pred = self.get_road_predecessor(road_id)
            if pred:
                if pred.is_road:
                    connected.append((pred.element_id, 'predecessor'))
                elif pred.is_junction:
                    # Get all connecting roads through junction
                    for conn_road_id in self.get_junction_connections_from_road(pred.element_id, road_id):
                        connected.append((conn_road_id, 'junction_predecessor'))

        return connected
