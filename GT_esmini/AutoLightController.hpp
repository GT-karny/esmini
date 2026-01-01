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

#pragma once

#include "Entities.hpp"  // esmini
#include "ExtraEntities.hpp"  // GT_esmini extension
#include <deque>
#include <utility> // for std::pair

namespace gt_esmini
{
    class AutoLightController
    {
    public:
        /**
         * @brief Constructor
         * @param vehicle Target vehicle
         * @param lightExt Vehicle light extension
         */
        AutoLightController(scenarioengine::Vehicle* vehicle, VehicleLightExtension* lightExt);
        
        ~AutoLightController();
        
        /**
         * @brief Update function called every frame
         * @param dt Delta time [s]
         */
        void Update(double dt);
        
        /**
         * @brief Enable/disable AutoLight for this controller
         * @param enabled true: enable, false: disable
         */
        void Enable(bool enabled);
        
        /**
         * @brief Check if AutoLight is enabled
         * @return true if enabled
         */
        bool IsEnabled() const { return enabled_; }
        
    private:
        scenarioengine::Vehicle* vehicle_;
        VehicleLightExtension* lightExt_;
        bool enabled_;
        
        /**
         * @brief Control brake lights based on deceleration
         */
        void UpdateBrakeLights(double dt, double currentSpeed);
        
        /**
         * @brief Control indicators based on lane changes and turns
         */
        void UpdateIndicators(double dt);
        
        /**
         * @brief Control reversing lights based on speed
         */
        void UpdateReversingLights();
        
        // State variables for logic
        double prevSpeed_;
        int prevLaneId_;
        // Brake Light Logic (Policy A+)
        double smoothedAcc_;
        LightState::Mode lastBrakeState_;
        double brakeLatchTimer_;
        
        // Speed History for Brake Event Detection (Time, Speed)
        std::deque<std::pair<double, double>> speedHistory_;

        // Indicator Logic
        enum class IndicatorState { OFF, LEFT_ACTIVE, RIGHT_ACTIVE };
        IndicatorState indicatorState_;
        double indicatorTimer_;        // Counts down minimum active time
        double laneChangeDetectTime_;  // Debounce for LC start
        
        // Robustness
        double timeSinceLastUpdate_;   // For frequency limiting

        // Thresholds
        static constexpr double BRAKE_ON_THRESHOLD = -1.2;     // m/s^2 (Hard Decel Trigger)
        static constexpr double STOP_SPEED_THRESHOLD = 0.1;    // m/s (Stop Hold)
        static constexpr double BRAKE_LATCH_TIME = 0.7;        // s (Minimum ON time)
        static constexpr double BRAKE_EVENT_WINDOW = 0.3;      // s (Delta V window)
        static constexpr double BRAKE_EVENT_DV = 0.4;          // m/s (Delta V threshold for ON)
        
        static constexpr double ACC_SMOOTHING_ALPHA = 0.1;     // EMA factor (Lower is smoother)
        
        static constexpr double MIN_INDICATOR_DURATION = 2.0; // Seconds
        static constexpr double STEER_THRESHOLD = 0.08;      // rad check
        static constexpr double YAW_RATE_THRESHOLD = 0.05;   // rad/s check
        static constexpr double UPDATE_INTERVAL = 0.05;      // 20Hz
    };
}
