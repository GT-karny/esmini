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

#include "GT_ScenarioReader.hpp"
#include "ExtraEntities.hpp"

namespace gt_esmini
{
    GT_ScenarioReader::GT_ScenarioReader(scenarioengine::Entities*       entities,
                                         scenarioengine::Catalogs*       catalogs,
                                         scenarioengine::OSCEnvironment* environment,
                                         bool                            disable_controllers)
        : scenarioengine::ScenarioReader(entities, catalogs, environment, disable_controllers)
    {
        // Phase 1: Stub implementation
        // Only call parent class constructor
    }

    GT_ScenarioReader::~GT_ScenarioReader()
    {
        // Phase 1: Stub implementation
    }

    scenarioengine::OSCPrivateAction* GT_ScenarioReader::parseOSCPrivateAction(pugi::xml_node           actionNode,
                                                                                scenarioengine::Object*  object,
                                                                                scenarioengine::Event*   parent)
    {
        // Phase 2: Check for AppearanceAction and delegate to ParseAppearanceAction
        // All other actions are delegated to parent class implementation
        
        for (pugi::xml_node actionChild = actionNode.first_child(); actionChild; actionChild = actionChild.next_sibling())
        {
            if (actionChild.name() == std::string("AppearanceAction"))
            {
                return ParseAppearanceAction(actionChild, object, parent);
            }
        }
        
        // Delegate all other actions to parent class
        return scenarioengine::ScenarioReader::parseOSCPrivateAction(actionNode, object, parent);
    }

    OSCLightStateAction* GT_ScenarioReader::ParseLightStateAction(pugi::xml_node node)
    {
        // Phase 2: Implement actual parsing logic
        OSCLightStateAction* action = new OSCLightStateAction(nullptr);
        
        // Parse LightType
        pugi::xml_node lightTypeNode = node.child("LightType");
        if (lightTypeNode.empty())
        {
            LOG_ERROR("LightStateAction: Missing mandatory LightType element");
            delete action;
            return nullptr;
        }
        
        pugi::xml_node vehicleLightNode = lightTypeNode.child("VehicleLight");
        if (vehicleLightNode.empty())
        {
            LOG_ERROR("LightStateAction: Missing VehicleLight element in LightType");
            delete action;
            return nullptr;
        }
        
        std::string lightTypeStr = parameters.ReadAttribute(vehicleLightNode, "vehicleLightType");
        if (lightTypeStr.empty())
        {
            LOG_ERROR("LightStateAction: Missing mandatory vehicleLightType attribute");
            delete action;
            return nullptr;
        }
        
        // Convert string to VehicleLightType enum
        if (lightTypeStr == "daytimeRunningLights")
            action->lightType_ = VehicleLightType::DAYTIME_RUNNING_LIGHTS;
        else if (lightTypeStr == "lowBeam")
            action->lightType_ = VehicleLightType::LOW_BEAM;
        else if (lightTypeStr == "highBeam")
            action->lightType_ = VehicleLightType::HIGH_BEAM;
        else if (lightTypeStr == "fogLights")
            action->lightType_ = VehicleLightType::FOG_LIGHTS;
        else if (lightTypeStr == "fogLightsFront")
            action->lightType_ = VehicleLightType::FOG_LIGHTS_FRONT;
        else if (lightTypeStr == "fogLightsRear")
            action->lightType_ = VehicleLightType::FOG_LIGHTS_REAR;
        else if (lightTypeStr == "brakeLights")
            action->lightType_ = VehicleLightType::BRAKE_LIGHTS;
        else if (lightTypeStr == "warningLights")
            action->lightType_ = VehicleLightType::WARNING_LIGHTS;
        else if (lightTypeStr == "indicatorLeft")
            action->lightType_ = VehicleLightType::INDICATOR_LEFT;
        else if (lightTypeStr == "indicatorRight")
            action->lightType_ = VehicleLightType::INDICATOR_RIGHT;
        else if (lightTypeStr == "reversingLights")
            action->lightType_ = VehicleLightType::REVERSING_LIGHTS;
        else if (lightTypeStr == "licensePlateIllumination")
            action->lightType_ = VehicleLightType::LICENSE_PLATE_ILLUMINATION;
        else if (lightTypeStr == "specialPurposeLights")
            action->lightType_ = VehicleLightType::SPECIAL_PURPOSE_LIGHTS;
        else
        {
            LOG_WARN("LightStateAction: Unknown vehicleLightType '{}', defaulting to brakeLights", lightTypeStr);
            action->lightType_ = VehicleLightType::BRAKE_LIGHTS;
        }
        
        // Parse LightState
        pugi::xml_node lightStateNode = node.child("LightState");
        if (lightStateNode.empty())
        {
            LOG_ERROR("LightStateAction: Missing mandatory LightState element");
            delete action;
            return nullptr;
        }
        
        std::string modeStr = parameters.ReadAttribute(lightStateNode, "mode");
        if (modeStr.empty())
        {
            LOG_ERROR("LightStateAction: Missing mandatory mode attribute");
            delete action;
            return nullptr;
        }
        
        // Convert string to LightState::Mode enum
        if (modeStr == "on")
            action->lightState_.mode = LightState::Mode::ON;
        else if (modeStr == "off")
            action->lightState_.mode = LightState::Mode::OFF;
        else if (modeStr == "flashing")
            action->lightState_.mode = LightState::Mode::FLASHING;
        else
        {
            LOG_WARN("LightStateAction: Unknown mode '{}', defaulting to off", modeStr);
            action->lightState_.mode = LightState::Mode::OFF;
        }
        
        // Parse optional attributes
        if (!lightStateNode.attribute("luminousIntensity").empty())
        {
            action->lightState_.luminousIntensity = strtod(parameters.ReadAttribute(lightStateNode, "luminousIntensity"));
        }
        
        if (!lightStateNode.attribute("flashingOnDuration").empty())
        {
            action->lightState_.flashingOnDuration = strtod(parameters.ReadAttribute(lightStateNode, "flashingOnDuration"));
        }
        
        if (!lightStateNode.attribute("flashingOffDuration").empty())
        {
            action->lightState_.flashingOffDuration = strtod(parameters.ReadAttribute(lightStateNode, "flashingOffDuration"));
        }
        
        // Parse optional Color element
        pugi::xml_node colorNode = lightStateNode.child("Color");
        if (!colorNode.empty())
        {
            if (!colorNode.attribute("r").empty())
                action->lightState_.colorR = strtod(parameters.ReadAttribute(colorNode, "r"));
            if (!colorNode.attribute("g").empty())
                action->lightState_.colorG = strtod(parameters.ReadAttribute(colorNode, "g"));
            if (!colorNode.attribute("b").empty())
                action->lightState_.colorB = strtod(parameters.ReadAttribute(colorNode, "b"));
        }
        
        // Parse transitionTime attribute
        if (!node.attribute("transitionTime").empty())
        {
            action->transitionTime_ = strtod(parameters.ReadAttribute(node, "transitionTime"));
        }
        
        return action;
    }

