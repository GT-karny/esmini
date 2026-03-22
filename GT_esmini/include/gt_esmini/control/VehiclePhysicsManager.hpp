/*
 * GT_esmini - Extended esmini with Vehicle Physics
 * https://github.com/esmini/esmini
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2024 GT_esmini contributors
 */

#pragma once

#include "gt_esmini/control/ObservedVehiclePhysics.hpp"

#include <string>
#include <vector>

// Forward declarations (avoid including EnvironmentSimulator headers here)
namespace scenarioengine
{
class Entities;
class Object;
} // namespace scenarioengine

namespace gt_esmini
{

/// Applies observation-based vehicle physics (pitch/roll) to all vehicles
/// that do NOT have a GT custom controller (RealDriver / PythonDriver).
/// Follows the same singleton + post-processing pattern as AutoLightManager.
class VehiclePhysicsManager
{
public:
    static VehiclePhysicsManager& Instance();

    /// Scan entities and create physics entries for non-GT-controller vehicles.
    /// @param entities   Pointer to scenario entities (lifetime must exceed manager)
    void Init(scenarioengine::Entities* entities);

    /// Advance physics for all tracked vehicles and set pitch/roll on objects.
    void Update(double dt);

    /// Clear all tracked vehicles.
    void Close();

    void Enable(bool enable) { enabled_ = enable; }
    bool IsEnabled() const { return enabled_; }

    /// Load per-category parameter profiles from the observed_vehicle section
    /// of real_vehicle_params.json.
    void LoadProfiles(const std::string& configPath);

private:
    VehiclePhysicsManager() = default;

    /// Resolve parameters for a given vehicle category enum value.
    ObservedVehiclePhysics::Params ResolveParams(int category) const;

    /// Convert Vehicle::Category int to JSON key string.
    static std::string CategoryToKey(int category);

    struct Entry
    {
        int                    objectId;
        scenarioengine::Object* object;  // non-owning
        ObservedVehiclePhysics physics;
    };

    std::vector<Entry> entries_;
    bool enabled_ = false;  // off until explicitly enabled via CLI flag

    // Per-category parameter profiles loaded from config
    ObservedVehiclePhysics::Params defaultParams_;
    struct CategoryProfile
    {
        std::string key;
        ObservedVehiclePhysics::Params params;
    };
    std::vector<CategoryProfile> categoryProfiles_;
    bool profilesLoaded_ = false;
};

} // namespace gt_esmini
