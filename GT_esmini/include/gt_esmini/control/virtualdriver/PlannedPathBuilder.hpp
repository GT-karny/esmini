/*
 * GT_esmini extension -- turn a short planner snapshot into the path published for the
 * OSI future_trajectory (gt_esmini/osi/GT_PlannedPathRegistry.hpp).
 *
 * Header-only and free of controller state on purpose: the two rules below are the ones
 * that decide whether the reported line ends where the vehicle ends up, and both are
 * easy to get subtly wrong, so they are unit-tested directly
 * (test/unit/osi/test_PlannedPathRegistry.cpp).
 */
#pragma once

#include "gt_esmini/control/virtualdriver/VirtualDriverTypes.hpp"
#include "gt_esmini/osi/GT_PlannedPathRegistry.hpp"

#include <cmath>

namespace gt_esmini
{

// Speed below which the plan is treated as "stopped here" [m/s].
constexpr double kPlannedStoppedSpeed = 0.05;

// Build the published path from a plan.
//
// RULE 1 -- freeze at the planned stop.
//   The preview deliberately keeps walking after the planned speed reaches 0:
//   TrajectoryShortPlannerConfig::min_preview_span holds it at >= 10 m so the driver's
//   pure-pursuit lookahead stays reachable at a standstill (without it the steering
//   saturates to full lock; see that field's own measurement). That floor is a CONTROL
//   artifact. osi3 future_trajectory answers a different question -- "where will this
//   object be at time t" -- and marching ~10 m past a red light because the steering
//   needs something to aim at is the same class of wrong as reporting a stop line the
//   vehicle will not reach. So once the plan reaches zero speed we hold the last moving
//   pose for every remaining timestamp: the consumer sees the path arrive somewhere and
//   stay there, which is what the vehicle is going to do. A plan with no motion at all
//   collapses to its first pose.
//
// RULE 2 -- report the path of the vehicle's own reference point.
//   The preview is anchored at the CONTROL POINT, shifted control_point_offset metres
//   ahead of the object origin so the driver nulls a front-axle cross-track error
//   (TrajectoryShortPlanner's cp_applied). Publishing it unshifted put the whole
//   reported path that far ahead of the vehicle -- measured 1.62 m past the stop line
//   for a vehicle standing still at a red light. Shifting each point back along its own
//   heading returns the path to the OBJECT ORIGIN frame, which is where the OSI reporter
//   then applies the same bounding-box-centre offset it applies to its own projected
//   path, so both sources land on the reference point base.position uses.
//
// RULE 3 -- stop where the vehicle will actually stop.
//   The preview's speed model is "hold the commanded speed": SampleTargetSpeed returns
//   min(commanded, mid/long ceiling), and while the ego is braking under a storyboard
//   SpeedAction the commanded value tracks the current speed instead of describing the
//   ramp. Nothing in the plan then says the vehicle is stopping, so the reported path is
//   just current_speed * horizon and RULE 1 never fires. Measured on virtual_driver_basic
//   under its StopAction: at t=13.5 the reported path was 135 m long, and it stayed at
//   speed * 10 s the whole way down to a standstill.
//
//   current_speed and accel are the MEASURED longitudinal state. Only deceleration is
//   extrapolated (an acceleration held for the whole horizon would overstate the path),
//   and the resulting v^2/2a distance freezes the path exactly like a planned stop does --
//   so a consumer sees the same "arrives here and stays" shape either way. Pass
//   current_speed < 0 (the default) to disable this rule, e.g. from tests that only
//   exercise the plan-side freeze.
inline PlannedPath BuildPlannedPath(int                         object_id,
                                   double                      stamp,
                                   const ShortPlannerSnapshot& plan,
                                   double                      control_point_offset,
                                   double                      current_speed = -1.0,
                                   double                      accel         = 0.0)
{
    PlannedPath pp;
    pp.object_id = object_id;
    pp.stamp     = stamp;
    pp.points.reserve(plan.preview.size() + plan.extension.size());

    bool             frozen    = false;
    bool             have_hold = false;
    PlannedPathPoint hold{};

    // RULE 3: distance the measured deceleration says is left before standstill.
    double stop_distance = -1.0;  // < 0 = no measured stop
    if (current_speed >= 0.0 && accel < -0.05 && current_speed > kPlannedStoppedSpeed)
    {
        stop_distance = (current_speed * current_speed) / (2.0 * -accel);
    }
    double travelled = 0.0;
    bool   have_prev = false;
    double prev_x = 0.0, prev_y = 0.0;

    auto emit = [&](const TrajectoryPoint& tp)
    {
        // RULE 2: back to the object-origin frame.
        const double ox = tp.x - control_point_offset * std::cos(tp.h);
        const double oy = tp.y - control_point_offset * std::sin(tp.h);

        if (have_prev)
        {
            travelled += std::hypot(ox - prev_x, oy - prev_y);
        }
        prev_x    = ox;
        prev_y    = oy;
        have_prev = true;

        if (stop_distance >= 0.0 && travelled > stop_distance)
        {
            frozen = true;
        }

        if (!frozen && tp.v > kPlannedStoppedSpeed)
        {
            PlannedPathPoint out{tp.t, ox, oy, tp.z, tp.h, tp.p, tp.r, tp.v};
            pp.points.push_back(out);
            hold      = out;
            have_hold = true;
            return;
        }

        // RULE 1: hold the last moving pose, keeping this sample's timestamp.
        //
        // When NOTHING has moved yet (a plan that is stopped from its very first
        // sample, e.g. a vehicle standing at a red light), the pose to hold is that
        // first sample's -- latch it here. Falling through to "use each point's own
        // position" instead would let a fully stopped plan report the preview's
        // min_preview_span floor as a path, which is the standing-vehicle half of the
        // exact defect this rule exists to remove.
        frozen = true;
        if (!have_hold)
        {
            hold      = PlannedPathPoint{tp.t, ox, oy, tp.z, tp.h, tp.p, tp.r, 0.0};
            have_hold = true;
        }
        PlannedPathPoint out = hold;
        out.t                = tp.t;
        out.v                = 0.0;
        pp.points.push_back(out);
    };

    for (const auto& tp : plan.preview)
    {
        emit(tp);
    }
    for (const auto& tp : plan.extension)
    {
        emit(tp);
    }
    return pp;
}

}  // namespace gt_esmini
