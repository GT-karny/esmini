#include "gt_esmini/control/virtualdriver/policies/LeadVehicleAware.hpp"

#include "gt_esmini/control/virtualdriver/PolicyDetail.hpp"

#include "Entities.hpp"
#include "RoadManager.hpp"

#include <algorithm>
#include <cmath>

using namespace scenarioengine;

namespace gt_esmini
{

namespace lead_idm
{
double DesiredGap(const Params& p, double v_ego, double v_lead)
{
    const double ab = p.max_accel * p.comfort_decel;
    const double dv = v_ego - v_lead;
    const double dyn = (ab > 1.0e-6) ? (v_ego * p.time_headway + v_ego * dv / (2.0 * std::sqrt(ab))) : (v_ego * p.time_headway);
    return p.min_gap + std::max(0.0, dyn);
}

double DesiredAccel(const Params& p, double v_ego, double v_lead, double gap)
{
    constexpr int delta = 4;
    const double  v0    = std::max(1.0e-3, p.desired_speed);
    double        accel = p.max_accel * (1.0 - std::pow(v_ego / v0, delta));

    const double s     = std::max(1.0e-3, gap);
    const double sstar = DesiredGap(p, v_ego, v_lead);
    accel -= p.max_accel * std::pow(sstar / s, 2);
    return accel;
}
}  // namespace lead_idm

TrafficPolicySnapshot LeadVehicleAware::Evaluate(const TrafficPolicyContext& ctx)
{
    TrafficPolicySnapshot snap;
    if (!ctx.ego || !ctx.entities) return snap;

    Object* ego = ctx.ego;

    // --- Find the nearest same-lane lead vehicle ahead (NaturalDriver pattern) ---
    Object*      lead    = nullptr;
    double       lead_ds = cfg_.lookahead;  // smallest forward gap so far
    // Cheap Euclidean pre-filter before the (expensive) road-network Delta() search, which otherwise
    // runs for EVERY entity on the map every frame and dominates the policy cost (~0.1 ms/vehicle).
    // Provably non-behavioural: a road-following path is never shorter than the straight line between
    // its endpoints (arc >= chord), so euclid <= |ds| + |dt|. Acceptance below already requires
    // |dt| <= lateral_tol and ds < lookahead, hence any entity with euclid > lookahead + lateral_tol
    // would be rejected anyway. Squared compare -- no sqrt on the hot path.
    const double reject_radius    = cfg_.lookahead + cfg_.lateral_tol;
    const double reject_radius_sq = reject_radius * reject_radius;

    for (auto* other : ctx.entities->object_)
    {
        if (!other || other == ego) continue;

        const double dx = other->pos_.GetX() - ego->pos_.GetX();
        const double dy = other->pos_.GetY() - ego->pos_.GetY();
        if (dx * dx + dy * dy > reject_radius_sq) continue;

        roadmanager::PositionDiff diff = {};
        if (!ego->pos_.Delta(&other->pos_, diff, false, cfg_.lookahead)) continue;

        if (diff.dLaneId != 0) continue;                 // same lane only
        if (diff.ds <= 0.0) continue;                    // must be ahead
        if (std::fabs(diff.dt) > cfg_.lateral_tol) continue;
        if (diff.ds < lead_ds)
        {
            lead_ds = diff.ds;
            lead    = other;
        }
    }

    if (!lead) return snap;  // free flow — let the SpeedAction/planner govern

    // Bumper-to-bumper freespace (mirrors NaturalDriver::EstimateFreespace for the
    // same-direction case; ref points are BB centers).
    const double half_ego  = ego->boundingbox_.dimensions_.length_ / 2.0 + ego->boundingbox_.center_.x_;
    const double half_lead = lead->boundingbox_.dimensions_.length_ / 2.0 - lead->boundingbox_.center_.x_;
    double       gap       = lead_ds - half_ego - half_lead;
    if (gap < 0.0) gap = 0.0;

    const double v_ego  = ego->GetSpeed();
    const double v_lead = lead->GetSpeed();
    const double sstar  = lead_idm::DesiredGap(cfg_.idm, v_ego, v_lead);

    // W3-style diagnostics, negative case included: with a lead in scope but no
    // constraint emitted, gap vs the IDM desired gap sstar is what explains the
    // silence ("comfortably far" is recomputable offline from these three).
    AddDetail(snap.detail, "gt.lead_vehicle.gap_m", gap);
    AddDetail(snap.detail, "gt.lead_vehicle.v_lead_mps", v_lead);
    AddDetail(snap.detail, "gt.lead_vehicle.sstar_m", sstar);

    // Comfortably far behind a moving lead -> no constraint (free acceleration).
    if (v_lead > cfg_.stop_speed_eps && gap > cfg_.follow_margin * sstar)
        return snap;

    // Lead essentially stopped and we are within the standstill gap -> hard stop.
    if (v_lead <= cfg_.stop_speed_eps && gap <= cfg_.idm.min_gap + 1.0)
    {
        PolicyConstraint c;
        c.kind   = PolicyConstraint::Kind::STOP_AT_S;
        c.s      = std::max(0.0, gap - cfg_.idm.min_gap);
        c.value  = 0.0;
        c.source = "lead_vehicle";
        snap.constraints.push_back(c);
        snap.valid = true;
        return snap;
    }

    // Following regime: cap the speed at the IDM target for this frame.
    const double a_idm  = lead_idm::DesiredAccel(cfg_.idm, v_ego, v_lead, gap);
    const double target = std::max(0.0, v_ego + a_idm * cfg_.target_horizon);

    AddDetail(snap.detail, "gt.lead_vehicle.v_target_mps", target);

    PolicyConstraint c;
    c.kind   = PolicyConstraint::Kind::MAX_SPEED;
    c.s      = 0.0;
    c.value  = target;
    c.source = "lead_vehicle";
    snap.constraints.push_back(c);
    snap.valid = true;
    return snap;
}

}  // namespace gt_esmini
