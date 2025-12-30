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
#include "ExtraAction.hpp"     // GT_esmini extension

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
         * @brief Parse OSC Private Action (overrides parent class method via name hiding)
         * 
         * Checks for AppearanceAction and delegates to ParseAppearanceAction.
         * All other actions are delegated to parent class implementation.
         * 
         * Phase 2: Implement to support AppearanceAction
         * 
         * @param actionNode PrivateAction XML node
         * @param object Target object
         * @param parent Parent event
         * @return Parsed OSCPrivateAction
         */
        scenarioengine::OSCPrivateAction* parseOSCPrivateAction(pugi::xml_node           actionNode,
                                                                scenarioengine::Object*  object,
                                                                scenarioengine::Event*   parent);

    protected:
        /**
         * @brief Parse LightStateAction
         * 
         * Phase 1: Stub implementation
         * Phase 2: Implement actual parsing logic
         * 
         * @param node LightStateAction XML node
         * @return Parsed OSCLightStateAction
         */
        OSCLightStateAction* ParseLightStateAction(pugi::xml_node node);

        /**
         * @brief Parse AppearanceAction (including LightStateAction)
         * 
         * Phase 1: Stub implementation
         * Phase 2: Implement actual parsing logic
         * 
         * @param node AppearanceAction XML node
         * @param object Target object
         * @param parent Parent event
         * @return Parsed OSCPrivateAction
         */
        scenarioengine::OSCPrivateAction* ParseAppearanceAction(pugi::xml_node           node,
                                                                scenarioengine::Object*  object,
                                                                scenarioengine::Event*   parent);
    };

}  // namespace gt_esmini
