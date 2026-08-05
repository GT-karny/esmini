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
#include "gt_esmini/control/virtualdriver/policies/StopYieldSignAware.hpp"
#include "gt_esmini/control/virtualdriver/policies/TrafficLightAware.hpp"

#include "Entities.hpp"

#include <algorithm>
#include <cmath>

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
                                              const TrafficPolicySnapshot& acc_policy,
                                              const ManualAdasEnvironment& env,
                                              const PedalSteerCommand&     driver_cmd,
                                              double                       ego_speed_mps,
                                              double                       measured_decel_mps2,
                                              double                       dt,
                                              KickdownDetector&            kickdown,
                                              PedalArbitrator&             arbitrator,
                                              AccLonController&            acc,
                                              ManualAdasRuntime&           runtime)
{
    ManualAdasFrameResult result;

    // Domain / config bypass (design §2-3): do not arbitrate AT ALL, do not
    // touch the shared KickdownDetector/PedalArbitrator/ACC state, and leave
    // `detail` EMPTY (not zeroed) -- see header's DOMAIN OWNERSHIP block.
    //
    // PHASE C widened the config half of the condition: the bypass now
    // requires that NO manual ADAS function is enabled. Keeping the phase-A
    // `!cfg.aeb_enabled` test would have made an ACC-only or MSL-only
    // configuration take the bypass and produce nothing at all -- silently,
    // with the rows still reporting UNAVAILABLE, which is the failure mode
    // that looks exactly like a correctly-disabled run.
    const bool any_function_enabled = cfg.aeb_enabled || cfg.acc.enabled || cfg.msl.enabled;
    if (!owns_longitudinal || !any_function_enabled)
    {
        result.pedals = PassThrough(driver_cmd);
        // The button anchor still advances: it is not a decision, it is the
        // reference the NEXT frame's edge detection is taken against. Freezing
        // it here would turn a button that was pressed and released during a
        // bypass into a spurious edge on the first frame after the bypass ends.
        runtime.prev_buttons = env.buttons;
        return result;
    }

    // Shared detector (design §3-3): fed every frame the stack actually
    // runs, regardless of whether AEB itself has anything to say this frame
    // -- MSL reads the same latch (SpeedLimiter.hpp's KICKDOWN block).
    const bool kickdown_raw       = kickdown.Update(driver_cmd.throttle);
    const bool kickdown_effective = cfg.kickdown_suppress_enabled ? kickdown_raw : false;

    // ======================================================================
    // Operating controls + ACC/MSL exclusivity (design §4-1, §6)
    // ======================================================================
    const AdasOperations ops = DecodeAdasOperations(env.buttons, runtime.prev_buttons);
    runtime.prev_buttons     = env.buttons;

    // ---- power toggles + "the later ON wins" (design §6) -------------------
    //
    // A TOGGLE PRESSED ON A **DEMOTED** FUNCTION RECLAIMS IT; IT DOES NOT
    // SWITCH IT OFF. This is the one non-obvious rule here, and it was found by
    // running rather than by reasoning: the first phase-C measurement pass had
    // md_acc_msl_exclusion going ...ACTIVE -> STANDBY (demoted by MSL) ->
    // UNAVAILABLE, because a plain toggle on a STANDBY function powers it OFF.
    // Under exclusivity that reading is wrong twice over -- the driver who
    // presses ACC while the limiter holds the stalk is asking for ACC, not
    // asking to switch off a function that is already not running; and it makes
    // "the later ON wins" unreachable in the only direction that needs two
    // presses to test. So a toggle on a SUSPENDED function is CONSUMED here
    // (never forwarded to the function's own state machine) and swaps the
    // suspension instead.
    bool           acc_reclaim = false;
    AdasOperations acc_ops     = ops;

    if (cfg.msl.enabled && ops.msl_toggle && runtime.msl_on && runtime.msl_suspended)
    {
        // Reclaim the limiter: it keeps its cap (ManualAdasRuntime) and ACC is
        // the one demoted from here on.
        runtime.msl_suspended = false;
        runtime.acc_suspended = true;
    }
    else if (cfg.msl.enabled && ops.msl_toggle)
    {
        runtime.msl_on = !runtime.msl_on;
        if (runtime.msl_on)
        {
            if (!runtime.msl_has_set_speed)
            {
                runtime.msl_set_speed_mps = std::max(0.0, ego_speed_mps);
                runtime.msl_has_set_speed = true;
            }
            runtime.msl_suspended = false;
            runtime.acc_suspended = true;  // later ON wins (design §6)
        }
        else
        {
            runtime.msl_has_set_speed = false;
            runtime.msl_set_speed_mps = 0.0;
            runtime.acc_suspended     = false;
        }
    }

    // `acc.State() != OFF` is load-bearing: there is nothing to RECLAIM on a
    // function the driver never switched on. Without it, the very first
    // ACC_TOGGLE after the limiter armed would be swallowed by the reclaim
    // branch -- ACC would stay OFF while the code believed it had just taken
    // the stalk, and the driver would have to press twice to switch it on.
    if (cfg.acc.enabled && ops.acc_toggle && runtime.acc_suspended && acc.State() != AccState::OFF)
    {
        acc_reclaim           = true;
        acc_ops.acc_toggle    = false;  // consumed: do NOT power ACC off
        runtime.acc_suspended = false;
        runtime.msl_suspended = runtime.msl_on;
    }
    else if (cfg.acc.enabled && ops.acc_toggle)
    {
        // ACC's own OFF<->STANDBY transition happens inside AccLonController;
        // what belongs HERE is only the cross-function consequence. Reading
        // the PRE-step state is what makes "turning ACC on" distinguishable
        // from "turning ACC off" without duplicating the state machine.
        const bool acc_turning_on = acc.State() == AccState::OFF;
        if (acc_turning_on)
        {
            runtime.acc_suspended = false;
            runtime.msl_suspended = runtime.msl_on;
        }
        else
        {
            runtime.msl_suspended = false;
        }
    }
    (void)acc_reclaim;  // kept named for readability of the branch above

    // The speed-adjust buttons drive whichever function currently holds the
    // stalk. Exclusivity means at most one is un-suspended, so this is a
    // routing decision, not an arbitration one.
    const bool msl_holds_stalk = runtime.msl_on && !runtime.msl_suspended;
    if (msl_holds_stalk)
    {
        acc_ops.acc_speed_up   = false;
        acc_ops.acc_speed_down = false;
        if (runtime.msl_has_set_speed && (ops.acc_speed_up || ops.acc_speed_down))
        {
            const double delta = ops.acc_speed_up ? cfg.acc.set_speed_step_mps : -cfg.acc.set_speed_step_mps;
            runtime.msl_set_speed_mps = std::max(0.0, runtime.msl_set_speed_mps + delta);
        }
    }

    // ======================================================================
    // Stage 1 -- ACC (generate), design §3-1 step 1 / §4
    // ======================================================================
    const AccCeiling ceiling = EvaluateAccCeiling(acc_policy.constraints, cfg.acc.decel_max_mps2);

    AccFrameInput acc_in;
    acc_in.ops             = acc_ops;
    acc_in.driver_throttle = driver_cmd.throttle;
    acc_in.driver_brake    = driver_cmd.brake;
    acc_in.ego_speed_mps   = ego_speed_mps;
    acc_in.policy          = ceiling;
    acc_in.speed_limit_mps = env.speed_limit_mps;
    acc_in.suspended       = runtime.acc_suspended;
    // Observation only -- see PolicyDetail.hpp's TryGetDetail block for why
    // reading the following policy's own gap is the correct source for the
    // reported headway rather than a second lead search.
    {
        double gap_m = 0.0;
        if (TryGetDetail(acc_policy.detail, "gt.lead_vehicle.gap_m", &gap_m) && ego_speed_mps > 0.1)
        {
            acc_in.thw_actual_s = gap_m / ego_speed_mps;
        }
    }
    const AccFrameOutput acc_out = acc.Step(acc_in, dt);
    result.acc                   = acc_out;

    // ======================================================================
    // Stage 2 -- MSL (limit), design §3-1 step 2 / §6
    // ======================================================================
    SpeedLimiterConfig msl_cfg = cfg.msl;
    // A limiter the driver has not switched on has no cap: enabled-in-config
    // is STANDBY, switched-on-by-the-driver is what arms it.
    msl_cfg.enabled            = cfg.msl.enabled && runtime.msl_on && !runtime.msl_suspended;
    msl_cfg.set_speed_mps      = runtime.msl_set_speed_mps;
    const SpeedLimiterResult msl_out =
        ApplySpeedLimiter(msl_cfg, acc_out.throttle, ego_speed_mps, env.speed_limit_mps, kickdown_raw);
    result.msl = msl_out;

    // ======================================================================
    // Stage 3 -- AEB (safety), design §3-1 step 3. Unchanged from phase A
    // apart from its inputs now being the previous two stages' output rather
    // than the raw human pedals -- exactly the seam PedalArbitrator.hpp
    // described.
    // ======================================================================
    double     a_req       = 0.0;
    const bool has_request =
        SelectStrictestAebRequest(intervention, ego_speed_mps, cfg.arbitrator.full_brake_decel_mps2, &a_req);

    PedalArbitrationInput in;
    in.driver_throttle     = msl_out.throttle_out;
    in.driver_brake        = acc_out.brake;
    in.aeb_requested       = has_request;
    in.aeb_decel_mps2      = a_req;  // 0.0 when !has_request (never written above in that case)
    in.measured_decel_mps2 = measured_decel_mps2;
    in.kickdown_active     = kickdown_effective;

    result.pedals                 = arbitrator.Arbitrate(in, dt);
    result.aeb_decel_request_mps2 = a_req;

    result.decision.aeb_intervening = has_request;
    // req-vd-ad:REQ-AD-028 段b (phase B): the accelerator-origin driver
    // override. kickdown_effective, NOT pedals.aeb_suppressed -- the wider
    // "AEB is being held off by the driver's accelerator" condition, which is
    // what OSI's DriverOverride asks about. See ManualAdasDecision::
    // driver_override_accel in AdasFunctionReport.hpp for the full rationale
    // (the narrower per-frame veto stays observable as gt.aeb.suppressed,
    // emitted a few lines below).
    result.decision.driver_override_accel = kickdown_effective;
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

    // ======================================================================
    // phase C decisions + observables (design §8-4, REQ-AD-026 e/g/h,
    // REQ-AD-030 c/d, REQ-AD-031 a)
    // ======================================================================
    //
    // AccState -> the int ManualAdasDecision carries. The translation is here,
    // in one place, precisely because AdasFunctionReport.hpp must not include a
    // manualdrive/ header (see that field's comment).
    auto acc_state_code = [](AccState s) {
        return s == AccState::ACTIVE ? 2 : (s == AccState::STANDBY ? 1 : 0);
    };
    result.decision.acc_state                 = acc_state_code(acc_out.state);
    result.decision.acc_driver_override_brake = acc_out.driver_override_brake;
    result.decision.acc_driver_override_accel = acc_out.driver_override_accel;

    // MSL's three-value state: not switched on by the driver (or demoted by
    // ACC) = 0/1, actually clamping = 2. `limiting` rather than "enabled" is
    // what ACTIVE means for a limiter -- an armed limiter below its cap is
    // watching, not acting, which is the same STANDBY-vs-ACTIVE distinction
    // REQ-AD-028 段a makes everywhere else.
    if (!runtime.msl_on || runtime.msl_suspended)
        result.decision.msl_state = runtime.msl_on ? 1 : 0;
    else
        result.decision.msl_state = msl_out.limiting ? 2 : 1;
    result.decision.msl_driver_override_accel = msl_out.kickdown_released;

    if (cfg.acc.enabled)
    {
        // "Set" and "effective" as two separate observables -- the whole
        // judgment basis for REQ-AD-026 steps e/g/h (AccLonController.hpp's
        // last block). A matcher that could only see one of them would pass a
        // controller that stored the setting and ignored it.
        AddDetail(result.detail, "gt.acc.set_speed_mps", acc_out.set_speed_mps);
        AddDetail(result.detail, "gt.acc.effective_cap_mps", acc_out.effective_cap_mps);
        AddDetail(result.detail, "gt.acc.thw_setting_s", acc_out.thw_setting_s);
        AddDetail(result.detail, "gt.acc.thw_actual_s", acc_out.thw_actual_s);
        AddDetail(result.detail, "gt.acc.thw_stage", acc_out.thw_stage);
        AddDetail(result.detail, "gt.acc.engaged", acc_out.engaged);
        AddDetail(result.detail, "gt.acc.stop_hold", acc_out.stop_hold);
        AddDetail(result.detail, "gt.acc.throttle_out", acc_out.throttle);
        AddDetail(result.detail, "gt.acc.brake_out", acc_out.brake);
        // Whether ANY setting change has happened yet on this run. The
        // setting_reflected matcher requires it: a run in which the driver
        // never touched the stalk cannot evidence "a change was reflected",
        // and without this key such a run would pass vacuously on a constant
        // field (verification plan §4-2's explicit "構造的に赤" requirement).
        AddDetail(result.detail, "gt.acc.setting_changed", acc.SettingEverChanged());
        // Stop-target composition, so the two configurations of
        // md-sng-target-config-polarity are distinguishable from the stream
        // itself rather than only from the manifest that produced it.
        AddDetail(result.detail, "gt.acc.stop_requested", ceiling.stop_requested);
        AddDetail(result.detail, "gt.acc.stop_distance_m", ceiling.stop_distance_m);
    }

    if (cfg.msl.enabled)
    {
        AddDetail(result.detail, "gt.msl.cap_mps", msl_out.cap_mps);
        AddDetail(result.detail, "gt.msl.kickdown", msl_out.kickdown_released);
        AddDetail(result.detail, "gt.msl.limiting", msl_out.limiting);
        AddDetail(result.detail, "gt.msl.throttle_in", acc_out.throttle);
        AddDetail(result.detail, "gt.msl.throttle_out", msl_out.throttle_out);
        // The brake the LIMIT stage contributed, which is identically zero by
        // construction (SpeedLimiter.hpp's "A LIMITER IS NOT A CONTROLLER").
        // Emitted rather than assumed because REQ-AD-030 step a's negative
        // (md-msl-no-brake-downhill) has to be judged from the stream, and a
        // claim nobody can observe is a claim nobody can falsify.
        AddDetail(result.detail, "gt.msl.brake_out", 0.0);
    }

    return result;
}

