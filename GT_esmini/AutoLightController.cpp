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
          isInLaneChange_(false),
          smoothedAcc_(0.0),
          lastBrakeState_(LightState::Mode::OFF),
          indicatorState_(IndicatorState::OFF),
          indicatorTimer_(0.0),
          laneChangeDetectTime_(0.0),
          timeSinceLastUpdate_(0.0)
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

        // Frequency Limiting
        timeSinceLastUpdate_ += dt;
        if (timeSinceLastUpdate_ < UPDATE_INTERVAL)
        {
            return;
        }
        
        // Use accumulated time as effective dt for this step
        double stepDt = timeSinceLastUpdate_;
        
        // 1. Reversing Lights (Can run every frame or less freq, less freq is fine)
        UpdateReversingLights();

        // 2. Brake Lights
        double speed = vehicle_->GetSpeed();
        UpdateBrakeLights(stepDt, speed);
        
        // 3. Indicators
        UpdateIndicators(stepDt);

        // Update state
        prevSpeed_ = speed;
        prevLaneId_ = vehicle_->pos_.GetLaneId();
        
        // Reset timer
        timeSinceLastUpdate_ = 0.0;
    }

    void AutoLightController::UpdateBrakeLights(double dt, double currentSpeed)
    {
        // 1. Calculate raw acceleration
        double rawAcc = 0.0;
        if (dt > 0.0001)
        {
            rawAcc = (currentSpeed - prevSpeed_) / dt;
        }

        // 2. Apply smoothing (EMA)
        smoothedAcc_ = ACC_SMOOTHING_ALPHA * rawAcc + (1.0 - ACC_SMOOTHING_ALPHA) * smoothedAcc_;

        // 3. Low speed disable logic (don't react to noise when almost stopped)
        // However, if we are braking to a stop, we want lights ON.
        // But if stopped, rawAcc might be 0.
        // Let's keep logic simple: if speed is very low, assume stopped/parking -> OFF? 
        // Or hold last state? Real cars keep brake light if pedal pressed.
        // We don't have pedal info, only motion. 
        // If v ~= 0 and acc ~= 0, likely stopped. Turn OFF brake lights? 
        // (Unless stopped at light... but we can't know. Default to OFF for stopped to avoid stuck lights).
        if (currentSpeed < 0.1 && std::abs(rawAcc) < 0.1)
        {
             smoothedAcc_ = 0.0; // Reset
        }

        // 4. Hysteresis Logic
        LightState::Mode desiredState = lastBrakeState_;

        if (lastBrakeState_ == LightState::Mode::OFF)
        {
            if (smoothedAcc_ < BRAKE_ON_THRESHOLD)
            {
                desiredState = LightState::Mode::ON;
            }
        }
        else // Currently ON
        {
            if (smoothedAcc_ > BRAKE_OFF_THRESHOLD)
            {
                desiredState = LightState::Mode::OFF;
            }
        }

        // 5. Edge Triggered Update
        if (desiredState != lastBrakeState_)
        {
            LightState state;
            state.mode = desiredState;
            lightExt_->SetLightState(VehicleLightType::BRAKE_LIGHTS, state);
            lastBrakeState_ = desiredState;
        }
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

    void AutoLightController::UpdateIndicators(double dt)
    {
        // Inputs
        double steer = vehicle_->GetWheelAngle(); // Radians (Positive Left)
        id_t junctionId = vehicle_->pos_.GetJunctionId();
        int currentLaneId = vehicle_->pos_.GetLaneId();
        
        // --- 1. Event Detection ---
        bool turnLeft = false;
        bool turnRight = false;
        
        // A. Intersection / Steering Logic
        if (steer > STEER_THRESHOLD) turnLeft = true;
        else if (steer < -STEER_THRESHOLD) turnRight = true;
        
        // B. Lane Change Logic (Simple Lane ID Check for now, improved later with Lateral Velocity if needed)
        // Note: Lane ID based detection is edge-triggered.
        if (currentLaneId != prevLaneId_)
        {
            // Determine direction based on ODR logic
            // Assuming standard ODR: Left is t+, Right is t-
            // RHT: -1 (Right) -> -2 (Right LC) ? No, -2 is further right.
            // Lane ID magnitude increases outwards from center.
            // Right lanes (neg): -1 -> -2 (Right), -2 -> -1 (Left)
            // Left lanes (pos): 1 -> 2 (Left), 2 -> 1 (Right)
            
            bool isRightSide = (prevLaneId_ < 0);
            bool isLeftSide = (prevLaneId_ > 0);
            
            if (isRightSide)
            {
                if (currentLaneId < prevLaneId_) turnRight = true; // -1 to -2
                else if (currentLaneId > prevLaneId_) turnLeft = true; // -2 to -1
            }
            else if (isLeftSide)
            {
                if (currentLaneId > prevLaneId_) turnLeft = true; // 1 to 2
                else if (currentLaneId < prevLaneId_) turnRight = true; // 2 to 1
            }
            // Reset timer to keep signal active for a minimum duration after LC event
             indicatorTimer_ = MIN_INDICATOR_DURATION;
        }

        // --- 2. State Machine Update ---
        
        // If an active turn event is detected, refresh validity
        if (turnLeft) 
        {
            indicatorState_ = IndicatorState::LEFT_ACTIVE;
            indicatorTimer_ = MIN_INDICATOR_DURATION; // Keep refreshing while turning
        }
        else if (turnRight) 
        {
            indicatorState_ = IndicatorState::RIGHT_ACTIVE;
            indicatorTimer_ = MIN_INDICATOR_DURATION;
        }
        else
        {
            // No active input this frame.
            // Check timer.
            if (indicatorTimer_ > 0.0)
            {
                indicatorTimer_ -= dt;
                if (indicatorTimer_ <= 0.0)
                {
                    indicatorState_ = IndicatorState::OFF;
                }
            }
            else
            {
                 indicatorState_ = IndicatorState::OFF;
            }
        }
        
        // --- 3. Output Application (Edge + Continuous State) ---
        // AutoLight applies state continuously if it thinks it should be ON, 
        // to override potentially stale states?
        // Or should we respect Arbitration?
        // Proposal: Apply state derived from FSM.
        
        LightState leftState;
        LightState rightState;
        leftState.mode = LightState::Mode::OFF;
        rightState.mode = LightState::Mode::OFF;
        
        if (indicatorState_ == IndicatorState::LEFT_ACTIVE)
        {
            leftState.mode = LightState::Mode::FLASHING;
        }
        else if (indicatorState_ == IndicatorState::RIGHT_ACTIVE)
        {
            rightState.mode = LightState::Mode::FLASHING;
        }
        
        // Apply to extension
        lightExt_->SetLightState(VehicleLightType::INDICATOR_LEFT, leftState);
        lightExt_->SetLightState(VehicleLightType::INDICATOR_RIGHT, rightState);
    }
}
