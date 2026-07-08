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

#include "gt_esmini/control/HeadingCorrectionManager.hpp"

#include "Entities.hpp"
#include "RoadManager.hpp"
#include "gt_esmini/control/ControllerRealDriver.hpp"
#include "gt_esmini/control/ControllerPythonDriver.hpp"
#include "gt_esmini/control/ControllerManualDrive.hpp"
#include "gt_esmini/control/ControllerKinematic.hpp"

#include <fstream>
#include <iostream>
#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace gt_esmini
{

// Normalize angle to [-PI, +PI]
static double NormalizeAngle(double angle)
{
    while (angle > M_PI)
    {
        angle -= 2.0 * M_PI;
    }
    while (angle < -M_PI)
    {
        angle += 2.0 * M_PI;
    }
    return angle;
}

HeadingCorrectionManager& HeadingCorrectionManager::Instance()
{
    static HeadingCorrectionManager instance;
    return instance;
}

void HeadingCorrectionManager::Init(scenarioengine::Entities* entities)
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
        // (they manage their own heading via RealVehicle dynamics)
        // Note: SUMO vehicles are NOT skipped — SUMO also sets heading based on
        // velocity direction without bicycle-model correction, causing the same
        // "no nose-leading" issue. Our correction is applied after SUMO's H_ABS
        // heading via SetHeadingRelative(), adding the curvature offset on top.
        if (obj->GetController(CONTROLLER_REAL_DRIVER_TYPE_NAME) ||
#ifdef GT_ENABLE_EMBEDDED_PYTHON
            obj->GetController(CONTROLLER_PYTHON_DRIVER_TYPE_NAME) ||
#endif
            obj->GetController(CONTROLLER_MANUAL_DRIVE_TYPE_NAME) ||
            obj->GetController(CONTROLLER_KINEMATIC_TYPE_NAME))
        {
            continue;
        }

        auto* vehicle = static_cast<scenarioengine::Vehicle*>(obj);
        double wheelbase = vehicle->front_axle_.positionX - vehicle->rear_axle_.positionX;

        // Skip vehicles with invalid or zero wheelbase
        if (wheelbase < 0.1)
        {
            continue;
        }

        Entry entry;
        entry.objectId  = obj->GetId();
        entry.object    = obj;
        entry.wheelbase = wheelbase;
        entry.params    = ResolveParams(obj->category_);

        entries_.push_back(entry);
    }

    if (!entries_.empty())
    {
        std::cout << "HeadingCorrectionManager: Tracking " << entries_.size()
                  << " vehicle(s) for heading correction" << std::endl;
    }
}

void HeadingCorrectionManager::Update(double /*dt*/)
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

        double L = entry.wheelbase;
        const auto& params = entry.params;

        // --- Core idea ---
        // esmini places the rear axle on the road at (x, y) with heading = road tangent.
        // We look ahead on the road by L (wheelbase) to find where the front axle SHOULD be,
        // then set heading = direction from rear to that front road position.
        // This anchors the front axle on the road; the rear naturally trails.

        // Create a lookahead position: copy current pos and advance by wheelbase along the road
        roadmanager::Position frontPos = obj->pos_;
        frontPos.MoveAlongS(L);

        // Direction from rear axle to front axle road position
        double dx = frontPos.GetX() - obj->pos_.GetX();
        double dy = frontPos.GetY() - obj->pos_.GetY();
        double h_to_front = atan2(dy, dx);

        // Road heading at rear axle (strip off any existing relative heading like lane-change offset)
        double h_road = NormalizeAngle(obj->pos_.GetH() - obj->pos_.GetHRelative());

        // The curvature-induced correction: difference between
        // "direction to front road point" and "road tangent at rear"
        double delta_h = NormalizeAngle(h_to_front - h_road) * params.blend_factor;

        // Clamp
        double max_rad = params.max_correction_deg * M_PI / 180.0;
        delta_h = std::max(-max_rad, std::min(max_rad, delta_h));

        // Apply on top of existing h_relative (preserves lane-change heading offset etc.)
        if (fabs(delta_h) > 1e-8)
        {
            obj->pos_.SetHeadingRelative(obj->pos_.GetHRelative() + delta_h);
        }
    }
}

void HeadingCorrectionManager::Close()
{
    entries_.clear();
    profilesLoaded_ = false;
    categoryProfiles_.clear();
}

void HeadingCorrectionManager::LoadProfiles(const std::string& configPath)
{
    std::ifstream file(configPath);
    if (!file.is_open())
    {
        return;
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    file.close();

    // Find "heading_correction" section
    size_t hc_pos = content.find("\"heading_correction\"");
    if (hc_pos == std::string::npos)
    {
        return;
    }

    size_t hc_brace = content.find('{', hc_pos);
    if (hc_brace == std::string::npos)
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
        int    depth = 0;
        size_t end   = start;
        for (; end < content.size(); ++end)
        {
            if (content[end] == '{')
                depth++;
            else if (content[end] == '}')
            {
                depth--;
                if (depth == 0)
                    break;
            }
        }
        return content.substr(start, end - start + 1);
    };

    // Helper: parse params from a JSON block
    auto parseParams = [&parseDouble](const std::string&                     block,
                                       const HeadingCorrectionManager::Params& fallback) -> HeadingCorrectionManager::Params {
        HeadingCorrectionManager::Params p;
        p.blend_factor       = parseDouble(block, "blend_factor", fallback.blend_factor);
        p.max_correction_deg = parseDouble(block, "max_correction_deg", fallback.max_correction_deg);
        return p;
    };

    std::string hcBlock = extractBlock(hc_brace);

    // Parse "default" sub-block.
    // Note: positions found in hcBlock are relative to hcBlock, but extractBlock
    // operates on `content`. Add hc_brace to convert to content-absolute positions.
    size_t defPos = hcBlock.find("\"default\"");
    if (defPos != std::string::npos)
    {
        size_t defBrace = hcBlock.find('{', defPos);
        if (defBrace != std::string::npos)
        {
            std::string defBlock = extractBlock(hc_brace + defBrace);
            defaultParams_       = parseParams(defBlock, defaultParams_);
        }
    }

    // Parse category sub-blocks
    const char* categories[] = {"car", "truck", "bus", "van", "motorbike", "bicycle", "semitrailer", "trailer"};
    for (const char* cat : categories)
    {
        std::string key    = std::string("\"") + cat + "\"";
        size_t      catPos = hcBlock.find(key);
        if (catPos != std::string::npos)
        {
            size_t catBrace = hcBlock.find('{', catPos);
            if (catBrace != std::string::npos)
            {
                std::string     catBlock = extractBlock(hc_brace + catBrace);
                CategoryProfile profile;
                profile.key    = cat;
                profile.params = parseParams(catBlock, defaultParams_);
                categoryProfiles_.push_back(profile);
            }
        }
    }

    profilesLoaded_ = true;
    std::cout << "HeadingCorrectionManager: Loaded profiles ("
              << categoryProfiles_.size() << " category overrides)"
              << " | default blend_factor=" << defaultParams_.blend_factor
              << " max_correction_deg=" << defaultParams_.max_correction_deg
              << std::endl;
}

HeadingCorrectionManager::Params HeadingCorrectionManager::ResolveParams(int category) const
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

std::string HeadingCorrectionManager::CategoryToKey(int category)
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
