//
// Copyright 2016 -- 2018 PMSF IT Consulting Pierre R. Mai
// Copyright 2023 BMW AG
// SPDX-License-Identifier: MPL-2.0
//

#include "esminiLib.hpp"
// [GT_MOD] START: Include GT extension
#include "GT_esminiLib.hpp"
// [GT_MOD] END

#include "osi_common.pb.h"
#include "osi_object.pb.h"
#include "osi_groundtruth.pb.h"
#include "osi_trafficupdate.pb.h"

#include "esmini.h"

/*
 * Debug Breaks
 *
 * If you define DEBUG_BREAKS the FMU will automatically break
 * into an attached Debugger on all major computation functions.
 * Note that the FMU is likely to break all environments if no
 * Debugger is actually attached when the breaks are triggered.
 */
#if defined(DEBUG_BREAKS) && !defined(NDEBUG)
#if defined(__has_builtin) && !defined(__ibmxl__)
#if __has_builtin(__builtin_debugtrap)
#define DEBUGBREAK() __builtin_debugtrap()
#elif __has_builtin(__debugbreak)
#define DEBUGBREAK() __debugbreak()
#endif
#endif
#if !defined(DEBUGBREAK)
#if defined(_MSC_VER) || defined(__INTEL_COMPILER)
#include <intrin.h>
#define DEBUGBREAK() __debugbreak()
#else
#include <signal.h>
#if defined(SIGTRAP)
#define DEBUGBREAK() raise(SIGTRAP)
#else
#define DEBUGBREAK() raise(SIGABRT)
#endif
#endif
#endif
#else
#define DEBUGBREAK()
#endif

#include <iostream>
#include <string>
#include <algorithm>
#include <cstdint>
#include <cmath>

using namespace std;
#include <sstream>

#ifdef PRIVATE_LOG_PATH
ofstream COSMPDummySource::private_log_file;
#endif

/*
 * ProtocolBuffer Accessors
 */

void* decode_integer_to_pointer(fmi2Integer hi, fmi2Integer lo)
{
#if PTRDIFF_MAX == INT64_MAX
    union addrconv
    {
        struct
        {
            int lo;
            int hi;
        } base;
        unsigned long long address;
    } myaddr;
    myaddr.base.lo = lo;
    myaddr.base.hi = hi;
    return reinterpret_cast<void*>(myaddr.address);
#elif PTRDIFF_MAX == INT32_MAX
    return reinterpret_cast<void*>(lo);
#else
#error "Cannot determine 32bit or 64bit environment!"
#endif
}

void encode_pointer_to_integer(const void* ptr, fmi2Integer& hi, fmi2Integer& lo)
{
#if PTRDIFF_MAX == INT64_MAX
    union addrconv
    {
        struct
        {
            int lo;
            int hi;
        } base;
        unsigned long long address;
    } myaddr;
    myaddr.address = reinterpret_cast<unsigned long long>(ptr);
    hi             = myaddr.base.hi;
    lo             = myaddr.base.lo;
#elif PTRDIFF_MAX == INT32_MAX
    hi = 0;
    lo = reinterpret_cast<int>(ptr);
#else
#error "Cannot determine 32bit or 64bit environment!"
#endif
}

bool EsminiOsiSource::get_fmi_traffic_update_in(osi3::TrafficUpdate& data)
{
    if (integer_vars[FMI_INTEGER_TRAFFICUPDATE_IN_SIZE_IDX] > 0)
    {
        void* buffer =
            decode_integer_to_pointer(integer_vars[FMI_INTEGER_TRAFFICUPDATE_IN_BASEHI_IDX], integer_vars[FMI_INTEGER_TRAFFICUPDATE_IN_BASELO_IDX]);
        normal_log("OSMP",
                   "Got %08X %08X, reading from %p ...",
                   integer_vars[FMI_INTEGER_TRAFFICUPDATE_IN_BASEHI_IDX],
                   integer_vars[FMI_INTEGER_TRAFFICUPDATE_IN_BASELO_IDX],
                   buffer);
        data.ParseFromArray(buffer, integer_vars[FMI_INTEGER_TRAFFICUPDATE_IN_SIZE_IDX]);
        return true;
    }
    else
    {
        return false;
    }
}

void EsminiOsiSource::set_fmi_sensor_view_out(const osi3::SensorView& data)
{
    data.SerializeToString(currentBufferSVOut);
    encode_pointer_to_integer(currentBufferSVOut->data(),
                              integer_vars[FMI_INTEGER_SENSORVIEW_OUT_BASEHI_IDX],
                              integer_vars[FMI_INTEGER_SENSORVIEW_OUT_BASELO_IDX]);
    integer_vars[FMI_INTEGER_SENSORVIEW_OUT_SIZE_IDX] = (fmi2Integer)currentBufferSVOut->length();
    normal_log("OSMP",
               "Providing %08X %08X, writing from %p ...",
               integer_vars[FMI_INTEGER_SENSORVIEW_OUT_BASEHI_IDX],
               integer_vars[FMI_INTEGER_SENSORVIEW_OUT_BASELO_IDX],
               currentBufferSVOut->data());
    swap(currentBufferSVOut, lastBufferSVOut);
}

void EsminiOsiSource::reset_fmi_sensor_view_out()
{
    integer_vars[FMI_INTEGER_SENSORVIEW_OUT_SIZE_IDX]   = 0;
    integer_vars[FMI_INTEGER_SENSORVIEW_OUT_BASEHI_IDX] = 0;
    integer_vars[FMI_INTEGER_SENSORVIEW_OUT_BASELO_IDX] = 0;
}

