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

#include "ExtraEntities.hpp"

namespace gt_esmini
{
    // ========== VehicleLightExtension ==========

    VehicleLightExtension::VehicleLightExtension(scenarioengine::Vehicle* vehicle) : vehicle_(vehicle), autoLightEnabled_(false)
    {
        // Phase 1: Stub implementation
        // Initialize all lights to OFF state
        for (int i = 0; i < static_cast<int>(VehicleLightType::SPECIAL_PURPOSE_LIGHTS) + 1; ++i)
        {
            LightState state;
            state.mode = LightState::Mode::OFF;
            lightStates_[static_cast<VehicleLightType>(i)] = state;
        }
    }

    VehicleLightExtension::~VehicleLightExtension()
    {
        // Phase 1: Stub implementation
    }

    void VehicleLightExtension::SetLightState(VehicleLightType type, const LightState& state)
    {
        // Phase 1: Stub implementation
        // Phase 2: Implement actual light state change logic
        lightStates_[type] = state;
    }

    LightState VehicleLightExtension::GetLightState(VehicleLightType type) const
    {
        // Phase 1: Stub implementation
        auto it = lightStates_.find(type);
        if (it != lightStates_.end())
        {
            return it->second;
        }

        // Default is OFF
        LightState state;
        state.mode = LightState::Mode::OFF;
        return state;
    }

    // ========== VehicleExtensionManager ==========

    VehicleExtensionManager& VehicleExtensionManager::Instance()
    {
        static VehicleExtensionManager instance;
        return instance;
    }

    VehicleLightExtension* VehicleExtensionManager::GetExtension(scenarioengine::Vehicle* vehicle)
    {
        // Phase 1: Stub implementation
        auto it = extensions_.find(vehicle);
        if (it != extensions_.end())
        {
            return it->second.get();
        }
        return nullptr;
    }

    void VehicleExtensionManager::RegisterExtension(scenarioengine::Vehicle* vehicle, VehicleLightExtension* ext)
    {
        // Phase 1: Stub implementation
        // Phase 2: Add handling for existing extensions
        extensions_[vehicle] = std::unique_ptr<VehicleLightExtension>(ext);
    }

    void VehicleExtensionManager::Clear()
    {
        // Phase 1: Stub implementation
        extensions_.clear();
    }

}  // namespace gt_esmini
