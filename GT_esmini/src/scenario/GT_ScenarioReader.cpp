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
#include "gt_esmini/scenario/GT_ScenarioReader.hpp"
#include "gt_esmini/scenario/ExtraEntities.hpp"
#include "gt_esmini/scenario/VehicleLightBridge.hpp"
#include "gt_esmini/scenario/TrafficSignalController.hpp"
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

    void GT_ScenarioReader::ParseExtensionActions(const pugi::xml_document& doc, scenarioengine::StoryBoard& storyBoard)
    {
        pugi::xml_node oscNode = doc.child("OpenSCENARIO");
        pugi::xml_node sbNode = oscNode.child("Storyboard");

        if (sbNode.empty())
        {
            // No storyboard, but still register any scenario light ownership (none here).
            return;
        }

        // 0. Parse TrafficSignalController definitions from RoadNetwork
        ParseTrafficSignalControllers(doc);

        // 1. Parse Init actions
        // R5-U3: Init LightStateAction/AppearanceAction are handled natively by SE_Init.
        // We only handle GT-specific Init GlobalAction > TrafficSignalControllerAction here.
        pugi::xml_node initNode = sbNode.child("Init");
        if (!initNode.empty())
        {
            pugi::xml_node actionsNode = initNode.child("Actions");
            if (!actionsNode.empty())
            {
                // 1b. Parse Init GlobalAction > TrafficSignalControllerAction
                for (pugi::xml_node gaNode = actionsNode.child("GlobalAction"); gaNode; gaNode = gaNode.next_sibling("GlobalAction"))
                {
                    auto* action = ParseTrafficSignalControllerAction(gaNode, nullptr);
                    if (action)
                    {
                        storyBoard.init_.global_action_.push_back(action);
                        action->Start(0.0);
                    }
                }
            }
        }

        // 2. Parse Stories (TrafficSignalController actions + conditions only).
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
                                // Check for GlobalAction > TrafficSignalControllerAction
                                pugi::xml_node globalNode = actionNode.child("GlobalAction");
                                if (!globalNode.empty())
                                {
                                    auto* ctrlAction = ParseTrafficSignalControllerAction(globalNode, evtObj);
                                    if (ctrlAction)
                                    {
                                        evtObj->action_.push_back(ctrlAction);
                                    }
                                }
                            }

                            // Scan StartTrigger for TrafficSignalControllerCondition
                            pugi::xml_node startTrigger = evtNode.child("StartTrigger");
                            if (!startTrigger.empty())
                            {
                                int cgIndex = 0;
                                for (pugi::xml_node cgNode = startTrigger.child("ConditionGroup"); cgNode; cgNode = cgNode.next_sibling("ConditionGroup"))
                                {
                                    for (pugi::xml_node condNode = cgNode.child("Condition"); condNode; condNode = condNode.next_sibling("Condition"))
                                    {
                                        auto* cond = ParseTrafficSignalControllerCondition(condNode);
                                        if (cond)
                                        {
                                            // Set condition metadata
                                            cond->name_ = parameters.ReadAttribute(condNode, "name");
                                            std::string delayStr = parameters.ReadAttribute(condNode, "delay");
                                            if (!delayStr.empty()) cond->delay_ = strtod(delayStr.c_str(), nullptr);
                                            std::string edgeStr = parameters.ReadAttribute(condNode, "conditionEdge");
                                            if (edgeStr == "rising") cond->edge_ = scenarioengine::OSCCondition::ConditionEdge::RISING;
                                            else if (edgeStr == "falling") cond->edge_ = scenarioengine::OSCCondition::ConditionEdge::FALLING;
                                            else if (edgeStr == "risingOrFalling") cond->edge_ = scenarioengine::OSCCondition::ConditionEdge::RISING_OR_FALLING;
                                            else cond->edge_ = scenarioengine::OSCCondition::ConditionEdge::NONE;

                                            LOG_INFO("GT_ScenarioReader: Injecting condition '{}' (ctrl={}, phase={}, edge={}) into event '{}'",
                                                     cond->name_, cond->controllerRef_, cond->phaseName_, edgeStr, evtName);

                                            // Inject into existing ConditionGroup or create new one
                                            if (evtObj->start_trigger_ &&
                                                cgIndex < static_cast<int>(evtObj->start_trigger_->conditionGroup_.size()))
                                            {
                                                evtObj->start_trigger_->conditionGroup_[cgIndex]->condition_.push_back(cond);
                                            }
                                            else if (evtObj->start_trigger_)
                                            {
                                                auto* newCG = new scenarioengine::ConditionGroup();
                                                newCG->condition_.push_back(cond);
                                                evtObj->start_trigger_->conditionGroup_.push_back(newCG);
                                            }
                                            else
                                            {
                                                LOG_WARN("GT_ScenarioReader: Event '{}' has no start_trigger_!", evtName);
                                            }
                                        }
                                    }
                                    cgIndex++;
                                }
                            }
                        }
                    }
                }
            }
        }

        // R5-U3: register SCENARIO light ownership from the native storyboard. The native
        // ScenarioReader (SE_Init) already created the native LightStateActions; we scan
        // them here so GT controllers (AutoLight / ManualDrive / Virtual / RealDriver) defer
        // to scenario-owned lights via IsScenarioControlled(). Latch is evaluated lazily.
        ScenarioLightRegistry::Instance().RegisterFromStoryboard(storyBoard);
    }

    // =========================================================================
    // TrafficSignalController parsing helpers
    // =========================================================================

    void GT_ScenarioReader::ParseTrafficSignalControllers(const pugi::xml_document& doc)
    {
        pugi::xml_node oscNode   = doc.child("OpenSCENARIO");
        pugi::xml_node roadNet   = oscNode.child("RoadNetwork");
        pugi::xml_node tSignals  = roadNet.child("TrafficSignals");

        if (tSignals.empty()) return;

        for (pugi::xml_node ctrlNode = tSignals.child("TrafficSignalController"); ctrlNode;
             ctrlNode = ctrlNode.next_sibling("TrafficSignalController"))
        {
            std::string name     = parameters.ReadAttribute(ctrlNode, "name");
            std::string delayStr = parameters.ReadAttribute(ctrlNode, "delay");
            double delay         = delayStr.empty() ? 0.0 : strtod(delayStr.c_str(), nullptr);

            OSCTrafficSignalController controller(name, delay);

            std::string ref = parameters.ReadAttribute(ctrlNode, "reference");
            if (!ref.empty())
                controller.SetReference(ref);

            for (pugi::xml_node phaseNode = ctrlNode.child("Phase"); phaseNode;
                 phaseNode = phaseNode.next_sibling("Phase"))
            {
                TrafficSignalPhase phase;
                phase.name     = parameters.ReadAttribute(phaseNode, "name");
                std::string durStr = parameters.ReadAttribute(phaseNode, "duration");
                phase.duration = durStr.empty() ? 0.0 : strtod(durStr.c_str(), nullptr);

                for (pugi::xml_node stateNode = phaseNode.child("TrafficSignalState"); stateNode;
                     stateNode = stateNode.next_sibling("TrafficSignalState"))
                {
                    TrafficSignalPhaseState ss;
                    std::string idStr = parameters.ReadAttribute(stateNode, "trafficSignalId");
                    ss.signalId       = idStr.empty() ? -1 : std::stoi(idStr);
                    ss.state          = parameters.ReadAttribute(stateNode, "state");
                    phase.signalStates.push_back(ss);
                }

                controller.AddPhase(phase);
            }

            TrafficSignalControllerManager::Instance().AddController(std::move(controller));
        }
    }

    GT_TrafficSignalControllerAction* GT_ScenarioReader::ParseTrafficSignalControllerAction(
        pugi::xml_node globalActionNode,
        scenarioengine::StoryBoardElement* parent)
    {
        pugi::xml_node infraNode = globalActionNode.child("InfrastructureAction");
        if (infraNode.empty()) return nullptr;

        pugi::xml_node tsaNode = infraNode.child("TrafficSignalAction");
        if (tsaNode.empty()) return nullptr;

        pugi::xml_node ctrlActionNode = tsaNode.child("TrafficSignalControllerAction");
        if (ctrlActionNode.empty()) return nullptr;

        auto* action = new GT_TrafficSignalControllerAction(parent);
        action->controllerRef_ = parameters.ReadAttribute(ctrlActionNode, "trafficSignalControllerRef");
        action->phaseName_     = parameters.ReadAttribute(ctrlActionNode, "phase");

        return action;
    }

    TrigByTrafficSignalController* GT_ScenarioReader::ParseTrafficSignalControllerCondition(pugi::xml_node conditionNode)
    {
        pugi::xml_node byValue = conditionNode.child("ByValueCondition");
        if (byValue.empty()) return nullptr;

        pugi::xml_node tscNode = byValue.child("TrafficSignalControllerCondition");
        if (tscNode.empty()) return nullptr;

        auto* cond = new TrigByTrafficSignalController();
        cond->controllerRef_ = parameters.ReadAttribute(tscNode, "trafficSignalControllerRef");
        cond->phaseName_     = parameters.ReadAttribute(tscNode, "phase");

        return cond;
    }

}  // namespace gt_esmini
