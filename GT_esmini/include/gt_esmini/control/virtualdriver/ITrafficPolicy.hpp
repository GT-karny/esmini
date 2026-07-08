#pragma once

#include "gt_esmini/control/virtualdriver/VirtualDriverTypes.hpp"

namespace scenarioengine
{
class Object;
class Entities;
}

namespace gt_esmini
{

// Per-frame inputs for a traffic policy (Phase 3+).
struct TrafficPolicyContext
{
    scenarioengine::Object*   ego      = nullptr;
    scenarioengine::Entities* entities = nullptr;  // surrounding traffic
    double                    sim_time = 0.0;
};

// Traffic policy: evaluates the current scene into a set of speed/stop
// constraints (lead-vehicle, traffic-light, stop/yield sign, conflict-point,
// junction priority). Each Phase-3 policy module implements this independently;
// IMidLongPlanner folds the constraints into v_target(s). Declared in Phase 0
// so the constraint contract is fixed early.
class ITrafficPolicy
{
public:
    virtual ~ITrafficPolicy() = default;
    virtual TrafficPolicySnapshot Evaluate(const TrafficPolicyContext& ctx) = 0;
};

}  // namespace gt_esmini
