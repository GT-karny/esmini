/*
 * GT_esmini - Extended esmini with HostVehicleData Functionality
 * https://github.com/esmini/esmini
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2024 GT_esmini contributors
 */

#include "gt_esmini/osi/HVDEstimator.hpp"
#include "Entities.hpp"
#include "gt_esmini/scenario/ExtraEntities.hpp"

#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace gt_esmini
{

HVDEstimator::EstimatedInputs HVDEstimator::Estimate(scenarioengine::Object* obj, double dt)
{
    EstimatedInputs result;
    if (!obj)
    {
        return result;
    }

    // Non-vehicle objects get defaults only
    if (obj->type_ != scenarioengine::Object::Type::VEHICLE)
    {
        return result;
    }

    int    id        = obj->GetId();
    double speed     = obj->GetSpeed();
    double abs_speed = std::abs(speed);

    // --- Gear estimation ---
    if (abs_speed < kSpeedThreshold)
    {
        result.gear = 0;  // neutral at standstill
    }
    else if (speed < 0.0)
    {
        result.gear = -1;  // reverse
    }
    else
    {
        result.gear = 1;  // forward
    }

    // --- Acceleration estimation ---
    auto& vc = cache_[id];
    double acceleration = 0.0;

    if (vc.initialized && dt > 1e-6)
    {
        acceleration = (speed - vc.prev_speed) / dt;
    }
    vc.prev_speed  = speed;
    vc.initialized = true;

    // --- Throttle / Brake estimation ---
    double max_acc = obj->GetMaxAcceleration();
    double max_dec = obj->GetMaxDeceleration();
    if (max_acc <= 0.0)
    {
        max_acc = kDefaultMaxAcc;
    }
    if (max_dec <= 0.0)
    {
        max_dec = kDefaultMaxDec;
    }

    double drag_acc = kDragCoeff * speed * speed;  // always >= 0

    if (abs_speed < kSpeedThreshold)
    {
        // Standing still: hold brake
        result.throttle = 0.0;
        result.brake    = 1.0;
    }
    else if (acceleration > 0.01)
    {
        // Accelerating: engine must overcome drag + provide net acceleration
        double needed   = acceleration + drag_acc;
        result.throttle = std::min(std::max(needed / max_acc, 0.0), 1.0);
        result.brake    = 0.0;
    }
    else if (acceleration < -0.01)
    {
        // Decelerating: drag helps, brake covers the rest
        double brake_decel = std::abs(acceleration) - drag_acc;
        if (brake_decel > 0.0)
        {
            result.brake = std::min(brake_decel / max_dec, 1.0);
        }
        result.throttle = 0.0;
    }
    else
    {
        // Cruising: throttle just compensates drag
        if (abs_speed > 0.5)
        {
            result.throttle = std::min(drag_acc / max_acc, 1.0);
        }
        result.brake = 0.0;
    }

    // --- Steering ---
    result.steering = obj->GetWheelAngle();

    // --- RPM estimation ---
    result.rpm = EstimateRPM(abs_speed);

    // --- Torque estimation ---
    result.torque = EstimateTorque(result.rpm);

    // --- Light mask ---
    result.lightMask = BuildLightMaskForObject(obj);

    return result;
}

double HVDEstimator::EstimateRPM(double abs_speed) const
{
    // speed → wheel rps → engine RPM via gear ratio
    double wheel_rps  = abs_speed / (2.0 * M_PI * kWheelRadius);
    double engine_rpm = wheel_rps * 60.0 * kGearRatio;

    engine_rpm = std::max(engine_rpm, kIdleRPM);
    engine_rpm = std::min(engine_rpm, kMaxRPM);
    return engine_rpm;
}

double HVDEstimator::EstimateTorque(double rpm) const
{
    // Same parabolic torque curve as RealVehicle::GetTorque
    double normalized_rpm = (rpm - kIdleRPM) / (kMaxRPM - kIdleRPM);
    normalized_rpm        = std::max(0.0, std::min(1.0, normalized_rpm));
    return 0.4 + 0.6 * (4.0 * normalized_rpm * (1.0 - normalized_rpm));
}

int HVDEstimator::BuildLightMaskForObject(scenarioengine::Object* obj)
{
    if (!obj)
    {
        return 0;
    }

    auto* vehicle = dynamic_cast<scenarioengine::Vehicle*>(obj);
    if (!vehicle)
    {
        return 0;
    }

    auto* ext = VehicleExtensionManager::Instance().GetExtension(vehicle);
    if (!ext)
    {
        return 0;
    }

    auto is_on = [&](VehicleLightType type) -> bool {
        return ext->GetLightState(type).mode == LightState::Mode::ON;
    };

    int mask = 0;
    if (is_on(VehicleLightType::LOW_BEAM))
    {
        mask |= 1;
    }
    if (is_on(VehicleLightType::HIGH_BEAM))
    {
        mask |= 2;
    }
    if (is_on(VehicleLightType::INDICATOR_LEFT))
    {
        mask |= 4;
    }
    if (is_on(VehicleLightType::INDICATOR_RIGHT))
    {
        mask |= 8;
    }
    if (is_on(VehicleLightType::FOG_LIGHTS) || is_on(VehicleLightType::FOG_LIGHTS_FRONT) ||
        is_on(VehicleLightType::FOG_LIGHTS_REAR))
    {
        mask |= 16;
    }
    return mask;
}

void HVDEstimator::Reset()
{
    cache_.clear();
}

} // namespace gt_esmini
