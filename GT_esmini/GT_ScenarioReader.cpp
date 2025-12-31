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

#ifdef Object
#undef Object
#endif
#include "GT_ScenarioReader.hpp"
#include "ExtraEntities.hpp"
#include <vector>
#include <string>

namespace gt_esmini
{
    GT_ScenarioReader::GT_ScenarioReader(scenarioengine::Entities*       entities,
                                         scenarioengine::Catalogs*       catalogs,
                                         scenarioengine::OSCEnvironment* environment,
                                         bool                            disable_controllers)
        : scenarioengine::ScenarioReader(entities, catalogs, environment, disable_controllers)
    {
        this->entities = entities;
    }

    GT_ScenarioReader::~GT_ScenarioReader()
    {
    }

    scenarioengine::OSCPrivateAction* GT_ScenarioReader::parseOSCPrivateAction(pugi::xml_node           actionNode,
                                                                                scenarioengine::Object*  object,
                                                                                scenarioengine::Event*   parent)
    {
        for (pugi::xml_node actionChild = actionNode.first_child(); actionChild; actionChild = actionChild.next_sibling())
        {
            if (std::string(actionChild.name()) == "AppearanceAction")
            {
                return ParseAppearanceAction(actionChild, object, parent);
            }
        }
        
        // Delegate all other actions to parent class
        return scenarioengine::ScenarioReader::parseOSCPrivateAction(actionNode, object, parent);
    }

    OSCLightStateAction* GT_ScenarioReader::ParseLightStateAction(pugi::xml_node node)
    {
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
        
        std::string lightTypeStr = parameters.ReadAttribute(vehicleLightNode, "vehicleLightType"); // Assumes 'parameters' member is available and initialized, or use generic reading
        // Wait, 'parameters' is member of ScenarioReader? Check inheritance. 
        // ScenarioReader has 'OSCParameterDeclarations parameters'. Yes.
        
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
            action->lightState_.luminousIntensity = strtod(parameters.ReadAttribute(lightStateNode, "luminousIntensity").c_str(), nullptr);
        }
        
        if (!lightStateNode.attribute("flashingOnDuration").empty())
        {
            action->lightState_.flashingOnDuration = strtod(parameters.ReadAttribute(lightStateNode, "flashingOnDuration").c_str(), nullptr);
        }
        
        if (!lightStateNode.attribute("flashingOffDuration").empty())
        {
            action->lightState_.flashingOffDuration = strtod(parameters.ReadAttribute(lightStateNode, "flashingOffDuration").c_str(), nullptr);
        }
        
        // Parse optional Color element
        pugi::xml_node colorNode = lightStateNode.child("Color");
        if (!colorNode.empty())
        {
            if (!colorNode.attribute("r").empty())
                action->lightState_.colorR = strtod(parameters.ReadAttribute(colorNode, "r").c_str(), nullptr);
            if (!colorNode.attribute("g").empty())
                action->lightState_.colorG = strtod(parameters.ReadAttribute(colorNode, "g").c_str(), nullptr);
            if (!colorNode.attribute("b").empty())
                action->lightState_.colorB = strtod(parameters.ReadAttribute(colorNode, "b").c_str(), nullptr);
        }
        
        // Parse transitionTime attribute based on XML (node is LightStateAction)
        if (!node.attribute("transitionTime").empty())
        {
            action->transitionTime_ = strtod(parameters.ReadAttribute(node, "transitionTime").c_str(), nullptr);
        }
        
