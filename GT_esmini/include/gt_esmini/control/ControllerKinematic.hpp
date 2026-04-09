#pragma once

#include <string>
#include "Controller.hpp"
#include "pugixml.hpp"
#include "Parameters.hpp"
#include "Entities.hpp"
#include "vehicle.hpp"
#include "RoadManager.hpp"

#define CONTROLLER_KINEMATIC_TYPE_NAME "KinematicController"

namespace gt_esmini
{
    /**
     * KinematicController: physically-based scenario following using a bicycle model.
     *
     * Instead of perfectly snapping to road geometry (like defaultController's MoveAlongS),
     * this controller uses a bicycle model to steer toward the scenario-driven target
     * position (object_->pos_) with realistic dynamics (rate-limited steering,
     * speed-dependent gain, heading inertia).
     *
     * Runs in MODE_ADDITIVE — does NOT override any domain.
     * All scenario actions (LaneChange, SpeedAction, Route, etc.) and defaultController
     * run normally, updating object_->pos_ as the "ideal path" target.
     * The bicycle model then produces physically plausible XY/heading to follow that path.
     *
     * Teleport actions (DirtyBit::TELEPORT) bypass the bicycle model and reset state.
     */
    class ControllerKinematic : public scenarioengine::Controller
    {
    public:
        struct Config
        {
            double look_ahead_time      = 0.8;    // seconds — multiplied by speed for look-ahead distance
            double min_look_ahead_dist  = 4.0;    // meters
            double max_look_ahead_dist  = 30.0;   // meters
            double max_steering_angle   = 1.047;  // radians (~60 degrees)
            double steering_speed_inertia = 0.005; // speed-dependent max-angle reduction at high speed
            double max_steering_rate     = 0.5;   // rad/s — fixed-speed interp rate (wheel_angle moves at most this fast)

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
        /// Compute the look-ahead target point from object's road position.
        /// Returns true if MoveAlongS succeeded, false if fallback was used.
        bool ComputeLookAheadTarget(double look_ahead_dist, double& target_x, double& target_y);

        vehicle::Vehicle      vehicle_;     // used only for wheelAngle_ / wheelRotation_ storage
        Config                 config_;

        // Internal state
        bool   initialized_;
        double prev_heading_error_;
    };

    scenarioengine::Controller* InstantiateControllerKinematic(void* args);
}  // namespace gt_esmini
