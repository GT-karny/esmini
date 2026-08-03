#include "gt_esmini/control/virtualdriver/OvertakeManeuver.hpp"

namespace gt_esmini
{

const char* OvertakePhaseName(OvertakePhase phase)
{
    switch (phase)
    {
        case OvertakePhase::IDLE:
            return "idle";
        case OvertakePhase::SIGNAL_OUT:
            return "signal_out";
        case OvertakePhase::MOVING_OUT:
            return "out";
        case OvertakePhase::PASS:
            return "pass";
        case OvertakePhase::SIGNAL_BACK:
            return "signal_back";
        case OvertakePhase::MOVING_BACK:
            return "back";
    }
    return "idle";  // unreachable for a valid enumerator; keeps -Wreturn-type happy
}

OvertakeTriggerResult EvaluateOvertakeTrigger(const OvertakeLeadSample&   lead,
                                              const OvertakeTriggerInput& in,
                                              const OvertakeConfig&       cfg)
{
    OvertakeTriggerResult result;

    if (!cfg.enabled || !lead.has_lead)
    {
        result.reason = "no_lead";
        return result;
    }

    // Every one of these three is derivable from (lead, in) alone, independent of which gate
    // below ultimately rejects the frame -- filled unconditionally (design doc section 9-1's
    // false-PASS concern generalizes to rejections too: a "not_slower" or "pass_too_long" frame
    // with delta_v_mps/t_pass_s left at 0 would be undiagnosable from telemetry alone).
    result.delta_v_mps      = in.v_desired_mps - lead.v_lead_mps;
    result.clear_distance_m = lead.gap_lead_m + in.return_clearance_m + in.ego_length_m + lead.lead_length_m;
    // Guarded here (not just at the final gate) so this field is NEVER negative/inf even when the
    // caller inspects it on a rejected ("free_flow"/"not_slower") frame.
    result.t_pass_s = (result.delta_v_mps > 0.0) ? (result.clear_distance_m / result.delta_v_mps) : 0.0;

    // Constraint gate (design doc section 3-2): only a lead LeadVehicleAware would already call
    // "not free flow" (gap <= follow_margin * s*) is a motive to overtake at all.
    if (lead.gap_lead_m > in.idm_follow_margin * in.idm_desired_gap_m)
    {
        result.reason = "free_flow";
        return result;
    }

    if (result.delta_v_mps <= 0.0)
    {
        result.reason = "not_slower";
        return result;
    }

    if (result.t_pass_s > cfg.max_pass_time_s)
    {
        result.reason = "pass_too_long";
        return result;
    }

    result.considered = true;
    return result;
}

OvertakeRouteGuardResult EvaluateOvertakeRouteGuard(const OvertakeRouteGuardInput&    in,
                                                    const LaneChangeInitiationConfig& lc_cfg)
{
    OvertakeRouteGuardResult result;

    // No route to protect -> nothing can be sacrificed for a pass (design doc section 2-3).
    if (!in.route_valid)
    {
        result.allowed = true;
        return result;
    }

    // RouteLanePlan's "-1 == no onward connection from this band" sentinel: there is no connection
    // to miss, so the guard has nothing to gate. See this function's header-comment for why this
    // is the SAME polarity as ShouldAttemptLaneChangeHop's own <0 handling (both true, for
    // different reasons) and the OPPOSITE of ShouldSignalLaneChangeHop's own <0 handling (false).
    if (in.dist_to_connection < 0.0)
    {
        result.allowed = true;
        return result;
    }

    // design doc section 2's formula, verbatim -- RequiredLaneChangeDistance is CALLED, not
    // reimplemented (its own internal + reserve_distance_m for n_back>0 is untouched here; this
    // function's own trailing + lc_cfg.reserve_distance_m is the design doc's literal formula, not
    // a correction of that internal term -- see the header comment for the resulting double count
    // when n_back>=1).
    const double d_out   = in.v_pass_mps * in.hop_duration_s;
    const double d_pass  = in.v_pass_mps * in.t_pass_s;
    const double d_back  = in.v_pass_mps * in.hop_duration_s;
    const double d_route = RequiredLaneChangeDistance(in.n_back, in.v_pass_mps, lc_cfg);

    result.required_m = d_out + d_pass + d_back + d_route + lc_cfg.reserve_distance_m;
    result.allowed    = (result.required_m <= in.dist_to_connection);
    return result;
}

bool AcceptOncomingGap(const OncomingSample& sample, double v_ego_mps, double t_total_s, const OvertakeConfig& cfg)
{
    if (!sample.has_oncoming)
    {
        return true;
    }

    // Deliberately NOT EvaluateGapAcceptance's forward-gap formula (design doc section 7-2): an
    // oncoming vehicle closes at (v_ego + v_oncoming), not at a same-direction headway rate, so
    // reusing that formula here would silently drop the oncoming vehicle's own closing speed.
    const double required_gap_m = (v_ego_mps + sample.v_oncoming_mps) * t_total_s * cfg.oncoming_safety_factor;
    return sample.gap_m >= required_gap_m;
}

int OvertakePassingLaneId(int current_lane_id)
{
    // "Toward lane id 0" is "toward the centerline" under BOTH RHT (negative driving-lane ids) and
    // LHT (positive driving-lane ids) -- see the header comment for the full argument. Lane id 0
    // itself is the zero-width center lane and is never a passing-lane candidate.
    if (current_lane_id < -1)
    {
        return current_lane_id + 1;
    }
    if (current_lane_id > 1)
    {
        return current_lane_id - 1;
    }
    return 0;  // already adjacent to the centerline (or already 0) -- no passing-lane candidate
}

int OvertakeOpposingLaneId(int current_lane_id)
{
    if (current_lane_id == -1)
    {
        return 1;
    }
    if (current_lane_id == 1)
    {
        return -1;
    }
    return 0;  // no defined opposite for this id (includes 0 itself, and anything not adjacent to 0)
}

bool HasClearedLead(double relative_ds_m, double ego_length_m, double lead_length_m, double return_clearance_m)
{
    return relative_ds_m >= return_clearance_m + (ego_length_m + lead_length_m) / 2.0;
}

bool SignalDwellSatisfied(double signal_start_time_s, double now_s, double lead_time_s)
{
    if (signal_start_time_s < 0.0)
    {
        return false;  // not signaling yet
    }
    return (now_s - signal_start_time_s) >= lead_time_s;
}

}  // namespace gt_esmini
