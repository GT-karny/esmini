/*
 * GT_esmini - Extended esmini with HostVehicleData Functionality
 * https://github.com/esmini/esmini
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <vector>

namespace gt_esmini
{
namespace shift_logic
{

struct ScheduleParams
{
    std::vector<double> shift_up_kmh   = {15, 30, 50, 75, 100};
    std::vector<double> shift_down_kmh = {10, 22, 40, 60,  85};
    double kickdown_gain             = 0.35;
    double brake_downshift_threshold = 0.4;
    double min_gear_hold_s           = 0.5;
    int    max_gear                  = 6;
};

struct ScheduleState
{
    int    current_gear     = 1;   // 1..max_gear
    double gear_hold_timer  = 0.0;
    int    last_shift_dir   = 0;   // +1 = upshift this step, -1 = downshift, 0 = none
};

// Pure function: advance the auto-shift schedule one step.
// Returns the new gear and writes last_shift_dir into state.
int StepSchedule(double speed_kmh, double throttle, double brake,
                 const ScheduleParams& p, ScheduleState& s, double dt);

// Seed gear for mid-cruise scenario starts based on current speed.
int SeedGear(double speed_kmh, const ScheduleParams& p);

} // namespace shift_logic
} // namespace gt_esmini
