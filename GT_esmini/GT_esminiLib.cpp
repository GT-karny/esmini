/*
 * GT_esmini - Extended esmini with Light Functionality
 * https://github.com/esmini/esmini
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2024 GT_esmini contributors
 */

// Include esminiLib.cpp to access static 'player' and 'scenarioEngine'
// This effectively compiles esminiLib code as part of this module
#include "../EnvironmentSimulator/Libraries/esminiLib/esminiLib.cpp"

#include "GT_esminiLib.hpp"
#include "GT_ScenarioReader.hpp"
#include "AutoLightController.hpp"
#include "ExtraEntities.hpp" // For VehicleExtensionManager

#include <vector>
#include <memory>
#include <functional>
#include <string>
#include <iostream>
#include <cstdio>
#include <osi_groundtruth.pb.h>

#include "ControllerRealDriver.hpp"
#include "GT_HostVehicleReporter.hpp"

// Forward declaration for GetCurrentModuleDirectory (defined in ControllerRealDriver.cpp)
namespace gt_esmini { std::string GetCurrentModuleDirectory(); }

// AutoLightManager Implementation
class AutoLightManager
{
public:
    static AutoLightManager& Instance()
    {
        static AutoLightManager instance;
        return instance;
    }

    void Init(scenarioengine::Entities* entities)
    {
        controllers_.clear();
        if (!entities) return;

        // Auto-detect Ego vehicle (Host Vehicle) as the first object in the list
        // This corresponds to OSI Host Vehicle ID logic
        if (!entities->object_.empty())
        {
            egoId_ = entities->object_.front()->GetId();
        }

        for (auto* obj : entities->object_)
        {
            if (obj && obj->type_ == scenarioengine::Object::Type::VEHICLE)
            {
                // Skip AutoLight for Ego vehicle if egoless mode is enabled
                if (egoless_ && obj->GetId() == egoId_)
                {
                    std::cout << "AutoLight: Skipping Ego vehicle (ID: " << egoId_ << ")" << std::endl;
                    continue;
                }

                scenarioengine::Vehicle* vehicle = static_cast<scenarioengine::Vehicle*>(obj);
                
                // Ensure VehicleLightExtension exists
                auto* ext = gt_esmini::VehicleExtensionManager::Instance().GetExtension(vehicle);
                if (!ext)
                {
                    ext = new gt_esmini::VehicleLightExtension(vehicle);
                    gt_esmini::VehicleExtensionManager::Instance().RegisterExtension(vehicle, ext);
                }

                // Create AutoLightController with both arguments
                controllers_.push_back(std::make_unique<gt_esmini::AutoLightController>(vehicle, ext));
            }
        }
    }

    void SetEgoless(bool egoless)
    {
        egoless_ = egoless;
    }

    void Enable(bool enable)
    {
        enabled_ = enable;
        for (auto& ctrl : controllers_)
        {
            ctrl->Enable(enable);
        }
    }

    void Update(double dt)
    {
        if (!enabled_) return;
        for (auto& ctrl : controllers_)
        {
            ctrl->Update(dt);
        }
    }

    void Close()
    {
        controllers_.clear();
        // Also clear extensions? They are owned by VehicleExtensionManager
        gt_esmini::VehicleExtensionManager::Instance().Clear();
    }

private:
    AutoLightManager() : enabled_(false), egoless_(false), egoId_(-1) {}
    
    std::vector<std::unique_ptr<gt_esmini::AutoLightController>> controllers_;
    bool enabled_;
    bool egoless_;
    int egoId_;
};

// Hook registration function (externally defined in GT_OSIReporter.cpp part of ScenarioEngine)
void GT_SetLightStateProvider(std::function<::gt_esmini::LightState(void*, int)> provider);

// --- GT_esminiLib C-API Implementation ---

