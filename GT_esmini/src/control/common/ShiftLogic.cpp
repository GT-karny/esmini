/*
 * GT_esmini - Extended esmini with HostVehicleData Functionality
 * https://github.com/esmini/esmini
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "gt_esmini/control/common/ShiftLogic.hpp"

#include <algorithm>

namespace gt_esmini
{
namespace shift_logic
{

int StepSchedule(double speed_kmh, double throttle, double brake,
                 const ScheduleParams& p, ScheduleState& s, double dt)
{
    s.last_shift_dir = 0;
    int max_gear = std::max(1, p.max_gear);
    int g = std::clamp(s.current_gear, 1, max_gear);

    s.gear_hold_timer = std::max(0.0, s.gear_hold_timer - dt);
    if (s.gear_hold_timer > 0.0)
    {
        s.current_gear = g;
        return g;
    }

    const auto& up = p.shift_up_kmh;
    const auto& dn = p.shift_down_kmh;
    int n_up = static_cast<int>(up.size());
    int n_dn = static_cast<int>(dn.size());

    double kickdown = 1.0 + throttle * p.kickdown_gain;

    // Upshift
    if (g <= n_up && g < max_gear)
    {
        double thr = up[g - 1] * kickdown;
        if (speed_kmh > thr)
        {
            s.current_gear     = g + 1;
            s.gear_hold_timer  = p.min_gear_hold_s;
            s.last_shift_dir   = +1;
            return s.current_gear;
        }
    }

    // Downshift (g >= 2)
    if (g >= 2 && g - 1 <= n_dn)
    {
        double base = dn[g - 2] * kickdown;
        if (brake > p.brake_downshift_threshold)
        {
            base *= 1.20;  // brake-induced earlier downshift
        }
        if (speed_kmh < base)
        {
            s.current_gear     = g - 1;
            s.gear_hold_timer  = p.min_gear_hold_s;
            s.last_shift_dir   = -1;
            return s.current_gear;
        }
    }

    s.current_gear = g;
    return g;
}

int SeedGear(double speed_kmh, const ScheduleParams& p)
{
    int gear = 1;
    const auto& up = p.shift_up_kmh;
    const auto& dn = p.shift_down_kmh;
    int n_thresh = static_cast<int>(std::min(up.size(), dn.size()));
    for (int g = 0; g < n_thresh; ++g)
    {
        double mid = 0.5 * (up[g] + dn[g]);
        if (speed_kmh >= mid)
        {
            gear = g + 2;
        }
    }
    return std::clamp(gear, 1, std::max(1, p.max_gear));
}

} // namespace shift_logic
} // namespace gt_esmini
