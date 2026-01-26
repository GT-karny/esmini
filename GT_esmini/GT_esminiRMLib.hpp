/*
 * GT_esminiRMLib - GT Extension for esminiRMLib
 *
 * This extension provides road connection query functions for Python-based
 * route calculation. It extends esminiRMLib without modifying the original.
 */

#pragma once

#include <cstdint>

#ifdef WIN32
#define GT_RM_DLL_API __declspec(dllexport)
#else
#define GT_RM_DLL_API
#endif

// Link types for road connections
#define GT_RM_LINK_TYPE_PREDECESSOR 0
#define GT_RM_LINK_TYPE_SUCCESSOR   1

// Element types for road links
#define GT_RM_ELEMENT_TYPE_UNKNOWN  0
#define GT_RM_ELEMENT_TYPE_ROAD     1
#define GT_RM_ELEMENT_TYPE_JUNCTION 2

// Contact point types
#define GT_RM_CONTACT_POINT_UNKNOWN 0
#define GT_RM_CONTACT_POINT_START   1
#define GT_RM_CONTACT_POINT_END     2

// Road link information structure
typedef struct
{
    uint32_t elementId;      // ID of connected road or junction
    int      elementType;    // GT_RM_ELEMENT_TYPE_*
    int      contactPoint;   // GT_RM_CONTACT_POINT_*
} GT_RM_RoadLinkInfo;

// Junction connection information structure
typedef struct
{
    uint32_t incomingRoadId;   // ID of incoming road
    uint32_t connectingRoadId; // ID of connecting road (through junction)
    int      contactPoint;     // Contact point type
} GT_RM_JunctionConnection;

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * Get the successor link of a road.
     * @param roadId The road ID
     * @param linkInfo Output: Link information
     * @return 0 on success, -1 if road not found, -2 if no successor
     */
    GT_RM_DLL_API int GT_RM_GetRoadSuccessor(uint32_t roadId, GT_RM_RoadLinkInfo* linkInfo);

    /**
     * Get the predecessor link of a road.
     * @param roadId The road ID
     * @param linkInfo Output: Link information
     * @return 0 on success, -1 if road not found, -2 if no predecessor
     */
    GT_RM_DLL_API int GT_RM_GetRoadPredecessor(uint32_t roadId, GT_RM_RoadLinkInfo* linkInfo);

    /**
     * Get the number of connections in a junction.
     * @param junctionId The junction ID
     * @return Number of connections, or -1 if junction not found
     */
    GT_RM_DLL_API int GT_RM_GetJunctionConnectionCount(uint32_t junctionId);

    /**
     * Get a junction connection by index.
     * @param junctionId The junction ID
     * @param index Connection index (0-based)
     * @param connection Output: Connection information
     * @return 0 on success, -1 if junction not found, -2 if index out of range
     */
    GT_RM_DLL_API int GT_RM_GetJunctionConnection(uint32_t junctionId, int index,
                                                   GT_RM_JunctionConnection* connection);

    /**
     * Get all junction connections for a specific incoming road.
     * @param junctionId The junction ID
     * @param incomingRoadId The incoming road ID
     * @return Number of connections from this incoming road, or -1 on error
     */
    GT_RM_DLL_API int GT_RM_GetJunctionConnectionsFromRoad(uint32_t junctionId,
                                                            uint32_t incomingRoadId);

    /**
     * Get a junction connection from a specific incoming road by index.
     * @param junctionId The junction ID
     * @param incomingRoadId The incoming road ID
     * @param index Connection index (0-based, among connections from this road)
     * @param connectingRoadId Output: The connecting road ID
     * @return 0 on success, -1 on error
     */
    GT_RM_DLL_API int GT_RM_GetJunctionConnectionFromRoadByIndex(uint32_t junctionId,
                                                                  uint32_t incomingRoadId,
                                                                  int index,
                                                                  uint32_t* connectingRoadId);

    /**
     * Get the number of roads in the loaded OpenDRIVE.
     * @return Number of roads, or -1 if no map loaded
     */
    GT_RM_DLL_API int GT_RM_GetNumRoads();

    /**
     * Get road ID by index.
     * @param index Road index (0-based)
     * @return Road ID, or 0xFFFFFFFF if index out of range
     */
    GT_RM_DLL_API uint32_t GT_RM_GetRoadIdByIndex(int index);

    /**
     * Get road length.
     * @param roadId The road ID
     * @return Road length in meters, or -1 if road not found
     */
    GT_RM_DLL_API double GT_RM_GetRoadLength(uint32_t roadId);

#ifdef __cplusplus
}
#endif
