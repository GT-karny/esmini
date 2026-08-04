// req-vd-ad:REQ-AD-025 / req-vd-ad:REQ-AD-028 / vd-func:FUNC-075
//
// PHASE A, STEP 2: real implementation of the semantics documented in
// AdasCoexistenceStack.hpp (two-instance FCW mechanism, a_req derivation,
// domain/config bypass, detail merge rule, warning-threshold looseness
// guard). See that header for the full rationale behind every choice below;
// this file only implements it.
//
// NOTE for the coordinator: AdasCoexistenceStack::Step() calls
// KickdownDetector::Update() and PedalArbitrator::Arbitrate(), both still
// PHASE A STEP 1 stubs as of this writing (their own headers document the
// real contract; their .cpp bodies do not implement it yet). Any
// test_AdasCoexistenceStack.cpp case that depends on a correct
// Arbitrate()/Update() result (pass-through byte-equality, AEB brake
// composition, kickdown suppression, the derived gt.aeb.* detail keys that
// read pedals.aeb_brake_request/aeb_suppressed) will stay red until those two
// land their own step 2 -- that is a dependency gap, not a defect in this
// file. This file's OWN logic (constraint filtering/selection, the a_req
// formula, the domain bypass, the superset repair, the config guard, the
// detail merge) is exercised directly wherever a test does not also require
// PedalArbitrator/KickdownDetector to be correct.

#include "gt_esmini/control/manualdrive/AdasCoexistenceStack.hpp"

#include "gt_esmini/control/virtualdriver/PolicyDetail.hpp"

#include "Entities.hpp"

#include <algorithm>

using namespace scenarioengine;

