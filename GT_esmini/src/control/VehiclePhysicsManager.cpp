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

#include "gt_esmini/control/VehiclePhysicsManager.hpp"

#include "Entities.hpp"
#include "gt_esmini/control/ControllerRealDriver.hpp"
#include "gt_esmini/control/ControllerPythonDriver.hpp"

#include <fstream>
#include <iostream>
#include <algorithm>
#include <cmath>

namespace gt_esmini
{

VehiclePhysicsManager& VehiclePhysicsManager::Instance()
{
    static VehiclePhysicsManager instance;
    return instance;
}

void VehiclePhysicsManager::Init(scenarioengine::Entities* entities)
{
    entries_.clear();

    if (!entities)
    {
        return;
    }

    for (auto* obj : entities->object_)
    {
        if (!obj || obj->type_ != scenarioengine::Object::Type::VEHICLE)
        {
            continue;
        }

        // Skip vehicles that already have a GT custom controller
        // (they manage their own pitch/roll via RealVehicle)
        bool hasGTController = false;
        if (obj->GetController(CONTROLLER_REAL_DRIVER_TYPE_NAME) ||
            obj->GetController(CONTROLLER_PYTHON_DRIVER_TYPE_NAME))
        {
            hasGTController = true;
        }

        if (hasGTController)
        {
            continue;
        }

        Entry entry;
        entry.objectId = obj->GetId();
        entry.object   = obj;
        entry.physics  = ObservedVehiclePhysics(ResolveParams(obj->category_));

        entries_.push_back(entry);
    }

    if (!entries_.empty())
    {
        std::cout << "VehiclePhysicsManager: Tracking " << entries_.size()
                  << " vehicle(s) for observed physics" << std::endl;
    }
}

void VehiclePhysicsManager::Update(double dt)
{
    if (!enabled_ || entries_.empty())
    {
        return;
    }

    for (auto& entry : entries_)
    {
        auto* obj = entry.object;
        if (!obj)
        {
            continue;
        }

        // Read accelerations computed by esmini from trajectory
        double long_acc = obj->pos_.GetAccLong();
        double lat_acc  = obj->pos_.GetAccLat();

        // Advance spring-damper model
        entry.physics.Update(dt, long_acc, lat_acc);

        // Set dynamic pitch/roll as relative values on the Position object.
        // esmini's road alignment already provides the terrain (road) pitch/roll,
        // so we only add the dynamic suspension component as a relative offset.
        // Using SetPitchRelative/SetRollRelative avoids conflicting with
        // esmini's internal road alignment and DefaultController trajectory control.
        obj->pos_.SetPitchRelative(entry.physics.GetDynamicPitch());
        obj->pos_.SetRollRelative(entry.physics.GetDynamicRoll());
    }
}

void VehiclePhysicsManager::Close()
{
    entries_.clear();
    profilesLoaded_ = false;
    categoryProfiles_.clear();
}

void VehiclePhysicsManager::LoadProfiles(const std::string& configPath)
{
    // Parse real_vehicle_params.json and extract "observed_vehicle" section.
    // Uses same simple line-based parser approach as RealVehicle::LoadParameters.
    std::ifstream file(configPath);
    if (!file.is_open())
    {
        return;
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    file.close();

    // Find "observed_vehicle" section
    size_t ov_pos = content.find("\"observed_vehicle\"");
    if (ov_pos == std::string::npos)
    {
        return;
    }

    // Find the opening brace of observed_vehicle object
    size_t ov_brace = content.find('{', ov_pos);
    if (ov_brace == std::string::npos)
    {
        return;
    }

    // Helper: parse a double value for a given key in a substring
    auto parseDouble = [](const std::string& text, const std::string& key, double fallback) -> double {
        size_t pos = text.find("\"" + key + "\"");
        if (pos == std::string::npos)
        {
            return fallback;
        }
        size_t colon = text.find(':', pos);
        if (colon == std::string::npos)
        {
            return fallback;
        }
        try
        {
            return std::stod(text.substr(colon + 1));
        }
        catch (...)
        {
            return fallback;
        }
    };

    // Helper: extract a brace-delimited block starting at a given position
    auto extractBlock = [&content](size_t start) -> std::string {
        if (start >= content.size() || content[start] != '{')
        {
            return "";
        }
        int depth = 0;
        size_t end = start;
        for (; end < content.size(); ++end)
        {
            if (content[end] == '{') depth++;
            else if (content[end] == '}') { depth--; if (depth == 0) break; }
        }
        return content.substr(start, end - start + 1);
    };

    // Helper: parse params from a JSON block
    auto parseParams = [&parseDouble](const std::string& block,
                                       const ObservedVehiclePhysics::Params& fallback)
        -> ObservedVehiclePhysics::Params {
        ObservedVehiclePhysics::Params p;
        p.pitch_stiffness = parseDouble(block, "pitch_stiffness", fallback.pitch_stiffness);
        p.pitch_damping   = parseDouble(block, "pitch_damping",   fallback.pitch_damping);
        p.roll_stiffness  = parseDouble(block, "roll_stiffness",  fallback.roll_stiffness);
        p.roll_damping    = parseDouble(block, "roll_damping",    fallback.roll_damping);
        p.mass_height     = parseDouble(block, "mass_height",     fallback.mass_height);
        p.max_pitch_deg   = parseDouble(block, "max_pitch_deg",   fallback.max_pitch_deg);
        p.max_roll_deg    = parseDouble(block, "max_roll_deg",    fallback.max_roll_deg);
        return p;
    };

    std::string ovBlock = extractBlock(ov_brace);

    // Parse "default" sub-block
    size_t defPos = ovBlock.find("\"default\"");
    if (defPos != std::string::npos)
    {
        size_t defBrace = ovBlock.find('{', defPos);
        if (defBrace != std::string::npos)
        {
            std::string defBlock = extractBlock(defBrace);
            defaultParams_ = parseParams(defBlock, defaultParams_);
        }
    }

    // Parse category sub-blocks
    const char* categories[] = {"car", "truck", "bus", "van", "motorbike",
                                "bicycle", "semitrailer", "trailer"};
    for (const char* cat : categories)
    {
        std::string key = std::string("\"") + cat + "\"";
        size_t catPos = ovBlock.find(key);
        if (catPos != std::string::npos)
        {
            size_t catBrace = ovBlock.find('{', catPos);
            if (catBrace != std::string::npos)
            {
                std::string catBlock = extractBlock(catBrace);
                // Fallback to default for unspecified keys
                CategoryProfile profile;
                profile.key    = cat;
                profile.params = parseParams(catBlock, defaultParams_);
                categoryProfiles_.push_back(profile);
            }
        }
    }

    profilesLoaded_ = true;
    std::cout << "VehiclePhysicsManager: Loaded profiles ("
              << categoryProfiles_.size() << " category overrides)" << std::endl;
}

ObservedVehiclePhysics::Params VehiclePhysicsManager::ResolveParams(int category) const
{
    if (!profilesLoaded_)
    {
        return defaultParams_;
    }

    std::string key = CategoryToKey(category);
    for (const auto& profile : categoryProfiles_)
    {
        if (profile.key == key)
        {
            return profile.params;
        }
    }
    return defaultParams_;
}

std::string VehiclePhysicsManager::CategoryToKey(int category)
{
    // Maps scenarioengine::Vehicle::Category enum to JSON key
    switch (category)
    {
        case 0: return "car";
        case 1: return "van";
        case 2: return "truck";
        case 3: return "semitrailer";
        case 4: return "trailer";
        case 5: return "bus";
        case 6: return "motorbike";
        case 7: return "bicycle";
        default: return "default";
    }
}

} // namespace gt_esmini
