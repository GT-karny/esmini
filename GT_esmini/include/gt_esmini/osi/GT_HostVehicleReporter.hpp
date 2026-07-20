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

#pragma once

#include "UDP.hpp"
// #include "ScenarioGateway.hpp" // removed in v3.0.0
#include "gt_esmini/osi/IHostVehicleDataProvider.hpp"
#include <string>
#include <vector>
#include <map>
#include "osi_hostvehicledata.pb.h"

namespace gt_esmini
{

/**
 * @brief Singleton class for managing HostVehicleData generation and UDP transmission
 *
 * This class handles OSI HostVehicleData independently from OSIReporter,
 * allowing for clean separation of GT_esmini extensions from core esmini code.
 */
class GT_HostVehicleReporter : public IHostVehicleDataProvider
{
public:
    /**
     * Get singleton instance
     */
    static GT_HostVehicleReporter& Instance();

    /**
     * Initialize HostVehicleData reporter
     * @param udp_port UDP port for transmission (default: 48199)
     * @param config_file Path to configuration JSON file
     * @param target_ip Target IP address override. Empty means use config/default.
     */
    void Init(int udp_port = 48199, const std::string& config_file = "", const std::string& target_ip = "");

    /**
     * Load configuration from JSON file
     * @param config_file Path to config file
     */
    void LoadConfig(const std::string& config_file);

    /**
     * Set base HostVehicleData (from ControllerRealDriver for example)
     * This allows external sources to provide the full HVD structure, which 
     * will then be updated with simulation specific data (position, speed) before sending.
     * @param vehicle_id Vehicle ID
     * @param data The base HostVehicleData
     */
    void SetBaseHostVehicleData(int vehicle_id, const osi3::HostVehicleData& data);

    /**
     * Set host vehicle control inputs
     * @param vehicle_id Vehicle ID
     * @param throttle Throttle input [0, 1]
     * @param brake Brake input [0, 1]
     * @param steering Steering input (will be multiplied by ratio from config)
     * @param gear Gear (-1=Reverse, 0=Neutral, 1=Drive)
     */
    void SetInputs(int vehicle_id, double throttle, double brake, double steering, int gear);

    /**
     * Set host vehicle light state
     * @param vehicle_id Vehicle ID
     * @param light_mask Light state bitmask
     */
    void SetLights(int vehicle_id, int light_mask);

    /**
     * Set host vehicle powertrain data
     * @param vehicle_id Vehicle ID
     * @param rpm Engine RPM
     * @param torque Engine torque (Nm)
     */
    void SetPowertrain(int vehicle_id, double rpm, double torque);

    /**
     * Add or update ADAS function state
     * @param vehicle_id Vehicle ID
     * @param function_name OSI function name (e.g., "ADAPTIVE_CRUISE_CONTROL")
     * @param state OSI function state (0-6)
     */
    void AddADASFunction(int vehicle_id, const std::string& function_name, int state);

    /**
     * Add or update one ADAS function using the OSI Name enum (W1).
     *
     * AddADASFunction() above always lands as NAME_OTHER + custom_name, because
     * its callers only have a label. This overload carries the real OSI
     * `Name` enum value plus the custom_detail KVs, which is what makes the
     * function machine-identifiable to any OSI consumer instead of merely
     * human-readable. Both paths coexist: ControllerRealDriver / PythonDriver
     * keep the fixed-24-slot label path unchanged.
     *
     * @param vehicle_id  Vehicle ID
     * @param osi_name    osi3 ...VehicleAutomatedDrivingFunction_Name value
     * @param custom_name Label (always set; the only identity for NAME_OTHER)
     * @param state       osi3 ...VehicleAutomatedDrivingFunction_State value
     * @param detail      custom_detail key/value pairs (see PolicyDetail.hpp)
     */
    void AddADASFunctionEx(int                                                     vehicle_id,
                           int                                                     osi_name,
                           const std::string&                                      custom_name,
                           int                                                     state,
                           const std::vector<std::pair<std::string, std::string>>& detail);

    /**
     * Clear all ADAS functions for a vehicle (call before updating each frame)
     * @param vehicle_id Vehicle ID
     */
    void ClearADASFunctions(int vehicle_id);

    /**
     * Update HostVehicleData from ObjectState
     * @param egoState Pointer to ego vehicle ObjectState
     * @return 0 on success, -1 on error
     */
    int UpdateFromObjectState(const scenarioengine::Object* egoObj) override;

    /**
     * Send HostVehicleData via UDP
     */
    void Send() override;

    /**
     * Check if initialized
     * @return true if initialized
     */
    bool IsInitialized() const { return initialized_; }

    /**
     * Get target vehicle name for HVD output.
     * Empty string means use default (index 0).
     */
    const std::string& GetTargetVehicle() const { return config_.target_vehicle; }

    /**
     * Get UDP client status
     * @return 0 if OK, -1 if not available
     */
    int GetUDPClientStatus() const;

    /**
     * Cleanup resources
     */
    void Close();

private:
    GT_HostVehicleReporter();
    ~GT_HostVehicleReporter();

    // Prevent copying
    GT_HostVehicleReporter(const GT_HostVehicleReporter&) = delete;
    GT_HostVehicleReporter& operator=(const GT_HostVehicleReporter&) = delete;

    // UDP client for HostVehicleData transmission
    UDPClient* udp_client_ = nullptr;

    // Initialization flag
    bool initialized_ = false;

    // Configuration
    struct Config
    {
        double steering_input_to_wheel_ratio = 1.0;
        int udp_port = 48199;
        std::string target_ip = "127.0.0.1";
        bool enabled = true;
        std::string target_vehicle = "";  // Object name for HVD target; empty = index 0 (default)
    } config_;

    // Input cache per vehicle
    struct InputCache
    {
        double throttle = 0.0;
        double brake = 0.0;
        double steering_angle = 0.0;  // in radians (after applying ratio)
        int gear = 1;
        int light_mask = 0;
        double rpm = 0.0;
        double torque = 0.0;

        struct ADASFunction
        {
            std::string name;
            int state;
            // W1: -1 means "no OSI Name known" -> emitted as NAME_OTHER, which is
            // exactly what the legacy AddADASFunction() label path always did.
            int osi_name = -1;
            std::vector<std::pair<std::string, std::string>> detail;  // -> custom_detail
        };
        std::vector<ADASFunction> adas_functions;

        // Base Data provided by Controller (optional)
        bool has_base_data = false;
        osi3::HostVehicleData base_data;
    };

    std::map<int, InputCache> input_cache_;

    // Serialized data buffer
    struct
    {
        std::string data;
        unsigned int size = 0;
    } serialized_data_;

    // UDP buffer for transmission
    static constexpr int MAX_UDP_DATA_SIZE = 8192;
    struct
    {
        int counter = 0;
        unsigned int datasize = 0;
        char data[MAX_UDP_DATA_SIZE];
    } udp_buf_;
};

} // namespace gt_esmini
