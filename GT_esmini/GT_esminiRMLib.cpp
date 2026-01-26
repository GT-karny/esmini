/*
 * GT_esminiRMLib - GT Extension for esminiRMLib
 *
 * Implementation of road connection query functions.
 */

#include "GT_esminiRMLib.hpp"
#include "RoadManager.hpp"

using namespace roadmanager;

// Helper function to get OpenDrive instance
static OpenDrive* GetODR()
{
    return Position::GetOpenDrive();
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