// Basic XOSC sanitizer to allow SE_Init to pass even with unsupported actions
static bool CreateSanitizedScenario(const char* inFile, const std::string& outFile)
{
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_file(inFile);
    if (!result) return false;

    // We need to remove AppearanceAction and LightStateAction nodes
    // because standard esmini ScenarioReader throws exception on them.
    // Traversing to find them.
    
    // Recursive lambda to strip unsupported nodes
    std::function<void(pugi::xml_node)> stripUnsupported;
    stripUnsupported = [&](pugi::xml_node node) {
        for (pugi::xml_node child = node.first_child(); child; )
        {
            pugi::xml_node next = child.next_sibling();
            std::string name = child.name();
            if (name == "AppearanceAction" || name == "ParameterDeclarations" && node.name() == "Private") 
            {
                // Note: Removing ParameterDeclarations inside Private? No, just AppearanceAction.
                // Re-aligned logic. Only AppearanceAction causes error in standard reader (PrivateAction).
                if(name == "AppearanceAction") {
                    node.remove_child(child);
                } else {
                    stripUnsupported(child);
                }
            }
            else // Not AppearanceAction
            {
                // Specifically look for it.
                if (name == "AppearanceAction")
                {
                    node.remove_child(child);
                }
                else
                {
                    stripUnsupported(child);
                }
            }
            child = next;
        }
    };
    
    // Refined logic: stripUnsupported traverses all.
    // However, we just need to target AppearanceAction which is under Private -> PrivateAction
    // Let's keep it simple: any node named AppearanceAction is removed.
    // (Standard Reader throws if name is not Longitudinal or Lateral)
    // Actually, ScenarioReader throws if it finds ANY child of PrivateAction it doesn't know.
    // So we just remove AppearanceAction children.
    
    std::function<void(pugi::xml_node)> strip;
    strip = [&](pugi::xml_node node) {
        for (pugi::xml_node child = node.first_child(); child; )
        {
            pugi::xml_node next = child.next_sibling();
            std::string name = child.name();
            
            if (name == "AppearanceAction") 
            {
                node.remove_child(child);
            }
            else
            {
                strip(child);
            }
            child = next;
        }
    };

    strip(doc);
    
    return doc.save_file(outFile.c_str());
}

GT_ESMINI_API int GT_Init(const char* oscFilename, int disable_ctrls)
{
    // 1. Create a sanitized version of the scenario
    // esmini throws error on AppearanceAction/LightStateAction.
    // We strip them for the main initialization.
    std::string sanitizedFile = std::string(oscFilename) + ".temp.xosc";
    if (!CreateSanitizedScenario(oscFilename, sanitizedFile))
    {
         std::cerr << "GT_Init: Failed to create sanitized scenario file." << std::endl;
         return -1;
    }

    // 1.5 Register Custom Controller
    scenarioengine::ScenarioReader::RegisterController(CONTROLLER_REAL_DRIVER_TYPE_NAME, gt_esmini::InstantiateControllerRealDriver);

    // 2. Initialize esmini using SE_Init with sanitized file
    int ret = SE_Init(sanitizedFile.c_str(), disable_ctrls, 0, 0, 0); 
    
    // Clean up temp file
    std::remove(sanitizedFile.c_str()); 

    if (ret != 0)
    {
        return ret;
    }

    // 3. Perform Delta Parsing for Extensions using ORIGINAL file
    if (player && player->scenarioEngine)
    {
        // Load ORIGINAL XML
        pugi::xml_document doc;
        pugi::xml_parse_result result = doc.load_file(oscFilename);
        
        if (result)
        {
            // Use GT_ScenarioReader to parse extensions
            // Access Catalogs via existing loader because it's private in Engine
            auto* scReader = player->scenarioEngine->GetScenarioReader();
            auto* catalogs = scReader ? scReader->GetCatalogs() : nullptr;
            
            gt_esmini::GT_ScenarioReader reader(
                &player->scenarioEngine->entities_,
                catalogs, 
                &player->scenarioEngine->environment
            );
            
            // Inject actions into Storyboard
            reader.ParseExtensionActions(doc, player->scenarioEngine->storyBoard);
        }
        else
        {
            std::cerr << "GT_Init: Failed to reload XOSC for extensions: " << result.description() << std::endl;
        }

        // 4. Initialize AutoLightManager
        AutoLightManager::Instance().Init(&player->scenarioEngine->entities_);

        // 5. Register Hook for OSIReporter
        // Forward declaration of GT_SetLightStateProvider (defined in GT_OSIReporter.cpp)
        extern void GT_SetLightStateProvider(std::function<::gt_esmini::LightState(void*, int)> provider);
        
        GT_SetLightStateProvider([](void* v, int t) -> gt_esmini::LightState {
            auto* vehicle = static_cast<scenarioengine::Vehicle*>(v);
            auto* ext = gt_esmini::VehicleExtensionManager::Instance().GetExtension(vehicle);
            if (ext) {
                return ext->GetLightState(static_cast<gt_esmini::VehicleLightType>(t));
            }
            gt_esmini::LightState emptyState;
            emptyState.mode = gt_esmini::LightState::Mode::OFF;
            return emptyState;
        });

        // 6. Register OSIReporter for global access (for Light state)
#ifdef _USE_OSI
        extern void GT_SetCurrentOSIReporter(OSIReporter* reporter);
        if (player->osiReporter)
        {
            GT_SetCurrentOSIReporter(player->osiReporter);
        }
#endif  // _USE_OSI

        // 7. Initialize GT_HostVehicleReporter (separated from OSIReporter)
        {
            std::string exeDir = gt_esmini::GetCurrentModuleDirectory();
            std::string configFile = exeDir + "/host_vehicle_config.json";
            gt_esmini::GT_HostVehicleReporter::Instance().Init(48199, configFile);
        }
    }

    return 0;
}

