/*
 * GT_esmini - Extended esmini with HostVehicleData Functionality
 * https://github.com/esmini/esmini
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "gt_esmini/control/manualdrive/AutoTransmission.hpp"

#include <algorithm>
#include <cmath>

namespace gt_esmini
{

void AutoTransmission::Reset()
{
    range_                  = Range::DRIVE;
    sched_state_            = shift_logic::ScheduleState{};
    sched_state_.current_gear = 1;
    prev_up_                = false;
    prev_down_              = false;
    both_held_timer_        = 0.0;
    both_press_consumed_    = false;
    manual_override_timer_  = 0.0;
}

void AutoTransmission::SeedFromSpeed(double speed_kmh)
{
    sched_state_.current_gear   = shift_logic::SeedGear(speed_kmh, params_.schedule);
    sched_state_.gear_hold_timer = params_.schedule.min_gear_hold_s;
}

AutoTransmission::Outputs AutoTransmission::Step(const Inputs& in, double dt)
{
    Outputs out;

    // Edge detection
    bool up_edge   = in.paddle_up_pressed   && !prev_up_;
    bool down_edge = in.paddle_down_pressed && !prev_down_;

    // -- Both-paddles-together → request NEUTRAL --
    bool both_now = in.paddle_up_pressed && in.paddle_down_pressed;
    if (both_now)
    {
        both_held_timer_ += dt;
    }
    else
    {
        both_held_timer_     = 0.0;
        both_press_consumed_ = false;
    }
    bool simul_n_request = false;
    if (both_now && !both_press_consumed_ &&
        both_held_timer_ >= params_.paddle_simul_press_threshold_s)
    {
        simul_n_request = true;
        both_press_consumed_ = true;
        // Suppress single edges that come from this combo
        up_edge = false;
        down_edge = false;
    }
    // While both are held, do not act on individual paddle edges (avoid race).
    if (both_now)
    {
        up_edge = false;
        down_edge = false;
    }

    // -- Range transitions --
    if (simul_n_request)
    {
        range_ = Range::NEUTRAL;
        manual_override_timer_ = 0.0;
    }
    else if (range_ == Range::NEUTRAL)
    {
        if (up_edge)
        {
            // N → D (gear 1)
            range_ = Range::DRIVE;
            sched_state_.current_gear = 1;
            sched_state_.gear_hold_timer = params_.schedule.min_gear_hold_s;
            manual_override_timer_ = 0.0;
        }
        else if (down_edge)
        {
            // N → R
            range_ = Range::REVERSE;
            manual_override_timer_ = 0.0;
        }
    }
    else if (range_ == Range::REVERSE)
    {
        if (up_edge)
        {
            // R → N
            range_ = Range::NEUTRAL;
            manual_override_timer_ = 0.0;
        }
        // down_edge in R: ignored
    }
    else // DRIVE
    {
        // -- Forward-gear selection --
        const int max_gear = std::max(1, params_.schedule.max_gear);
        double speed_kmh = std::fabs(in.speed_mps) * 3.6;

        if (up_edge)
        {
            int g = std::clamp(sched_state_.current_gear + 1, 1, max_gear);
            if (g != sched_state_.current_gear)
            {
                sched_state_.current_gear   = g;
                sched_state_.gear_hold_timer = params_.schedule.min_gear_hold_s;
                sched_state_.last_shift_dir = +1;
                out.shifted_up = true;
            }
            manual_override_timer_ = params_.manual_override_timeout_s;
        }
        else if (down_edge)
        {
            int g = std::clamp(sched_state_.current_gear - 1, 1, max_gear);
            if (g != sched_state_.current_gear)
            {
                sched_state_.current_gear   = g;
                sched_state_.gear_hold_timer = params_.schedule.min_gear_hold_s;
                sched_state_.last_shift_dir = -1;
                out.shifted_down = true;
            }
            manual_override_timer_ = params_.manual_override_timeout_s;
        }
        else if (manual_override_timer_ > 0.0)
        {
            // Tiptronic countdown: don't decrement while driver is actively
            // managing the car via brake or while crawling at low speed
            // (matches Porsche PDK / Audi Tiptronic behaviour).
            bool actively_managing =
                (in.brake > 0.05) ||
                (speed_kmh < params_.low_speed_kmh_for_revert);
            if (!actively_managing)
            {
                manual_override_timer_ = std::max(0.0, manual_override_timer_ - dt);
            }
            // Still tick the gear hold timer down so it doesn't get stuck
            sched_state_.gear_hold_timer =
                std::max(0.0, sched_state_.gear_hold_timer - dt);
        }
        else
        {
            // Pure auto schedule
            int prev_g = sched_state_.current_gear;
            int new_g  = shift_logic::StepSchedule(
                speed_kmh, in.throttle, in.brake,
                params_.schedule, sched_state_, dt);
            if (new_g > prev_g) out.shifted_up   = true;
            else if (new_g < prev_g) out.shifted_down = true;
        }
    }

    // -- Build outputs --
    out.range        = range_;
    out.forward_gear = sched_state_.current_gear;
    out.manual_mode  = (range_ == Range::DRIVE) && (manual_override_timer_ > 0.0);
    switch (range_)
    {
        case Range::REVERSE: out.gear_for_drivetrain = -1; break;
        case Range::NEUTRAL: out.gear_for_drivetrain =  0; break;
        case Range::DRIVE:   out.gear_for_drivetrain = sched_state_.current_gear; break;
    }

    prev_up_   = in.paddle_up_pressed;
    prev_down_ = in.paddle_down_pressed;

    return out;
}

} // namespace gt_esmini
