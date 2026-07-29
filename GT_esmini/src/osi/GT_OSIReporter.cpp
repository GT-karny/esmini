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
#include "OSITrafficCommand.hpp"
#include "OSCPrivateAction.hpp"
#include "RoadManager.hpp"
#include "gt_esmini/road/OdrSideModel.hpp"  // WP4: authored junction boundary -> OSI intersection contour
// #include "gt_esmini/scenario/ExtraEntities.hpp"
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>
#include <utility>
#include <array>
#include <map>

#ifdef _WIN32
#include <winsock2.h>
#include <Ws2tcpip.h>
#else
/* Assume that any non-Windows platform uses POSIX-style sockets instead. */
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>  /* Needed for getaddrinfo() and freeaddrinfo() */
#include <unistd.h> /* Needed for close() */
#endif

#define OSI_OUT_PORT          48198
#define OSI_MAX_UDP_DATA_SIZE 8192

constexpr const char *SOURCE_REF_TYPE_ODR = "net.asam.opendrive";
constexpr const char *SOURCE_REF_TYPE_OSC = "net.asam.openscenario";

// [GT_MOD] Env gate for the GT-only "odr_type:<...>" source_reference identifier
// (signal:crosswalk_footprint). Default ON; GT_OSI_ODR_OBJECT_TYPE=0/false/off/no disables.
// Same idiom and rationale as GT_OSI_FUTURE_TRAJECTORY (GT_OSIReporter_Moving.cpp): upstream
// unit suites assert the exact upstream source_reference layout (ScenarioPlayer_test
// OSI.TestStationaryObjects ASSERTs identifier().size() == 3, and its early-ASSERT abort then
// corrupts the shared obj_osi_internal state for later fixtures in the same process) and those
// binaries cannot call GT config APIs, so run_tests.sh turns the identifier off for them only.
static bool OdrObjectTypeIdentifierEnabled()
{
    static int cached = -1;  // -1 = uninitialized, 0 = disabled, 1 = enabled
    if (cached < 0)
    {
        bool        disabled = false;
        const char *v        = std::getenv("GT_OSI_ODR_OBJECT_TYPE");
        if (v != nullptr && v[0] != '\0')
        {
            std::string s(v);
            for (char &c : s)
            {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            disabled = (s == "0" || s == "false" || s == "off" || s == "no");
        }
        cached = disabled ? 0 : 1;
    }
    return cached == 1;
}

// Large OSI messages needs to be split for UDP transmission
// This struct must be mached on receiver side
static struct
{
    int          counter;
    unsigned int datasize;
    char         data[OSI_MAX_UDP_DATA_SIZE];
} osi_udp_buf;

OsiInternalObjects obj_osi_internal = {};
OsiExternalObjects obj_osi_external = {};
OsiTrafficCommandBuffer osiTrafficCommand = {};
OsiGroundTruthBuffer osiGroundTruth = {};
OsiRoadLaneBuffer osiRoadLane = {};
OsiRoadLaneBoundaryBuffer osiRoadLaneBoundary = {};

using namespace scenarioengine;

// Global OSIReporter pointer for access from Controllers
static OSIReporter* g_current_osi_reporter_ = nullptr;

void GT_SetCurrentOSIReporter(OSIReporter* reporter)
{
    g_current_osi_reporter_ = reporter;
}

OSIReporter* GT_GetCurrentOSIReporter()
{
    return g_current_osi_reporter_;
}

// ScenarioGateway

OSIReporter::OSIReporter(ScenarioEngine *scenarioengine)
{
    udp_client_      = nullptr;
    scenario_engine_ = scenarioengine;

    obj_osi_internal.static_gt         = new osi3::GroundTruth();
    obj_osi_internal.static_updated_gt = new osi3::GroundTruth();
    obj_osi_internal.dynamic_gt        = new osi3::GroundTruth();
    obj_osi_external.gt                = new osi3::GroundTruth();
    obj_osi_external.sv                = new osi3::SensorView();
    obj_osi_external.tc                = new osi3::TrafficCommand();

    // Read version number of the OSI code base
    auto current_osi_version = osi3::InterfaceVersion::descriptor()->file()->options().GetExtension(osi3::current_interface_version);

    obj_osi_internal.static_gt->mutable_version()->set_version_major(current_osi_version.version_major());
    obj_osi_internal.static_gt->mutable_version()->set_version_minor(current_osi_version.version_minor());
    obj_osi_internal.static_gt->mutable_version()->set_version_patch(current_osi_version.version_patch());

    obj_osi_internal.dynamic_gt->mutable_timestamp()->set_seconds(0);
    obj_osi_internal.dynamic_gt->mutable_timestamp()->set_nanos(0);

    obj_osi_external.tc->mutable_timestamp()->set_seconds(0);
    obj_osi_external.tc->mutable_timestamp()->set_nanos(0);

    // Sensor Data
    obj_osi_internal.sd = new osi3::SensorData();
}

OSIReporter::~OSIReporter()
{
    if (obj_osi_internal.static_gt)
    {
        obj_osi_internal.static_gt->Clear();
        delete obj_osi_internal.static_gt;
    }

    if (obj_osi_internal.static_updated_gt)
    {
        obj_osi_internal.static_updated_gt->Clear();
        delete obj_osi_internal.static_updated_gt;
    }

    if (obj_osi_internal.dynamic_gt)
    {
        obj_osi_internal.dynamic_gt->Clear();
        delete obj_osi_internal.dynamic_gt;
    }

    if (obj_osi_external.gt)
    {
        obj_osi_external.gt->Clear();
        delete obj_osi_external.gt;
    }

    if (obj_osi_internal.sd)
    {
        obj_osi_internal.sd->Clear();
        delete obj_osi_internal.sd;
    }

    if (obj_osi_external.sv)
    {
        obj_osi_external.sv->Clear();
        delete obj_osi_external.sv;
    }

    if (obj_osi_external.tc)
    {
        obj_osi_external.tc->Clear();
        delete obj_osi_external.tc;
    }

    obj_osi_internal.ln.clear();
    obj_osi_internal.lnb.clear();

    osiGroundTruth.size    = 0;
    osiRoadLane.size       = 0;
    osiTrafficCommand.size = 0;

    delete udp_client_;

    if (osi_file.is_open())
    {
        osi_file.close();
    }

    SE_Env::Inst().ResetOSITimeStamp();
}

SE_SOCKET OSIReporter::OpenSocket(std::string ipaddr)
{
    udp_client_ = new UDPClient(OSI_OUT_PORT, ipaddr);

    return udp_client_->GetStatus();
}

void OSIReporter::ReportSensors(std::vector<ObjectSensor *> sensor)
{
    if (sensor.size() == 0)
    {
        return;
    }
    while (obj_osi_internal.sd->sensor_view_size() < static_cast<int>(sensor.size()))
    {
        obj_osi_internal.sd->add_sensor_view();
    }
    for (unsigned int i = 0; i < sensor.size(); i++)
    {
        // Clear history
        obj_osi_internal.sd->mutable_sensor_view(static_cast<int>(i))->mutable_global_ground_truth()->clear_moving_object();
        for (unsigned int j = 0; j < static_cast<unsigned int>(sensor[i]->nObj_); j++)
        {
            // Create moving object
            osi3::MovingObject *mobj;
            mobj = obj_osi_internal.sd->mutable_sensor_view(static_cast<int>(i))->mutable_global_ground_truth()->add_moving_object();

            // Populate sensor data
            mobj->mutable_id()->set_value(sensor[i]->hitList_[j].obj_->g_id_);
            mobj->mutable_base()->mutable_position()->set_x(sensor[i]->hitList_[j].x_ +
                                                            static_cast<double>(sensor[i]->hitList_[j].obj_->boundingbox_.center_.x_) *
                                                                cos(sensor[i]->hitList_[j].yaw_));
            mobj->mutable_base()->mutable_position()->set_y(sensor[i]->hitList_[j].y_ +
                                                            static_cast<double>(sensor[i]->hitList_[j].obj_->boundingbox_.center_.x_) *
                                                                sin(sensor[i]->hitList_[j].yaw_));
            mobj->mutable_base()->mutable_position()->set_z(sensor[i]->hitList_[j].z_);
            mobj->mutable_base()->mutable_velocity()->set_x(sensor[i]->hitList_[j].velX_);
            mobj->mutable_base()->mutable_velocity()->set_y(sensor[i]->hitList_[j].velY_);
            mobj->mutable_base()->mutable_velocity()->set_z(sensor[i]->hitList_[j].velZ_);
            mobj->mutable_base()->mutable_acceleration()->set_x(sensor[i]->hitList_[j].accX_);
            mobj->mutable_base()->mutable_acceleration()->set_y(sensor[i]->hitList_[j].accY_);
            mobj->mutable_base()->mutable_acceleration()->set_z(sensor[i]->hitList_[j].accZ_);
            mobj->mutable_base()->mutable_orientation()->set_yaw(sensor[i]->hitList_[j].yaw_);
            mobj->mutable_base()->mutable_orientation_rate()->set_yaw(sensor[i]->hitList_[j].yawRate_);
            mobj->mutable_base()->mutable_orientation_acceleration()->set_yaw(sensor[i]->hitList_[j].yawAcc_);
            mobj->mutable_base()->mutable_dimension()->set_height(sensor[i]->hitList_[j].obj_->boundingbox_.dimensions_.height_);
            mobj->mutable_base()->mutable_dimension()->set_length(sensor[i]->hitList_[j].obj_->boundingbox_.dimensions_.length_);
            mobj->mutable_base()->mutable_dimension()->set_width(sensor[i]->hitList_[j].obj_->boundingbox_.dimensions_.width_);
        }
    }
}

bool OSIReporter::OpenOSIFile(const char *filename)
{
    osi_file.open(filename, std::ios_base::binary);
    if (!osi_file.good())
    {
        LOG_ERROR("Failed open OSI tracefile {}", filename);
        return false;
    }
    LOG_INFO("OSI tracefile {} opened", filename);
    return true;
}

void OSIReporter::CloseOSIFile()
{
    osi_file.close();
}

bool OSIReporter::WriteOSIFile()
{
    if (!osi_file.good())
    {
        return false;
    }

    // write to file, first size of message
    osi_file.write(reinterpret_cast<char *>(&osiGroundTruth.size), sizeof(osiGroundTruth.size));

    // write to file, actual message - the groundtruth object including timestamp and moving objects
    osi_file.write(osiGroundTruth.ground_truth.c_str(), osiGroundTruth.size);

    if (!osi_file.good())
    {
        LOG_ERROR("Failed write osi file");
        return false;
    }
    return true;
}

void OSIReporter::FlushOSIFile()
{
    if (osi_file.good())
    {
        osi_file.flush();
    }
}
void OSIReporter::SetOSIStaticReportMode(OSIStaticReportMode mode)
{
    static_update_mode_ = mode;
}

int OSIReporter::UpdateOSIGroundTruth(const std::vector<scenarioengine::Object*> &objectState)
{
    if (osi_initialized_ && (GetUpdated() || (GetCounter() - counter_offset_) % osi_freq_ != 0))
    {
        return 0;
    }
    osiGroundTruth.ground_truth.clear();
    osiGroundTruth.size = 0;
    if (!osi_initialized_)
    {
        CreateOSIStaticGroundTruthFromODR();
        UpdateOSIStaticGroundTruth(objectState);
        UpdateOSIDynamicGroundTruth(objectState);

        if (!objectState.empty())
        {
            obj_osi_internal.static_gt->mutable_host_vehicle_id()->set_value(objectState.front()->g_id_);
        }

        if (IsFileOpen() || GetUDPClientStatus() == 0)
        {
            SerializeDynamicAndStaticData();
        }
        // Merge for API
        obj_osi_external.gt->CopyFrom(*obj_osi_internal.dynamic_gt);
        obj_osi_external.gt->MergeFrom(*obj_osi_internal.static_gt);

        counter_offset_  = GetCounter();
        osi_initialized_ = true;
    }
    else
    {
        // We always want to update the dynamic ground truth
        UpdateOSIDynamicGroundTruth(objectState);
        obj_osi_external.gt->CopyFrom(*obj_osi_internal.dynamic_gt);

        UpdateOSIStaticGroundTruth(objectState);

        switch (static_update_mode_)
        {
            case OSIStaticReportMode::DEFAULT:  // Only log and transmit dynamic ground truth
                 if (IsFileOpen() || GetUDPClientStatus() == 0)
                {
                    SerializeDynamicData();
                }
                // include any added misc objects
                if (obj_osi_internal.static_updated_gt->stationary_object_size() > 0)
                {
                    obj_osi_external.gt->MergeFrom(*obj_osi_internal.static_updated_gt);
                }
                break;
            case OSIStaticReportMode::API:  // Log dynamic ground truth, serialize and transmit combined ground truth
                SerializeDynamicData();

                obj_osi_external.gt->MergeFrom(*obj_osi_internal.static_gt);  // Merge for API
                break;
            case OSIStaticReportMode::API_AND_LOG:  // Log combined ground truth, serialze and transmit combined ground truth
                SerializeDynamicAndStaticData();

                obj_osi_external.gt->MergeFrom(*obj_osi_internal.static_gt);  // Merge for API
                break;
        }
    }

    if (IsFileOpen())
    {
        WriteOSIFile();
    }

    if (GetUDPClientStatus() == 0)
    {
        // send over udp - split large OSI messages in multiple transmissions
        unsigned int sentDataBytes = 0;

        for (osi_udp_buf.counter = 1; sentDataBytes < osiGroundTruth.size; osi_udp_buf.counter++)
        {
            osi_udp_buf.datasize = MIN(osiGroundTruth.size - sentDataBytes, OSI_MAX_UDP_DATA_SIZE);
            memcpy(osi_udp_buf.data, &osiGroundTruth.ground_truth.c_str()[sentDataBytes], osi_udp_buf.datasize);
            int packSize = static_cast<int>(sizeof(osi_udp_buf)) - static_cast<int>((OSI_MAX_UDP_DATA_SIZE - osi_udp_buf.datasize));

            if (sentDataBytes + osi_udp_buf.datasize >= osiGroundTruth.size)
            {
                // Last package indicated by negative counter number
                osi_udp_buf.counter = -osi_udp_buf.counter;
            }

            int sendResult = udp_client_->Send(reinterpret_cast<char *>(&osi_udp_buf), static_cast<unsigned int>(packSize));  // TODO: @Emil

            if (sendResult != packSize)
            {
#ifdef _WIN32
                LOG_ERROR("Failed send osi package over UDP (error {})", WSAGetLastError());
#else
                LOG_ERROR("Failed send osi package over UDP");
#endif
                // Give up
                sentDataBytes = osiGroundTruth.size;
            }
            else
            {
                sentDataBytes += osi_udp_buf.datasize;
            }
        }
    }

    SetUpdated(true);
    return 0;
}

void OSIReporter::SerializeDynamicData()
{
    obj_osi_internal.static_updated_gt->SerializeToString(&osiGroundTruth.ground_truth);
    obj_osi_internal.dynamic_gt->AppendToString(&osiGroundTruth.ground_truth);
    osiGroundTruth.size = static_cast<unsigned int>(osiGroundTruth.ground_truth.size());
}

void OSIReporter::SerializeDynamicAndStaticData()
{
    obj_osi_internal.static_gt->AppendToString(&osiGroundTruth.ground_truth);
    obj_osi_internal.dynamic_gt->AppendToString(&osiGroundTruth.ground_truth);
    osiGroundTruth.size = static_cast<unsigned int>(osiGroundTruth.ground_truth.size());
}

namespace
{
// [GT_ODR:junc-boundary] P7 WP4 (cluster 8 L3), FLAGGED default OFF. Post-pass over the intersection
// lanes the PRISTINE upstream OSIReporter::UpdateOSIIntersection() just produced: for every junction
// that (a) is an OSI intersection and (b) has an authored <boundary> the side model could evaluate to
// a >= 3-point world polyline, synthesize ONE osi3::LaneBoundary and REPLACE the intersection lane's
// free_lane_boundary_id list with just that new boundary id. Otherwise the heuristic result is left
// untouched. Runs in GT code only; upstream stays pristine.
//
// Synthetic id scheme: every RM/OSI global id (lanes, lane boundaries, junctions, roadmark lines) is
// drawn from ONE monotonic counter via CommonMini GetNewGlobalId(). By the time this post-pass runs
// all real ids are already assigned, so a fresh GetNewGlobalId() is guaranteed collision-free against
// every existing boundary id -- no documented offset needed.
void ApplyAuthoredJunctionBoundaries(roadmanager::OpenDrive* opendrive)
{
    if (opendrive == nullptr || !gt_esmini::odr::GetUseAuthoredJunctionBoundary())
    {
        return;  // hard default-OFF no-op: no observable change anywhere
    }

    const void* key = static_cast<const void*>(opendrive);
    for (unsigned int i = 0; i < opendrive->GetNumOfJunctions(); i++)
    {
        roadmanager::Junction* junction = opendrive->GetJunctionByIdx(i);
        if (junction == nullptr || !junction->IsOsiIntersection())
        {
            continue;
        }

        std::vector<gt_esmini::odr::OdrBoundaryPoint> poly;
        if (!gt_esmini::odr::BuildAuthoredJunctionBoundaryPolyline(key, junction->GetIdStr(), opendrive, poly))
        {
            continue;  // no authored boundary / dangling ref / degenerate -> keep heuristic
        }

        // Locate the intersection lane the base pass added (osi lane id == junction global id).
        const id_t   junction_gid = junction->GetGlobalId();
        osi3::Lane*  osi_lane     = nullptr;
        for (int j = 0; j < obj_osi_internal.static_gt->lane_size(); j++)
        {
            osi3::Lane* cand = obj_osi_internal.static_gt->mutable_lane(j);
            if (cand->mutable_id()->value() == junction_gid)
            {
                osi_lane = cand;
                break;
            }
        }
        if (osi_lane == nullptr)
        {
            continue;  // base pass produced no intersection lane for this junction -> nothing to replace
        }

        // Synthesize one LaneBoundary from the authored polyline.
        osi3::LaneBoundary* osi_lb = obj_osi_internal.static_gt->add_lane_boundary();
        const id_t          lb_id  = GetNewGlobalId();
        osi_lb->mutable_id()->set_value(lb_id);
        for (const gt_esmini::odr::OdrBoundaryPoint& p : poly)
        {
            osi3::LaneBoundary_BoundaryPoint* bp = osi_lb->add_boundary_line();
            bp->mutable_position()->set_x(p.x);
            bp->mutable_position()->set_y(p.y);
            bp->mutable_position()->set_z(p.z);
        }
        osi_lb->mutable_classification()->set_type(
            osi3::LaneBoundary_Classification_Type::LaneBoundary_Classification_Type_TYPE_ROAD_EDGE);

        // REPLACE the heuristic free-lane-boundary ids with the single authored contour id.
        osi_lane->mutable_classification()->clear_free_lane_boundary_id();
        osi_lane->mutable_classification()->add_free_lane_boundary_id()->set_value(lb_id);

        LOG_INFO("[GT_ODR:junc-boundary] junction {} (osi lane {}): replaced heuristic free lane boundary with "
                 "authored contour (boundary id {}, {} pts)",
                 junction->GetIdStr(),
                 junction_gid,
                 lb_id,
                 poly.size());
    }
}
}  // namespace

int OSIReporter::CreateOSIStaticGroundTruthFromODR()
{
    int retval = 0;
    // First pick objects from the OpenSCENARIO description
    static roadmanager::OpenDrive *opendrive = roadmanager::Position::GetOpenDrive();
    for (unsigned i = 0; i < opendrive->GetNumOfRoads(); i++)
    {
        roadmanager::Road *road = opendrive->GetRoadByIdx(i);
        if (road)
        {
            for (unsigned int j = 0; j < road->GetNumberOfObjects(); j++)
            {
                roadmanager::RMObject *object = road->GetRoadObject(j);
                if (object)
                {
                    // P8: invalidated (1.9) -> excluded from OSI ground truth (see gt_roadmanager_patches.md P8).
                    // A model-invalid object is not part of the static ground truth. Objects without stored
                    // extras (the majority) and P5-synthesized CROSSWALKs (no authored entry) return nullptr
                    // here and are emitted unchanged.
                    const std::string odr_road_id =
                        road->GetIdStr().empty() ? std::to_string(road->GetId()) : road->GetIdStr();
                    const gt_esmini::odr::OdrObjectExtras *inv_ox =
                        gt_esmini::odr::GetObjectExtras(opendrive, odr_road_id, std::to_string(object->GetId()));
                    if (inv_ox != nullptr && inv_ox->invalidated)
                    {
                        continue;
                    }

                    if (UpdateOSIStationaryObjectODR(object, road))
                    {
                        retval = -1;
                    }
                    else if (retval > -1)
                    {
                        retval++;
                    }

                    if (object->GetNumberOfMarkings() > 0)
                    {
                        UpdateOSIRoadMarkingsODR(object, road);
                    }
                }
            }
        }
    }

    UpdateOSIRoadLane();
    UpdateOSILaneBoundary();
    UpdateOSIIntersection();
    // [GT_ODR:junc-boundary] WP4 flagged post-pass: swap heuristic free lane boundary for the authored
    // junction <boundary> contour (default OFF -> hard no-op, all OSI goldens byte-identical).
    ApplyAuthoredJunctionBoundaries(opendrive);
    UpdateStaticTrafficSignals();

    // Set the original geo reference string as is
    std::string proj_string_delimiter = "";
    if (!opendrive->GetGeoReferenceOriginalString().empty() && !opendrive->GetGeoOffsetOriginalString().empty())
    {
        proj_string_delimiter = ";";
    }
    obj_osi_internal.static_gt->set_proj_string(
        (opendrive->GetGeoReferenceOriginalString() + proj_string_delimiter + opendrive->GetGeoOffsetOriginalString()).c_str());
    obj_osi_internal.static_gt->set_map_reference(opendrive->GetGeoReferenceAsString());
    obj_osi_internal.static_gt->set_model_reference(stationary_model_reference);

    return retval;
}

int OSIReporter::UpdateOSIStaticGroundTruth(const std::vector<scenarioengine::Object*> &objectState)
{
    int retval = 0;

    obj_osi_internal.static_updated_gt->Clear();

    // Pick objects from the OpenSCENARIO description
    for (size_t i = 0; i < objectState.size(); i++)
    {
        if (objectState[i]->type_ == Object::Type::VEHICLE ||
            objectState[i]->type_ == Object::Type::PEDESTRIAN)
        {
            // do nothing
        }
        else if (objectState[i]->type_ == Object::Type::MISC_OBJECT)
        {
            retval += UpdateOSIStationaryObject(*objectState[i]);
        }
        else
        {
            LOG_WARN("Warning: Object type {} is not supported in OSIReporter, and hence no OSI update for this object",
                     objectState[i]->type_);
            retval = -1;
        }
    }

    // add any created stationary misc objects for serialization
    obj_osi_internal.static_gt->MergeFrom(*obj_osi_internal.static_updated_gt);

    return retval;
}

void OSIReporter::CropOSIDynamicGroundTruth(const int id, const double radius)
{
    if (osi_crop_.empty() && radius > SMALL_NUMBER)
    {
        osi_crop_.emplace_back(id, radius);
    }
    else
    {
        for (size_t i = 0; i < osi_crop_.size(); i++)
        {
            if (osi_crop_[i].first == id)
            {
                if (radius > SMALL_NUMBER)
                {
                    osi_crop_[i].second = radius;
                }
                else
                {
                    osi_crop_.erase(osi_crop_.begin() + static_cast<int>(i));
                    LOG_INFO("CropGroundTruth: Removed crop for entity id {}", id);
                }
                return;
            }
        }
        if (radius > SMALL_NUMBER)
        {
            osi_crop_.emplace_back(id, radius);
        }
    }
    LOG_INFO("CropGroundTruth: Added crop for entity id {} with radius {}", id, radius);
}

void OSIReporter::CheckDynamicTypeAndUpdate(const scenarioengine::Object& objectState)
{
    if (objectState.type_ == Object::Type::VEHICLE ||
        objectState.type_ == Object::Type::PEDESTRIAN)
    {
        if (objectState.GetControllerTypeActiveOnDomain(ControlDomains::DOMAIN_LONG) != Controller::Type::GHOST_RESERVED_TYPE || report_ghost_)
        {
            UpdateOSIMovingObject(objectState);
            // All non-ghost objects are always updated. Ghosts only on request.
        }
    }
    else if (objectState.type_ == Object::Type::MISC_OBJECT)
    {
        // do nothing
    }
    else
    {
        LOG_WARN("Warning: Object type {} is not supported in OSIReporter, and hence no OSI update for this object",
                 static_cast<int>(objectState.type_));
    }
}

int OSIReporter::UpdateOSIDynamicGroundTruth(const std::vector<scenarioengine::Object*> &objectState)
{
    obj_osi_internal.dynamic_gt->clear_moving_object();
    obj_osi_internal.dynamic_gt->clear_timestamp();

    if (SE_Env::Inst().IsOSITimeStampSet())
    {
        // use excplicit timestamp
        obj_osi_internal.dynamic_gt->mutable_timestamp()->set_seconds(static_cast<int64_t>((SE_Env::Inst().GetOSITimeStamp() / 1000000000)));
        obj_osi_internal.dynamic_gt->mutable_timestamp()->set_nanos(static_cast<uint32_t>((SE_Env::Inst().GetOSITimeStamp() % 1000000000)));
    }
    else if (scenario_engine_ != nullptr)
    {
        // report simulation time (v3.0.0: timestamp moved from per-object ObjectState to ScenarioEngine)
        double   time    = scenario_engine_->getSimulationTime();
        uint32_t seconds = static_cast<uint32_t>(floor(time));
        obj_osi_internal.dynamic_gt->mutable_timestamp()->set_seconds(seconds);
        obj_osi_internal.dynamic_gt->mutable_timestamp()->set_nanos(static_cast<uint32_t>((time - seconds) * 1e9));
    }
    else
    {
        // report time = 0
        obj_osi_internal.dynamic_gt->mutable_timestamp()->set_seconds(static_cast<int64_t>(0));
        obj_osi_internal.dynamic_gt->mutable_timestamp()->set_nanos(static_cast<uint32_t>(0));
    }

    // Set host_vehicle_id to the first object (Ego vehicle)
    if (!objectState.empty())
    {
        obj_osi_internal.dynamic_gt->mutable_host_vehicle_id()->set_value(
            objectState.front()->g_id_);
    }

    // Set OSI Moving Object Position
    // As OSI defines the origin of the object coordinates in the center of the bounding box and esmini (as OpenSCENARIO)
    // at the center of the rear axle, the position needs to be transformed.
    // For the transformation the orientation of the object has to be taken into account.
    for (const auto &obj : objectState)
    {
        obj->pos_.SetOsiXYZ(obj->boundingbox_.center_.x_,
                                  obj->boundingbox_.center_.y_,
                                  obj->boundingbox_.center_.z_);
    }

    if (osi_crop_.empty())
    {
        for (const auto &obj : objectState)
        {
            CheckDynamicTypeAndUpdate(*obj);
        }
    }
    else
    {
        std::unordered_set<int> ids_added;
        for (const auto &crop : osi_crop_)
        {
            scenarioengine::Object *crop_obj = nullptr;

            std::vector<scenarioengine::Object*>::const_iterator itr =
                std::find_if(objectState.begin(),
                             objectState.end(),
                             [crop](const scenarioengine::Object* obj) { return obj->id_ == crop.first; });
            if (itr != objectState.end())
            {
                crop_obj = *itr;
            }
            else
            {
                LOG_WARN("Warning: Object with id {} not found in the scenario, and hence no OSI update around this object", crop.first);
                continue;
            }

            for (const auto &obj : objectState)
            {
                bool update = false;
                if (crop_obj->id_ == obj->id_)  // Update the crop object itself
                {
                    update = true;
                }
                else
                {
                    // Check OSI relative distance
                    double rel_dist = pow(crop_obj->pos_.GetOsiX() - obj->pos_.GetOsiX(), 2) +
                                      pow(crop_obj->pos_.GetOsiY() - obj->pos_.GetOsiY(), 2) +
                                      pow(crop_obj->pos_.GetOsiZ() - obj->pos_.GetOsiZ(), 2);

                    if (rel_dist < crop.second * crop.second)  // Update the object if it is within the crop distance
                    {
                        update = true;
                    }
                }

                if (update && !ids_added.count(obj->id_))  // Update only once
                {
                    ids_added.insert(obj->id_);
                    CheckDynamicTypeAndUpdate(*obj);
                }
            }
        }
    }

    UpdateEnvironment(scenario_engine_->environment);
    UpdateDynamicTrafficSignals();

    return 0;
}

// UpdateOSIHostVehicleData() removed: orphan method (entire body was commented out,
// no declaration in OSIReporter.hpp or GT headers). Dead code cleanup during v3.0.0 migration.

int OSIReporter::UpdateOSIStationaryObjectODR(roadmanager::RMObject *object, roadmanager::Road *road)
{
    // [fork-sync #37 G1] Ported from upstream 752dcaa0..77028d83 (stationary/outline/markings rework,
    // cbf22f5a + ed95d1c6 + 4ab787ac) + c0a143d5 (open-outline idx bounds guard). Replaces the previous
    // GT body (single instance, raw outline corners, no repeat support) with upstream's per-repeat-instance
    // emission built on RMObject::GetRepeatInstances(road), which is the single source of truth shared with
    // the viewer. GT P8 invalidated-object skip stays in the caller (CreateOSIStaticGroundTruthFromODR),
    // unchanged by this port.

    // Build the OSI base polygon (object local 2D frame) into 'base' and return the average corner
    // height. The polygon is reported relative to the instance position/orientation (base.position /
    // base.orientation). The corner positions were resolved once in RMObject::GetRepeatInstances and are
    // stored on the instance in the local frame, so here they are copied straight into base_polygon (and
    // shared with the viewer). cornerRoad corners follow the road curvature (and therefore differ between
    // repeat instances on a curve); cornerLocal corners keep a fixed local shape.
    auto build_outline_polygon = [&](osi3::BaseStationary *base, const roadmanager::RepeatInstance &ri) -> double
    {
        double       height_sum   = 0.0;
        unsigned int corner_count = 0;

        for (unsigned int k = 0; k < ri.outline_corners.size(); k++)
        {
            const std::vector<roadmanager::ResolvedOutlineCorner> &corners = ri.outline_corners[k];
            const roadmanager::Outline                            *outline = object->GetOutline(k);

            const int start = base->base_polygon_size();
            for (const roadmanager::ResolvedOutlineCorner &corner : corners)
            {
                // The resolved corner is already in the instance local frame
                osi3::Vector2d *vec = base->add_base_polygon();
                vec->set_x(corner.x);
                vec->set_y(corner.y);
                height_sum += corner.height;
                corner_count++;
            }

            if (outline != nullptr && !outline->closed_)
            {
                // Repeat intermediate vertices to close the polygon, avoiding single edge between last and first vertices
                for (int l = static_cast<int>(corners.size()) - 2; l > 0; l--)
                {
                    int desired_idx = start + l;
                    if (desired_idx >= 0)
                    {
                        osi3::Vector2d *vec = base->add_base_polygon();
                        vec->set_x(base->base_polygon().at(desired_idx).x());
                        vec->set_y(base->base_polygon().at(desired_idx).y());
                    }
                }
            }
        }

        return corner_count > 0 ? height_sum / static_cast<double>(corner_count) : 0.0;
    };

    // Emit a single OSI stationary object for one instance of the road object. The classification and
    // source reference are the same for every instance; the position, orientation and dimension are
    // instance specific. The first instance keeps the object's reserved global id, additional repeated
    // instances get fresh global ids.
    auto emit_instance = [&](const roadmanager::RepeatInstance &ri, bool first_instance)
    {
        obj_osi_internal.sobj = obj_osi_internal.static_gt->add_stationary_object();

        // SOURCE REFERENCE
        auto source_reference = obj_osi_internal.sobj->add_source_reference();
        source_reference->set_type(SOURCE_REF_TYPE_ODR);
        std::string src_ref_type = "object";

        // Set OSI Stationary Object Mutable ID (unique per emitted instance)
        obj_osi_internal.sobj->mutable_id()->set_value(first_instance ? object->GetGlobalId() : GetNewGlobalId());

        // Set OSI Stationary Object Type and Classification
        auto obj_type = object->GetType();
        if (obj_type == roadmanager::RMObject::ObjectType::POLE)
        {
            obj_osi_internal.sobj->mutable_classification()->set_type(
                osi3::StationaryObject_Classification_Type::StationaryObject_Classification_Type_TYPE_POLE);
        }
        else if (obj_type == roadmanager::RMObject::ObjectType::TREE)
        {
            obj_osi_internal.sobj->mutable_classification()->set_type(
                osi3::StationaryObject_Classification_Type::StationaryObject_Classification_Type_TYPE_TREE);
        }
        else if (obj_type == roadmanager::RMObject::ObjectType::VEGETATION)
        {
            obj_osi_internal.sobj->mutable_classification()->set_type(
                osi3::StationaryObject_Classification_Type::StationaryObject_Classification_Type_TYPE_VEGETATION);
        }
        else if (obj_type == roadmanager::RMObject::ObjectType::BARRIER)
        {
            obj_osi_internal.sobj->mutable_classification()->set_type(
                osi3::StationaryObject_Classification_Type::StationaryObject_Classification_Type_TYPE_BARRIER);
        }
        else if (obj_type == roadmanager::RMObject::ObjectType::BUILDING)
        {
            obj_osi_internal.sobj->mutable_classification()->set_type(
                osi3::StationaryObject_Classification_Type::StationaryObject_Classification_Type_TYPE_BUILDING);
        }
        else if (obj_type == roadmanager::RMObject::ObjectType::PARKINGSPACE)
        {
            obj_osi_internal.sobj->mutable_classification()->set_type(
                osi3::StationaryObject_Classification_Type::StationaryObject_Classification_Type_TYPE_OTHER);
            obj_osi_internal.sobj->mutable_classification()->set_material(
                osi3::StationaryObject_Classification_Material::StationaryObject_Classification_Material_MATERIAL_CONCRETE);
            obj_osi_internal.sobj->mutable_classification()->set_density(
                osi3::StationaryObject_Classification_Density::StationaryObject_Classification_Density_DENSITY_SOLID);
            obj_osi_internal.sobj->mutable_classification()->set_color(
                osi3::StationaryObject_Classification_Color::StationaryObject_Classification_Color_COLOR_GREY);

            source_reference->add_identifier(fmt::format("restrictions:{}", object->GetParkingSpace().GetRestrictions()));
        }
        else if (obj_type == roadmanager::RMObject::ObjectType::OBSTACLE || obj_type == roadmanager::RMObject::ObjectType::RAILING ||
                 obj_type == roadmanager::RMObject::ObjectType::PATCH || obj_type == roadmanager::RMObject::ObjectType::TRAFFICISLAND ||
                 obj_type == roadmanager::RMObject::ObjectType::CROSSWALK || obj_type == roadmanager::RMObject::ObjectType::STREETLAMP ||
                 obj_type == roadmanager::RMObject::ObjectType::GANTRY || obj_type == roadmanager::RMObject::ObjectType::SOUNDBARRIER ||
                 obj_type == roadmanager::RMObject::ObjectType::WIND || obj_type == roadmanager::RMObject::ObjectType::ROADMARK)
        {
            obj_osi_internal.sobj->mutable_classification()->set_type(
                osi3::StationaryObject_Classification_Type::StationaryObject_Classification_Type_TYPE_OTHER);
        }
        else if (obj_type == roadmanager::RMObject::ObjectType::BRIDGE)
        {
            obj_osi_internal.sobj->mutable_classification()->set_type(
                osi3::StationaryObject_Classification_Type::StationaryObject_Classification_Type_TYPE_BRIDGE);
            src_ref_type = "bridge";
        }
        else
        {
            obj_osi_internal.sobj->mutable_classification()->set_type(
                osi3::StationaryObject_Classification_Type::StationaryObject_Classification_Type_TYPE_UNKNOWN);

            if (obj_type != roadmanager::RMObject::ObjectType::NONE)
            {
                LOG_ERROR("OSIReporter::UpdateOSIStationaryObjectODR -> Unsupported stationary object category {}", obj_type);
            }
        }

        source_reference->add_identifier(fmt::format("object_type:{}", src_ref_type));
        source_reference->add_identifier(fmt::format("object_id:{}", object->GetId()));
        // [GT_MOD] In-band ODR object kind (signal:crosswalk_footprint): the classification above folds
        // crosswalk/railing/patch/... into TYPE_OTHER, so the ODR <object type> string is the only way a
        // consumer can tell them apart without re-parsing the xodr. Synthesized crossPath objects carry
        // ObjectType::CROSSWALK too, so "odr_type:crosswalk" covers both authored and synthesized ones.
        // Env-gated (see OdrObjectTypeIdentifierEnabled above) for the upstream unit suites only.
        if (OdrObjectTypeIdentifierEnabled())
        {
            source_reference->add_identifier(fmt::format("odr_type:{}", object->GetTypeStr()));
        }

        // Set OSI Stationary Object Orientation
        obj_osi_internal.sobj->mutable_base()->mutable_orientation()->set_roll(GetAngleInIntervalMinusPIPlusPI(ri.r));
        obj_osi_internal.sobj->mutable_base()->mutable_orientation()->set_pitch(GetAngleInIntervalMinusPIPlusPI(ri.p));
        obj_osi_internal.sobj->mutable_base()->mutable_orientation()->set_yaw(GetAngleInIntervalMinusPIPlusPI(ri.h));

        // Outline based objects report their footprint as the OSI base polygon and derive the height
        // from the (scaled) average outline corner height; other objects use the bounding box height.
        double height = ri.inst_hgt;
        if (object->GetNumberOfOutlines() > 0)
        {
            height = build_outline_polygon(obj_osi_internal.sobj->mutable_base(), ri);
        }

        // Set OSI Stationary Object Boundingbox
        obj_osi_internal.sobj->mutable_base()->mutable_dimension()->set_height(height);
        obj_osi_internal.sobj->mutable_base()->mutable_dimension()->set_width(ri.inst_wid);
        obj_osi_internal.sobj->mutable_base()->mutable_dimension()->set_length(ri.inst_len);

        // Set OSI Stationary Object Position (OSI origin is the center of the bounding box)
        obj_osi_internal.sobj->mutable_base()->mutable_position()->set_x(ri.x);
        obj_osi_internal.sobj->mutable_base()->mutable_position()->set_y(ri.y);
        obj_osi_internal.sobj->mutable_base()->mutable_position()->set_z(ri.z + height / 2.0);

        if (!object->GetModel3DFullPath().empty())
        {
            // Set 3D model file as OSI model reference
            obj_osi_internal.sobj->set_model_reference(object->GetModel3DFullPath());
        }
    };

    // One OSI stationary object per resolved instance (single object, or one per repeat copy). The
    // instance placement is computed once in RoadManager so the viewer and OSI stay in sync.
    const std::vector<roadmanager::RepeatInstance> instances = object->GetRepeatInstances(road);
    for (size_t i = 0; i < instances.size(); i++)
    {
        emit_instance(instances[i], i == 0);
    }

    return 0;
}

static osi3::RoadMarking_Classification_Color ODRColor2OSIMarkingColor(roadmanager::RoadMarkColor color)
{
    switch (color)
    {
        case roadmanager::RoadMarkColor::WHITE:
        case roadmanager::RoadMarkColor::STANDARD:
            return osi3::RoadMarking_Classification_Color_COLOR_WHITE;
        case roadmanager::RoadMarkColor::YELLOW:
            return osi3::RoadMarking_Classification_Color_COLOR_YELLOW;
        case roadmanager::RoadMarkColor::BLUE:
            return osi3::RoadMarking_Classification_Color_COLOR_BLUE;
        case roadmanager::RoadMarkColor::RED:
            return osi3::RoadMarking_Classification_Color_COLOR_RED;
        case roadmanager::RoadMarkColor::GREEN:
            return osi3::RoadMarking_Classification_Color_COLOR_GREEN;
        case roadmanager::RoadMarkColor::VIOLET:
            return osi3::RoadMarking_Classification_Color_COLOR_VIOLET;
        case roadmanager::RoadMarkColor::ORANGE:
            return osi3::RoadMarking_Classification_Color_COLOR_ORANGE;
        default:
            return osi3::RoadMarking_Classification_Color_COLOR_OTHER;
    }
}

int OSIReporter::UpdateOSIRoadMarkingsODR(roadmanager::RMObject *object, roadmanager::Road *road)
{
    // [fork-sync #37 G1] New port from upstream 752dcaa0..77028d83 (same stationary/outline/markings
    // rework as UpdateOSIStationaryObjectODR above). GT previously had no implementation of this
    // function despite it being declared in the shared OSIReporter.hpp (header never swapped).
    //
    // Build and report one osi3::RoadMarking for the given marking, placed at a single object instance.
    // Outline (cornerReference) markings reuse the outline corner positions resolved once in
    // RMObject::GetRepeatInstances (so they follow the road curvature for cornerRoad and keep a fixed
    // shape for cornerLocal, consistent with the outline geometry and the viewer). Bounding box side
    // markings are placed in the object local frame and rigidly transformed to the instance.
    auto emit_marking = [&](const roadmanager::ObjectMarking *marking, unsigned int marking_index, const roadmanager::RepeatInstance &ri)
    {
        if (marking == nullptr)
        {
            return;
        }

        std::vector<std::array<double, 3>> points;

        const double inst_x = ri.x;
        const double inst_y = ri.y;
        const double cosh   = cos(ri.h);
        const double sinh   = sin(ri.h);
        const double z      = ri.z + marking->z_offset_;

        if (marking->UsesOutline())
        {
            // Outline based marking: reuse the resolved corner (stored in the instance local frame)
            for (size_t c = 0; c < marking->corner_references_.size(); c++)
            {
                const roadmanager::ResolvedOutlineCorner *corner = ri.FindCorner(marking->corner_references_[c]);
                if (corner != nullptr)
                {
                    // Transform the instance local corner (scaled edge, object floor) to world
                    const double wx = inst_x + corner->x * cosh - corner->y * sinh;
                    const double wy = inst_y + corner->x * sinh + corner->y * cosh;
                    points.push_back({wx, wy, corner->marking_z + marking->z_offset_});
                }
            }
        }
        else if (marking->side_ != roadmanager::ObjectMarking::Side::NONE)
        {
            // Bounding box side based marking: build the two endpoints of the selected side
            double half_l = ri.inst_len / 2.0;
            double half_w = ri.inst_wid / 2.0;

            // local endpoints (u along heading, v to the left)
            double u0, v0, u1, v1;
            switch (marking->side_)
            {
                case roadmanager::ObjectMarking::Side::FRONT:
                    u0 = half_l, v0 = half_w, u1 = half_l, v1 = -half_w;
                    break;
                case roadmanager::ObjectMarking::Side::REAR:
                    u0 = -half_l, v0 = half_w, u1 = -half_l, v1 = -half_w;
                    break;
                case roadmanager::ObjectMarking::Side::LEFT:
                    u0 = half_l, v0 = half_w, u1 = -half_l, v1 = half_w;
                    break;
                case roadmanager::ObjectMarking::Side::RIGHT:
                    u0 = half_l, v0 = -half_w, u1 = -half_l, v1 = -half_w;
                    break;
                default:
                    return;
            }

            points.push_back({inst_x + u0 * cosh - v0 * sinh, inst_y + u0 * sinh + v0 * cosh, z});
            points.push_back({inst_x + u1 * cosh - v1 * sinh, inst_y + u1 * sinh + v1 * cosh, z});
        }

        if (points.size() < 2)
        {
            return;
        }

        osi3::RoadMarking *road_marking = obj_osi_internal.static_gt->add_road_marking();
        road_marking->mutable_id()->set_value(GetNewGlobalId());

        // Classification
        road_marking->mutable_classification()->set_type(osi3::RoadMarking_Classification_Type_TYPE_OTHER);
        road_marking->mutable_classification()->set_monochrome_color(ODRColor2OSIMarkingColor(marking->color_));

        // Source reference back to the OpenDRIVE object and marking index
        auto source_reference = road_marking->add_source_reference();
        source_reference->set_type(SOURCE_REF_TYPE_ODR);
        source_reference->add_identifier(fmt::format("object_id:{}", object->GetId()));
        source_reference->add_identifier(fmt::format("marking_index:{}", marking_index));

        // Compute centroid for the base position and report the polyline relative to it
        double cx = 0.0, cy = 0.0, cz = 0.0;
        for (const auto &p : points)
        {
            cx += p[0];
            cy += p[1];
            cz += p[2];
        }
        cx /= static_cast<double>(points.size());
        cy /= static_cast<double>(points.size());
        cz /= static_cast<double>(points.size());

        road_marking->mutable_base()->mutable_position()->set_x(cx);
        road_marking->mutable_base()->mutable_position()->set_y(cy);
        road_marking->mutable_base()->mutable_position()->set_z(cz);
        road_marking->mutable_base()->mutable_dimension()->set_width(marking->width_);

        for (const auto &p : points)
        {
            osi3::Vector2d *vec = road_marking->mutable_base()->add_base_polygon();
            vec->set_x(p[0] - cx);
            vec->set_y(p[1] - cy);
        }
    };

    // Replicate the markings for each resolved instance (single object, or one per repeat copy). The
    // instance placement and corner resolution are computed once in RoadManager so the viewer and OSI
    // stay in sync.
    const std::vector<roadmanager::RepeatInstance> instances = object->GetRepeatInstances(road);
    for (const roadmanager::RepeatInstance &ri : instances)
    {
        for (unsigned int m = 0; m < object->GetNumberOfMarkings(); m++)
        {
            emit_marking(object->GetMarking(m), m, ri);
        }
    }

    return 0;
}

int OSIReporter::UpdateOSIStationaryObject(scenarioengine::Object &objectState)
{
    // First check if the object has not been created
    if (objectState.GetOSIIndex() != ID_UNDEFINED)
    {
        return 0;
    }

    // Create OSI Stationary Object
    obj_osi_internal.sobj = obj_osi_internal.static_updated_gt->add_stationary_object();

    // Set OSI Stationary Object Mutable ID
    obj_osi_internal.sobj->mutable_id()->set_value(objectState.g_id_);

    // Set OSI Stationary Object Type and Classification
    if (objectState.type_ == Object::Type::MISC_OBJECT)
    {
        if (objectState.category_ == static_cast<int>(MiscObject::Category::NONE))
        {
            obj_osi_internal.sobj->mutable_classification()->set_type(
                osi3::StationaryObject_Classification_Type::StationaryObject_Classification_Type_TYPE_UNKNOWN);
        }
        else if (objectState.category_ == static_cast<int>(MiscObject::Category::POLE))
        {
            obj_osi_internal.sobj->mutable_classification()->set_type(
                osi3::StationaryObject_Classification_Type::StationaryObject_Classification_Type_TYPE_POLE);
        }
        else if (objectState.category_ == static_cast<int>(MiscObject::Category::TREE))
        {
            obj_osi_internal.sobj->mutable_classification()->set_type(
                osi3::StationaryObject_Classification_Type::StationaryObject_Classification_Type_TYPE_TREE);
        }
        else if (objectState.category_ == static_cast<int>(MiscObject::Category::VEGETATION))
        {
            obj_osi_internal.sobj->mutable_classification()->set_type(
                osi3::StationaryObject_Classification_Type::StationaryObject_Classification_Type_TYPE_VEGETATION);
        }
        else if (objectState.category_ == static_cast<int>(MiscObject::Category::BARRIER))
        {
            obj_osi_internal.sobj->mutable_classification()->set_type(
                osi3::StationaryObject_Classification_Type::StationaryObject_Classification_Type_TYPE_BARRIER);
        }
        else if (objectState.category_ == static_cast<int>(MiscObject::Category::BUILDING))
        {
            obj_osi_internal.sobj->mutable_classification()->set_type(
                osi3::StationaryObject_Classification_Type::StationaryObject_Classification_Type_TYPE_BUILDING);
        }
        else if (objectState.category_ == static_cast<int>(MiscObject::Category::OBSTACLE) ||
                 objectState.category_ == static_cast<int>(MiscObject::Category::PARKINGSPACE) ||
                 objectState.category_ == static_cast<int>(MiscObject::Category::RAILING) ||
                 objectState.category_ == static_cast<int>(MiscObject::Category::PATCH) ||
                 objectState.category_ == static_cast<int>(MiscObject::Category::TRAFFICISLAND) ||
                 objectState.category_ == static_cast<int>(MiscObject::Category::CROSSWALK) ||
                 objectState.category_ == static_cast<int>(MiscObject::Category::STREETLAMP) ||
                 objectState.category_ == static_cast<int>(MiscObject::Category::GANTRY) ||
                 objectState.category_ == static_cast<int>(MiscObject::Category::SOUNDBARRIER) ||
                 objectState.category_ == static_cast<int>(MiscObject::Category::WIND) ||
                 objectState.category_ == static_cast<int>(MiscObject::Category::ROADMARK))
        {
            obj_osi_internal.sobj->mutable_classification()->set_type(
                osi3::StationaryObject_Classification_Type::StationaryObject_Classification_Type_TYPE_OTHER);
        }
        else
        {
            obj_osi_internal.sobj->mutable_classification()->set_type(
                osi3::StationaryObject_Classification_Type::StationaryObject_Classification_Type_TYPE_UNKNOWN);

            LOG_WARN("OSIReporter::UpdateOSIStationaryObject -> Unsupported stationary object category {}", objectState.category_);
        }
    }

    // Set OSI Stationary Object Boundingbox
    obj_osi_internal.sobj->mutable_base()->mutable_dimension()->set_height(objectState.boundingbox_.dimensions_.height_);
    obj_osi_internal.sobj->mutable_base()->mutable_dimension()->set_width(objectState.boundingbox_.dimensions_.width_);
    obj_osi_internal.sobj->mutable_base()->mutable_dimension()->set_length(objectState.boundingbox_.dimensions_.length_);

    // [fork-sync #37 G3] Set 3D model file as OSI model reference. Upstream 752dcaa0..77028d83 switched
    // this from the bare filename (GetModel3DFilename) to the full resolved path (GetModel3DFullPath),
    // matching the ODR-side stationary object and the moving object model_reference.
    obj_osi_internal.sobj->set_model_reference(objectState.GetModel3DFullPath());

    // SOURCE REFERENCE
    auto source_reference = obj_osi_internal.sobj->add_source_reference();
    source_reference->set_type(SOURCE_REF_TYPE_OSC);

    std::string entity_type = fmt::format("object_type:MiscObject");
    std::string entity_name = fmt::format("object_name:{}", objectState.name_);

    source_reference->add_identifier(entity_type);
    source_reference->add_identifier(entity_name);

    // Add source reference if available in scenario
    if (!objectState.GetSourceReference().empty())
    {
        for (const auto &ref : objectState.GetSourceReference())
        {
            source_reference->add_identifier(ref);
        }
    }

    // [fork-sync #37 G3] Color (ported from upstream): report the authored <Color> (if any) as an OSI
    // color_description RGB triplet.
    if (!objectState.GetColorStr().empty())
    {
        auto rgb = objectState.GetColorRgb();
        obj_osi_internal.sobj->mutable_color_description()->mutable_rgb()->set_red(rgb.r);
        obj_osi_internal.sobj->mutable_color_description()->mutable_rgb()->set_green(rgb.g);
        obj_osi_internal.sobj->mutable_color_description()->mutable_rgb()->set_blue(rgb.b);
    }

    // Set OSI Stationary Object Position
    obj_osi_internal.sobj->mutable_base()->mutable_position()->set_x(
        objectState.pos_.GetX() + static_cast<double>(objectState.boundingbox_.center_.x_) * cos(objectState.pos_.GetH()));
    obj_osi_internal.sobj->mutable_base()->mutable_position()->set_y(
        objectState.pos_.GetY() + static_cast<double>(objectState.boundingbox_.center_.x_) * sin(objectState.pos_.GetH()));
    obj_osi_internal.sobj->mutable_base()->mutable_position()->set_z(
        objectState.pos_.GetZ() + static_cast<double>(objectState.boundingbox_.dimensions_.height_) / 2.0);

    // Set OSI Stationary Object Orientation
    obj_osi_internal.sobj->mutable_base()->mutable_orientation()->set_roll(GetAngleInIntervalMinusPIPlusPI(objectState.pos_.GetR()));
    obj_osi_internal.sobj->mutable_base()->mutable_orientation()->set_pitch(GetAngleInIntervalMinusPIPlusPI(objectState.pos_.GetP()));
    obj_osi_internal.sobj->mutable_base()->mutable_orientation()->set_yaw(GetAngleInIntervalMinusPIPlusPI(objectState.pos_.GetH()));

    // [fork-sync #37 G3] Set outline for scenario MiscObjects, if available (ported from upstream).
    for (const auto &p : objectState.outline_2d_)
    {
        osi3::Vector2d *vec = obj_osi_internal.sobj->mutable_base()->add_base_polygon();
        vec->set_x(p.x);
        vec->set_y(p.y);
    }

    objectState.SetOSIIndex(obj_osi_internal.static_gt->stationary_object_size());

    return 1;
}

// [GT_MOD] Helper function to get target lane ID from route for a given road
// Moving-object classification and projected trajectory related OSIReporter member definitions
// are implemented in GT_OSIReporter_Moving.cpp.

// Geometry/lane/intersection related OSIReporter member definitions
// are implemented in GT_OSIReporter_Geometry.cpp.

// Traffic signal and traffic command related OSIReporter member definitions
// are implemented in GT_OSIReporter_Traffic.cpp.

// SensorView conversion related OSIReporter member definitions
// are implemented in GT_OSIReporter_Sensor.cpp.

// OSI raw/lane query related OSIReporter member definitions
// are implemented in GT_OSIReporter_Api.cpp.

// Environment and light mapping related OSIReporter member definitions
// are implemented in GT_OSIReporter_Environment.cpp.



