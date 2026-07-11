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

// -----------------------------------------------------------------------------
// HeadlightLogic — F6 AutoLight environment-driven headlight decision (pure).
// -----------------------------------------------------------------------------
// This header contains ONLY dependency-free decision logic (no esmini / OSI
// headers) so it is trivially unit-testable (see test/unit/test_HeadlightLogic).
// The AutoLightController glues these functions to the live ScenarioEngine
// environment, the OpenDRIVE tunnel list and the entity forward scan.
//
//  * DecideNight()          night/day/unknown from an environment snapshot
//  * ParseHourFromDateTime()/IsHourNight()  TimeOfDay fallback
//  * PointInTunnel()        s-in-interval test for OpenDRIVE <tunnel>
//  * ForwardDistanceInCorridor()  ego-frame projection of another vehicle
//  * HighBeamHysteresis     Schmitt (distance) + dwell (time) auto-high-beam FSM

#include <string>

namespace gt_esmini
{
namespace headlight
{
    // ---- Configuration (populated from config/auto_light.json) --------------
    struct HeadlightConfig
    {
        // Master switch for the whole F6 environment-driven rule. Default OFF so
        // existing users (brake/reversing/indicator AutoLight) see no change.
        bool enabled = false;

        // --- Night (low beam) detection -----------------------------------
        // Below this measured illuminance (lux) => night. Daylight ~100000 lux.
        double illuminance_lux_threshold = 3000.0;
        // Sun elevation (radians) at/below which it is treated as night when no
        // illuminance is given. 0 = geometric horizon.
        double sun_elevation_threshold_rad = 0.0;
        // Use the TimeOfDay dateTime hour when neither illuminance nor sun set.
        bool   use_time_of_day = true;
        double dusk_hour       = 19.0;  // hour >= dusk  => night
        double dawn_hour       = 6.0;   // hour <  dawn  => night

        // --- Tunnel (low beam) --------------------------------------------
        bool tunnel_enabled = true;  // low beam ON while inside an OpenDRIVE tunnel

        // --- Auto high beam ------------------------------------------------
        bool   highbeam_enabled            = true;
        double highbeam_range_m            = 120.0;  // detect vehicles within this range
        double highbeam_range_hysteresis_m = 20.0;   // Schmitt band beyond range to re-clear
        double highbeam_corridor_half_m    = 6.0;    // ego-frame lateral half-width of the scan corridor
        double highbeam_on_delay_s         = 1.5;    // clear must persist this long to raise high beam
        double highbeam_off_delay_s        = 0.3;    // detection must persist this long to dim
    };

    // ---- Night decision -----------------------------------------------------
    enum class NightState
    {
        UNKNOWN,  // no usable environment info -> caller keeps lights off (legacy behaviour)
        DAY,
        NIGHT
    };

    // Snapshot of the scenario environment relevant to night detection. The
    // controller fills this from OSCEnvironment; the decision below is pure.
    struct EnvSnapshot
    {
        bool        has_illuminance   = false;
        double      illuminance_lux   = 0.0;
        bool        has_sun_elevation = false;
        double      sun_elevation_rad = 0.0;
        bool        has_time_of_day   = false;
        std::string date_time;  // ISO8601, e.g. "2025-06-13T21:00:00.000+00:00"
    };

    // Priority: measured illuminance > sun elevation > TimeOfDay hour > UNKNOWN.
    NightState DecideNight(const EnvSnapshot& env, const HeadlightConfig& cfg);

    // Parse the hour-of-day (0..23) out of an ISO8601 date-time. Returns false
    // if no "T<hh>" field can be read. Pure/testable.
    bool ParseHourFromDateTime(const std::string& date_time, int& hour_out);

    // Night if hour is before dawn or at/after dusk (handles the midnight wrap).
    bool IsHourNight(int hour, double dawn_hour, double dusk_hour);

    // ---- Tunnel -------------------------------------------------------------
    // True if road-s lies within [start_s, start_s + length] (inclusive).
    bool PointInTunnel(double s, double start_s, double length);

    // ---- Forward vehicle scan (auto high beam) ------------------------------
    // Project the vector (dx,dy) from ego to another object into the ego heading
    // frame. Returns the forward (longitudinal) distance if the object is AHEAD
    // (forward > 0) and within the lateral corridor |lateral| <= half_width;
    // returns a negative value otherwise. cos_h/sin_h are cos/sin of ego heading.
    double ForwardDistanceInCorridor(double dx, double dy, double cos_h, double sin_h, double half_width);

    // ---- Auto-high-beam hysteresis FSM (pure) -------------------------------
    // Combines a distance Schmitt trigger (range vs range+hysteresis) with dwell
    // timers (on_delay to raise, off_delay to dim) to stop flicker. Feed it the
    // nearest forward-in-corridor vehicle distance each step (a large/negative
    // value means "nobody ahead"). Returns whether high beam should be ON.
    class HighBeamHysteresis
    {
    public:
        // nearest_dist: distance to nearest vehicle ahead in corridor; use a
        // large sentinel (or <0) when none. Returns true => high beam ON.
        bool Update(double nearest_dist, double dt, const HeadlightConfig& cfg);

        void Reset()
        {
            high_        = false;
            present_     = false;
            clear_timer_ = 0.0;
            dim_timer_   = 0.0;
        }

        bool IsHigh() const { return high_; }

    private:
        bool   high_        = false;  // current high-beam output latch
        bool   present_     = false;  // Schmitt latch: is a blocking vehicle "present"
        double clear_timer_ = 0.0;    // seconds the road has been clear
        double dim_timer_   = 0.0;    // seconds a vehicle has been present
    };

}  // namespace headlight
}  // namespace gt_esmini
