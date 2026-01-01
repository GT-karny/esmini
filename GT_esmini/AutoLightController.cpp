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
#include <algorithm>

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
          prepareTimerLeft_(0.0),
          prepareTimerRight_(0.0),
          prepareOffTimer_(0.0),
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
        id_t junctionId = vehicle_->pos_.GetJunctionId();
        int currentLaneId = vehicle_->pos_.GetLaneId();
        double t = vehicle_->pos_.GetOffset(); // Lane Center Offset (Positive Left)
        
        // Calculate t_dot
        double t_dot = 0.0;
        if (dt > 0.0001)
        {
             if (currentLaneId == prevLaneId_)
             {
                 t_dot = (t - prev_t_) / dt;
             }
             else
             {
                 // Lane Change Jump: Reset t_dot or assume consistent direction?
                 // We rely on FSM + Position check for direction.
                 t_dot = 0.0; 
             }
        }
        
        // --- Event Detection ---
        bool steerLeft = (steer > STEER_THRESHOLD);
        bool steerRight = (steer < -STEER_THRESHOLD);
        
        // Lane Change Logic
        bool laneChanged = (currentLaneId != prevLaneId_);
        bool laneChgLeft = false;
        bool laneChgRight = false;

        if (laneChanged)
        {
            // Robust Direction Detection based on User Feedback
            // "Prioritize t sign after lane change"
            // Left Move (Right->Left relative) -> New t is Negative (Right side of new lane)
            // Right Move (Left->Right relative) -> New t is Positive (Left side of new lane)
            // Note: Standard OpenDRIVE: Lane width > 0 usually?
            // Left Lane (ID > 0): Center to Left. Entering from Right -> t < 0.
            // Right Lane (ID < 0): Center to Right. Entering from Left -> t > 0.
            // Wait, Right Lane (ID < 0) has t increasing Leftwards (towards center).
            // So if I move Right (away from center), t becomes more negative?
            // No, OpenDRIVE t axis is always orthogonal to s. Standard: Left is positive t.
            // Lane Center Offset:
            // "Offset from lane center".
            // Left Move (to lane with more positive t):
            // I am at +1.75 (Lane 1 Left Edge). Enter Lane 2 Right Edge (-1.75).
            // So Left Move -> New t is Negative.
            // Right Move (to lane with more negative t):
            // I am at -1.75 (Lane 1 High Edge? No).
            // Lane 1 Center t=1.75. Current t=0 (center).
            // Right edge of Lane 1 is at t=0 (Road Ref)? No.
            // Let's assume t (Offset) is local.
            if (t < -0.5) laneChgLeft = true;
            else if (t > 0.5) laneChgRight = true;
            else
            {
                 // Fallback to LaneID diff if t is near 0 (perfect fit?)
                 if (currentLaneId - prevLaneId_ > 0) laneChgLeft = true;
                 else if (currentLaneId - prevLaneId_ < 0) laneChgRight = true;
            }
        }

        // --- FSM Update ---
        
        // 0. Update Output Timer
        if (indicatorTimer_ > 0.0) indicatorTimer_ -= dt;

        // 1. Steering Override (Junction Only)
        // User Requirement: "junctionId != -1"
        // Also keep override logic simple.
        
        bool inJunction = (junctionId != -1);
        
        if (inJunction)
        {
            if (steerLeft)
            {
                 indicatorState_ = IndicatorState::ACTIVE_LEFT;
                 indicatorTimer_ = MIN_INDICATOR_DURATION; 
                 centerHoldTimer_ = 0.0; // Reset
            }
            else if (steerRight)
            {
                 indicatorState_ = IndicatorState::ACTIVE_RIGHT;
                 indicatorTimer_ = MIN_INDICATOR_DURATION;
                 centerHoldTimer_ = 0.0; // Reset
            }
            // If no steer but in junction, retain state or fall to FSM?
            // Fall to FSM allows cancellation if driving straignt in junction.
        }

        switch (indicatorState_)
        {
        case IndicatorState::OFF:
        {
            // Transition to PREPARE?
            bool preLeft = (t_dot > TDOT_PREARM && t > T_PREARM_MIN);
            bool preRight = (t_dot < -TDOT_PREARM && t < -T_PREARM_MIN); 
            
            if (preLeft)
            {
                 prepareTimerLeft_ += dt;
                 prepareTimerRight_ = 0.0; // Reset other
                 if (prepareTimerLeft_ > T_PREARM_TIME)
                 {
                      indicatorState_ = IndicatorState::PREPARE_LEFT;
                      prepareTimerLeft_ = 0.0;
                      indicatorTimer_ = MIN_INDICATOR_DURATION; // Set min duration on entry
                 }
            }
            else if (preRight)
            {
                 prepareTimerRight_ += dt;
                 prepareTimerLeft_ = 0.0; // Reset other
                 if (prepareTimerRight_ > T_PREARM_TIME)
                 {
                      indicatorState_ = IndicatorState::PREPARE_RIGHT;
                      prepareTimerRight_ = 0.0;
                      indicatorTimer_ = MIN_INDICATOR_DURATION; // Set min duration on entry
                 }
            }
            else
            {
                 prepareTimerLeft_ = 0.0;
                 prepareTimerRight_ = 0.0;
            }

            // Priority 2: Junction Turn Prediction
            // If lateral motion didn't trigger, check for upcoming junction turn.
            if (indicatorState_ == IndicatorState::OFF)
            {
                int turnDir = 0;
                // Scan from 5m to LOOKAHEAD to find the nearest junction/turn
                for (double d = 5.0; d <= JUNCTION_LOOKAHEAD; d += 5.0)
                {
                    int res = DetectJunctionTurn(d);
                    if (res != 0)
                    {
                         // Found a turn. Is it close enough to blink?
                         if (d <= JUNCTION_BLINK_DIST)
                         {
                             turnDir = res;
                         }
                         break; // Found the nearest feature, stop scanning
                    }
                }

                if (turnDir == 1) // Left
                {
                     indicatorState_ = IndicatorState::ACTIVE_LEFT; // Use ACTIVE to stay on while stopped
                     indicatorTimer_ = MIN_INDICATOR_DURATION; 
                     centerHoldTimer_ = 0.0;
                }
                else if (turnDir == -1) // Right
                {
                     indicatorState_ = IndicatorState::ACTIVE_RIGHT;
                     indicatorTimer_ = MIN_INDICATOR_DURATION;
                     centerHoldTimer_ = 0.0;
                }
            }
            break;
        }
        case IndicatorState::PREPARE_LEFT:
        {
            // Trigger ACTIVE?
            if (laneChgLeft || t > T_ACTIVE_MIN)
            {
                indicatorState_ = IndicatorState::ACTIVE_LEFT;
                indicatorTimer_ = MIN_INDICATOR_DURATION; // Refresh/Latch
                centerHoldTimer_ = 0.0; // Reset Hold Timer
                prepareOffTimer_ = 0.0; // Reset Cancel Timer
            }
            // Cancel?
            // Condition: Time-based stable cancel condition
            // Uses ABS values for robust cancellation near 0
            else 
            {
                 if (std::abs(t_dot) < TDOT_CANCEL && std::abs(t) < T_CANCEL_MIN)
                 {
                      prepareOffTimer_ += dt;
                 }
                 else
                 {
                      prepareOffTimer_ = 0.0;
                 }
                 
                 // Execute Cancel only if timer expired AND min duration expired
                 if (prepareOffTimer_ > T_CANCEL_TIME && indicatorTimer_ <= 0.0)
                 {
                      indicatorState_ = IndicatorState::OFF;
                      prepareOffTimer_ = 0.0;
                 }
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
                 centerHoldTimer_ = 0.0;
                 prepareOffTimer_ = 0.0;
             }
             // Cancel?
             else 
             {
                 if (std::abs(t_dot) < TDOT_CANCEL && std::abs(t) < T_CANCEL_MIN)
                 {
                      prepareOffTimer_ += dt;
                 }
                 else
                 {
                      prepareOffTimer_ = 0.0;
                 }
                 
                 if (prepareOffTimer_ > T_CANCEL_TIME && indicatorTimer_ <= 0.0)
                 {
                      indicatorState_ = IndicatorState::OFF;
                      prepareOffTimer_ = 0.0;
                 }
             }
             break;
        }
        case IndicatorState::ACTIVE_LEFT:
        {
             // 1. Immediate Reversal (Level 1: Lane Change Event)
             // If we just changed into the RIGHT lane (or any lane to the right), switch immediately.
             if (laneChgRight)
             {
                 indicatorState_ = IndicatorState::ACTIVE_RIGHT;
                 indicatorTimer_ = MIN_INDICATOR_DURATION; // Latch for visibility
                 centerHoldTimer_ = 0.0;
                 prepareOffTimer_ = 0.0;
                 prepareTimerLeft_ = 0.0;
                 prepareTimerRight_ = 0.0;
                 break;
             }

             // 2. Immediate Reversal (Level 2: Strong Lateral Motion)
             // Override min duration if strong evidence exists.
             bool strongReversal = (t_dot < -REVERSAL_TDOT && t < -REVERSAL_T_MIN);
             if (strongReversal)
             {
                 prepareTimerRight_ += dt;
                 // If evidence persists, force switch
                 if (prepareTimerRight_ > REVERSAL_CONFIRM_TIME)
                 {
                     // Decide: PREPARE or direct ACTIVE? 
                     // Since signal is strong, PREPARE is safer but fast.
                     indicatorState_ = IndicatorState::PREPARE_RIGHT; 
                     
                     // Ensure min duration for new state, but allow fast switch if needed later
                     indicatorTimer_ = std::max(indicatorTimer_, REVERSAL_MIN_ACTIVE); 
                     
                     prepareTimerRight_ = 0.0;
                     centerHoldTimer_ = 0.0; 
                     prepareOffTimer_ = 0.0;
                     break; 
                 }
             }
             else
             {
                 prepareTimerRight_ = 0.0;
             }
             
             // 3. Normal Direction Reversal (Weak/Early detection)
             // Only if min duration expired
             if (indicatorTimer_ <= 0.0)
             {
                 // Standard pre-arm (already covered by Level 2 mostly, but keep for lower thresholds if needed)
                 // Actually, Level 2 covers the "Emergency/Fast" case. 
                 // If we want slow reversal, we can add it here, or rely on Level 2 thresholds being tuned well.
                 // For now, let's stick to Level 2 as the main reversal path to avoid complexity.
             }

             // 4. Turn OFF? (Return to Center)
             // Only cancel if:
             // A. We are centered (t < EPS)
             // B. Min duration expired
             // C. NOT in a junction (don't cancel mid-turn)
             // D. NOT approaching a left turn (don't cancel while waiting at light)
             
             bool waitingAtJunction = (DetectJunctionTurn(JUNCTION_LOOKAHEAD) == 1); // 1 = Left
             bool inJunction = (vehicle_ && vehicle_->pos_.GetJunctionId() != -1);

             if (std::abs(t) < T_CENTER_EPS && !waitingAtJunction && !inJunction)
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
             // 1. Immediate Reversal (Level 1: Lane Change Event)
             if (laneChgLeft)
             {
                 indicatorState_ = IndicatorState::ACTIVE_LEFT;
                 indicatorTimer_ = MIN_INDICATOR_DURATION; 
                 centerHoldTimer_ = 0.0;
                 prepareOffTimer_ = 0.0;
                 prepareTimerLeft_ = 0.0;
                 prepareTimerRight_ = 0.0;
                 break;
             }

             // 2. Immediate Reversal (Level 2: Strong Lateral Motion)
             bool strongReversal = (t_dot > REVERSAL_TDOT && t > REVERSAL_T_MIN);
             if (strongReversal)
             {
                 prepareTimerLeft_ += dt;
                 if (prepareTimerLeft_ > REVERSAL_CONFIRM_TIME)
                 {
                     indicatorState_ = IndicatorState::PREPARE_LEFT;
                     indicatorTimer_ = std::max(indicatorTimer_, REVERSAL_MIN_ACTIVE);
                     prepareTimerLeft_ = 0.0;
                     centerHoldTimer_ = 0.0; 
                     prepareOffTimer_ = 0.0;
                     break; 
                 }
             }
             else
             {
                 prepareTimerLeft_ = 0.0;
             }

             // 3. Turn OFF?
             bool waitingAtJunction = (DetectJunctionTurn(JUNCTION_LOOKAHEAD) == -1); // -1 = Right
             bool inJunction = (vehicle_ && vehicle_->pos_.GetJunctionId() != -1);

             if (std::abs(t) < T_CENTER_EPS && !waitingAtJunction && !inJunction)
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

    // End of UpdateIndicators, continuing namespace...

    int AutoLightController::DetectJunctionTurn(double lookahead)
    {
        if (!vehicle_) return 0;

        roadmanager::RoadProbeInfo info;
        auto ret = vehicle_->pos_.GetProbeInfo(lookahead, &info, roadmanager::Position::LookAheadMode::LOOKAHEADMODE_AT_LANE_CENTER);

        if (ret != roadmanager::Position::ReturnCode::OK)
        {
             return 0;
        }

        if (info.road_lane_info.junctionId == -1)
        {
            return 0; 
        }

        double relH = info.relative_h; 
        
        // Threshold check
        int res = 0;
        if (relH > JUNCTION_TURN_THRESHOLD) res = 1;
        else if (relH < -JUNCTION_TURN_THRESHOLD) res = -1;

        return res;
    }


} // namespace gt_esmini