GT_ESMINI_API int GT_InitWithArgs(int argc, const char* argv[])
{
    std::cerr << "[GT_esmini] GT_InitWithArgs called with argc=" << argc << std::endl;
    if (argc > 0 && argv) {
        std::cerr << "[GT_esmini] argv[0]=" << (argv[0] ? argv[0] : "NULL") << std::endl;
    }
    const char* filename = nullptr;
    
    // Simple argument parsing to find the filename.
    // Logic mostly copied from esmini-dyn/main.cpp to identify the filename arg
    if (argc >= 2)
    {
        if (strncmp(argv[1], "--", 2) != 0)
        {
             filename = argv[1];
        }
        else
        {
            // Look for --osc argument if needed, or iterate
            for(int i=1; i<argc; i++)
            {
                if (strcmp(argv[i], "--osc") == 0 && i+1 < argc)
                {
                    filename = argv[i+1];
                    break;
                }
            }
        }
    }

    // Capture OSI IP if provided
    std::string osiTargetIp = "";

    // If filename found, sanitized it
    std::string sanitizedFile;
    std::vector<const char*> newArgv;
    std::vector<std::string> argStorage; // to keep strings alive

    if (filename)
    {
        std::cerr << "[GT_esmini] Sanitizing filename: " << filename << std::endl;
        sanitizedFile = std::string(filename) + ".temp.xosc";
        if (!CreateSanitizedScenario(filename, sanitizedFile))
        {
             std::cerr << "GT_InitWithArgs: Failed to create sanitized scenario file." << std::endl;
             // Try proceeding with original filename (might crash if unsupported actions present)
             sanitizedFile = filename;
        }

        // Reconstruct argv with sanitized filename
        for(int i=0; i<argc; i++)
        {
            if (argv[i] && strcmp(argv[i], filename) == 0)
            {
                argStorage.push_back(sanitizedFile);
                newArgv.push_back(argStorage.back().c_str());
            }
            // Filter custom arguments that esmini doesn't recognize
            else if (argv[i] && (strcmp(argv[i], "--autolight") == 0 || strcmp(argv[i], "--autolight-egoless") == 0 || strcmp(argv[i], "--osi") == 0 || strcmp(argv[i], "--hz") == 0)) 
            {
                if (strcmp(argv[i], "--autolight-egoless") == 0)
                {
                    AutoLightManager::Instance().SetEgoless(true);
                }

                if (strcmp(argv[i], "--osi") == 0)
                {
                    if (i + 1 < argc)
                    {
                        osiTargetIp = argv[i+1];
                        i++; // Skip the IP
                    }
                }
                else if (strcmp(argv[i], "--hz") == 0)
                {
                    i++; // Skip the frequency
                }
            }
            else
            {
                newArgv.push_back(argv[i]);
            }
        }
    }
    else
    {
        // No filename found? Pass as is.
        for(int i=0; i<argc; i++) newArgv.push_back(argv[i]);
    }

    // 1.5 Register Custom Controller
    scenarioengine::ScenarioReader::RegisterController(CONTROLLER_REAL_DRIVER_TYPE_NAME, gt_esmini::InstantiateControllerRealDriver);

    // 2. Initialize esmini using SE_Init with sanitized args
    std::cerr << "[GT_esmini] Calling SE_InitWithArgs with " << newArgv.size() << " args." << std::endl;
    int ret = SE_InitWithArgs(newArgv.size(), newArgv.data());
    std::cerr << "[GT_esmini] SE_InitWithArgs returned: " << ret << std::endl;
    
    // Clean up temp file (or keep for debug?)
    // std::remove(sanitizedFile.c_str()); 

    if (ret != 0)
    {
        return ret;
    }

    // [GT_MOD] DIAGNOSTIC & FIX: Check and Reset QuitFlag
    int postSeInitQuit = SE_GetQuitFlag();
    if (postSeInitQuit) {
        std::cout << "GT_InitWithArgs: WARNING: SE_InitWithArgs returned 0 but QuitFlag is " << postSeInitQuit << ". Forcing reset." << std::endl;
        if (player) {
            player->SetQuitRequest(false);
            std::cout << "GT_InitWithArgs: QuitFlag forced to 0." << std::endl;
        }
    } else {
        std::cout << "GT_InitWithArgs: SE_InitWithArgs OK. QuitFlag=0." << std::endl;
    }
    // [GT_MOD] END

    // 3. Perform Delta Parsing for Extensions using ORIGINAL file
    if (filename && player && player->scenarioEngine)
    {
        // Load ORIGINAL XML
        pugi::xml_document doc;
        pugi::xml_parse_result result = doc.load_file(filename);
        
        if (result)
        {
            auto* scReader = player->scenarioEngine->GetScenarioReader();
            auto* catalogs = scReader ? scReader->GetCatalogs() : nullptr;
            
            gt_esmini::GT_ScenarioReader reader(
                &player->scenarioEngine->entities_,
                catalogs, 
                &player->scenarioEngine->environment
            );
            
            // Inject actions into Storyboard
            reader.ParseExtensionActions(doc, player->scenarioEngine->storyBoard);
        }
        else
        {
            std::cerr << "GT_InitWithArgs: Failed to reload XOSC for extensions: " << result.description() << std::endl;
        }

        // 4. Initialize AutoLightManager
        AutoLightManager::Instance().Init(&player->scenarioEngine->entities_);

        // Check for --autolight argument in original argv
        bool autoLightEnabled = false;
        for(int i=0; i<argc; i++) {
            if(argv[i] && strcmp(argv[i], "--autolight") == 0) {
                autoLightEnabled = true;
                break;
            }
        }
        if (autoLightEnabled) {
             AutoLightManager::Instance().Enable(true);
             std::cout << "GT_Init: AutoLight enabled via argument." << std::endl;
        }

        // 5. Register Hook for OSIReporter
        extern void GT_SetLightStateProvider(std::function<::gt_esmini::LightState(void*, int)> provider);

        GT_SetLightStateProvider([](void* v, int t) -> gt_esmini::LightState {
            auto* vehicle = static_cast<scenarioengine::Vehicle*>(v);
            auto* ext = gt_esmini::VehicleExtensionManager::Instance().GetExtension(vehicle);
            if (ext) {
                return ext->GetLightState(static_cast<gt_esmini::VehicleLightType>(t));
            }
            gt_esmini::LightState emptyState;
            emptyState.mode = gt_esmini::LightState::Mode::OFF;
            return emptyState;
        });

        // 6. Register OSIReporter for global access (for Light state)
#ifdef _USE_OSI
        extern void GT_SetCurrentOSIReporter(OSIReporter* reporter);
        if (player->osiReporter)
        {
            GT_SetCurrentOSIReporter(player->osiReporter);
        }
#endif  // _USE_OSI

        // 7. Initialize GT_HostVehicleReporter (separated from OSIReporter)
        {
            std::string exeDir = gt_esmini::GetCurrentModuleDirectory();
            std::string configFile = exeDir + "/host_vehicle_config.json";
            gt_esmini::GT_HostVehicleReporter::Instance().Init(48199, configFile, osiTargetIp);
        }
    }

    // [GT_MOD] DIAGNOSTIC
    int finalQuit = SE_GetQuitFlag();
    if (finalQuit) {
        std::cerr << "GT_InitWithArgs: CRITICAL! QuitFlag=" << finalQuit << " at end of GT_InitWithArgs." << std::endl;
    }
    // [GT_MOD] END

    return 0;
}

