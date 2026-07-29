#pragma once

#include <array>
#include <cstddef>
#include <unordered_map>

#include "gt_esmini/control/virtualdriver/ITrafficPolicy.hpp"
#include "gt_esmini/control/virtualdriver/PolicyDetail.hpp"

namespace gt_esmini
{

struct AebSafetyConfig
{
    // Candidate search horizon for Object::pos_.Delta() (route-distance bound),
    // mirrors LeadVehicleAwareConfig::lookahead. Not JSON-exposed (phase 1): AEB
    // is a close-range guardian and the default comfortably covers the verified
    // cut-in scenarios; the orchestrator can promote it to a config key later if
    // it needs tuning.
    double lookahead     = 120.0;  // [m]
    // Widened admission window vs LeadVehicleAware's idm_lateral_tol: a lead
    // that is still mid-lane-change (dLaneId still +-1) must remain visible, so
    // this must comfortably exceed one lane half-width.
    double lateral_tol   = 3.5;    // [m]
    // Fire when the predicted time-to-collision against the selected candidate
    // drops below this.
    double ttc_threshold = 2.5;    // [s]
    // Fire only when the deceleration required to avoid the collision exceeds
    // this — filters out soft-following geometry that a comfort-tier policy
    // (e.g. LeadVehicleAware) already handles.
    double min_a_req     = 3.0;    // [m/s^2]
    // Extra standoff behind the bumper-to-bumper gap for the emitted stop point
    // (STOP_AT_S is solved by the mid/long planner at emergency_decel).
    double stop_margin   = 2.0;    // [m]
};

// The collision-course gate, extracted from AebSafety::Evaluate() as a pure
// function of (gap, closing speed) so both the decision AND the quantities it
// was made from are testable and reportable (W3). Candidate SELECTION stays in
// Evaluate() — it needs the road network; the safety DECISION does not.
namespace aeb
{
struct GateResult
{
    double ttc   = 0.0;    // time to collision [s]
    double a_req = 0.0;    // deceleration required to avoid it [m/s^2]
    bool   valid = false;  // false when not closing / already overlapping (no gate math)
    bool   triggered = false;
};

inline GateResult EvaluateGate(const AebSafetyConfig& cfg, double gap, double v_close)
{
    GateResult r;
    if (v_close <= 0.0 || gap <= 0.0) return r;  // not closing (or overlapping)

    r.valid     = true;
    r.ttc       = gap / v_close;
    r.a_req     = (v_close * v_close) / (2.0 * gap);
    r.triggered = (r.ttc < cfg.ttc_threshold && r.a_req > cfg.min_a_req);
    return r;
}

// Emits the gate's internals under the gt.aeb.* key convention (PolicyDetail.hpp).
inline void AppendGateDetail(PolicyDetail& detail, const GateResult& r)
{
    AddDetail(detail, "gt.aeb.ttc_s", r.ttc);
    AddDetail(detail, "gt.aeb.a_req_mps2", r.a_req);
    AddDetail(detail, "gt.aeb.triggered", r.triggered);
}
}  // namespace aeb

// Short per-candidate ring buffer of recent |dt| (lateral-offset magnitude)
// samples — debounces the lateral-encroachment cue (AebSafety::dt_history_)
// against curved-reference-line dt jitter. On a curved road,
// roadmanager::PositionDiff::dt carries sub-millimetre per-frame numerical
// noise even for a laterally static (lane-holding) neighbour; comparing only
// against the immediately-previous frame mistook that noise for genuine
// lateral closing. Comparing instead against the sample from kDepth frames
// back requires a real, cumulative shrink of at least kEncroachMove (see
// anonymous namespace in the .cpp) before the cue fires.
struct AebDtHistory
{
    static constexpr std::size_t kDepth = 3;  // frames of retained history

    std::array<double, kDepth> samples{};  // ring buffer of |dt| samples
    std::size_t                count = 0;  // valid samples so far, saturates at kDepth
    std::size_t                next  = 0;  // write cursor == index of the oldest sample once count == kDepth
};

// AEB phase 1: a pure ADAS guardian layered on top of (and independent from)
// LeadVehicleAware. Longitudinal-only — never steers. Where LeadVehicleAware
// is a comfort-tier same-lane follower (dLaneId==0 only, so it only notices a
// cut-in once the lane change has fully completed), AebSafety additionally
// admits a still-changing-lane neighbour (|dLaneId|<=1) that is actively
// encroaching toward the ego lane, and only intervenes when a collision-course
// gate (closing speed + bumper gap -> TTC / required-decel) says the geometry
// is actually urgent. On fire it emits ONE SAFETY-tier STOP_AT_S constraint
// (PolicyConstraint::Tier::SAFETY), which ManeuverAwareSpeedPlanner ramps at
// emergency_decel instead of comfort_decel. When nothing is urgent it emits
// nothing, so it composes cleanly alongside "lead" (strictest constraint wins
// in the planner's fold).
class AebSafety : public ITrafficPolicy
{
public:
    explicit AebSafety(const AebSafetyConfig& cfg = {}) : cfg_(cfg) {}
    TrafficPolicySnapshot Evaluate(const TrafficPolicyContext& ctx) override;

private:
    AebSafetyConfig cfg_;

    // Encroachment cue (kinematic, primary): short per-candidate-object |dt|
    // history (AebDtHistory, above), used to detect a genuine, debounced
    // lateral-closing rate (|dt| shrinking toward the ego lane) for
    // neighbours that are not yet dLaneId==0. Cross-call state, same pattern
    // as StopYieldSignAware::stop_states_ / ConflictPointResolver's
    // committed_*.
    std::unordered_map<int, AebDtHistory> dt_history_;
};

}  // namespace gt_esmini
