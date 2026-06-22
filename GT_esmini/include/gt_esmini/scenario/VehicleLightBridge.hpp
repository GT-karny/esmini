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

// VehicleLightBridge (R5-U3 light storage unification)
// ----------------------------------------------------
// Single source of truth for vehicle light MODE/emission is the upstream
// `scenarioengine::Object::vehLghtStsList[]` + `Object::DirtyBit::LIGHT_STATE`.
// This bridge translates between the GT light vocabulary (gt_esmini::LightState +
// gt_esmini::VehicleLightType) and that native storage, and replicates the native
// LightStateAction emission math so GT-driven writes (AutoLight / ManualDrive /
// Real/VirtualDriver) look identical to scenario-driven ones in the OSG viewer,
// the .dat recording and OSI.
//
// ApplyLight()  : GT write -> vehLghtStsList[] (+ dirty bit on real change)
// ReadLight()   : vehLghtStsList[] -> GT LightState  (the single read helper)
// BlinkTicker   : per-object blink bookkeeping for GT-written FLASHING lights
//                 (native scenario FLASHING is driven by the native action itself).

#include "Entities.hpp"  // scenarioengine::Object / VehicleLightType / VehicleLightMode
#include "gt_esmini/scenario/ExtraAction.hpp"  // gt_esmini::LightState / VehicleLightType

#include <cstdint>
#include <map>
#include <set>
#include <vector>

namespace scenarioengine
{
    class StoryBoard;
    class LightStateAction;
}  // namespace scenarioengine

namespace gt_esmini
{
    namespace lightbridge
    {
        // Default candela used when an ON/FLASHING write has luminousIntensity <= 0.
        // Mirrors LightStateAction::DEFAULT_LUMINOUS_INTENSITY_ (6000.0).
        constexpr double kDefaultLuminousIntensity = 6000.0;

        // Mirrors LightStateAction MAX_INTENSITY_LUM / non-linear emission exponent.
        constexpr double kMaxIntensityLum = 12000.0;
        constexpr double kEmissionExponent = 0.25;

        // Total mapping: GT VehicleLightType (13) -> upstream Object::VehicleLightType.
        // Upstream additionally has TAIL_LIGHTS (no GT counterpart) which is intentionally
        // unmapped. Returns UNDEFINED only for out-of-range input (never for a valid GT type).
        scenarioengine::Object::VehicleLightType ToUpstream(VehicleLightType gt);

        // Inverse for the read path. Upstream types with no GT counterpart (TAIL_LIGHTS,
        // UNDEFINED, VEHICLE_LIGHT_SIZE) map to false/undefined.
        bool ToGt(scenarioengine::Object::VehicleLightType up, VehicleLightType& out);

        // Compile-time-style completeness self-check used by unit tests.
        // Returns the number of GT light types that map to a distinct, valid upstream slot.
        int MappedGtTypeCount();
    }  // namespace lightbridge

    /**
     * @brief Write a GT light state into the native vehLghtStsList[] storage.
     *
     * Mirrors native SetVehicleLights() entry setup (type field) and
     * SetVehicleLightState() emission math. Aggregate GT types are expanded the same
     * way the native action does: FOG_LIGHTS -> FOG_LIGHTS_FRONT + FOG_LIGHTS_REAR,
     * WARNING_LIGHTS -> INDICATOR_LEFT + INDICATOR_RIGHT (the aggregate slot keeps the
     * mode for read-back). OFF zeroes emission. Robust when maxRgb==0 (headless / model
     * without light geodes): emission stays 0 but mode is still recorded.
     *
     * Sets DirtyBit::LIGHT_STATE only when the effective stored mode/emission changed.
     *
     * @return true if the stored state changed (and the dirty bit was set).
     */
    bool ApplyLight(scenarioengine::Object* obj, VehicleLightType type, const LightState& st);

    /**
     * @brief Read a GT light state out of the native vehLghtStsList[] storage.
     *
     * THE single read helper (resolves audit CTL-8). Works for any Object, with or
     * without a VehicleLightExtension. Upstream mode UNKNOWN maps to OFF.
     */
    LightState ReadLight(const scenarioengine::Object* obj, VehicleLightType type);

    /**
     * @brief Per-object blink ticker for GT-written FLASHING lights.
     *
     * Native scenario FLASHING is animated by the native LightStateAction; the bridge
     * blink only covers lights whose latest writer is GT (AUTO / MANUAL sources). It
     * toggles emission (NOT mode) on phase edges and sets the dirty bit on the edge.
     * Driven exclusively by sim time / dt supplied by controllers (never wall clock).
     */
    class LightBlinkTicker
    {
    public:
        // Advance the blink phase by dt (idempotent per identical timestamp). Toggles the
        // emission of every light currently in FLASHING mode written by GT. simTime is used
        // only to dedupe repeated Tick() calls within one frame.
        void Tick(scenarioengine::Object* obj, double simTime, double dt,
                  double onDuration, double offDuration);

        // Reset blink bookkeeping (e.g. when the controlling light leaves FLASHING).
        void Reset() { phase_ = false; timer_ = 0.0; lastSimTime_ = -1.0; }

        bool IsOnPhase() const { return phase_; }

    private:
        bool   phase_       = false;  // true: emission-on phase
        double timer_       = 0.0;
        double lastSimTime_ = -1.0;
    };

    /**
     * @brief SCENARIO light-ownership registry (arbitration latch).
     *
     * After scenario init, RegisterFromStoryboard() scans the NATIVE storyboard for
     * native LightStateActions and records, per (Object, GT light type), the controlling
     * action(s). IsScenarioControlled() reports true once any controlling action for that
     * (object, type) has STARTED (left INIT/STANDBY) and stays latched forever after
     * (matching the previous GT SetLightSource(SCENARIO) semantics where the latch was set
     * in the action's Start()). This blocks MANUAL/AUTO writers from overriding scenario
     * lights, exactly as before.
     *
     * Aggregate scenario types are expanded to their concrete members so that, e.g., a
     * warningLights action latches INDICATOR_LEFT and INDICATOR_RIGHT.
     *
     * Singleton; Clear() is hooked wherever VehicleExtensionManager::Clear() is called.
     */
    class ScenarioLightRegistry
    {
    public:
        static ScenarioLightRegistry& Instance();

        // Scan the storyboard (Init + all Story/Act/ManeuverGroup/Maneuver/Event actions)
        // and record every native LightStateAction's (object, GT type) ownership.
        void RegisterFromStoryboard(scenarioengine::StoryBoard& storyBoard);

        // True once a scenario action for (obj, type) has started (latched permanently).
        bool IsScenarioControlled(const scenarioengine::Object* obj, VehicleLightType type);

        // True if (obj, type) is registered as scenario-owned at all (regardless of latch).
        bool IsRegistered(const scenarioengine::Object* obj, VehicleLightType type) const;

        void Clear();

    private:
        ScenarioLightRegistry() = default;

        struct Key
        {
            const scenarioengine::Object* obj;
            int                           type;
            bool operator<(const Key& o) const
            {
                return obj != o.obj ? obj < o.obj : type < o.type;
            }
        };

        // (obj,type) -> list of controlling native actions (for lazy latch evaluation).
        std::map<Key, std::vector<scenarioengine::LightStateAction*>> owners_;
        // (obj,type) keys whose latch is already permanently true.
        std::set<Key> latched_;
    };

}  // namespace gt_esmini
