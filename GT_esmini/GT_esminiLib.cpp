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
    }

    return 0;
}

GT_ESMINI_API int GT_InitWithArgs(int argc, const char* argv[])
{
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

    // If filename found, sanitized it
    std::string sanitizedFile;
    std::vector<const char*> newArgv;
    std::vector<std::string> argStorage; // to keep strings alive

    if (filename)
    {
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

                if (strcmp(argv[i], "--osi") == 0 || strcmp(argv[i], "--hz") == 0)
                {
                    i++; // Skip the argument value (IP or frequency)
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

    // 2. Initialize esmini using SE_Init with sanitized args
    int ret = SE_InitWithArgs(newArgv.size(), newArgv.data());
    
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
