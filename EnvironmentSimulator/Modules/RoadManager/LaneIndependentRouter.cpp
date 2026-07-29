#include <algorithm>
#include "CommonMini.hpp"
#include "pugixml.hpp"
#include "LaneIndependentRouter.hpp"

using namespace roadmanager;
// [GT_ODR:vj-router-begin] Virtual-junction anchor injection: a main road carrying registry anchors can branch
// off mid-road (design §4 STAGE 6). For a node sitting on such a road, entered at entryS and travelling toward
// travelToEnd (true = +s / END contact, false = -s / START contact), enqueue one child node per anchor lying in
// the [entry, traversal-end] window. The child rides the registry-owned anchor link (element_s_ < 0, a stable
// per-anchor pointer) onto the branch; its anchorS records the main-road s where it left so the weight and the
// emitted waypoint land at the branch-off, exactly like RoadPath::CheckRoad's anchor expansion. Node identity is
// the anchor link pointer, so an anchor node never conflates with an end-contact node on the same road.
static void InjectVirtualJunctionAnchorNodes(OpenDrive                                                       *odr,
                                             Node                                                            *fromNode,
                                             double                                                           entryS,
                                             bool                                                             travelToEnd,
                                             std::priority_queue<Node *, std::vector<Node *>, WeightCompare> &pushTo,
                                             const std::vector<Node *>                                       &visited,
                                             RoadCalculations                                                &roadCalc,
                                             Position::RouteStrategy                                          routeStrategy)
{
    Road *road = fromNode->road;
    for (const OpenDrive::VirtualJunctionAnchor &anchor : odr->GetVirtualJunctionAnchors(road->GetId()))
    {
        if (travelToEnd ? anchor.anchor_s_ < entryS : anchor.anchor_s_ > entryS)
        {
            continue;  // anchor is behind the travel direction from this entry
        }
        // dedup on the registry anchor link identity (visited set + fromNode's own chain)
        bool alreadySeen =
            std::find_if(visited.begin(), visited.end(), [&anchor](const Node *n) { return n->link == anchor.link_; }) != visited.end();
        for (Node *p = fromNode; !alreadySeen && p != nullptr; p = p->previous)
        {
            alreadySeen = p->link == anchor.link_;
        }
        if (alreadySeen)
        {
            continue;
        }
        Node *aNode          = new Node;
        aNode->road          = road;
        aNode->link          = anchor.link_;
        aNode->currentLaneId = fromNode->currentLaneId;
        aNode->fromLaneId    = fromNode->fromLaneId;
        aNode->previous      = fromNode;
        aNode->anchorS       = anchor.anchor_s_;
        // weight = (accumulated BEFORE this road) + the PARTIAL main-road traversal to the branch-off. fromNode's
        // own weight already spans the whole road to its far contact, so branch off the pre-road base instead.
        double baseWeight    = fromNode->previous != nullptr ? fromNode->previous->weight : 0.0;
        double partialLength = fabs(anchor.anchor_s_ - entryS);
        aNode->weight        = baseWeight + roadCalc.CalcWeight(fromNode, routeStrategy, partialLength, road);
        pushTo.push(aNode);
    }
}
// [GT_ODR:vj-router-end]

LaneIndependentRouter::LaneIndependentRouter(OpenDrive *odr) : odr_(odr), roadCalculations_(RoadCalculations())
{
}

LaneIndependentRouter::~LaneIndependentRouter()
{
    clearQueue(unvisited_);
    clearVector(visited_);
}

// Gets the next pathnode for the nextroad based on current srcnode
std::vector<Node *> LaneIndependentRouter::GetNextNodes(Road *nextRoad, Road *targetRoad, Node *currentNode)
{
    std::vector<std::pair<int, int>> connectingLaneIds = GetConnectingLanes(currentNode, nextRoad);
    if (connectingLaneIds.empty())
    {
        return {};
    }

    std::vector<Node *> nextNodes;
    int                 targetLaneId = targetWaypoint_.GetLaneId();
    ;
    for (std::pair<int, int> lanePair : connectingLaneIds)
    {
        Node *pNode = nullptr;
        if (nextRoad == targetRoad && lanePair.second == targetLaneId)
        {
            // Target road found and driving in same direction, create a target node.
            pNode = CreateTargetNode(currentNode, nextRoad, lanePair);
        }
        else
        {
            RoadLink *nextLink = GetNextLink(currentNode, nextRoad);
            if (!nextLink)
            {
                // Dont add node if it does not have a link. (end of road)
                continue;
            }
            // create next non target node
            pNode                = new Node;
            pNode->link          = nextLink;
            pNode->road          = nextRoad;
            pNode->currentLaneId = lanePair.second;
            pNode->fromLaneId    = lanePair.first;
            pNode->previous      = currentNode;
            double nextWeight    = roadCalculations_.CalcWeight(currentNode, routeStrategy_, nextRoad->GetLength(), nextRoad);
            pNode->weight        = currentNode->weight + nextWeight;
        }
        if (pNode)
        {
            nextNodes.push_back(pNode);
        }
    }
    return nextNodes;
}

