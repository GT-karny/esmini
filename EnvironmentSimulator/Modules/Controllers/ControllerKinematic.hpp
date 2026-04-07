/*
 * esmini - Environment Simulator Minimalistic
 * https://github.com/esmini/esmini
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) partners of Simulation Scenarios
 * https://sites.google.com/view/simulationscenarios
 */

#pragma once

#include <string>
#include "Controller.hpp"
#include "pugixml.hpp"
#include "Parameters.hpp"
#include "Entities.hpp"
#include "vehicle.hpp"
#include "RoadManager.hpp"

#define CONTROLLER_KINEMATIC_TYPE_NAME "KinematicController"

namespace scenarioengine
{
    class ScenarioPlayer;
    class ScenarioEngine;

    /**
     * KinematicController: physically-based scenario following using a bicycle model.
     *
     * Instead of perfectly snapping to road geometry (like defaultController's MoveAlongS),
     * this controller maintains an internal "ghost" position that follows the scenario path,
     * and uses a bicycle model to steer toward it with realistic dynamics (rate-limited
     * steering, speed-dependent gain, heading inertia).
     *
     * Overrides the LATERAL domain only.
     * Speed target comes from scenario (SpeedAction via obj->speed_), with
     * curvature-adaptive reduction applied by the controller.
     * XY position and heading are computed by the bicycle model.
     * Road coordinates (s, t, lane_id) are reverse-computed via XYZ2TrackPos.
     *
     * Teleport actions (DirtyBit::TELEPORT) bypass the bicycle model and reset state.
     */
    class ControllerKinematic : public Controller
    {
    public:
        struct Config
        {
            double look_ahead_time      = 0.8;    // seconds — multiplied by speed for look-ahead distance
            double min_look_ahead_dist  = 2.0;    // meters
            double max_look_ahead_dist  = 30.0;   // meters
            double max_steering_angle   = 1.047;  // radians (~60 degrees)
            double max_steering_rate    = 5.0;    // rad/s
            double max_lateral_error    = 10.0;   // meters — error threshold for simulation stop
            double pd_kp               = 2.0;     // PD proportional gain
            double pd_kd               = 0.5;     // PD derivative gain
            double steering_speed_inertia = 0.01; // speed-dependent steering gain factor
            double max_acc             = 10.0;    // m/s² — acceleration limit for speed convergence
            double max_dec             = 10.0;    // m/s² — deceleration limit for speed convergence
            double max_speed           = 100.0;   // m/s
            double curve_speed_reduction_k  = 0.6;  // quadratic reduction coefficient (0=disabled, 1=full)
            double curve_speed_min_factor   = 0.2;  // minimum speed fraction (never reduce below 20%)

            enum class RoadEndBehavior
            {
                INERTIA,     // continue straight with current heading/speed
                STOP,        // decelerate to zero
                HALT_ERROR   // error stop the simulation
            };
            RoadEndBehavior road_end_behavior = RoadEndBehavior::INERTIA;

            bool debug_log = false;
        };

        ControllerKinematic(InitArgs* args);

        virtual const char* GetTypeName()
        {
            return CONTROLLER_KINEMATIC_TYPE_NAME;
        }
        virtual int GetType()
        {
            return CONTROLLER_TYPE_KINEMATIC;
        }

        void Init() override;
        void Step(double timeStep) override;
        int  Activate(const ControlActivationMode (&mode)[static_cast<unsigned int>(ControlDomains::COUNT)]);
        void ReportKeyEvent(int key, bool down);

        /// Load configuration from a JSON file.
        void LoadConfig(const std::string& configPath);

        /// Set configuration programmatically.
        void SetConfig(const Config& config) { config_ = config; }

        /// Get current wheel angle (for HVD reporting).
        double GetWheelAngle() const { return vehicle_.wheelAngle_; }

    private:
        /// Reset bicycle model and ghost position to match the object's current state.
        void ResetToObject();

        /// Compute the look-ahead target point from ghost position.
        void ComputeLookAheadTarget(double speed, double& target_x, double& target_y);

        vehicle::Vehicle      vehicle_;     // kinematic bicycle model
        roadmanager::Position  ghostPos_;   // internal "scenario target" position
        Config                 config_;

        // Internal state
        bool   initialized_;
        bool   ghost_valid_;
        double prev_heading_error_;
    };

    Controller* InstantiateControllerKinematic(void* args);
}  // namespace scenarioengine