AdasCoexistenceStack::AdasCoexistenceStack(const ManualAdasStackConfig& cfg)
    : cfg_(cfg)
    , kickdown_(cfg.kickdown)
    , arbitrator_(cfg.arbitrator)
    , acc_(cfg.acc)
{
    if (cfg_.aeb_enabled)
    {
        // INTERVENTION path: exactly one AebSafety, at the policy's own
        // (intervention) thresholds.
        policies_.Add(std::make_unique<AebSafety>(cfg_.aeb));
        // WARNING path: a SECOND, independent AebSafety at the looser,
        // guard-clamped thresholds. See header's "THE TWO AebSafety
        // INSTANCES" block.
        fcw_gate_ = std::make_unique<AebSafety>(DeriveFcwGateConfig(cfg_));
    }

    if (cfg_.acc.enabled)
    {
        // ACC-side policies. LeadVehicleAware is built at the DEFAULT
        // following-distance stage and rebuilt whenever the driver cycles it
        // (RebuildLeadPolicy).
        RebuildLeadPolicy(cfg_.acc.thw_stages.AtStage(acc_.ThwStage()));
        lead_stage_built_ = acc_.ThwStage();

        // REQ-AD-031 段b: the 段b stop targets exist in this run only if the
        // config asked for them. Not adding the policy (rather than adding it
        // and filtering its output) is what makes the negative direction
        // structural -- there is no constraint to leak through a filter that
        // someone might later relax.
        if (cfg_.acc.stop_and_go.enabled && cfg_.acc.stop_and_go.stop_at_traffic_light)
        {
            stop_policies_.Add(std::make_unique<TrafficLightAware>());
        }
        if (cfg_.acc.stop_and_go.enabled && cfg_.acc.stop_and_go.stop_at_stop_sign)
        {
            stop_policies_.Add(std::make_unique<StopYieldSignAware>());
        }
    }
}

