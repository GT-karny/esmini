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

#include "OSCPrivateAction.hpp"  // Include esmini's OSCPrivateAction.hpp
#include <map>

namespace gt_esmini
{
    /**
     * @brief Light state structure
     */
    struct LightState
    {
        enum class Mode
        {
            OFF,
            ON,
            FLASHING
        };

        Mode   mode                = Mode::OFF;
        double luminousIntensity   = 0.0;  // optional
        double flashingOnDuration  = 0.0;  // optional (for FLASHING mode)
        double flashingOffDuration = 0.0;  // optional (for FLASHING mode)
        // Color (R, G, B) - optional
        double colorR = 1.0;
        double colorG = 1.0;
        double colorB = 1.0;
    };

    /**
     * @brief VehicleLightType enum (OpenSCENARIO v1.2 compliant)
     */
    enum class VehicleLightType
    {
        DAYTIME_RUNNING_LIGHTS,
        LOW_BEAM,
        HIGH_BEAM,
        FOG_LIGHTS,
        FOG_LIGHTS_FRONT,
        FOG_LIGHTS_REAR,
        BRAKE_LIGHTS,
        WARNING_LIGHTS,
        INDICATOR_LEFT,
        INDICATOR_RIGHT,
        REVERSING_LIGHTS,
        LICENSE_PLATE_ILLUMINATION,
        SPECIAL_PURPOSE_LIGHTS
    };

    /**
     * @brief LightStateAction class (inherits from esmini's OSCPrivateAction)
     * 
     * Phase 1: Stub implementation
     * Phase 2: Implement actual action execution logic
     */
    class OSCLightStateAction : public scenarioengine::OSCPrivateAction
    {
    public:
        OSCLightStateAction(scenarioengine::StoryBoardElement* parent);
        virtual ~OSCLightStateAction();

        void Start(double simTime) override;
        void Step(double simTime, double dt) override;
        void End() override;

        scenarioengine::OSCPrivateAction* Copy() override;
        std::string                       Type2Str() override { return "LightStateAction"; }

        VehicleLightType lightType_;
        LightState       lightState_;
        double           transitionTime_;

    private:
        double startTime_;
        bool   isTransitioning_;
    };

}  // namespace gt_esmini
