/*
 * esmini - Environment Simulator Minimalistic
 * https://github.com/esmini/esmini
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) partners of Simulation Scenarios
 * https://sites.google.com/view/simulationscenarios
 */

#include "CommonMini.hpp"
#include "OSIReporter.hpp"
#include "GT_OSIReporter_Internals.hpp"
#include <algorithm>
#include <set>

namespace
{
constexpr const char *kSourceRefTypeOdr = "net.asam.opendrive";
}

int OSIReporter::UpdateOSIIntersection()
{
    // NOTE: for free_lane_boundary this algoritm will only work for open drive solid roadmarks in the junction (or atleast the outest driving lane's
    // roadmark)

    // tolerance to check if points are close or not
    double tolerance = 0.01;

    typedef struct
    {
        id_t                    road_id;
        double                  length;
        idx_t                   g_id;
        roadmanager::OSIPoints *osipoints;
    } LaneLengthStruct;

    roadmanager::Junction         *junction;
    roadmanager::Connection       *connection;
    roadmanager::JunctionLaneLink *junctionlanelink;
    roadmanager::Road             *incomming_road;
    roadmanager::Road             *outgoing_road;
    roadmanager::Road             *connecting_road;
    roadmanager::ContactPointType  contactpoint;
    roadmanager::RoadLink         *roadlink          = 0;
    LaneLengthStruct               left_lane_struct  = {0, 0.0, 0, nullptr};
    LaneLengthStruct               right_lane_struct = {0, 0.0, 0, nullptr};
    // s values to know where on the road to check for the lanes
    double incomming_s_value;
    double outgoing_s_value;
    double connecting_outgoing_s_value;

    // value for the linktype for the connecting road and the outgoing road
    roadmanager::LinkType connecting_road_link_type = roadmanager::LinkType::NONE;
    // value for the linktype for the incomming road and the connecting road
    roadmanager::LinkType incomming_road_link_type = roadmanager::LinkType::NONE;
    // some values used for fixing free lane boundary
    double                  length;
    bool                    new_connecting_road;
    idx_t                   g_id;
    roadmanager::OSIPoints *osipoints;

    static roadmanager::OpenDrive *opendrive = roadmanager::Position::GetOpenDrive();
    osi3::Lane                    *osi_lane  = nullptr;
    for (unsigned int i = 0; i < opendrive->GetNumOfJunctions(); i++)
    {
        // add check if it is an intersection or an highway exit/entry
        junction = opendrive->GetJunctionByIdx(i);

        if (junction->GetType() == roadmanager::Junction::JunctionType::DIRECT)
        {
            // resolve direct junction connections
            for (auto &c : junction->GetConnections())
            {
                roadmanager::Road        *road_in          = c->GetIncomingRoad();
                roadmanager::Road        *road_out         = c->GetConnectingRoad();
                roadmanager::LaneSection *lane_section_in  = road_in->GetLaneSectionByIdx(road_in->GetNumberOfLaneSections() - 1);
                roadmanager::LaneSection *lane_section_out = road_out->GetLaneSectionByIdx(0);
                for (unsigned int l = 0; l < c->GetNumberOfLaneLinks(); l++)
                {
                    roadmanager::JunctionLaneLink *ll             = c->GetLaneLink(l);
                    int                            from_lane_id   = ll->from_;
                    int                            to_lane_id     = ll->to_;
                    idx_t                          from_global_id = lane_section_in->GetLaneGlobalIdById(from_lane_id);
                    idx_t                          to_global_id   = lane_section_out->GetLaneGlobalIdById(to_lane_id);

                    // locate outgoing lane and register incoming lane
                    for (unsigned int jj = 0; jj < obj_osi_internal.ln.size(); jj++)
                    {
                        if (obj_osi_internal.ln[jj]->mutable_id()->value() == to_global_id)
                        {
                            osi_lane                                            = obj_osi_internal.ln[jj];
                            osi3::Lane_Classification_LanePairing *lane_pairing = nullptr;

                            if (osi_lane->mutable_classification()->mutable_lane_pairing()->size() == 0)
                            {
                                // create lane pairing element to add first connection to one of the ends
                                lane_pairing = osi_lane->mutable_classification()->add_lane_pairing();
                            }
                            else
                            {
                                if (osi_lane->mutable_classification()->mutable_lane_pairing()->size() > 1)
                                {
                                    LOG_ERROR("Unexpected lane pairing size for osi lane {}", to_global_id);
                                }
                                // reuse existing lane pairing element to add connection for the other end
                                lane_pairing = osi_lane->mutable_classification()->mutable_lane_pairing(0);
                            }

                            // all connections are mutual, i.e. any incoming->outgoing pair exists twice, one for each direction.
                            // Hence, register only one way here. Register if for the to-lane, since that direction is known.
                            if (c->GetContactPoint() == roadmanager::ContactPointType::CONTACT_POINT_END)
                            {
                                lane_pairing->mutable_successor_lane_id()->set_value(from_global_id);
                            }
                            else if (c->GetContactPoint() == roadmanager::ContactPointType::CONTACT_POINT_START)
                            {
                                lane_pairing->mutable_antecessor_lane_id()->set_value(from_global_id);
                            }
                            else
                            {
                                LOG_ERROR("Unexpected direct junction lane link contact point (junction {})", junction->GetId());
                            }
                            break;
                        }
                    }
                }
            }
        }
        else if (junction->IsOsiIntersection())
        {
            // genereric data for the junction
            osi_lane = obj_osi_internal.static_gt->add_lane();
            osi_lane->mutable_id()->set_value(junction->GetGlobalId());
            osi_lane->mutable_classification()->set_type(osi3::Lane_Classification_Type::Lane_Classification_Type_TYPE_INTERSECTION);
            std::vector<LaneLengthStruct> left_lane_lengths;
            std::vector<LaneLengthStruct> right_lane_lengths;
            std::vector<LaneLengthStruct> lane_lengths;
            std::vector<LaneLengthStruct> tmp_lane_lengths;
            std::set<id_t>                connected_roads;
            // check all connections in the junction
            for (unsigned int j = 0; j < junction->GetNumberOfConnections(); j++)
            {
                connection          = junction->GetConnectionByIdx(j);
                incomming_road      = connection->GetIncomingRoad();
                connecting_road     = connection->GetConnectingRoad();
                new_connecting_road = true;

                if (incomming_road == nullptr)
                {
                    LOG_WARN("WARNING: Can't find incoming road to intersection, can't establish an OSI intersection");
                    return -1;
                }

                if (connecting_road == nullptr)
                {
                    LOG_WARN("WARNING: Can't find connectiong road in intersection, can't establish an OSI intersection");
                    return -1;
                }

                // check if the connecting road has been used before
                for (unsigned int l = 0; l < lane_lengths.size(); l++)
                {
                    if (lane_lengths[l].road_id == connecting_road->GetId())
                    {
                        new_connecting_road = false;
                    }
                }

                for (unsigned int l = 0; l < left_lane_lengths.size(); l++)
                {
                    if (left_lane_lengths[l].road_id == connecting_road->GetId())
                    {
                        new_connecting_road = false;
                    }
                }

                // get needed info about the incomming road
                if (incomming_road->GetLink(roadmanager::LinkType::SUCCESSOR) != 0)
                {
                    if (incomming_road->GetLink(roadmanager::LinkType::SUCCESSOR)->GetElementId() == connecting_road->GetJunction())
                    {
                        incomming_s_value        = incomming_road->GetLength();
                        incomming_road_link_type = roadmanager::LinkType::SUCCESSOR;
                    }
                    else
                    {
                        incomming_s_value        = 0;
                        incomming_road_link_type = roadmanager::LinkType::PREDECESSOR;
                    }
                }
                else
                {
                    incomming_s_value        = 0;
                    incomming_road_link_type = roadmanager::LinkType::PREDECESSOR;
                }

                // Get info about the connecting road, and to get the correct outgoing road
                contactpoint = connection->GetContactPoint();
                if (contactpoint == roadmanager::ContactPointType::CONTACT_POINT_START)
                {
                    connecting_road_link_type   = roadmanager::LinkType::SUCCESSOR;
                    roadlink                    = connecting_road->GetLink(connecting_road_link_type);
                    connecting_outgoing_s_value = connecting_road->GetLength();
                }
                else if (contactpoint == roadmanager::ContactPointType::CONTACT_POINT_END)
                {
                    connecting_road_link_type   = roadmanager::LinkType::PREDECESSOR;
                    roadlink                    = connecting_road->GetLink(connecting_road_link_type);
                    connecting_outgoing_s_value = 0;
                }
                else
                {
                    LOG_WARN("WARNING: Unknow connection detected, can't establish outgoing connection in OSI junction");
                    return -1;
                }
                if (roadlink == nullptr)
                {
                    LOG_WARN("Failed to resolve {} link of connected road id {} with incoming road id {}",
                             roadmanager::OpenDrive::LinkType2Str(connecting_road_link_type),
                             connecting_road->GetId(),
                             incomming_road->GetId());
                    continue;
                }

                if (roadlink->GetElementType() == roadmanager::RoadLink::ElementType::ELEMENT_TYPE_JUNCTION)
                {
                    LOG_WARN("Failed to resolve outgoing road of connecting road {} from incoming road id {}, link is a junction - not yet supported",
                             connecting_road->GetId(),
                             incomming_road->GetId());
                    continue;
                }
                outgoing_road = opendrive->GetRoadById(roadlink->GetElementId());
                connected_roads.insert(incomming_road->GetId());
                connected_roads.insert(outgoing_road->GetId());
                // Get neccesary info about the outgoing road
                if (outgoing_road->GetLink(roadmanager::LinkType::SUCCESSOR) != 0)
                {
                    if (outgoing_road->GetLink(roadmanager::LinkType::SUCCESSOR)->GetElementId() == connecting_road->GetJunction())
                    {
                        outgoing_s_value = outgoing_road->GetLength();
                    }
                    else
                    {
                        outgoing_s_value = 0;
                    }
                }
                else
                {
                    outgoing_s_value = 0;
                }

                if (new_connecting_road)
                {
                    left_lane_struct.road_id  = connecting_road->GetId();
                    left_lane_struct.length   = LARGE_NUMBER;
                    right_lane_struct.road_id = connecting_road->GetId();
                    right_lane_struct.length  = LARGE_NUMBER;

                    for (int l_id = 1; static_cast<unsigned int>(l_id) <= connecting_road->GetLaneSectionByS(0, 0)->GetNUmberOfLanesRight(); l_id++)
                    {
                        if (connecting_road->GetLaneSectionByS(0, 0)->GetLaneById(-l_id)->IsDriving())
                        {
                            // check if an roadmark exist or use a laneboundary
                            // NOTE: assumes only simple lines in an intersection
                            if (connecting_road->GetLaneSectionByS(0, 0)->GetLaneById(-l_id)->GetLaneBoundaryGlobalId() != ID_UNDEFINED)
                            {
                                osipoints = connecting_road->GetLaneSectionByS(0, 0)->GetLaneById(-l_id)->GetLaneBoundary()->GetOSIPoints();
                                length    = osipoints->GetLength();
                                g_id      = connecting_road->GetLaneSectionByS(0, 0)->GetLaneById(-l_id)->GetLaneBoundary()->GetGlobalId();
                            }
                            else
                            {
                                osipoints = connecting_road->GetLaneSectionByS(0, 0)
                                                ->GetLaneById(-l_id)
                                                ->GetLaneRoadMarkByIdx(0)
                                                ->GetLaneRoadMarkTypeByIdx(0)
                                                ->GetLaneRoadMarkTypeLineByIdx(0)
                                                ->GetOSIPoints();
                                length = connecting_road->GetLaneSectionByS(0, 0)
                                             ->GetLaneById(-l_id)
                                             ->GetLaneRoadMarkByIdx(0)
                                             ->GetLaneRoadMarkTypeByIdx(0)
                                             ->GetLaneRoadMarkTypeLineByIdx(0)
                                             ->GetOSIPoints()
                                             ->GetLength();
                                g_id = connecting_road->GetLaneSectionByS(0, 0)
                                           ->GetLaneById(-l_id)
                                           ->GetLaneRoadMarkByIdx(0)
                                           ->GetLaneRoadMarkTypeByIdx(0)
                                           ->GetLaneRoadMarkTypeLineByIdx(0)
                                           ->GetGlobalId();
                            }
                            if ((right_lane_struct.length > length) || (fabs(right_lane_struct.length - length) < tolerance))
                            {
                                right_lane_struct.length    = length;
                                right_lane_struct.g_id      = g_id;
                                right_lane_struct.osipoints = osipoints;
                            }
                        }
                    }
                    for (int l_id = 1; static_cast<unsigned int>(l_id) <= connecting_road->GetLaneSectionByS(0, 0)->GetNUmberOfLanesLeft(); l_id++)
                    {
                        if (connecting_road->GetLaneSectionByS(0)->GetLaneById(l_id)->IsDriving())
                        {
                            if (connecting_road->GetLaneSectionByS(0, 0)->GetLaneById(l_id)->GetLaneBoundaryGlobalId() != ID_UNDEFINED)
                            {
                                osipoints = connecting_road->GetLaneSectionByS(0, 0)->GetLaneById(l_id)->GetLaneBoundary()->GetOSIPoints();
                                length = connecting_road->GetLaneSectionByS(0, 0)->GetLaneById(l_id)->GetLaneBoundary()->GetOSIPoints()->GetLength();
                                g_id   = connecting_road->GetLaneSectionByS(0, 0)->GetLaneById(l_id)->GetLaneBoundary()->GetGlobalId();
                            }
                            else
                            {
                                osipoints = connecting_road->GetLaneSectionByS(0, 0)
                                                ->GetLaneById(l_id)
                                                ->GetLaneRoadMarkByIdx(0)
                                                ->GetLaneRoadMarkTypeByIdx(0)
                                                ->GetLaneRoadMarkTypeLineByIdx(0)
                                                ->GetOSIPoints();
                                length = connecting_road->GetLaneSectionByS(0, 0)
                                             ->GetLaneById(l_id)
                                             ->GetLaneRoadMarkByIdx(0)
                                             ->GetLaneRoadMarkTypeByIdx(0)
                                             ->GetLaneRoadMarkTypeLineByIdx(0)
                                             ->GetOSIPoints()
                                             ->GetLength();
                                g_id = connecting_road->GetLaneSectionByS(0, 0)
                                           ->GetLaneById(l_id)
                                           ->GetLaneRoadMarkByIdx(0)
                                           ->GetLaneRoadMarkTypeByIdx(0)
                                           ->GetLaneRoadMarkTypeLineByIdx(0)
                                           ->GetGlobalId();
                            }
                            if ((left_lane_struct.length > length) || (fabs(right_lane_struct.length - length) < tolerance))
                            {
                                left_lane_struct.length    = length;
                                left_lane_struct.g_id      = g_id;
                                left_lane_struct.osipoints = osipoints;
                            }
                        }
                    }
                    if (fabs(left_lane_struct.length - right_lane_struct.length) < SMALL_NUMBER)
                    {
                        left_lane_lengths.push_back(left_lane_struct);
                        right_lane_lengths.push_back(right_lane_struct);
                    }
                    else
                    {
                        if (left_lane_struct.length < right_lane_struct.length)
                        {
                            lane_lengths.push_back(left_lane_struct);
                        }
                        else
                        {
                            lane_lengths.push_back(right_lane_struct);
                        }
                    }
                }
                bool right_hand_traffic = (incomming_road->GetRule() == roadmanager::Road::RoadRule::RIGHT_HAND_TRAFFIC ||
                                           connecting_road->GetRule() == roadmanager::Road::RoadRule::RIGHT_HAND_TRAFFIC);
                // create all lane parings for the junction
                for (unsigned int l = 0; l < connection->GetNumberOfLaneLinks(); l++)
                {
                    junctionlanelink = connection->GetLaneLink(l);
                    // check if the connecting road has been checked before, otherwise get the shortest laneboundary

                    // TODO: will only work for right hand traffic right now
                    if ((((incomming_road_link_type == roadmanager::LinkType::SUCCESSOR && junctionlanelink->from_ < 0) ||
                          (incomming_road_link_type == roadmanager::LinkType::PREDECESSOR && junctionlanelink->from_ > 0)) &&
                         incomming_road->GetDrivingLaneById(incomming_s_value, junctionlanelink->from_) != 0 && right_hand_traffic) ||
                        (((incomming_road_link_type == roadmanager::LinkType::SUCCESSOR && junctionlanelink->from_ > 0) ||
                          (incomming_road_link_type == roadmanager::LinkType::PREDECESSOR && junctionlanelink->from_ < 0)) &&
                         incomming_road->GetDrivingLaneById(incomming_s_value, junctionlanelink->to_) != 0 && !right_hand_traffic))
                    {
                        osi3::Lane_Classification_LanePairing *laneparing = osi_lane->mutable_classification()->add_lane_pairing();
                        laneparing->mutable_antecessor_lane_id()->set_value(
                            incomming_road->GetDrivingLaneById(incomming_s_value, junctionlanelink->from_)->GetGlobalId());

                        roadmanager::Lane *lane = connecting_road->GetDrivingLaneById(connecting_outgoing_s_value, junctionlanelink->to_);
                        if (!lane)
                        {
                            LOG_ERROR("Connecting road {} incoming road {} failed get lane by id {}",
                                      connecting_road->GetId(),
                                      connection->GetIncomingRoad()->GetId(),
                                      junctionlanelink->to_);
                            continue;
                        }

                        // GT-FORK sync: upstream 7a0844b1 (null lane_link check, esmini #780/#781)
                        roadmanager::LaneLink *lane_link = lane->GetLink(connecting_road_link_type);
                        if (!lane_link)
                        {
                            LOG_ERROR("Connecting road {} incoming road {} failed get lane by id {}, missing link: maybe vanishing lane?",
                                      connecting_road->GetId(),
                                      connection->GetIncomingRoad()->GetId(),
                                      junctionlanelink->to_);
                            continue;
                        }

                        roadmanager::Lane *successor_lane = outgoing_road->GetDrivingLaneById(outgoing_s_value, lane_link->GetId());
                        if (!successor_lane)
                        {
                            LOG_ERROR("Outgoing road {} incoming road {} failed get lane by id {}",
                                      outgoing_road->GetId(),
                                      connection->GetIncomingRoad()->GetId(),
                                      lane_link->GetId());
                            continue;
                        }

                        laneparing->mutable_successor_lane_id()->set_value(successor_lane->GetGlobalId());
                    }
                }
            }
            // sort the correct free-boundaries
            for (unsigned int j = 0; j < left_lane_lengths.size(); j++)
            {
                bool keep_right = true;
                bool keep_left  = true;
                for (unsigned int k = 0; k < lane_lengths.size(); k++)
                {
                    int same_left = roadmanager::CheckOverlapingOSIPoints(left_lane_lengths[j].osipoints, lane_lengths[k].osipoints, tolerance);
                    if (same_left < 0)
                    {
                        LOG_DEBUG(
                            "CheckOverlapingOSIPoints() -> left_lane_lengths road_id {} length {}, lane_lengths road_id {} length {}",
                            left_lane_lengths[j].road_id,
                            left_lane_lengths[j].osipoints == nullptr ? -1 : static_cast<int>(left_lane_lengths[j].osipoints->GetNumOfOSIPoints()),
                            lane_lengths[k].road_id,
                            lane_lengths[k].osipoints == nullptr ? -1 : static_cast<int>(lane_lengths[k].osipoints->GetNumOfOSIPoints()));
                    }
                    if (same_left > 0)
                    {
                        keep_left = false;
                    }
                    int same_right = roadmanager::CheckOverlapingOSIPoints(right_lane_lengths[j].osipoints, lane_lengths[k].osipoints, tolerance);
                    if (same_right < 0)
                    {
                        LOG_DEBUG(
                            "CheckOverlapingOSIPoints() -> right_lane_lengths road_id {} length {}, lane_lengths road_id {} length {}",
                            right_lane_lengths[j].road_id,
                            right_lane_lengths[j].osipoints == nullptr ? -1 : static_cast<int>(right_lane_lengths[j].osipoints->GetNumOfOSIPoints()),
                            lane_lengths[k].road_id,
                            lane_lengths[k].osipoints == nullptr ? -1 : static_cast<int>(lane_lengths[k].osipoints->GetNumOfOSIPoints()));
                    }
                    if (same_right > 0)
                    {
                        keep_right = false;
                    }
                }
                if (keep_left)
                {
                    tmp_lane_lengths.push_back(left_lane_lengths[j]);
                }
                else if (keep_right)
                {
                    tmp_lane_lengths.push_back(right_lane_lengths[j]);
                }
            }
            for (unsigned int j = 0; j < tmp_lane_lengths.size(); j++)
            {
                lane_lengths.push_back(tmp_lane_lengths[j]);
            }

            if (lane_lengths.size() == connected_roads.size())
            {
                for (unsigned int j = 0; j < lane_lengths.size(); j++)
                {
                    osi3::Identifier *free_lane_id = osi_lane->mutable_classification()->add_free_lane_boundary_id();
                    free_lane_id->set_value(lane_lengths[j].g_id);
                }
            }
            else
            {
                std::vector<int> ids_to_remove;
                for (unsigned int j = 0; j < lane_lengths.size(); j++)
                {
                    if (!(std::find(ids_to_remove.begin(), ids_to_remove.end(), static_cast<int>(j)) != ids_to_remove.end()))
                    {
                        for (unsigned int k = 0; k < lane_lengths.size(); k++)
                        {
                            if (k != j)
                            {
                                int same_points =
                                    roadmanager::CheckOverlapingOSIPoints(lane_lengths[k].osipoints, lane_lengths[j].osipoints, tolerance);
                                if (same_points > 0)
                                {
                                    if (lane_lengths[k].length < lane_lengths[j].length)
                                    {
                                        ids_to_remove.push_back(static_cast<int>(j));
                                    }
                                    else
                                    {
                                        ids_to_remove.push_back(static_cast<int>(k));
                                        continue;
                                    }
                                }
                            }
                        }
                    }
                }

                for (unsigned int j = 0; j < lane_lengths.size(); j++)
                {
                    if (!(std::find(ids_to_remove.begin(), ids_to_remove.end(), static_cast<int>(j)) != ids_to_remove.end()))
                    {
                        osi3::Identifier *free_lane_id = osi_lane->mutable_classification()->add_free_lane_boundary_id();
                        free_lane_id->set_value(lane_lengths[j].g_id);
                    }
                }
                LOG_WARN("Issues with the Intersection {} (global id {}) for the osi free lane boundary, not all lanes added.",
                         junction->GetId(),
                         junction->GetGlobalId());
            }
        }
    }

    // Lets Update the antecessor and successor lanes of the lanes that are not intersections
    // Get all the intersection lanes, this lanes have the predecessor and successor lanes information
    std::vector<osi3::Lane *> IntersectionLanes;
    for (int i = 0; i < obj_osi_internal.static_gt->lane_size(); ++i)
    {
        if (obj_osi_internal.static_gt->lane(i).classification().type() == osi3::Lane_Classification_Type::Lane_Classification_Type_TYPE_INTERSECTION)
        {
            IntersectionLanes.push_back(obj_osi_internal.static_gt->mutable_lane(i));
        }
    }

    // For each lane in OSI groundTruth
    for (int i = 0; i < obj_osi_internal.static_gt->lane_size(); ++i)
    {
        // Check if the lane is in the intersection
        for (unsigned int j = 0; j < IntersectionLanes.size(); ++j)
        {
            for (int k = 0; k < IntersectionLanes[j]->classification().lane_pairing_size(); ++k)
            {
                // Check predecessors
                if (IntersectionLanes[j]->classification().lane_pairing()[k].has_antecessor_lane_id())
                {
                    // It lane is in predecesor of the intersection
                    if (obj_osi_internal.static_gt->lane(i).id().value() ==
                        IntersectionLanes[j]->classification().lane_pairing()[k].antecessor_lane_id().value())
                    {
                        // then we add the intersection ID to the successor of the lane
                        if (obj_osi_internal.static_gt->mutable_lane(i)->mutable_classification()->lane_pairing_size() == 0)
                        {
                            obj_osi_internal.static_gt->mutable_lane(i)
                                ->mutable_classification()
                                ->add_lane_pairing()
                                ->mutable_successor_lane_id()
                                ->set_value(IntersectionLanes[j]->id().value());
                        }
                    }
                }

                // Check successors
                if (IntersectionLanes[j]->classification().lane_pairing()[k].has_successor_lane_id())
                {
                    // It lane is in successor of the intersection
                    if (obj_osi_internal.static_gt->lane(i).id().value() ==
                        IntersectionLanes[j]->classification().lane_pairing()[k].successor_lane_id().value())
                    {
                        // then we add the intersection ID to the predecessor of the lane
                        if (obj_osi_internal.static_gt->mutable_lane(i)->mutable_classification()->lane_pairing_size() == 0)
                        {
                            obj_osi_internal.static_gt->mutable_lane(i)
                                ->mutable_classification()
                                ->add_lane_pairing()
                                ->mutable_antecessor_lane_id()
                                ->set_value(IntersectionLanes[j]->id().value());
                        }
                    }
                }
            }
        }
    }

    return 0;
}

