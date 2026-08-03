#include "gt_esmini/control/virtualdriver/policies/StopYieldSignAware.hpp"
#include "gt_esmini/control/virtualdriver/PolicyDetail.hpp"
#include "gt_esmini/control/virtualdriver/policies/RouteSignalScan.hpp"
#include "gt_esmini/road/OdrSideModel.hpp"
#include "gt_esmini/road/OdrSideExtras.hpp"

#include "Entities.hpp"
#include "RoadManager.hpp"

#include <algorithm>
#include <string>
#include <vector>

using namespace scenarioengine;

namespace gt_esmini
{

// P4 L2 pure classifier (see StopYieldSignAware.hpp). First behavioural priority
// type in document order wins.
PriorityClass ClassifyPriorityTypes(const std::vector<std::string>& priority_types)
{
    for (const std::string& t : priority_types)
    {
        if (t == "stop" || t == "stopLine")
        {
            return PriorityClass::STOP;
        }
        if (t == "yield")
        {
            return PriorityClass::GIVE_WAY;
        }
    }
    return PriorityClass::NONE;
}

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

    // ── P4 L2: catalog-FIRST, semantics-FALLBACK sign classification ──────────
    // The country traffic-signal catalog is the tested path for every shipped
    // asset; only when it left a signal OSI-unclassified do we consult the GT side
    // model's <semantics><priority @type> (decision 1). A behavioural stop/stopLine
    // -> STOP or yield -> GIVE_WAY (decision 2) then makes the existing STOP/YIELD
    // machinery treat the sign accordingly. For any signal WITHOUT semantics (or
    // without a behavioural priority type) this resolves to the raw catalog OSI
    // type, so the path is bit-identical to the pre-P4 behaviour. Speed/lane
    // semantics carry NO behaviour in P4 (accessor + L3 OSI only).
    auto effective_osi = [&](roadmanager::Signal* sig) -> int {
        const int osi = sig->GetOSIType();
        if (osi == roadmanager::Signal::TYPE_STOP || osi == roadmanager::Signal::TYPE_GIVE_WAY)
        {
            return osi;  // catalog already classified it -> semantics never consulted
        }
        // Unclassified by the catalog: resolve (and memoize) the semantics fallback.
        const void* sig_key = static_cast<const void*>(sig);
        auto        it      = semantic_class_cache_.find(sig_key);
        PriorityClass pc;
        if (it != semantic_class_cache_.end())
        {
            pc = it->second;
        }
        else
        {
            pc = PriorityClass::NONE;
            const gt_esmini::odr::OdrSignalExtras* ex =
                gt_esmini::odr::GetSignalExtras(roadmanager::Position::GetOpenDrive(), sig);
            if (ex != nullptr && ex->has_semantics)
            {
                pc = ClassifyPriorityTypes(ex->semantics.priority_types);
            }
            semantic_class_cache_[sig_key] = pc;
        }
        if (pc == PriorityClass::STOP)
        {
            return roadmanager::Signal::TYPE_STOP;
        }
        if (pc == PriorityClass::GIVE_WAY)
        {
            return roadmanager::Signal::TYPE_GIVE_WAY;
        }
        return osi;  // no behavioural semantics -> unchanged
    };

    // Diagnostics (W3 pattern) for the NEAREST governing priority sign only —
    // the scan is distance-ordered, so the first stop/yield sign is the one
    // whose state answers "why did/didn't the ego stop here". Tokens:
    // yield / approach / hold / creep / cleared ("cleared" is the negative case
    // that used to be undiagnosable: sign visible, dwell already served).
    auto phase_token = [](stop_fsm::Phase p) -> const char* {
        switch (p)
        {
            case stop_fsm::Phase::APPROACH: return "approach";
            case stop_fsm::Phase::HOLD:     return "hold";
            case stop_fsm::Phase::CREEP:    return "creep";
            default:                        return "cleared";
        }
    };
    bool detail_done = false;

    std::vector<int> visible_stop_ids;
    for (size_t i = 0; i < signals.size(); ++i)
    {
        const ScannedSignal& s   = signals[i];
        const int            osi = effective_osi(s.signal);
        const int            id  = s.signal->GetId();

        if (osi == roadmanager::Signal::TYPE_GIVE_WAY)
        {
            if (!detail_done)
            {
                detail_done = true;
                AddDetail(snap.detail, "gt.stop_yield.sign_id", id);
                AddDetail(snap.detail, "gt.stop_yield.dist_m", s.distance_ahead);
                AddDetail(snap.detail, "gt.stop_yield.phase", "yield");
                // Stop-line pairing never applies to YIELD (design §7): it only
                // decelerates to the sign itself, so there is no stop point to swap.
                AddDetail(snap.detail, "gt.stop_yield.stop_line_used", false);
            }
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
            stop_fsm::State& st         = stop_states_[id];
            const bool       first_sign = !detail_done;

            // Stop-line pairing, per STOP sign. A miss (kill switch off, or no
            // paired entry in window) leaves dist_ahead == s.distance_ahead, so
            // dist_adj below is unchanged from the pre-pairing code.
            double dist_ahead     = s.distance_ahead;
            bool   stop_line_used = false;
            if (cfg_.stop_line_aware_enabled)
            {
                const auto paired = FindPairedStopLine(signals, i, cfg_.stop_line_window);
                if (paired)
                {
                    dist_ahead     = signals[*paired].distance_ahead;
                    stop_line_used = true;
                }
            }

            if (first_sign)
            {
                detail_done = true;
                AddDetail(snap.detail, "gt.stop_yield.sign_id", id);
                AddDetail(snap.detail, "gt.stop_yield.dist_m", s.distance_ahead);
                AddDetail(snap.detail, "gt.stop_yield.stop_line_used", stop_line_used);
            }
            if (st.phase == stop_fsm::Phase::CLEARED)
            {
                if (first_sign) AddDetail(snap.detail, "gt.stop_yield.phase", "cleared");
                continue;  // already done
            }

            // Target a point stop_margin before the sign (or the paired stop line)
            // so the front halts at the line and the sign stays in scan (FSM sees
            // adjusted dist ~ 0 when stopped).
            const double dist_adj = std::max(0.0, dist_ahead - cfg_.stop_margin);
            PolicyConstraint c = stop_fsm::Update(st, dist_adj, v_ego, now, cfg_.stop);
            if (first_sign) AddDetail(snap.detail, "gt.stop_yield.phase", phase_token(st.phase));
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