GT_ESMINI_API void GT_Step(double dt)
{
    // Call standard step
    SE_StepDT(dt);

    // Update AutoLight
    AutoLightManager::Instance().Update(dt);

    // Update HostVehicleData (using separated GT_HostVehicleReporter)
#ifdef _USE_OSI
    if (player && player->scenarioGateway && player->scenarioEngine &&
        gt_esmini::GT_HostVehicleReporter::Instance().IsInitialized())
    {
        auto& hvReporter = gt_esmini::GT_HostVehicleReporter::Instance();

        // Get ego vehicle (first object) via ScenarioGateway
        ObjectState* egoState = player->scenarioGateway->getObjectStatePtrByIdx(0);
        if (egoState)
        {
            int vehicleId = egoState->state_.info.id;

            // Clear ADAS functions from previous frame
            hvReporter.ClearADASFunctions(vehicleId);

            // Try to get RealDriverController and pass input data to HostVehicleReporter
            Object* egoObject = player->scenarioEngine->entities_.GetObjectById(vehicleId);
            if (egoObject)
            {
                Controller* ctrl = egoObject->GetController(CONTROLLER_REAL_DRIVER_TYPE_NAME);
                if (ctrl)
                {
                    // Cast to RealDriverController
                    gt_esmini::ControllerRealDriver* realDriver =
                        dynamic_cast<gt_esmini::ControllerRealDriver*>(ctrl);

                    if (realDriver)
                    {
                        // Get input data from controller
                        double throttle, brake, steering;
                        int gear, lightMask;
                        realDriver->GetInputsForOSI(throttle, brake, steering, gear, lightMask);

                        // Get powertrain data
                        double rpm, torque;
                        realDriver->GetPowertrainForOSI(rpm, torque);

                        // Pass to GT_HostVehicleReporter
                        hvReporter.SetInputs(vehicleId, throttle, brake, steering, gear);
                        hvReporter.SetLights(vehicleId, lightMask);
                        hvReporter.SetPowertrain(vehicleId, rpm, torque);

                        // Get and pass ADAS data (OSI compliant function names)
                        unsigned int adasEnabled, adasAvailable;
                        realDriver->GetADASForOSI(adasEnabled, adasAvailable);

                        // Map bits to OSI ADAS function names (24 types based on OSI VehicleAutomatedDrivingFunction_Name)
                        static const char* adasNames[] = {
                            "BLIND_SPOT_WARNING",                  // 0x00000001
                            "FORWARD_COLLISION_WARNING",           // 0x00000002
                            "LANE_DEPARTURE_WARNING",              // 0x00000004
                            "PARKING_COLLISION_WARNING",           // 0x00000008
                            "REAR_CROSS_TRAFFIC_WARNING",          // 0x00000010
                            "AUTOMATIC_EMERGENCY_BRAKING",         // 0x00000020
                            "AUTOMATIC_EMERGENCY_STEERING",        // 0x00000040
                            "REVERSE_AUTOMATIC_EMERGENCY_BRAKING", // 0x00000080
                            "ADAPTIVE_CRUISE_CONTROL",             // 0x00000100
                            "LANE_KEEPING_ASSIST",                 // 0x00000200
                            "ACTIVE_DRIVING_ASSISTANCE",           // 0x00000400
                            "BACKUP_CAMERA",                       // 0x00000800
                            "SURROUND_VIEW_CAMERA",                // 0x00001000
                            "NIGHT_VISION",                        // 0x00002000
                            "HEAD_UP_DISPLAY",                     // 0x00004000
                            "ACTIVE_PARKING_ASSISTANCE",           // 0x00008000
                            "REMOTE_PARKING_ASSISTANCE",           // 0x00010000
                            "TRAILER_ASSISTANCE",                  // 0x00020000
                            "AUTOMATIC_HIGH_BEAMS",                // 0x00040000
                            "DRIVER_MONITORING",                   // 0x00080000
                            "URBAN_DRIVING",                       // 0x00100000
                            "HIGHWAY_AUTOPILOT",                   // 0x00200000
                            "CRUISE_CONTROL",                      // 0x00400000
                            "SPEED_LIMIT_CONTROL",                 // 0x00800000
                        };

                        for (int i = 0; i < 24; i++)
                        {
                            unsigned int bit = 1u << i;
                            bool enabled = (adasEnabled & bit) != 0;
                            bool available = (adasAvailable & bit) != 0;

                            if (enabled || available)
                            {
                                hvReporter.AddADASFunction(vehicleId, adasNames[i], enabled, available);
                            }
                        }
                    }
                }
            }

            // Update HostVehicleData for ego vehicle and send
            hvReporter.UpdateFromObjectState(egoState);
            hvReporter.Send();
        }
    }
#endif  // _USE_OSI
}