void EsminiOsiSource::set_fmi_traffic_command_out(const osi3::TrafficCommand& data)
{
    data.SerializeToString(currentBufferTCOut);
    encode_pointer_to_integer(currentBufferTCOut->data(),
                              integer_vars[FMI_INTEGER_TRAFFICCOMMAND_OUT_BASEHI_IDX],
                              integer_vars[FMI_INTEGER_TRAFFICCOMMAND_OUT_BASELO_IDX]);
    integer_vars[FMI_INTEGER_TRAFFICCOMMAND_OUT_SIZE_IDX] = (fmi2Integer)currentBufferTCOut->length();
    normal_log("OSMP",
               "Providing %08X %08X, writing from %p ...",
               integer_vars[FMI_INTEGER_TRAFFICCOMMAND_OUT_BASEHI_IDX],
               integer_vars[FMI_INTEGER_TRAFFICCOMMAND_OUT_BASELO_IDX],
               currentBufferTCOut->data());
    swap(currentBufferTCOut, lastBufferTCOut);
}

void EsminiOsiSource::reset_fmi_traffic_command_out()
{
    integer_vars[FMI_INTEGER_TRAFFICCOMMAND_OUT_SIZE_IDX]   = 0;
    integer_vars[FMI_INTEGER_TRAFFICCOMMAND_OUT_BASEHI_IDX] = 0;
    integer_vars[FMI_INTEGER_TRAFFICCOMMAND_OUT_BASELO_IDX] = 0;
}

/*
 * Actual Core Content
 */

void EsminiOsiSource::update_osmp_output(double time)
{
    normal_log("OSMP", "update_osmp_output called for time: %f", time);
    const void* raw_gt = SE_GetOSIGroundTruthRaw();
    normal_log("OSMP", "SE_GetOSIGroundTruthRaw returned: %p", raw_gt);

    osi3::SensorView currentOut;
    currentOut.Clear();
    currentOut.mutable_sensor_id()->set_value(0);

    if (raw_gt != nullptr)
    {
        // [GT_MOD] DIAGNOSTIC: Validate pointer somewhat (basic check)
        // In a real scenario we can't easily validate a raw pointer, but we can check if it looks like a valid address (not small int)
        // For now, trusting standard nullptr check.
        
        const auto* se_osi_ground_truth = reinterpret_cast<const osi3::GroundTruth*>(raw_gt);  // Fetch OSI struct
        if (se_osi_ground_truth->has_host_vehicle_id())
        {
            currentOut.mutable_host_vehicle_id()->set_value(se_osi_ground_truth->host_vehicle_id().value());
        }
        
        osi3::GroundTruth* currentGT = currentOut.mutable_global_ground_truth();
        currentGT->CopyFrom(*se_osi_ground_truth);
    }
    else
    {
        // [GT_MOD] DIAGNOSTIC LOG
        std::cerr << "[OSMP] ERROR: SE_GetOSIGroundTruthRaw returned nullptr at time " << time << std::endl;
        normal_log("OSMP", "Warning: No ground truth available at time %f. Creating empty SensorView.", time);
    }
    
    currentOut.mutable_timestamp()->set_seconds((long long int)floor(time));
    const double sec_to_nanos = 1000000000.0;
    currentOut.mutable_timestamp()->set_nanos((int)((time - floor(time)) * sec_to_nanos));
    
    set_fmi_sensor_view_out(currentOut);

    // Handle OSI TrafficCommand output
    const void* raw_tc = SE_GetOSITrafficCommandRaw();
    if (raw_tc != nullptr)
    {
        const auto* traffic_command = reinterpret_cast<const osi3::TrafficCommand*>(raw_tc);  // Fetch OSI struct (via pointer, no copying of data)
        set_fmi_traffic_command_out(*traffic_command);
    }
    else
    {
         // Reset output or just log? For now just log if verbose, but standard flow might expect valid pointer update or keeping previous.
         // If we don't call set_fmi_traffic_command_out, the indices remain what they were (potentially pointing to old buffer).
         // Given standard usage, we might want to minimally ensure valid state if needed, but FMI requires us to update if we say we do.
         // However, if null, there is no command.
    }

    set_fmi_valid(1);
}

fmi2Status EsminiOsiSource::doInit()
{
    DEBUGBREAK();

    /* Booleans */
    for (int i = 0; i < FMI_BOOLEAN_VARS; i++)
        boolean_vars[i] = fmi2False;

    /* Integers */
    for (int i = 0; i < FMI_INTEGER_VARS; i++)
        integer_vars[i] = 0;

    /* Reals */
    for (int i = 0; i < FMI_REAL_VARS; i++)
        real_vars[i] = 0.0;

    /* Strings */
    for (int i = 0; i < FMI_STRING_VARS; i++)
        string_vars[i] = "";

    // [GT_MOD] Load Parameter Mapping
    parameter_map_.clear();
    std::string param_file = "fmu_parameters.txt";
    // Check local directory matching xosc path or resource location might be better, 
    // but for now check current working directory (simple).
    std::ifstream file(param_file);
    if (file.is_open()) {
        std::string line;
        while (std::getline(file, line)) {
            // Simple parsing: ID, Name
            std::stringstream ss(line);
            std::string segment;
            std::vector<std::string> seglist;
            while(std::getline(ss, segment, ',')) {
                 seglist.push_back(segment);
            }
            if(seglist.size() >= 2) {
                try {
                    int vr = std::stoi(seglist[0]);
                    std::string name = seglist[1];
                    // Trim whitespace
                    size_t first = name.find_first_not_of(" \t\n\r");
                    if (first != std::string::npos) {
                        size_t last = name.find_last_not_of(" \t\n\r");
                        name = name.substr(first, (last - first + 1));
                    }
                    
                    parameter_map_[vr] = name;
                    normal_log("OSMP", "Mapped VR %d to Parameter '%s'", vr, name.c_str());
                } catch (...) {
                     normal_log("OSMP", "Failed to parse line in fmu_parameters.txt: %s", line.c_str());
                }
            }
        }
        file.close();
    } else {
         normal_log("OSMP", "No fmu_parameters.txt found. Generic parameter mapping skipped.");
    }
    // [GT_MOD] END

    return fmi2OK;
}

