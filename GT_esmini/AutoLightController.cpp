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
          prev_t_(0.0),
          prepareTimer_(0.0),
          centerHoldTimer_(0.0),
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
        prev_t_ = vehicle_->pos_.GetOffset();
        
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
        int currentLaneId = vehicle_->pos_.GetLaneId();
        double t = vehicle_->pos_.GetOffset(); // Lane Center Offset (Positive Left)
        
        // Calculate t_dot
        double t_dot = 0.0;
        if (dt > 0.0001)
        {
             // Note: t jumps when LaneID changes. Avoid spurious t_dot.
             if (currentLaneId == prevLaneId_)
             {
                 t_dot = (t - prev_t_) / dt;
             }
             else
             {
                 // Lane ID changed this step. t_dot calculation is invalid across the jump.
                 // We rely on the state machine transition triggered by lane ID change.
                 t_dot = 0.0; 
             }
        }

        // --- Event Detection ---
        bool steerLeft = (steer > STEER_THRESHOLD);
        bool steerRight = (steer < -STEER_THRESHOLD);
        
        bool laneChanged = (currentLaneId != prevLaneId_);
        // Delta LaneID > 0 implies Left move, < 0 implies Right move (in standard OpenDRIVE lane numbering)
        // Example: -1 -> -2 (Right), -2 -> -1 (Left). 1 -> 2 (Left), 2 -> 1 (Right).
        bool laneChgLeft = laneChanged && (currentLaneId - prevLaneId_ > 0);
        bool laneChgRight = laneChanged && (currentLaneId - prevLaneId_ < 0);
        // Special case: Crossing center (1 <-> -1). 
        // 1 -> -1: diff -2 (Right). Correct.
        // -1 -> 1: diff +2 (Left). Correct.

        // --- FSM Update ---
        
        // 0. Update Timers
        if (indicatorTimer_ > 0.0) indicatorTimer_ -= dt;

        // 1. Steering Override (Junction Turns) - Immediate ACTIVE
        if (steerLeft)
        {
             indicatorState_ = IndicatorState::ACTIVE_LEFT;
             indicatorTimer_ = MIN_INDICATOR_DURATION; // Keep refreshing
        }
        else if (steerRight)
        {
             indicatorState_ = IndicatorState::ACTIVE_RIGHT;
             indicatorTimer_ = MIN_INDICATOR_DURATION; // Keep refreshing
        }
        else
        {
            // 2. Predictive Lane Change FSM
            switch (indicatorState_)
            {
            case IndicatorState::OFF:
            {
                // Transition to PREPARE?
                // Condition: t_dot large AND t large (in same direction)
                // PREPARE_LEFT: t_dot > Threshold && t > Threshold
                bool preLeft = (t_dot > TDOT_PREARM && t > T_PREARM_MIN);
                bool preRight = (t_dot < -TDOT_PREARM && t < -T_PREARM_MIN); 
                
                if (preLeft)
                {
                     prepareTimer_ += dt;
                     if (prepareTimer_ > T_PREARM_TIME)
                     {
                          indicatorState_ = IndicatorState::PREPARE_LEFT;
                          prepareTimer_ = 0.0;
                          indicatorTimer_ = MIN_INDICATOR_DURATION; // Initial duration
                     }
                }
                else if (preRight)
                {
                     prepareTimer_ += dt;
                     if (prepareTimer_ > T_PREARM_TIME)
                     {
                          indicatorState_ = IndicatorState::PREPARE_RIGHT;
                          prepareTimer_ = 0.0;
                          indicatorTimer_ = MIN_INDICATOR_DURATION; // Initial duration
                     }
                }
                else
                {
                     prepareTimer_ = 0.0;
                }
                break;
            }
            case IndicatorState::PREPARE_LEFT:
            {
                // Trigger ACTIVE?
                // Condition: Lane Change detected OR t > T_ACTIVE_MIN
                if (laneChgLeft || t > T_ACTIVE_MIN)
                {
                    indicatorState_ = IndicatorState::ACTIVE_LEFT;
                    indicatorTimer_ = MIN_INDICATOR_DURATION; // Refresh/Latch
                }
                // Cancel?
                // If moving back to center (t_dot < 0) AND t < T_PREARM_MIN (back in safe zone)
                else if (t_dot < 0 && t < T_PREARM_MIN)
                {
                    indicatorState_ = IndicatorState::OFF;
                }
                break;
            }
            case IndicatorState::PREPARE_RIGHT:
            {
                 // Trigger ACTIVE?
                 if (laneChgRight || t < -T_ACTIVE_MIN)
                 {
                     indicatorState_ = IndicatorState::ACTIVE_RIGHT;
                     indicatorTimer_ = MIN_INDICATOR_DURATION; // Refresh/Latch
                 }
                 // Cancel?
                 else if (t_dot > 0 && t > -T_PREARM_MIN)
                 {
                     indicatorState_ = IndicatorState::OFF;
                 }
                 break;
            }
            case IndicatorState::ACTIVE_LEFT:
            {
                 // Turn OFF?
                 // Condition: Vehicle is centered in lane (t ~ 0) for some time, AND minimum duration expired.
                 // Note: During LC, t transitions from high to low (new lane).
                 // We wait for it to settle near 0.
                 
                 if (std::abs(t) < T_CENTER_EPS)
                 {
                      centerHoldTimer_ += dt;
                 }
                 else
                 {
                      centerHoldTimer_ = 0.0;
                 }
                 
                 if (centerHoldTimer_ > T_CENTER_HOLD && indicatorTimer_ <= 0.0)
                 {
                      indicatorState_ = IndicatorState::OFF;
                      centerHoldTimer_ = 0.0;
                 }
                 break;
            }
            case IndicatorState::ACTIVE_RIGHT:
            {
                 if (std::abs(t) < T_CENTER_EPS)
                 {
                      centerHoldTimer_ += dt;
                 }
                 else
                 {
                      centerHoldTimer_ = 0.0;
                 }
                 
                 if (centerHoldTimer_ > T_CENTER_HOLD && indicatorTimer_ <= 0.0)
                 {
                      indicatorState_ = IndicatorState::OFF;
                      centerHoldTimer_ = 0.0;
                 }
                 break;
            }
            }
        }
        
        // --- Output Application ---
        
        LightState leftState;
        LightState rightState;
        leftState.mode = LightState::Mode::OFF;
        rightState.mode = LightState::Mode::OFF;
        
        if (indicatorState_ == IndicatorState::ACTIVE_LEFT || indicatorState_ == IndicatorState::PREPARE_LEFT)
        {
             leftState.mode = LightState::Mode::FLASHING;
        }
        else if (indicatorState_ == IndicatorState::ACTIVE_RIGHT || indicatorState_ == IndicatorState::PREPARE_RIGHT)
        {
             rightState.mode = LightState::Mode::FLASHING;
        }
        
        lightExt_->SetLightState(VehicleLightType::INDICATOR_LEFT, leftState);
        lightExt_->SetLightState(VehicleLightType::INDICATOR_RIGHT, rightState);
    }
}