void AdasCoexistenceStack::RebuildLeadPolicy(double time_headway_s)
{
    LeadVehicleAwareConfig lead_cfg = cfg_.lead;
    lead_cfg.idm.time_headway       = time_headway_s;
    lead_                           = std::make_unique<LeadVehicleAware>(lead_cfg);
}

ManualAdasFrameResult AdasCoexistenceStack::Step(const TrafficPolicyContext&  ctx,
                                                  bool                         owns_longitudinal,
                                                  const PedalSteerCommand&     driver_cmd,
                                                  const ManualAdasEnvironment& env,
                                                  double                       dt)
{
    const double ego_speed_mps       = (ctx.ego != nullptr) ? ctx.ego->GetSpeed() : 0.0;
    const double measured_decel_mps2 = ComputeMeasuredDecel(ego_speed_mps, prev_speed_mps_, dt);
    prev_speed_mps_                  = ego_speed_mps;

    // Evaluated every frame regardless of owns_longitudinal -- see this
    // method's own header-comment doc for why (keeps AebSafety's cross-frame
    // dt_history_ encroachment debounce warm across an ownership hand-off).
    // The ACC-side policies are evaluated on the same terms and for the same
    // reason: StopYieldSignAware carries its own cross-frame stop timers.
    const TrafficPolicySnapshot intervention = policies_.Evaluate(ctx);
    const TrafficPolicySnapshot warning       = fcw_gate_ ? fcw_gate_->Evaluate(ctx) : TrafficPolicySnapshot{};

    TrafficPolicySnapshot acc_policy = stop_policies_.Evaluate(ctx);
    if (lead_)
    {
        const TrafficPolicySnapshot s = lead_->Evaluate(ctx);
        acc_policy.constraints.insert(acc_policy.constraints.end(), s.constraints.begin(), s.constraints.end());
        acc_policy.detail.insert(acc_policy.detail.end(), s.detail.begin(), s.detail.end());
    }

    ManualAdasFrameResult result = ComputeManualAdasFrame(cfg_, owns_longitudinal, intervention, warning, acc_policy,
                                                           env, driver_cmd, ego_speed_mps, measured_decel_mps2, dt,
                                                           kickdown_, arbitrator_, acc_, runtime_);

    // REQ-AD-026 step h: the stage the driver just selected takes effect on
    // the NEXT frame's following constraint. Rebuilding here (after the frame,
    // not before) rather than inside ComputeManualAdasFrame keeps that pure
    // function free of policy ownership -- and the one-frame latency is
    // invisible at any dt this project runs, while the alternative (rebuilding
    // mid-frame between the ACC step and the policy evaluation that already
    // happened) would report a headway the frame did not actually use.
    if (lead_ && acc_.ThwStage() != lead_stage_built_)
    {
        RebuildLeadPolicy(cfg_.acc.thw_stages.AtStage(acc_.ThwStage()));
        lead_stage_built_ = acc_.ThwStage();
    }

    return result;
}

}  // namespace gt_esmini
