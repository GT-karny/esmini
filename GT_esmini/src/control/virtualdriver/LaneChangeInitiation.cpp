#include "gt_esmini/control/virtualdriver/LaneChangeInitiation.hpp"

#include "gt_esmini/control/common/OsiIdentity.hpp"

#include "Entities.hpp"
#include "RoadManager.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

using namespace scenarioengine;

namespace gt_esmini
{

LaneHopPlan ComputeLaneHopPlan(int current_lane_id, const std::vector<int>& target_lanes)
{
    LaneHopPlan plan;
    if (target_lanes.empty())
    {
        return plan;
    }

    int best      = target_lanes.front();
    int best_dist = std::abs(target_lanes.front() - current_lane_id);
    for (int t : target_lanes)
    {
        const int d = std::abs(t - current_lane_id);
        if (d < best_dist)
        {
            best      = t;
            best_dist = d;
        }
    }

    if (best_dist == 0)
    {
        return plan;  // already on a target lane -- no hop to plan
    }

    plan.direction_step   = (best > current_lane_id) ? 1 : -1;
    plan.next_hop_lane_id = current_lane_id + plan.direction_step;
    plan.n_remaining      = best_dist;
    plan.valid            = true;
    return plan;
}

double RequiredLaneChangeDistance(int n_remaining, double v_ego, const LaneChangeInitiationConfig& cfg)
{
    if (n_remaining <= 0)
    {
        return 0.0;
    }
    return static_cast<double>(n_remaining) * std::max(v_ego * cfg.lead_time_s, cfg.min_lead_distance_m) +
           cfg.reserve_distance_m;
}

bool ShouldAttemptLaneChangeHop(int n_remaining, double dist_to_connection, double v_ego, const LaneChangeInitiationConfig& cfg)
{
    if (!cfg.enabled || n_remaining <= 0)
    {
        return false;
    }
    if (dist_to_connection < 0.0)
    {
        return true;  // "not applicable" (final band) -- nothing left to wait for
    }
    return dist_to_connection <= RequiredLaneChangeDistance(n_remaining, v_ego, cfg);
}

bool ShouldSignalLaneChangeHop(int    n_remaining,
                               double dist_to_connection,
                               double v_ego,
                               double a_ego,
                               double v_cap,
                               const LaneChangeInitiationConfig& cfg)
{
    if (!cfg.enabled || n_remaining <= 0)
    {
        return false;
    }
    // design doc section 11-3's explicit trap: dist_to_connection<0 ("not applicable" / final
    // band) must NOT be treated as "close enough" here, unlike ShouldAttemptLaneChangeHop's own
    // < 0 handling above (which returns true there). A naive `<=` against a negative sentinel is
    // always true and would latch the indicator on forever in the final band.
    if (dist_to_connection < 0.0)
    {
        return false;
    }

    const double T     = cfg.indicator_lead_time_s;
    const double v_now = std::max(0.0, v_ego);
    const double a     = std::max(0.0, a_ego);  // decelerating is treated as flat -- safe/earlier side
    double       v_pred = v_now + a * T;
    if (v_cap > 0.0)
    {
        v_pred = std::min(v_pred, std::max(v_now, v_cap));  // cap never pulls below current speed
    }
    const double travel   = 0.5 * (v_now + v_pred) * T;  // trapezoidal distance over T seconds
    const double required = RequiredLaneChangeDistance(n_remaining, v_pred, cfg);
    return (dist_to_connection - travel) <= required;
}

GapAcceptanceResult EvaluateGapAcceptance(const LaneChangeGapSample&        gap,
                                          double                            v_ego,
                                          const LaneChangeInitiationConfig& cfg)
{
    GapAcceptanceResult result;

    // design vd_intent_layer.md section 8-2 (1) / 8-7: the three conditions are evaluated in
    // the SAME front-to-back order as before, but NONE of them returns early any more. What was
    // "stop at the first failure and name it" is now "collect every failure and name the first"
    // -- accepted and reason come out identical, and the blockers a caller previously could not
    // see (the follower sitting behind the leader that was already blocking) are all there.

    if (gap.has_lead)
    {
        const double required_lead = std::max(cfg.gap_min_m, v_ego * cfg.gap_headway_lead_s);
        if (gap.gap_lead_m < required_lead)
        {
            // section 8-3: a NEGATIVE unfloored gap means the two bodies overlap
            // longitudinally, i.e. the "leader" is really alongside. Only the LABEL changes --
            // gap_lead_m is floored at 0 and still fails this same comparison, so the verdict
            // is untouched.
            const bool    overlapping = gap.lead_overlap_m < 0.0;
            IntentBlocker blocker;
            blocker.where          = overlapping ? IntentWhere::SIDE : IntentWhere::FRONT;
            blocker.subject_osi_id = gap.lead_osi_id;
            blocker.code           = overlapping ? kBlockerSideOverlap : kBlockerLeadGap;
            blocker.quantity       = kQuantityGapM;
            blocker.measured       = overlapping ? gap.lead_overlap_m : gap.gap_lead_m;
            blocker.required       = required_lead;
            result.blockers.push_back(blocker);
        }
    }

    if (gap.has_rear)
    {
        const double required_rear = std::max(cfg.gap_min_m, gap.v_rear_mps * cfg.gap_headway_rear_s);
        if (gap.gap_rear_m < required_rear)
        {
            const bool    overlapping = gap.rear_overlap_m < 0.0;
            IntentBlocker blocker;
            blocker.where          = overlapping ? IntentWhere::SIDE : IntentWhere::REAR;
            blocker.subject_osi_id = gap.rear_osi_id;
            blocker.code           = overlapping ? kBlockerSideOverlap : kBlockerRearGap;
            blocker.quantity       = kQuantityGapM;
            blocker.measured       = overlapping ? gap.rear_overlap_m : gap.gap_rear_m;
            blocker.required       = required_rear;
            result.blockers.push_back(blocker);
        }

        // TTC only applies while the follower is actually closing (v_rear > v_ego); a follower
        // that is stationary or slower relative to the ego cannot "catch up" in finite time, so
        // gap_rear_m / (v_rear - v_ego) would be negative or undefined there -- design doc
        // section 4's table gates this condition on "後続車が接近中" for exactly that reason.
        //
        // NOTE this now runs even when the rear GAP condition above already failed, which it
        // could not before. That is the point: "too close AND closing fast" and "too close but
        // drifting away" are different situations, and the old form could only report the
        // first. It still cannot divide by zero -- the guard is a strict >.
        if (gap.v_rear_mps > v_ego)
        {
            const double closing = gap.v_rear_mps - v_ego;
            const double ttc     = gap.gap_rear_m / closing;
            if (ttc < cfg.gap_ttc_min_s)
            {
                IntentBlocker blocker;
                blocker.where          = IntentWhere::REAR;
                blocker.subject_osi_id = gap.rear_osi_id;
                blocker.code           = kBlockerRearTtc;
                blocker.quantity       = kQuantityTtcS;
                blocker.measured       = ttc;
                blocker.required       = cfg.gap_ttc_min_s;
                result.blockers.push_back(blocker);
            }
        }
    }

    // Identical to the old "returned early at least once": one failing condition is enough.
    result.accepted = result.blockers.empty();
    if (!result.accepted)
    {
        result.reason = result.blockers.front().code;  // section 8-7 (2)
    }
    return result;
}

LaneChangeGapSample ScanAdjacentLaneGap(const Object& ego, const Entities& entities, int direction_step, double lookahead)
{
    LaneChangeGapSample sample;
    if (direction_step == 0 || lookahead <= 0.0)
    {
        return sample;
    }

    const Object* lead_obj = nullptr;
    const Object* rear_obj = nullptr;
    double        lead_ds  = lookahead;  // smallest forward gap seen so far
    double        rear_ds  = lookahead;  // smallest rearward gap MAGNITUDE seen so far

    // Same cheap Euclidean pre-filter LeadVehicleAware uses before the road-network Delta() call
    // (see LeadVehicleAware.cpp for the non-behavioural proof: arc >= chord).
    const double reject_radius_sq = lookahead * lookahead;

    for (auto* other : entities.object_)
    {
        if (!other || other == &ego)
        {
            continue;
        }

        const double dx = other->pos_.GetX() - ego.pos_.GetX();
        const double dy = other->pos_.GetY() - ego.pos_.GetY();
        if (dx * dx + dy * dy > reject_radius_sq)
        {
            continue;
        }

        roadmanager::PositionDiff diff = {};
        // bothDirections=true (unlike LeadVehicleAware, which only ever looks ahead): a lane
        // change needs the nearest vehicle BEHIND in the target lane too (design doc section 1's
        // "覆った想定1" -- the backward search was already in Position::Delta, just unused).
        if (!ego.pos_.Delta(&other->pos_, diff, true, lookahead))
        {
            continue;
        }

        if (diff.dLaneId != direction_step)
        {
            continue;  // not in the ONE lane this hop targets
        }

        if (diff.ds > 0.0 && diff.ds < lead_ds)
        {
            lead_ds  = diff.ds;
            lead_obj = other;
        }
        else if (diff.ds < 0.0 && -diff.ds < rear_ds)
        {
            rear_ds  = -diff.ds;
            rear_obj = other;
        }
    }

    if (lead_obj)
    {
        const double half_ego_front  = ego.boundingbox_.dimensions_.length_ / 2.0 + ego.boundingbox_.center_.x_;
        const double half_lead_rear  = lead_obj->boundingbox_.dimensions_.length_ / 2.0 - lead_obj->boundingbox_.center_.x_;
        // Bumper-to-bumper freespace BEFORE the floor. gap_lead_m keeps the floor (every
        // existing reader expects a non-negative gap); lead_overlap_m keeps the negative half,
        // which is the only evidence that the "leader" is in fact alongside (design
        // vd_intent_layer.md section 8-3).
        const double raw      = lead_ds - half_ego_front - half_lead_rear;
        sample.has_lead       = true;
        sample.gap_lead_m     = std::max(0.0, raw);
        sample.lead_overlap_m = std::min(0.0, raw);
        sample.v_lead_mps     = lead_obj->GetSpeed();
        sample.lead_osi_id    = OsiIdOf(lead_obj);
    }

    if (rear_obj)
    {
        const double half_ego_rear    = ego.boundingbox_.dimensions_.length_ / 2.0 - ego.boundingbox_.center_.x_;
        const double half_rear_front  = rear_obj->boundingbox_.dimensions_.length_ / 2.0 + rear_obj->boundingbox_.center_.x_;
        const double raw      = rear_ds - half_ego_rear - half_rear_front;
        sample.has_rear       = true;
        sample.gap_rear_m     = std::max(0.0, raw);
        sample.rear_overlap_m = std::min(0.0, raw);
        sample.v_rear_mps     = rear_obj->GetSpeed();
        sample.rear_osi_id    = OsiIdOf(rear_obj);
    }

    return sample;
}

void ArmLaneChangeHop(LaneChangeInitiationState& state,
                      unsigned int               hop_track_id,
                      int                        hop_target_lane_id,
                      int                        direction_step,
                      int                        direction_indicator)
{
    state.armed               = true;
    state.hop_track_id        = hop_track_id;
    state.hop_target_lane_id  = hop_target_lane_id;
    state.direction_step      = direction_step;
    state.direction_indicator = direction_indicator;
    state.last_gap_reason.clear();
    // design vd_intent_layer.md section 3-4: "次の arm で消す". A fresh hop has not been
    // aborted, so the breadcrumb from the PREVIOUS one must not leak into it -- otherwise the
    // intent layer would read a stale abort and call this hop's completion an ABORTING.
    state.aborted_reason.clear();
}

void DisarmLaneChangeHop(LaneChangeInitiationState& state)
{
    state.armed = false;
}

void AbortLaneChangeHop(LaneChangeInitiationState& state, const std::string& reason)
{
    // Same single write DisarmLaneChangeHop makes -- the abort path must not differ from the
    // completion path in anything the CONTROL side can see (design vd_intent_layer.md's
    // "既存挙動はビット単位で不変" requirement); the reason string is observation only.
    state.armed          = false;
    state.aborted_reason = reason;
}

}  // namespace gt_esmini