std::vector<Road *> LaneIndependentRouter::GetNextRoads(RoadLink *link, Road *currentRoad)
{
    std::vector<Road *> nextRoads;
    Road               *nextRoad;
    if (link->GetElementType() == RoadLink::ElementType::ELEMENT_TYPE_ROAD)
    {
        nextRoad = odr_->GetRoadById(link->GetElementId());
        if (nextRoad)  // Dont push nullptr
        {
            nextRoads.push_back(nextRoad);
        }
    }
    else if (link->GetElementType() == RoadLink::ElementType::ELEMENT_TYPE_JUNCTION)
    {
        // check all junction links (connecting roads) that has pivot road as incoming road
        Junction *junction = odr_->GetJunctionById(link->GetElementId());
        for (unsigned int j = 0; j < junction->GetNoConnectionsFromRoadId(currentRoad->GetId()); j++)
        {
            id_t roadId = junction->GetConnectingRoadIdFromIncomingRoadId(currentRoad->GetId(), j);
            nextRoad    = odr_->GetRoadById(roadId);
            if (nextRoad)  // Dont push nullptr
            {
                nextRoads.push_back(nextRoad);
            }
        }
    }
    return nextRoads;
}

RoadLink *LaneIndependentRouter::GetNextLink(Node *currentNode, Road *nextRoad)
{
    if (currentNode->link->GetElementType() == RoadLink::ELEMENT_TYPE_ROAD)
    {
        // [GT_ODR:vj-router] the node's link is a branch road's OWN elementS link (contact UNDEFINED) merging back
        // onto the unsplit main road mid-span. The onward link on the main road is picked by the elementDir rule:
        // '+' means the main road continues in +s past the anchor (SUCCESSOR), '-' toward -s (PREDECESSOR). On a
        // link-less main road this is null = end of route (the merge-back target is handled before GetNextLink).
        if (currentNode->link->GetElementS() >= 0.0 && currentNode->link->GetContactPointType() == ContactPointType::CONTACT_POINT_UNDEFINED)
        {
            return nextRoad->GetLink(currentNode->link->GetElementDir() == RoadLink::DIR_MINUS ? LinkType::PREDECESSOR : LinkType::SUCCESSOR);
        }
        // node link is a road, find link in the other end of it
        if (currentNode->link->GetContactPointType() == ContactPointType::CONTACT_POINT_END)
        {
            return nextRoad->GetLink(LinkType::PREDECESSOR);
        }
        else
        {
            return nextRoad->GetLink(LinkType::SUCCESSOR);
        }
    }
    else if (currentNode->link->GetElementType() == RoadLink::ElementType::ELEMENT_TYPE_JUNCTION)
    {
        Junction *junction = Position::GetOpenDrive()->GetJunctionById(currentNode->link->GetElementId());
        id_t      elementId;
        if (junction && junction->GetType() == Junction::JunctionType::DIRECT)
        {
            elementId = junction->GetId();
        }
        else
        {
            // Default junction
            elementId = currentNode->road->GetId();
        }

        if (nextRoad->GetLink(LinkType::SUCCESSOR) && nextRoad->GetLink(LinkType::SUCCESSOR)->GetElementId() == elementId)
        {
            return nextRoad->GetLink(LinkType::PREDECESSOR);
        }
        else if (nextRoad->GetLink(LinkType::PREDECESSOR) && nextRoad->GetLink(LinkType::PREDECESSOR)->GetElementId() == elementId)
        {
            return nextRoad->GetLink(LinkType::SUCCESSOR);
        }
    }
    // end of road
    return nullptr;
}