fmi2Status EsminiOsiSource::doStart(fmi2Boolean toleranceDefined,
                                    fmi2Real    tolerance,
                                    fmi2Real    startTime,
                                    fmi2Boolean stopTimeDefined,
                                    fmi2Real    stopTime)
{
    DEBUGBREAK();

    fmiSlaveTerminated = false;
    m_startTime = startTime;

    return fmi2OK;
}

fmi2Status EsminiOsiSource::doEnterInitializationMode()
{
    DEBUGBREAK();

    return fmi2OK;
}

fmi2Status EsminiOsiSource::doExitInitializationMode()
{
    DEBUGBREAK();
    normal_log("OSMP", "doExitInitializationMode called");

    std::string esmini_args = fmi_esmini_args();
    if (esmini_args.empty())
    {
        const std::string xosc_path = fmi_xosc_path();
        if (xosc_path.empty())
        {
            std::cerr << "No OpenScenario file selected!" << std::endl;
            return fmi2Error;
        }
        // [GT_MOD] START: Use GT_Init to support extended actions
        if (GT_Init(xosc_path.c_str(), 0) != 0)
        {
            std::cerr << "Failed to initialize the scenario (GT_Init)" << std::endl;
            normal_log("OSMP", "GT_Init failed");
            return fmi2Error;
        }
        else
        {
            normal_log("OSMP", "GT_Init succeeded");
        }
        // [GT_MOD] END
    }
    else
    {
        std::vector<std::string> args = {"esmini(lib)"};
        size_t start = 0, end = 0;
        bool hasOsc = false;
        
        // [GT_MOD] OSI UDP Support: Variable to capture OSI IP
        std::string osiIp;
        
        while ((end = esmini_args.find(' ', start)) != size_t(-1))
        {
            std::string arg = esmini_args.substr(start, end - start);
            if (!arg.empty()) {
                if (arg == "--osc") hasOsc = true;
                
                // [GT_MOD] OSI UDP Support: Check for --osi argument
                if (arg == "--osi") {
                    // Get next token as IP address
                    size_t nextStart = end + 1;
                    size_t nextEnd = esmini_args.find(' ', nextStart);
                    if (nextEnd == size_t(-1)) nextEnd = esmini_args.length();
                    osiIp = esmini_args.substr(nextStart, nextEnd - nextStart);
                    normal_log("OSMP", "Captured OSI IP: %s", osiIp.c_str());
                }
                
                args.push_back(arg);
            }
            start = end + 1;
        }
        std::string lastArg = esmini_args.substr(start);
        if (!lastArg.empty()) {
            if (lastArg == "--osc") hasOsc = true;
            args.push_back(lastArg);
        }

        // [GT_MOD] START: Fallback to xosc_path if --osc is missing
        if (!hasOsc) {
            std::string xosc_path = fmi_xosc_path();
            if (!xosc_path.empty()) {
                normal_log("OSMP", "Adding missing --osc argument from xosc_path: %s", xosc_path.c_str());
                args.push_back("--osc");
                args.push_back(xosc_path);
            } else {
                normal_log("OSMP", "Warning: --osc missing in args and xosc_path is empty.");
            }
        }
        // [GT_MOD] END

        // [GT_MOD] START: Use GT_InitWithArgs and fix VLA for Windows/MSVC
        int argc = static_cast<int>(args.size());
        std::vector<const char*> argvVector(argc);
        for (int i = 0; i < argc; i++)
        {
            argvVector[i] = args.at(i).c_str();
        }
        
        // Log the final arguments for debugging
        std::string debugArgs;
        for(const auto* arg : argvVector) debugArgs += std::string(arg) + " ";
        normal_log("OSMP", "Initializing with args: %s", debugArgs.c_str());


        // [GT_MOD] START: Initialize the scenario

        std::cerr << "[FMU] BEFORE GT_InitWithArgs" << std::endl;
        int rc = GT_InitWithArgs(argc, argvVector.data());
        std::cerr << "[FMU] AFTER GT_InitWithArgs rc=" << rc << std::endl;

        if (rc != 0)
        {
            std::cerr << "Failed to initialize the scenario (GT_InitWithArgs)" << std::endl;
            normal_log("OSMP", "GT_InitWithArgs failed");
            return fmi2Error;
        }
        else
        {
            normal_log("OSMP", "GT_InitWithArgs succeeded");
            
            // [GT_MOD] OSI UDP Support: Open OSI socket if requested
            if (!osiIp.empty()) {
                normal_log("OSMP", "Opening OSI socket to: %s", osiIp.c_str());
                SE_OpenOSISocket(osiIp.c_str());
            }
        }
        
        // [GT_MOD] DIAGNOSTIC: Check QuitFlag immediately after Init
        int postInitQuit = SE_GetQuitFlag();
        std::cout << "[OSMP] Post-Init QuitFlag: " << postInitQuit << std::endl;
        if (postInitQuit) {
             std::cerr << "[OSMP] CRITICAL: QuitFlag set immediately after GT_InitWithArgs!" << std::endl;
        }

        // [GT_MOD] END
    }

    // [GT_MOD] START: Output initial OSI data (T=0)
    normal_log("OSMP", "Setting OSI Static Report Mode for T=0 output");
    SE_SetOSIStaticReportMode(SE_OSIStaticReportMode::API);
    
    // [GT_MOD] DIAGNOSTIC
    int postModeQuit = SE_GetQuitFlag();
    std::cout << "[OSMP] Post-SetMode QuitFlag: " << postModeQuit << std::endl;

    update_osmp_output(m_startTime);
    
    // [GT_MOD] DIAGNOSTIC
    int postUpdateQuit = SE_GetQuitFlag();
    std::cout << "[OSMP] Post-Update QuitFlag: " << postUpdateQuit << std::endl;
    // [GT_MOD] END

    normal_log("OSMP", "doExitInitializationMode completed");
    return fmi2OK;
}

