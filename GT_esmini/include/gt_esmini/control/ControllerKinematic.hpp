#pragma once

#include <string>
#include <vector>
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
     * KinematicController: physically-based scenario following using trajectory curvature.
     *
     * Builds a future trajectory polyline each frame that integrates ALL path sources:
     *   - Road/route geometry (incremental MoveAlongS in global x,y)
     *   - LaneChange / LaneOffset displacements (TransitionDynamics f(progress))
     *   - FollowTrajectoryAction (Shape::Evaluate sampling)
     *
     * Curvature is computed from global (x,y) via Menger formula — naturally
     * continuous across road connections, junctions, and action boundaries.
     *
     * Runs in MODE_ADDITIVE — does NOT override any domain.
     */
    class ControllerKinematic : public scenarioengine::Controller
    {
    public:
        struct Config
        {
            double trajectory_step        = 0.5;    // [m] polyline sampling interval
            double curvature_preview_time = 0.3;    // [s] preview distance = speed × this
            double min_preview_dist       = 2.0;    // [m]
            double max_preview_dist       = 12.0;   // [m]

            double max_steering_angle     = 1.047;  // [rad] ~60 degrees
            double steering_speed_inertia = 0.005;  // speed-dependent max-angle reduction
            double max_steering_rate      = 1.5;    // [rad/s] ≈ 86 deg/s
            double max_steering_accel     = 3.0;    // [rad/s²] steering rate change limit
            double output_smoothing_tau   = 0.05;   // [s] LPF time constant on final output

            bool debug_log = false;
        };

        ControllerKinematic(InitArgs* args);

        virtual const char* GetTypeName() const override { return CONTROLLER_KINEMATIC_TYPE_NAME; }
        virtual Controller::Type GetType() const override { return static_cast<Controller::Type>(CONTROLLER_TYPE_KINEMATIC); }

        void Init() override;
        void Step(double timeStep) override;
        int  Activate(const ControlActivationMode (&mode)[static_cast<unsigned int>(ControlDomains::COUNT)]);
        void ReportKeyEvent(int key, bool down);

        void LoadConfig(const std::string& configPath);
        void SetConfig(const Config& config) { config_ = config; }
        double GetWheelAngle() const { return vehicle_.wheelAngle_; }

    private:
        struct PathPoint { double x, y; };

        /// Build the unified future trajectory polyline.
        /// Integrates road geometry + LC/LaneOffset displacements, or
        /// FollowTrajectory shape sampling.
        void RebuildFuturePath(double total_dist, double speed);

        /// Try to build polyline from FollowTrajectoryAction. Returns true if active.
        bool BuildPathFromTrajectory(double total_dist);

        /// Build polyline from road/route with LC/LaneOffset displacements overlaid.
        void BuildPathFromRoad(double total_dist, double speed);

        /// Menger curvature from three (x,y) polyline points at preview distance.
        double CurvatureFromPath(double preview_dist) const;

        vehicle::Vehicle         vehicle_;
        Config                   config_;
        std::vector<PathPoint>   future_path_;

        bool   initialized_;
        double prev_curvature_;
        double prev_rate_;              // steering rate from previous frame [rad/s]
        double smoothed_output_;        // LPF-filtered final output [rad]
    };

    scenarioengine::Controller* InstantiateControllerKinematic(void* args);
}  // namespace gt_esmini
