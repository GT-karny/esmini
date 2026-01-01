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
        double laneChangeStartTime_;
        bool isInLaneChange_;

        // Brake Light Logic
        double smoothedAcc_;
        LightState::Mode lastBrakeState_;

        // Indicator Logic
        enum class IndicatorState { OFF, LEFT_ACTIVE, RIGHT_ACTIVE };
        IndicatorState indicatorState_;
        double indicatorTimer_;        // Counts down minimum active time
        double laneChangeDetectTime_;  // Debounce for LC start
        
        // Robustness
        double timeSinceLastUpdate_;   // For frequency limiting

        // Thresholds
        static constexpr double BRAKE_ON_THRESHOLD = -1.2;  // m/s^2 (Hysteresis ON)
        static constexpr double BRAKE_OFF_THRESHOLD = -0.5; // m/s^2 (Hysteresis OFF)
        static constexpr double ACC_SMOOTHING_ALPHA = 0.2;  // EMA factor assuming ~60Hz
        
        static constexpr double MIN_INDICATOR_DURATION = 2.0; // Seconds
        static constexpr double STEER_THRESHOLD = 0.08;      // rad check
        static constexpr double YAW_RATE_THRESHOLD = 0.05;   // rad/s check
        static constexpr double UPDATE_INTERVAL = 0.05;      // 20Hz
    };
}