GT_ESMINI_API void GT_EnableAutoLight()
{
    AutoLightManager::Instance().Enable(true);
}

GT_ESMINI_API void GT_Close()
{
    AutoLightManager::Instance().Close();
    SE_Close();
}

GT_ESMINI_API int GT_GetLightState(int vehicleId, int lightType)
{
    if (!player || !player->scenarioEngine) return -1;
    
    for (auto* obj : player->scenarioEngine->entities_.object_)
    {
        if (obj->id_ == vehicleId && obj->type_ == scenarioengine::Object::Type::VEHICLE)
        {
             scenarioengine::Vehicle* vehicle = static_cast<scenarioengine::Vehicle*>(obj);
             auto* ext = gt_esmini::VehicleExtensionManager::Instance().GetExtension(vehicle);
             if (ext)
             {
                 gt_esmini::LightState state = ext->GetLightState(static_cast<gt_esmini::VehicleLightType>(lightType));
                 return static_cast<int>(state.mode);
             }
             return -1; // Extension not found
        }
    }
    return -1; // Vehicle not found
}

GT_ESMINI_API void GT_SetExternalLightState(int vehicleId, int lightType, int mode)
{
    if (!player || !player->scenarioEngine) return;

    for (auto* obj : player->scenarioEngine->entities_.object_)
    {
        if (obj->id_ == vehicleId && obj->type_ == scenarioengine::Object::Type::VEHICLE)
        {
            scenarioengine::Vehicle* vehicle = static_cast<scenarioengine::Vehicle*>(obj);
            
            // Ensure extension exists
            auto* ext = gt_esmini::VehicleExtensionManager::Instance().GetExtension(vehicle);
            if (!ext)
            {
                ext = new gt_esmini::VehicleLightExtension(vehicle);
                gt_esmini::VehicleExtensionManager::Instance().RegisterExtension(vehicle, ext);
            }

            gt_esmini::LightState state;
            state.mode = static_cast<gt_esmini::LightState::Mode>(mode);
            ext->SetLightState(static_cast<gt_esmini::VehicleLightType>(lightType), state);
            return;
        }
    }
}

