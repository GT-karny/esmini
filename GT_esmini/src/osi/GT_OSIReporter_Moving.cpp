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

#include "CommonMini.hpp"
#include "OSIReporter.hpp"
#include "GT_OSIReporter_Internals.hpp"
#include "gt_esmini/control/common/TransitionDynamics.hpp"
#include "gt_esmini/osi/GT_PlannedPathRegistry.hpp"
#include <array>
#include <cctype>
#include <cstdlib>
#include <map>

constexpr const char *SOURCE_REF_TYPE_OSC = "net.asam.openscenario";

// [GT_MOD #37 G4] Env gate for the GT-only future_trajectory (Shadow Simulation) output.
// Default ON (GT behavior unchanged): the gate only disables when GT_OSI_FUTURE_TRAJECTORY is
// explicitly set to a falsy value (0/false/off/no). Unset or any other value -> enabled.
// Rationale: the projected trajectory inflates/reshapes the OSI MovingObject message relative to
// pristine upstream, which is intentional GT surface -- but upstream unit tests
// (GroundTruthTests.check_* exact serialized sizes, GetOSIRoadLaneTest.lane_no_obj) assert
// byte-exact upstream layouts. Those test binaries cannot call GT config APIs, so an env var
// (read once, same lazy-init idiom as GT_ODR_OSI_AUTHORED_JUNCTION_BOUNDARY in OdrJunctionGeom)
// lets run_tests.sh turn the field off for the upstream suites only.
static bool FutureTrajectoryEnabled()
{
    static int cached = -1;  // -1 = uninitialized, 0 = disabled, 1 = enabled
    if (cached < 0)
    {
        bool        disabled = false;
        const char *v        = std::getenv("GT_OSI_FUTURE_TRAJECTORY");
        if (v != nullptr && v[0] != '\0')
        {
            std::string s(v);
            for (char &c : s)
            {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            disabled = (s == "0" || s == "false" || s == "off" || s == "no");
        }
        cached = disabled ? 0 : 1;
    }
    return cached == 1;
}

// [GT_MOD] Resolve the OSI assigned_lane_id (lane global id) for a moving object.
//
// osi_object.proto defines assigned_lane_id as the lane(s) the object is *assigned*
// to (semantic membership), not merely a lane the body geometrically overlaps.
// Position::GetLaneGlobalId() re-derives the lane from (s_, t_) on every call via
// GetClosestLaneIdx(..., LANE_TYPE_ANY), so a driving vehicle whose lateral offset
// drifts outward (e.g. VirtualDriver writing back world coordinates with an
// intentional lateral lag) gets reported as assigned to a border/sidewalk lane
// (-2 / -3) even while the object's own cached driving lane (Position::GetLaneId(),
// snapped with LANE_TYPE_ANY_DRIVING) stays -1. That contradicts the object's own
// reported track/lane and the "assigned" semantics of the field.
//
// This helper mirrors GetLaneGlobalId()'s junction handling but, for the plain lane
// case, returns the global id of the cached driving lane instead of re-searching all
// lane types. It falls back to GetLaneGlobalId() whenever the road / lane section is
// unavailable or the cached lane id has no global id in the current section, so the
// output never regresses relative to the previous behaviour. Scope is intentionally
// limited to the moving-object assigned_lane_id: GetLaneGlobalId() itself is left
// untouched for its other callers (adjacency scan, RouteSignalScan, HVD) that
// legitimately want "any lane the position sits on".
static id_t ResolveMovingObjectAssignedLaneGlobalId(const roadmanager::Position &pos)
{
    using namespace roadmanager;

    Road *road = pos.GetRoadById(pos.GetTrackId());
    if (road == nullptr)
    {
        return pos.GetLaneGlobalId();  // no road: defer to the canonical resolver
    }

    // Parity with GetLaneGlobalId(): an object on an OSI-intersection connecting
    // road is assigned to the intersection itself.
    if (road->GetJunction() != ID_UNDEFINED)
    {
        Junction *junction = Position::GetOpenDrive()->GetJunctionById(road->GetJunction());
        if (junction != nullptr && junction->IsOsiIntersection())
        {
            return junction->GetGlobalId();
        }
    }

    LaneSection *lane_section = road->GetLaneSectionByS(pos.GetS());
    if (lane_section == nullptr)
    {
        return pos.GetLaneGlobalId();
    }

    id_t global_id = lane_section->GetLaneGlobalIdById(pos.GetLaneId());
    if (global_id == ID_UNDEFINED)
    {
        return pos.GetLaneGlobalId();
    }

    return global_id;
}

// [GT_MOD] Longitudinal state the projected-trajectory fallback keeps per object, so
// it can extrapolate a DECELERATION instead of a flat speed when no storyboard
// SpeedAction is visible (a controller holding the LONG domain in MODE_OVERRIDE makes
// LongSpeedAction::Start() End() the action immediately, so it never appears in
// getPrivateActions()).
//
// The predecessor of this map was a function-static "last commanded target speed" that
// NOTHING ever cleared, not even a scenario reload, so a second run in the same process
// (web backend / in-process gt_sim_test runs) started out predicting the previous run's
// target speed. This one is dropped whenever sim time moves backwards.
struct ProjectedLonState
{
    double last_time  = -1.0;
    double last_speed = 0.0;
    double acc        = 0.0;  // low-pass filtered dv/dt [m/s^2]
};
static std::map<int, ProjectedLonState> g_projected_lon_state;
static double                           g_projected_last_sim_time = -1.0;

// [GT_MOD] Helper to generate projected trajectory based on road geometry and active actions (Shadow Simulation)
static void GenerateProjectedTrajectory(const scenarioengine::Object& objectStateRef, scenarioengine::ScenarioEngine* scenario_engine)
{
    if (!scenario_engine) return;
    const scenarioengine::Object* objectState = &objectStateRef;
    int id = objectState->id_;

    auto* simObj = scenario_engine->entities_.GetObjectById(id);
    if (!simObj) return;

    // Scenario (re)load guard: sim time restarting drops every per-object memory.
    const double sim_time_now = scenario_engine->getSimulationTime();
    if (sim_time_now < g_projected_last_sim_time - SMALL_NUMBER)
    {
        g_projected_lon_state.clear();
    }
    g_projected_last_sim_time = sim_time_now;

    // Shadow simulation: clone the current position. The clone keeps the route
    // assignment, which is what makes the walk below branch correctly at junctions.
    roadmanager::Position ghostPos = simObj->pos_;

    // [GT_MOD] Recover from a snap onto a road that is NOT on the route (e.g. a body
    // overlapping a parallel connector inside a junction makes Position pick road 7
    // where the route runs over road 13). Probe every route waypoint road and keep the
    // one whose lateral distance to the current XYZ is smallest, within 5 m.
    //
    // This used to be applied to the REAL entity (targetObj->pos_) from inside the OSI
    // reporter, i.e. an output stage writing simulation state that then fed the next
    // frame's controller input. It is ghost-local now: the reported line gets the same
    // correction, the simulation does not move.
    if (simObj->pos_.GetRoute() && simObj->pos_.GetRoute()->IsValid())
    {
        const auto& waypoints     = simObj->pos_.GetRoute()->minimal_waypoints_;
        const id_t  currentRoadId = ghostPos.GetTrackId();

        bool roadOnRoute = false;
        for (const auto& wp : waypoints)
        {
            if (wp.GetTrackId() == currentRoadId)
            {
                roadOnRoute = true;
                break;
            }
        }

        if (!roadOnRoute && !waypoints.empty())
        {
            const double curX = ghostPos.GetX();
            const double curY = ghostPos.GetY();
            const double curZ = ghostPos.GetZ();

            double bestT      = LARGE_NUMBER;
            id_t   bestRoadId = ID_UNDEFINED;
            for (const auto& wp : waypoints)
            {
                roadmanager::Position tempPos;
                tempPos.XYZ2TrackPos(curX, curY, curZ, roadmanager::Position::PosMode::UNDEFINED, false, wp.GetTrackId());
                const double t_abs = std::abs(tempPos.GetT());
                if (t_abs < bestT)
                {
                    bestT      = t_abs;
                    bestRoadId = wp.GetTrackId();
                }
            }

            // 5 m tolerance: same lane or the one next to it, never a different corridor.
            if (bestRoadId != ID_UNDEFINED && bestRoadId != currentRoadId && bestT < 5.0)
            {
                ghostPos.XYZ2TrackPos(curX, curY, curZ, roadmanager::Position::PosMode::UNDEFINED, false, bestRoadId);
            }
        }
    }

    // Introspect active actions
    scenarioengine::LatLaneChangeAction* activeLcAction = nullptr;
    scenarioengine::LongSpeedAction* activeSpeedAction = nullptr;
    
    auto privateActions = simObj->getPrivateActions();
    
    for (auto* action : privateActions)
    {
         std::string typeStr = action->Type2Str();
         if (typeStr == "LaneChangeAction" && !activeLcAction)
         {
             activeLcAction = static_cast<scenarioengine::LatLaneChangeAction*>(action);
         }
         else if (typeStr == "SpeedAction" && !activeSpeedAction)
         {
             activeSpeedAction = static_cast<scenarioengine::LongSpeedAction*>(action);
         }
    }

    // [GT_MOD] Start the walk from the CURRENT lane center rather than from the
    // vehicle's lateral offset: a controller running a cross-track error (or a vehicle
    // cutting a corner) would otherwise either drag that offset along the whole
    // prediction, or make an intermediate MoveAlongS snap into a border/sidewalk lane.
    //
    // NOT while a lane change is running. There the walk keeps the CAR-anchored base and
    // the overlay below adds the maneuver's REMAINING displacement on top; re-centering
    // first would count the part already travelled twice. (TrajectoryShortPlanner makes
    // exactly the same distinction -- it only anchors when lat_actions is empty.) Measured
    // with the reset left in: the reported line ran out to +7.04 m where the target lane
    // centre is +3.58 m, i.e. two lane widths.
    if (!activeLcAction && std::abs(ghostPos.GetOffset()) > 0.001)
    {
        ghostPos.SetLanePos(ghostPos.GetTrackId(), ghostPos.GetLaneId(), ghostPos.GetS(), 0.0);
        // Also fix heading to align with road
        ghostPos.SetHeadingRelative(0.0);
    }

    // [GT_MOD] Clone the route so the walk cannot corrupt the shared route state:
    // ghostPos advances waypoints during the prediction, and sharing the pointer made
    // the real object skip turns (and double-free on destruction).
    if (simObj->pos_.GetRoute())
    {
        roadmanager::Route* clonedRoute = new roadmanager::Route();
        clonedRoute->CopyFrom(*simObj->pos_.GetRoute());
        ghostPos.SetRoute(clonedRoute);
        // ghostPos now owns clonedRoute and will delete it in its destructor.
    }

    // NOTE -- deliberately NO lane forcing here. An earlier version snapped ghostPos
    // onto the lane named by the route waypoint for the current road. That made the
    // reported line leave the lane the vehicle is actually driving the instant the two
    // disagreed: measured as a full lane width (3.57 m) of lateral error from the FIRST
    // sample on, for a vehicle that never changed lane. It was not needed for junction
    // branching either. Position::MoveToConnectingRoad already picks the ROAD from the
    // route and the LANE from the current lane's lane links (RoadManager.cpp,
    // ELEMENT_TYPE_JUNCTION branch), so handing the walk a valid route is enough.

    // ---------------------------------------------------------------------------
    // Longitudinal prediction.
    //
    // Two sources, in priority order:
    //   (1) a RUNNING storyboard SpeedAction -> shadow its own TransitionDynamics;
    //   (2) nothing visible -> extrapolate the object's measured speed and its
    //       measured deceleration down to a stop.
    //
    // (2) exists because a controller that holds the LONG domain in MODE_OVERRIDE
    // makes LongSpeedAction::Start() call End() immediately, so the action never
    // shows up in getPrivateActions() even though the vehicle is visibly braking.
    // ---------------------------------------------------------------------------

    // Measured longitudinal state (also feeds the (2) fallback below).
    const double current_speed = simObj->GetSpeed();
    ProjectedLonState& lon = g_projected_lon_state[id];
    if (lon.last_time >= 0.0)
    {
        const double dt_meas = sim_time_now - lon.last_time;
        if (dt_meas > 1e-4 && dt_meas < 1.0)
        {
            const double acc_raw = (current_speed - lon.last_speed) / dt_meas;
            // Light low-pass: a raw per-frame difference is noisy enough that the
            // reported path length would flicker frame to frame.
            lon.acc = 0.7 * lon.acc + 0.3 * acc_raw;
        }
    }
    lon.last_time  = sim_time_now;
    lon.last_speed = current_speed;

    // Shadow copy of the action's dynamics state, when there is an action.
    scenarioengine::OSCPrivateAction::TransitionDynamics speedDynamics;
    bool usingSpeedAction = false;

    if (activeSpeedAction)
    {
        // Copy the transition VERBATIM -- do not Reset() it.
        //
        // The previous version called Reset() (which zeroes param_val_, start_val_,
        // target_val_ AND scale_factor_) and then re-anchored start_val_ to the
        // current speed while leaving param_target_val_ at the action's FULL original
        // span. That predicts "the whole transition starts again, now", so every
        // frame the predicted stop point ran further ahead than the real one: with a
        // 30 m/s -> 0 over 6 s action, at t+4 s the reported line was 26.8 m long
        // where the true remaining distance was 10.3 m. Reset() also discarded the
        // scale_factor_ that SetMaxRate() had installed for the vehicle's
        // performance limits.
        //
        // The live transition_ already carries the correct start/target, the elapsed
        // param_val_ and the scale factor, so stepping the copy forward from where
        // the action actually is reproduces the remaining profile exactly.
        speedDynamics    = activeSpeedAction->transition_;
        usingSpeedAction = true;
    }

    // [GT_MOD] Lane-change overlay state, captured ONCE before the walk.
    //
    // The displacement is applied in WORLD coordinates to the emitted point, NOT through
    // MoveAlongS's dLaneOffset. Feeding it as dLaneOffset made the walk cross a lane
    // boundary, at which point Position re-snapped lane_id_ and GetOffset() started
    // reporting against the NEW lane -- so the next step's "desired minus current" was one
    // lane too large and the reported path was pushed a further lane out. Measured on a
    // one-lane change: the first reported point sat at +6.68 m where the target lane centre
    // is +3.58 m, converging back only as the transition saturated.
    //
    // lane_sign undoes OSCPrivateAction's lane-sign-agnostic storage, exactly as
    // TrajectoryShortPlanner's own overlay does; without it the displacement goes the wrong
    // way on one side of the road.
    bool   lc_valid      = false;
    double lc_start_val  = 0.0;
    double lc_amplitude  = 0.0;
    double lc_param_end  = 0.0;
    double lc_param_now  = 0.0;
    double lc_offset_now = 0.0;
    bool   lc_time_based = false;
    double lc_lane_sign  = 1.0;
    scenarioengine::OSCPrivateAction::DynamicsShape lc_shape =
        scenarioengine::OSCPrivateAction::DynamicsShape::LINEAR;

    if (activeLcAction)
    {
        const auto& td = activeLcAction->transition_;
        if (td.GetParamTargetVal() > 1e-6)
        {
            lc_valid      = true;
            lc_shape      = td.shape_;
            lc_start_val  = td.GetStartVal();
            lc_amplitude  = td.GetTargetVal() - lc_start_val;
            lc_param_end  = td.GetParamTargetVal();
            lc_param_now  = td.GetParamVal();
            lc_offset_now = gt_esmini::EvaluateTransitionShape(lc_shape, lc_start_val, lc_amplitude, lc_param_now / lc_param_end);
            lc_time_based = (td.dimension_ == scenarioengine::OSCPrivateAction::DynamicsDimension::TIME ||
                             td.dimension_ == scenarioengine::OSCPrivateAction::DynamicsDimension::RATE);
            lc_lane_sign  = static_cast<double>(SIGN(ghostPos.GetLaneId()));
        }
    }
    const double lc_nominal_speed = std::max(0.1, std::abs(current_speed));

    double current_time = sim_time_now;
    int samples = 20;
    double dt = 0.5;

    for (int i = 1; i <= samples; ++i)
    {
        double t_future = current_time + i * dt;
        double dt_step = dt;

        // Calculate speed for this step
        double speed = 0.0;

        if (usingSpeedAction)
        {
            // Always step by dt (seconds), for EVERY dimension -- this mirrors
            // LongSpeedAction::Step(), which does exactly the same.
            //
            // It looks wrong for DynamicsDimension::DISTANCE and RATE, and it is the
            // fix for both:
            //   - DISTANCE: LongSpeedAction::Start() CONVERTS the authored distance
            //     into a duration (SetParamTargetVal(2*dist/(v0+v1))) while leaving
            //     dimension_ == DISTANCE. The old code read dimension_ and stepped
            //     the parameter by speed*dt METRES into a SECONDS-valued parameter,
            //     so at 30 m/s one 0.5 s sample advanced it by 15 "seconds" and the
            //     parameter saturated immediately: the predicted speed dropped to the
            //     target on sample 2 and the whole reported line collapsed to 1.4 m
            //     against a true 76 m of braking distance. That is the reported
            //     "the line stops far short of the actual stop position".
            //   - RATE: had no branch at all, so param_val_ never advanced and
            //     Evaluate() returned the start value forever -- a flat-speed line
            //     that never decelerated (298 m reported against 76 m real).
            //     UpdateRate() has already turned the rate into a duration in
            //     param_target_val_, so dt is the right step here too.
            // Trapezoid, not end-of-interval. Evaluate() BEFORE and AFTER the step and
            // average: for a linear ramp that is the exact mean speed over the
            // interval, whereas sampling only the end value understates every step by
            // a*dt/2 and the error accumulates over the whole horizon. Measured on a
            // 30 m/s -> 0 at 5 m/s^2 stop, dt = 0.5 s: end-of-interval sampling put the
            // predicted stop 6.9 m short of the real one at the start of braking and
            // 1.3 m short near the end -- i.e. the reported line stopped before the
            // vehicle did, which is precisely what it must not do.
            const double v_begin = speedDynamics.Evaluate();
            speedDynamics.Step(dt_step);
            const double v_end = speedDynamics.Evaluate();
            speed = 0.5 * (v_begin + v_end);
        }
        else
        {
            // No visible action: extrapolate the measurement. Only DECELERATION is
            // extrapolated -- an acceleration held for 10 s would overstate the path
            // badly, while a deceleration held to v=0 is exactly the question being
            // asked ("where will it stop?"). Below the noise floor, hold the speed.
            //
            // Sampled at the interval MIDPOINT for the same reason as the trapezoid
            // above: it is the exact mean of a constant-deceleration ramp. Clamped at
            // 0 so a decelerating vehicle piles the remaining samples on its stop
            // position instead of reversing through it.
            const double t_mid = (static_cast<double>(i) - 0.5) * dt;
            if (lon.acc < -0.05)
            {
                speed = std::max(0.0, current_speed + lon.acc * t_mid);
            }
            else
            {
                speed = current_speed;
            }
        }

        double ds = speed * dt_step;
        
        // Lateral: with no lane change, steer the walk back to the lane centre and align
        // with the road. During a lane change, hold the car-anchored offset instead (see
        // the reset guard above) -- the displacement is added in world coordinates at emit
        // time and is never fed back into the walk, because feeding it through MoveAlongS's
        // dLaneOffset let a lane re-snap turn GetOffset() into a different frame mid-walk.
        double dLaneOffset = 0.0;
        if (!lc_valid)
        {
            dLaneOffset = -ghostPos.GetOffset();
            ghostPos.SetHeadingRelative(0.0);
        }

        // Move the ghost position forward.
        //
        // junctionSelectorAngle is 0.0 (straight-most), NOT the -1.0 convenience value
        // that used to be here. -1.0 means "pick at random" whenever the walk reaches a
        // junction with no route to steer it, so the reported path flickered between
        // the straight and the turning connector from frame to frame. This is the same
        // trap TrajectoryShortPlanner documents for the driver preview (issue #31); a
        // valid on-route route still steers inside MoveToConnectingRoad, so this only
        // changes the off-route case, from random to deterministic.
        auto ret = ghostPos.MoveAlongS(ds, dLaneOffset, 0.0, true, roadmanager::Position::MoveDirectionMode::HEADING_DIRECTION, true);

        // [GT_MOD] Fallback: retry without route bookkeeping (e.g. past end of route).
        if (static_cast<int>(ret) < 0)
        {
             ret = ghostPos.MoveAlongS(ds, dLaneOffset, 0.0, true, roadmanager::Position::MoveDirectionMode::HEADING_DIRECTION, false);
        }

        // NOTE -- deliberately NO lane forcing on road transition either (see the
        // matching note at the top of this function). MoveToConnectingRoad resolves the
        // outgoing lane from the current lane's lane links, which is what a vehicle
        // holding its lane through a junction actually does. Overwriting it with the
        // route waypoint's lane teleported the reported line sideways every time the
        // vehicle was not in the exact lane the route named.

        // Add point to OSI message
        auto* point = obj_osi_internal.mobj->add_future_trajectory();
        point->mutable_timestamp()->set_seconds((long long)t_future);
        point->mutable_timestamp()->set_nanos((int)((t_future - (long long)t_future) * 1e9));
        
        // Report the path of the reference point base.position uses -- the bounding-box
        // CENTER (Position::GetOsiX/Y/Z = origin + R(h,p,r) * bbox center), not the
        // object origin. Emitting raw GetX/GetY here put the whole line one center
        // offset behind the vehicle: measured 1.40 m for the standard car, so a
        // consumer drawing the box and the path together saw the path start behind the
        // rear of the car it belongs to. Recomputed per sample because the rotation
        // follows the predicted heading, not the current one.
        // Lane-change displacement, in world coordinates, relative to the lane centre the
        // walk is following.
        double lc_dx = 0.0, lc_dy = 0.0;
        if (lc_valid)
        {
            const double advance   = lc_time_based ? (i * dt) : (i * dt * lc_nominal_speed);
            const double future_p  = std::min(lc_param_now + advance, lc_param_end);
            const double future_off = gt_esmini::EvaluateTransitionShape(lc_shape, lc_start_val, lc_amplitude, future_p / lc_param_end);
            const double delta_t   = (future_off - lc_offset_now) * lc_lane_sign;
            const double road_h    = ghostPos.GetHRoad();
            lc_dx = delta_t * -std::sin(road_h);
            lc_dy = delta_t *  std::cos(road_h);
        }

        double bb_dx = 0.0, bb_dy = 0.0, bb_dz = 0.0;
        RotateVec3d(ghostPos.GetH(),
                    ghostPos.GetP(),
                    ghostPos.GetR(),
                    static_cast<double>(objectState->boundingbox_.center_.x_),
                    static_cast<double>(objectState->boundingbox_.center_.y_),
                    static_cast<double>(objectState->boundingbox_.center_.z_),
                    bb_dx,
                    bb_dy,
                    bb_dz);

        point->mutable_position()->set_x(ghostPos.GetX() + lc_dx + bb_dx);
        point->mutable_position()->set_y(ghostPos.GetY() + lc_dy + bb_dy);
        point->mutable_position()->set_z(ghostPos.GetZ() + bb_dz);
        
        point->mutable_orientation()->set_yaw(ghostPos.GetH());
        point->mutable_orientation()->set_roll(ghostPos.GetR()); 
        point->mutable_orientation()->set_pitch(ghostPos.GetP());
    }
}

int OSIReporter::UpdateOSIMovingObject(const scenarioengine::Object &objectState)
{
    // Create OSI Moving object
    obj_osi_internal.mobj = obj_osi_internal.dynamic_gt->add_moving_object();

    // Set OSI Moving Object Mutable ID
    obj_osi_internal.mobj->mutable_id()->set_value(objectState.g_id_);

    // Set OSI Moving Object Type and Classification
    std::string entity_type = "Vehicle";
    if (objectState.type_ == Object::Type::VEHICLE)
    {
        obj_osi_internal.mobj->set_type(osi3::MovingObject::Type::MovingObject_Type_TYPE_VEHICLE);

        if (objectState.category_ == static_cast<int>(Vehicle::Category::CAR))
        {
            obj_osi_internal.mobj->mutable_vehicle_classification()->set_type(osi3::MovingObject_VehicleClassification::TYPE_MEDIUM_CAR);
        }
        else if (objectState.category_ == static_cast<int>(Vehicle::Category::BICYCLE))
        {
            obj_osi_internal.mobj->mutable_vehicle_classification()->set_type(osi3::MovingObject_VehicleClassification::TYPE_BICYCLE);
        }
        else if (objectState.category_ == static_cast<int>(Vehicle::Category::BUS))
        {
            obj_osi_internal.mobj->mutable_vehicle_classification()->set_type(osi3::MovingObject_VehicleClassification::TYPE_BUS);
        }
        else if (objectState.category_ == static_cast<int>(Vehicle::Category::MOTORBIKE))
        {
            obj_osi_internal.mobj->mutable_vehicle_classification()->set_type(osi3::MovingObject_VehicleClassification::TYPE_MOTORBIKE);
        }
        else if (objectState.category_ == static_cast<int>(Vehicle::Category::SEMITRAILER))
        {
            obj_osi_internal.mobj->mutable_vehicle_classification()->set_type(osi3::MovingObject_VehicleClassification::TYPE_SEMITRAILER);
        }
        else if (objectState.category_ == static_cast<int>(Vehicle::Category::TRAIN))
        {
            obj_osi_internal.mobj->mutable_vehicle_classification()->set_type(osi3::MovingObject_VehicleClassification::TYPE_TRAIN);
        }
        else if (objectState.category_ == static_cast<int>(Vehicle::Category::TRAM))
        {
            obj_osi_internal.mobj->mutable_vehicle_classification()->set_type(osi3::MovingObject_VehicleClassification::TYPE_TRAM);
        }
        else if (objectState.category_ == static_cast<int>(Vehicle::Category::TRUCK))
        {
            obj_osi_internal.mobj->mutable_vehicle_classification()->set_type(osi3::MovingObject_VehicleClassification::TYPE_HEAVY_TRUCK);
        }
        else if (objectState.category_ == static_cast<int>(Vehicle::Category::TRAILER))
        {
            obj_osi_internal.mobj->mutable_vehicle_classification()->set_type(osi3::MovingObject_VehicleClassification::TYPE_TRAILER);
        }
        else if (objectState.category_ == static_cast<int>(Vehicle::Category::VAN))
        {
            obj_osi_internal.mobj->mutable_vehicle_classification()->set_type(osi3::MovingObject_VehicleClassification::TYPE_DELIVERY_VAN);
        }
        else
        {
            LOG_ERROR("OSIReporter::UpdateOSIMovingObject -> Unsupported moving object vehicle category: {} ({}). Set to UNKNOWN.",
                      objectState.category_,
                      Vehicle::Category2String(objectState.category_));
            obj_osi_internal.mobj->mutable_vehicle_classification()->set_type(osi3::MovingObject_VehicleClassification::TYPE_UNKNOWN);
        }

        // GT_esmini (R5-U4): OSI light state read directly from the native
        // vehLghtStsList[] storage -- the R5-U3 single source of truth. Ported from
        // upstream OSIReporter::UpdateOSIMovingObject (commit 8d2ebfb7 "Add basic
        // VehicleLightState info in API"). This replaces the former g_LightStateProvider
        // hook, which after R5-U3 was only a thin indirection over the same storage
        // (GT_SetLightStateProvider -> gt_esmini::ReadLight -> vehLghtStsList[]).
        //
        // has_lightstate_action_ latches per object once it has ever emitted a light
        // change (DirtyBit::LIGHT_STATE, set by VehicleLightBridge::ApplyLight and native
        // LightStateActions); thereafter the current state is reported every frame.
        const id_t light_obj_id = static_cast<id_t>(objectState.id_);

        if (objectState.dirty_.Check(static_cast<uint64_t>(Object::DirtyBit::LIGHT_STATE)))
        {
            if (light_obj_id >= has_lightstate_action_.size())
            {
                has_lightstate_action_.resize(light_obj_id + 1, 0);
            }

            has_lightstate_action_[light_obj_id] = 1;
        }

        if (light_obj_id < has_lightstate_action_.size() && has_lightstate_action_[light_obj_id] == 1)
        {
            auto light_state     = obj_osi_internal.mobj->mutable_vehicle_classification()->mutable_light_state();
            auto indicator_state = osi3::MovingObject_VehicleClassification_LightState_IndicatorState_INDICATOR_STATE_OFF;

            for (size_t i = 0; i < static_cast<size_t>(Object::VehicleLightType::VEHICLE_LIGHT_SIZE); i++)
            {
                const Object::VehicleLightMode &light_mode = objectState.vehLghtStsList[i].mode;

                if (light_mode == Object::VehicleLightMode::UNKNOWN)
                {
                    continue;  // If mode not set, move to next light
                }

                const Object::VehicleLightType &light_type = objectState.vehLghtStsList[i].type;

                switch (light_type)
                {
                    case Object::VehicleLightType::DAYTIME_RUNNING_LIGHTS:
                        light_state->set_head_light(GetGenericLightMode(light_mode));
                        break;
                    case Object::VehicleLightType::LOW_BEAM:
                        if (objectState.vehLghtStsList[static_cast<size_t>(Object::VehicleLightType::DAYTIME_RUNNING_LIGHTS)].mode ==
                                Object::VehicleLightMode::ON ||
                            objectState.vehLghtStsList[static_cast<size_t>(Object::VehicleLightType::DAYTIME_RUNNING_LIGHTS)].mode ==
                                Object::VehicleLightMode::FLASHING)
                        {
                            break;
                        }
                        light_state->set_head_light(GetGenericLightMode(light_mode));
                        break;
                    case Object::VehicleLightType::HIGH_BEAM:
                        light_state->set_high_beam(GetGenericLightMode(light_mode));
                        break;
                    case Object::VehicleLightType::FOG_LIGHTS:
                        light_state->set_front_fog_light(GetGenericLightMode(light_mode));
                        light_state->set_rear_fog_light(GetGenericLightMode(light_mode));
                        break;
                    case Object::VehicleLightType::FOG_LIGHTS_FRONT:
                        light_state->set_front_fog_light(GetGenericLightMode(light_mode));
                        break;
                    case Object::VehicleLightType::FOG_LIGHTS_REAR:
                        light_state->set_rear_fog_light(GetGenericLightMode(light_mode));
                        break;
                    case Object::VehicleLightType::BRAKE_LIGHTS:
                        light_state->set_brake_light_state(GetBrakeLightMode(light_mode, objectState.vehLghtStsList[i].luminousIntensity));
                        break;
                    case Object::VehicleLightType::WARNING_LIGHTS:
                        if (light_mode != Object::VehicleLightMode::OFF)
                        {
                            indicator_state = GetIndicatorLightMode(light_mode, light_type);
                        }
                        break;
                    case Object::VehicleLightType::INDICATOR_LEFT:
                        if (indicator_state != osi3::MovingObject_VehicleClassification_LightState_IndicatorState_INDICATOR_STATE_WARNING)
                        {
                            auto mode = GetIndicatorLightMode(light_mode, light_type);
                            if (mode != osi3::MovingObject_VehicleClassification_LightState_IndicatorState_INDICATOR_STATE_OFF)
                            {
                                indicator_state = mode;
                            }
                        }
                        break;
                    case Object::VehicleLightType::INDICATOR_RIGHT:
                        if (indicator_state != osi3::MovingObject_VehicleClassification_LightState_IndicatorState_INDICATOR_STATE_WARNING)
                        {
                            auto mode = GetIndicatorLightMode(light_mode, light_type);
                            if (mode != osi3::MovingObject_VehicleClassification_LightState_IndicatorState_INDICATOR_STATE_OFF)
                            {
                                indicator_state = mode;
                            }
                        }
                        break;
                    case Object::VehicleLightType::REVERSING_LIGHTS:
                        light_state->set_reversing_light(GetGenericLightMode(light_mode));
                        break;
                    case Object::VehicleLightType::TAIL_LIGHTS:
                        // supported in OSI 3.8
                        break;
                    case Object::VehicleLightType::LICENSE_PLATE_ILLUMINATION:
                        light_state->set_license_plate_illumination_rear(GetGenericLightMode(light_mode));
                        break;
                    case Object::VehicleLightType::SPECIAL_PURPOSE_LIGHTS:
                    {
                        const auto &role = static_cast<Object::Role>(objectState.role_);
                        if (role == Object::Role::AMBULANCE || role == Object::Role::POLICE || role == Object::Role::FIRE)
                        {
                            light_state->set_emergency_vehicle_illumination(GetSpecialPurposeLightMode(light_mode, role));
                        }
                        else
                        {
                            light_state->set_service_vehicle_illumination(GetServiceVehicleLightMode(light_mode));
                        }
                        break;
                    }
                    default:
                        break;
                }
            }

            light_state->set_indicator_state(indicator_state);
        }

        // [New] Generate Future Trajectory
        // [GT_MOD #37 G4] env-gated (GT_OSI_FUTURE_TRAJECTORY=0 disables; default ON). This single
        // block is the only producer of osi3 future_trajectory points: the ghost trail_ sampling
        // below and both GenerateProjectedTrajectory call sites (whose add_future_trajectory lives
        // inside that helper) are all reached exclusively from here.
        if (this->scenario_engine_ && FutureTrajectoryEnabled())
        {
            int id = objectState.id_;
            const double current_sim_time = this->scenario_engine_->getSimulationTime();
            scenarioengine::Object* targetObj = this->scenario_engine_->entities_.GetObjectById(id);

            if (targetObj)
            {
                // CASE 1: Ghost Object (Report its own future trajectory)
                // targetObj->isGhost_ determines if it IS a ghost? Or logic check.
                // Entities.hpp says isGhost_ is a member.
                // Or check controllers specific type if needed.
                // The original code checked: ctrl_type == GHOST_RESERVED_TYPE
                
                // Let's rely on simple checks suitable for the context.
                // Check if it's the Ghost object itself.
                bool is_ghost = false;
                // Method 1: Check driver_id / ctrl_type from objectState (which is reliable for current frame)
                if (objectState.GetControllerTypeActiveOnDomain(ControlDomains::DOMAIN_LONG) == Controller::Type::GHOST_RESERVED_TYPE) {
                    is_ghost = true;
                }
                
                if (is_ghost)
                {
                    // Sample future points from trail_
                    double current_time = this->scenario_engine_->getSimulationTime(); 
                    
                    if (targetObj->trail_.GetNumberOfVertices() > 0)
                    {
                        int samples = 20;
                        double dt = 0.5; 

                         for(int i=1; i<=samples; ++i)
                        {
                            double t_future = current_time + i*dt;
                            roadmanager::TrajVertex v;
                            idx_t index = -1; // Removed scenarioengine:: qualifier if idx_t is global
                            
                            if (targetObj->trail_.FindPointAtTime(t_future, v, index) == 0) 
                            {
                                auto* point = obj_osi_internal.mobj->add_future_trajectory();
                                point->mutable_timestamp()->set_seconds((long long)t_future);
                                point->mutable_timestamp()->set_nanos((int)((t_future - (long long)t_future) * 1e9));
                                
                                point->mutable_position()->set_x(v.x);
                                point->mutable_position()->set_y(v.y);
                                point->mutable_position()->set_z(v.z);
                                
                                point->mutable_orientation()->set_yaw(v.h);
                                point->mutable_orientation()->set_roll(0); 
                                point->mutable_orientation()->set_pitch(0);
                            }
                        }
                    }
                    else
                    {
                        // [GT_MOD] Fallback: Generate projected trajectory if trail is empty
                        GenerateProjectedTrajectory(objectState, this->scenario_engine_);
                    }
                }
                // CASE 2: Ego Object (Report Spline to Ghost)

                else
                {
                    // [GT_MOD] Universal Fallback: Optimized
                    // Only generate for Ego/External/Interactive vehicles to save performance.
                    // ID 0 is typically Ego.
                    int ctrlType = objectState.GetControllerTypeActiveOnDomain(ControlDomains::DOMAIN_LONG);
                    bool isEgoOrExternal = (objectState.id_ == 0) ||
                                           (ctrlType == scenarioengine::Controller::CONTROLLER_TYPE_EXTERNAL) ||
                                           (ctrlType == scenarioengine::Controller::CONTROLLER_TYPE_UDP_DRIVER) ||
                                           (ctrlType == scenarioengine::Controller::CONTROLLER_TYPE_INTERACTIVE);

                    if (isEgoOrExternal)
                    {
                        // [GT_MOD] Prefer the path the object's OWN planner is tracking.
                        //
                        // When a VirtualDriver drives this object it publishes the very
                        // preview its driver model follows (the polyline the Live
                        // telemetry view draws), extended along the same route walk with
                        // the mid/long speed profile. Reporting a separately re-derived
                        // shadow simulation instead meant the OSI line and the actual
                        // driven path disagreed: measured on virtual_driver_basic at
                        // t=1 s, the vehicle was doing 1.1 m/s while the reported line
                        // was 131 m long (the shadow used the scenario's 15 m/s target),
                        // and at t=8 s the far end sat 21 m sideways of the driven lane.
                        //
                        // Scope note: EGO/host only. osi3 marks future_trajectory as
                        // GroundTruth-side ("should not be made available to the stack
                        // under test") and this project's capability model forbids
                        // routing OTHER vehicles' predictions here. Reaching this branch
                        // already requires isEgoOrExternal.
                        gt_esmini::PlannedPathRegistry& planned = gt_esmini::PlannedPathRegistry::Instance();
                        planned.SetConsumerActive(true);
                        const gt_esmini::PlannedPath* pp = planned.Get(objectState.id_, current_sim_time);

                        if (pp != nullptr && pp->points.size() >= 2)
                        {
                            for (const auto& pt : pp->points)
                            {
                                const double t_future = current_sim_time + pt.t;
                                auto* point = obj_osi_internal.mobj->add_future_trajectory();
                                point->mutable_timestamp()->set_seconds((long long)t_future);
                                point->mutable_timestamp()->set_nanos((int)((t_future - (long long)t_future) * 1e9));

                                // Same reference-point rule as the projected branch below:
                                // the publisher hands over an OBJECT-ORIGIN path, and
                                // base.position is the bounding-box centre, so shift by the
                                // centre offset rotated into the point's own heading. Doing it
                                // here (rather than in the publisher) keeps the OSI frame
                                // convention in ONE place for both path sources.
                                double bb_dx = 0.0, bb_dy = 0.0, bb_dz = 0.0;
                                RotateVec3d(pt.h,
                                            pt.p,
                                            pt.r,
                                            static_cast<double>(objectState.boundingbox_.center_.x_),
                                            static_cast<double>(objectState.boundingbox_.center_.y_),
                                            static_cast<double>(objectState.boundingbox_.center_.z_),
                                            bb_dx,
                                            bb_dy,
                                            bb_dz);

                                point->mutable_position()->set_x(pt.x + bb_dx);
                                point->mutable_position()->set_y(pt.y + bb_dy);
                                point->mutable_position()->set_z(pt.z + bb_dz);

                                point->mutable_orientation()->set_yaw(pt.h);
                                point->mutable_orientation()->set_pitch(pt.p);
                                point->mutable_orientation()->set_roll(pt.r);
                            }
                        }
                        else
                        {
                            // No on-board planner (scenario-driven ego, ManualDrive,
                            // external control, ...) -> shadow-simulate the path.
                            //
                            // The "wrong road snap" recovery that used to sit here and
                            // wrote targetObj->pos_ -- an OSI output stage mutating
                            // simulation state that then fed the next frame's controller
                            // input -- now lives INSIDE GenerateProjectedTrajectory and
                            // is applied to the ghost copy only.
                            GenerateProjectedTrajectory(objectState, this->scenario_engine_);
                        }
                    }
                }
            }
        }

        if (objectState.role_ == Object::Role::AMBULANCE)
        {
            obj_osi_internal.mobj->mutable_vehicle_classification()->set_role(osi3::MovingObject_VehicleClassification::ROLE_AMBULANCE);
        }
        else if (objectState.role_ == Object::Role::CIVIL)
        {
            obj_osi_internal.mobj->mutable_vehicle_classification()->set_role(osi3::MovingObject_VehicleClassification::ROLE_CIVIL);
        }
        else if (objectState.role_ == Object::Role::FIRE)
        {
            obj_osi_internal.mobj->mutable_vehicle_classification()->set_role(osi3::MovingObject_VehicleClassification::ROLE_FIRE);
        }
        else if (objectState.role_ == Object::Role::MILITARY)
        {
            obj_osi_internal.mobj->mutable_vehicle_classification()->set_role(osi3::MovingObject_VehicleClassification::ROLE_MILITARY);
        }
        else if (objectState.role_ == Object::Role::POLICE)
        {
            obj_osi_internal.mobj->mutable_vehicle_classification()->set_role(osi3::MovingObject_VehicleClassification::ROLE_POLICE);
        }
        else if (objectState.role_ == Object::Role::PUBLIC_TRANSPORT)
        {
            obj_osi_internal.mobj->mutable_vehicle_classification()->set_role(osi3::MovingObject_VehicleClassification::ROLE_PUBLIC_TRANSPORT);
        }
        else if (objectState.role_ == Object::Role::ROAD_ASSISTANCE)
        {
            obj_osi_internal.mobj->mutable_vehicle_classification()->set_role(osi3::MovingObject_VehicleClassification::ROLE_ROAD_ASSISTANCE);
        }
        else if (objectState.role_ == Object::Role::NONE)
        {
            obj_osi_internal.mobj->mutable_vehicle_classification()->set_role(osi3::MovingObject_VehicleClassification::ROLE_UNKNOWN);
        }
        else
        {
            LOG_ERROR("OSIReporter::UpdateOSIMovingObject -> Unsupported moving object vehicle role: {} ({}). Set classification UNKNOWN.",
                      objectState.role_,
                      Vehicle::Role2String(objectState.role_).c_str());
            obj_osi_internal.mobj->mutable_vehicle_classification()->set_role(osi3::MovingObject_VehicleClassification::ROLE_UNKNOWN);
        }
    }
    else if (objectState.type_ == Object::Type::PEDESTRIAN)
    {
        entity_type = "Pedestrian";
        if (objectState.category_ == static_cast<int>(Pedestrian::Category::PEDESTRIAN))
        {
            obj_osi_internal.mobj->set_type(osi3::MovingObject::Type::MovingObject_Type_TYPE_PEDESTRIAN);
        }
        else if (objectState.category_ == static_cast<int>(Pedestrian::Category::ANIMAL))
        {
            obj_osi_internal.mobj->set_type(osi3::MovingObject::Type::MovingObject_Type_TYPE_ANIMAL);
        }
        else if (objectState.category_ == static_cast<int>(Pedestrian::Category::WHEELCHAIR))
        {
            obj_osi_internal.mobj->set_type(osi3::MovingObject::Type::MovingObject_Type_TYPE_OTHER);
        }
        else
        {
            LOG_ERROR("OSIReporter::UpdateOSIMovingObject -> Unsupported moving object pedestrian category: {} ({}). Set type UNKNOWN.",
                      objectState.category_,
                      Pedestrian::Category2String(objectState.category_));
            obj_osi_internal.mobj->set_type(osi3::MovingObject::Type::MovingObject_Type_TYPE_UNKNOWN);
        }
    }
    else
    {
        LOG_ERROR("OSIReporter::UpdateOSIMovingObject -> Unsupported moving object type: {} ({}). Set UNKNOWN.",
                  objectState.type_,
                  Object::Type2String(objectState.type_));
        obj_osi_internal.mobj->set_type(osi3::MovingObject::Type::MovingObject_Type_TYPE_UNKNOWN);
    }

    // Set OSI Moving Object Control Type
    obj_osi_internal.mobj->mutable_vehicle_attributes()->mutable_driver_id()->set_value(static_cast<uint64_t>(objectState.GetControllerTypeActiveOnDomain(ControlDomains::DOMAIN_LONG)));

    // Set OSI Moving Object Boundingbox
    obj_osi_internal.mobj->mutable_vehicle_attributes()->mutable_bbcenter_to_rear()->set_x(
        static_cast<double>(-objectState.boundingbox_.center_.x_));
    obj_osi_internal.mobj->mutable_vehicle_attributes()->mutable_bbcenter_to_rear()->set_y(
        static_cast<double>(-objectState.boundingbox_.center_.y_));
    obj_osi_internal.mobj->mutable_vehicle_attributes()->mutable_bbcenter_to_rear()->set_z(
        objectState.rear_axle_.positionZ - static_cast<double>(objectState.boundingbox_.center_.z_));
    obj_osi_internal.mobj->mutable_vehicle_attributes()->mutable_bbcenter_to_front()->set_x(
        objectState.front_axle_.positionX - static_cast<double>(objectState.boundingbox_.center_.x_));
    obj_osi_internal.mobj->mutable_vehicle_attributes()->mutable_bbcenter_to_front()->set_y(
        static_cast<double>(-objectState.boundingbox_.center_.y_));
    obj_osi_internal.mobj->mutable_vehicle_attributes()->mutable_bbcenter_to_front()->set_z(
        objectState.front_axle_.positionZ - static_cast<double>(objectState.boundingbox_.center_.z_));
    obj_osi_internal.mobj->mutable_base()->mutable_dimension()->set_height(objectState.boundingbox_.dimensions_.height_);
    obj_osi_internal.mobj->mutable_base()->mutable_dimension()->set_width(objectState.boundingbox_.dimensions_.width_);
    obj_osi_internal.mobj->mutable_base()->mutable_dimension()->set_length(objectState.boundingbox_.dimensions_.length_);

    // OSI XYZ is center of BB, have been calculated in SetOsiXYZ
    obj_osi_internal.mobj->mutable_base()->mutable_position()->set_x(objectState.pos_.GetOsiX());
    obj_osi_internal.mobj->mutable_base()->mutable_position()->set_y(objectState.pos_.GetOsiY());
    obj_osi_internal.mobj->mutable_base()->mutable_position()->set_z(objectState.pos_.GetOsiZ());

    // Set OSI Moving Object Orientation
    obj_osi_internal.mobj->mutable_base()->mutable_orientation()->set_roll(GetAngleInIntervalMinusPIPlusPI(objectState.pos_.GetR()));
    obj_osi_internal.mobj->mutable_base()->mutable_orientation()->set_pitch(GetAngleInIntervalMinusPIPlusPI(objectState.pos_.GetP()));
    obj_osi_internal.mobj->mutable_base()->mutable_orientation()->set_yaw(GetAngleInIntervalMinusPIPlusPI(objectState.pos_.GetH()));
    obj_osi_internal.mobj->mutable_base()->mutable_orientation_rate()->set_yaw(objectState.pos_.GetHRate());
    obj_osi_internal.mobj->mutable_base()->mutable_orientation_rate()->set_pitch(objectState.pos_.GetPRate());
    obj_osi_internal.mobj->mutable_base()->mutable_orientation_rate()->set_roll(objectState.pos_.GetRRate());
    obj_osi_internal.mobj->mutable_base()->mutable_orientation_acceleration()->set_yaw(objectState.pos_.GetHAcc());
    obj_osi_internal.mobj->mutable_base()->mutable_orientation_acceleration()->set_pitch(objectState.pos_.GetPAcc());
    obj_osi_internal.mobj->mutable_base()->mutable_orientation_acceleration()->set_roll(objectState.pos_.GetRAcc());

    // Set OSI Moving Object Velocity
    obj_osi_internal.mobj->mutable_base()->mutable_velocity()->set_x(objectState.pos_.GetVelX());
    obj_osi_internal.mobj->mutable_base()->mutable_velocity()->set_y(objectState.pos_.GetVelY());
    obj_osi_internal.mobj->mutable_base()->mutable_velocity()->set_z(objectState.pos_.GetVelZ());

    // Set OSI Moving Object Acceleration
    obj_osi_internal.mobj->mutable_base()->mutable_acceleration()->set_x(objectState.pos_.GetAccX());
    obj_osi_internal.mobj->mutable_base()->mutable_acceleration()->set_y(objectState.pos_.GetAccY());
    obj_osi_internal.mobj->mutable_base()->mutable_acceleration()->set_z(objectState.pos_.GetAccZ());

    // Set ego lane
    // [GT_MOD] Use the cached driving-lane global id (see ResolveMovingObjectAssignedLaneGlobalId)
    // so a laterally-drifting driving vehicle is not reported as assigned to a border/sidewalk lane.
    // Dual emit during the deprecation transition: MovingObject.assigned_lane_id (field 4) is
    // deprecated in OSI 3.7.0 in favour of MovingObjectClassification.assigned_lane_id, but the
    // deprecated field is still what replayer's osi_receiver and the upstream UDP samples read —
    // those are core-side consumers GT cannot patch (R1). GT-side consumers prefer the
    // classification field and fall back to the deprecated one (gt_sim_test._gt_to_scene).
    const id_t assigned_lane_gid = ResolveMovingObjectAssignedLaneGlobalId(objectState.pos_);
    obj_osi_internal.mobj->add_assigned_lane_id()->set_value(assigned_lane_gid);
    obj_osi_internal.mobj->mutable_moving_object_classification()->add_assigned_lane_id()->set_value(assigned_lane_gid);

    // simplified wheel info, set nr wheels based on object type
    // can be improved by considering axels and actual wheel configuration

    if (objectState.type_ == Object::Type::VEHICLE)
    {
        const auto& wheelData = static_cast<const scenarioengine::Vehicle&>(objectState).GetWheelData();
        // Set some data for each wheel
        for (unsigned int i = 0; i < wheelData.size(); i++)
        {
            if (wheelData[i].axle > -1)
            {
                // create wheel data message
                int ii = static_cast<int>(i);
                obj_osi_internal.mobj->mutable_vehicle_attributes()->add_wheel_data();
                obj_osi_internal.mobj->mutable_vehicle_attributes()->mutable_wheel_data(ii)->mutable_position()->set_x(
                    wheelData[i].x - static_cast<double>(objectState.boundingbox_.center_.x_));
                obj_osi_internal.mobj->mutable_vehicle_attributes()->mutable_wheel_data(ii)->mutable_position()->set_y(
                    wheelData[i].y - static_cast<double>(objectState.boundingbox_.center_.y_));
                obj_osi_internal.mobj->mutable_vehicle_attributes()->mutable_wheel_data(ii)->mutable_position()->set_z(
                    wheelData[i].z - static_cast<double>(objectState.boundingbox_.center_.z_));

                obj_osi_internal.mobj->mutable_vehicle_attributes()->mutable_wheel_data(ii)->mutable_orientation()->set_yaw(
                    wheelData[i].h);
                obj_osi_internal.mobj->mutable_vehicle_attributes()->mutable_wheel_data(ii)->mutable_orientation()->set_pitch(
                    wheelData[i].p);
                obj_osi_internal.mobj->mutable_vehicle_attributes()->mutable_wheel_data(ii)->set_friction_coefficient(
                    wheelData[i].friction_coefficient);
                obj_osi_internal.mobj->mutable_vehicle_attributes()->mutable_wheel_data(ii)->set_axle(
                    static_cast<unsigned int>(wheelData[i].axle));
                obj_osi_internal.mobj->mutable_vehicle_attributes()->mutable_wheel_data(ii)->set_index(
                    static_cast<unsigned int>(wheelData[i].index));  // Index along axis
                obj_osi_internal.mobj->mutable_vehicle_attributes()->mutable_wheel_data(ii)->set_wheel_radius(
                    wheelData[i].wheel_radius);
                obj_osi_internal.mobj->mutable_vehicle_attributes()->mutable_wheel_data(ii)->set_rotation_rate(
                    wheelData[i].rotation_rate);
                obj_osi_internal.mobj->mutable_vehicle_attributes()->set_number_wheels(
                    static_cast<unsigned int>(wheelData.size()));
            }
        }
    }

    // [fork-sync #37 G3] Set 3D model file as OSI model reference. Upstream 752dcaa0..77028d83 switched
    // this from the bare filename (GetModel3DFilename) to the full resolved path (GetModel3DFullPath).
    obj_osi_internal.mobj->set_model_reference(objectState.GetModel3DFullPath());

    // SOURCE REFERENCE
    auto source_reference = obj_osi_internal.mobj->add_source_reference();
    source_reference->set_type(SOURCE_REF_TYPE_OSC);

    source_reference->add_identifier(fmt::format("entity_id:{}", objectState.id_));
    source_reference->add_identifier(fmt::format("entity_type:{}", entity_type));
    source_reference->add_identifier(fmt::format("entity_name:{}", objectState.name_));

    // [fork-sync #37 G3] Color (ported from upstream): report the authored <Color> (if any) as an OSI
    // color_description RGB triplet.
    if (!objectState.GetColorStr().empty())
    {
        auto rgb = objectState.GetColorRgb();
        obj_osi_internal.mobj->mutable_color_description()->mutable_rgb()->set_red(rgb.r);
        obj_osi_internal.mobj->mutable_color_description()->mutable_rgb()->set_green(rgb.g);
        obj_osi_internal.mobj->mutable_color_description()->mutable_rgb()->set_blue(rgb.b);
    }

    // Set source reference if available
    if (!objectState.GetSourceReference().empty())
    {
        for (const auto &ref : objectState.GetSourceReference())
        {
            source_reference->add_identifier(ref);
        }
    }

    // [fork-sync #37 G3] Set outline if available (ported from upstream): obj.outline_2d_ -> base_polygon.
    for (const auto &p : objectState.outline_2d_)
    {
        osi3::Vector2d *vec = obj_osi_internal.mobj->mutable_base()->add_base_polygon();
        vec->set_x(p.x);
        vec->set_y(p.y);
    }

    return 0;
}

// -----------------------------------------------------------------------------
// GT_esmini (R5-U4): vehicle light-mode -> OSI light-state mappers.
// Ported verbatim from upstream OSIReporter.cpp (commit 8d2ebfb7). Declared in the
// upstream OSIReporter.hpp; defined here in the fork's Moving split alongside the
// UpdateOSIMovingObject light block that consumes them.
// -----------------------------------------------------------------------------

osi3::MovingObject_VehicleClassification_LightState_GenericLightState OSIReporter::GetServiceVehicleLightMode(
    const Object::VehicleLightMode &mode) const
{
    switch (mode)
    {
        case Object::VehicleLightMode::OFF:
            return osi3::MovingObject_VehicleClassification_LightState::GENERIC_LIGHT_STATE_OFF;
        case Object::VehicleLightMode::FLASHING:
            return osi3::MovingObject_VehicleClassification_LightState::GENERIC_LIGHT_STATE_FLASHING_AMBER;
        case Object::VehicleLightMode::ON:
            return osi3::MovingObject_VehicleClassification_LightState::GENERIC_LIGHT_STATE_ON;
        default:
            return osi3::MovingObject_VehicleClassification_LightState::GENERIC_LIGHT_STATE_OTHER;
    }
}

osi3::MovingObject_VehicleClassification_LightState_GenericLightState OSIReporter::GetSpecialPurposeLightMode(const Object::VehicleLightMode &mode,
                                                                                                              const Object::Role &role) const
{
    switch (mode)
    {
        case Object::VehicleLightMode::OFF:
            return osi3::MovingObject_VehicleClassification_LightState::GENERIC_LIGHT_STATE_OFF;
        case Object::VehicleLightMode::FLASHING:
            if (role == Object::Role::AMBULANCE || role == Object::Role::POLICE)
            {
                return osi3::MovingObject_VehicleClassification_LightState::GENERIC_LIGHT_STATE_FLASHING_BLUE;
            }
            else if (role == Object::Role::FIRE)
            {
                return osi3::MovingObject_VehicleClassification_LightState::GENERIC_LIGHT_STATE_FLASHING_BLUE_AND_RED;
            }
            else
            {
                return osi3::MovingObject_VehicleClassification_LightState::GENERIC_LIGHT_STATE_ON;
            }
            break;
        case Object::VehicleLightMode::ON:
            return osi3::MovingObject_VehicleClassification_LightState::GENERIC_LIGHT_STATE_ON;
        default:
            return osi3::MovingObject_VehicleClassification_LightState::GENERIC_LIGHT_STATE_OTHER;
    }
}

osi3::MovingObject_VehicleClassification_LightState_BrakeLightState OSIReporter::GetBrakeLightMode(const Object::VehicleLightMode &mode,
                                                                                                   const double                   &luminousity) const
{
    switch (mode)
    {
        case Object::VehicleLightMode::OFF:
            return osi3::MovingObject_VehicleClassification_LightState::BRAKE_LIGHT_STATE_OFF;
        case Object::VehicleLightMode::FLASHING:
        case Object::VehicleLightMode::ON:
            return (luminousity > 6000.0 + SMALL_NUMBER) ? osi3::MovingObject_VehicleClassification_LightState::BRAKE_LIGHT_STATE_STRONG
                                                         : osi3::MovingObject_VehicleClassification_LightState::BRAKE_LIGHT_STATE_NORMAL;
        default:
            return osi3::MovingObject_VehicleClassification_LightState::BRAKE_LIGHT_STATE_OTHER;
    }
}

osi3::MovingObject_VehicleClassification_LightState_IndicatorState OSIReporter::GetIndicatorLightMode(const Object::VehicleLightMode &mode,
                                                                                                      const Object::VehicleLightType &type) const
{
    switch (mode)
    {
        case Object::VehicleLightMode::OFF:
            return osi3::MovingObject_VehicleClassification_LightState::INDICATOR_STATE_OFF;
        case Object::VehicleLightMode::FLASHING:
        case Object::VehicleLightMode::ON:
            if (type == Object::VehicleLightType::INDICATOR_LEFT)
            {
                return osi3::MovingObject_VehicleClassification_LightState_IndicatorState_INDICATOR_STATE_LEFT;
            }
            else if (type == Object::VehicleLightType::INDICATOR_RIGHT)
            {
                return osi3::MovingObject_VehicleClassification_LightState_IndicatorState_INDICATOR_STATE_RIGHT;
            }
            else if (type == Object::VehicleLightType::WARNING_LIGHTS)
            {
                return osi3::MovingObject_VehicleClassification_LightState_IndicatorState_INDICATOR_STATE_WARNING;
            }
            else
            {
                LOG_WARN("OSIReporter: Indicator type neither left/right/warning, setting other");
                return osi3::MovingObject_VehicleClassification_LightState::INDICATOR_STATE_OTHER;
            }
        default:
            return osi3::MovingObject_VehicleClassification_LightState::INDICATOR_STATE_OTHER;
    }
}

osi3::MovingObject_VehicleClassification_LightState_GenericLightState OSIReporter::GetGenericLightMode(const Object::VehicleLightMode &mode) const
{
    switch (mode)
    {
        case Object::VehicleLightMode::OFF:
            return osi3::MovingObject_VehicleClassification_LightState::GENERIC_LIGHT_STATE_OFF;
        case Object::VehicleLightMode::FLASHING:
        case Object::VehicleLightMode::ON:
            return osi3::MovingObject_VehicleClassification_LightState::GENERIC_LIGHT_STATE_ON;
        default:
            return osi3::MovingObject_VehicleClassification_LightState::GENERIC_LIGHT_STATE_OTHER;
    }
}

// ===================================================================================
// [GT_MOD] PlannedPathRegistry storage.
//
// This lives in an OSI-reporter TU on purpose. GT_OSIReporter_Moving.cpp is compiled
// INTO ScenarioEngine (the module CMakeLists swaps it in for upstream's
// OSIReporter.cpp), while the publisher (ControllerVirtualDriver) is in GT_esminiLib,
// which LINKS ScenarioEngine. Putting the storage on the lower layer keeps the
// dependency one-way and needs no core build-file change (R1). See
// gt_esmini/osi/GT_PlannedPathRegistry.hpp.
// ===================================================================================
namespace gt_esmini
{

PlannedPathRegistry& PlannedPathRegistry::Instance()
{
    static PlannedPathRegistry inst;
    return inst;
}

void PlannedPathRegistry::Publish(const PlannedPath& path)
{
    for (auto& p : paths_)
    {
        if (p.object_id == path.object_id)
        {
            p = path;
            return;
        }
    }
    paths_.push_back(path);
}

const PlannedPath* PlannedPathRegistry::Get(int object_id, double sim_time, double max_age_s) const
{
    for (const auto& p : paths_)
    {
        if (p.object_id != object_id)
        {
            continue;
        }
        const double age = sim_time - p.stamp;
        // age < 0 => stamped in the future => a scenario reload restarted sim time
        // while this entry survived from the previous run. Reject rather than draw a
        // line from a run that is over.
        if (age < -SMALL_NUMBER || age > max_age_s)
        {
            return nullptr;
        }
        return &p;
    }
    return nullptr;
}

void PlannedPathRegistry::SetConsumerActive(bool active)
{
    consumer_active_ = active;
}

bool PlannedPathRegistry::IsConsumerActive() const
{
    return consumer_active_;
}

void PlannedPathRegistry::Clear()
{
    paths_.clear();
}

}  // namespace gt_esmini
