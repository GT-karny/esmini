/*
 * GT_esminiRMLib - GT Extension for esminiRMLib
 *
 * Implementation of road connection query functions.
 */

#include "gt_esmini/core/GT_esminiRMLib.hpp"
#include "RoadManager.hpp"
#include "LaneIndependentRouter.hpp"
#include "gt_esmini/road/route_lanechange_util.hpp"
#include <cstring>
#include <vector>

using namespace roadmanager;

// Helper function to get OpenDrive instance
static OpenDrive* GetODR()
{
    return Position::GetOpenDrive();
}

// --- Route calculation result cache (filled by GT_RM_CalcRoute) ---
static std::vector<GT_RM_RouteWaypoint>     g_routeWaypoints;
static std::vector<gt_esmini::route::LaneChange> g_routeLaneChanges;
static double                               g_routeLength = -1.0;

GT_RM_DLL_API int GT_RM_Init(const char* odrFilename)
{
    if (!odrFilename)
    {
        return -1;
    }

    // Load OpenDRIVE file using Position::LoadOpenDrive
    // This sets the global OpenDrive instance that GetODR() returns
    if (!Position::LoadOpenDrive(odrFilename))
    {
        return -1;
    }

    return 0;
}

GT_RM_DLL_API void GT_RM_Close()
{
    // The OpenDrive instance is managed statically by Position class
    // No explicit cleanup needed, but we could add it if necessary
}
GT_RM_DLL_API int GT_RM_GetRoadSuccessor(uint32_t roadId, GT_RM_RoadLinkInfo* linkInfo)
{
    if (!linkInfo) return -1;

    OpenDrive* odr = GetODR();
    if (!odr) return -1;

    Road* road = odr->GetRoadById(static_cast<id_t>(roadId));
    if (!road) return -1;

    RoadLink* link = road->GetLink(LinkType::SUCCESSOR);
    if (!link) return -2;

    linkInfo->elementId = static_cast<uint32_t>(link->GetElementId());

    switch (link->GetElementType())
    {
        case RoadLink::ElementType::ELEMENT_TYPE_ROAD:
            linkInfo->elementType = GT_RM_ELEMENT_TYPE_ROAD;
            break;
        case RoadLink::ElementType::ELEMENT_TYPE_JUNCTION:
            linkInfo->elementType = GT_RM_ELEMENT_TYPE_JUNCTION;
            break;
        default:
            linkInfo->elementType = GT_RM_ELEMENT_TYPE_UNKNOWN;
            break;
    }

    switch (link->GetContactPointType())
    {
        case ContactPointType::CONTACT_POINT_START:
            linkInfo->contactPoint = GT_RM_CONTACT_POINT_START;
            break;
        case ContactPointType::CONTACT_POINT_END:
            linkInfo->contactPoint = GT_RM_CONTACT_POINT_END;
            break;
        default:
            linkInfo->contactPoint = GT_RM_CONTACT_POINT_UNKNOWN;
            break;
    }

    return 0;
}

GT_RM_DLL_API int GT_RM_GetRoadPredecessor(uint32_t roadId, GT_RM_RoadLinkInfo* linkInfo)
{
    if (!linkInfo) return -1;

    OpenDrive* odr = GetODR();
    if (!odr) return -1;

    Road* road = odr->GetRoadById(static_cast<id_t>(roadId));
    if (!road) return -1;

    RoadLink* link = road->GetLink(LinkType::PREDECESSOR);
    if (!link) return -2;

    linkInfo->elementId = static_cast<uint32_t>(link->GetElementId());

    switch (link->GetElementType())
    {
        case RoadLink::ElementType::ELEMENT_TYPE_ROAD:
            linkInfo->elementType = GT_RM_ELEMENT_TYPE_ROAD;
            break;
        case RoadLink::ElementType::ELEMENT_TYPE_JUNCTION:
            linkInfo->elementType = GT_RM_ELEMENT_TYPE_JUNCTION;
            break;
        default:
            linkInfo->elementType = GT_RM_ELEMENT_TYPE_UNKNOWN;
            break;
    }

    switch (link->GetContactPointType())
    {
        case ContactPointType::CONTACT_POINT_START:
            linkInfo->contactPoint = GT_RM_CONTACT_POINT_START;
            break;
        case ContactPointType::CONTACT_POINT_END:
            linkInfo->contactPoint = GT_RM_CONTACT_POINT_END;
            break;
        default:
            linkInfo->contactPoint = GT_RM_CONTACT_POINT_UNKNOWN;
            break;
    }

    return 0;
}

GT_RM_DLL_API int GT_RM_GetJunctionConnectionCount(uint32_t junctionId)
{
    OpenDrive* odr = GetODR();
    if (!odr) return -1;

    Junction* junction = odr->GetJunctionById(static_cast<id_t>(junctionId));
    if (!junction) return -1;

    return static_cast<int>(junction->GetConnections().size());
}

