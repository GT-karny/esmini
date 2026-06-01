#pragma once

#include "gt_esmini/control/virtualdriver/VirtualDriverTypes.hpp"

namespace scenarioengine
{
class Object;
}

namespace gt_esmini
{

// Per-frame inputs for the short planner.
struct ShortPlanContext
{
    scenarioengine::Object* object    = nullptr;  // ego; route lives in object->pos_
    double                  sim_time  = 0.0;
    double                  horizon_s = 3.0;        // seconds to look ahead
    double                  dt        = 0.1;        // sampling step [s]
    // Speed boundary from the mid/long planner (Phase 2). When null (Phase 1),
    // the planner falls back to fallback_speed (the ego's commanded target).
    const MidLongPlannerSnapshot* v_target = nullptr;
    double                        fallback_speed = 0.0;  // [m/s], used when v_target == nullptr
};

// Short-horizon trajectory planner.
//
// Produces an equal-Δt (x,y,v,t) preview that the IDriverModel tracks. The
// geometry (x,y) is read by walking the route ahead; the speed profile (v) is
// taken from v_target when available, else fallback_speed. Time-domain sampling
// is intentional: long-horizon speed shaping (e.g. slowing for a turn) is the
// job of IMidLongPlanner, which works in the route s domain.
class IShortPlanner
{
public:
    virtual ~IShortPlanner() = default;
    virtual ShortPlannerSnapshot Plan(const ShortPlanContext& ctx) = 0;
};

}  // namespace gt_esmini
