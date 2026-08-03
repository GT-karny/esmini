#include "gt_esmini/control/virtualdriver/LaneChangeInitiation.hpp"

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

    if (gap.has_lead)
    {
        const double required_lead = std::max(cfg.gap_min_m, v_ego * cfg.gap_headway_lead_s);
        if (gap.gap_lead_m < required_lead)
        {
            result.accepted = false;
            result.reason   = "lead_gap";
            return result;
        }
    }

    if (gap.has_rear)
    {
        const double required_rear = std::max(cfg.gap_min_m, gap.v_rear_mps * cfg.gap_headway_rear_s);
        if (gap.gap_rear_m < required_rear)
        {
            result.accepted = false;
            result.reason   = "rear_gap";
            return result;
        }

        // TTC only applies while the follower is actually closing (v_rear > v_ego); a follower
        // that is stationary or slower relative to the ego cannot "catch up" in finite time, so
        // gap_rear_m / (v_rear - v_ego) would be negative or undefined there -- design doc
        // section 4's table gates this condition on "後続車が接近中" for exactly that reason.
        if (gap.v_rear_mps > v_ego)
        {
            const double closing = gap.v_rear_mps - v_ego;
            const double ttc     = gap.gap_rear_m / closing;
            if (ttc < cfg.gap_ttc_min_s)
            {
                result.accepted = false;
                result.reason   = "rear_ttc";
                return result;
            }
        }
    }

    result.accepted = true;
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
        sample.has_lead   = true;
        sample.gap_lead_m = std::max(0.0, lead_ds - half_ego_front - half_lead_rear);
        sample.v_lead_mps = lead_obj->GetSpeed();
    }

    if (rear_obj)
    {
        const double half_ego_rear    = ego.boundingbox_.dimensions_.length_ / 2.0 - ego.boundingbox_.center_.x_;
        const double half_rear_front  = rear_obj->boundingbox_.dimensions_.length_ / 2.0 + rear_obj->boundingbox_.center_.x_;
        sample.has_rear   = true;
        sample.gap_rear_m = std::max(0.0, rear_ds - half_ego_rear - half_rear_front);
        sample.v_rear_mps = rear_obj->GetSpeed();
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
}

void DisarmLaneChangeHop(LaneChangeInitiationState& state)
{
    state.armed = false;
}

}  // namespace gt_esmini
