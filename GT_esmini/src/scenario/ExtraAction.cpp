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

#include "gt_esmini/scenario/ExtraAction.hpp"
#include "gt_esmini/scenario/ExtraEntities.hpp"
#include "gt_esmini/scenario/TrafficSignalController.hpp"
#include "Entities.hpp"
#include "logger.hpp"

namespace gt_esmini
{
    // R5-U3: GT OSCLightStateAction implementation removed. Scenario light actions are now
    // executed by the native scenarioengine::LightStateAction (see VehicleLightBridge).

    // =========================================================================
    // GT_TrafficSignalControllerAction
    // =========================================================================

    GT_TrafficSignalControllerAction::GT_TrafficSignalControllerAction(scenarioengine::StoryBoardElement* parent)
        : scenarioengine::OSCGlobalAction(scenarioengine::OSCAction::ActionType::INFRASTRUCTURE, parent)
    {
    }

    void GT_TrafficSignalControllerAction::Start(double simTime)
    {
        auto* ctrl = TrafficSignalControllerManager::Instance().GetController(controllerRef_);
        if (ctrl)
        {
            if (!ctrl->SetPhase(phaseName_))
            {
                LOG_ERROR("TrafficSignalControllerAction: phase '{}' not found in controller '{}'", phaseName_, controllerRef_);
            }
        }
        else
        {
            LOG_ERROR("TrafficSignalControllerAction: controller '{}' not found", controllerRef_);
        }

        OSCAction::Start(simTime);
    }

    void GT_TrafficSignalControllerAction::Step(double simTime, double dt)
    {
        // Instantaneous action — complete immediately
        OSCAction::Stop();
    }

    scenarioengine::OSCGlobalAction* GT_TrafficSignalControllerAction::Copy()
    {
        auto* copy         = new GT_TrafficSignalControllerAction(parent_);
        copy->controllerRef_ = controllerRef_;
        copy->phaseName_     = phaseName_;
        return copy;
    }

}  // namespace gt_esmini
