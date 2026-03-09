/*
 * GT_esmini - Extended esmini with Traffic Signal Controller support
 * https://github.com/esmini/esmini
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2024 GT_esmini contributors
 */

#include "gt_esmini/scenario/TrafficSignalController.hpp"
#include "logger.hpp"

namespace gt_esmini
{
    // =========================================================================
    // OSCTrafficSignalController
    // =========================================================================

    OSCTrafficSignalController::OSCTrafficSignalController(const std::string& name, double delay)
        : name_(name), delay_(delay)
    {
    }

    void OSCTrafficSignalController::AddPhase(const TrafficSignalPhase& phase)
    {
        phases_.push_back(phase);
    }

    void OSCTrafficSignalController::Init()
    {
        if (phases_.empty())
        {
            LOG_WARN("TrafficSignalController '{}': no phases defined", name_);
            return;
        }

        // Resolve signal IDs to TrafficLight pointers
        auto* odr = roadmanager::Position::GetOpenDrive();
        if (!odr)
        {
            LOG_ERROR("TrafficSignalController '{}': OpenDRIVE not loaded", name_);
            return;
        }

        auto dynamicSignals = odr->GetDynamicSignals();

        for (const auto& phase : phases_)
        {
            for (const auto& ss : phase.signalStates)
            {
                if (resolvedSignals_.count(ss.signalId) > 0)
                    continue;  // already resolved

                roadmanager::TrafficLight* tl = nullptr;
                for (auto* signal : dynamicSignals)
                {
                    auto* candidate = dynamic_cast<roadmanager::TrafficLight*>(signal);
                    if (candidate && candidate->GetId() == ss.signalId)
                    {
                        tl = candidate;
                        break;
                    }
                }

                if (tl)
                {
                    resolvedSignals_[ss.signalId] = tl;
                    tl->SetHasOSCAction(true);
                }
                else
                {
                    LOG_WARN("TrafficSignalController '{}': signal ID {} not found in OpenDRIVE dynamic signals", name_, ss.signalId);
                }
            }
        }

        // Apply initial phase
        currentPhaseIndex_ = 0;
        phaseElapsed_      = 0.0;
        delayElapsed_      = 0.0;
        started_           = (delay_ <= 0.0);
        initialized_       = true;

        if (started_)
        {
            ApplyCurrentPhaseStates();
        }

        LOG_INFO("TrafficSignalController '{}' initialized: {} phases, delay={:.1f}s, {} signals resolved",
                 name_, phases_.size(), delay_, resolvedSignals_.size());
    }

    void OSCTrafficSignalController::Step(double dt)
    {
        if (!initialized_ || phases_.empty())
            return;

        // Handle initial delay
        if (!started_)
        {
            delayElapsed_ += dt;
            if (delayElapsed_ >= delay_)
            {
                started_ = true;
                ApplyCurrentPhaseStates();
            }
            return;
        }

        // Advance phase timer
        phaseElapsed_ += dt;

        double currentDuration = phases_[currentPhaseIndex_].duration;
        if (currentDuration > 0.0 && phaseElapsed_ >= currentDuration)
        {
            // Advance to next phase (wrap around)
            phaseElapsed_ -= currentDuration;
            currentPhaseIndex_ = (currentPhaseIndex_ + 1) % static_cast<int>(phases_.size());
            ApplyCurrentPhaseStates();
        }
    }

    bool OSCTrafficSignalController::SetPhase(const std::string& phaseName)
    {
        for (int i = 0; i < static_cast<int>(phases_.size()); i++)
        {
            if (phases_[i].name == phaseName)
            {
                currentPhaseIndex_ = i;
                phaseElapsed_      = 0.0;
                started_           = true;  // SetPhase bypasses delay
                ApplyCurrentPhaseStates();
                return true;
            }
        }

        LOG_WARN("TrafficSignalController '{}': phase '{}' not found", name_, phaseName);
        return false;
    }

    std::string OSCTrafficSignalController::GetCurrentPhaseName() const
    {
        if (phases_.empty())
            return "";
        return phases_[currentPhaseIndex_].name;
    }

    void OSCTrafficSignalController::ApplyCurrentPhaseStates()
    {
        if (phases_.empty())
            return;

        const auto& phase = phases_[currentPhaseIndex_];
        for (const auto& ss : phase.signalStates)
        {
            auto it = resolvedSignals_.find(ss.signalId);
            if (it != resolvedSignals_.end() && it->second)
            {
                it->second->UpdateState(ss.state);
            }
        }
    }

    // =========================================================================
    // TrafficSignalControllerManager
    // =========================================================================

    void TrafficSignalControllerManager::AddController(OSCTrafficSignalController controller)
    {
        controllers_.push_back(std::move(controller));
    }

    void TrafficSignalControllerManager::InitAll()
    {
        for (auto& ctrl : controllers_)
        {
            ctrl.Init();
        }
    }

    void TrafficSignalControllerManager::StepAll(double dt)
    {
        for (auto& ctrl : controllers_)
        {
            ctrl.Step(dt);
        }
    }

    OSCTrafficSignalController* TrafficSignalControllerManager::GetController(const std::string& name)
    {
        for (auto& ctrl : controllers_)
        {
            if (ctrl.GetName() == name)
                return &ctrl;
        }
        return nullptr;
    }

    void TrafficSignalControllerManager::Clear()
    {
        controllers_.clear();
    }

}  // namespace gt_esmini
