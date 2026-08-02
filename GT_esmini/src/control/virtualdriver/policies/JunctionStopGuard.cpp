#include "gt_esmini/control/virtualdriver/policies/JunctionStopGuard.hpp"

#include <algorithm>

namespace gt_esmini
{

namespace
{
// The stopped ego would occupy this junction: the target is past the entry and
// not clear of the exit. See the header for why the entry test is strict.
bool StopBlocksSpan(double s_stop, const RouteJunctionSpan& span, double exit_clearance)
{
    return span.entry_ahead < s_stop && s_stop < span.exit_ahead + exit_clearance;
}

const RouteJunctionSpan* FindBlockedSpan(double s_stop, const std::vector<RouteJunctionSpan>& spans, double exit_clearance)
{
    for (const auto& span : spans)
    {
        if (StopBlocksSpan(s_stop, span, exit_clearance)) return &span;
    }
    return nullptr;
}
}  // namespace

JunctionStopResolution ResolveJunctionSafeStop(double                                s_stop_wanted,
                                               const std::vector<RouteJunctionSpan>& spans,
                                               double                                v_ego,
                                               const JunctionStopGuardParams&        p,
                                               bool                                  stop_already_committed)
{
    JunctionStopResolution res;
    res.action = JunctionStopAction::HOLD;
    res.s_stop = s_stop_wanted;

    const double exit_clearance = std::max(0.0, p.exit_clearance);

    const RouteJunctionSpan* blocked = FindBlockedSpan(s_stop_wanted, spans, exit_clearance);
    if (!blocked) return res;

    res.blocked     = true;
    res.junction_id = blocked->junction_id;

    // Already in the box: there is nowhere short of it left to stop. Suppressing
    // the constraint is what lets the ego drive out; the pull-back branch below
    // is what should have kept it from entering in the first place.
    if (blocked->ego_inside)
    {
        res.action = JunctionStopAction::SUPPRESS;
        res.s_stop = 0.0;
        return res;
    }

    const double s_pull = std::max(0.0, blocked->entry_ahead - std::max(0.0, p.stop_margin));

    if (!stop_already_committed)
    {
        const double decel      = std::max(0.1, p.decel);
        const double brake_dist = (v_ego * v_ego) / (2.0 * decel);
        if (s_pull < brake_dist)
        {
            // Cannot stop short of the junction any more. Braking to a halt here
            // would strand the ego in the box, so clear it instead.
            res.action = JunctionStopAction::SUPPRESS;
            res.s_stop = 0.0;
            return res;
        }
    }

    // Pathological chain (junctions closer together than the clearance): the
    // pulled-back target itself lands in another box. Nothing safe to emit.
    if (FindBlockedSpan(s_pull, spans, exit_clearance) != nullptr)
    {
        res.action = JunctionStopAction::SUPPRESS;
        res.s_stop = 0.0;
        return res;
    }

    res.action = JunctionStopAction::PULL_BACK;
    res.s_stop = s_pull;
    return res;
}

}  // namespace gt_esmini
