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

#include "Entities.hpp"       // esmini core
#include "ExtraEntities.hpp"  // GT_esmini extension

namespace gt_esmini
{
    /**
     * @brief AutoLight feature controller class
     * 
     * Automatically controls lights based on vehicle behavior:
     * - Brake lights: Turn on when acceleration is -0.1G or less
     * - Turn signals: Turn on during lane changes and intersection turns
     * - Reversing lights: Turn on when speed is negative (reversing)
     * 
     * Phase 1: Stub implementation
     * Phase 3: Implement actual control logic
     */
    class AutoLightController
    {
    public:
        AutoLightController(scenarioengine::Vehicle* vehicle, VehicleLightExtension* lightExt);
        ~AutoLightController();

        /**
         * @brief Called every frame
         * @param dt Delta time (seconds)
         */
        void Update(double dt);

        /**
         * @brief Enable/disable AutoLight feature
         * @param enabled true: enabled, false: disabled
         */
        void SetEnabled(bool enabled);

        /**
         * @brief Check if AutoLight feature is enabled
         * @return true: enabled, false: disabled
         */
        bool IsEnabled() const { return enabled_; }

    private:
        scenarioengine::Vehicle* vehicle_;
        VehicleLightExtension*   lightExt_;  // Reference to light extension
        bool                     enabled_;

        // Brake light control
        void UpdateBrakeLights(double acceleration);

        // Turn signal control
        void UpdateIndicators();

        // Reversing light control
        void UpdateReversingLights();

        // Previous state
        double prevSpeed_;
        int    prevLaneId_;
        double laneChangeStartTime_;
        bool   isInLaneChange_;
    };

}  // namespace gt_esmini
