#pragma once

#include <memory>
#include <vector>

#include "gt_esmini/control/virtualdriver/ITrafficPolicy.hpp"

namespace gt_esmini
{

// Bundles the Phase-3 traffic policies (lead-vehicle, traffic-light, stop/yield
// sign) behind one Evaluate() call. Each policy independently emits a set of
// PolicyConstraints for the current frame; the manager concatenates them into a
// single TrafficPolicySnapshot which the controller hands to the mid/long
// planner (MidLongContext::policy). The planner folds every constraint into the
// v_target(s) ceiling via min() (STOP -> 0), so the strictest constraint wins
// automatically when several policies are active at once.
//
// On/off is handled by the owner: only the enabled policies are Add()-ed, so the
// manager itself stays branch-free.
class TrafficPolicyManager
{
public:
    void Add(std::unique_ptr<ITrafficPolicy> policy)
    {
        if (policy) policies_.push_back(std::move(policy));
    }

    size_t PolicyCount() const { return policies_.size(); }

    // Evaluate every registered policy and concatenate their constraints.
    TrafficPolicySnapshot Evaluate(const TrafficPolicyContext& ctx)
    {
        TrafficPolicySnapshot snap;
        for (auto& p : policies_)
        {
            TrafficPolicySnapshot s = p->Evaluate(ctx);
            for (auto& c : s.constraints)
                snap.constraints.push_back(c);
            // Diagnostics (W3) concatenate too. Keys are namespaced per policy
            // (gt.<policy>.*), so no collision between policies.
            for (auto& kv : s.detail)
                snap.detail.push_back(kv);
        }
        snap.valid = !snap.constraints.empty();
        return snap;
    }

private:
    std::vector<std::unique_ptr<ITrafficPolicy>> policies_;
};

}  // namespace gt_esmini