std::vector<std::pair<int, int>> LaneIndependentRouter::GetConnectingLanes(Node *currentNode, Road *nextRoad)
{
    LaneSection *lanesection = nullptr;
    if (currentNode->anchorS >= 0.0)
    {
        // [GT_ODR:vj-router] the hop leaves the main road mid-road at the anchor s -- resolve source lanes at the
        // anchor's lane section, not an end section (the VJ-aware GetConnectingLaneId then maps via the connection)
        lanesection = currentNode->road->GetLaneSectionByS(currentNode->anchorS);
    }
    else if (currentNode->link->GetType() == LinkType::SUCCESSOR)
    {
        unsigned int nrOfLanesection = currentNode->road->GetNumberOfLaneSections();
        lanesection                  = currentNode->road->GetLaneSectionByIdx(nrOfLanesection - 1);
    }
    else
    {
        lanesection = currentNode->road->GetLaneSectionByIdx(0);
    }

    std::vector<std::pair<int, int>> connectingLaneIds;
    unsigned int                     nrOfLanes = lanesection->GetNumberOfLanes();
    for (unsigned int i = 0; i < nrOfLanes; i++)
    {
        Lane *lane          = lanesection->GetLaneByIdx(i);
        int   currentlaneId = lane->GetId();
        if (lane->IsDriving() && SIGN(currentlaneId) == SIGN(currentNode->currentLaneId) && lane->GetId() != 0)
        {
            int nextLaneId = currentNode->road->GetConnectingLaneId(currentNode->link, currentlaneId, nextRoad->GetId());
            if (nextLaneId != 0)
            {
                connectingLaneIds.push_back({currentlaneId, nextLaneId});
            }
        }
    }

    return connectingLaneIds;
}

Node *LaneIndependentRouter::CreateTargetNode(Node *currentNode, Road *nextRoad, std::pair<int, int> laneIds)
{
    // Create last node (targetnode)
    Node *targetNode          = new Node;
    targetNode->previous      = currentNode;
    targetNode->road          = nextRoad;
    targetNode->currentLaneId = laneIds.second;
    targetNode->fromLaneId    = laneIds.first;
    targetNode->link          = nullptr;
    double nextWeight         = roadCalculations_.CalcWeightWithPos(currentNode, targetWaypoint_, nextRoad, routeStrategy_);
    targetNode->weight        = currentNode->weight + nextWeight;
    return targetNode;
}

bool LaneIndependentRouter::FindGoal()
{
    while (!unvisited_.empty())
    {
        Node *currentNode = unvisited_.top();
        unvisited_.pop();
        bool nodeIsVisited =
            std::find_if(visited_.begin(), visited_.end(), [currentNode](const Node *n) { return *n == *currentNode; }) != visited_.end();
        if (nodeIsVisited)
        {
            delete currentNode;
            continue;
        }
        visited_.push_back(currentNode);
        Road *targetRoad   = odr_->GetRoadById(targetWaypoint_.GetTrackId());
        int   targetLaneId = targetWaypoint_.GetLaneId();
        if (currentNode->road == targetRoad && currentNode->currentLaneId == targetLaneId)
        {
            return true;
        }
        if (!currentNode->link)
        {
            continue;
        }
        // [GT_ODR:vj-router] expand mid-road anchors on an already-reached (non-start) road: the road was entered
        // at a contact end, so entryS/direction follow the node's far-end link type. The start road is seeded in
        // CalculatePath (with the real start s), so skip anchor nodes and the start node here to avoid duplicates.
        if (currentNode->previous != nullptr && currentNode->anchorS < 0.0)
        {
            bool travelToEnd = currentNode->link->GetType() == LinkType::SUCCESSOR;
            InjectVirtualJunctionAnchorNodes(odr_,
                                             currentNode,
                                             travelToEnd ? 0.0 : currentNode->road->GetLength(),
                                             travelToEnd,
                                             unvisited_,
                                             visited_,
                                             roadCalculations_,
                                             routeStrategy_);
        }
        std::vector<Road *> nextRoads = GetNextRoads(currentNode->link, currentNode->road);
        for (Road *nextRoad : nextRoads)
        {
            std::vector<Node *> nextNodes = GetNextNodes(nextRoad, targetRoad, currentNode);
            for (Node *n : nextNodes)
            {
                unvisited_.push(n);
            }
        }
    }
    return false;
}