GT_ESMINI_API int GT_GetLocalIdFromGlobalId(int global_id)
{
    // Access Raw OSI data via esmini API
    const char* rawPtr = SE_GetOSIGroundTruthRaw();
    if (!rawPtr) return -1;

    // Cast to osi3::GroundTruth*
    // Note: esminiLib returns the internal pointer which is osi3::GroundTruth*
    const osi3::GroundTruth* gt = reinterpret_cast<const osi3::GroundTruth*>(rawPtr);

    // Search Moving Objects
    // OSI IDs are generally uint64, esmini global_id is int (but stored as uint64 in OSI)
    for (int i = 0; i < gt->moving_object_size(); ++i) {
        const auto& obj = gt->moving_object(i);
        if (obj.id().value() == (uint64_t)global_id) {
             // Found object, parse source_reference for Local ID
             for (int j=0; j < obj.source_reference_size(); ++j) {
                 const auto& ref = obj.source_reference(j);
                 for (int k=0; k < ref.identifier_size(); ++k) {
                     const std::string& id_str = ref.identifier(k);
                     // Format created in OSIReporter.cpp: "entity_id:{id}"
                     if (id_str.find("entity_id:") == 0) {
                         try {
                             return std::stoi(id_str.substr(10));
                         } catch (...) {
                             return -1;
                         }
                     }
                 }
             }
        }
    }
    
    // Also check Stationary Objects if necessary, but TrafficUpdates typically target MovingObjects
    // (Vehicles, Pedestrians)
    
    return -1;
}

