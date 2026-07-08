#pragma once

#include "gt_esmini/control/virtualdriver/VirtualDriverTypes.hpp"

namespace scenarioengine
{
class Object;
}

namespace gt_esmini
{

// Per-frame inputs for the mid/long planner (Phase 2+).
struct MidLongContext
{
    scenarioengine::Object* object    = nullptr;  // ego; route in object->pos_
    double                  sim_time  = 0.0;
    double                  scan_dist = 200.0;      // how far ahead to scan [m]
    // Constraints from traffic policies (Phase 3); null/empty in Phase 2.
    const TrafficPolicySnapshot* policy = nullptr;
};

// Mid/long-horizon speed planner.
//
// Produces a v_target(s) profile by scanning the route ahead (curvature →
// lateral-accel limit, junction turn speeds, speed-limit changes, grade) and
// folding in policy constraints. Phase 2 deliverable; the interface is declared
// in Phase 0 so the contract is locked and downstream (IShortPlanner) can read
// v_target without later rework.
class IMidLongPlanner
{
public:
    virtual ~IMidLongPlanner() = default;
    virtual MidLongPlannerSnapshot Plan(const MidLongContext& ctx) = 0;
};

}  // namespace gt_esmini