bool LaneIndependentRouter::IsPositionValid(Position pos) const
{
    Road *road = odr_->GetRoadById(pos.GetTrackId());
    if (!road)
    {
        return false;
    }
    if (pos.GetS() > road->GetLength() || pos.GetS() < 0)
    {
        return false;
    }
    LaneSection *laneSection = road->GetLaneSectionByS(pos.GetS());
    Lane        *lane        = laneSection->GetLaneById(pos.GetLaneId());
    if (!lane)
    {
        return false;
    }
    return lane->IsDriving();  // true if lane is defined as drivable
}

Node *LaneIndependentRouter::CreateStartNode(RoadLink *link, Road *road, int laneId, ContactPointType contactPoint, Position pos)
{
    Node *startNode          = new Node;
    startNode->link          = link;
    startNode->road          = road;
    startNode->currentLaneId = laneId;
    startNode->fromLaneId    = 0;
    startNode->previous      = 0;

    double roadLength = 0;

    if (contactPoint == ContactPointType::CONTACT_POINT_START)
    {
        roadLength = pos.GetS();
    }
    else if (contactPoint == ContactPointType::CONTACT_POINT_END)
    {
        roadLength = road->GetLength() - pos.GetS();
    }

    double nextWeight = 0;
    if (routeStrategy_ == Position::RouteStrategy::SHORTEST)
    {
        nextWeight = roadLength;
    }
    else if (routeStrategy_ == Position::RouteStrategy::FASTEST)
    {
        double averageSpeed = roadCalculations_.CalcAverageSpeed(road);
        nextWeight          = roadLength / averageSpeed;
    }
    else if (routeStrategy_ == Position::RouteStrategy::MIN_INTERSECTIONS)
    {
        nextWeight = 0;
    }
    startNode->weight = nextWeight;
    return startNode;
}

std::vector<Node> LaneIndependentRouter::CalculatePath(Position start, Position target)
{
    clearQueue(unvisited_);
    clearVector(visited_);

    if (!IsPositionValid(start))
    {
        LOG_ERROR("(LaneIndependentRouter::CalculatePath) Error: Start position is invalid");
        return {};
    }
    if (!IsPositionValid(target))
    {
        LOG_ERROR("(LaneIndependentRouter::CalculatePath) Error: Target position is invalid");
        return {};
    }

    Road *startRoad   = odr_->GetRoadById(start.GetTrackId());
    int   startLaneId = start.GetLaneId();

    targetWaypoint_    = target;
    Road *targetRoad   = odr_->GetRoadById(targetWaypoint_.GetTrackId());
    int   targetLaneId = targetWaypoint_.GetLaneId();

    // Get routestrategy from traget position
    routeStrategy_ = target.GetRouteStrategy();

    ContactPointType contactPoint         = ContactPointType::CONTACT_POINT_START;
    RoadLink        *nextElement          = nullptr;
    bool             isInForwardDirection = start.GetHRelative() < M_PI_2 || start.GetHRelative() > 3 * M_PI_2;

    if (isInForwardDirection)
    {
        // Along road direction
        contactPoint = ContactPointType::CONTACT_POINT_END;
        nextElement  = startRoad->GetLink(LinkType::SUCCESSOR);  // Find link to next road or junction
    }
    else
    {
        // Opposite road direction
        nextElement = startRoad->GetLink(LinkType::PREDECESSOR);  // Find link to previous road or junction
    }

    // If start and end waypoint are on the same road and same lane,
    // no pathToGoal is needed
    if (startRoad == targetRoad && startLaneId == targetLaneId)
    {
        LOG_ERROR("(LaneIndependentRouter::CalculatePath) Error: start pos and target pos on same road and lane");
        return {};
    }

    // [GT_ODR:vj-router] a virtual-junction main road may carry NO end link yet still branch off mid-road through
    // a registry anchor -- tolerate a missing end link when reachable anchors exist in the travel direction.
    bool hasReachableAnchor = false;
    for (const OpenDrive::VirtualJunctionAnchor &anchor : odr_->GetVirtualJunctionAnchors(startRoad->GetId()))
    {
        hasReachableAnchor = hasReachableAnchor || (contactPoint == ContactPointType::CONTACT_POINT_END ? anchor.anchor_s_ >= start.GetS()
                                                                                                        : anchor.anchor_s_ <= start.GetS());
    }
    if (!nextElement && !hasReachableAnchor)
    {
        // No link (next road element) found
        LOG_ERROR("(LaneIndependentRouter::CalculatePath) Error: No link from start pos");
        return {};
    }

    Node *startNode = CreateStartNode(nextElement, startRoad, startLaneId, contactPoint, start);
    unvisited_.push(startNode);
    // [GT_ODR:vj-router] a link-less main road seeds only anchor nodes; a linked road seeds both. entryS = the
    // real start s, direction from the start heading (CONTACT_POINT_END == along +s).
    InjectVirtualJunctionAnchorNodes(odr_,
                                     startNode,
                                     start.GetS(),
                                     contactPoint == ContactPointType::CONTACT_POINT_END,
                                     unvisited_,
                                     visited_,
                                     roadCalculations_,
                                     routeStrategy_);

    bool              found = FindGoal();
    std::vector<Node> pathToGoal;
    if (found)
    {
        Node *nodeIterator = visited_.back();
        while (nodeIterator != 0)
        {
            pathToGoal.push_back(*nodeIterator);
            nodeIterator = nodeIterator->previous;
        }
    }
    else
    {
        LOG_WARN("(LaneIndependentRouter::CalculatePath) Warning: Path to target not found");
    }
    std::reverse(pathToGoal.begin(), pathToGoal.end());
    return pathToGoal;
}