    scenarioengine::OSCPrivateAction* GT_ScenarioReader::ParseAppearanceAction(pugi::xml_node          node,
                                                                                scenarioengine::Object* object,
                                                                                scenarioengine::Event*  parent)
    {
        // Phase 2: Implement actual parsing logic
        
        // Check AppearanceAction child elements
        pugi::xml_node appearanceChild = node.first_child();
        
        if (appearanceChild.empty())
        {
            LOG_WARN("AppearanceAction: No child element found");
            return nullptr;
        }
        
        if (std::string(appearanceChild.name()) == "LightStateAction")
        {
            OSCLightStateAction* action = ParseLightStateAction(appearanceChild);
            
            if (action != nullptr)
            {
                // Register VehicleLightExtension to Vehicle if not already registered
                if (object != nullptr && object->type_ == scenarioengine::Object::Type::VEHICLE)
                {
                    scenarioengine::Vehicle* vehicle = static_cast<scenarioengine::Vehicle*>(object);
                    
                    // Check if extension already exists
                    auto* ext = VehicleExtensionManager::Instance().GetExtension(vehicle);
                    if (ext == nullptr)
                    {
                        // Create and register new extension
                        ext = new VehicleLightExtension(vehicle);
                        VehicleExtensionManager::Instance().RegisterExtension(vehicle, ext);
                    }
                }
                
                // Set parent for the action
                action->parent_ = parent;
                action->object_ = object;
            }
            
            return action;
        }
        else
        {
            LOG_WARN("AppearanceAction: Unsupported child element '{}'", appearanceChild.name());
            return nullptr;
        }
    }

}  // namespace gt_esmini
