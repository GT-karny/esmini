#pragma once

#include "gt_esmini/control/virtualdriver/ITrafficPolicy.hpp"

namespace gt_esmini
{

// Intelligent Driver Model parameters + pure math, factored out of the policy so
// it can be unit-tested with no engine dependency. The formula is ported from
// esmini's ControllerNaturalDriver (GetAcceleration / GetDesiredGap), not
// reinvented.
namespace lead_idm
{
struct Params
{
    double time_headway  = 1.5;   // T  [s]      desired time gap to the lead
    double min_gap       = 2.0;   // s0 [m]      standstill bumper-to-bumper gap
    double max_accel     = 1.5;   // a  [m/s^2]  max comfortable acceleration
    double comfort_decel = 2.0;   // b  [m/s^2]  comfortable deceleration (>0)
    double desired_speed = 50.0;  // v0 [m/s]    free-flow speed. Kept high so the
                                  //             gap term governs and the scenario
                                  //             SpeedAction stays the real target.
};

// IDM desired (minimum safe) gap s* = s0 + max(0, v*T + v*dv / (2*sqrt(a*b))).
double DesiredGap(const Params& p, double v_ego, double v_lead);

// IDM acceleration a*(1 - (v/v0)^4 - (s*/gap)^2). gap is the bumper-to-bumper
// freespace [m] (clamped to a small positive inside). With no lead pass a huge
// gap to get the free-flow term only.
double DesiredAccel(const Params& p, double v_ego, double v_lead, double gap);
}  // namespace lead_idm

struct LeadVehicleAwareConfig
{
    lead_idm::Params idm;
    double lookahead       = 120.0;  // [m]   how far ahead to search for a lead
    double lateral_tol     = 2.0;    // [m]   reject leads offset more than this
    double target_horizon  = 0.5;    // [s]   tau: v_target = v_ego + a_idm * tau
    double stop_speed_eps  = 0.3;    // [m/s] lead at/below this counts as stopped
    double follow_margin   = 1.5;    // gap > follow_margin * s* -> free flow (no cap)
};

// Phase 3a: follow the nearest same-lane lead vehicle using IDM. Emits
//   - STOP_AT_S  when the lead is essentially stopped and within the min gap
//                (jam stop; s = gap - min_gap, bypasses the planner min_speed),
//   - MAX_SPEED  = v_ego + a_idm*tau  while inside the following regime,
//   - nothing    when no lead, or the lead is comfortably far (free flow).
class LeadVehicleAware : public ITrafficPolicy
{
public:
    explicit LeadVehicleAware(const LeadVehicleAwareConfig& cfg = {}) : cfg_(cfg) {}
    TrafficPolicySnapshot Evaluate(const TrafficPolicyContext& ctx) override;

private:
    LeadVehicleAwareConfig cfg_;
};

}  // namespace gt_esmini