int OSIReporter::UpdateOSILaneBoundary()
{
    // Retrieve opendrive class from RoadManager
    static roadmanager::OpenDrive *opendrive = roadmanager::Position::GetOpenDrive();

    // Loop over all roads
    for (unsigned int i = 0; i < opendrive->GetNumOfRoads(); i++)
    {
        roadmanager::Road *road = opendrive->GetRoadByIdx(i);

        // loop over all lane sections
        for (unsigned int j = 0; j < road->GetNumberOfLaneSections(); j++)
        {
            roadmanager::LaneSection *lane_section = road->GetLaneSectionByIdx(j);

            // loop over all lanes
            for (unsigned int k = 0; k < lane_section->GetNumberOfLanes(); k++)
            {
                roadmanager::Lane *lane = lane_section->GetLaneByIdx(k);

                unsigned int n_roadmarks = lane->GetNumberOfRoadMarks();
                if (n_roadmarks != 0)  // if there are road marks
                {
                    // loop over RoadMarks
                    for (unsigned int ii = 0; ii < lane->GetNumberOfRoadMarks(); ii++)
                    {
                        roadmanager::LaneRoadMark *laneroadmark = lane->GetLaneRoadMarkByIdx(ii);

                        // loop over road mark types
                        for (unsigned int jj = 0; jj < laneroadmark->GetNumberOfRoadMarkTypes(); jj++)
                        {
                            roadmanager::LaneRoadMarkType *laneroadmarktype = laneroadmark->GetLaneRoadMarkTypeByIdx(jj);

                            idx_t inner_index = ID_UNDEFINED;
                            if (laneroadmark->GetType() == roadmanager::LaneRoadMark::RoadMarkType::BROKEN_SOLID ||
                                laneroadmark->GetType() == roadmanager::LaneRoadMark::RoadMarkType::SOLID_BROKEN)
                            {
                                if (laneroadmarktype->GetNumberOfRoadMarkTypeLines() < 2)
                                {
                                    LOG_ERROR_AND_QUIT("You need to specify at least 2 line for broken solid or solid broken roadmark type");
                                    break;
                                }
                                std::vector<double> sort_solidbroken_brokensolid;
                                for (unsigned int q = 0; q < laneroadmarktype->GetNumberOfRoadMarkTypeLines(); q++)
                                {
                                    sort_solidbroken_brokensolid.push_back(laneroadmarktype->GetLaneRoadMarkTypeLineByIdx(q)->GetTOffset());
                                }

                                if (lane->GetId() < 0 || lane->GetId() == 0)
                                {
                                    inner_index = static_cast<unsigned int>(
                                        std::max_element(sort_solidbroken_brokensolid.begin(), sort_solidbroken_brokensolid.end()) -
                                        sort_solidbroken_brokensolid.begin());
                                }
                                else
                                {
                                    inner_index = static_cast<unsigned int>(
                                        std::min_element(sort_solidbroken_brokensolid.begin(), sort_solidbroken_brokensolid.end()) -
                                        sort_solidbroken_brokensolid.begin());
                                }
                            }

                            // loop over LaneRoadMarkTypeLine
                            for (unsigned int kk = 0; kk < laneroadmarktype->GetNumberOfRoadMarkTypeLines(); kk++)
                            {
                                roadmanager::LaneRoadMarkTypeLine *laneroadmarktypeline = laneroadmarktype->GetLaneRoadMarkTypeLineByIdx(kk);

                                bool broken = false;
                                if (laneroadmark->GetType() == roadmanager::LaneRoadMark::RoadMarkType::BROKEN_SOLID)
                                {
                                    if (inner_index == kk)
                                    {
                                        broken = true;
                                    }
                                }

                                if (laneroadmark->GetType() == roadmanager::LaneRoadMark::RoadMarkType::SOLID_BROKEN)
                                {
                                    broken = true;
                                    if (inner_index == kk)
                                    {
                                        broken = false;
                                    }
                                }

                                if (laneroadmark->GetType() == roadmanager::LaneRoadMark::RoadMarkType::BROKEN ||
                                    laneroadmark->GetType() == roadmanager::LaneRoadMark::RoadMarkType::BROKEN_BROKEN)
                                {
                                    broken = true;
                                }

                                osi3::LaneBoundary *osi_laneboundary = 0;

                                idx_t line_id = laneroadmarktypeline->GetGlobalId();

                                // Check if this line is already pushed to OSI
                                for (unsigned int h = 0; h < obj_osi_internal.lnb.size(); h++)
                                {
                                    if (obj_osi_internal.lnb[h]->mutable_id()->value() == line_id)
                                    {
                                        osi_laneboundary = obj_osi_internal.lnb[h];
                                    }
                                }
                                if (!osi_laneboundary)
                                {
                                    osi_laneboundary = obj_osi_internal.static_gt->add_lane_boundary();

                                    // update id
                                    osi_laneboundary->mutable_id()->set_value(line_id);

                                    unsigned int n_osi_points = laneroadmarktypeline->GetOSIPoints()->GetNumOfOSIPoints();
                                    bool         startpoint   = true;
                                    for (unsigned int h = 0; h < n_osi_points; h++)
                                    {
                                        bool endpoint = laneroadmarktypeline->GetOSIPoints()->GetPoint(h).endpoint;

                                        if (broken && !startpoint && !endpoint)
                                        {
                                            // skip intermediate points
                                            continue;
                                        }

                                        osi3::LaneBoundary_BoundaryPoint *boundary_point = osi_laneboundary->add_boundary_line();
                                        boundary_point->mutable_position()->set_x(laneroadmarktypeline->GetOSIPoints()->GetXfromIdx(h));
                                        boundary_point->mutable_position()->set_y(laneroadmarktypeline->GetOSIPoints()->GetYfromIdx(h));
                                        boundary_point->mutable_position()->set_z(laneroadmarktypeline->GetOSIPoints()->GetZfromIdx(h));
                                        boundary_point->set_width(laneroadmarktypeline->GetWidth());
                                        boundary_point->set_height(laneroadmark->GetHeight());

                                        startpoint = endpoint ? true : false;
                                    }

                                    // update classification type
                                    osi3::LaneBoundary_Classification_Type classific_type;
                                    switch (laneroadmark->GetType())
                                    {
                                        case roadmanager::LaneRoadMark::RoadMarkType::NONE_TYPE:
                                            classific_type = osi3::LaneBoundary_Classification_Type::LaneBoundary_Classification_Type_TYPE_NO_LINE;
                                            break;
                                        case roadmanager::LaneRoadMark::RoadMarkType::SOLID:
                                            classific_type = osi3::LaneBoundary_Classification_Type::LaneBoundary_Classification_Type_TYPE_SOLID_LINE;
                                            break;
                                        case roadmanager::LaneRoadMark::RoadMarkType::SOLID_SOLID:
                                            classific_type = osi3::LaneBoundary_Classification_Type::LaneBoundary_Classification_Type_TYPE_SOLID_LINE;
                                            break;
                                        case roadmanager::LaneRoadMark::RoadMarkType::BROKEN:
                                            classific_type =
                                                osi3::LaneBoundary_Classification_Type::LaneBoundary_Classification_Type_TYPE_DASHED_LINE;
                                            break;
                                        case roadmanager::LaneRoadMark::RoadMarkType::BROKEN_BROKEN:
                                            classific_type =
                                                osi3::LaneBoundary_Classification_Type::LaneBoundary_Classification_Type_TYPE_DASHED_LINE;
                                            break;
                                        case roadmanager::LaneRoadMark::RoadMarkType::SOLID_BROKEN:
                                            if (broken)
                                            {
                                                classific_type =
                                                    osi3::LaneBoundary_Classification_Type::LaneBoundary_Classification_Type_TYPE_DASHED_LINE;
                                            }
                                            else
                                            {
                                                classific_type =
                                                    osi3::LaneBoundary_Classification_Type::LaneBoundary_Classification_Type_TYPE_SOLID_LINE;
                                            }
                                            break;
                                        case roadmanager::LaneRoadMark::RoadMarkType::BROKEN_SOLID:
                                            if (broken)
                                            {
                                                classific_type =
                                                    osi3::LaneBoundary_Classification_Type::LaneBoundary_Classification_Type_TYPE_DASHED_LINE;
                                            }
                                            else
                                            {
                                                classific_type =
                                                    osi3::LaneBoundary_Classification_Type::LaneBoundary_Classification_Type_TYPE_SOLID_LINE;
                                            }
                                            break;
                                        case roadmanager::LaneRoadMark::RoadMarkType::BOTTS_DOTS:
                                            classific_type = osi3::LaneBoundary_Classification_Type::LaneBoundary_Classification_Type_TYPE_BOTTS_DOTS;
                                            break;
                                        default:
                                            classific_type = osi3::LaneBoundary_Classification_Type::LaneBoundary_Classification_Type_TYPE_SOLID_LINE;
                                    }
                                    osi_laneboundary->mutable_classification()->set_type(classific_type);

                                    // update classification color
                                    osi3::LaneBoundary_Classification_Color classific_col;
                                    switch (laneroadmark->GetColor())
                                    {
                                        case roadmanager::RoadMarkColor::STANDARD:
                                            classific_col = osi3::LaneBoundary_Classification_Color::LaneBoundary_Classification_Color_COLOR_WHITE;
                                            break;
                                        case roadmanager::RoadMarkColor::BLUE:
                                            classific_col = osi3::LaneBoundary_Classification_Color::LaneBoundary_Classification_Color_COLOR_BLUE;
                                            break;
                                        case roadmanager::RoadMarkColor::GREEN:
                                            classific_col = osi3::LaneBoundary_Classification_Color::LaneBoundary_Classification_Color_COLOR_GREEN;
                                            break;
                                        case roadmanager::RoadMarkColor::RED:
                                            classific_col = osi3::LaneBoundary_Classification_Color::LaneBoundary_Classification_Color_COLOR_RED;
                                            break;
                                        case roadmanager::RoadMarkColor::WHITE:
                                            classific_col = osi3::LaneBoundary_Classification_Color::LaneBoundary_Classification_Color_COLOR_WHITE;
                                            break;
                                        case roadmanager::RoadMarkColor::YELLOW:
                                            classific_col = osi3::LaneBoundary_Classification_Color::LaneBoundary_Classification_Color_COLOR_YELLOW;
                                            break;
                                        default:
                                            classific_col = osi3::LaneBoundary_Classification_Color::LaneBoundary_Classification_Color_COLOR_WHITE;
                                    }
                                    osi_laneboundary->mutable_classification()->set_color(classific_col);

                                    // update limiting structure id only if the type of lane boundary is set to TYPE_STRUCTURE - for now it is not
                                    // implemented
                                    // osi_laneboundary->mutable_classification()->mutable_limiting_structure_id(0)->set_value(0);

                                    obj_osi_internal.lnb.push_back(osi_laneboundary);
                                }
                            }
                        }
                    }
                }
                else  // if there are no road marks I take the lane boundary
                {
                    roadmanager::LaneBoundaryOSI *laneboundary = lane->GetLaneBoundary();
                    // Check if this line is already pushed to OSI
                    idx_t               boundary_id      = laneboundary->GetGlobalId();
                    osi3::LaneBoundary *osi_laneboundary = 0;
                    for (unsigned int h = 0; h < obj_osi_internal.lnb.size(); h++)
                    {
                        if (obj_osi_internal.lnb[h]->mutable_id()->value() == boundary_id)
                        {
                            osi_laneboundary = obj_osi_internal.lnb[h];
                        }
                    }
                    if (!osi_laneboundary)
                    {
                        osi_laneboundary = obj_osi_internal.static_gt->add_lane_boundary();

                        // update id
                        osi_laneboundary->mutable_id()->set_value(boundary_id);

                        unsigned int n_osi_points = laneboundary->GetOSIPoints()->GetNumOfOSIPoints();
                        for (unsigned int h = 0; h < n_osi_points; h++)
                        {
                            osi3::LaneBoundary_BoundaryPoint *boundary_point = osi_laneboundary->add_boundary_line();
                            boundary_point->mutable_position()->set_x(laneboundary->GetOSIPoints()->GetXfromIdx(h));
                            boundary_point->mutable_position()->set_y(laneboundary->GetOSIPoints()->GetYfromIdx(h));
                            boundary_point->mutable_position()->set_z(laneboundary->GetOSIPoints()->GetZfromIdx(h));
                            // boundary_point->set_width(laneboundary->GetWidth());
                            // boundary_point->set_height(laneroadmark->GetHeight());
                        }

                        if (lane->IsRoadEdge())
                        {
                            osi_laneboundary->mutable_classification()->set_type(
                                osi3::LaneBoundary_Classification_Type::LaneBoundary_Classification_Type_TYPE_ROAD_EDGE);
                        }
                        else
                        {
                            osi_laneboundary->mutable_classification()->set_type(
                                osi3::LaneBoundary_Classification_Type::LaneBoundary_Classification_Type_TYPE_NO_LINE);
                        }

                        osi3::LaneBoundary_Classification_Color classific_col =
                            osi3::LaneBoundary_Classification_Color::LaneBoundary_Classification_Color_COLOR_UNKNOWN;
                        osi_laneboundary->mutable_classification()->set_color(classific_col);

                        obj_osi_internal.lnb.push_back(osi_laneboundary);
                    }
                }
            }
        }
    }

    // set any tunnel boundaries
    for (unsigned int i = 0; i < opendrive->GetNumOfRoads(); i++)
    {
        roadmanager::Road *road = opendrive->GetRoadByIdx(i);
        for (unsigned int j = 0; j < road->GetNumberOfTunnels(); j++)
        {
            roadmanager::Tunnel *tunnel = road->GetTunnel(j);

            // create 10 m points for tunnel
            for (unsigned int k = 0; k < 2; k++)
            {
                osi3::LaneBoundary *osi_laneboundary = obj_osi_internal.static_gt->add_lane_boundary();

                // set id and points
                osi_laneboundary->mutable_id()->set_value(tunnel->id_);
                for (unsigned int l = 0; l < tunnel->boundary_[k].GetOSIPoints()->GetPoints().size(); l++)
                {
                    roadmanager::PointStruct         &p              = tunnel->boundary_[k].GetOSIPoints()->GetPoints()[l];
                    osi3::LaneBoundary_BoundaryPoint *boundary_point = osi_laneboundary->add_boundary_line();
                    boundary_point->mutable_position()->set_x(p.x);
                    boundary_point->mutable_position()->set_y(p.y);
                    boundary_point->mutable_position()->set_z(p.z);
                }
                // set STRUCTURE type which covers tunnel
                osi_laneboundary->mutable_classification()->set_type(
                    osi3::LaneBoundary_Classification_Type::LaneBoundary_Classification_Type_TYPE_STRUCTURE);
                obj_osi_internal.lnb.push_back(osi_laneboundary);
            }
        }
    }

    return 0;
}