fmi2Status EsminiOsiSource::doCalc(fmi2Real currentCommunicationPoint, fmi2Real communicationStepSize, fmi2Boolean noSetFMUStatePriorToCurrentPoint)
{
    DEBUGBREAK();
    // [GT_MOD] DIAGNOSTIC LOG
    // Check if we are already in a quit state before we even start
    int preQuitFlag = SE_GetQuitFlag();
    if (preQuitFlag != 0) {
        std::cerr << "[OSMP] CRITICAL: doCalc envoked but SE_GetQuitFlag is ALREADY set (" << preQuitFlag << "). Previous error likely." << std::endl;
    }
    normal_log("OSMP", "doCalc called. Time: %f, Step: %f, PreQuit: %d", currentCommunicationPoint, communicationStepSize, preQuitFlag);

    // Handle OSI TrafficUpdate input
    osi3::TrafficUpdate traffic_update;
    if (get_fmi_traffic_update_in(traffic_update))
    {
        for (const auto& obj : traffic_update.update())
        {
            // [GT_MOD] ID Resolution
            int obj_id = GT_GetLocalIdFromGlobalId((int)obj.id().value());
            if (obj_id == -1)
            {
                // Only log once per ID to avoid flooding? Or just log warn.
                // Use static set to log once?
                // For now, log to cerr (visible in console)
                // std::cerr << "[OSMP] Warning: OSI Global ID " << obj.id().value() << " not found in esmini." << std::endl;
                continue; 
            }
            SE_ScenarioObjectState vehicleState;
            SE_GetObjectState(obj_id, &vehicleState);
            if (obj.base().has_orientation())
            {
                vehicleState.h = (float)obj.base().orientation().yaw();
                vehicleState.p = (float)obj.base().orientation().pitch();
                vehicleState.r = (float)obj.base().orientation().roll();
            }

            if (obj.base().has_position())
            {
                // Correct OSI center position to esmini reference point (Rear Axle)
                // Ref_Point = OSI_Position - Rotate(Center_Offset)
                // Apply 2D rotation for X/Y offsets (assuming Yaw is dominant)
                vehicleState.x =
                    obj.base().position().x() - (vehicleState.centerOffsetX * cos(vehicleState.h) - vehicleState.centerOffsetY * sin(vehicleState.h));
                vehicleState.y =
                    obj.base().position().y() - (vehicleState.centerOffsetX * sin(vehicleState.h) + vehicleState.centerOffsetY * cos(vehicleState.h));
                
                // Apply Z offset correction (simplified subtraction)
                vehicleState.z = obj.base().position().z() - vehicleState.centerOffsetZ;
            }

            // Report full 6DOF position/rotation
            SE_ReportObjectPos(obj_id, 0, vehicleState.x, vehicleState.y, vehicleState.z, vehicleState.h, vehicleState.p, vehicleState.r);

            if (obj.base().has_velocity())
            {
                SE_ReportObjectVel(obj_id, 0, obj.base().velocity().x(), obj.base().velocity().y(), obj.base().velocity().z());
            }

            // [GT_MOD] START: Inject Light State from OSI TrafficUpdate
            if (obj.has_vehicle_classification() && obj.vehicle_classification().has_light_state())
            {
                const auto& ls = obj.vehicle_classification().light_state();

                // 1. Indicators (Left/Right/Hazard)
                // GT_esmini Types: 8=Left, 9=Right. Mode: 0=Off, 1=On, 2=Flash
                int indLeft = 0; // Off
                int indRight = 0; // Off
                
                auto ind = ls.indicator_state();
                if (ind == osi3::MovingObject_VehicleClassification_LightState::INDICATOR_STATE_LEFT)
                {
                    indLeft = 2; // Flashing
                }
                else if (ind == osi3::MovingObject_VehicleClassification_LightState::INDICATOR_STATE_RIGHT)
                {
                    indRight = 2; // Flashing
                }
                else if (ind == osi3::MovingObject_VehicleClassification_LightState::INDICATOR_STATE_WARNING)
                {
                    indLeft = 2; // Hazard
                    indRight = 2;
                }
                
                GT_SetExternalLightState(obj_id, 8, indLeft);
                GT_SetExternalLightState(obj_id, 9, indRight);

                // 2. Brake Lights
                // GT_esmini Type: 6
                int brake = 0;
                // Check if Brake Light State is Normal or Strong
                auto brk = ls.brake_light_state();
                if (brk == osi3::MovingObject_VehicleClassification_LightState::BRAKE_LIGHT_STATE_NORMAL ||
                    brk == osi3::MovingObject_VehicleClassification_LightState::BRAKE_LIGHT_STATE_STRONG)
                {
                    brake = 1; // On
                }
                GT_SetExternalLightState(obj_id, 6, brake);

                // 3. Reversing Lights
                // GT_esmini Type: 10
                int reverse = 0;
                auto rev = ls.reversing_light();
                if (rev == osi3::MovingObject_VehicleClassification_LightState::GENERIC_LIGHT_STATE_ON)
                {
                    reverse = 1;
                }
                 GT_SetExternalLightState(obj_id, 10, reverse);

                // 4. Headlights (Low/High)
                // GT_esmini: 1=LowBeam, 2=HighBeam
                // OSI: head_light, high_beam (GenericLightState)
                int lowBeam = 0;
                if (ls.head_light() == osi3::MovingObject_VehicleClassification_LightState::GENERIC_LIGHT_STATE_ON)
                {
                    lowBeam = 1;
                }
                GT_SetExternalLightState(obj_id, 1, lowBeam);

                int highBeam = 0;
                if (ls.high_beam() == osi3::MovingObject_VehicleClassification_LightState::GENERIC_LIGHT_STATE_ON)
                {
                    highBeam = 1;
                }
                GT_SetExternalLightState(obj_id, 2, highBeam);
            }
            // [GT_MOD] END
        }
    }
    else
    {
        normal_log("OSMP", "No TrafficUpdate received.");
    }

    // Run simulation step
    // [GT_MOD] START: Use GT_Step to update AutoLight logic
    // Ensure OSI report mode is set for the step
    SE_SetOSIStaticReportMode(SE_OSIStaticReportMode::API);
    
    // [GT_MOD] DIAGNOSTIC LOG
    std::cout << "[OSMP] Calling GT_Step with dt: " << communicationStepSize << " at time " << currentCommunicationPoint << std::endl;
    normal_log("OSMP", "Calling GT_Step with dt: %f", communicationStepSize);
    
    GT_Step((double)communicationStepSize);
    
    int quitFlag = SE_GetQuitFlag();
    
    // [GT_MOD] DIAGNOSTIC LOG
    std::cout << "[OSMP] GT_Step returned. QuitFlag: " << quitFlag << std::endl;
    normal_log("OSMP", "GT_Step returned. QuitFlag: %d", quitFlag);
    
    if (quitFlag) 
    {
       std::cerr << "[OSMP] Esmini reported QuitFlag=" << quitFlag << " after GT_Step." << std::endl;
       // doCalc checks it at line 506 (originally 505)
    } 
    // [GT_MOD] END

    update_osmp_output(currentCommunicationPoint + communicationStepSize);
    if (SE_GetQuitFlag() > 0) {
        normal_log("OSMP", ("esmini terminated with flag " + std::to_string(SE_GetQuitFlag()) + ". Terminating agent!").c_str());
        fmiSlaveTerminated = true;
        return fmi2Discard;
    }

    return fmi2OK;
}

