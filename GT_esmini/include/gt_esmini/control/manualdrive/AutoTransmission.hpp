/*
 * GT_esmini - Extended esmini with HostVehicleData Functionality
 * https://github.com/esmini/esmini
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#pragma once

#include "gt_esmini/control/common/ShiftLogic.hpp"

namespace gt_esmini
{

/**
 * @brief Forward-direction automatic transmission with paddle override.
 *
 * Models a 6AT with tiptronic-style paddle behavior:
 *   - In D (range_ == DRIVE), an automatic shift schedule selects gears.
 *   - Paddle activity (up/down) puts the AT into a temporary manual mode
 *     for `manual_override_timeout_s` seconds, after which it reverts to D.
 *   - Holding both paddles together past `paddle_simul_press_threshold_s`
 *     transitions the range to NEUTRAL. From NEUTRAL, a single ↓ moves to
 *     REVERSE and a single ↑ moves back to DRIVE (gear 1). From REVERSE, a
 *     single ↑ moves to NEUTRAL.
 *
 * The output gear convention (`gear_for_drivetrain`) is:
 *   -1 = reverse, 0 = neutral, 1..max_gear = forward gear engaged.
 */
class AutoTransmission
{
public:
    enum class Range
    {
        REVERSE = -1,
        NEUTRAL =  0,
        DRIVE   = +1,
    };

    struct Params
    {
        shift_logic::ScheduleParams schedule;
        double manual_override_timeout_s   = 10.0;
        double paddle_simul_press_threshold_s = 0.15;
        double low_speed_kmh_for_revert    = 5.0;  // below this, manual override doesn't auto-revert
    };

    struct Inputs
    {
        bool paddle_up_pressed   = false;  // current frame button state
        bool paddle_down_pressed = false;
        double throttle          = 0.0;
        double brake             = 0.0;
        double speed_mps         = 0.0;    // signed: + forward, - reverse
    };

    struct Outputs
    {
        Range range              = Range::DRIVE;
        int   forward_gear       = 1;        // current 1..max_gear (only meaningful in DRIVE)
        int   gear_for_drivetrain = 1;       // -1, 0, or 1..max_gear
        bool  shifted_up         = false;    // edge: did we just upshift this frame?
        bool  shifted_down       = false;    // edge: did we just downshift this frame?
        bool  manual_mode        = false;    // tiptronic temp manual active
    };

    AutoTransmission() = default;

    void SetParams(const Params& p) { params_ = p; }
    const Params& GetParams() const { return params_; }

    /// Reset to clean DRIVE state at gear 1.
    void Reset();

    /// Seed the automatic schedule from a starting speed (mid-cruise spawn).
    void SeedFromSpeed(double speed_kmh);

    /// Advance the transmission one step.
    Outputs Step(const Inputs& in, double dt);

    Range GetRange() const { return range_; }
    int   GetForwardGear() const { return sched_state_.current_gear; }

private:
    Params params_;
    Range  range_                  = Range::DRIVE;
    shift_logic::ScheduleState sched_state_;

    // Paddle edge detection
    bool   prev_up_                = false;
    bool   prev_down_              = false;

    // Both-paddles-held-together timer
    double both_held_timer_        = 0.0;
    bool   both_press_consumed_    = false;  // suppress further N triggers until release

    // Tiptronic auto-revert timer
    double manual_override_timer_  = 0.0;
};

} // namespace gt_esmini
