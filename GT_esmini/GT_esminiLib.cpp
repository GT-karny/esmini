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

#include "GT_esminiLib.hpp"
#include "GT_ScenarioReader.hpp"
#include "ExtraEntities.hpp"
#include "AutoLightController.hpp"
#include <map>
#include <memory>

namespace gt_esmini
{
    // GT_esmini player management
    static gt_esmini::GT_ScenarioReader* gt_reader = nullptr;

    /**
     * @brief AutoLightController manager class
     */
    class AutoLightManager
    {
    public:
        static AutoLightManager& Instance()
        {
            static AutoLightManager instance;
            return instance;
        }

        void InitAutoLight(scenarioengine::Entities* entities)
        {
            // Phase 1: Stub implementation
            // Phase 3: Implement actual initialization logic
            (void)entities;  // Suppress unused warning
        }

        void UpdateAutoLight(double dt)
        {
            // Phase 1: Stub implementation
            // Phase 3: Implement actual update logic
            (void)dt;  // Suppress unused warning
        }

        void Clear()
        {
            // Phase 1: Stub implementation
            controllers_.clear();
        }

    private:
        std::map<scenarioengine::Vehicle*, std::unique_ptr<AutoLightController>> controllers_;
    };

}  // namespace gt_esmini

// C API implementation

int GT_Init(const char* oscFilename, int disable_ctrls)
{
    // Phase 1: Stub implementation
    // Phase 3: Implement actual initialization logic
    //
    // Planned implementation:
    // - Use GT_ScenarioReader based on esmini's initialization logic
    // - Check --auto-light argument
    // - Initialize AutoLight after entity creation

    (void)oscFilename;    // Suppress unused warning
    (void)disable_ctrls;  // Suppress unused warning

    return 0;  // Success
}

void GT_Step(double dt)
{
    // Phase 1: Stub implementation
    // Phase 3: Implement AutoLight update logic
    gt_esmini::AutoLightManager::Instance().UpdateAutoLight(dt);
}

void GT_EnableAutoLight()
{
    // Phase 1: Stub implementation
    // Phase 3: Implement AutoLight enable logic
    //
    // Planned implementation:
    // - Get entities from SE_GetScenarioEngine()
    // - AutoLightManager::Instance().InitAutoLight(entities);
}

void GT_Close()
{
    // Phase 1: Stub implementation
    // Phase 3: Implement resource cleanup logic
    gt_esmini::AutoLightManager::Instance().Clear();
    gt_esmini::VehicleExtensionManager::Instance().Clear();

    if (gt_esmini::gt_reader != nullptr)
    {
        delete gt_esmini::gt_reader;
        gt_esmini::gt_reader = nullptr;
    }
}