fmi2Status EsminiOsiSource::doTerm()
{
    DEBUGBREAK();
    return fmi2OK;
}

void EsminiOsiSource::doFree()
{
    DEBUGBREAK();
}

/*
 * Generic C++ Wrapper Code
 */

EsminiOsiSource::EsminiOsiSource(fmi2String                   theinstanceName,
                                 fmi2Type                     thefmuType,
                                 fmi2String                   thefmuGUID,
                                 fmi2String                   thefmuResourceLocation,
                                 const fmi2CallbackFunctions* thefunctions,
                                 fmi2Boolean                  thevisible,
                                 fmi2Boolean                  theloggingOn)
    : instanceName(theinstanceName),
      fmuType(thefmuType),
      fmuGUID(thefmuGUID),
      fmuResourceLocation(thefmuResourceLocation),
      functions(*thefunctions),
      visible(!!thevisible),
      loggingOn(!!theloggingOn)
{
    currentBufferSVOut = new string();
    currentBufferTCOut = new string();
    lastBufferSVOut = new string();
    lastBufferTCOut = new string();
    loggingCategories.clear();
    loggingCategories.insert("FMI");
    loggingCategories.insert("OSMP");
    loggingCategories.insert("OSI");
}

EsminiOsiSource::~EsminiOsiSource()
{
    delete currentBufferSVOut;
    delete currentBufferTCOut;
    delete lastBufferSVOut;
    delete lastBufferTCOut;
}

fmi2Status EsminiOsiSource::SetDebugLogging(fmi2Boolean theloggingOn, size_t nCategories, const fmi2String categories[])
{
    fmi_verbose_log("fmi2SetDebugLogging(%s)", theloggingOn ? "true" : "false");
    loggingOn = theloggingOn ? true : false;
    if (categories && (nCategories > 0))
    {
        loggingCategories.clear();
        for (size_t i = 0; i < nCategories; i++)
        {
            if (0 == strcmp(categories[i], "FMI"))
                loggingCategories.insert("FMI");
            else if (0 == strcmp(categories[i], "OSMP"))
                loggingCategories.insert("OSMP");
            else if (0 == strcmp(categories[i], "OSI"))
                loggingCategories.insert("OSI");
        }
    }
    else
    {
        loggingCategories.clear();
        loggingCategories.insert("FMI");
        loggingCategories.insert("OSMP");
        loggingCategories.insert("OSI");
    }
    return fmi2OK;
}