namespace gt_esmini
{

namespace
{

// kind==STOP_AT_S && tier==SAFETY && source=="aeb" -- see header's REQUIRED
// DECELERATION block. Every other kind/tier/source (MAX_SPEED ceilings,
// COMFORT-tier lead-following, future phase C/D policies) is phase A's to
// ignore.
bool IsQualifyingAebConstraint(const PolicyConstraint& c)
{
    return c.kind == PolicyConstraint::Kind::STOP_AT_S && c.tier == PolicyConstraint::Tier::SAFETY &&
           c.source == "aeb";
}

bool HasQualifyingAebConstraint(const TrafficPolicySnapshot& snap)
{
    for (const auto& c : snap.constraints)
        if (IsQualifyingAebConstraint(c)) return true;
    return false;
}

// Selects the STRICTEST qualifying constraint in `snap` (largest resulting
// a_req, equivalently smallest distance) and writes its required
// deceleration into *out_a_req. Returns false (no request) when either no
// constraint qualifies OR v<=0 (already stopped -- see header). d<=
// kMinStopDistanceM saturates to `full_brake_decel_mps2` instead of dividing
// by a near-zero distance.
bool SelectStrictestAebRequest(const TrafficPolicySnapshot& snap,
                                double                       v,
                                double                       full_brake_decel_mps2,
                                double*                      out_a_req)
{
    if (v <= 0.0) return false;  // already stopped: never divide by/into a non-positive speed

    bool   found = false;
    double best  = 0.0;
    for (const auto& c : snap.constraints)
    {
        if (!IsQualifyingAebConstraint(c)) continue;

        const double d = c.s;
        const double a_req =
            (d <= kMinStopDistanceM) ? full_brake_decel_mps2  // saturating sentinel, avoids div-by-~0
                                     : (v * v) / (2.0 * d);

        if (!found || a_req > best)  // strictest = largest a_req = smallest d
        {
            best  = a_req;
            found = true;
        }
    }

    if (found) *out_a_req = best;
    return found;
}

// Plain pass-through PedalArbitrationSnapshot, used by both the domain and
// config bypasses (header's DOMAIN OWNERSHIP block): the driver's own
// throttle/brake, no AEB engagement/suppression claimed.
PedalArbitrationSnapshot PassThrough(const PedalSteerCommand& driver_cmd)
{
    PedalArbitrationSnapshot snap;
    snap.throttle_out = driver_cmd.throttle;
    snap.brake_out    = driver_cmd.brake;
    return snap;
}

}  // namespace

double ComputeMeasuredDecel(double v_now, double v_prev, double dt)
{
    if (dt <= 0.0) return 0.0;
    // POSITIVE when slowing (v_now < v_prev) -- matches PedalArbitrator.hpp's
    // sign convention verbatim.
    return (v_prev - v_now) / dt;
}

AebSafetyConfig DeriveFcwGateConfig(const ManualAdasStackConfig& cfg)
{
    AebSafetyConfig fcw = cfg.aeb;  // candidate-selection params copied verbatim (lookahead/lateral_tol/stop_margin)

    // GUARD: clamp, never let the warning gate end up tighter than the
    // intervention gate -- see header's "GUARD CHOICE" comment on
    // DeriveFcwGateConfig for why this is a clamp and not a throw/reject.
    fcw.ttc_threshold = std::max(cfg.warning_ttc_threshold_s, cfg.aeb.ttc_threshold);
    fcw.min_a_req     = std::min(cfg.warning_min_a_req_mps2, cfg.aeb.min_a_req);

    return fcw;
}

ManualAdasFrameResult ComputeManualAdasFrame(const ManualAdasStackConfig& cfg,
                                              bool                         owns_longitudinal,
                                              const TrafficPolicySnapshot& intervention,
                                              const TrafficPolicySnapshot& warning,
                                              const PedalSteerCommand&     driver_cmd,
                                              double                       ego_speed_mps,
                                              double                       measured_decel_mps2,
                                              double                       dt,
                                              KickdownDetector&            kickdown,
                                              PedalArbitrator&             arbitrator)
{
    ManualAdasFrameResult result;

    // Domain / config bypass (design §2-3): do not arbitrate AT ALL, do not
    // touch the shared KickdownDetector/PedalArbitrator state, and leave
    // `detail` EMPTY (not zeroed) -- see header's DOMAIN OWNERSHIP block.
    if (!owns_longitudinal || !cfg.aeb_enabled)
    {
        result.pedals = PassThrough(driver_cmd);
        return result;
    }

    // Shared detector (design §3-3): fed every frame the stack actually
    // runs, regardless of whether AEB itself has anything to say this frame
    // -- MSL (phase C) reads the same latch.
    const bool kickdown_raw       = kickdown.Update(driver_cmd.throttle);
    const bool kickdown_effective = cfg.kickdown_suppress_enabled ? kickdown_raw : false;

    double     a_req       = 0.0;
    const bool has_request =
        SelectStrictestAebRequest(intervention, ego_speed_mps, cfg.arbitrator.full_brake_decel_mps2, &a_req);

    PedalArbitrationInput in;
    in.driver_throttle     = driver_cmd.throttle;
    in.driver_brake        = driver_cmd.brake;
    in.aeb_requested       = has_request;
    in.aeb_decel_mps2      = a_req;  // 0.0 when !has_request (never written above in that case)
    in.measured_decel_mps2 = measured_decel_mps2;
    in.kickdown_active     = kickdown_effective;

    result.pedals                 = arbitrator.Arbitrate(in, dt);
    result.aeb_decel_request_mps2 = a_req;

    result.decision.aeb_intervening = has_request;
    // Superset repair (header's "InterventionWithoutWarningSnapshot..."
    // rationale): an actual intervention always implies the warning flag,
    // regardless of what the (possibly inconsistent) warning snapshot says.
    result.decision.fcw_warning = HasQualifyingAebConstraint(warning) || result.decision.aeb_intervening;

    // detail: intervention's own W3 diagnostics, copied VERBATIM, plus the
    // stack's own additions (design §8-4 + driver_brake/brake_out) -- the
    // WARNING snapshot's own detail is intentionally NOT merged (see header).
    result.detail = intervention.detail;
    AddDetail(result.detail, "gt.aeb.warning", result.decision.fcw_warning);
    AddDetail(result.detail, "gt.aeb.decel_request_mps2", result.aeb_decel_request_mps2);
    AddDetail(result.detail, "gt.aeb.brake_request", result.pedals.aeb_brake_request);
    AddDetail(result.detail, "gt.aeb.suppressed", result.pedals.aeb_suppressed);
    AddDetail(result.detail, "gt.aeb.kickdown", kickdown_raw);
    AddDetail(result.detail, "gt.aeb.driver_brake", driver_cmd.brake);
    AddDetail(result.detail, "gt.aeb.brake_out", result.pedals.brake_out);

    return result;
}

AdasCoexistenceStack::AdasCoexistenceStack(const ManualAdasStackConfig& cfg)
    : cfg_(cfg)
    , kickdown_(cfg.kickdown)
    , arbitrator_(cfg.arbitrator)
{
    if (cfg_.aeb_enabled)
    {
        // INTERVENTION path: phase A adds exactly one AebSafety, at the
        // policy's own (intervention) thresholds.
        policies_.Add(std::make_unique<AebSafety>(cfg_.aeb));
        // WARNING path: a SECOND, independent AebSafety at the looser,
        // guard-clamped thresholds. See header's "THE TWO AebSafety
        // INSTANCES" block.
        fcw_gate_ = std::make_unique<AebSafety>(DeriveFcwGateConfig(cfg_));
    }
}

ManualAdasFrameResult AdasCoexistenceStack::Step(const TrafficPolicyContext& ctx,
                                                  bool                        owns_longitudinal,
                                                  const PedalSteerCommand&    driver_cmd,
                                                  double                      dt)
{
    const double ego_speed_mps       = (ctx.ego != nullptr) ? ctx.ego->GetSpeed() : 0.0;
    const double measured_decel_mps2 = ComputeMeasuredDecel(ego_speed_mps, prev_speed_mps_, dt);
    prev_speed_mps_                  = ego_speed_mps;

    // Evaluated every frame regardless of owns_longitudinal -- see this
    // method's own header-comment doc for why (keeps AebSafety's cross-frame
    // dt_history_ encroachment debounce warm across an ownership hand-off).
    const TrafficPolicySnapshot intervention = policies_.Evaluate(ctx);
    const TrafficPolicySnapshot warning       = fcw_gate_ ? fcw_gate_->Evaluate(ctx) : TrafficPolicySnapshot{};

    return ComputeManualAdasFrame(cfg_, owns_longitudinal, intervention, warning, driver_cmd, ego_speed_mps,
                                   measured_decel_mps2, dt, kickdown_, arbitrator_);
}

}  // namespace gt_esmini
