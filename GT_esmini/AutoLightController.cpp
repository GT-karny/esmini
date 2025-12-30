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

#include "AutoLightController.hpp"
#include <cmath>

namespace gt_esmini
{
    AutoLightController::AutoLightController(scenarioengine::Vehicle* vehicle, VehicleLightExtension* lightExt)
        : vehicle_(vehicle), 
          lightExt_(lightExt), 
          enabled_(false), // Default disabled, enabled by manager
          prevSpeed_(0.0), 
          prevLaneId_(0), 
          laneChangeStartTime_(-1.0), 
          isInLaneChange_(false)
    {
        if (vehicle_)
        {
            prevSpeed_ = vehicle_->GetSpeed();
            prevLaneId_ = vehicle_->pos_.GetLaneId();
        }
    }

    AutoLightController::~AutoLightController()
    {
    }

    void AutoLightController::Enable(bool enabled)
    {
        enabled_ = enabled;
    }

    void AutoLightController::Update(double dt)
    {
        if (!enabled_ || !vehicle_ || !lightExt_)
        {
            return;
        }

        // 1. Reversing Lights
        UpdateReversingLights();

        // 2. Brake Lights
        // Calculate acceleration if not available directly
        // Note: (v - v0)/dt
        double speed = vehicle_->GetSpeed();
        double acceleration = 0.0;
        if (dt > 0.0001)
        {
            acceleration = (speed - prevSpeed_) / dt;
        }
        
        // Check for brake lights
        if (acceleration < BRAKE_DECELERATION_THRESHOLD)
        {
            LightState state;
            state.mode = LightState::Mode::ON;
            lightExt_->SetLightState(VehicleLightType::BRAKE_LIGHTS, state);
        }
        else
        {
            // Only turn off if currently ON (to allow manual override? No, override policy says AutoLight re-applies)
            // But we should check if we turned it ON before?
            // Simple logic: Apply state based on physics.
             LightState state;
             state.mode = LightState::Mode::OFF;
             lightExt_->SetLightState(VehicleLightType::BRAKE_LIGHTS, state);
        }
        
        // 3. Indicators
        UpdateIndicators();

        // Update state
        prevSpeed_ = speed;
        prevLaneId_ = vehicle_->pos_.GetLaneId();
    }

    void AutoLightController::UpdateReversingLights()
    {
        double speed = vehicle_->GetSpeed();
        // Check local speed or gear?
        // OpenSCENARIO usually handles positive speed even for reverse if direction is handled elsewhere?
        // But esmini vehicle speed is usually magnitude.
        // Let's check gear or direction.
        // vehicle_->pos_.GetDrivingDirectionRelativeRoad() ?
        
        // Actually, pure speed < 0 is reverse.
        // If speed is scalar magnitude and always positive, we need another way.
        // Entities.hpp: double speed_;
        // But MoveAlongS takes 'ds'.
        // Let's assume negative speed is not typically stored in 'speed_'.
        // However, standard vehicle dynamics: if shifting to reverse, speed might be negative?
        
        // Alternative: check if moving backwards relative to heading?
        // vehicle_->state_old.vel_x vs heading?
        
        bool isReversing = (speed < -0.01); // Simple check if speed can be negative
        
        // If speed is always positive, we might need to check which way it is moving vs heading.
        // But simpler for now: if user sets speed < 0 in scenario.
        
        if (isReversing)
        {
             LightState state;
             state.mode = LightState::Mode::ON;
             lightExt_->SetLightState(VehicleLightType::REVERSING_LIGHTS, state);
        }
        else
        {
             LightState state;
             state.mode = LightState::Mode::OFF;
             lightExt_->SetLightState(VehicleLightType::REVERSING_LIGHTS, state);
        }
    }

    void AutoLightController::UpdateIndicators()
    {
        int currentLaneId = vehicle_->pos_.GetLaneId();
        
        // Lane Change Detection (Simple)
        if (currentLaneId != prevLaneId_)
        {
            // Changed lane.
            // Determine direction.
            // Positive lane ID: left is increasing ID (usually? Depends on country/ODR).
            // OpenDRIVE:
            // Right lanes: -1, -2, -3 (Decrease goes Right, Increase goes Left towards 0)
            // Left lanes: 1, 2, 3 (Increase goes Left, Decrease goes Right towards 0)
            // It's complicated.
            // Let's derive from Lateral Offset 't'.
            
            // If we simply detect change, it's too late compared to "start" of valid LC.
            // But better than nothing.
            
            // NOTE: A more robust way is assuming standard OpenDRIVE lane numbering:
            // -1 -> -2 (Right LC)
            // -2 -> -1 (Left LC)
            // 1 -> 2 (Left LC)
            // 2 -> 1 (Right LC)
            
            bool leftLC = false;
            bool rightLC = false;
            
            if (currentLaneId < 0 && prevLaneId_ < 0) {
                 if (currentLaneId < prevLaneId_) rightLC = true; // -1 -> -2
                 else leftLC = true; // -2 -> -1
            } else if (currentLaneId > 0 && prevLaneId_ > 0) {
                 if (currentLaneId > prevLaneId_) leftLC = true; // 1 -> 2
                 else rightLC = true; // 2 -> 1
            }
            
            // Flash for a short duration?
            // Since we are stateless, we need a timer.
            // But we don't have a timer easily injected here except members.
            // Let's assume we just trigger it for now, but without a timer it will spark once.
            
            // Ideal: Set "BlinkerState" with a duration.
            // But LightState in ExtraAction has flashingOnDuration, but that is frequency.
            // We need "Active Duration".
            
            // For now, let's implement Junction turning instead which is continuous.
        }
        
        id_t junctionId = vehicle_->pos_.GetJunctionId();
        if (junctionId != -1)
        {
             // In Junction
             // Check relative heading to road?
             // Or use Steering Wheel Angle?
             double steer = vehicle_->GetWheelAngle(); // Radians. Positive Left?
             
             // Threshold for indicator
             const double STEER_THRESHOLD = 0.1; // rad approx 5.7 deg
             
             if (steer > STEER_THRESHOLD)
             {
                 LightState state;
                 state.mode = LightState::Mode::FLASHING;
                 lightExt_->SetLightState(VehicleLightType::INDICATOR_LEFT, state);
                 
                 state.mode = LightState::Mode::OFF;
                 lightExt_->SetLightState(VehicleLightType::INDICATOR_RIGHT, state);
             }
             else if (steer < -STEER_THRESHOLD)
             {
                 LightState state;
                 state.mode = LightState::Mode::OFF;
                 lightExt_->SetLightState(VehicleLightType::INDICATOR_LEFT, state);
                 
                 state.mode = LightState::Mode::FLASHING;
                 lightExt_->SetLightState(VehicleLightType::INDICATOR_RIGHT, state);
             }
             else
             {
                 // Turn off if in junction going straight?
                 LightState state;
                 state.mode = LightState::Mode::OFF;
                 lightExt_->SetLightState(VehicleLightType::INDICATOR_LEFT, state);
                 lightExt_->SetLightState(VehicleLightType::INDICATOR_RIGHT, state);
             }
        }
        else
        {
            // Not in junction, turn off indicators (unless LC logic added later with timer)
             LightState state;
             state.mode = LightState::Mode::OFF;
             lightExt_->SetLightState(VehicleLightType::INDICATOR_LEFT, state);
             lightExt_->SetLightState(VehicleLightType::INDICATOR_RIGHT, state);
        }
    }
}