fmi2Component EsminiOsiSource::Instantiate(fmi2String                   instanceName,
                                           fmi2Type                     fmuType,
                                           fmi2String                   fmuGUID,
                                           fmi2String                   fmuResourceLocation,
                                           const fmi2CallbackFunctions* functions,
                                           fmi2Boolean                  visible,
                                           fmi2Boolean                  loggingOn)
{
    EsminiOsiSource* myc = new EsminiOsiSource(instanceName, fmuType, fmuGUID, fmuResourceLocation, functions, visible, loggingOn);

    if (myc == NULL)
    {
        fmi_verbose_log_global("fmi2Instantiate(\"%s\",%d,\"%s\",\"%s\",\"%s\",%d,%d) = NULL (alloc failure)",
                               instanceName,
                               fmuType,
                               fmuGUID,
                               (fmuResourceLocation != NULL) ? fmuResourceLocation : "<NULL>",
                               "FUNCTIONS",
                               visible,
                               loggingOn);
        return NULL;
    }

    if (myc->doInit() != fmi2OK)
    {
        fmi_verbose_log_global("fmi2Instantiate(\"%s\",%d,\"%s\",\"%s\",\"%s\",%d,%d) = NULL (doInit failure)",
                               instanceName,
                               fmuType,
                               fmuGUID,
                               (fmuResourceLocation != NULL) ? fmuResourceLocation : "<NULL>",
                               "FUNCTIONS",
                               visible,
                               loggingOn);
        delete myc;
        return NULL;
    }
    else
    {
        fmi_verbose_log_global("fmi2Instantiate(\"%s\",%d,\"%s\",\"%s\",\"%s\",%d,%d) = %p",
                               instanceName,
                               fmuType,
                               fmuGUID,
                               (fmuResourceLocation != NULL) ? fmuResourceLocation : "<NULL>",
                               "FUNCTIONS",
                               visible,
                               loggingOn,
                               myc);
        return (fmi2Component)myc;
    }
}

fmi2Status EsminiOsiSource::SetupExperiment(fmi2Boolean toleranceDefined,
                                            fmi2Real    tolerance,
                                            fmi2Real    startTime,
                                            fmi2Boolean stopTimeDefined,
                                            fmi2Real    stopTime)
{
    fmi_verbose_log("fmi2SetupExperiment(%d,%g,%g,%d,%g)", toleranceDefined, tolerance, startTime, stopTimeDefined, stopTime);
    return doStart(toleranceDefined, tolerance, startTime, stopTimeDefined, stopTime);
}

fmi2Status EsminiOsiSource::EnterInitializationMode()
{
    fmi_verbose_log("fmi2EnterInitializationMode()");
    return doEnterInitializationMode();
}

fmi2Status EsminiOsiSource::ExitInitializationMode()
{
    fmi_verbose_log("fmi2ExitInitializationMode()");
    return doExitInitializationMode();
}

fmi2Status EsminiOsiSource::DoStep(fmi2Real    currentCommunicationPoint,
                                   fmi2Real    communicationStepSize,
                                   fmi2Boolean noSetFMUStatePriorToCurrentPointfmi2Component)
{
    fmi_verbose_log("fmi2DoStep(%g,%g,%d)", currentCommunicationPoint, communicationStepSize, noSetFMUStatePriorToCurrentPointfmi2Component);
    return doCalc(currentCommunicationPoint, communicationStepSize, noSetFMUStatePriorToCurrentPointfmi2Component);
}

fmi2Status EsminiOsiSource::Terminate()
{
    fmi_verbose_log("fmi2Terminate()");
    return doTerm();
}

fmi2Status EsminiOsiSource::Reset()
{
    fmi_verbose_log("fmi2Reset()");

    fmiSlaveTerminated = false;

    doFree();
    return doInit();
}

void EsminiOsiSource::FreeInstance()
{
    fmi_verbose_log("fmi2FreeInstance()");
    doFree();
}

fmi2Status EsminiOsiSource::GetReal(const fmi2ValueReference vr[], size_t nvr, fmi2Real value[])
{
    fmi_verbose_log("fmi2GetReal(...)");
    for (size_t i = 0; i < nvr; i++)
    {
        if (vr[i] < FMI_REAL_VARS)
            value[i] = real_vars[vr[i]];
        else
            return fmi2Error;
    }
    return fmi2OK;
}

fmi2Status EsminiOsiSource::GetInteger(const fmi2ValueReference vr[], size_t nvr, fmi2Integer value[])
{
    fmi_verbose_log("fmi2GetInteger(...)");
    for (size_t i = 0; i < nvr; i++)
    {
        if (vr[i] < FMI_INTEGER_VARS)
            value[i] = integer_vars[vr[i]];
        else
            return fmi2Error;
    }
    return fmi2OK;
}

fmi2Status EsminiOsiSource::GetBoolean(const fmi2ValueReference vr[], size_t nvr, fmi2Boolean value[])
{
    fmi_verbose_log("fmi2GetBoolean(...)");
    for (size_t i = 0; i < nvr; i++)
    {
        if (vr[i] < FMI_BOOLEAN_VARS)
            value[i] = boolean_vars[vr[i]];
        else
            return fmi2Error;
    }
    return fmi2OK;
}

fmi2Status EsminiOsiSource::GetString(const fmi2ValueReference vr[], size_t nvr, fmi2String value[])
{
    fmi_verbose_log("fmi2GetString(...)");
    for (size_t i = 0; i < nvr; i++)
    {
        if (vr[i] < FMI_STRING_VARS)
            value[i] = string_vars[vr[i]].c_str();
        else
            return fmi2Error;
    }
    return fmi2OK;
}

