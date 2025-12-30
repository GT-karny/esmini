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

        for (auto* obj : entities->object_)
        {
            if (obj && obj->type_ == scenarioengine::Object::Type::VEHICLE)
            {
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
    AutoLightManager() : enabled_(false) {}
    
    std::vector<std::unique_ptr<gt_esmini::AutoLightController>> controllers_;
    bool enabled_;
};

// --- GT_esminiLib C-API Implementation ---

GT_ESMINI_API int GT_Init(const char* oscFilename, int disable_ctrls)
{
    // 1. Initialize esmini using standard SE_Init (which we compiled in)
    int ret = SE_Init(oscFilename, disable_ctrls, 0, 0, 0); 
    if (ret != 0)
    {
        return ret;
    }

    // 2. Perform Delta Parsing for Extensions
    if (player && player->scenarioEngine)
    {
        // Load XML independently
        pugi::xml_document doc;
        pugi::xml_parse_result result = doc.load_file(oscFilename);
        
        if (result)
        {
            // Use GT_ScenarioReader to parse extensions
            // Access Catalogs via existing loader because it's private in Engine
            auto* catalogs = player->scenarioEngine->GetScenarioReader()->GetCatalogs();
            
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

        // 3. Initialize AutoLightManager
        AutoLightManager::Instance().Init(&player->scenarioEngine->entities_);
    }

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