std::vector<Position> LaneIndependentRouter::GetWaypoints(std::vector<Node> path, Position start, Position target)
{
    std::vector<Position> waypoints;
    for (unsigned int idx = 0; idx < path.size() - 1; idx++)
    {
        Node  *current    = &path[idx];
        Node  *next       = &path[idx + 1];
        double laneLength = 0;
        double sPos       = 0;
        double heading    = 0;
        // [GT_ODR:vj-router-begin] anchored transitions on a virtual-junction main road. A link-less start node (no
        // end link, seeded only for its anchors) emits the route origin; an anchor node (its link rides the
        // registry anchor onto the branch) emits the branch-off waypoint AT the anchor s on the main road. Heading
        // is along +s for a '+' anchor (dir '-' would flip it), matching the elementDir reverse-merge convention.
        if (current->link == nullptr || current->anchorS >= 0.0)
        {
            const double s = current->link == nullptr ? start.GetS() : current->anchorS;
            const double h = (current->link != nullptr && current->link->GetElementDir() == RoadLink::DIR_MINUS) ? M_PI : 0.0;
            Position     p(current->road->GetId(), current->currentLaneId, s, 0.0);
            p.SetHeadingRelativeRoadDirection(h);
            waypoints.push_back(p);
            continue;
        }
        // [GT_ODR:vj-router-end]
        if (current->link->GetType() == LinkType::SUCCESSOR)
        {
            for (unsigned int i = 0; i < current->road->GetNumberOfLaneSections(); i++)
            {
                LaneSection *lsec = current->road->GetLaneSectionByIdx(current->road->GetNumberOfLaneSections() - 1 - i);
                Lane        *lane = lsec->GetLaneById(next->fromLaneId);
                if (!lane || !lane->IsDriving())
                {
                    break;
                }
                laneLength += lsec->GetLength();
            }

            heading = 0;
            // calculate sPos to be the middle of the road
            sPos = current->road->GetLength() - (laneLength / 2);
            if (idx == 0 && start.GetS() > current->road->GetLength() - laneLength)
            {
                sPos = start.GetS();
            }
        }
        else if (current->link->GetType() == LinkType::PREDECESSOR)
        {
            for (unsigned int i = 0; i < current->road->GetNumberOfLaneSections(); i++)
            {
                Lane *lane = current->road->GetLaneSectionByIdx(i)->GetLaneById(next->fromLaneId);
                if (!lane || !lane->IsDriving())
                {
                    break;
                }
                laneLength += current->road->GetLaneSectionByIdx(i)->GetLength();
            }

            heading = M_PI;
            // calculate sPos to be the middle of the road
            sPos = laneLength / 2;
            if (idx == 0 && start.GetS() < laneLength)
            {
                sPos = start.GetS();
            }
        }
        Position p(current->road->GetId(), next->fromLaneId, sPos, 0.0);
        p.SetHeadingRelativeRoadDirection(heading);
        waypoints.push_back(p);
    }

    waypoints.push_back(target);

    return waypoints;
}