fmi2Status EsminiOsiSource::SetReal(const fmi2ValueReference vr[], size_t nvr, const fmi2Real value[])
{
    fmi_verbose_log("fmi2SetReal(...)");
    for (size_t i = 0; i < nvr; i++)
    {
        if (vr[i] < FMI_REAL_VARS) {
            real_vars[vr[i]] = value[i];
            
            // [GT_MOD] Apply Parameter if mapped
            if (parameter_map_.find(vr[i]) != parameter_map_.end()) {
                std::string paramName = parameter_map_[vr[i]];
                // Set parameter in esmini
                // Note: esmini parameters are global/shared.
                SE_SetParameterDouble(paramName.c_str(), value[i]);
                // Log verbose
                // fmi_verbose_log("SetReal: Mapped VR %d -> %s = %f", vr[i], paramName.c_str(), value[i]);
            }
            // [GT_MOD] END
        }
        else
            return fmi2Error;
    }
    return fmi2OK;
}

fmi2Status EsminiOsiSource::SetInteger(const fmi2ValueReference vr[], size_t nvr, const fmi2Integer value[])
{
    fmi_verbose_log("fmi2SetInteger(...)");
    for (size_t i = 0; i < nvr; i++)
    {
        if (vr[i] < FMI_INTEGER_VARS)
            integer_vars[vr[i]] = value[i];
        else
            return fmi2Error;
    }
    return fmi2OK;
}

fmi2Status EsminiOsiSource::SetBoolean(const fmi2ValueReference vr[], size_t nvr, const fmi2Boolean value[])
{
    fmi_verbose_log("fmi2SetBoolean(...)");
    for (size_t i = 0; i < nvr; i++)
    {
        if (vr[i] < FMI_BOOLEAN_VARS)
            boolean_vars[vr[i]] = value[i];
        else
            return fmi2Error;
    }
    return fmi2OK;
}

fmi2Status EsminiOsiSource::SetString(const fmi2ValueReference vr[], size_t nvr, const fmi2String value[])
{
    fmi_verbose_log("fmi2SetString(...)");
    for (size_t i = 0; i < nvr; i++)
    {
        if (vr[i] < FMI_STRING_VARS)
            string_vars[vr[i]] = value[i];
        else
            return fmi2Error;
    }
    return fmi2OK;
}

/*
 * FMI 2.0 Co-Simulation Interface API
 */

