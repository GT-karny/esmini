#pragma once

#include "gt_esmini/control/virtualdriver/IShortPlanner.hpp"

namespace gt_esmini
{

struct TrajectoryShortPlannerConfig
{
    double min_step = 0.05;  // [m] floor on the per-sample arc length (low-speed/stop)
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
    // Target speed at distance s_ahead along the route (m). Uses v_target when
    // valid, else the constant fallback.
    double SampleTargetSpeed(const ShortPlanContext& ctx, double s_ahead) const;

    TrajectoryShortPlannerConfig cfg_;
};

}  // namespace gt_esmini
