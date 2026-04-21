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

const char *OSIReporter::GetOSIGroundTruth(int *size)
{
    if (!(GetUDPClientStatus() == 0 || IsFileOpen()))
    {
        obj_osi_external.gt->SerializeToString(&osiGroundTruth.ground_truth);
        osiGroundTruth.size = static_cast<unsigned int>(obj_osi_external.gt->ByteSizeLong());
    }
    *size = static_cast<int>(osiGroundTruth.size);
    return osiGroundTruth.ground_truth.data();
}

const char *OSIReporter::GetOSIGroundTruthRaw()
{
    return reinterpret_cast<char *>(obj_osi_external.gt);
}

const char *OSIReporter::GetOSITrafficCommandRaw()
{
    return reinterpret_cast<char *>(obj_osi_external.tc);
}

const char *OSIReporter::GetOSIRoadLane(const std::vector<scenarioengine::Object*> &objectState, int *size, int object_id)
{
    if (static_cast<unsigned int>(object_id) >= objectState.size())
    {
        LOG_ERROR("Object {} not available, only {} registered", object_id, objectState.size());
        *size = 0;
        return 0;
    }

    roadmanager::Position pos;
    for (size_t i = 0; i < objectState.size(); i++)
    {
        if (object_id == objectState[i]->id_)
        {
            pos = objectState[i]->pos_;
            break;
        }
    }

    id_t  lane_id_of_vehicle = pos.GetLaneGlobalId();
    idx_t idx                = IDX_UNDEFINED;
    for (unsigned int i = 0; i < obj_osi_internal.ln.size(); i++)
    {
        osi3::Identifier identifier = obj_osi_internal.ln[i]->id();
        id_t             found_id   = static_cast<unsigned int>(identifier.value());
        if (found_id == lane_id_of_vehicle)
        {
            idx = i;
            break;
        }
    }
    if (idx == IDX_UNDEFINED)
    {
        LOG_ERROR("Failed to locate vehicle lane id!");
        return 0;
    }

    obj_osi_internal.ln[idx]->SerializeToString(&osiRoadLane.osi_lane_info);
    osiRoadLane.size = static_cast<unsigned int>(obj_osi_internal.ln[idx]->ByteSizeLong());
    *size            = static_cast<int>(osiRoadLane.size);
    return osiRoadLane.osi_lane_info.data();
}

const char *OSIReporter::GetOSIRoadLaneBoundary(int *size, int g_id)
{
    int idx = -1;
    for (unsigned int i = 0; i < obj_osi_internal.lnb.size(); i++)
    {
        osi3::Identifier identifier = obj_osi_internal.lnb[i]->id();
        int              found_id   = static_cast<int>(identifier.value());
        if (found_id == g_id)
        {
            idx = static_cast<int>(i);
            break;
        }
    }

    if (idx == -1)
    {
        return 0;
    }

    obj_osi_internal.lnb[static_cast<unsigned int>(idx)]->SerializeToString(&osiRoadLaneBoundary.osi_lane_boundary_info);
    osiRoadLaneBoundary.size = static_cast<unsigned int>(obj_osi_internal.lnb[static_cast<unsigned int>(idx)]->ByteSizeLong());
    *size                    = static_cast<int>(osiRoadLaneBoundary.size);
    return osiRoadLaneBoundary.osi_lane_boundary_info.data();
}

bool OSIReporter::IsCentralOSILane(int lane_idx)
{
    osi3::Identifier Left_lb_id = obj_osi_internal.ln[static_cast<unsigned int>(lane_idx)]->mutable_classification()->left_lane_boundary_id(0);
    int              left_lb_id = static_cast<int>(Left_lb_id.value());

    osi3::Identifier Right_lb_id = obj_osi_internal.ln[static_cast<unsigned int>(lane_idx)]->mutable_classification()->right_lane_boundary_id(0);
    int              right_lb_id = static_cast<int>(Right_lb_id.value());

    return left_lb_id == right_lb_id;
}

idx_t OSIReporter::GetLaneIdxfromIdOSI(id_t lane_id)
{
    id_t idx = ID_UNDEFINED;
    for (unsigned int i = 0; i < obj_osi_internal.ln.size(); i++)
    {
        osi3::Identifier identifier = obj_osi_internal.ln[i]->id();
        id_t             found_id   = static_cast<unsigned int>(identifier.value());
        if (found_id == lane_id)
        {
            idx = i;
            break;
        }
    }
    return idx;
}

