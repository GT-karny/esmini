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
        void UpdateBrakeLights();
        
        /**
         * @brief Control indicators based on lane changes and turns
         */
        void UpdateIndicators();
        
        /**
         * @brief Control reversing lights based on speed
         */
        void UpdateReversingLights();
        
        // State variables for logic
        double prevSpeed_;
        int prevLaneId_;
        double laneChangeStartTime_;
        bool isInLaneChange_;
        
        // Thresholds
        const double BRAKE_DECELERATION_THRESHOLD = -0.98; // -0.1G [m/s^2]
    };
}
