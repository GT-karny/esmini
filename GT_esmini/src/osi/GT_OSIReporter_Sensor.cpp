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

#include "OSIReporter.hpp"
#include "GT_OSIReporter_Internals.hpp"

int OSIReporter::CreateSensorViewFromSensorData(const osi3::SensorData &sd)
{
    obj_osi_external.sv->Clear();
    for (int i = 0; i < sd.moving_object_size(); i++)
    {
        CreateMovingObjectFromSensorData(sd, i);
    }

    for (int i = 0; i < sd.lane_boundary_size(); i++)
    {
        CreateLaneBoundaryFromSensordata(sd, i);
    }
    return 0;
}

void OSIReporter::CreateMovingObjectFromSensorData(const osi3::SensorData &sd, int obj_nr)
{
    osi3::DetectedMovingObject object = sd.moving_object(obj_nr);
    double                     x      = object.base().position().x() + sd.mounting_position().position().x();
    double                     y      = object.base().position().y() + sd.mounting_position().position().y();
    double                     z      = object.base().position().z();
    double                     yaw    = object.base().orientation().yaw();

    yaw = sd.mounting_position().orientation().yaw() + yaw;
    yaw = sd.host_vehicle_location().orientation().yaw() + yaw;

    osi3::MovingObject *obj = obj_osi_external.sv->mutable_global_ground_truth()->add_moving_object();

    obj->mutable_id()->set_value(object.header().tracking_id().value());
    obj->mutable_base()->mutable_position()->set_x(x);
    obj->mutable_base()->mutable_position()->set_y(y);
    obj->mutable_base()->mutable_position()->set_z(z);
    obj->mutable_base()->mutable_orientation()->set_yaw(yaw);

    obj->mutable_base()->mutable_dimension()->set_height(object.base().dimension().height());
    obj->mutable_base()->mutable_dimension()->set_length(object.base().dimension().length());
    obj->mutable_base()->mutable_dimension()->set_width(object.base().dimension().width());
}

void OSIReporter::CreateLaneBoundaryFromSensordata(const osi3::SensorData &sd, int lane_boundary_nr)
{
    osi3::DetectedLaneBoundary lane_boundary = sd.lane_boundary(lane_boundary_nr);

    if (lane_boundary.header().ground_truth_id_size() == 0)
    {
        // Without a ground-truth id the boundary cannot be linked back to the map; header().ground_truth_id().at(0)
        // below would throw std::out_of_range on the empty repeated field, so skip this boundary.
        LOG_WARN("CreateLaneBoundaryFromSensordata: lane boundary {} has no ground truth id, skipping", lane_boundary_nr);
        return;
    }

    osi3::LaneBoundary *new_lane_boundary = obj_osi_external.sv->mutable_global_ground_truth()->add_lane_boundary();

    for (int i = 0; i < sd.lane_boundary(lane_boundary_nr).boundary_line_size(); i++)
    {
        double x = lane_boundary.boundary_line(i).position().x() + sd.mounting_position().position().x();
        double y = lane_boundary.boundary_line(i).position().y() + sd.mounting_position().position().y();
        double z = lane_boundary.boundary_line(i).position().z();

        new_lane_boundary->mutable_id()->set_value(lane_boundary.header().ground_truth_id().at(0).value());
        osi3::LaneBoundary_BoundaryPoint *boundary_point = new_lane_boundary->add_boundary_line();

        boundary_point->mutable_position()->set_x(x);
        boundary_point->mutable_position()->set_y(y);
        boundary_point->mutable_position()->set_z(z);
    }
}
