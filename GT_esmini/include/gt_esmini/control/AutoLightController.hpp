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

#include "Entities.hpp"  // esmini
#include "gt_esmini/scenario/ExtraEntities.hpp"  // GT_esmini extension
#include "gt_esmini/control/HeadlightLogic.hpp"  // F6 environment-driven headlights (pure logic)
#include <deque>
#include <utility> // for std::pair

namespace scenarioengine
{
    class OSCEnvironment;
}

namespace gt_esmini
{
    class AutoLightController
    {
    public:
        /**
         * @brief Constructor
         * @param vehicle Target vehicle
         * @param lightExt Vehicle light extension
         */
        AutoLightController(scenarioengine::Vehicle* vehicle, VehicleLightExtension* lightExt);
        
        ~AutoLightController();
        
        /**
         * @brief Update function called every frame
         * @param dt Delta time [s]
         */
        void Update(double dt);
        
        /**
         * @brief Enable/disable AutoLight for this controller
         * @param enabled true: enable, false: disable
         */
        void Enable(bool enabled);
        
        /**
         * @brief Check if AutoLight is enabled
         * @return true if enabled
         */
        bool IsEnabled() const { return enabled_; }

        /**
         * @brief Configure the F6 environment-driven headlight rule (rule 4).
         *
         * Injected by AutoLightManager after Init: the shared config (loaded from
         * config/auto_light.json), a pointer to the live ScenarioEngine environment
         * (night detection) and the entity list (auto-high-beam forward scan). When
         * cfg.enabled is false the headlight rule is a no-op — brake/reversing/indicator
         * behaviour is untouched.
         *
         * @param cfg      Headlight configuration.
         * @param env      Live OSCEnvironment (may be null -> night undecided).
         * @param entities Entity list for the forward scan (may be null -> no high beam).
         */
        void ConfigureHeadlights(const headlight::HeadlightConfig& cfg,
                                 const scenarioengine::OSCEnvironment* env,
                                 const scenarioengine::Entities*      entities);

    private:
        scenarioengine::Vehicle* vehicle_;
        VehicleLightExtension* lightExt_;
        bool enabled_;
        
        /**
         * @brief Control brake lights based on deceleration
         */
        void UpdateBrakeLights(double dt, double currentSpeed);
        
        /**
         * @brief Control indicators based on lane changes and turns
         */
        void UpdateIndicators(double dt);
        
        /**
         * @brief Control reversing lights based on speed
         */
        void UpdateReversingLights();

        /**
         * @brief F6 rule 4 — environment-driven headlights.
         *
         * Low beam ON at night (OpenSCENARIO Environment) or inside an OpenDRIVE
         * <tunnel>; auto high beam while low beam is on and the road ahead is clear
         * (hysteresis). No-op unless ConfigureHeadlights() enabled the feature.
         */
        void UpdateHeadlights(double dt);

        /**
         * @brief Build a night-detection snapshot from the live environment. Pure glue.
         */
        headlight::EnvSnapshot BuildEnvSnapshot() const;

        /**
         * @brief True if the vehicle's current (road, s) lies inside an OpenDRIVE tunnel.
         */
        bool IsInTunnel() const;

        /**
         * @brief Nearest vehicle ahead within the high-beam scan corridor, or -1 if none.
         */
        double NearestVehicleAhead() const;

        /**
         * @brief Arbitrated, edge-triggered AutoLight write.
         *
         * Honours the light-ownership hierarchy (SCENARIO > MANUAL > AUTO): a slot that a
         * native scenario LightStateAction controls is never overwritten. Otherwise the
         * bridge is only touched when the desired mode differs from the last AUTO-written
         * mode (edge trigger) so a GT-written FLASHING slot is written once and left for
         * the blink ticker instead of being re-stamped (and un-blinked) every frame.
         *
         * @param type    Light slot to drive.
         * @param desired Mode AutoLight wants for this frame.
         * @param last    Shadow of the last AUTO-written mode for this slot (updated in place).
         */
        void ApplyAutoLight(VehicleLightType type, LightState::Mode desired, LightState::Mode& last);

        // State variables for logic
        double prevSpeed_;
        int prevLaneId_;
        // Brake Light Logic (Policy A+)
        double smoothedAcc_;
        LightState::Mode lastBrakeState_;
        double brakeLatchTimer_;

        // Edge-trigger shadows for the every-frame slots (reversing + indicators) so
        // AutoLight only writes on a real mode change and never fights the blink ticker.
        LightState::Mode lastReversingState_     = LightState::Mode::OFF;
        LightState::Mode lastLeftIndicatorState_  = LightState::Mode::OFF;
        LightState::Mode lastRightIndicatorState_ = LightState::Mode::OFF;
        