int OSIReporter::UpdateOSIRoadLane()
{
    // road network is static, needs to be processed only once
    if (obj_osi_internal.ln.size() > 0)
    {
        return 0;
    }

    // Retrieve opendrive class from RoadManager
    static roadmanager::OpenDrive *opendrive = roadmanager::Position::GetOpenDrive();

    // Loop over all roads
    for (unsigned int i = 0; i < opendrive->GetNumOfRoads(); i++)
    {
        roadmanager::Road *road = opendrive->GetRoadByIdx(i);

        // loop over all lane sections
        for (unsigned int j = 0; j < road->GetNumberOfLaneSections(); j++)
        {
            roadmanager::LaneSection *lane_section = road->GetLaneSectionByIdx(j);

            // loop over all lanes
            for (unsigned int k = 0; k < lane_section->GetNumberOfLanes(); k++)
            {
                roadmanager::Lane *lane = lane_section->GetLaneByIdx(k);
                if ((!lane->IsCenter() && !lane->IsOSIIntersection()))
                {
                    idx_t lane_global_id = lane->GetGlobalId();
                    int   lane_id        = lane->GetId();

                    // LANE ID
                    osi3::Lane *osi_lane = obj_osi_internal.static_gt->add_lane();
                    osi_lane->mutable_id()->set_value(lane_global_id);

                    // CLASSIFICATION TYPE
                    roadmanager::Lane::LaneType       lanetype      = lane->GetLaneType();
                    osi3::Lane_Classification_Type    class_type    = osi3::Lane_Classification_Type::Lane_Classification_Type_TYPE_UNKNOWN;
                    osi3::Lane_Classification_Subtype subclass_type = osi3::Lane_Classification_Subtype::Lane_Classification_Subtype_SUBTYPE_UNKNOWN;
                    if (lanetype == roadmanager::Lane::LaneType::LANE_TYPE_DRIVING)
                    {
                        class_type    = osi3::Lane_Classification_Type::Lane_Classification_Type_TYPE_DRIVING;
                        subclass_type = osi3::Lane_Classification_Subtype::Lane_Classification_Subtype_SUBTYPE_NORMAL;
                    }
                    else if (lanetype == roadmanager::Lane::LaneType::LANE_TYPE_PARKING)
                    {
                        class_type    = osi3::Lane_Classification_Type::Lane_Classification_Type_TYPE_NONDRIVING;
                        subclass_type = osi3::Lane_Classification_Subtype::Lane_Classification_Subtype_SUBTYPE_PARKING;
                    }
                    else if (lanetype == roadmanager::Lane::LaneType::LANE_TYPE_BIDIRECTIONAL)
                    {
                        class_type    = osi3::Lane_Classification_Type::Lane_Classification_Type_TYPE_DRIVING;
                        subclass_type = osi3::Lane_Classification_Subtype::Lane_Classification_Subtype_SUBTYPE_NORMAL;
                    }
                    else if (lanetype == roadmanager::Lane::LaneType::LANE_TYPE_STOP)
                    {
                        class_type    = osi3::Lane_Classification_Type::Lane_Classification_Type_TYPE_NONDRIVING;
                        subclass_type = osi3::Lane_Classification_Subtype::Lane_Classification_Subtype_SUBTYPE_STOP;
                    }
                    else if (lanetype == roadmanager::Lane::LaneType::LANE_TYPE_BIKING)
                    {
                        class_type    = osi3::Lane_Classification_Type::Lane_Classification_Type_TYPE_NONDRIVING;
                        subclass_type = osi3::Lane_Classification_Subtype::Lane_Classification_Subtype_SUBTYPE_BIKING;
                    }
                    else if (lanetype == roadmanager::Lane::LaneType::LANE_TYPE_SIDEWALK)
                    {
                        class_type    = osi3::Lane_Classification_Type::Lane_Classification_Type_TYPE_NONDRIVING;
                        subclass_type = osi3::Lane_Classification_Subtype::Lane_Classification_Subtype_SUBTYPE_SIDEWALK;
                    }
                    else if (lanetype == roadmanager::Lane::LaneType::LANE_TYPE_BORDER)
                    {
                        class_type    = osi3::Lane_Classification_Type::Lane_Classification_Type_TYPE_NONDRIVING;
                        subclass_type = osi3::Lane_Classification_Subtype::Lane_Classification_Subtype_SUBTYPE_BORDER;
                    }
                    else if (lanetype == roadmanager::Lane::LaneType::LANE_TYPE_RESTRICTED)
                    {
                        class_type    = osi3::Lane_Classification_Type::Lane_Classification_Type_TYPE_NONDRIVING;
                        subclass_type = osi3::Lane_Classification_Subtype::Lane_Classification_Subtype_SUBTYPE_RESTRICTED;
                    }
                    else if (lanetype == roadmanager::Lane::LaneType::LANE_TYPE_ROADWORKS)
                    {
                        class_type    = osi3::Lane_Classification_Type::Lane_Classification_Type_TYPE_NONDRIVING;
                        subclass_type = osi3::Lane_Classification_Subtype::Lane_Classification_Subtype_SUBTYPE_OTHER;
                    }
                    else if (lanetype == roadmanager::Lane::LaneType::LANE_TYPE_TRAM)
                    {
                        class_type    = osi3::Lane_Classification_Type::Lane_Classification_Type_TYPE_NONDRIVING;
                        subclass_type = osi3::Lane_Classification_Subtype::Lane_Classification_Subtype_SUBTYPE_OTHER;
                    }
                    else if (lanetype == roadmanager::Lane::LaneType::LANE_TYPE_RAIL)
                    {
                        class_type    = osi3::Lane_Classification_Type::Lane_Classification_Type_TYPE_NONDRIVING;
                        subclass_type = osi3::Lane_Classification_Subtype::Lane_Classification_Subtype_SUBTYPE_OTHER;
                    }
                    else if (lanetype == roadmanager::Lane::LaneType::LANE_TYPE_ENTRY)
                    {
                        class_type    = osi3::Lane_Classification_Type::Lane_Classification_Type_TYPE_DRIVING;
                        subclass_type = osi3::Lane_Classification_Subtype::Lane_Classification_Subtype_SUBTYPE_ENTRY;
                    }
                    else if (lanetype == roadmanager::Lane::LaneType::LANE_TYPE_EXIT)
                    {
                        class_type    = osi3::Lane_Classification_Type::Lane_Classification_Type_TYPE_DRIVING;
                        subclass_type = osi3::Lane_Classification_Subtype::Lane_Classification_Subtype_SUBTYPE_EXIT;
                    }
                    else if (lanetype == roadmanager::Lane::LaneType::LANE_TYPE_OFF_RAMP)
                    {
                        class_type    = osi3::Lane_Classification_Type::Lane_Classification_Type_TYPE_DRIVING;
                        subclass_type = osi3::Lane_Classification_Subtype::Lane_Classification_Subtype_SUBTYPE_OFFRAMP;
                    }
                    else if (lanetype == roadmanager::Lane::LaneType::LANE_TYPE_ON_RAMP)
                    {
                        class_type    = osi3::Lane_Classification_Type::Lane_Classification_Type_TYPE_DRIVING;
                        subclass_type = osi3::Lane_Classification_Subtype::Lane_Classification_Subtype_SUBTYPE_ONRAMP;
                    }
                    else if (lanetype == roadmanager::Lane::LaneType::LANE_TYPE_MEDIAN)
                    {
                        class_type    = osi3::Lane_Classification_Type::Lane_Classification_Type_TYPE_NONDRIVING;
                        subclass_type = osi3::Lane_Classification_Subtype::Lane_Classification_Subtype_SUBTYPE_OTHER;
                    }
                    else if (lanetype == roadmanager::Lane::LaneType::LANE_TYPE_SHOULDER)
                    {
                        class_type    = osi3::Lane_Classification_Type::Lane_Classification_Type_TYPE_NONDRIVING;
                        subclass_type = osi3::Lane_Classification_Subtype::Lane_Classification_Subtype_SUBTYPE_SHOULDER;
                    }
                    else if (lanetype == roadmanager::Lane::LaneType::LANE_TYPE_CURB)
                    {
                        class_type    = osi3::Lane_Classification_Type::Lane_Classification_Type_TYPE_NONDRIVING;
                        subclass_type = osi3::Lane_Classification_Subtype::Lane_Classification_Subtype_SUBTYPE_BORDER;
                    }
                    else if (lanetype == roadmanager::Lane::LaneType::LANE_TYPE_CONNECTING_RAMP)
                    {
                        class_type    = osi3::Lane_Classification_Type::Lane_Classification_Type_TYPE_DRIVING;
                        subclass_type = osi3::Lane_Classification_Subtype::Lane_Classification_Subtype_SUBTYPE_CONNECTINGRAMP;
                    }
                    else if (lanetype == roadmanager::Lane::LaneType::LANE_TYPE_SPECIAL1 ||
                             lanetype == roadmanager::Lane::LaneType::LANE_TYPE_SPECIAL2 ||
                             lanetype == roadmanager::Lane::LaneType::LANE_TYPE_SPECIAL3)
                    {
                        class_type    = osi3::Lane_Classification_Type::Lane_Classification_Type_TYPE_OTHER;
                        subclass_type = osi3::Lane_Classification_Subtype::Lane_Classification_Subtype_SUBTYPE_OTHER;
                    }
                    else if (lanetype == roadmanager::Lane::LaneType::LANE_TYPE_NONE)
                    {
                        class_type    = osi3::Lane_Classification_Type::Lane_Classification_Type_TYPE_UNKNOWN;
                        subclass_type = osi3::Lane_Classification_Subtype::Lane_Classification_Subtype_SUBTYPE_UNKNOWN;
                    }
                    osi_lane->mutable_classification()->set_type(class_type);
                    osi_lane->mutable_classification()->set_subtype(subclass_type);

                    // CENTERLINE POINTS
                    unsigned int n_osi_points = lane->GetOSIPoints()->GetNumOfOSIPoints();
                    for (unsigned int jj = 0; jj < n_osi_points; jj++)
                    {
                        osi3::Vector3d *centerLine = osi_lane->mutable_classification()->add_centerline();
                        centerLine->set_x(lane->GetOSIPoints()->GetXfromIdx(jj));
                        centerLine->set_y(lane->GetOSIPoints()->GetYfromIdx(jj));
                        centerLine->set_z(lane->GetOSIPoints()->GetZfromIdx(jj));
                    }

                    // DRIVING DIRECTION
                    bool driving_direction = true;
                    if ((lane_id >= 0 && road->GetRule() == roadmanager::Road::RoadRule::RIGHT_HAND_TRAFFIC) ||
                        (lane_id < 0 && road->GetRule() == roadmanager::Road::RoadRule::LEFT_HAND_TRAFFIC))
                    {
                        driving_direction = false;
                    }
                    osi_lane->mutable_classification()->set_centerline_is_driving_direction(driving_direction);

                    // LEFT AND RIGHT LANE IDS
                    std::vector<std::pair<int, int>> globalid_ids_left;
                    std::vector<std::pair<int, int>> globalid_ids_right;

                    if (lane_section->IsOSILaneById(lane_id + (1)))
                    {
                        globalid_ids_left.push_back(std::make_pair(lane_id - (1), lane_section->GetLaneGlobalIdById(lane_id + (1))));
                    }
                    else if (lane_section->IsOSILaneById(lane_id + (2)))
                    {
                        globalid_ids_left.push_back(std::make_pair(lane_id - (2), lane_section->GetLaneGlobalIdById(lane_id + (2))));
                    }

                    if (lane_section->IsOSILaneById(lane_id - (1)))
                    {
                        globalid_ids_right.push_back(std::make_pair(lane_id - (1), lane_section->GetLaneGlobalIdById(lane_id - (1))));
                    }
                    else if (lane_section->IsOSILaneById(lane_id - (2)))
                    {
                        globalid_ids_right.push_back(std::make_pair(lane_id - (2), lane_section->GetLaneGlobalIdById(lane_id - (2))));
                    }

                    // order global id with local id to maintain geographical order
                    std::sort(globalid_ids_left.begin(), globalid_ids_left.end());
                    std::sort(globalid_ids_right.begin(), globalid_ids_right.end());

                    for (unsigned int jj = 0; jj < globalid_ids_left.size(); jj++)
                    {
                        osi3::Identifier *left_id = osi_lane->mutable_classification()->add_left_adjacent_lane_id();
                        left_id->set_value(static_cast<uint64_t>(globalid_ids_left[jj].second));
                    }
                    for (unsigned int jj = 0; jj < globalid_ids_right.size(); jj++)
                    {
                        osi3::Identifier *right_id = osi_lane->mutable_classification()->add_right_adjacent_lane_id();
                        right_id->set_value(static_cast<uint64_t>(globalid_ids_right[jj].second));
                    }

                    // LANE BOUNDARY IDS
                    if (lane_id == 0)  // for central lane I use the laneboundary osi points as right and left boundary so that it can be used
                                       // from both sides
                    {
                        // check if lane has road mark
                        std::vector<id_t> line_ids = lane->GetLineGlobalIds();
                        if (!line_ids.empty())  // lane has RoadMarks
                        {
                            for (unsigned int jj = 0; jj < line_ids.size(); jj++)
                            {
                                osi3::Identifier *left_lane_bound_id = osi_lane->mutable_classification()->add_left_lane_boundary_id();
                                left_lane_bound_id->set_value(line_ids[jj]);
                                osi3::Identifier *right_lane_bound_id = osi_lane->mutable_classification()->add_right_lane_boundary_id();
                                right_lane_bound_id->set_value(line_ids[jj]);
                            }
                        }
                        else  // no road marks -> we take lane boundary
                        {
                            id_t laneboundary_global_id = lane->GetLaneBoundaryGlobalId();
                            if (laneboundary_global_id != ID_UNDEFINED)
                            {
                                osi3::Identifier *left_lane_bound_id = osi_lane->mutable_classification()->add_left_lane_boundary_id();
                                left_lane_bound_id->set_value(laneboundary_global_id);
                                osi3::Identifier *right_lane_bound_id = osi_lane->mutable_classification()->add_right_lane_boundary_id();
                                right_lane_bound_id->set_value(laneboundary_global_id);
                            }
                        }
                    }
                    else
                    {
                        // Set left/right laneboundary ID for left/right lanes- we use LaneMarks is they exist, if not we take laneboundary
                        std::vector<id_t> line_ids = lane->GetLineGlobalIds();
                        if (!line_ids.empty())  // lane has RoadMarks
                        {
                            for (unsigned int jj = 0; jj < line_ids.size(); jj++)
                            {
                                if (lane_id < 0)
                                {
                                    osi3::Identifier *left_lane_bound_id = osi_lane->mutable_classification()->add_right_lane_boundary_id();
                                    left_lane_bound_id->set_value(line_ids[jj]);
                                }
                                else
                                {
                                    osi3::Identifier *left_lane_bound_id = osi_lane->mutable_classification()->add_left_lane_boundary_id();
                                    left_lane_bound_id->set_value(line_ids[jj]);
                                }
                            }
                        }
                        else
                        {
                            id_t laneboundary_global_id = lane->GetLaneBoundaryGlobalId();
                            if (lane_id < 0 && laneboundary_global_id != ID_UNDEFINED)
                            {
                                osi3::Identifier *left_lane_bound_id = osi_lane->mutable_classification()->add_right_lane_boundary_id();
                                left_lane_bound_id->set_value(laneboundary_global_id);
                            }
                            else if (lane_id > 0 && laneboundary_global_id != ID_UNDEFINED)
                            {
                                osi3::Identifier *left_lane_bound_id = osi_lane->mutable_classification()->add_left_lane_boundary_id();
                                left_lane_bound_id->set_value(laneboundary_global_id);
                            }
                        }

                        // Set right/left laneboundary ID for left/right lanes - we look at neightbour lanes
                        int next_lane_id = 0;
                        if (lane_id < 0)  // if lane is on the right, then it contains its right boundary. So I need to look into its left lane
                                          // for the left boundary
                        {
                            next_lane_id = lane_id + 1;
                        }
                        else  // if lane is on the left, then it contains its left boundary. So I need to look into its right
                              // lane for the right boundary
                        {
                            next_lane_id = lane_id - 1;
                        }
                        // look at right lane and check if it has Lines for RoadMarks
                        roadmanager::Lane *next_lane = lane_section->GetLaneById(next_lane_id);
                        if (next_lane != nullptr)
                        {
                            std::vector<id_t> nextlane_line_ids = next_lane->GetLineGlobalIds();
                            if (!nextlane_line_ids.empty())
                            {
                                for (unsigned int jj = 0; jj < nextlane_line_ids.size(); jj++)
                                {
                                    if (lane_id < 0)
                                    {
                                        osi3::Identifier *right_lane_bound_id = osi_lane->mutable_classification()->add_left_lane_boundary_id();
                                        right_lane_bound_id->set_value(nextlane_line_ids[jj]);
                                    }
                                    else
                                    {
                                        osi3::Identifier *right_lane_bound_id = osi_lane->mutable_classification()->add_right_lane_boundary_id();
                                        right_lane_bound_id->set_value(nextlane_line_ids[jj]);
                                    }
                                }
                            }
                            else  // if the neightbour lane does not have Lines for RoadMakrs we take the LaneBoundary
                            {
                                id_t next_laneboundary_global_id = next_lane->GetLaneBoundaryGlobalId();
                                if (lane_id < 0 && next_laneboundary_global_id != ID_UNDEFINED)
                                {
                                    osi3::Identifier *right_lane_bound_id = osi_lane->mutable_classification()->add_left_lane_boundary_id();
                                    right_lane_bound_id->set_value(next_laneboundary_global_id);
                                }
                                else if (lane_id > 0 && next_laneboundary_global_id != ID_UNDEFINED)
                                {
                                    osi3::Identifier *right_lane_bound_id = osi_lane->mutable_classification()->add_right_lane_boundary_id();
                                    right_lane_bound_id->set_value(next_laneboundary_global_id);
                                }
                            }
                        }
                    }

                    // SOURCE REFERENCE
                    auto source_reference = osi_lane->add_source_reference();
                    source_reference->set_type(kSourceRefTypeOdr);
                    std::string t_road_id = fmt::format("road_id:{}", road->GetId());
                    std::string t_road_s  = fmt::format("road_s:{}", lane_section->GetS());
                    std::string t_lane_id = fmt::format("lane_id:{}", lane->GetId());

                    source_reference->add_identifier(t_road_id);
                    source_reference->add_identifier(t_road_s);
                    source_reference->add_identifier(t_lane_id);

                    // STILL TO DO:
                    double temp = 0;
                    osi_lane->mutable_classification()->mutable_road_condition()->set_surface_temperature(temp);
                    osi_lane->mutable_classification()->mutable_road_condition()->set_surface_water_film(temp);
                    osi_lane->mutable_classification()->mutable_road_condition()->set_surface_freezing_point(temp);
                    osi_lane->mutable_classification()->mutable_road_condition()->set_surface_ice(temp);
                    osi_lane->mutable_classification()->mutable_road_condition()->set_surface_roughness(temp);
                    osi_lane->mutable_classification()->mutable_road_condition()->set_surface_texture(temp);

                    obj_osi_internal.ln.push_back(osi_lane);
                }
            }
        }
    }

    // sort lanes by global id, for faster lookup
    std::sort(obj_osi_internal.ln.begin(), obj_osi_internal.ln.end(), [](osi3::Lane *a, osi3::Lane *b) { return a->id().value() < b->id().value(); });

    // now when all lanes has been collected, resolve lane connectivity
    for (unsigned int i = 0; i < opendrive->GetNumOfRoads(); i++)
    {
        roadmanager::Road *road = opendrive->GetRoadByIdx(i);

        // Get predecessor and successor roads if exists
        roadmanager::RoadLink *roadLink = nullptr;

        roadmanager::Road *predecessorRoad = nullptr;
        roadmanager::Road *successorRoad   = nullptr;

        roadmanager::Junction *predecessorJunction = nullptr;
        roadmanager::Junction *successorJunction   = nullptr;

        roadLink = road->GetLink(roadmanager::LinkType::PREDECESSOR);
        if (roadLink)
        {
            if (roadLink->GetElementType() == roadmanager::RoadLink::ElementType::ELEMENT_TYPE_ROAD)
            {
                predecessorRoad = opendrive->GetRoadById(roadLink->GetElementId());
            }
            else if (roadLink->GetElementType() == roadmanager::RoadLink::ElementType::ELEMENT_TYPE_JUNCTION)
            {
                predecessorJunction = opendrive->GetJunctionById(roadLink->GetElementId());
            }
        }

        roadLink = road->GetLink(roadmanager::LinkType::SUCCESSOR);
        if (roadLink)
        {
            if (roadLink->GetElementType() == roadmanager::RoadLink::ElementType::ELEMENT_TYPE_ROAD)
            {
                successorRoad = opendrive->GetRoadById(roadLink->GetElementId());
            }
            if (roadLink->GetElementType() == roadmanager::RoadLink::ElementType::ELEMENT_TYPE_JUNCTION)
            {
                successorJunction = opendrive->GetJunctionById(roadLink->GetElementId());
            }
        }

        // loop over all lane sections
        for (unsigned int j = 0; j < road->GetNumberOfLaneSections(); j++)
        {
            roadmanager::LaneSection *lane_section                   = road->GetLaneSectionByIdx(j);
            id_t                      global_predecessor_junction_id = ID_UNDEFINED;
            id_t                      global_successor_junction_id   = ID_UNDEFINED;

            // Get predecessor and successor lane_sections
            roadmanager::LaneSection *predecessor_lane_section = nullptr;
            roadmanager::LaneSection *successor_lane_section   = nullptr;

            // if there are more than 1 section we use the previous lane section in the same road
            if (j > 0)
            {
                predecessor_lane_section = road->GetLaneSectionByIdx(j - 1);
            }
            else
            {
                // Otherwise we use the last lane section of the predecessor road
                if (predecessorRoad)
                {
                    // get first or last lane section depending on road direction
                    if (predecessorRoad->GetLink(roadmanager::LinkType::PREDECESSOR))
                    {
                        if (predecessorRoad->GetLink(roadmanager::LinkType::PREDECESSOR)->GetElementId() == road->GetId())
                        {
                            predecessor_lane_section = predecessorRoad->GetLaneSectionByIdx(0);
                        }
                        else if (predecessorRoad->GetLink(roadmanager::LinkType::PREDECESSOR)->GetElementId() == road->GetJunction())
                        {
                            predecessor_lane_section = predecessorRoad->GetLaneSectionByIdx(0);
                        }
                    }
                    if (predecessorRoad->GetLink(roadmanager::LinkType::SUCCESSOR))
                    {
                        if (predecessorRoad->GetLink(roadmanager::LinkType::SUCCESSOR)->GetElementId() == road->GetId())
                        {
                            predecessor_lane_section = predecessorRoad->GetLaneSectionByIdx(predecessorRoad->GetNumberOfLaneSections() - 1);
                        }
                        else if (predecessorRoad->GetLink(roadmanager::LinkType::SUCCESSOR)->GetElementId() == road->GetJunction())
                        {
                            predecessor_lane_section = predecessorRoad->GetLaneSectionByIdx(predecessorRoad->GetNumberOfLaneSections() - 1);
                        }
                    }
                }
                else if (predecessorJunction && predecessorJunction->IsOsiIntersection())
                {
                    global_predecessor_junction_id = predecessorJunction->GetGlobalId();
                }
            }

            // If this is not the last lane section, pick next lane section as successor
            if (j + 1 < road->GetNumberOfLaneSections())
            {
                successor_lane_section = road->GetLaneSectionByIdx(j + 1);
            }
            else
            {
                // Otherwise (is the last lane section) we use the first lane section of the successor road if exists
                if (successorRoad)
                {
                    // get first or last lane section depending on road direction
                    if (successorRoad->GetLink(roadmanager::LinkType::PREDECESSOR))
                    {
                        if (successorRoad->GetLink(roadmanager::LinkType::PREDECESSOR)->GetElementId() == road->GetId())
                        {
                            successor_lane_section = successorRoad->GetLaneSectionByIdx(0);
                        }
                        else if (successorRoad->GetLink(roadmanager::LinkType::PREDECESSOR)->GetElementId() == road->GetJunction())
                        {
                            successor_lane_section = successorRoad->GetLaneSectionByIdx(0);
                        }
                    }
                    if (successorRoad->GetLink(roadmanager::LinkType::SUCCESSOR))
                    {
                        if (successorRoad->GetLink(roadmanager::LinkType::SUCCESSOR)->GetElementId() == road->GetId())
                        {
                            successor_lane_section = successorRoad->GetLaneSectionByIdx(successorRoad->GetNumberOfLaneSections() - 1);
                        }
                        else if (successorRoad->GetLink(roadmanager::LinkType::SUCCESSOR)->GetElementId() == road->GetJunction())
                        {
                            successor_lane_section = successorRoad->GetLaneSectionByIdx(successorRoad->GetNumberOfLaneSections() - 1);
                        }
                    }
                }
                else if (successorJunction && successorJunction->IsOsiIntersection())
                {
                    global_successor_junction_id = successorJunction->GetGlobalId();
                }
            }

            // loop over all lanes
            for (unsigned int k = 0; k < lane_section->GetNumberOfLanes(); k++)
            {
                roadmanager::Lane *lane = lane_section->GetLaneByIdx(k);
                if ((!lane->IsCenter() && !lane->IsOSIIntersection()))
                {
                    osi3::Lane *osi_lane       = GetOSILaneFromGlobalId(lane->GetGlobalId());
                    idx_t       lane_global_id = lane->GetGlobalId();

                    if (osi_lane == nullptr)
                    {
                        LOG_ERROR("OSI Lane with global id {} / id {} not found", lane_global_id, lane->GetId());
                        continue;
                    }

                    // Get the predecessor and successor lanes
                    roadmanager::Lane *predecessorLane = nullptr;
                    roadmanager::Lane *successorLane   = nullptr;

                    osi3::Lane_Classification_LanePairing *lane_pairing = nullptr;
                    if (predecessor_lane_section && lane->GetLink(roadmanager::LinkType::PREDECESSOR))
                    {
                        predecessorLane = predecessor_lane_section->GetLaneById(lane->GetLink(roadmanager::LinkType::PREDECESSOR)->GetId());
                        if (predecessorLane)
                        {
                            lane_pairing = osi_lane->mutable_classification()->add_lane_pairing();
                            lane_pairing->mutable_antecessor_lane_id()->set_value(predecessorLane->GetGlobalId());
                        }
                    }

                    if (successor_lane_section && lane->GetLink(roadmanager::LinkType::SUCCESSOR))
                    {
                        successorLane = successor_lane_section->GetLaneById(lane->GetLink(roadmanager::LinkType::SUCCESSOR)->GetId());
                        if (successorLane)
                        {
                            if (!lane_pairing)
                            {
                                lane_pairing = osi_lane->mutable_classification()->add_lane_pairing();
                            }
                            lane_pairing->mutable_successor_lane_id()->set_value(successorLane->GetGlobalId());
                        }
                    }

                    if (global_predecessor_junction_id != ID_UNDEFINED)
                    {
                        if (!lane_pairing)
                        {
                            lane_pairing = osi_lane->mutable_classification()->add_lane_pairing();
                        }
                        lane_pairing->mutable_antecessor_lane_id()->set_value(global_predecessor_junction_id);
                    }

                    if (global_successor_junction_id != ID_UNDEFINED)
                    {
                        if (!lane_pairing)
                        {
                            lane_pairing = osi_lane->mutable_classification()->add_lane_pairing();
                        }
                        lane_pairing->mutable_successor_lane_id()->set_value(global_successor_junction_id);
                    }
                    roadmanager::Junction *junction = opendrive->GetJunctionById(road->GetJunction());

                    // Update lanes that connect with junctions that are not intersections
                    if (junction && !junction->IsOsiIntersection())
                    {
                        roadmanager::LaneLink *link_predecessor = lane->GetLink(roadmanager::LinkType::PREDECESSOR);
                        roadmanager::LaneLink *link_successor   = lane->GetLink(roadmanager::LinkType::SUCCESSOR);

                        roadmanager::Lane *driving_lane_predecessor = 0;
                        roadmanager::Lane *driving_lane_successor   = 0;

                        if (link_predecessor && predecessor_lane_section)
                        {
                            driving_lane_predecessor =
                                predecessorRoad->GetDrivingLaneById(predecessor_lane_section->GetS(), link_predecessor->GetId());
                            if (!driving_lane_predecessor)
                            {
                                LOG_WARN("Lane {} on predecessor road {} s {:.2f} is not a driving lane",
                                         lane->GetId(),
                                         predecessorRoad->GetId(),
                                         predecessor_lane_section->GetS());
                            }
                        }

                        if (link_successor && successor_lane_section)
                        {
                            driving_lane_successor = successorRoad->GetDrivingLaneById(successor_lane_section->GetS(), link_successor->GetId());
                            if (!driving_lane_successor)
                            {
                                LOG_WARN("Lane {} on successor road {} s {:.2f} is not a driving lane",
                                         lane->GetId(),
                                         successorRoad->GetId(),
                                         successor_lane_section->GetS());
                            }
                        }

                        for (int l = 0; l < obj_osi_internal.static_gt->lane_size(); ++l)
                        {
                            lane_pairing = nullptr;

                            if (predecessorRoad && predecessor_lane_section && link_predecessor && driving_lane_predecessor &&
                                driving_lane_predecessor->GetGlobalId() == obj_osi_internal.static_gt->lane(l).id().value())
                            {
                                // find first empty pairing slot for successor lane
                                for (int m = 0; m < obj_osi_internal.static_gt->lane(l).classification().lane_pairing_size(); ++m)
                                {
                                    if (!obj_osi_internal.static_gt->lane(l).classification().lane_pairing(m).has_successor_lane_id())
                                    {
                                        lane_pairing = obj_osi_internal.static_gt->mutable_lane(l)->mutable_classification()->mutable_lane_pairing(m);
                                        break;
                                    }
                                }

                                if (lane_pairing == nullptr)
                                {
                                    // create a new lane pairing entry
                                    lane_pairing = obj_osi_internal.static_gt->mutable_lane(l)->mutable_classification()->add_lane_pairing();
                                }

                                if ((road->GetLink(roadmanager::LinkType::PREDECESSOR) != 0))
                                {
                                    lane_pairing->mutable_successor_lane_id()->set_value(lane_global_id);
                                }
                            }

                            if (successorRoad && successor_lane_section && link_successor && driving_lane_successor &&
                                driving_lane_successor->GetGlobalId() == obj_osi_internal.static_gt->lane(l).id().value())
                            {
                                // find first empty pairing slot for successor lane
                                for (int m = 0; m < obj_osi_internal.static_gt->lane(l).classification().lane_pairing_size(); ++m)
                                {
                                    if (!obj_osi_internal.static_gt->lane(l).classification().lane_pairing(m).has_antecessor_lane_id())
                                    {
                                        lane_pairing = obj_osi_internal.static_gt->mutable_lane(l)->mutable_classification()->mutable_lane_pairing(m);
                                        break;
                                    }
                                }

                                if (lane_pairing == nullptr)
                                {
                                    // create a new lane pairing entry
                                    lane_pairing = obj_osi_internal.static_gt->mutable_lane(l)->mutable_classification()->add_lane_pairing();
                                }

                                if ((road->GetLink(roadmanager::LinkType::SUCCESSOR) != 0))
                                {
                                    lane_pairing->mutable_antecessor_lane_id()->set_value(lane_global_id);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return 0;
}