extern "C"
{
    FMI2_Export const char* fmi2GetTypesPlatform()
    {
        return fmi2TypesPlatform;
    }

    FMI2_Export const char* fmi2GetVersion()
    {
        return fmi2Version;
    }

    FMI2_Export fmi2Status fmi2SetDebugLogging(fmi2Component c, fmi2Boolean loggingOn, size_t nCategories, const fmi2String categories[])
    {
        EsminiOsiSource* myc = (EsminiOsiSource*)c;
        return myc->SetDebugLogging(loggingOn, nCategories, categories);
    }

    /*
     * Functions for Co-Simulation
     */
    FMI2_Export fmi2Component fmi2Instantiate(fmi2String                   instanceName,
                                              fmi2Type                     fmuType,
                                              fmi2String                   fmuGUID,
                                              fmi2String                   fmuResourceLocation,
                                              const fmi2CallbackFunctions* functions,
                                              fmi2Boolean                  visible,
                                              fmi2Boolean                  loggingOn)
    {
        return EsminiOsiSource::Instantiate(instanceName, fmuType, fmuGUID, fmuResourceLocation, functions, visible, loggingOn);
    }

    FMI2_Export fmi2Status fmi2SetupExperiment(fmi2Component c,
                                               fmi2Boolean   toleranceDefined,
                                               fmi2Real      tolerance,
                                               fmi2Real      startTime,
                                               fmi2Boolean   stopTimeDefined,
                                               fmi2Real      stopTime)
    {
        EsminiOsiSource* myc = (EsminiOsiSource*)c;
        return myc->SetupExperiment(toleranceDefined, tolerance, startTime, stopTimeDefined, stopTime);
    }

    FMI2_Export fmi2Status fmi2EnterInitializationMode(fmi2Component c)
    {
        EsminiOsiSource* myc = (EsminiOsiSource*)c;
        return myc->EnterInitializationMode();
    }

    FMI2_Export fmi2Status fmi2ExitInitializationMode(fmi2Component c)
    {
        EsminiOsiSource* myc = (EsminiOsiSource*)c;
        return myc->ExitInitializationMode();
    }

    FMI2_Export fmi2Status fmi2DoStep(fmi2Component c,
                                      fmi2Real      currentCommunicationPoint,
                                      fmi2Real      communicationStepSize,
                                      fmi2Boolean   noSetFMUStatePriorToCurrentPointfmi2Component)
    {
        EsminiOsiSource* myc = (EsminiOsiSource*)c;
        return myc->DoStep(currentCommunicationPoint, communicationStepSize, noSetFMUStatePriorToCurrentPointfmi2Component);
    }

    FMI2_Export fmi2Status fmi2Terminate(fmi2Component c)
    {
        EsminiOsiSource* myc = (EsminiOsiSource*)c;
        return myc->Terminate();
    }

    FMI2_Export fmi2Status fmi2Reset(fmi2Component c)
    {
        EsminiOsiSource* myc = (EsminiOsiSource*)c;
        return myc->Reset();
    }

    FMI2_Export void fmi2FreeInstance(fmi2Component c)
    {
        EsminiOsiSource* myc = (EsminiOsiSource*)c;
        myc->FreeInstance();
        delete myc;
    }

    /*
     * Data Exchange Functions
     */
    FMI2_Export fmi2Status fmi2GetReal(fmi2Component c, const fmi2ValueReference vr[], size_t nvr, fmi2Real value[])
    {
        EsminiOsiSource* myc = (EsminiOsiSource*)c;
        return myc->GetReal(vr, nvr, value);
    }

    FMI2_Export fmi2Status fmi2GetInteger(fmi2Component c, const fmi2ValueReference vr[], size_t nvr, fmi2Integer value[])
    {
        EsminiOsiSource* myc = (EsminiOsiSource*)c;
        return myc->GetInteger(vr, nvr, value);
    }

    FMI2_Export fmi2Status fmi2GetBoolean(fmi2Component c, const fmi2ValueReference vr[], size_t nvr, fmi2Boolean value[])
    {
        EsminiOsiSource* myc = (EsminiOsiSource*)c;
        return myc->GetBoolean(vr, nvr, value);
    }

    FMI2_Export fmi2Status fmi2GetString(fmi2Component c, const fmi2ValueReference vr[], size_t nvr, fmi2String value[])
    {
        EsminiOsiSource* myc = (EsminiOsiSource*)c;
        return myc->GetString(vr, nvr, value);
    }

    FMI2_Export fmi2Status fmi2SetReal(fmi2Component c, const fmi2ValueReference vr[], size_t nvr, const fmi2Real value[])
    {
        EsminiOsiSource* myc = (EsminiOsiSource*)c;
        return myc->SetReal(vr, nvr, value);
    }

    FMI2_Export fmi2Status fmi2SetInteger(fmi2Component c, const fmi2ValueReference vr[], size_t nvr, const fmi2Integer value[])
    {
        EsminiOsiSource* myc = (EsminiOsiSource*)c;
        return myc->SetInteger(vr, nvr, value);
    }

    FMI2_Export fmi2Status fmi2SetBoolean(fmi2Component c, const fmi2ValueReference vr[], size_t nvr, const fmi2Boolean value[])
    {
        EsminiOsiSource* myc = (EsminiOsiSource*)c;
        return myc->SetBoolean(vr, nvr, value);
    }

    FMI2_Export fmi2Status fmi2SetString(fmi2Component c, const fmi2ValueReference vr[], size_t nvr, const fmi2String value[])
    {
        EsminiOsiSource* myc = (EsminiOsiSource*)c;
        return myc->SetString(vr, nvr, value);
    }

    /*
     * Unsupported Features (FMUState, Derivatives, Async DoStep, Status Enquiries)
     */
    FMI2_Export fmi2Status fmi2GetFMUstate(fmi2Component c, fmi2FMUstate* FMUstate)
    {
        return fmi2Error;
    }

    FMI2_Export fmi2Status fmi2SetFMUstate(fmi2Component c, fmi2FMUstate FMUstate)
    {
        return fmi2Error;
    }

    FMI2_Export fmi2Status fmi2FreeFMUstate(fmi2Component c, fmi2FMUstate* FMUstate)
    {
        return fmi2Error;
    }

    FMI2_Export fmi2Status fmi2SerializedFMUstateSize(fmi2Component c, fmi2FMUstate FMUstate, size_t* size)
    {
        return fmi2Error;
    }

    FMI2_Export fmi2Status fmi2SerializeFMUstate(fmi2Component c, fmi2FMUstate FMUstate, fmi2Byte serializedState[], size_t size)
    {
        return fmi2Error;
    }

    FMI2_Export fmi2Status fmi2DeSerializeFMUstate(fmi2Component c, const fmi2Byte serializedState[], size_t size, fmi2FMUstate* FMUstate)
    {
        return fmi2Error;
    }

    FMI2_Export fmi2Status fmi2GetDirectionalDerivative(fmi2Component            c,
                                                        const fmi2ValueReference vUnknown_ref[],
                                                        size_t                   nUnknown,
                                                        const fmi2ValueReference vKnown_ref[],
                                                        size_t                   nKnown,
                                                        const fmi2Real           dvKnown[],
                                                        fmi2Real                 dvUnknown[])
    {
        return fmi2Error;
    }

    FMI2_Export fmi2Status
    fmi2SetRealInputDerivatives(fmi2Component c, const fmi2ValueReference vr[], size_t nvr, const fmi2Integer order[], const fmi2Real value[])
    {
        return fmi2Error;
    }

    FMI2_Export fmi2Status
    fmi2GetRealOutputDerivatives(fmi2Component c, const fmi2ValueReference vr[], size_t nvr, const fmi2Integer order[], fmi2Real value[])
    {
        return fmi2Error;
    }

    FMI2_Export fmi2Status fmi2CancelStep(fmi2Component c)
    {
        return fmi2OK;
    }

    FMI2_Export fmi2Status fmi2GetStatus(fmi2Component c, const fmi2StatusKind s, fmi2Status* value)
    {
        return fmi2Discard;
    }

    FMI2_Export fmi2Status fmi2GetRealStatus(fmi2Component c, const fmi2StatusKind s, fmi2Real* value)
    {
        return fmi2Discard;
    }

    FMI2_Export fmi2Status fmi2GetIntegerStatus(fmi2Component c, const fmi2StatusKind s, fmi2Integer* value)
    {
        return fmi2Discard;
    }

    FMI2_Export fmi2Status fmi2GetBooleanStatus(fmi2Component c, const fmi2StatusKind s, fmi2Boolean* value)
    {
        if (s == fmi2StatusKind::fmi2Terminated) {
            EsminiOsiSource* myc = (EsminiOsiSource*)c;
            *value = myc->get_slave_terminated() ? fmi2True : fmi2False;
            return fmi2OK;
        }
        return fmi2Discard;
    }

    FMI2_Export fmi2Status fmi2GetStringStatus(fmi2Component c, const fmi2StatusKind s, fmi2String* value)
    {
        return fmi2Discard;
    }
}
