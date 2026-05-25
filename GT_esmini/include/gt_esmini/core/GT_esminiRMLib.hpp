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

// Route strategies (mirror roadmanager::Position::RouteStrategy)
#define GT_RM_ROUTE_SHORTEST          0
#define GT_RM_ROUTE_FASTEST           1
#define GT_RM_ROUTE_MIN_INTERSECTIONS 2

// A single waypoint along a calculated route
typedef struct
{
    uint32_t roadId;     // road ID of this waypoint
    uint32_t junctionId; // junction ID (0xFFFFFFFF if not in a junction)
    int      laneId;     // lane ID on the road
    double   s;          // longitudinal position along the road
    double   x;          // world X
    double   y;          // world Y
    double   z;          // world Z
    double   h;          // heading (global)
} GT_RM_RouteWaypoint;

// A lane change required while driving along one road of the route
typedef struct
{
    uint32_t roadId;     // road on which the change must happen
    double   s;          // road-entry s; change should complete before road end
    int      fromLaneId; // lane entered on this road
    int      toLaneId;   // lane needed to connect onward
} GT_RM_LaneChange;

// Road Signal information structure
typedef struct
{
    int id;
    double s;
    double t;
    double x;
    double y;
    double z;
    double h;
    double p;
    double r;
    char type[64];
    char subtype[64];
    char country[64];
    double value;
    char unit[64];
    char text[128];
    bool isDynamic;
    double height;
    double width;
} GT_RM_RoadSignalInfo;

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * Initialize GT_esminiRMLib with an OpenDRIVE file.
     * This loads the road network into the internal RoadManager.
     * @param odrFilename Path to the OpenDRIVE (.xodr) file
     * @return 0 on success, -1 on failure
     */
    GT_RM_DLL_API int GT_RM_Init(const char* odrFilename);

    /**
     * Close GT_esminiRMLib and release resources.
     */
    GT_RM_DLL_API void GT_RM_Close();

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

    /**
     * Get the number of signals on a road.
     * @param roadId The road ID
     * @return Number of signals, or -1 if road not found
     */
    GT_RM_DLL_API int GT_RM_GetRoadSignalCount(uint32_t roadId);

    /**
     * Get signal information by index.
     * @param roadId The road ID
     * @param index Signal index (0-based)
     * @param signalInfo Output: Signal information
     * @return 0 on success, -1 if road not found, -2 if index out of range
     */
    GT_RM_DLL_API int GT_RM_GetRoadSignal(uint32_t roadId, int index, GT_RM_RoadSignalInfo* signalInfo);

    /* ------------------------------------------------------------------ */
    /* Lane-change-aware route calculation (roadmanager::LaneIndependentRouter) */
    /* ------------------------------------------------------------------ */

    /**
     * Calculate a lane-change-aware route from a start to a target lane position.
     * Unlike the road-level RoadPath, this finds routes that require lane changes
     * (e.g. moving into a turn lane before a junction). The result is cached
     * internally; use the getters below to read waypoints and the lane-change plan.
     * @param startRoadId   Start road ID
     * @param startLaneId   Start lane ID
     * @param startS        Start s-position
     * @param targetRoadId  Target road ID
     * @param targetLaneId  Target lane ID
     * @param targetS        Target s-position
     * @param routeStrategy GT_RM_ROUTE_SHORTEST / _FASTEST / _MIN_INTERSECTIONS
     * @return number of waypoints (>= 0), -1 on error (bad map/args), -2 if no route found
     */
    GT_RM_DLL_API int GT_RM_CalcRoute(uint32_t startRoadId, int startLaneId, double startS,
                                      uint32_t targetRoadId, int targetLaneId, double targetS,
                                      int routeStrategy);

    /**
     * Number of waypoints in the last calculated route (0 if none).
     */
    GT_RM_DLL_API int GT_RM_GetRouteWaypointCount();

    /**
     * Get a waypoint of the last calculated route by index.
     * @return 0 on success, -1 on error / index out of range
     */
    GT_RM_DLL_API int GT_RM_GetRouteWaypoint(int index, GT_RM_RouteWaypoint* wp);

    /**
     * Total accumulated cost of the last route (meters for SHORTEST, seconds for
     * FASTEST, junction count for MIN_INTERSECTIONS). Negative if no route.
     */
    GT_RM_DLL_API double GT_RM_GetRouteLength();

    /**
     * Number of lane changes required by the last calculated route.
     */
    GT_RM_DLL_API int GT_RM_GetLaneChangeCount();

    /**
     * Get a lane-change event of the last route by index.
     * @return 0 on success, -1 on error / index out of range
     */
    GT_RM_DLL_API int GT_RM_GetLaneChange(int index, GT_RM_LaneChange* lc);

#ifdef __cplusplus
}
#endif
