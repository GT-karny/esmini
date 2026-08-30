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
     * As GT_RM_CalcRoute, but with an explicit start heading.
     *
     * WHY THIS EXISTS: LaneIndependentRouter picks its search direction off the start
     * road from the start Position's hRelative -- forward (< pi/2 or > 3pi/2) follows
     * the road's SUCCESSOR link, otherwise the PREDECESSOR link. GT_RM_CalcRoute leaves
     * hRelative at its 0.0 default, so it can ONLY find routes leaving via the start
     * road's successor end; a route whose first hop leaves via the predecessor end
     * returns -2 ("no route") even though one exists. Pass startHRelative = M_PI to
     * search the other way.
     *
     * For "route in the direction this lane is legally driven", ask
     * GT_RM_GetLaneDrivingDirection first and map +1 -> 0.0, -1 -> M_PI.
     *
     * @param startHRelative Start heading relative to the road's s-direction [rad]
     * @return same as GT_RM_CalcRoute
     */
    GT_RM_DLL_API int GT_RM_CalcRouteH(uint32_t startRoadId, int startLaneId, double startS,
                                       uint32_t targetRoadId, int targetLaneId, double targetS,
                                       int routeStrategy, double startHRelative);

    /**
     * Legal driving direction of a lane, relative to the road's s-axis.
     * Folds the road's RoadRule (left/right hand traffic) into the lane-sign
     * convention, so callers must NOT re-derive it from the lane id alone.
     * @return +1 (drives +s), -1 (drives -s), or 0 on error (no map / bad road/lane)
     */
    GT_RM_DLL_API int GT_RM_GetLaneDrivingDirection(uint32_t roadId, int laneId, double s);

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

    /* ================================================================== */
    /* GT ODR side-model metadata (P9a) -- JSON accessors                  */
    /* ================================================================== */
    /*
     * These functions expose the GT-side OpenDRIVE "side model" -- the second,
     * GT-owned pass over the parsed xodr that records everything upstream
     * RoadManager cannot store (version awareness, coverage audit, userData /
     * dataQuality blobs, signal <semantics>, junction priorities / crossPaths,
     * and railroad switches / stations). See gt_esmini/road/OdrSideModel.hpp.
     *
     * ---- Uniform buffer protocol (applies to EVERY GT_RM_GetXxxJson below) ----
     *   int GT_RM_GetXxxJson(char* buffer, int bufferSize);
     *
     *   * Serializes the whole document's category to a UTF-8 JSON string.
     *   * RETURNS the required length in bytes EXCLUDING the terminating NUL,
     *     regardless of whether truncation occurred. A caller does the standard
     *     two-call dance: call once with (NULL, 0) [or any too-small buffer] to
     *     learn the size, allocate required+1 bytes, then call again to fetch.
     *   * When buffer != NULL && bufferSize > 0: copies min(required, bufferSize-1)
     *     bytes into `buffer` and always NUL-terminates. buffer == NULL or
     *     bufferSize <= 0 performs a size probe only (nothing is copied).
     *   * RETURNS -1 when no OpenDRIVE is loaded (GT_RM_Init not called / failed)
     *     OR no side model is registered for it. In that case `buffer`, if given,
     *     is left NUL-terminated-empty when bufferSize > 0.
     *
     * Doubles are emitted via snprintf("%.12g") (compact, round-trip-ish for the
     * s-coordinates / speed values these carry -- NOT a bit-exact IEEE754
     * serialization); integers are emitted as-is; strings are UTF-8 passthrough
     * with JSON escaping of " \\ and control chars (\n / \t rendered as their
     * short escapes). Empty categories yield empty JSON arrays (never null).
     *
     * NOTE: the railroad data (GT_RM_GetRailroadJson) is L1/INERT -- it is stored
     * and serialized here but consumed by no runtime (no rail runtime, no OSI, no
     * policy); it exists for tooling / inspection only.
     */

    /**
     * Coverage-audit summary + version header.
     * {"version":{"rev_major":N,"rev_minor":N},"unsupported_elements":N,
     *  "unsupported_attributes":N,"removed16_hits":N,"entries":[<stored-format strings>]}
     * See the buffer protocol above for return / truncation semantics.
     */
    GT_RM_DLL_API int GT_RM_GetOdrAuditJson(char* buffer, int bufferSize);

    /**
     * Raw additionalData blobs captured verbatim during parse.
     * {"user_data":[{"owner_path":s,"context_id":s,"xml":s}],"data_quality":[<same shape>]}
     */
    GT_RM_DLL_API int GT_RM_GetUserDataJson(char* buffer, int bufferSize);

    /**
     * Per-signal side extras (one entry per stored signal_extras element; these
     * are already sparse -- only signals carrying dependency/reference/semantics/
     * board/flag data appear).
     * {"signals":[{"road_id":s,"signal_id":s,"has_semantics":b,
     *   "semantics":{"speeds":[{"type":s,"value":num,"unit":s}],"lane_types":[s],
     *     "priority_types":[s],"prohibited":[{"kind":s,"category":s}],
     *     "warning_count":N,"routing_count":N,"streetname_count":N,"parking_count":N,
     *     "tourist_count":N,"supplementary_explanatory_count":N},
     *   "dependencies":[{"id":s,"type":s}],
     *   "references":[{"element_type":s,"element_id":s,"type":s}],
     *   "temporary":b,"invalidated":b}]}
     */
    GT_RM_DLL_API int GT_RM_GetSignalSemanticsJson(char* buffer, int bufferSize);

    /**
     * Junction <priority high low> lists -- ONLY junctions with a non-empty
     * priority list appear.
     * {"junctions":[{"junction_id":s,"type":s,"priorities":[{"high":s,"low":s}]}]}
     */
    GT_RM_DLL_API int GT_RM_GetJunctionPrioritiesJson(char* buffer, int bufferSize);

    /**
     * All <crossPath> entries flattened across every junction's junction_extras.
     * {"cross_paths":[{"junction_id":s,"id":s,"crossing_road":s,"road_at_start":s,
     *   "road_at_end":s,"synth_object_id":N}]}
     */
    GT_RM_DLL_API int GT_RM_GetCrosswalksJson(char* buffer, int bufferSize);

    /**
     * Railroad switches + root-level stations (L1 / INERT -- no runtime consumer).
     * {"switches":[{"road_id":s,"name":s,"id":s,"position":s,
     *   "main_track":{"id":s,"s":num,"dir":s}|null,
     *   "side_track":{"id":s,"s":num,"dir":s}|null,
     *   "partner":{"name":s,"id":s}|null}],
     *  "stations":[{"id":s,"name":s,"type":s,
     *   "platforms":[{"id":s,"name":s,
     *     "segments":[{"road_id":s,"s_start":num,"s_end":num,"side":s}]}]}]}
     * A main_track / side_track / partner object is null when the corresponding
     * has_main_track / has_side_track / has_partner flag is false.
     */
    GT_RM_DLL_API int GT_RM_GetRailroadJson(char* buffer, int bufferSize);

    /**
     * P9b: 1.9 lane-layer (cluster 4/22) info from the P8 OdrRoadLaneLayers shadow
     * storage, plus the process-wide selection-mode latch (env GT_ODR_LANE_LAYERS,
     * plan D1 -- latched once per process, no runtime switching).
     * {"mode":"permanent"|"temporary",
     *  "roads":[{"road_id":s,"active_mode":s,"has_temporary":b,
     *    "temp_s_start":num,"temp_s_end":num,
     *    "layers":[{"name":s,"lane_offset_count":N,
     *      "sections":[{"s":num,"length":num,"has_length":b,"lane_count":N}]}]}]}
     * `roads` is sparse: only roads that authored @layer or >1 <lanes> appear
     * (legacy assets yield an empty array). "mode" is always present.
     */
    GT_RM_DLL_API int GT_RM_GetLaneLayersJson(char* buffer, int bufferSize);

    /**
     * P9b: virtual-junction (P6, cluster 6) metadata -- one entry per junction
     * with @type="virtual", from the parsed core model + the [GT_ODR:vj-model]
     * anchor registry (GetVirtualJunctionAnchors). Metadata only; routing/motion
     * behavior is the P6 native implementation.
     * {"virtual_junctions":[{"junction_id":s,"name":s,"main_road_id":s,
     *   "main_road_length":num,"s_start":num,"s_end":num,
     *   "orientation":"+"|"-"|"","anchor_count":N,"connection_count":N}]}
     */
    GT_RM_DLL_API int GT_RM_GetVirtualJunctionsJson(char* buffer, int bufferSize);

#ifdef __cplusplus
}
#endif
