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

#include "gt_esmini/scenario/ExtraCondition.hpp"
#include "gt_esmini/scenario/TrafficSignalController.hpp"
#include "logger.hpp"

namespace gt_esmini
{
    bool TrigByTrafficSignalController::CheckCondition(double sim_time)
    {
        auto* ctrl = TrafficSignalControllerManager::Instance().GetController(controllerRef_);
        if (!ctrl)
        {
            LOG_INFO("TrigByTrafficSignalController: controller '{}' not found", controllerRef_);
            return false;
        }

        bool result = (ctrl->GetCurrentPhaseName() == phaseName_);

        // Note: Do NOT set last_result_ here. The base class Evaluate() handles it
        // after calling CheckEdge(). Setting it here would defeat edge detection.
        return result;
    }

    std::string TrigByTrafficSignalController::GetAdditionalLogInfo()
    {
        auto* ctrl = TrafficSignalControllerManager::Instance().GetController(controllerRef_);
        std::string currentPhase = ctrl ? ctrl->GetCurrentPhaseName() : "N/A";
        return "TrafficSignalControllerCondition: controller=" + controllerRef_ +
               " expected=" + phaseName_ +
               " current=" + currentPhase;
    }

}  // namespace gt_esmini
