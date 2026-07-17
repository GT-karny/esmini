#include "gt_esmini/control/virtualdriver/policies/AebSafety.hpp"

#include "Entities.hpp"
#include "RoadManager.hpp"

#include <algorithm>
#include <cmath>

using namespace scenarioengine;

namespace gt_esmini
{

namespace
{
// Minimum CUMULATIVE shrink in |dt| over AebDtHistory::kDepth frames to count
// as "encroaching" rather than curved-reference-line dt jitter. A single-frame
// test (comparing only to the immediately previous frame) mistakes that
// jitter for genuine lateral closing: roadmanager::PositionDiff::dt carries
// sub-millimetre per-frame numerical noise on a curved reference line even
// for a laterally static (lane-holding) neighbour, whereas a straight road's
// dt is bit-exact constant. Debouncing over kDepth frames (see
// AebSafety::dt_history_) fixes this: a real cut-in (sinusoidal
// LaneChangeAction, ~3.07 m over ~1.4 s => ~0.11 m/frame at dt=0.05s, i.e.
// ~0.33 m over 3 frames) clears this threshold comfortably, while sub-mm
// jitter accumulated over the same window never does.
constexpr double kEncroachMove = 0.03;  // [m], cumulative over AebDtHistory::kDepth frames
}  // namespace

TrafficPolicySnapshot AebSafety::Evaluate(const TrafficPolicyContext& ctx)
{
    TrafficPolicySnapshot snap;
    if (!ctx.ego || !ctx.entities) return snap;

    Object* ego = ctx.ego;

    // --- Select the nearest admitted, in-path-or-encroaching candidate ahead ---
    // Mirrors LeadVehicleAware's candidate loop (cheap Euclidean pre-filter +
    // Delta() road-network search), but wider: a still-changing-lane cut-in
    // (|dLaneId|==1) is admitted, not just an already-same-lane lead
    // (dLaneId==0) — LeadVehicleAware's dLaneId==0-only filter is exactly what
    // makes it see a cut-in late.
    Object*                   best      = nullptr;
    double                    best_ds   = cfg_.lookahead;
    roadmanager::PositionDiff best_diff = {};

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

        if (diff.ds <= 0.0) continue;                        // must be ahead
        if (std::fabs(diff.dt) > cfg_.lateral_tol) continue;  // outside the encroachment window
        if (diff.dLaneId > 1 || diff.dLaneId < -1) continue;  // more than one lane away -> not yet a threat

        // Encroachment cue (kinematic): lateral offset shrinking toward the ego
        // lane, tracked per-object across frames. A same-lane candidate
        // (dLaneId == 0) is already in-path — like a LeadVehicleAware target —
        // and needs no encroachment history. A still-adjacent candidate
        // (|dLaneId| == 1) must be actively cutting in to be eligible, so a
        // merely-parallel cruiser (REQ-AD-013 negative) never reaches the
        // collision-course gate below. The comparison is debounced over
        // AebDtHistory::kDepth frames rather than the immediately-previous
        // frame — see kEncroachMove above for why a single-frame test
        // false-positives on a curved reference line.
        const int    other_id = other->GetId();
        const double abs_dt   = std::fabs(diff.dt);
        bool         encroaching = false;

        AebDtHistory& hist = dt_history_[other_id];
        if (hist.count == AebDtHistory::kDepth)
        {
            // hist.samples[hist.next] is the slot about to be overwritten
            // below, i.e. still holds the sample from kDepth frames ago.
            encroaching = abs_dt < (hist.samples[hist.next] - kEncroachMove);
        }
        hist.samples[hist.next] = abs_dt;
        hist.next               = (hist.next + 1) % AebDtHistory::kDepth;
        if (hist.count < AebDtHistory::kDepth) ++hist.count;

        const bool in_path = (diff.dLaneId == 0);
        if (!in_path && !encroaching) continue;

        if (diff.ds < best_ds)
        {
            best_ds   = diff.ds;
            best      = other;
            best_diff = diff;
        }
    }

    if (!best) return snap;  // no admitted in-path/encroaching candidate — nothing to guard against

    // --- Collision-course gate (the safety decision; the cue above only picks
    //     the candidate) — longitudinal-only, no steering. ---
    const double ego_len   = ego->boundingbox_.dimensions_.length_;
    const double other_len = best->boundingbox_.dimensions_.length_;
    const double gap       = best_diff.ds - (ego_len / 2.0 + other_len / 2.0);

    const double v_ego   = ego->GetSpeed();
    const double v_other = best->GetSpeed();
    const double v_close = v_ego - v_other;

    if (v_close <= 0.0 || gap <= 0.0) return snap;  // not closing (or already overlapping) -> no gate math

    const double ttc   = gap / v_close;
    const double a_req = (v_close * v_close) / (2.0 * gap);

    if (ttc < cfg_.ttc_threshold && a_req > cfg_.min_a_req)
    {
        PolicyConstraint c;
        c.kind   = PolicyConstraint::Kind::STOP_AT_S;
        c.tier   = PolicyConstraint::Tier::SAFETY;
        c.s      = std::max(0.0, gap - cfg_.stop_margin);
        c.value  = 0.0;
        c.source = "aeb";
        snap.constraints.push_back(c);
        snap.valid = true;
    }

    return snap;
}

}  // namespace gt_esmini