GT_RM_DLL_API int GT_RM_GetJunctionConnection(uint32_t junctionId, int index,
                                               GT_RM_JunctionConnection* connection)
{
    if (!connection) return -1;

    OpenDrive* odr = GetODR();
    if (!odr) return -1;

    Junction* junction = odr->GetJunctionById(static_cast<id_t>(junctionId));
    if (!junction) return -1;

    const auto& connections = junction->GetConnections();
    if (index < 0 || index >= static_cast<int>(connections.size())) return -2;

    Connection* conn = connections[static_cast<size_t>(index)];
    if (!conn) return -1;

    Road* incomingRoad = conn->GetIncomingRoad();
    Road* connectingRoad = conn->GetConnectingRoad();

    connection->incomingRoadId = incomingRoad ? static_cast<uint32_t>(incomingRoad->GetId()) : 0xFFFFFFFF;
    connection->connectingRoadId = connectingRoad ? static_cast<uint32_t>(connectingRoad->GetId()) : 0xFFFFFFFF;

    switch (conn->GetContactPoint())
    {
        case ContactPointType::CONTACT_POINT_START:
            connection->contactPoint = GT_RM_CONTACT_POINT_START;
            break;
        case ContactPointType::CONTACT_POINT_END:
            connection->contactPoint = GT_RM_CONTACT_POINT_END;
            break;
        default:
            connection->contactPoint = GT_RM_CONTACT_POINT_UNKNOWN;
            break;
    }

    return 0;
}

GT_RM_DLL_API int GT_RM_GetJunctionConnectionsFromRoad(uint32_t junctionId,
                                                        uint32_t incomingRoadId)
{
    OpenDrive* odr = GetODR();
    if (!odr) return -1;

    Junction* junction = odr->GetJunctionById(static_cast<id_t>(junctionId));
    if (!junction) return -1;

    return static_cast<int>(junction->GetNoConnectionsFromRoadId(static_cast<id_t>(incomingRoadId)));
}

GT_RM_DLL_API int GT_RM_GetJunctionConnectionFromRoadByIndex(uint32_t junctionId,
                                                              uint32_t incomingRoadId,
                                                              int index,
                                                              uint32_t* connectingRoadId)
{
    if (!connectingRoadId) return -1;

    OpenDrive* odr = GetODR();
    if (!odr) return -1;

    Junction* junction = odr->GetJunctionById(static_cast<id_t>(junctionId));
    if (!junction) return -1;

    id_t roadId = junction->GetConnectingRoadIdFromIncomingRoadId(
        static_cast<id_t>(incomingRoadId),
        static_cast<unsigned int>(index)
    );

    if (roadId == 0) return -1;

    *connectingRoadId = static_cast<uint32_t>(roadId);
    return 0;
}

GT_RM_DLL_API int GT_RM_GetNumRoads()
{
    OpenDrive* odr = GetODR();
    if (!odr) return -1;

    return static_cast<int>(odr->GetNumOfRoads());
}

GT_RM_DLL_API uint32_t GT_RM_GetRoadIdByIndex(int index)
{
    OpenDrive* odr = GetODR();
    if (!odr) return 0xFFFFFFFF;

    if (index < 0 || index >= static_cast<int>(odr->GetNumOfRoads()))
        return 0xFFFFFFFF;

    Road* road = odr->GetRoadByIdx(static_cast<idx_t>(index));
    if (!road) return 0xFFFFFFFF;

    return static_cast<uint32_t>(road->GetId());
}

GT_RM_DLL_API double GT_RM_GetRoadLength(uint32_t roadId)
{
    OpenDrive* odr = GetODR();
    if (!odr) return -1.0;

    Road* road = odr->GetRoadById(static_cast<id_t>(roadId));
    if (!road) return -1.0;

    return road->GetLength();
}

GT_RM_DLL_API int GT_RM_GetRoadSignalCount(uint32_t roadId)
{
    OpenDrive* odr = GetODR();
    if (!odr) return -1;

    Road* road = odr->GetRoadById(static_cast<id_t>(roadId));
    if (!road) return -1;

    return static_cast<int>(road->GetNumberOfSignals());
}

