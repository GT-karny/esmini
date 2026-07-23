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

static int GetTargetLaneIdFromRoute(const roadmanager::Route* route, id_t roadId)
{
    if (!route || !route->IsValid())
    {
        return 0; // Default to lane 0 if no route
    }

    // Search for the road in route waypoints
    for (size_t i = 0; i < route->minimal_waypoints_.size(); i++)
    {
        if (route->minimal_waypoints_[i].GetTrackId() == roadId)
        {
            return route->minimal_waypoints_[i].GetLaneId();
        }
    }

    return 0; // Not found in route, default to lane 0
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

// [GT_MOD] Helper to generate projected trajectory based on road geometry and active actions (Shadow Simulation)
static void GenerateProjectedTrajectory(const scenarioengine::Object& objectStateRef, scenarioengine::ScenarioEngine* scenario_engine)
{
    if (!scenario_engine) return;
    const scenarioengine::Object* objectState = &objectStateRef;
    int id = objectState->id_;
    
    // [GT_MOD] State Memory for Speed Actions (Sustain WaitOnRed)
    static std::map<int, double> lastTargetSpeedMap;

    auto* simObj = scenario_engine->entities_.GetObjectById(id);
    if (!simObj) return;

    // Shadow simulation: Clone current position
    // This maintains the current Route info if assigned
    // Shadow simulation: Clone current position
    // This maintains the current Route info if assigned
    roadmanager::Position ghostPos = simObj->pos_;

    // [GT_MOD] CRITICAL FIX: Reset lateral offset to 0.0
    // Real vehicle might be offset due to PID deviation (e.g. cutting corners).
    // If we keep this offset, ghostPos might snap to wrong lane (e.g. sidewalk)
    // or generate a trajectory that maintains this offset.
    // We force the ghost vehicle to start at the center of its current lane.
    if (std::abs(ghostPos.GetOffset()) > 0.001)
    {
        ghostPos.SetLanePos(ghostPos.GetTrackId(), ghostPos.GetLaneId(), ghostPos.GetS(), 0.0);
        // Also fix heading to align with road
        ghostPos.SetHeadingRelative(0.0);
    }

    // [GT_MOD] Inject Route: Ensure ghost position has the route for correct branching at junctions
    if (simObj->pos_.GetRoute())
    {
        // [GT_MOD] Clone Route to prevent shared state corruption and double-free
    // ghostPos modifies the route (advances waypoints) during prediction. 
    // If we share the pointer, simObj's route state gets corrupted (e.g. skips turns).
    if (simObj->pos_.GetRoute())
    {
        roadmanager::Route* clonedRoute = new roadmanager::Route();
        clonedRoute->CopyFrom(*simObj->pos_.GetRoute());
        ghostPos.SetRoute(clonedRoute); 
        // ghostPos now owns clonedRoute and will delete it in its destructor.
    }
    }

    if (ghostPos.GetRoute() && ghostPos.GetRoute()->IsValid())
    {
        // [GT_MOD] CRITICAL FIX: Correct ghostPos lane ID to match route
        // Problem: PID control causes vehicle to deviate from lane center, causing esmini
        // to snap to wrong lane (e.g. lane 1 instead of lane -1). This causes MoveAlongS
        // to select wrong road at junctions (e.g. road 16 instead of road 13).
        // Solution: Find current waypoint in route and force ghostPos to use that lane ID.
        int currentRoadId = ghostPos.GetTrackId();
        for (const auto& wp : ghostPos.GetRoute()->minimal_waypoints_)
        {
            if (wp.GetTrackId() == currentRoadId)
            {
                int routeLaneId = wp.GetLaneId();
                int currentLaneId = ghostPos.GetLaneId();
                if (routeLaneId != currentLaneId)
                {
                    ghostPos.SetLanePos(currentRoadId, routeLaneId, ghostPos.GetS(), 0.0);
                }
                break;
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

    // [GT_MOD] Fallback Speed Flag
    double fallbackSpeed = 0.0;
    bool useFallback = false;

    if (activeSpeedAction)
    {
        // Case 1: Active Speed Action -> Update Memory
        if (activeSpeedAction->target_)
        {
            lastTargetSpeedMap[id] = activeSpeedAction->target_->GetValue();
        }
    }
    else
    {
        // Fallback: If no active SpeedAction found, check:
        // 1. Memory (Last known target)
        // 2. Init actions (History)
        // 3. Default to 0.0 (Stop) - Physical speed is NOT used as fallback.
        
        if (lastTargetSpeedMap.count(id))
        {
             // Case 2: Use Memory
             fallbackSpeed = lastTargetSpeedMap[id];
             useFallback = true;
        }
        else
        {
             // Case 3: Check Init Actions
             for (auto* action : simObj->initActions_)
             {
                 std::string typeStr = action->Type2Str();
                 if (typeStr == "SpeedAction")
                 {
                     activeSpeedAction = static_cast<scenarioengine::LongSpeedAction*>(action);
                 }
             }

             if (activeSpeedAction)
             {
                 // Found Init Action -> Update Memory and Use it
                 if (activeSpeedAction->target_)
                 {
                     lastTargetSpeedMap[id] = activeSpeedAction->target_->GetValue();
                 }
             }
             else
             {
                 // Case 4: No definition -> Stop
                 fallbackSpeed = 0.0;
                 useFallback = true;
             }
        }
    }

    // Shadow copy of speed action dynamics state if available
    scenarioengine::OSCPrivateAction::TransitionDynamics speedDynamics;
    bool usingSpeedAction = false;
    double debugTargetSpeed = -1.0; 

    if (activeSpeedAction)
    {
        speedDynamics = activeSpeedAction->transition_;
        usingSpeedAction = true;

        // [GT_MOD] Fix static trajectory:
        // Identify intended target speed and reset dynamics to predict path from CURRENT speed to TARGET.
        if (activeSpeedAction->target_)
        {
            double targetSpeed = activeSpeedAction->target_->GetValue();
            double currentSpeed = simObj->GetSpeed();
            debugTargetSpeed = targetSpeed;

            // Re-initialize dynamics relative to current physical state
            // [GT_MOD] Reset() is crucial to clear 'param_val_' (elapsed time).
            // This ensures we predict a fresh transition from Current to Target over the original Duration/Shape,
            // avoiding "Instant Jump" if the action was previously completed.
            speedDynamics.Reset();
            speedDynamics.SetStartVal(currentSpeed);
            speedDynamics.SetTargetVal(targetSpeed);
            // Ensure internal rate/parameters are updated
            speedDynamics.UpdateRate();
        }
    }

    double current_time = scenario_engine->getSimulationTime();
    int samples = 20;
    double dt = 0.5;

    // [GT_DEBUG] Log once per second or frame (limiting output slightly effectively by loop)
    // Actually, just log every time for now since we are stuck.
    // printf("GT_DEBUG: Obj %d, ActiveSpeedAction: %d\n", id, usingSpeedAction);

    for (int i = 1; i <= samples; ++i)
    {
        double t_future = current_time + i * dt;
        double dt_step = dt;
        
        // Calculate speed for this step
        double speed = 0.0; 

        if (usingSpeedAction)
        {
            // Advance the dynamics state to predict future speed
            if (activeSpeedAction->transition_.dimension_ == scenarioengine::OSCPrivateAction::DynamicsDimension::TIME)
            {
                speedDynamics.Step(dt_step); 
            }
            else if (activeSpeedAction->transition_.dimension_ == scenarioengine::OSCPrivateAction::DynamicsDimension::DISTANCE)
            {
                // Note: using 'speed' here is circular if speed is not yet set. 
                // However, for trajectory prediction, we validly use the PREVIOUS step's speed or current state evaluation.
                // But Evaluate() returns the speed for the CURRENT state.
                // We should use the speed from *previous* evaluation for distance step.
                // Assuming constant acc over step effectively.
                double currentEvalSpeed = speedDynamics.Evaluate();
                speedDynamics.Step(currentEvalSpeed * dt_step); 
            }
            // Update speed from dynamics
            speed = speedDynamics.Evaluate();
        }
        else if (useFallback)
        {
            speed = fallbackSpeed;
        }
        else
        {
            // Should not be reached given logic above, but safely 0.0
            speed = 0.0;
        }

        double ds = speed * dt_step;
        
        // Lateral Logic (Lane Change)
        double dLaneOffset = 0.0;
        
        if (activeLcAction)
        {
            scenarioengine::OSCPrivateAction::TransitionDynamics futureDynamics = activeLcAction->transition_;
            if (futureDynamics.dimension_ == scenarioengine::OSCPrivateAction::DynamicsDimension::TIME)
            {
                futureDynamics.Step(i * dt);
            }
            else if (futureDynamics.dimension_ == scenarioengine::OSCPrivateAction::DynamicsDimension::DISTANCE)
            {
                futureDynamics.Step(i * ds);
            }
            double desiredOffset = futureDynamics.Evaluate();
            double currentGhostOffset = ghostPos.GetOffset(); 
            dLaneOffset = desiredOffset - currentGhostOffset; 
        }
        else
        {
            // [GT_MOD] Default behavior: Steer back to lane center and align heading
            // If not changing lanes, we want trajectory to be centered and parallel to lane.
            dLaneOffset = -ghostPos.GetOffset();
            ghostPos.SetHeadingRelative(0.0);
        }


        // Move the ghost position forward
        int prevRoadId = ghostPos.GetTrackId();
        auto ret = ghostPos.MoveAlongS(ds, dLaneOffset, -1.0, true, roadmanager::Position::MoveDirectionMode::HEADING_DIRECTION, true);
        
        // [GT_MOD] Fallback
        if (static_cast<int>(ret) < 0) 
        {
             ret = ghostPos.MoveAlongS(ds, dLaneOffset, -1.0, true, roadmanager::Position::MoveDirectionMode::HEADING_DIRECTION, false);
        }
        
        // [GT_MOD] Detect road transition and correct lane ID based on route
        int currentRoadId = ghostPos.GetTrackId();
        if (currentRoadId != prevRoadId && ghostPos.GetRoute())
        {
            // Road transition detected (Junction passed)
            int targetLaneId = GetTargetLaneIdFromRoute(ghostPos.GetRoute(), currentRoadId);
            int currentLaneId = ghostPos.GetLaneId();
            
            if (targetLaneId != currentLaneId)
            {
                // Correct to target lane from route
                double currentS = ghostPos.GetS();
                ghostPos.SetLanePos(currentRoadId, targetLaneId, currentS, 0.0);
            }
        }
        
        // Add point to OSI message
        auto* point = obj_osi_internal.mobj->add_future_trajectory();
        point->mutable_timestamp()->set_seconds((long long)t_future);
        point->mutable_timestamp()->set_nanos((int)((t_future - (long long)t_future) * 1e9));
        
        point->mutable_position()->set_x(ghostPos.GetX());
        point->mutable_position()->set_y(ghostPos.GetY());
        point->mutable_position()->set_z(ghostPos.GetZ());
        
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
                        // [GT_MOD] LOGIC FIX: Prevent "Wrong Road Snap" (e.g. Road 7 vs Road 13 in Junction 4)
                        // If vehicle snaps to a road NOT in the route, try to find a better road THAT IS in the route.
                        if (targetObj && targetObj->pos_.GetRoute() && targetObj->pos_.GetRoute()->IsValid())
                        {
                            id_t currentRoadId = targetObj->pos_.GetTrackId();
                            
                            // Check if current road is in the route
                            bool roadOnRoute = false;
                            const auto& waypoints = targetObj->pos_.GetRoute()->minimal_waypoints_;
                            
                            for (const auto& wp : waypoints)
                            {
                                if (wp.GetTrackId() == currentRoadId)
                                {
                                    roadOnRoute = true;
                                    break;
                                }
                            }

                            // If snapped to a wrong road (e.g. Road 7), attempt to recover
                            if (!roadOnRoute && !waypoints.empty())
                            {
                                double bestT = 1e9;
                                id_t bestRoadId = 0;  // Use 0 instead of -1 for unsigned type
                                
                                double curX = targetObj->pos_.GetX();
                                double curY = targetObj->pos_.GetY();
                                double curZ = targetObj->pos_.GetZ();

                                // Search for closest road among route waypoints
                                for (const auto& wp : waypoints)
                                {
                                    id_t candidateId = wp.GetTrackId();
                                    
                                    // Use a temporary position to probe the candidate road
                                    roadmanager::Position tempPos;
                                    // XYZ2TrackPos: mode=UNDEFINED (defaults), connectedOnly=false, roadId=candidateId, CheckOverlapping=false, AlongRoute=false(since check specific)
                                    tempPos.XYZ2TrackPos(curX, curY, curZ, roadmanager::Position::PosMode::UNDEFINED, false, candidateId);
                                    
                                    // Check lateral offset magnitude
                                    double t = std::abs(tempPos.GetT());
                                    
                                    // Check if valid match (XYZ2TrackPos returns valid S for the road?)
                                    // It clamps S if out of bounds usually.
                                    // We use T as metric.
                                    if (t < bestT)
                                    {
                                        bestT = t;
                                        bestRoadId = candidateId;
                                    }
                                }

                                // Threshold: 5m tolerance (same lane or adjacent)
                                // If we found a much better road on the route, force snap to it.
                                if (bestRoadId != -1 && bestRoadId != currentRoadId && bestT < 5.0)
                                {
                                    // Apply correction to the ACTUAL object state
                                    // This fixes "Entity Ego" behavior for the NEXT frame and current reporting
                                    targetObj->pos_.XYZ2TrackPos(curX, curY, curZ, roadmanager::Position::PosMode::UNDEFINED, false, bestRoadId);
                                }
                            }
                        }

                        GenerateProjectedTrajectory(objectState, this->scenario_engine_);
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
    obj_osi_internal.mobj->add_assigned_lane_id()->set_value(ResolveMovingObjectAssignedLaneGlobalId(objectState.pos_));

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