double RoadCalculations::CalcAverageSpeed(Road *road)
{
    unsigned int roadTypeCount = road->GetNumberOfRoadTypes();
    double       default_speed = 19.444;
    if (roadTypeCount == 0)
    {
        // Assume road is rural
        LOG_WARN("Warning: Road {} has no road types (and speed limit). Returning default speed {} m/s", road->GetId(), default_speed);

        return default_speed;
    }

    double totalSpeed = 0;
    for (auto &t : road->GetRoadType())
    {
        switch (t.second->road_type_)
        {
            case Road::RoadType::ROADTYPE_PEDESTRIAN:
            case Road::RoadType::ROADTYPE_BICYCLE:
            {
                if (t.second->speed_ > SMALL_NUMBER)
                {
                    totalSpeed += t.second->speed_;
                }
                else
                {
                    totalSpeed += 1.389;
                }
                break;
            }
            case Road::RoadType::ROADTYPE_LOWSPEED:
            case Road::RoadType::ROADTYPE_TOWNPLAYSTREET:
            case Road::RoadType::ROADTYPE_TOWNPRIVATE:
            {
                if (t.second->speed_ > SMALL_NUMBER)
                {
                    totalSpeed += t.second->speed_;
                }
                else
                {
                    totalSpeed += 8.333;
                }
                break;
            }
            case Road::RoadType::ROADTYPE_TOWN:
            case Road::RoadType::ROADTYPE_TOWNLOCAL:
            {
                if (t.second->speed_ > SMALL_NUMBER)
                {
                    totalSpeed += t.second->speed_;
                }
                else
                {
                    totalSpeed += 13.888;
                }
                break;
            }
            case Road::RoadType::ROADTYPE_RURAL:
            case Road::RoadType::ROADTYPE_UNKNOWN:
            {
                if (t.second->speed_ > SMALL_NUMBER)
                {
                    totalSpeed += t.second->speed_;
                }
                else
                {
                    totalSpeed += 19.444;
                }
                break;
            }
            case Road::RoadType::ROADTYPE_MOTORWAY:
            {
                if (t.second->speed_ > SMALL_NUMBER)
                {
                    totalSpeed += t.second->speed_;
                }
                else
                {
                    totalSpeed += 25.0;
                }
                break;
            }
            default:
            {
                LOG_WARN("Warning: Road {} has undefined road type. Setting default speed {} m/s", road->GetId(), default_speed);
                totalSpeed += default_speed;
                break;
            }
        }
    }

    return totalSpeed / static_cast<double>(roadTypeCount);
}

double RoadCalculations::CalcWeightWithPos(Node *previousNode, Position pos, Road *road, Position::RouteStrategy routeStrategy)
{
    double roadLength = 0;

    // [GT_ODR:vj-router] merge-back target: the incoming link is a mid-road elementS anchor (contact UNDEFINED) that
    // lands on the target road at element_s_; the traversed length is the partial |anchor - target s|, not a whole leg.
    if (previousNode->link->GetElementS() >= 0.0)
    {
        roadLength = fabs(previousNode->link->GetElementS() - pos.GetS());
    }
    else if (previousNode->link->GetContactPointType() == ContactPointType::CONTACT_POINT_START)
    {
        roadLength = pos.GetS();
    }
    else if (previousNode->link->GetContactPointType() == ContactPointType::CONTACT_POINT_END)
    {
        roadLength = road->GetLength() - pos.GetS();
    }

    return CalcWeight(previousNode, routeStrategy, roadLength, road);
}

double RoadCalculations::CalcWeight(Node *previousNode, Position::RouteStrategy routeStrategy, double roadLength, Road *road)
{
    if (routeStrategy == Position::RouteStrategy::SHORTEST)
    {
        return roadLength;
    }
    else if (routeStrategy == Position::RouteStrategy::FASTEST)
    {
        double averageSpeed = CalcAverageSpeed(road);
        if (averageSpeed == 0)
        {
            // If average speed is 0, road can't be traveled on
            return LARGE_NUMBER;
        }

        return roadLength / averageSpeed;
    }
    else if (routeStrategy == Position::RouteStrategy::MIN_INTERSECTIONS)
    {
        // [GT_ODR:vj-router] a link-less virtual-junction main-road start node has no link; branching off it
        // mid-road crosses no ordinary junction, so it costs no intersection.
        if (previousNode->link != nullptr && previousNode->link->GetElementType() == RoadLink::ELEMENT_TYPE_JUNCTION)
        {
            return 1;
        }
        return 0;
    }
    else
    {
        LOG_ERROR("Error: Position::RouteStrategy weight calculation is not defined");
        return 0;
    }
}