GT_ESMINI_API int GT_ReportObjectVel(int object_id, float timestamp, float x_vel, float y_vel, float z_vel)
{
    // Call original esminiLib function to update velocity vector
    int ret = SE_ReportObjectVel(object_id, timestamp, x_vel, y_vel, z_vel);
    if (ret != 0)
    {
        return ret;
    }

    // [GT_MOD] Sync scalar speed to match velocity vector magnitude
    float speed = std::sqrt(x_vel * x_vel + y_vel * y_vel + z_vel * z_vel);
    
    // Update speed via ScenarioGateway and Object
    if (player && player->scenarioGateway)
    {
        player->scenarioGateway->updateObjectSpeed(object_id, 0.0, speed);
    }
    
    // Also update Object directly (for callback context)
    Object* obj = nullptr;
    if (player && player->scenarioEngine)
    {
        obj = player->scenarioEngine->entities_.GetObjectById(object_id);
        if (obj)
        {
            obj->SetSpeed(speed);
        }
    }

    return 0;
}

// =====================================
// HostVehicleData APIs
// =====================================

GT_ESMINI_API void GT_SetHostVehicleInputs(int vehicle_id, double throttle, double brake, double steering, int gear)
{
#ifdef _USE_OSI
    if (gt_esmini::GT_HostVehicleReporter::Instance().IsInitialized())
    {
        // If vehicle_id is -1, use the first vehicle (ego)
        int actual_id = vehicle_id;
        if (actual_id < 0 && player && player->scenarioGateway)
        {
            ObjectState* egoState = player->scenarioGateway->getObjectStatePtrByIdx(0);
            if (egoState)
            {
                actual_id = egoState->state_.info.id;
            }
        }

        if (actual_id >= 0)
        {
            gt_esmini::GT_HostVehicleReporter::Instance().SetInputs(actual_id, throttle, brake, steering, gear);
        }
    }
#endif
}

GT_ESMINI_API void GT_SetHostVehicleLights(int vehicle_id, int light_mask)
{
#ifdef _USE_OSI
    if (gt_esmini::GT_HostVehicleReporter::Instance().IsInitialized())
    {
        // If vehicle_id is -1, use the first vehicle (ego)
        int actual_id = vehicle_id;
        if (actual_id < 0 && player && player->scenarioGateway)
        {
            ObjectState* egoState = player->scenarioGateway->getObjectStatePtrByIdx(0);
            if (egoState)
            {
                actual_id = egoState->state_.info.id;
            }
        }

        if (actual_id >= 0)
        {
            gt_esmini::GT_HostVehicleReporter::Instance().SetLights(actual_id, light_mask);
        }
    }
#endif
}

GT_ESMINI_API void GT_SetHostVehiclePowertrain(int vehicle_id, double rpm, double torque)
{
#ifdef _USE_OSI
    if (gt_esmini::GT_HostVehicleReporter::Instance().IsInitialized())
    {
        // If vehicle_id is -1, use the first vehicle (ego)
        int actual_id = vehicle_id;
        if (actual_id < 0 && player && player->scenarioGateway)
        {
            ObjectState* egoState = player->scenarioGateway->getObjectStatePtrByIdx(0);
            if (egoState)
            {
                actual_id = egoState->state_.info.id;
            }
        }

        if (actual_id >= 0)
        {
            gt_esmini::GT_HostVehicleReporter::Instance().SetPowertrain(actual_id, rpm, torque);
        }
    }
#endif
}