        // Speed History for Brake Event Detection (Time, Speed)
        std::deque<std::pair<double, double>> speedHistory_;

        // Indicator Logic
        enum class IndicatorState { OFF, PREPARE_LEFT, PREPARE_RIGHT, ACTIVE_LEFT, ACTIVE_RIGHT };
        IndicatorState indicatorState_;
        double indicatorTimer_;        // Counts down minimum active time

        // Predictive Turn Signal State
        double prev_t_;             // Previous lateral offset (t)
        double prepareTimerLeft_;   // Timer for PREPARE LEFT
        double prepareTimerRight_;  // Timer for PREPARE RIGHT
        double prepareOffTimer_;    // Timer for PREPARE -> OFF (Cancel)
        double centerHoldTimer_;    // Timer for detecting return to center (ACTIVE -> OFF)
        int lastJunctionId_;      // To detect junction exit event
        
        // Robustness
        double timeSinceLastUpdate_;   // For frequency limiting
        double simClock_ = 0.0;        // Accumulated sim time for the blink ticker

        // F6 environment-driven headlights (rule 4). Injected via ConfigureHeadlights.
        headlight::HeadlightConfig             headlightCfg_{};   // enabled=false by default
        const scenarioengine::OSCEnvironment*  environment_ = nullptr;
        const scenarioengine::Entities*        entities_    = nullptr;
        headlight::HighBeamHysteresis          highBeamHyst_{};
        LightState::Mode                       lastLowBeamState_  = LightState::Mode::OFF;
        LightState::Mode                       lastHighBeamState_ = LightState::Mode::OFF;

        // Thresholds
        static constexpr double BRAKE_ON_THRESHOLD = -1.2;     // m/s^2 (Hard Decel Trigger)
        static constexpr double STOP_SPEED_THRESHOLD = 0.1;    // m/s (Stop Hold)
        static constexpr double BRAKE_LATCH_TIME = 0.7;        // s (Minimum ON time)
        static constexpr double BRAKE_EVENT_WINDOW = 0.3;      // s (Delta V window)
        static constexpr double BRAKE_EVENT_DV = 0.4;          // m/s (Delta V threshold for ON)
        
        static constexpr double ACC_SMOOTHING_ALPHA = 0.1;     // EMA factor (Lower is smoother)
        
        static constexpr double MIN_INDICATOR_DURATION = 0.0; // Seconds (User Request: Immediate OFF)
        static constexpr double STEER_THRESHOLD = 0.08;      // rad check
        static constexpr double YAW_RATE_THRESHOLD = 0.05;   // rad/s check
        static constexpr double UPDATE_INTERVAL = 0.05;      // 20Hz

        // Predictive Turn Signal Constants
        static constexpr double TDOT_PREARM = 0.25;    // m/s (Lateral velocity threshold for PREPARE)
        static constexpr double T_PREARM_MIN = 0.20;   // m (Lateral offset threshold for PREPARE)
        static constexpr double T_PREARM_TIME = 0.2;   // s (Duration to confirm PREPARE)
        
        static constexpr double TDOT_CANCEL = 0.1;     // m/s (Cancel threshold)
        static constexpr double T_CANCEL_MIN = 0.1;    // m (Cancel threshold)
        static constexpr double T_CANCEL_TIME = 0.5;   // s (Duration to confirm Cancel)

        static constexpr double T_ACTIVE_MIN = 0.45;   // m (Lateral offset threshold for ACTIVE)
        // Reversal Logic Constants
        static constexpr double REVERSAL_CONFIRM_TIME = 0.12; // s (approx 2-3 frames at 20Hz)
        static constexpr double REVERSAL_TDOT = 0.30;         // m/s (Strong lateral velocity)
        static constexpr double REVERSAL_T_MIN = 0.18;        // m (Significant lateral offset)
        static constexpr double REVERSAL_MIN_ACTIVE = 0.15;   // s (Min duration after fast reversal)

        static constexpr double T_CENTER_HOLD = 0.5; // Stay in center for this long -> OFF
        static constexpr double T_CENTER_EPS = 0.1;  // Near center threshold

        // Junction Prediction Constants
        static constexpr double JUNCTION_LOOKAHEAD = 35.0;      // m
        static constexpr double JUNCTION_TURN_THRESHOLD = 0.20; // rad (~11 deg)
        static constexpr double JUNCTION_BLINK_DIST = 30.0;     // m (Start blinking if closer than this)

        // Helper to detect future turn at junction
        // Returns: 0=None, 1=Left, -1=Right
        int DetectJunctionTurn(double lookahead);

    };
} // namespace gt_esmini

