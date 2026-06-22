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
#include "gt_esmini/scenario/VehicleLightBridge.hpp"
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
     *
     * R5-U3: light MODE/emission is no longer stored here. The single source of truth is
     * the native Object::vehLghtStsList[] storage. SetLightState/GetLightState delegate to
     * the VehicleLightBridge (ApplyLight/ReadLight). This class keeps only the GT-specific
     * arbitration bookkeeping (per-light manual override + writer source) and a blink
     * ticker for GT-written FLASHING lights.
     */
    class VehicleLightExtension
    {
    public:
        VehicleLightExtension(scenarioengine::Vehicle* vehicle) : vehicle_(vehicle), autoLightEnabled_(false)
        {
            for (int i = 0; i < static_cast<int>(VehicleLightType::SPECIAL_PURPOSE_LIGHTS) + 1; ++i)
            {
                auto t = static_cast<VehicleLightType>(i);
                manualOverrides_[t] = false;
                lightSource_[t] = LightSource::NONE;
            }
        }

        ~VehicleLightExtension() {}

        /**
         * @brief Set light state (writes to the native vehLghtStsList[] via the bridge).
         * @param type Light type
         * @param state Light state
         */
        void SetLightState(VehicleLightType type, const LightState& state)
        {
            ApplyLight(static_cast<scenarioengine::Object*>(vehicle_), type, state);
            // Track blink durations for the GT blink ticker (FLASHING lights only).
            if (state.mode == LightState::Mode::FLASHING)
            {
                blinkOnDuration_  = state.flashingOnDuration;
                blinkOffDuration_ = state.flashingOffDuration;
                anyFlashing_      = true;
            }
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
         * @brief Get light state (reads the native vehLghtStsList[] via the bridge).
         * @param type Light type
         * @return Light state
         */
        LightState GetLightState(VehicleLightType type) const
        {
            return ReadLight(static_cast<const scenarioengine::Object*>(vehicle_), type);
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

        /**
         * @brief Whether the given light is owned by a scenario LightStateAction.
         *
         * Delegates to the ScenarioLightRegistry latch (true once the controlling native
         * action has started, latched permanently after). Preserves the previous semantics
         * where the GT action's Start() flipped LightSource to SCENARIO.
         */
        bool IsScenarioControlled(VehicleLightType type) const
        {
            if (lightSource_.find(type) != lightSource_.end() && lightSource_.at(type) == LightSource::SCENARIO)
            {
                return true;
            }
            return ScenarioLightRegistry::Instance().IsScenarioControlled(
                static_cast<const scenarioengine::Object*>(vehicle_), type);
        }

        /**
         * @brief Advance the GT blink ticker (FLASHING lights written by GT writers).
         *
         * Idempotent per identical simTime. Driven by controllers (sim time / dt), never
         * wall clock. Native scenario FLASHING is animated by the native action; the GT
         * ticker only affects lights whose latest GT write was FLASHING.
         */
        void Tick(double simTime, double dt)
        {
            if (!anyFlashing_)
            {
                return;
            }
            blinkTicker_.Tick(static_cast<scenarioengine::Object*>(vehicle_), simTime, dt,
                              blinkOnDuration_, blinkOffDuration_);
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

        // GT arbitration bookkeeping (NOT a light-mode store anymore).
        std::map<VehicleLightType, bool>          manualOverrides_;
        std::map<VehicleLightType, LightSource>   lightSource_;
        bool                                      autoLightEnabled_ = false;

    private:
        scenarioengine::Vehicle* vehicle_;  // Reference to original Vehicle object
        LightBlinkTicker         blinkTicker_;
        double                   blinkOnDuration_  = 0.0;
        double                   blinkOffDuration_ = 0.0;
        bool                     anyFlashing_      = false;
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
