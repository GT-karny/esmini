#include "gt_esmini/control/virtualdriver/policies/StopYieldSignAware.hpp"
#include "gt_esmini/control/virtualdriver/policies/RouteSignalScan.hpp"

#include "Entities.hpp"
#include "RoadManager.hpp"

#include <algorithm>
#include <vector>

using namespace scenarioengine;

namespace gt_esmini
{

namespace stop_fsm
{
PolicyConstraint Update(State& st, double dist, double v_ego, double now, const Params& p)
{
    PolicyConstraint c;  // defaults to Kind::NONE

    if (st.phase == Phase::APPROACH)
    {
        if (v_ego < p.stop_detect_speed && dist < p.stop_line_tol)
        {
            st.phase         = Phase::HOLD;
            st.phase_start_t = now;
        }
        c.kind = PolicyConstraint::Kind::STOP_AT_S;
        c.s    = std::max(0.0, dist);
        return c;
    }

    if (st.phase == Phase::HOLD)
    {
        if (now - st.phase_start_t < p.stop_hold_time)
        {
            c.kind = PolicyConstraint::Kind::STOP_AT_S;
            c.s    = std::max(0.0, dist);
            return c;
        }
        st.phase         = Phase::CREEP;  // dwell elapsed -> start creeping
        st.phase_start_t = now;
    }

    if (st.phase == Phase::CREEP)
    {
        const double creep_time = (p.creep_speed > 1.0e-3) ? (p.creep_advance / p.creep_speed) : 1.0;
        if (now - st.phase_start_t >= creep_time)
        {
            st.phase = Phase::CLEARED;
            return c;  // NONE
        }
        c.kind  = PolicyConstraint::Kind::MAX_SPEED_TO_S;
        c.s     = std::max(0.0, dist) + p.creep_advance;
        c.value = p.creep_speed;
        return c;
    }

    return c;  // CLEARED -> NONE
}
}  // namespace stop_fsm

TrafficPolicySnapshot StopYieldSignAware::Evaluate(const TrafficPolicyContext& ctx)
{
    TrafficPolicySnapshot snap;
    if (!ctx.ego) return snap;

    const double v_ego = ctx.ego->GetSpeed();
    const double now   = ctx.sim_time;

    std::vector<ScannedSignal> signals = ScanSignalsAhead(ctx.ego, cfg_.lookahead);

    std::vector<int> visible_stop_ids;
    for (const auto& s : signals)
    {
        const int osi = s.signal->GetOSIType();
        const int id  = s.signal->GetId();

        if (osi == roadmanager::Signal::TYPE_GIVE_WAY)
        {
            // YIELD: decelerate to a creep up to the sign. No stop (deferred to 3d).
            PolicyConstraint c;
            c.kind   = PolicyConstraint::Kind::MAX_SPEED_TO_S;
            c.s      = s.distance_ahead;
            c.value  = cfg_.yield_creep_speed;
            c.source = "yield_sign";
            snap.constraints.push_back(c);
        }
        else if (osi == roadmanager::Signal::TYPE_STOP)
        {
            visible_stop_ids.push_back(id);
            stop_fsm::State& st = stop_states_[id];
            if (st.phase == stop_fsm::Phase::CLEARED) continue;  // already done

            PolicyConstraint c = stop_fsm::Update(st, s.distance_ahead, v_ego, now, cfg_.stop);
            if (c.kind != PolicyConstraint::Kind::NONE)
            {
                c.source = "stop_sign";
                snap.constraints.push_back(c);
            }
        }
    }

    // A STOP sign mid-dwell/creep that dropped out of view (ego crept past) is
    // considered cleared so it never re-triggers if the route loops back.
    for (auto& kv : stop_states_)
    {
        const bool visible = std::find(visible_stop_ids.begin(), visible_stop_ids.end(), kv.first) != visible_stop_ids.end();
        if (!visible && kv.second.phase != stop_fsm::Phase::APPROACH)
            kv.second.phase = stop_fsm::Phase::CLEARED;
    }

    snap.valid = !snap.constraints.empty();
    return snap;
}

}  // namespace gt_esmini