osi3::Lane *OSIReporter::GetOSILaneFromGlobalId(id_t g_id)
{
    auto it = std::lower_bound(obj_osi_internal.ln.begin(),
                               obj_osi_internal.ln.end(),
                               g_id,
                               [](osi3::Lane *lane, id_t gid) { return lane->id().value() < gid; });

    if (it != obj_osi_internal.ln.end() && (*it)->id().value() == g_id)
    {
        return *it;
    }

    return nullptr;
}

void OSIReporter::GetOSILaneBoundaryIds(const std::vector<scenarioengine::Object*> &objectState, std::vector<id_t> &ids, int object_id)
{
    idx_t             idx_central, idx_left, idx_right;
    id_t              left_lb_id, right_lb_id;
    id_t              far_left_lb_id, far_right_lb_id;
    std::vector<id_t> final_lb_ids;

    if (static_cast<unsigned int>(object_id) >= objectState.size())
    {
        LOG_ERROR("Object {} not available, only {} registered", object_id, objectState.size());
        ids = {ID_UNDEFINED, ID_UNDEFINED, ID_UNDEFINED, ID_UNDEFINED};
        return;
    }

    roadmanager::Position pos;
    for (size_t i = 0; i < objectState.size(); i++)
    {
        if (object_id == objectState[i]->id_)
        {
            pos = objectState[i]->pos_;
        }
    }

    id_t lane_id_of_vehicle = pos.GetLaneGlobalId();
    idx_central             = GetLaneIdxfromIdOSI(lane_id_of_vehicle);

    if (obj_osi_internal.ln[idx_central]->mutable_classification()->left_lane_boundary_id_size() == 0)
    {
        left_lb_id = ID_UNDEFINED;
    }
    else
    {
        osi3::Identifier left_lane = obj_osi_internal.ln[idx_central]->mutable_classification()->left_lane_boundary_id(0);
        left_lb_id                 = static_cast<unsigned int>(left_lane.value());
    }

    if (obj_osi_internal.ln[idx_central]->mutable_classification()->right_lane_boundary_id_size() == 0)
    {
        right_lb_id = ID_UNDEFINED;
    }
    else
    {
        osi3::Identifier right_lane = obj_osi_internal.ln[idx_central]->mutable_classification()->right_lane_boundary_id(0);
        right_lb_id                 = static_cast<unsigned int>(right_lane.value());
    }

    if (obj_osi_internal.ln[idx_central]->mutable_classification()->left_adjacent_lane_id_size() == 0)
    {
        far_left_lb_id = ID_UNDEFINED;
    }
    else
    {
        osi3::Identifier Left_lane_id = obj_osi_internal.ln[idx_central]->mutable_classification()->left_adjacent_lane_id(0);
        id_t             left_lane_id = static_cast<unsigned int>(Left_lane_id.value());
        idx_left                      = GetLaneIdxfromIdOSI(left_lane_id);

        if (obj_osi_internal.ln[idx_left]->mutable_classification()->left_lane_boundary_id_size() == 0)
        {
            far_left_lb_id = ID_UNDEFINED;
        }
        else
        {
            osi3::Identifier Far_left_lb_id = obj_osi_internal.ln[idx_left]->mutable_classification()->left_lane_boundary_id(0);
            far_left_lb_id                  = static_cast<unsigned int>(Far_left_lb_id.value());
        }
    }

    if (obj_osi_internal.ln[idx_central]->mutable_classification()->right_adjacent_lane_id_size() == 0)
    {
        far_right_lb_id = ID_UNDEFINED;
    }
    else
    {
        osi3::Identifier Right_lane_id = obj_osi_internal.ln[idx_central]->mutable_classification()->right_adjacent_lane_id(0);
        id_t             right_lane_id = static_cast<unsigned int>(Right_lane_id.value());
        idx_right                      = GetLaneIdxfromIdOSI(right_lane_id);

        if (obj_osi_internal.ln[idx_right]->mutable_classification()->right_lane_boundary_id_size() == 0)
        {
            far_right_lb_id = ID_UNDEFINED;
        }
        else
        {
            osi3::Identifier Far_right_lb_id = obj_osi_internal.ln[idx_right]->mutable_classification()->right_lane_boundary_id(0);
            far_right_lb_id                  = static_cast<unsigned int>(Far_right_lb_id.value());
        }
    }

    final_lb_ids.push_back(far_left_lb_id);
    final_lb_ids.push_back(left_lb_id);
    final_lb_ids.push_back(right_lb_id);
    final_lb_ids.push_back(far_right_lb_id);

    ids = final_lb_ids;
}
