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

#include "GT_HostVehicleReporter.hpp"
#include "CommonMini.hpp"
#include "osi_hostvehicledata.pb.h"

#include <fstream>
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
    Close();
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
    
    // Determine target IP:
    // If argument is provided (not empty), it takes precedence over config and main default.
    std::string final_ip = config_.target_ip; // Start with config (default is "127.0.0.1")
    
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
    std::ifstream file(config_file);
    if (!file.is_open())
    {
        LOG_INFO("GT_HostVehicleReporter: Config file not found, using defaults: {}", config_file);
        return;
    }

    std::string line;

    auto parse_val = [&](const std::string& key, double& val)
    {
        if (line.find(key) != std::string::npos)
        {
            size_t colon = line.find(":");
            if (colon != std::string::npos)
            {
                try
                {
                    std::string value_str = line.substr(colon + 1);
                    size_t comma = value_str.find(",");
                    if (comma != std::string::npos)
                    {
                        value_str = value_str.substr(0, comma);
                    }
                    val = std::stod(value_str);
                }
                catch (...)
                {
                    LOG_WARN("GT_HostVehicleReporter: Failed to parse {} from config", key);
                }
            }
        }
    };

    auto parse_int = [&](const std::string& key, int& val)
    {
        if (line.find(key) != std::string::npos)
        {
            size_t colon = line.find(":");
            if (colon != std::string::npos)
            {
                try
                {
                    std::string value_str = line.substr(colon + 1);
                    size_t comma = value_str.find(",");
                    if (comma != std::string::npos)
                    {
                        value_str = value_str.substr(0, comma);
                    }
                    val = std::stoi(value_str);
                }
                catch (...)
                {
                    LOG_WARN("GT_HostVehicleReporter: Failed to parse {} from config", key);
                }
            }
        }
    };

    auto parse_bool = [&](const std::string& key, bool& val)
    {
        if (line.find(key) != std::string::npos)
        {
            if (line.find("true") != std::string::npos)
            {
                val = true;
            }
            else if (line.find("false") != std::string::npos)
            {
                val = false;
            }
        }
    };
    
    auto parse_str = [&](const std::string& key, std::string& val)
    {
        if (line.find(key) != std::string::npos)
        {
            size_t colon = line.find(":");
            if (colon != std::string::npos)
            {
                std::string value_str = line.substr(colon + 1);
                // Remove trailing comma if exists
                size_t comma = value_str.find(",");
                if (comma != std::string::npos)
                {
                    value_str = value_str.substr(0, comma);
                }
                
                // Trim quotes and whitespace
                // Simple trim
                size_t first = value_str.find_first_not_of(" \t\"");
                size_t last = value_str.find_last_not_of(" \t\"");
                if (first != std::string::npos && last != std::string::npos)
                {
                    val = value_str.substr(first, last - first + 1);
                }
            }
        }
    };

    while (std::getline(file, line))
    {
        parse_val("steering_input_to_wheel_ratio", config_.steering_input_to_wheel_ratio);
        parse_int("udp_port", config_.udp_port);
        parse_bool("enable_host_vehicle_data", config_.enabled);
        parse_str("target_ip", config_.target_ip);
    }

    LOG_INFO("GT_HostVehicleReporter: Config loaded: steering_ratio={}, udp_port={}, target_ip={}, enabled={}",
             config_.steering_input_to_wheel_ratio,
             config_.udp_port,
             config_.target_ip,
             config_.enabled);
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

void GT_HostVehicleReporter::SetPowertrain(int vehicle_id, double rpm, double torque)
{
    auto& cache = input_cache_[vehicle_id];
    cache.rpm = rpm;
    cache.torque = torque;
}

void GT_HostVehicleReporter::AddADASFunction(int vehicle_id, const std::string& function_name, bool is_enabled, bool is_available)
{
    auto& cache = input_cache_[vehicle_id];

    // Update existing or add new
    bool found = false;
    for (auto& func : cache.adas_functions)
    {
        if (func.name == function_name)
        {
            func.is_enabled = is_enabled;
            func.is_available = is_available;
            found = true;
            break;
        }
    }

    if (!found)
    {
        InputCache::ADASFunction func;
        func.name = function_name;
        func.is_enabled = is_enabled;
        func.is_available = is_available;
        cache.adas_functions.push_back(func);
    }
}

void GT_HostVehicleReporter::ClearADASFunctions(int vehicle_id)
{
    if (input_cache_.count(vehicle_id) > 0)
    {
        input_cache_[vehicle_id].adas_functions.clear();
    }
}

int GT_HostVehicleReporter::UpdateFromObjectState(const scenarioengine::ObjectState* egoState)
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

    int vehicle_id = egoState->state_.info.g_id;

    // 1. Location (position, velocity, orientation)
    auto* location = hv_data.mutable_location();

    location->mutable_position()->set_x(egoState->state_.pos.GetX());
    location->mutable_position()->set_y(egoState->state_.pos.GetY());
    location->mutable_position()->set_z(egoState->state_.pos.GetZ());

    location->mutable_velocity()->set_x(egoState->state_.pos.GetVelX());
    location->mutable_velocity()->set_y(egoState->state_.pos.GetVelY());
    location->mutable_velocity()->set_z(egoState->state_.pos.GetVelZ());

    location->mutable_orientation()->set_yaw(egoState->state_.pos.GetH());
    location->mutable_orientation()->set_pitch(egoState->state_.pos.GetP());
    location->mutable_orientation()->set_roll(egoState->state_.pos.GetR());

    // 2. Vehicle Basics (operating state)
    auto* basics = hv_data.mutable_vehicle_basics();
    basics->set_operating_state(osi3::HostVehicleData_VehicleBasics_OperatingState_OPERATING_STATE_DRIVING);

    // 3. Vehicle inputs (steering, throttle, brake, gear)
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
        if (input.rpm > 0.0 || input.torque != 0.0)
        {
            auto* motor = powertrain->add_motor();
            motor->set_type(osi3::HostVehicleData_VehiclePowertrain_Motor_Type_TYPE_OTTO);  // Default to gasoline
            motor->set_rpm(input.rpm);
            motor->set_torque(input.torque);  // Nm
        }
    }

    // 4. ADAS functions
    if (input_cache_.count(vehicle_id) > 0)
    {
        auto& input = input_cache_[vehicle_id];
        for (const auto& func : input.adas_functions)
        {
            auto* adas_func = hv_data.add_vehicle_automated_driving_function();

            // Map function name to OSI enum
            // For now, use NAME_OTHER and store custom name
            adas_func->set_name(osi3::HostVehicleData_VehicleAutomatedDrivingFunction_Name_NAME_OTHER);
            adas_func->set_custom_name(func.name);

            // Set state based on enabled/available
            if (func.is_enabled)
            {
                adas_func->set_state(osi3::HostVehicleData_VehicleAutomatedDrivingFunction_State_STATE_ACTIVE);
            }
            else if (func.is_available)
            {
                adas_func->set_state(osi3::HostVehicleData_VehicleAutomatedDrivingFunction_State_STATE_AVAILABLE);
            }
            else
            {
                adas_func->set_state(osi3::HostVehicleData_VehicleAutomatedDrivingFunction_State_STATE_UNAVAILABLE);
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

} // namespace gt_esmini
