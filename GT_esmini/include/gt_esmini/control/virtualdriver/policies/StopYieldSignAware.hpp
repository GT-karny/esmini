#pragma once

#include <unordered_map>

#include "gt_esmini/control/virtualdriver/ITrafficPolicy.hpp"

namespace gt_esmini
{

// STOP-sign behaviour as a small state machine, factored out for unit testing.
// Reproduces "stop fully -> creep forward a little to check -> go". The
// CREEP->CLEARED transition is a fixed timer today; Phase 3d will replace it with
// a "no conflicting traffic" gate so the go decision becomes situation-aware.
namespace stop_fsm
{
enum class Phase
{
    APPROACH,  // braking to the stop line
    HOLD,      // stopped, holding for the dwell time
    CREEP,     // edging forward at creep speed to check
    CLEARED    // done — never stop again for this sign
};

struct Params
{
    double stop_hold_time    = 1.5;  // [s]   dwell once stopped
    double stop_detect_speed = 0.3;  // [m/s] at/below this counts as stopped
    double stop_line_tol     = 2.0;  // [m]   close enough to the line to count as "at" it
    double creep_speed       = 2.0;  // [m/s] speed cap while edging forward
    double creep_advance     = 4.0;  // [m]   how far past the line to creep before going
};

struct State
{
    Phase  phase         = Phase::APPROACH;
    double phase_start_t = 0.0;
};

// Advance the FSM for one frame given the sign distance ahead, ego speed and the
// current sim time. Returns the constraint to emit this frame; Kind::NONE means
// emit nothing (CLEARED). Pure given (state, observation, params).
PolicyConstraint Update(State& st, double dist, double v_ego, double now, const Params& p);
}  // namespace stop_fsm

struct StopYieldSignAwareConfig
{
    stop_fsm::Params stop;
    double           yield_creep_speed = 3.0;  // [m/s] YIELD = decelerate only (stop deferred to 3d)
    double           lookahead         = 80.0;  // [m]
    // Stop this far BEFORE the sign s (front halts at the line, origin stays short
    // of it) so the sign stays within the forward scan while stopped and the FSM
    // keeps tracking it. ~ vehicle front overhang. The FSM sees the margin-adjusted
    // distance, so "stopped at the line" is detected at adjusted-dist ~ 0.
    double           stop_margin       = 3.0;   // [m]
};

// Phase 3c: react to STOP (OSI type 17) and YIELD/GIVE_WAY (OSI type 16) signs
// ahead. STOP runs the dwell+creep FSM; YIELD only caps speed to a creep up to
// the sign (full stop / right-of-way judgement is Phase 3d).
class StopYieldSignAware : public ITrafficPolicy
{
public:
    explicit StopYieldSignAware(const StopYieldSignAwareConfig& cfg = {}) : cfg_(cfg) {}
    TrafficPolicySnapshot Evaluate(const TrafficPolicyContext& ctx) override;

private:
    StopYieldSignAwareConfig          cfg_;
    std::unordered_map<int, stop_fsm::State> stop_states_;  // keyed by signal id
};

}  // namespace gt_esmini
