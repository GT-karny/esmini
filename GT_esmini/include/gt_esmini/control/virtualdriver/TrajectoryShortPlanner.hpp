#pragma once

#include "gt_esmini/control/virtualdriver/IShortPlanner.hpp"

namespace gt_esmini
{

struct TrajectoryShortPlannerConfig
{
    double min_step = 0.05;  // [m] floor on the per-sample arc length (low-speed/stop)

    // [m] floor on the TOTAL preview length, independent of speed.
    //
    // The preview advances by v*dt per sample, so its total span is v*horizon_s
    // — which collapses as the vehicle stops (min_step alone left it at
    // 30 * 0.05 = 1.5 m). The driver's pure-pursuit lookahead does NOT collapse
    // with it: it is clamped at min_lookahead (4.0 m). A preview shorter than
    // the lookahead means no preview point ever satisfies the lookahead, so the
    // driver falls back to the LAST preview point and computes curvature over
    // that very short baseline. A stopped vehicle's standing cross-track error
    // (~0.33 m on a curve) then subtends a huge angle over 1.5 m, the curvature
    // 2*sin(alpha)/1.5 explodes, and the steering command saturates to full
    // lock — after passing through neutral on the way. Measured: the wheel went
    // -0.105 rad -> 0 -> +0.610 rad (full lock) as the car stopped on an R~49 m
    // arc, and stayed there.
    //
    // Keeping the preview at least this long means the lookahead is always
    // reachable, so the standstill geometry stays the same shape as the moving
    // geometry.
    //
    // Must cover the CONTROL POINT OFFSET plus the driver's min_lookahead, not
    // just the lookahead: the driver measures the lookahead from a reference
    // shifted forward to the front axle (~3 m), so the preview has to reach
    // past that shift before the lookahead is satisfiable. 10 m = ~3 m offset
    // + 4 m min_lookahead + margin. Only binds below
    // v = min_preview_span / horizon_s (3.3 m/s at the defaults), and it only
    // lengthens the preview — the samples it adds are further along the same
    // route walk, so nothing already in the preview moves.
    double min_preview_span = 10.0;
};

// Phase 1 short planner.
//
// Walks the ego's route forward (roadmanager MoveAlongS on a duplicated
// Position, so the shared route state is untouched) at equal time steps
// (ds = v * dt), overlaying any active lane-change / lane-offset displacement
// exactly as ControllerKinematic::BuildPathFromRoad does. Produces an
// equal-Δt (x,y,v,t) preview. Speed comes from the mid/long v_target profile
// when supplied, else from ctx.fallback_speed (the scenario's target speed).
class TrajectoryShortPlanner : public IShortPlanner
{
public:
    explicit TrajectoryShortPlanner(const TrajectoryShortPlannerConfig& cfg = {}) : cfg_(cfg) {}
    void Configure(const TrajectoryShortPlannerConfig& cfg) { cfg_ = cfg; }

    ShortPlannerSnapshot Plan(const ShortPlanContext& ctx) override;

private:
    // The mid/long speed ceiling at distance s_ahead (large sentinel if none).
    double SampleCeiling(const ShortPlanContext& ctx, double s_ahead) const;
    // Target speed at distance s_ahead along the route (m) = min(commanded, ceiling).
    double SampleTargetSpeed(const ShortPlanContext& ctx, double s_ahead) const;

    TrajectoryShortPlannerConfig cfg_;
};

}  // namespace gt_esmini