GT_RM_DLL_API int GT_RM_GetRoadSignal(uint32_t roadId, int index, GT_RM_RoadSignalInfo* signalInfo)
{
    if (!signalInfo) return -1;

    OpenDrive* odr = GetODR();
    if (!odr) return -1;

    Road* road = odr->GetRoadById(static_cast<id_t>(roadId));
    if (!road) return -1;

    if (index < 0 || index >= static_cast<int>(road->GetNumberOfSignals())) return -2;

    Signal* signal = road->GetSignal(static_cast<idx_t>(index));
    if (!signal) return -1;

    signalInfo->id = signal->GetId();
    signalInfo->s = signal->GetS();
    signalInfo->t = signal->GetT();

    // Calculate world position
    Position pos;
    // We suppress error checking here since some signals might be slightly off-road or on invalid lanes
    // but SetTrackPos usually initializes basic coords anyway.
    pos.SetTrackPos(static_cast<id_t>(roadId), signalInfo->s, signalInfo->t);

    signalInfo->x = pos.GetX();
    signalInfo->y = pos.GetY();
    signalInfo->z = pos.GetZ() + signal->GetZOffset();

    signalInfo->h = pos.GetH() + signal->GetHOffset();
    signalInfo->p = pos.GetP() + signal->GetPitch();
    signalInfo->r = pos.GetR() + signal->GetRoll();

    strncpy(signalInfo->type, signal->GetType().c_str(), 63);
    signalInfo->type[63] = '\0';

    strncpy(signalInfo->subtype, signal->GetSubType().c_str(), 63);
    signalInfo->subtype[63] = '\0';

    strncpy(signalInfo->country, signal->GetCountry().c_str(), 63);
    signalInfo->country[63] = '\0';

    signalInfo->value = signal->GetValue();

    strncpy(signalInfo->unit, signal->GetUnit().c_str(), 63);
    signalInfo->unit[63] = '\0';

    strncpy(signalInfo->text, signal->GetText().c_str(), 127);
    signalInfo->text[127] = '\0';

    signalInfo->isDynamic = signal->IsDynamic();
    signalInfo->height = signal->GetHeight();
    signalInfo->width = signal->GetWidth();

    return 0;
}

// --------------------------------------------------------------------------
// Lane-change-aware route calculation
// --------------------------------------------------------------------------

static Position::RouteStrategy MapRouteStrategy(int strategy)
{
    switch (strategy)
    {
        case GT_RM_ROUTE_FASTEST:
            return Position::RouteStrategy::FASTEST;
        case GT_RM_ROUTE_MIN_INTERSECTIONS:
            return Position::RouteStrategy::MIN_INTERSECTIONS;
        case GT_RM_ROUTE_SHORTEST:
        default:
            return Position::RouteStrategy::SHORTEST;
    }
}

GT_RM_DLL_API int GT_RM_CalcRoute(uint32_t startRoadId, int startLaneId, double startS,
                                  uint32_t targetRoadId, int targetLaneId, double targetS,
                                  int routeStrategy)
{
    g_routeWaypoints.clear();
    g_routeLaneChanges.clear();
    g_routeLength = -1.0;

    OpenDrive* odr = GetODR();
    if (!odr) return -1;

    Position startPos;
    startPos.SetLanePos(static_cast<id_t>(startRoadId), startLaneId, startS, 0.0);

    Position targetPos;
    targetPos.SetLanePos(static_cast<id_t>(targetRoadId), targetLaneId, targetS, 0.0);
    targetPos.SetRouteStrategy(MapRouteStrategy(routeStrategy));

    LaneIndependentRouter      router(odr);
    std::vector<Node>          path = router.CalculatePath(startPos, targetPos);
    if (path.empty())
    {
        return -2;  // no route found
    }

    std::vector<Position> waypoints = router.GetWaypoints(path, startPos, targetPos);

    g_routeWaypoints.reserve(waypoints.size());
    for (const Position& wp : waypoints)
    {
        GT_RM_RouteWaypoint out;
        out.roadId     = static_cast<uint32_t>(wp.GetTrackId());
        out.junctionId = static_cast<uint32_t>(wp.GetJunctionId());
        out.laneId     = wp.GetLaneId();
        out.s          = wp.GetS();
        out.x          = wp.GetX();
        out.y          = wp.GetY();
        out.z          = wp.GetZ();
        out.h          = wp.GetH();
        g_routeWaypoints.push_back(out);
    }

    g_routeLaneChanges = gt_esmini::route::DeriveLaneChanges(path);
    g_routeLength      = path.back().weight;

    return static_cast<int>(g_routeWaypoints.size());
}

GT_RM_DLL_API int GT_RM_GetRouteWaypointCount()
{
    return static_cast<int>(g_routeWaypoints.size());
}

GT_RM_DLL_API int GT_RM_GetRouteWaypoint(int index, GT_RM_RouteWaypoint* wp)
{
    if (!wp) return -1;
    if (index < 0 || index >= static_cast<int>(g_routeWaypoints.size())) return -1;

    *wp = g_routeWaypoints[static_cast<size_t>(index)];
    return 0;
}

GT_RM_DLL_API double GT_RM_GetRouteLength()
{
    return g_routeLength;
}

GT_RM_DLL_API int GT_RM_GetLaneChangeCount()
{
    return static_cast<int>(g_routeLaneChanges.size());
}

GT_RM_DLL_API int GT_RM_GetLaneChange(int index, GT_RM_LaneChange* lc)
{
    if (!lc) return -1;
    if (index < 0 || index >= static_cast<int>(g_routeLaneChanges.size())) return -1;

    const gt_esmini::route::LaneChange& src = g_routeLaneChanges[static_cast<size_t>(index)];
    lc->roadId     = src.roadId;
    lc->s          = src.s;
    lc->fromLaneId = src.fromLaneId;
    lc->toLaneId   = src.toLaneId;
    return 0;
}
