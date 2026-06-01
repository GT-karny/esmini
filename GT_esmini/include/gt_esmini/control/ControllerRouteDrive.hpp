/*
 * GT_esmini - Extended esmini
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
#include "Controller.hpp"
#include "pugixml.hpp"
#include "Parameters.hpp"
#include "Entities.hpp"
#include "OSCPrivateAction.hpp"
#include "RoadManager.hpp"

#define CONTROLLER_ROUTE_DRIVE_TYPE_NAME "RouteDriveController"

namespace gt_esmini
{
    // User-range controller type id. Uses the reserved USER_CONTROLLER_TYPE_BASE
    // (=1000) range so we do NOT touch core's Controller::Type enum (R1 clean).
    // Looked up via Object::GetAssignedControllerOftype(static_cast<Type>(...)).
    constexpr int CONTROLLER_TYPE_ROUTE_DRIVE = 1001;  // USER_CONTROLLER_TYPE_BASE + 1

    /**
     * RouteDriveController: a "strong default controller".
     *
     * Lane-aware route following with automatic lane changes (reuses esmini's
     * roadmanager::LaneIndependentRouter), plus turn-signal (winker) pre-arming
     * N seconds before each lane change. Behaves like the plain default
     * controller (MoveAlongS) when no route is assigned.
     *
     * Runs MODE_ADDITIVE on the LATERAL domain; only the lane-change execution
     * temporarily takes MODE_OVERRIDE (mirrors ControllerFollowRoute). Writes
     * the vehicle position via the lane-change action; longitudinal motion is
     * left to the default controller. Steering wheel angle rendering is left to
     * a stacked ControllerKinematic, which reads the in-progress lane change via
     * GetActiveLaneChangeAction().
     */
    class ControllerRouteDrive : public scenarioengine::Controller
    {
    public:
        struct Config
        {
            double winker_lead_time       = 2.0;    // [s] indicator ON this long before LC trigger
            double lane_change_time       = 4.0;    // [s] LC transition duration
            double min_dist_for_collision = 10.0;   // [m] 0 disables collision check; also the required-gap floor
            double look_ahead_dist        = 200.0;  // [m] max distance ahead to start seeking a lane change (Timing=Early)
            double gap_comfort_distance   = 25.0;   // [m] comfortable target-lane gap required when Gap=Wide
            bool   debug_log              = false;
        };

        ControllerRouteDrive(InitArgs* args);
        ~ControllerRouteDrive();

        virtual const char* GetTypeName() const override { return CONTROLLER_ROUTE_DRIVE_TYPE_NAME; }
        virtual scenarioengine::Controller::Type GetType() const override
        {
            return static_cast<scenarioengine::Controller::Type>(CONTROLLER_TYPE_ROUTE_DRIVE);
        }

        void Init() override;
        void Step(double timeStep) override;
        int  Activate(const ControlActivationMode (&mode)[static_cast<unsigned int>(ControlDomains::COUNT)]) override;
        void ReportKeyEvent(int key, bool down) override;

        void LoadConfig(const std::string& configPath);
        void SetConfig(const Config& c) { config_ = c; }

        // Lane-change timing knobs (override JSON defaults; e.g. from CLI flags).
        // alpha (Timing): 0=Late .. 1=Early. beta (Gap): 0=Wide(cautious) .. 1=Tight(aggressive).
        void SetTimingGap(double alpha, double beta)
        {
            timing_alpha_ = CLAMP(alpha, 0.0, 1.0);
            gap_beta_     = CLAMP(beta, 0.0, 1.0);
        }

        // Exposed so a stacked ControllerKinematic can overlay the in-progress
        // lane change into its steering preview. Returns nullptr when not changing.
        const scenarioengine::LatLaneChangeAction* GetActiveLaneChangeAction() const
        {
            return changingLane_ ? laneChangeAction_ : nullptr;
        }

    private:
        enum class WaypointStatus
        {
            MISSED,
            PASSED,
            NOT_REACHED
        };

        void           CalculateWaypoints();
        void           CreateLaneChange(int lane);
        void           ChangeLane(double timeStep);
        bool           CanChangeLane(int lane);
        // True if target lane exists and is wide enough at the current s.
        bool           TargetLaneAvailable(int lane);
        // Nearest longitudinal gap (m) to a vehicle ahead/behind in the target lane on the
        // current road. LARGE_NUMBER when none. Used by the Gap timing knob.
        void           ComputeTargetLaneGaps(int lane, double& ahead, double& behind);
        void           UpdateWaypoints(roadmanager::Position vehiclePos, roadmanager::Position nextWaypoint);
        WaypointStatus GetWaypointStatus(roadmanager::Position vehiclePos, roadmanager::Position waypoint);
        double         DistanceBetween(roadmanager::Position p1, roadmanager::Position p2);
        void           Deactivate() override;

        // Turn-signal helpers. dir: +1 = vehicle-left, -1 = vehicle-right, 0 = off.
        int  LaneChangeDirection(const roadmanager::Position& pos, int targetLane) const;
        // Route-based junction turn direction (+1 left, -1 right, 0 straight/none).
        // Derived from our own waypoints_ (the heading of the road the vehicle drives
        // on AFTER the next junction vs. the current driving direction) — no geometric
        // probe guessing, since the route is known.
        int  JunctionTurnDirection() const;
        void ApplyIndicator(int dir);

        Config                                config_;
        scenarioengine::LatLaneChangeAction*  laneChangeAction_   = nullptr;
        roadmanager::OpenDrive*               odr_                = nullptr;
        std::vector<roadmanager::Position>    waypoints_;
        int                                   currentWaypointIndex_  = 0;
        int                                   scenarioWaypointIndex_ = 0;
        bool                                  changingLane_          = false;
        bool                                  pathCalculated_        = false;
        double                                minLaneWidth_          = 0.5;

        double timing_alpha_ = 0.5;     // Timing knob: 0=Late .. 1=Early (default Normal)
        double gap_beta_     = 0.5;     // Gap knob:    0=Wide .. 1=Tight (default Normal)

        int  laneChangeDir_   = 0;      // latched indicator direction during a change
        int  junctionTurnDir_ = 0;      // latched indicator direction while turning through a junction
        bool junctionArmed_   = false;  // junction-turn decision latched for the current maneuver
        int  lcDirThisRoad_   = 0;      // dir of last completed lane change on the current road (exit-lane detection)
        id_t prevTrackId_     = ID_UNDEFINED;  // detect road change to reset lcDirThisRoad_
        bool indicatorLeftOn_  = false; // cached output state (avoid redundant writes)
        bool indicatorRightOn_ = false;
    };

    scenarioengine::Controller* InstantiateControllerRouteDrive(void* args);
}  // namespace gt_esmini
