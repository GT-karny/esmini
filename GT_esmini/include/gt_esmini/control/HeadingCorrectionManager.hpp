/*
 * GT_esmini - Extended esmini with Heading Correction
 * https://github.com/esmini/esmini
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2024 GT_esmini contributors
 */

#pragma once

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

/// Applies front-axle-anchored heading correction to all vehicles
/// that do NOT have a GT custom controller (RealDriver / PythonDriver / ManualDrive).
/// Looks ahead on the road by wheelbase distance to find the front axle's road position,
/// then sets heading = direction from rear to front. This anchors the front wheels on the
/// road path so the nose leads turns naturally, matching real vehicle behavior.
/// Follows the same singleton + post-processing pattern as VehiclePhysicsManager.
class HeadingCorrectionManager
{
public:
    struct Params
    {
        double blend_factor       = 1.0;  // how much of the geometric correction to apply (1.0 = full)
        double max_correction_deg = 5.0;  // safety clamp (degrees)
    };

    static HeadingCorrectionManager& Instance();

    /// Scan entities and create entries for non-GT-controller vehicles.
    /// @param entities   Pointer to scenario entities (lifetime must exceed manager)
    void Init(scenarioengine::Entities* entities);

    /// Compute and apply heading correction for all tracked vehicles.
    void Update(double dt);

    /// Clear all tracked vehicles.
    void Close();

    void Enable(bool enable) { enabled_ = enable; }
    bool IsEnabled() const { return enabled_; }

    /// Load per-category parameter profiles from the heading_correction section
    /// of real_vehicle_params.json.
    void LoadProfiles(const std::string& configPath);

private:
    HeadingCorrectionManager() = default;

    /// Resolve parameters for a given vehicle category enum value.
    Params ResolveParams(int category) const;

    /// Convert Vehicle::Category int to JSON key string.
    static std::string CategoryToKey(int category);

    struct Entry
    {
        int                     objectId;
        scenarioengine::Object* object;  // non-owning
        double                  wheelbase;
        Params                  params;
    };

    std::vector<Entry> entries_;
    bool               enabled_ = false;  // off until explicitly enabled via CLI flag

    // Per-category parameter profiles loaded from config
    Params defaultParams_;
    struct CategoryProfile
    {
        std::string key;
        Params      params;
    };
    std::vector<CategoryProfile> categoryProfiles_;
    bool                         profilesLoaded_ = false;
};

} // namespace gt_esmini
