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
          smoothedAcc_(0.0),
          lastBrakeState_(LightState::Mode::OFF),
          brakeLatchTimer_(0.0),
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
        
        // 3. Update Speed History
        // We need to store speed to look back 'BRAKE_EVENT_WINDOW' seconds.
        // Since we update at 'dt' (which is ~0.05s due to frequency limit), we can assume timestamps.
        // Or store simple pairs <age, speed> where age increments? 
        // Better: store current speed. We need to look back ~0.3s.
        // If dt is variable, we should be careful.
        // Let's assume we push {dt_duration, speed}.
        // But easier is simple ring buffer if we assume fixed step, but we don't assume fixed step completely.
        // Let's just push speed and keep track of total time in deque is not trivial without timestamps.
        // Okay, simpler: SpeedHistory stores pairs of {accumulated_dt, speed}.
        
        speedHistory_.push_back({dt, currentSpeed});
        
        // Clean up old history (> BRAKE_EVENT_WINDOW + margin)
        double totalHistoryTime = 0.0;
        // Iterate reverse to sum up time? No, forward.
        // But popping front is hard if we don't track total.
        // Let's just pop until total time is less than say 0.5s?
        // Actually, we need to Traverse from back to find the sample at T_window.
        
        // Let's limit size to 20 samples (1s at 20Hz)
        while (speedHistory_.size() > 20) {
            speedHistory_.pop_front();
        }

        // 4. Detect Brake Event (Delta V)
        bool brakeEvent = false;
        
        // Look back BRAKE_EVENT_WINDOW seconds (0.3s)
        double timeSum = 0.0;
        double pastSpeed = currentSpeed; 
        bool foundPast = false;
        
        for (auto it = speedHistory_.rbegin(); it != speedHistory_.rend(); ++it)
        {
            timeSum += it->first; // dt
            if (timeSum >= BRAKE_EVENT_WINDOW)
            {
                pastSpeed = it->second; // speed at that time
                foundPast = true;
                break;
            }
        }
        
        if (foundPast)
        {
            double dv = currentSpeed - pastSpeed;
            if (dv < -BRAKE_EVENT_DV)
            {
                brakeEvent = true;
            }
        }
        
        // 5. Update Latch Timer
        if (brakeLatchTimer_ > 0.0)
        {
            brakeLatchTimer_ -= dt;
        }
        
        if (brakeEvent)
        {
            brakeLatchTimer_ = BRAKE_LATCH_TIME;
        }

        // 6. Logic: Policy A+ (Stop Hold + Latch + Decel)
        
        // Condition 1: Stop Hold (Vehicle is stopped or almost stopped)
        bool isStopped = (currentSpeed < STOP_SPEED_THRESHOLD);
        
        // Condition 2: Latch Active (Recent Brake Event)
        bool isLatched = (brakeLatchTimer_ > 0.0);
        
        // Condition 3: Hard Deceleration (Safety fallback)
        bool isHardBraking = (smoothedAcc_ < BRAKE_ON_THRESHOLD);
        
        LightState::Mode desiredState = lastBrakeState_;

        if (isStopped || isLatched || isHardBraking)
        {
            desiredState = LightState::Mode::ON;
        }
        else
        {
            desiredState = LightState::Mode::OFF;
        }

        // 7. Edge Triggered Update
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
