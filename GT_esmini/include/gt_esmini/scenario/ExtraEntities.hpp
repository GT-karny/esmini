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

#include "Entities.hpp"  // Include esmini's Entities.hpp
#include "gt_esmini/scenario/ExtraAction.hpp"
#include <map>
#include <memory>

namespace gt_esmini
{
    // Source of the most recent light state write
    enum class LightSource { NONE, AUTO, MANUAL_DRIVE, SCENARIO };

    /**
     * @brief Vehicle class extension (composition pattern)
     *
     * Does not inherit from esmini's Vehicle class, adds extension features via composition.
     * This minimizes impact when esmini is updated.
     */
    class VehicleLightExtension
    {
    public:
        VehicleLightExtension(scenarioengine::Vehicle* vehicle) : vehicle_(vehicle), autoLightEnabled_(false)
        {
            // Initialize all lights to OFF state
            for (int i = 0; i < static_cast<int>(VehicleLightType::SPECIAL_PURPOSE_LIGHTS) + 1; ++i)
            {
                auto t = static_cast<VehicleLightType>(i);
                LightState state;
                state.mode = LightState::Mode::OFF;
                lightStates_[t] = state;
                manualOverrides_[t] = false;
                lightSource_[t] = LightSource::NONE;
            }
        }
        
        ~VehicleLightExtension() {}

        /**
         * @brief Set light state
         * @param type Light type
         * @param state Light state
         */
        void SetLightState(VehicleLightType type, const LightState& state)
        {
            lightStates_[type] = state;
        }

        /**
         * @brief Set per-light manual override policy.
         * @param type Light type
         * @param enabled true: manual control has priority, false: AutoLight may update
         */
        void SetManualOverride(VehicleLightType type, bool enabled)
        {
            manualOverrides_[type] = enabled;
        }

        /**
         * @brief Check if a light is currently manually overridden.
         * @param type Light type
         * @return true if manual override is enabled
         */
        bool IsManualOverride(VehicleLightType type) const
        {
            auto it = manualOverrides_.find(type);
            return it != manualOverrides_.end() ? it->second : false;
        }

        /**
         * @brief Get light state
         * @param type Light type
         * @return Light state
         */
        LightState GetLightState(VehicleLightType type) const
        {
            auto it = lightStates_.find(type);
            if (it != lightStates_.end())
            {
                return it->second;
            }
            LightState state;
            state.mode = LightState::Mode::OFF;
            return state;
        }

        void SetLightSource(VehicleLightType type, LightSource src)
        {
            lightSource_[type] = src;
        }

        LightSource GetLightSource(VehicleLightType type) const
        {
            auto it = lightSource_.find(type);
            return it != lightSource_.end() ? it->second : LightSource::NONE;
        }

        bool IsScenarioControlled(VehicleLightType type) const
        {
            return GetLightSource(type) == LightSource::SCENARIO;
        }

        /**
         * @brief Enable/disable AutoLight feature
         * @param enabled true: enabled, false: disabled
         */
        void SetAutoLight(bool enabled) { autoLightEnabled_ = enabled; }

        /**
         * @brief Check if AutoLight feature is enabled
         * @return true: enabled, false: disabled
         */
        bool IsAutoLightEnabled() const { return autoLightEnabled_; }

        // Hold light states
        std::map<VehicleLightType, LightState>   lightStates_;
        std::map<VehicleLightType, bool>          manualOverrides_;
        std::map<VehicleLightType, LightSource>   lightSource_;
        bool                                      autoLightEnabled_ = false;

    private:
        scenarioengine::Vehicle* vehicle_;  // Reference to original Vehicle object
    };

    /**
     * @brief Vehicle extension manager class (singleton)
     * 
     * Links esmini's Vehicle objects with GT_esmini extension features.
     */
    class VehicleExtensionManager
    {
    public:
        static VehicleExtensionManager& Instance();

        /**
         * @brief Get Vehicle extension
         * @param vehicle Vehicle object
         * @return Extension (nullptr if not found)
         */
        VehicleLightExtension* GetExtension(scenarioengine::Vehicle* vehicle);

        /**
         * @brief Register Vehicle extension
         * @param vehicle Vehicle object
         * @param ext Extension (ownership is transferred)
         */
        void RegisterExtension(scenarioengine::Vehicle* vehicle, VehicleLightExtension* ext);

        /**
         * @brief Clear all extensions
         */
        void Clear();

    private:
        VehicleExtensionManager()  = default;
        ~VehicleExtensionManager() = default;

        // Prevent copying
        VehicleExtensionManager(const VehicleExtensionManager&) = delete;
        VehicleExtensionManager& operator=(const VehicleExtensionManager&) = delete;

        std::map<scenarioengine::Vehicle*, std::unique_ptr<VehicleLightExtension>> extensions_;
    };

}  // namespace gt_esmini
