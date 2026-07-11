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

#include "gt_esmini/control/HeadlightLogic.hpp"

#include <cctype>

namespace gt_esmini
{
namespace headlight
{
    NightState DecideNight(const EnvSnapshot& env, const HeadlightConfig& cfg)
    {
        // 1. Measured illuminance is the most direct signal.
        if (env.has_illuminance)
        {
            return env.illuminance_lux < cfg.illuminance_lux_threshold ? NightState::NIGHT : NightState::DAY;
        }

        // 2. Sun elevation: at/below the threshold (horizon by default) => night.
        if (env.has_sun_elevation)
        {
            return env.sun_elevation_rad <= cfg.sun_elevation_threshold_rad ? NightState::NIGHT : NightState::DAY;
        }

        // 3. TimeOfDay hour fallback.
        if (cfg.use_time_of_day && env.has_time_of_day)
        {
            int hour = 0;
            if (ParseHourFromDateTime(env.date_time, hour))
            {
                return IsHourNight(hour, cfg.dawn_hour, cfg.dusk_hour) ? NightState::NIGHT : NightState::DAY;
            }
        }

        // 4. Nothing usable -> undecided (caller keeps the legacy "lights off").
        return NightState::UNKNOWN;
    }

    bool ParseHourFromDateTime(const std::string& date_time, int& hour_out)
    {
        // Expect ISO8601 "...THH:MM:SS...". Find the date/time separator 'T' and
        // read the two hour digits after it.
        const std::size_t t_pos = date_time.find('T');
        if (t_pos == std::string::npos || t_pos + 2 >= date_time.size())
        {
            return false;
        }
        const char h0 = date_time[t_pos + 1];
        const char h1 = date_time[t_pos + 2];
        if (!std::isdigit(static_cast<unsigned char>(h0)) || !std::isdigit(static_cast<unsigned char>(h1)))
        {
            return false;
        }
        const int hour = (h0 - '0') * 10 + (h1 - '0');
        if (hour < 0 || hour > 23)
        {
            return false;
        }
        hour_out = hour;
        return true;
    }

    bool IsHourNight(int hour, double dawn_hour, double dusk_hour)
    {
        // Night if before dawn OR at/after dusk. Works for the usual dawn < dusk
        // configuration (e.g. 6..19): daytime is [dawn, dusk).
        return (static_cast<double>(hour) < dawn_hour) || (static_cast<double>(hour) >= dusk_hour);
    }

    bool PointInTunnel(double s, double start_s, double length)
    {
        return s >= start_s && s <= start_s + length;
    }

    double ForwardDistanceInCorridor(double dx, double dy, double cos_h, double sin_h, double half_width)
    {
        // Rotate the world-frame delta into the ego heading frame.
        const double forward = dx * cos_h + dy * sin_h;   // +ahead
        const double lateral = -dx * sin_h + dy * cos_h;  // +left
        if (forward <= 0.0)
        {
            return -1.0;  // behind (or exactly beside) the ego
        }
        if (lateral < -half_width || lateral > half_width)
        {
            return -1.0;  // outside the lateral corridor
        }
        return forward;
    }

    bool HighBeamHysteresis::Update(double nearest_dist, double dt, const HeadlightConfig& cfg)
    {
        const bool has_target = nearest_dist >= 0.0;

        // Distance Schmitt trigger: a vehicle within `range` becomes "present";
        // it only clears once it is beyond `range + hysteresis`. In between, the
        // previous present/clear latch is held (prevents boundary flicker).
        if (has_target && nearest_dist <= cfg.highbeam_range_m)
        {
            present_ = true;
        }
        else if (!has_target || nearest_dist > cfg.highbeam_range_m + cfg.highbeam_range_hysteresis_m)
        {
            present_ = false;
        }
        // else: keep previous present_

        // Dwell timers: require the condition to persist before switching output.
        if (present_)
        {
            dim_timer_ += dt;
            clear_timer_ = 0.0;
            if (dim_timer_ >= cfg.highbeam_off_delay_s)
            {
                high_ = false;
            }
        }
        else
        {
            clear_timer_ += dt;
            dim_timer_ = 0.0;
            if (clear_timer_ >= cfg.highbeam_on_delay_s)
            {
                high_ = true;
            }
        }

        return high_;
    }

}  // namespace headlight
}  // namespace gt_esmini
