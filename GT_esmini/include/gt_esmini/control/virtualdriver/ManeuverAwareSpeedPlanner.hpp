#pragma once

#include "gt_esmini/control/virtualdriver/IMidLongPlanner.hpp"

namespace gt_esmini
{

struct ManeuverAwareSpeedPlannerConfig
{
    // Lateral-accel budget: caps speed in a curve to v = sqrt(a_lat / |kappa|).
    double max_lateral_accel = 2.0;   // [m/s^2]
    // Comfort longitudinal deceleration used by the backward pass to shape the
    // approach to a constraint (start braking early instead of late & hard).
    double comfort_decel     = 2.0;   // [m/s^2]
    // Comfort jerk limit for the spatial profile smoothing. 0 disables.
    double comfort_jerk      = 1.5;   // [m/s^3]
    // Forward scan resolution.
    double scan_step         = 2.0;   // [m] per MoveAlongS sample
    // Floor on any computed ceiling so the car keeps creeping (never planned to 0).
    double min_speed         = 2.0;   // [m/s]
    // Hard-zero band before a STOP_AT_S stop point. The sqrt comfort ramp only
    // reaches 0 exactly AT the point, so the car would crawl the final approach and
    // converge to 0 too slowly (failing to fully stop before a light turns green).
    // Commanding 0 over the last stop_band metres makes the speed PID brake fully
    // and settle at a firm standstill just short of the stop point.
    double stop_band         = 2.0;   // [m]
    // Conservative cap imposed over a detected junction connecting road, on top
    // of the curvature limit (covers gently-modelled junctions).
    double turn_speed        = 5.0;   // [m/s]
    // Fold the road speed limit into the ceiling. Off if it regresses Phase 1
    // (esmini returns a 60/120 km/h heuristic when no limit is authored).
    bool   respect_speed_limit = true;
    // Re-plan throttle (future optimization; unused in the per-frame baseline).
    double replan_distance   = 10.0;  // [m]
};

// Phase 2 mid/long-horizon speed planner.
//
// Scans the ego route ahead (curvature -> lateral-accel limit, junction turns,
// speed-limit changes) and emits a v_target(s) *ceiling* profile, shaped by a
// backward comfort-deceleration pass so braking begins well before a constraint.
//
// The profile is a pure upper bound: it does NOT know the scenario's commanded
// speed. TrajectoryShortPlanner::SampleTargetSpeed() combines it with the
// SpeedAction target via min(), so the ceiling only ever lowers the desired
// speed and never fights the Phase 1 SpeedAction latch.
//
// Profile s is RELATIVE distance ahead of the ego (0 .. scan_dist), matching the
// short planner's accumulated arc length.
class ManeuverAwareSpeedPlanner : public IMidLongPlanner
{
public:
    explicit ManeuverAwareSpeedPlanner(const ManeuverAwareSpeedPlannerConfig& cfg = {}) : cfg_(cfg) {}
    void Configure(const ManeuverAwareSpeedPlannerConfig& cfg) { cfg_ = cfg; }

    MidLongPlannerSnapshot Plan(const MidLongContext& ctx) override;

private:
    ManeuverAwareSpeedPlannerConfig cfg_;
};

}  // namespace gt_esmini
