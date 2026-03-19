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

        // If reference is set, resolve additional signals from OpenDRIVE controller
        if (!reference_.empty())
        {
            ResolveFromODRController(odr);
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

        LOG_INFO("TrafficSignalController '{}' initialized: {} phases, delay={:.1f}s, {} signals resolved{}",
                 name_, phases_.size(), delay_, resolvedSignals_.size(),
                 reference_.empty() ? "" : " (ref=" + reference_ + ")");
    }

    void OSCTrafficSignalController::ResolveFromODRController(roadmanager::OpenDrive* odr)
    {
        // Find OpenDRIVE controller by name
        roadmanager::Controller* odrCtrl = nullptr;
        for (unsigned int i = 0; i < odr->GetNumberOfControllers(); i++)
        {
            auto* candidate = odr->GetControllerByIdx(static_cast<int>(i));
            if (candidate && candidate->GetName() == reference_)
            {
                odrCtrl = candidate;
                break;
            }
        }

        if (!odrCtrl)
        {
            LOG_ERROR("TrafficSignalController '{}': OpenDRIVE controller '{}' not found, falling back to phase-defined signals",
                      name_, reference_);
            return;
        }

        LOG_INFO("TrafficSignalController '{}': resolved OpenDRIVE controller '{}' with {} controls",
                 name_, reference_, odrCtrl->GetNumberOfControls());

        // Collect all signal IDs already referenced in phases
        std::unordered_map<int, bool> phaseSignalIds;
        for (const auto& phase : phases_)
        {
            for (const auto& ss : phase.signalStates)
            {
                phaseSignalIds[ss.signalId] = true;
            }
        }

        // Resolve controller signals and add defaults for unreferenced ones
        auto dynamicSignals = odr->GetDynamicSignals();
        for (unsigned int i = 0; i < odrCtrl->GetNumberOfControls(); i++)
        {
            auto* ctrl = odrCtrl->GetControl(i);
            if (!ctrl) continue;

            int sigId = ctrl->signalId_;

            // Resolve TrafficLight pointer
            if (resolvedSignals_.count(sigId) == 0)
            {
                roadmanager::TrafficLight* tl = nullptr;
                for (auto* signal : dynamicSignals)
                {
                    auto* candidate = dynamic_cast<roadmanager::TrafficLight*>(signal);
                    if (candidate && candidate->GetId() == sigId)
                    {
                        tl = candidate;
                        break;
                    }
                }

                if (tl)
                {
                    resolvedSignals_[sigId] = tl;
                    tl->SetHasOSCAction(true);
                }
                else
                {
                    LOG_WARN("TrafficSignalController '{}': ODR controller signal ID {} not found as dynamic signal", name_, sigId);
                    continue;
                }
            }

            // If signal not referenced in any phase, add all-off default state to each phase
            if (phaseSignalIds.count(sigId) == 0)
            {
                // Build all-off state string based on lamp count
                auto* tl = resolvedSignals_[sigId];
                std::string offState;
                for (int lamp = 0; lamp < static_cast<int>(tl->GetNrLamps()); lamp++)
                {
                    if (lamp > 0) offState += ";";
                    offState += "off";
                }

                for (auto& phase : phases_)
                {
                    phase.signalStates.push_back({sigId, offState});
                }

                LOG_INFO("TrafficSignalController '{}': added default all-off state for signal {} (from ODR controller '{}')",
                         name_, sigId, reference_);
            }
        }
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
