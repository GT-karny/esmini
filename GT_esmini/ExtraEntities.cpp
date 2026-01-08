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

#ifdef Object
#undef Object
#endif
#include "ExtraEntities.hpp"

namespace gt_esmini
{
    // ========== VehicleLightExtension ==========
    // Methods moved to header (inlined) for cross-module linkage support without dllexport.
    // See ExtraEntities.hpp

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
        // Phase 1: Stub implementation (Corrected)
        // Phase 2: Add handling for existing extensions
        extensions_[vehicle] = std::unique_ptr<VehicleLightExtension>(ext);
    }

    void VehicleExtensionManager::Clear()
    {
        // Phase 1: Stub implementation
        extensions_.clear();
    }

}  // namespace gt_esmini
