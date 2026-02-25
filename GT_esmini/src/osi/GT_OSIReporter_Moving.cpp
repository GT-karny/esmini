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
#include <map>

constexpr const char *SOURCE_REF_TYPE_OSC = "net.asam.openscenario";

static int GetTargetLaneIdFromRoute(const roadmanager::Route* route, int roadId)
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

// [GT_MOD] Helper to generate projected trajectory based on road geometry and active actions (Shadow Simulation)
static void GenerateProjectedTrajectory(scenarioengine::ObjectState* objectState, scenarioengine::ScenarioEngine* scenario_engine)
{
    if (!scenario_engine) return;
    int id = objectState->state_.info.id;
    
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

int OSIReporter::UpdateOSIMovingObject(ObjectState *objectState)
{
    // Create OSI Moving object
    obj_osi_internal.mobj = obj_osi_internal.dynamic_gt->add_moving_object();

    // Set OSI Moving Object Mutable ID
    obj_osi_internal.mobj->mutable_id()->set_value(objectState->state_.info.g_id);

    // GT_esmini: Inject light state
    scenarioengine::Object* obj = scenario_engine_->entities_.GetObjectById(objectState->state_.info.id);
    if (obj && obj->GetType() == scenarioengine::Object::Type::VEHICLE)
    {
        // Hook removed


    }

    // Set OSI Moving Object Type and Classification
    std::string entity_type = "Vehicle";
    if (objectState->state_.info.obj_type == static_cast<int>(Object::Type::VEHICLE))
    {
        obj_osi_internal.mobj->set_type(osi3::MovingObject::Type::MovingObject_Type_TYPE_VEHICLE);

        if (objectState->state_.info.obj_category == static_cast<int>(Vehicle::Category::CAR))
        {
            obj_osi_internal.mobj->mutable_vehicle_classification()->set_type(osi3::MovingObject_VehicleClassification::TYPE_MEDIUM_CAR);
        }
        else if (objectState->state_.info.obj_category == static_cast<int>(Vehicle::Category::BICYCLE))
        {
            obj_osi_internal.mobj->mutable_vehicle_classification()->set_type(osi3::MovingObject_VehicleClassification::TYPE_BICYCLE);
        }
        else if (objectState->state_.info.obj_category == static_cast<int>(Vehicle::Category::BUS))
        {
            obj_osi_internal.mobj->mutable_vehicle_classification()->set_type(osi3::MovingObject_VehicleClassification::TYPE_BUS);
        }
        else if (objectState->state_.info.obj_category == static_cast<int>(Vehicle::Category::MOTORBIKE))
        {
            obj_osi_internal.mobj->mutable_vehicle_classification()->set_type(osi3::MovingObject_VehicleClassification::TYPE_MOTORBIKE);
        }
        else if (objectState->state_.info.obj_category == static_cast<int>(Vehicle::Category::SEMITRAILER))
        {
            obj_osi_internal.mobj->mutable_vehicle_classification()->set_type(osi3::MovingObject_VehicleClassification::TYPE_SEMITRAILER);
        }
        else if (objectState->state_.info.obj_category == static_cast<int>(Vehicle::Category::TRAIN))
        {
            obj_osi_internal.mobj->mutable_vehicle_classification()->set_type(osi3::MovingObject_VehicleClassification::TYPE_TRAIN);
        }
        else if (objectState->state_.info.obj_category == static_cast<int>(Vehicle::Category::TRAM))
        {
            obj_osi_internal.mobj->mutable_vehicle_classification()->set_type(osi3::MovingObject_VehicleClassification::TYPE_TRAM);
        }
        else if (objectState->state_.info.obj_category == static_cast<int>(Vehicle::Category::TRUCK))
        {
            obj_osi_internal.mobj->mutable_vehicle_classification()->set_type(osi3::MovingObject_VehicleClassification::TYPE_HEAVY_TRUCK);
        }
        else if (objectState->state_.info.obj_category == static_cast<int>(Vehicle::Category::TRAILER))
        {
            obj_osi_internal.mobj->mutable_vehicle_classification()->set_type(osi3::MovingObject_VehicleClassification::TYPE_TRAILER);
        }
        else if (objectState->state_.info.obj_category == static_cast<int>(Vehicle::Category::VAN))
        {
            obj_osi_internal.mobj->mutable_vehicle_classification()->set_type(osi3::MovingObject_VehicleClassification::TYPE_DELIVERY_VAN);
        }
        else
        {
            LOG_ERROR("OSIReporter::UpdateOSIMovingObject -> Unsupported moving object vehicle category: {} ({}). Set to UNKNOWN.",
                      objectState->state_.info.obj_category,
                      Vehicle::Category2String(objectState->state_.info.obj_category));
            obj_osi_internal.mobj->mutable_vehicle_classification()->set_type(osi3::MovingObject_VehicleClassification::TYPE_UNKNOWN);
        }

        // GT_esmini: Update OSI light state using hook callback
        if (g_LightStateProvider)
        {
            // Get Object pointer using the correct method
            scenarioengine::Object* obj = scenario_engine_->entities_.GetObjectById(objectState->state_.info.id);
            if (obj && obj->GetType() == scenarioengine::Object::Type::VEHICLE)
            {
                auto* vehicle = static_cast<scenarioengine::Vehicle*>(obj);
                auto* classification = obj_osi_internal.mobj->mutable_vehicle_classification();
                auto* light_state = classification->mutable_light_state();

                // Clear/initialize all light states to OFF/default before setting
                light_state->Clear();
                light_state->set_indicator_state(osi3::MovingObject_VehicleClassification_LightState::INDICATOR_STATE_OFF);
                light_state->set_brake_light_state(osi3::MovingObject_VehicleClassification_LightState::BRAKE_LIGHT_STATE_OFF);
                light_state->set_head_light(osi3::MovingObject_VehicleClassification_LightState::GENERIC_LIGHT_STATE_OFF);
                light_state->set_high_beam(osi3::MovingObject_VehicleClassification_LightState::GENERIC_LIGHT_STATE_OFF);
                light_state->set_reversing_light(osi3::MovingObject_VehicleClassification_LightState::GENERIC_LIGHT_STATE_OFF);
                light_state->set_front_fog_light(osi3::MovingObject_VehicleClassification_LightState::GENERIC_LIGHT_STATE_OFF);
                light_state->set_rear_fog_light(osi3::MovingObject_VehicleClassification_LightState::GENERIC_LIGHT_STATE_OFF);

                // Helper lambda to check light state using the provider hook
                auto is_on = [&](::gt_esmini::VehicleLightType type) -> bool {
                    auto state = g_LightStateProvider(static_cast<void*>(vehicle), static_cast<int>(type));
                    bool result = (state.mode != ::gt_esmini::LightState::Mode::OFF);

                    // Debug log (only log occasionally to reduce spam)
                    static int log_counter = 0;
                    if (log_counter++ % 100 == 0)
                    {
                        LOG_DEBUG("Light check: type={} mode={} result={}", static_cast<int>(type), static_cast<int>(state.mode), result);
                    }

                    return result;
                };

                // Indicators
                if (is_on(::gt_esmini::VehicleLightType::INDICATOR_LEFT) && is_on(::gt_esmini::VehicleLightType::INDICATOR_RIGHT))
                {
                    light_state->set_indicator_state(osi3::MovingObject_VehicleClassification_LightState::INDICATOR_STATE_WARNING);
                }
                else if (is_on(::gt_esmini::VehicleLightType::INDICATOR_LEFT))
                {
                    light_state->set_indicator_state(osi3::MovingObject_VehicleClassification_LightState::INDICATOR_STATE_LEFT);
                }
                else if (is_on(::gt_esmini::VehicleLightType::INDICATOR_RIGHT))
                {
                    light_state->set_indicator_state(osi3::MovingObject_VehicleClassification_LightState::INDICATOR_STATE_RIGHT);
                }
                else
                {
                    light_state->set_indicator_state(osi3::MovingObject_VehicleClassification_LightState::INDICATOR_STATE_OFF);
                }

                // Brake Lights
                if (is_on(::gt_esmini::VehicleLightType::BRAKE_LIGHTS))
                {
                    light_state->set_brake_light_state(osi3::MovingObject_VehicleClassification_LightState::BRAKE_LIGHT_STATE_NORMAL);
                }
                else
                {
                    light_state->set_brake_light_state(osi3::MovingObject_VehicleClassification_LightState::BRAKE_LIGHT_STATE_OFF);
                }

                // Head Lights (Low Beam)
                if (is_on(::gt_esmini::VehicleLightType::LOW_BEAM))
                {
                    light_state->set_head_light(osi3::MovingObject_VehicleClassification_LightState::GENERIC_LIGHT_STATE_ON);
                }
                else
                {
                    light_state->set_head_light(osi3::MovingObject_VehicleClassification_LightState::GENERIC_LIGHT_STATE_OFF);
                }

                // High Beam
                if (is_on(::gt_esmini::VehicleLightType::HIGH_BEAM))
                {
                    light_state->set_high_beam(osi3::MovingObject_VehicleClassification_LightState::GENERIC_LIGHT_STATE_ON);
                }
                else
                {
                    light_state->set_high_beam(osi3::MovingObject_VehicleClassification_LightState::GENERIC_LIGHT_STATE_OFF);
                }

                // Fog Lights (Front)
                if (is_on(::gt_esmini::VehicleLightType::FOG_LIGHTS) || is_on(::gt_esmini::VehicleLightType::FOG_LIGHTS_FRONT))
                {
                    light_state->set_front_fog_light(osi3::MovingObject_VehicleClassification_LightState::GENERIC_LIGHT_STATE_ON);
                }
                else
                {
                    light_state->set_front_fog_light(osi3::MovingObject_VehicleClassification_LightState::GENERIC_LIGHT_STATE_OFF);
                }

                // Fog Lights (Rear)
                if (is_on(::gt_esmini::VehicleLightType::FOG_LIGHTS) || is_on(::gt_esmini::VehicleLightType::FOG_LIGHTS_REAR))
                {
                    light_state->set_rear_fog_light(osi3::MovingObject_VehicleClassification_LightState::GENERIC_LIGHT_STATE_ON);
                }
                else
                {
                    light_state->set_rear_fog_light(osi3::MovingObject_VehicleClassification_LightState::GENERIC_LIGHT_STATE_OFF);
                }

                // Reversing Lights
                if (is_on(::gt_esmini::VehicleLightType::REVERSING_LIGHTS))
                {
                    light_state->set_reversing_light(osi3::MovingObject_VehicleClassification_LightState::GENERIC_LIGHT_STATE_ON);
                }
                else
                {
                    light_state->set_reversing_light(osi3::MovingObject_VehicleClassification_LightState::GENERIC_LIGHT_STATE_OFF);
                }
            }
        }

        // [New] Generate Future Trajectory
        if (this->scenario_engine_)
        {
            int id = objectState->state_.info.id;
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
                if (objectState->state_.info.ctrl_type == Controller::Type::GHOST_RESERVED_TYPE) {
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
                    int ctrlType = objectState->state_.info.ctrl_type;
                    bool isEgoOrExternal = (objectState->state_.info.id == 0) ||
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

        if (objectState->state_.info.obj_role == static_cast<int>(Object::Role::AMBULANCE))
        {
            obj_osi_internal.mobj->mutable_vehicle_classification()->set_role(osi3::MovingObject_VehicleClassification::ROLE_AMBULANCE);
        }
        else if (objectState->state_.info.obj_role == static_cast<int>(Object::Role::CIVIL))
        {
            obj_osi_internal.mobj->mutable_vehicle_classification()->set_role(osi3::MovingObject_VehicleClassification::ROLE_CIVIL);
        }
        else if (objectState->state_.info.obj_role == static_cast<int>(Object::Role::FIRE))
        {
            obj_osi_internal.mobj->mutable_vehicle_classification()->set_role(osi3::MovingObject_VehicleClassification::ROLE_FIRE);
        }
        else if (objectState->state_.info.obj_role == static_cast<int>(Object::Role::MILITARY))
        {
            obj_osi_internal.mobj->mutable_vehicle_classification()->set_role(osi3::MovingObject_VehicleClassification::ROLE_MILITARY);
        }
        else if (objectState->state_.info.obj_role == static_cast<int>(Object::Role::POLICE))
        {
            obj_osi_internal.mobj->mutable_vehicle_classification()->set_role(osi3::MovingObject_VehicleClassification::ROLE_POLICE);
        }
        else if (objectState->state_.info.obj_role == static_cast<int>(Object::Role::PUBLIC_TRANSPORT))
        {
            obj_osi_internal.mobj->mutable_vehicle_classification()->set_role(osi3::MovingObject_VehicleClassification::ROLE_PUBLIC_TRANSPORT);
        }
        else if (objectState->state_.info.obj_role == static_cast<int>(Object::Role::ROAD_ASSISTANCE))
        {
            obj_osi_internal.mobj->mutable_vehicle_classification()->set_role(osi3::MovingObject_VehicleClassification::ROLE_ROAD_ASSISTANCE);
        }
        else if (objectState->state_.info.obj_role == static_cast<int>(Object::Role::NONE))
        {
            obj_osi_internal.mobj->mutable_vehicle_classification()->set_role(osi3::MovingObject_VehicleClassification::ROLE_UNKNOWN);
        }
        else
        {
            LOG_ERROR("OSIReporter::UpdateOSIMovingObject -> Unsupported moving object vehicle role: {} ({}). Set classification UNKNOWN.",
                      objectState->state_.info.obj_role,
                      Vehicle::Role2String(objectState->state_.info.obj_role).c_str());
            obj_osi_internal.mobj->mutable_vehicle_classification()->set_role(osi3::MovingObject_VehicleClassification::ROLE_UNKNOWN);
        }
    }
    else if (objectState->state_.info.obj_type == static_cast<int>(Object::Type::PEDESTRIAN))
    {
        entity_type = "Pedestrian";
        if (objectState->state_.info.obj_category == static_cast<int>(Pedestrian::Category::PEDESTRIAN))
        {
            obj_osi_internal.mobj->set_type(osi3::MovingObject::Type::MovingObject_Type_TYPE_PEDESTRIAN);
        }
        else if (objectState->state_.info.obj_category == static_cast<int>(Pedestrian::Category::ANIMAL))
        {
            obj_osi_internal.mobj->set_type(osi3::MovingObject::Type::MovingObject_Type_TYPE_ANIMAL);
        }
        else if (objectState->state_.info.obj_category == static_cast<int>(Pedestrian::Category::WHEELCHAIR))
        {
            obj_osi_internal.mobj->set_type(osi3::MovingObject::Type::MovingObject_Type_TYPE_OTHER);
        }
        else
        {
            LOG_ERROR("OSIReporter::UpdateOSIMovingObject -> Unsupported moving object pedestrian category: {} ({}). Set type UNKNOWN.",
                      objectState->state_.info.obj_category,
                      Pedestrian::Category2String(objectState->state_.info.obj_category));
            obj_osi_internal.mobj->set_type(osi3::MovingObject::Type::MovingObject_Type_TYPE_UNKNOWN);
        }
    }
    else
    {
        LOG_ERROR("OSIReporter::UpdateOSIMovingObject -> Unsupported moving object type: {} ({}). Set UNKNOWN.",
                  objectState->state_.info.obj_type,
                  Object::Type2String(objectState->state_.info.obj_type));
        obj_osi_internal.mobj->set_type(osi3::MovingObject::Type::MovingObject_Type_TYPE_UNKNOWN);
    }

    // Set OSI Moving Object Control Type
    obj_osi_internal.mobj->mutable_vehicle_attributes()->mutable_driver_id()->set_value(static_cast<uint64_t>(objectState->state_.info.ctrl_type));

    // Set OSI Moving Object Boundingbox
    obj_osi_internal.mobj->mutable_vehicle_attributes()->mutable_bbcenter_to_rear()->set_x(
        static_cast<double>(-objectState->state_.info.boundingbox.center_.x_));
    obj_osi_internal.mobj->mutable_vehicle_attributes()->mutable_bbcenter_to_rear()->set_y(
        static_cast<double>(-objectState->state_.info.boundingbox.center_.y_));
    obj_osi_internal.mobj->mutable_vehicle_attributes()->mutable_bbcenter_to_rear()->set_z(
        objectState->state_.info.rear_axle_z_pos - static_cast<double>(objectState->state_.info.boundingbox.center_.z_));
    obj_osi_internal.mobj->mutable_vehicle_attributes()->mutable_bbcenter_to_front()->set_x(
        objectState->state_.info.front_axle_x_pos - static_cast<double>(objectState->state_.info.boundingbox.center_.x_));
    obj_osi_internal.mobj->mutable_vehicle_attributes()->mutable_bbcenter_to_front()->set_y(
        static_cast<double>(-objectState->state_.info.boundingbox.center_.y_));
    obj_osi_internal.mobj->mutable_vehicle_attributes()->mutable_bbcenter_to_front()->set_z(
        objectState->state_.info.front_axle_z_pos - static_cast<double>(objectState->state_.info.boundingbox.center_.z_));
    obj_osi_internal.mobj->mutable_base()->mutable_dimension()->set_height(objectState->state_.info.boundingbox.dimensions_.height_);
    obj_osi_internal.mobj->mutable_base()->mutable_dimension()->set_width(objectState->state_.info.boundingbox.dimensions_.width_);
    obj_osi_internal.mobj->mutable_base()->mutable_dimension()->set_length(objectState->state_.info.boundingbox.dimensions_.length_);

    // OSI XYZ is center of BB, have been calculated in SetOsiXYZ
    obj_osi_internal.mobj->mutable_base()->mutable_position()->set_x(objectState->state_.pos.GetOsiX());
    obj_osi_internal.mobj->mutable_base()->mutable_position()->set_y(objectState->state_.pos.GetOsiY());
    obj_osi_internal.mobj->mutable_base()->mutable_position()->set_z(objectState->state_.pos.GetOsiZ());

    // Set OSI Moving Object Orientation
    obj_osi_internal.mobj->mutable_base()->mutable_orientation()->set_roll(GetAngleInIntervalMinusPIPlusPI(objectState->state_.pos.GetR()));
    obj_osi_internal.mobj->mutable_base()->mutable_orientation()->set_pitch(GetAngleInIntervalMinusPIPlusPI(objectState->state_.pos.GetP()));
    obj_osi_internal.mobj->mutable_base()->mutable_orientation()->set_yaw(GetAngleInIntervalMinusPIPlusPI(objectState->state_.pos.GetH()));
    obj_osi_internal.mobj->mutable_base()->mutable_orientation_rate()->set_yaw(objectState->state_.pos.GetHRate());
    obj_osi_internal.mobj->mutable_base()->mutable_orientation_rate()->set_pitch(objectState->state_.pos.GetPRate());
    obj_osi_internal.mobj->mutable_base()->mutable_orientation_rate()->set_roll(objectState->state_.pos.GetRRate());
    obj_osi_internal.mobj->mutable_base()->mutable_orientation_acceleration()->set_yaw(objectState->state_.pos.GetHAcc());
    obj_osi_internal.mobj->mutable_base()->mutable_orientation_acceleration()->set_pitch(objectState->state_.pos.GetPAcc());
    obj_osi_internal.mobj->mutable_base()->mutable_orientation_acceleration()->set_roll(objectState->state_.pos.GetRAcc());

    // Set OSI Moving Object Velocity
    obj_osi_internal.mobj->mutable_base()->mutable_velocity()->set_x(objectState->state_.pos.GetVelX());
    obj_osi_internal.mobj->mutable_base()->mutable_velocity()->set_y(objectState->state_.pos.GetVelY());
    obj_osi_internal.mobj->mutable_base()->mutable_velocity()->set_z(objectState->state_.pos.GetVelZ());

    // Set OSI Moving Object Acceleration
    obj_osi_internal.mobj->mutable_base()->mutable_acceleration()->set_x(objectState->state_.pos.GetAccX());
    obj_osi_internal.mobj->mutable_base()->mutable_acceleration()->set_y(objectState->state_.pos.GetAccY());
    obj_osi_internal.mobj->mutable_base()->mutable_acceleration()->set_z(objectState->state_.pos.GetAccZ());

    // Set ego lane
    obj_osi_internal.mobj->add_assigned_lane_id()->set_value(objectState->state_.pos.GetLaneGlobalId());

    // simplified wheel info, set nr wheels based on object type
    // can be improved by considering axels and actual wheel configuration

    if (objectState->state_.info.obj_type == static_cast<int>(Object::Type::VEHICLE))
    {
        // Set some data for each wheel
        for (unsigned int i = 0; i < objectState->state_.info.wheel_data.size(); i++)
        {
            if (objectState->state_.info.wheel_data[i].axle > -1)
            {
                // create wheel data message
                int ii = static_cast<int>(i);
                obj_osi_internal.mobj->mutable_vehicle_attributes()->add_wheel_data();
                obj_osi_internal.mobj->mutable_vehicle_attributes()->mutable_wheel_data(ii)->mutable_position()->set_x(
                    objectState->state_.info.wheel_data[i].x - static_cast<double>(objectState->state_.info.boundingbox.center_.x_));
                obj_osi_internal.mobj->mutable_vehicle_attributes()->mutable_wheel_data(ii)->mutable_position()->set_y(
                    objectState->state_.info.wheel_data[i].y - static_cast<double>(objectState->state_.info.boundingbox.center_.y_));
                obj_osi_internal.mobj->mutable_vehicle_attributes()->mutable_wheel_data(ii)->mutable_position()->set_z(
                    objectState->state_.info.wheel_data[i].z - static_cast<double>(objectState->state_.info.boundingbox.center_.z_));

                obj_osi_internal.mobj->mutable_vehicle_attributes()->mutable_wheel_data(ii)->mutable_orientation()->set_yaw(
                    objectState->state_.info.wheel_data[i].h);
                obj_osi_internal.mobj->mutable_vehicle_attributes()->mutable_wheel_data(ii)->mutable_orientation()->set_pitch(
                    objectState->state_.info.wheel_data[i].p);
                obj_osi_internal.mobj->mutable_vehicle_attributes()->mutable_wheel_data(ii)->set_friction_coefficient(
                    objectState->state_.info.wheel_data[i].friction_coefficient);
                obj_osi_internal.mobj->mutable_vehicle_attributes()->mutable_wheel_data(ii)->set_axle(
                    static_cast<unsigned int>(objectState->state_.info.wheel_data[i].axle));
                obj_osi_internal.mobj->mutable_vehicle_attributes()->mutable_wheel_data(ii)->set_index(
                    static_cast<unsigned int>(objectState->state_.info.wheel_data[i].index));  // Index along axis
                obj_osi_internal.mobj->mutable_vehicle_attributes()->mutable_wheel_data(ii)->set_wheel_radius(
                    objectState->state_.info.wheel_data[i].wheel_radius);
                obj_osi_internal.mobj->mutable_vehicle_attributes()->mutable_wheel_data(ii)->set_rotation_rate(
                    objectState->state_.info.wheel_data[i].rotation_rate);
                obj_osi_internal.mobj->mutable_vehicle_attributes()->set_number_wheels(
                    static_cast<unsigned int>(objectState->state_.info.wheel_data.size()));
            }
        }
    }

    // Set 3D model file as OSI model reference
    obj_osi_internal.mobj->set_model_reference(objectState->state_.info.model3d);

    // SOURCE REFERENCE
    auto source_reference = obj_osi_internal.mobj->add_source_reference();
    source_reference->set_type(SOURCE_REF_TYPE_OSC);

    source_reference->add_identifier(fmt::format("entity_id:{}", objectState->state_.info.id));
    source_reference->add_identifier(fmt::format("entity_type:{}", entity_type));
    source_reference->add_identifier(fmt::format("entity_name:{}", objectState->state_.info.name));

    // Set source reference if available
    if (!objectState->state_.info.source_reference.empty())
    {
        for (const auto &ref : objectState->state_.info.source_reference)
        {
            source_reference->add_identifier(ref);
        }
    }

    return 0;
}

