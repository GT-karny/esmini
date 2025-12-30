/*
 * GT_esmini - Extended esmini with Light Functionality
 * https://github.com/esmini/esmini
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2024 GT_esmini contributors
 */

#include "ExtraAction.hpp"
#include "ExtraEntities.hpp"
#include "Entities.hpp"
#include "logger.hpp"

namespace gt_esmini
{
    OSCLightStateAction::OSCLightStateAction(scenarioengine::StoryBoardElement* parent)
        : scenarioengine::OSCPrivateAction(scenarioengine::OSCAction::ActionType::USER_DEFINED, parent, 0),
          transitionTime_(0.0),
          startTime_(0.0),
          isTransitioning_(false)
    {
        // Phase 1: Stub implementation
        // Phase 2: Implement initialization logic
    }

    OSCLightStateAction::~OSCLightStateAction()
    {
        // Phase 1: Stub implementation
    }

    void OSCLightStateAction::Start(double simTime)
    {
        // Phase 2: Implement light state change start logic
        OSCPrivateAction::Start(simTime);
        
        startTime_ = simTime;
        
        // Get target vehicle
        if (object_ == nullptr)
        {
            LOG_ERROR("LightStateAction::Start: object_ is null");
            return;
        }
        
        if (object_->type_ != scenarioengine::Object::Type::VEHICLE)
        {
            LOG_ERROR("LightStateAction::Start: object is not a Vehicle");
            return;
        }
        
        scenarioengine::Vehicle* vehicle = static_cast<scenarioengine::Vehicle*>(object_);
        
        // Get VehicleLightExtension
        auto* ext = VehicleExtensionManager::Instance().GetExtension(vehicle);
        if (ext == nullptr)
        {
            LOG_ERROR("LightStateAction::Start: VehicleLightExtension not found for vehicle '{}'", vehicle->GetName());
            return;
        }
        
        // Apply light state
        if (transitionTime_ <= 0.0)
        {
            // Immediate application
            ext->SetLightState(lightType_, lightState_);
            isTransitioning_ = false;
            
#ifdef GT_ESMINI_DEBUG_LOGGING
            LOG_INFO("LightStateAction started (immediate): vehicle={}, lightType={}, mode={}",
                     vehicle->GetName(),
                     static_cast<int>(lightType_),
                     static_cast<int>(lightState_.mode));
#endif
        }
        else
        {
            // Start transition
            isTransitioning_ = true;
            
#ifdef GT_ESMINI_DEBUG_LOGGING
            LOG_INFO("LightStateAction started (transition): vehicle={}, lightType={}, mode={}, transitionTime={}",
                     vehicle->GetName(),
                     static_cast<int>(lightType_),
                     static_cast<int>(lightState_.mode),
                     transitionTime_);
#endif
        }
    }

    void OSCLightStateAction::Step(double simTime, double dt)
    {
        // Phase 2: Implement transition logic
        OSCPrivateAction::Step(simTime, dt);
        
        if (isTransitioning_)
        {
            double elapsedTime = simTime - startTime_;
            
            if (elapsedTime >= transitionTime_)
            {
                // Transition complete - apply final state
                if (object_ != nullptr && object_->type_ == scenarioengine::Object::Type::VEHICLE)
                {
                    scenarioengine::Vehicle* vehicle = static_cast<scenarioengine::Vehicle*>(object_);
                    auto* ext = VehicleExtensionManager::Instance().GetExtension(vehicle);
                    
                    if (ext != nullptr)
                    {
                        ext->SetLightState(lightType_, lightState_);
                    }
                }
                
                isTransitioning_ = false;
            }
            // Note: For now, we apply the state immediately after transition time
            // Future enhancement: implement gradual fade effect
        }
    }

    void OSCLightStateAction::End()
    {
        // Phase 2: Implement light state change completion logic
        OSCPrivateAction::End();
        
        // If still transitioning, apply final state immediately
        if (isTransitioning_)
        {
            if (object_ != nullptr && object_->type_ == scenarioengine::Object::Type::VEHICLE)
            {
                scenarioengine::Vehicle* vehicle = static_cast<scenarioengine::Vehicle*>(object_);
                auto* ext = VehicleExtensionManager::Instance().GetExtension(vehicle);
                
                if (ext != nullptr)
                {
                    ext->SetLightState(lightType_, lightState_);
                }
            }
            
            isTransitioning_ = false;
        }
        
#ifdef GT_ESMINI_DEBUG_LOGGING
        if (object_ != nullptr && object_->type_ == scenarioengine::Object::Type::VEHICLE)
        {
            scenarioengine::Vehicle* vehicle = static_cast<scenarioengine::Vehicle*>(object_);
            LOG_INFO("LightStateAction ended: vehicle={}", vehicle->GetName());
        }
#endif
    }

    scenarioengine::OSCPrivateAction* OSCLightStateAction::Copy()
    {
        // Phase 1: Stub implementation
        // Phase 2: Implement deep copy logic
        OSCLightStateAction* newAction = new OSCLightStateAction(parent_);
        newAction->lightType_      = lightType_;
        newAction->lightState_     = lightState_;
        newAction->transitionTime_ = transitionTime_;
        return newAction;
    }

}  // namespace gt_esmini
