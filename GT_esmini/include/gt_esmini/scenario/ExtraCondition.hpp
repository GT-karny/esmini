/*
 * GT_esmini - Extended esmini with Traffic Signal Controller Condition
 * https://github.com/esmini/esmini
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2024 GT_esmini contributors
 */

#pragma once

#include "OSCCondition.hpp"

namespace gt_esmini
{
    /**
     * @brief Condition that checks if a TrafficSignalController is in a specific phase
     *
     * OpenSCENARIO: ByValueCondition > TrafficSignalControllerCondition
     */
    class TrigByTrafficSignalController : public scenarioengine::TrigByValue
    {
    public:
        std::string controllerRef_;
        std::string phaseName_;

        TrigByTrafficSignalController()
            : scenarioengine::TrigByValue(scenarioengine::TrigByValue::Type::TRAFFIC_SIGNAL)
        {
        }

        bool        CheckCondition(double sim_time);
        std::string GetAdditionalLogInfo() override;
    };

}  // namespace gt_esmini