        return action;
    }

    scenarioengine::OSCPrivateAction* GT_ScenarioReader::ParseAppearanceAction(pugi::xml_node          node,
                                                                                scenarioengine::Object* object,
                                                                                scenarioengine::Event*  parent)
    {
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
                    
                    auto* ext = VehicleExtensionManager::Instance().GetExtension(vehicle);
                    if (ext == nullptr)
                    {
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

    void GT_ScenarioReader::ParseExtensionActions(const pugi::xml_document& doc, scenarioengine::StoryBoard& storyBoard)
    {
        pugi::xml_node oscNode = doc.child("OpenSCENARIO");
        pugi::xml_node sbNode = oscNode.child("Storyboard");
        
        if (sbNode.empty()) return;

        // 1. Parse Init actions
        pugi::xml_node initNode = sbNode.child("Init");
        if (!initNode.empty())
        {
            pugi::xml_node actionsNode = initNode.child("Actions");
            if (!actionsNode.empty())
            {
                for (pugi::xml_node privNode = actionsNode.child("Private"); privNode; privNode = privNode.next_sibling("Private"))
                {
                    std::string entityRef = parameters.ReadAttribute(privNode, "entityRef");
                    scenarioengine::Object* object = entities->GetObjectByName(entityRef);
                    if (!object) continue;

                    for (pugi::xml_node actionNode = privNode.first_child(); actionNode; actionNode = actionNode.next_sibling())
                    {
                         if (std::string(actionNode.name()) == "AppearanceAction")
                         {
                             auto* action = ParseAppearanceAction(actionNode, object, nullptr);
                             if (action)
                             {
                                 storyBoard.init_.private_action_.push_back(action);
                                 // Execute immediately since StartTrigger for Init has already passed
                                 action->Start(0.0); 
                             }
                         }
                    }
                }
            }
        }

        // 2. Parse Stories
        for (pugi::xml_node storyNode = sbNode.child("Story"); storyNode; storyNode = storyNode.next_sibling("Story"))
        {
            std::string storyName = parameters.ReadAttribute(storyNode, "name");
            scenarioengine::Story* storyObj = nullptr;
            for(auto* s : storyBoard.story_) if(s->GetName() == storyName) { storyObj = s; break; }
            if(!storyObj) continue;

            for (pugi::xml_node actNode = storyNode.child("Act"); actNode; actNode = actNode.next_sibling("Act"))
            {
                std::string actName = parameters.ReadAttribute(actNode, "name");
                scenarioengine::Act* actObj = nullptr;
                for(auto* a : storyObj->act_) if(a->GetName() == actName) { actObj = a; break; }
                if(!actObj) continue;

                for (pugi::xml_node mgNode = actNode.child("ManeuverGroup"); mgNode; mgNode = mgNode.next_sibling("ManeuverGroup"))
                {
                    std::string mgName = parameters.ReadAttribute(mgNode, "name");
                    scenarioengine::ManeuverGroup* mgObj = nullptr;
                    for(auto* m : actObj->maneuverGroup_) if(m->GetName() == mgName) { mgObj = m; break; }
                    if(!mgObj) continue;

                    for (pugi::xml_node mNode = mgNode.child("Maneuver"); mNode; mNode = mNode.next_sibling("Maneuver"))
                    {
                        std::string mName = parameters.ReadAttribute(mNode, "name");
                        scenarioengine::Maneuver* mObj = nullptr;
                        for(auto* m : mgObj->maneuver_) if(m->GetName() == mName) { mObj = m; break; }
                        if(!mObj) continue;

                        for (pugi::xml_node evtNode = mNode.child("Event"); evtNode; evtNode = evtNode.next_sibling("Event"))
                        {
                            std::string evtName = parameters.ReadAttribute(evtNode, "name");
                            scenarioengine::Event* evtObj = nullptr;
                            for(auto* e : mObj->event_) if(e->GetName() == evtName) { evtObj = e; break; }
                            if(!evtObj) continue;

                            // Scan actions in Event
                            for (pugi::xml_node actionNode = evtNode.child("Action"); actionNode; actionNode = actionNode.next_sibling("Action"))
                            {
                                pugi::xml_node privNode = actionNode.child("PrivateAction");
                                if (!privNode.empty())
                                {
                                    pugi::xml_node appNode = privNode.child("AppearanceAction");
                                    if (!appNode.empty())
                                    {
                                        // Found one!
                                        // Create action for EACH actor
                                        for(auto* actor : mgObj->actor_)
                                        {
                                            auto* action = ParseAppearanceAction(appNode, actor->object_, evtObj);
                                            if (action)
                                            {
                                                evtObj->action_.push_back(action);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

}  // namespace gt_esmini
