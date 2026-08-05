/*
 * GT_esmini - Extended esmini with HostVehicleData Functionality
 * https://github.com/esmini/esmini
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2024 GT_esmini contributors
 */

#include "gt_esmini/osi/GT_HostVehicleReporter.hpp"
#include "CommonMini.hpp"
#include "gt_esmini/common/SimpleJson.hpp"
#include "logger.hpp"
#include "Entities.hpp"
#include "osi_hostvehicledata.pb.h"

#include <cmath>
#include <cstring>

namespace gt_esmini
{

GT_HostVehicleReporter& GT_HostVehicleReporter::Instance()
{
    static GT_HostVehicleReporter instance;
    return instance;
}

GT_HostVehicleReporter::GT_HostVehicleReporter()
    : udp_client_(nullptr)
    , initialized_(false)
{
}

GT_HostVehicleReporter::~GT_HostVehicleReporter()
{
    // The singleton is destroyed during process exit (static destruction), where
    // Winsock may already be de-initialized: deleting the UDP client there makes
    // core UDPBase::CloseGracefully() fail with WSANOTINITIALISED (10093).
    // Leave the socket to the OS; runtime cleanup goes through Close()/Init().
    udp_client_ = nullptr;
}

void GT_HostVehicleReporter::Close()
{
    if (udp_client_)
    {
        delete udp_client_;
        udp_client_ = nullptr;
    }
    initialized_ = false;
    input_cache_.clear();
}

void GT_HostVehicleReporter::Init(int udp_port, const std::string& config_file, const std::string& target_ip)
{
    // Load configuration if provided
    if (!config_file.empty())
    {
        LoadConfig(config_file);
    }

    // Use config port if set, otherwise use parameter
    int port = config_file.empty() ? udp_port : config_.udp_port;
    
    // Empty target_ip means use config/default. A non-empty argument is an explicit override.
    std::string final_ip = config_.target_ip;
    if (!target_ip.empty())
    {
        final_ip = target_ip;
    }

    // Initialize UDP client
    if (udp_client_)
    {
        delete udp_client_;
    }

    udp_client_ = new UDPClient(static_cast<unsigned short>(port), final_ip.c_str());

    if (udp_client_->GetStatus() != 0)
    {
        LOG_ERROR("GT_HostVehicleReporter: Failed to initialize UDP client on {}:{}", final_ip, port);
        delete udp_client_;
        udp_client_ = nullptr;
    }
    else
    {
        LOG_INFO("GT_HostVehicleReporter: UDP client initialized on {}:{}", final_ip, port);
        initialized_ = true;
    }
}

void GT_HostVehicleReporter::LoadConfig(const std::string& config_file)
{
    simplejson::Value root;
    std::string error;
    if (!simplejson::LoadFile(config_file, root, &error))
    {
        if (error == "failed to open file")
        {
            LOG_INFO("GT_HostVehicleReporter: Config file not found, using defaults: {}", config_file);
        }
        else
        {
            LOG_WARN("GT_HostVehicleReporter: Failed to parse config '{}': {} -- continuing with ALL built-in defaults (udp_port/target_ip/enable_host_vehicle_data unchanged)", config_file, error);
        }
        return;
    }
    if (!root.IsObject())
    {
        LOG_WARN("GT_HostVehicleReporter: Root JSON value in '{}' is not an object", config_file);
        return;
    }

    if (root.Find("steering_input_to_wheel_ratio") && !root.GetDouble("steering_input_to_wheel_ratio", config_.steering_input_to_wheel_ratio))
        LOG_WARN("GT_HostVehicleReporter: Failed to parse steering_input_to_wheel_ratio from config");
    if (root.Find("udp_port") && !root.GetInt("udp_port", config_.udp_port))
        LOG_WARN("GT_HostVehicleReporter: Failed to parse udp_port from config");
    if (root.Find("enable_host_vehicle_data") && !root.GetBool("enable_host_vehicle_data", config_.enabled))
        LOG_WARN("GT_HostVehicleReporter: Failed to parse enable_host_vehicle_data from config");
    if (root.Find("target_ip") && !root.GetString("target_ip", config_.target_ip))
        LOG_WARN("GT_HostVehicleReporter: Failed to parse target_ip from config");
    if (root.Find("target_vehicle") && !root.GetString("target_vehicle", config_.target_vehicle))
        LOG_WARN("GT_HostVehicleReporter: Failed to parse target_vehicle from config");

    LOG_INFO("GT_HostVehicleReporter: Config loaded: steering_ratio={}, udp_port={}, target_ip={}, enabled={}, target_vehicle={}",
             config_.steering_input_to_wheel_ratio,
             config_.udp_port,
             config_.target_ip,
             config_.enabled,
             config_.target_vehicle.empty() ? "(default: index 0)" : config_.target_vehicle);
}

void GT_HostVehicleReporter::SetInputs(int vehicle_id, double throttle, double brake, double steering, int gear)
{
    auto& cache = input_cache_[vehicle_id];
    cache.throttle = throttle;
    cache.brake = brake;
    cache.steering_angle = steering * config_.steering_input_to_wheel_ratio;
    cache.gear = gear;
}

void GT_HostVehicleReporter::SetLights(int vehicle_id, int light_mask)
{
    auto& cache = input_cache_[vehicle_id];
    cache.light_mask = light_mask;
}

void GT_HostVehicleReporter::SetBaseHostVehicleData(int vehicle_id, const osi3::HostVehicleData& data)
{
    auto& cache = input_cache_[vehicle_id];
    cache.base_data = data;
    cache.has_base_data = true;
}

void GT_HostVehicleReporter::SetPowertrain(int vehicle_id, double rpm, double torque)
{
    auto& cache = input_cache_[vehicle_id];
    cache.rpm = rpm;
    cache.torque = torque;
}

void GT_HostVehicleReporter::AddADASFunctionEx(int                                                     vehicle_id,
                                               int                                                     osi_name,
                                               const std::string&                                      custom_name,
                                               int                                                     state,
                                               const std::vector<std::pair<std::string, std::string>>& detail,
                                               const AdasFunctionOverride&                             driver_override)
{
    auto& cache = input_cache_[vehicle_id];

    for (auto& func : cache.adas_functions)
    {
        if (func.name == custom_name)
        {
            func.state           = state;
            func.osi_name        = osi_name;
            func.detail          = detail;
            func.driver_override = driver_override;
            return;
        }
    }

    InputCache::ADASFunction func;
    func.name            = custom_name;
    func.state           = state;
    func.osi_name        = osi_name;
    func.detail          = detail;
    func.driver_override = driver_override;
    cache.adas_functions.push_back(func);
}

void GT_HostVehicleReporter::ClearADASFunctions(int vehicle_id)
{
    if (input_cache_.count(vehicle_id) > 0)
    {
        input_cache_[vehicle_id].adas_functions.clear();
    }
}

int GT_HostVehicleReporter::UpdateFromObjectState(const scenarioengine::Object* egoState)
{
    if (!egoState)
    {
        LOG_WARN("GT_HostVehicleReporter::UpdateFromObjectState: egoState is null");
        return -1;
    }

    if (!config_.enabled)
    {
        return 0;  // Silently skip if not enabled
    }

    // Create HostVehicleData message
    osi3::HostVehicleData hv_data;
    
    int vehicle_id = egoState->id_;
    bool has_base = false;

    // 0. Init from Base Data if available
    if (input_cache_.count(vehicle_id) > 0)
    {
        auto& cache = input_cache_[vehicle_id];
        if (cache.has_base_data)
        {
            hv_data = cache.base_data;
            has_base = true;
        }
    }

    // Host vehicle identifier (global ID, consistent with GroundTruth)
    hv_data.mutable_host_vehicle_id()->set_value(egoState->g_id_);

    // 1. Timestamp
    auto* ts = hv_data.mutable_timestamp();
    double sim_time = SE_Env::Inst().GetOSITimeStamp() / 1.0e9;
    ts->set_seconds(static_cast<int64_t>(sim_time));
    ts->set_nanos(static_cast<int32_t>((sim_time - static_cast<int64_t>(sim_time)) * 1e9));

    // 2. Location (position, velocity, orientation) - Always OVERWRITE base data simulation state
    //
    // OSI 3.7.0 deprecates HostVehicleData.location (BaseMoving): position/orientation
    // moved to vehicle_localization, motion (velocity/acceleration) to vehicle_motion.
    // We emit the current recommended fields AND keep the deprecated `location` for
    // backward compatibility — `location` stays valid until the next OSI major, and
    // in-tree consumers (RealDriver HVD passthrough, DriverScript osi3 readers) still
    // read it. Once all consumers migrate, drop the `location` block below (audit F5).
    const double pos_x = egoState->pos_.GetX();
    const double pos_y = egoState->pos_.GetY();
    const double pos_z = egoState->pos_.GetZ();
    const double vel_x = egoState->pos_.GetVelX();
    const double vel_y = egoState->pos_.GetVelY();
    const double vel_z = egoState->pos_.GetVelZ();
    const double acc_x = egoState->pos_.GetAccX();
    const double acc_y = egoState->pos_.GetAccY();
    const double acc_z = egoState->pos_.GetAccZ();
    const double yaw   = egoState->pos_.GetH();
    const double pitch = egoState->pos_.GetP();
    const double roll  = egoState->pos_.GetR();

    // vehicle_motion velocity/acceleration are specified in the VEHICLE frame
    // (osi_hostvehicledata.proto VehicleMotion), unlike location /
    // vehicle_localization (global). When the controller supplied MEASURED
    // values via base_data (RealVehicleBackend model, ControllerRealDriver
    // external hardware), those are kept; otherwise the simulation state is
    // projected into the body frame below. Decide BEFORE any mutable_*() call
    // materializes an empty vehicle_motion. Writing the raw global vector here
    // was the G6 spec violation (frame mislabel).
    const bool keep_vm_vel = has_base && hv_data.has_vehicle_motion() && hv_data.vehicle_motion().has_velocity();
    const bool keep_vm_acc = has_base && hv_data.has_vehicle_motion() && hv_data.vehicle_motion().has_acceleration();

    // Deprecated (backward compatibility): HostVehicleData.location
    // BaseMoving is the parent (global) frame — acceleration included (G6: it
    // used to be left as the base_data body-frame value while everything else
    // here was global).
    auto* location = hv_data.mutable_location();
    location->mutable_position()->set_x(pos_x);
    location->mutable_position()->set_y(pos_y);
    location->mutable_position()->set_z(pos_z);
    location->mutable_velocity()->set_x(vel_x);
    location->mutable_velocity()->set_y(vel_y);
    location->mutable_velocity()->set_z(vel_z);
    location->mutable_acceleration()->set_x(acc_x);
    location->mutable_acceleration()->set_y(acc_y);
    location->mutable_acceleration()->set_z(acc_z);
    location->mutable_orientation()->set_yaw(yaw);
    location->mutable_orientation()->set_pitch(pitch);
    location->mutable_orientation()->set_roll(roll);

    // Current: vehicle_localization (position + orientation).
    auto* vloc = hv_data.mutable_vehicle_localization();
    vloc->mutable_position()->set_x(pos_x);
    vloc->mutable_position()->set_y(pos_y);
    vloc->mutable_position()->set_z(pos_z);
    vloc->mutable_orientation()->set_yaw(yaw);
    vloc->mutable_orientation()->set_pitch(pitch);
    vloc->mutable_orientation()->set_roll(roll);

    // Current: vehicle_motion. Position (global) and orientation stay
    // simulation-authoritative like location; velocity/acceleration are body
    // frame per spec — keep base-measured values, else project the simulation
    // state by yaw (planar rotation; the model itself is planar).
    auto* vmot = hv_data.mutable_vehicle_motion();
    vmot->mutable_position()->set_x(pos_x);
    vmot->mutable_position()->set_y(pos_y);
    vmot->mutable_position()->set_z(pos_z);
    vmot->mutable_orientation()->set_yaw(yaw);
    vmot->mutable_orientation()->set_pitch(pitch);
    vmot->mutable_orientation()->set_roll(roll);
    const double cos_yaw = std::cos(yaw);
    const double sin_yaw = std::sin(yaw);
    if (!keep_vm_vel)
    {
        vmot->mutable_velocity()->set_x(vel_x * cos_yaw + vel_y * sin_yaw);
        vmot->mutable_velocity()->set_y(-vel_x * sin_yaw + vel_y * cos_yaw);
        vmot->mutable_velocity()->set_z(vel_z);
    }
    if (!keep_vm_acc)
    {
        vmot->mutable_acceleration()->set_x(acc_x * cos_yaw + acc_y * sin_yaw);
        vmot->mutable_acceleration()->set_y(-acc_x * sin_yaw + acc_y * cos_yaw);
        vmot->mutable_acceleration()->set_z(acc_z);
    }

    // 2. Vehicle Basics (operating state)
    // If not set by base, set default
    if (!has_base)
    {
        auto* basics = hv_data.mutable_vehicle_basics();
        basics->set_operating_state(osi3::HostVehicleData_VehicleBasics_OperatingState_OPERATING_STATE_DRIVING);
    }

    // 3. Vehicle inputs (steering, throttle, brake, gear)
    // GT_Step always populates input_cache_ via SetInputs/SetPowertrain for ALL
    // controller types (DefaultController estimator, RealDriver, PythonDriver).
    // Always apply these overrides so the HVD message reflects the latest values.
    if (input_cache_.count(vehicle_id) > 0)
    {
        auto& input = input_cache_[vehicle_id];

        // Steering (OSI v10: vehicle_steering -> vehicle_steering_wheel -> angle)
        auto* steering = hv_data.mutable_vehicle_steering();
        steering->mutable_vehicle_steering_wheel()->set_angle(input.steering_angle);  // radians

        // Powertrain (throttle, gear)
        auto* powertrain = hv_data.mutable_vehicle_powertrain();
        powertrain->set_pedal_position_acceleration(input.throttle);  // [0, 1]
        powertrain->set_gear_transmission(input.gear);  // gear value

        // Brake (OSI v10: separate brake_system)
        auto* brake_system = hv_data.mutable_vehicle_brake_system();
        brake_system->set_pedal_position_brake(input.brake);  // [0, 1]

        // Engine/Motor data (OSI v10: motor is repeated field)
        // Clear existing motors first (base_data may have stale entries)
        powertrain->clear_motor();
        if (input.rpm > 0.0 || input.torque != 0.0)
        {
            auto* motor = powertrain->add_motor();
            motor->set_type(osi3::HostVehicleData_VehiclePowertrain_Motor_Type_TYPE_OTTO);  // Default to gasoline
            motor->set_rpm(input.rpm);
            motor->set_torque(input.torque);  // Nm
        }
    }

    // 4. ADAS functions
    // GT_Step populates ADAS via AddADASFunctionEx for all controller types.
    // Always apply input_cache_ ADAS functions (replaces any from base_data).
    if (input_cache_.count(vehicle_id) > 0)
    {
        auto& input = input_cache_[vehicle_id];
        if (!input.adas_functions.empty())
        {
            hv_data.clear_vehicle_automated_driving_function();
            for (const auto& func : input.adas_functions)
            {
                auto* adas_func = hv_data.add_vehicle_automated_driving_function();
                // W1: use the real OSI Name when the caller knows one
                // (AddADASFunctionEx); fall back to NAME_OTHER for the legacy
                // label-only path, which is what every caller used before.
                adas_func->set_name(
                    func.osi_name >= 0
                        ? static_cast<osi3::HostVehicleData_VehicleAutomatedDrivingFunction_Name>(func.osi_name)
                        : osi3::HostVehicleData_VehicleAutomatedDrivingFunction_Name_NAME_OTHER);
                adas_func->set_custom_name(func.name);
                adas_func->set_state(static_cast<osi3::HostVehicleData_VehicleAutomatedDrivingFunction_State>(func.state));
                // Numeric internals behind the state (§2.2 (a'), W3): TTC, required
                // decel, ... under the gt.* key convention (PolicyDetail.hpp).
                for (const auto& kv : func.detail)
                {
                    auto* pair = adas_func->add_custom_detail();
                    pair->set_key(kv.first);
                    pair->set_value(kv.second);
                }

                // req-vd-ad:REQ-AD-028 段b (design §8-3). BOTH fields are
                // written only when the producer actually has something to
                // say (see AdasFunctionOverride's doc comment): a
                // default-constructed override leaves the row's serialized
                // bytes exactly as they were before phase B, which is what
                // makes this change a no-op for the RealDriver 24-slot rows
                // and the VirtualDriver rows.
                //
                // set_active() is called EXPLICITLY even for false: OSI's
                // `active` is an `optional bool`, so an explicit false is
                // present-on-the-wire and readable as "evaluated, no
                // override", distinct from the absent submessage that means
                // "never evaluated".
                if (func.driver_override.reported)
                {
                    auto* ovr = adas_func->mutable_driver_override();
                    ovr->set_active(func.driver_override.active);
                    for (int reason : func.driver_override.reasons)
                    {
                        ovr->add_override_reason(
                            static_cast<osi3::HostVehicleData_VehicleAutomatedDrivingFunction_DriverOverride_Reason>(
                                reason));
                    }
                }
                if (!func.driver_override.custom_state.empty())
                {
                    adas_func->set_custom_state(func.driver_override.custom_state);
                }
            }
        }
    }

    // 5. Serialize
    serialized_data_.data.clear();
    hv_data.SerializeToString(&serialized_data_.data);
    serialized_data_.size = static_cast<unsigned int>(serialized_data_.data.size());

    return 0;
}

void GT_HostVehicleReporter::Send()
{
    if (!udp_client_ || serialized_data_.size == 0)
    {
        return;
    }

    if (!config_.enabled)
    {
        return;
    }

    // Send HostVehicleData via UDP
    if (serialized_data_.size <= MAX_UDP_DATA_SIZE)
    {
        // Small message: send in single packet
        udp_buf_.counter = 0;
        udp_buf_.datasize = serialized_data_.size;
        memcpy(udp_buf_.data,
               serialized_data_.data.data(),
               serialized_data_.size);

        int bytes_sent = udp_client_->Send(
            reinterpret_cast<char*>(&udp_buf_),
            sizeof(udp_buf_.counter) + sizeof(udp_buf_.datasize) + serialized_data_.size);

        if (bytes_sent < 0)
        {
            LOG_ERROR("GT_HostVehicleReporter: Failed to send HostVehicleData via UDP");
        }
        else
        {
             // [DEBUG] Log successful send (maybe throttle this log if too frequent)
             static int log_counter = 0;
             if (log_counter++ % 100 == 0) LOG_INFO("GT_HostVehicleReporter: Sent UDP packet bytes={}", bytes_sent);
        }
    }
    else
    {
        // Large message: would need splitting
        LOG_WARN("GT_HostVehicleReporter: Data size ({}) exceeds max UDP size ({}), splitting not yet implemented",
                 serialized_data_.size, MAX_UDP_DATA_SIZE);
    }
}

int GT_HostVehicleReporter::GetUDPClientStatus() const
{
    return (udp_client_ ? udp_client_->GetStatus() : -1);
}

const char* GT_HostVehicleReporter::GetSerializedHostVehicleData(int* size) const
{
    // Guard a null size, matching the header's documented contract.
    if (size)
    {
        *size = static_cast<int>(serialized_data_.size);
    }

    if (serialized_data_.data.empty())
    {
        return nullptr;
    }

    return serialized_data_.data.data();
}

} // namespace gt_esmini
