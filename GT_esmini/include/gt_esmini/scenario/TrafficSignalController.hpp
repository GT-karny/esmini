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

#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include "RoadManager.hpp"

namespace gt_esmini
{
    /**
     * @brief A single signal state within a phase (trafficSignalId + state string)
     */
    struct TrafficSignalPhaseState
    {
        int         signalId;  // OpenDRIVE signal ID
        std::string state;     // e.g., "on;off;off"
    };

    /**
     * @brief A phase in a traffic signal controller (name, duration, signal states)
     */
    struct TrafficSignalPhase
    {
        std::string                         name;
        double                              duration;  // seconds
        std::vector<TrafficSignalPhaseState> signalStates;
    };

    /**
     * @brief OpenSCENARIO TrafficSignalController runtime
     *
     * Manages phase cycling for a group of traffic signals.
     * Phases are defined in XOSC <RoadNetwork><TrafficSignals><TrafficSignalController>.
     * Auto-cycles through phases based on duration. Can be externally controlled
     * via TrafficSignalControllerAction to jump to a named phase.
     */
    class OSCTrafficSignalController
    {
    public:
        OSCTrafficSignalController(const std::string& name, double delay);

        /**
         * @brief Add a phase to this controller
         */
        void AddPhase(const TrafficSignalPhase& phase);

        /**
         * @brief Resolve signal IDs to TrafficLight pointers and apply initial phase
         * Must be called after OpenDRIVE is loaded.
         */
        void Init();

        /**
         * @brief Advance the controller by dt seconds (auto-cycling)
         */
        void Step(double dt);

        /**
         * @brief Jump to a named phase (resets phase elapsed time)
         * @return true if phase was found and set
         */
        bool SetPhase(const std::string& phaseName);

        /**
         * @brief Get the name of the current phase
         */
        std::string GetCurrentPhaseName() const;

        /**
         * @brief Get the controller name
         */
        const std::string& GetName() const { return name_; }

    private:
        void ApplyCurrentPhaseStates();

        std::string                    name_;
        double                         delay_;
        std::vector<TrafficSignalPhase> phases_;

        // Runtime state
        int    currentPhaseIndex_ = 0;
        double phaseElapsed_      = 0.0;
        double delayElapsed_      = 0.0;
        bool   started_           = false;
        bool   initialized_       = false;

        // Resolved signal pointers (signalId -> TrafficLight*)
        std::unordered_map<int, roadmanager::TrafficLight*> resolvedSignals_;
    };

    /**
     * @brief Singleton manager for all TrafficSignalControllers
     */
    class TrafficSignalControllerManager
    {
    public:
        static TrafficSignalControllerManager& Instance()
        {
            static TrafficSignalControllerManager instance;
            return instance;
        }

        /**
         * @brief Add a controller definition (before Init)
         */
        void AddController(OSCTrafficSignalController controller);

        /**
         * @brief Initialize all controllers (resolve signals, apply initial states)
         */
        void InitAll();

        /**
         * @brief Step all controllers (advance phase timers)
         */
        void StepAll(double dt);

        /**
         * @brief Find a controller by name
         * @return pointer to controller, or nullptr if not found
         */
        OSCTrafficSignalController* GetController(const std::string& name);

        /**
         * @brief Clear all controllers (on scenario close)
         */
        void Clear();

    private:
        TrafficSignalControllerManager() = default;
        std::vector<OSCTrafficSignalController> controllers_;
    };

}  // namespace gt_esmini
