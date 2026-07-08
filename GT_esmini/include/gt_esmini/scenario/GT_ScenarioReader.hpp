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

#pragma once

#include "ScenarioReader.hpp"  // esmini core
#include "gt_esmini/scenario/ExtraAction.hpp"      // GT_esmini extension
#include "gt_esmini/scenario/ExtraCondition.hpp"   // GT_esmini conditions

namespace gt_esmini
{
    /**
     * @brief GT_ScenarioReader class
     * 
     * Inherits from esmini's ScenarioReader and adds AppearanceAction parsing functionality.
     * 
     * Phase 1: Stub implementation
     * Phase 2: Implement AppearanceAction and LightStateAction parsing
     */
    class GT_ScenarioReader : public scenarioengine::ScenarioReader
    {
    public:
        GT_ScenarioReader(scenarioengine::Entities*       entities,
                          scenarioengine::Catalogs*       catalogs,
                          scenarioengine::OSCEnvironment* environment,
                          bool                            disable_controllers = false);

        virtual ~GT_ScenarioReader();

    public:
        /**
         * @brief Parse Extension Actions from full XML document
         *
         * Re-scans the document for actions the regular ScenarioReader does not handle in
         * the GT-specific way (TrafficSignalController definitions/actions/conditions) and
         * injects them into the Storyboard.
         *
         * R5-U3: LightStateAction/AppearanceAction are NO LONGER intercepted here. The
         * native ScenarioReader (used by SE_Init) creates the native LightStateAction with
         * full fidelity (transitions / candela / flashing / conflict handling), which writes
         * Object::vehLghtStsList[] + DirtyBit::LIGHT_STATE. After parsing, this method also
         * registers SCENARIO light ownership from the native storyboard for GT arbitration.
         *
         * @param doc Parsed XML document
         * @param storyBoard Storyboard where actions should be attached
         */
        void ParseExtensionActions(const pugi::xml_document& doc, scenarioengine::StoryBoard& storyBoard);

    protected:
        /**
         * @brief Parse TrafficSignalController definitions from RoadNetwork
         */
        void ParseTrafficSignalControllers(const pugi::xml_document& doc);

        /**
         * @brief Parse and inject TrafficSignalControllerAction from a GlobalAction node
         * @return action pointer if found, nullptr otherwise
         */
        GT_TrafficSignalControllerAction* ParseTrafficSignalControllerAction(pugi::xml_node globalActionNode,
                                                                              scenarioengine::StoryBoardElement* parent);

        /**
         * @brief Parse and inject TrafficSignalControllerCondition from a Condition node
         * @return condition pointer if found, nullptr otherwise
         */
        TrigByTrafficSignalController* ParseTrafficSignalControllerCondition(pugi::xml_node conditionNode);

    private:
        scenarioengine::Entities* entities = nullptr;
    };

}  // namespace gt_esmini
