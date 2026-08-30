#pragma once

// vd-func:FUNC-056 -- autonomous OVERTAKE maneuver, core pure-function layer. See
// docs/virtualdriver/design/overtake_maneuver.md (the design of record; this header implements
// it, not the other way around). Builds on top of LaneChangeInitiation.hpp's 1-hop lane-change
// mechanism (vd-func:FUNC-055) -- overtaking is that same mechanism run twice (out, then back),
// with a motive (a slow lead) instead of a deadline driving the first hop, and a route-budget
// GUARD in front of it so a route connection is never sacrificed for a pass (design doc section 2).
//
// Same convention as LaneChangeInitiation.hpp / RouteLanePlan.hpp: a free-function + POD layer
// living directly under virtualdriver/ (not policies/, not an ITrafficPolicy), never logs
// (diagnosis is returned, not logged), and is COMPLETELY engine-independent -- every function in
// this header is a pure function of PODs the caller fills (unlike LaneChangeInitiation.hpp, which
// still needs ScanAdjacentLaneGap's engine-dependent half; this layer's lead/oncoming samples are
// always supplied pre-scanned by the controller, per the design doc's OvertakeLeadSample /
// OncomingSample contracts). That makes every function here directly unit-testable with no loaded
// road network at all.
//
// What this layer does NOT do (design doc section 1's scope table, section 10's scope-out list):
//   - It does not generate a trajectory. ResumeMergeProfile.hpp is reused AS-IS by the controller,
//     exactly as LaneChangeInitiation's own hop is (design doc section 5's "1ホップ機構を2回まわす").
//   - It does not drive a state machine by itself. OvertakePhase is a plain enum; advancing it
//     (design doc section 5-1's transition table) is the CONTROLLER's job, not this layer's --
//     mirrors LaneChangeInitiationState being a passive latch armed/disarmed from outside.
//   - It does not implement passing-prohibition-zone awareness (vd-func:FUNC-030, not yet built)
//     or sight-distance occlusion modeling (design doc section 7-3) -- both are named, tracked
//     scope gaps, not silent omissions.
//   - It moves exactly ONE lane per hop, same as FUNC-055 (design doc section 10's table).

#include <string>

#include "gt_esmini/control/virtualdriver/LaneChangeInitiation.hpp"

namespace gt_esmini
{

// The overtake state machine's phase (design doc section 5). Distinct from
// LaneChangeInitiationState, which has no notion of phase -- it only knows "is a hop armed right
// now" (design doc section 5's "その上に薄いフェーズ状態を1つ置く"). The CONTROLLER owns an
// OvertakePhase variable and advances it per the transition table in section 5-1; this header only
// supplies the enum and its telemetry name, not the transition logic itself.
enum class OvertakePhase
{
    IDLE,         // not overtaking
    SIGNAL_OUT,   // pre-signaling the outbound hop; not yet moving laterally
    // NOT named OUT / BACK. `OUT` (and `IN`) are OBJECT-LIKE MACROS that expand to NOTHING in the
    // Windows SDK's own headers (`minwindef.h`'s SAL parameter annotations), which this TU pulls in
    // transitively. `OUT,` therefore preprocesses to a bare `,` and the enum stops parsing -- MSVC
    // reports it as a syntax error on the enum, with every downstream use showing up as
    // "C2589: ')' cannot follow ::". Same family of trap as `far`/`near` (see the /build skill's
    // troubleshooting table). The TELEMETRY strings are still "out"/"back" (design doc section 9-1's
    // fixed vocabulary is unaffected by this C++-side rename).
    MOVING_OUT,   // outbound hop armed (a SEPARATE ResumeMergeState instance is executing it)
    PASS,         // in the passing lane, drawing past the lead
    SIGNAL_BACK,  // pre-signaling the return hop; not yet moving laterally
    MOVING_BACK,  // return hop armed
};

// design doc section 9-1's `overtake.phase` telemetry field -- fixed vocabulary, one string per
// enumerator, mirroring OvertakePhase's own value set exactly (never omit/rename one without the
// other).
const char* OvertakePhaseName(OvertakePhase phase);

// Runtime tuning (design doc section 8's config table). Deliberately NOT folded into
// LaneChangeInitiationConfig -- design doc section 8's "新設は5個だけ" list is everything this
// feature needs beyond what it reuses (gap acceptance / decision-distance / signal-dwell keys all
// come from LaneChangeInitiationConfig, passed alongside this struct where needed).
struct OvertakeConfig
{
    bool   enabled                   = false;  // default OFF; independent of lane_change_initiation_enabled (design doc section 8)
    bool   use_opposing_lane_enabled = false;  // second, independent gate for opposing-lane passing (design doc section 7; FUNC-030 not yet built)
    double max_pass_time_s           = 10.0;   // [s] AASHTO PSD t2 (design doc section 3-1)
    double oncoming_lookahead_m      = 400.0;  // [m] SCAN distance, not a sight-distance claim (design doc section 7-3)
    double oncoming_safety_factor    = 1.5;    // unitless margin on the oncoming gap requirement (design doc section 7-3)
};

// The nearest same-lane lead vehicle, engine-dependent scan already performed by the caller
// (mirrors LaneChangeGapSample's own contract in LaneChangeInitiation.hpp -- has_lead==false means
// "no vehicle in range ahead", not "vehicle at zero gap").
struct OvertakeLeadSample
{
    bool   has_lead      = false;
    double gap_lead_m    = 0.0;   // [m] bumper-to-bumper freespace (g0, design doc section 3)
    double v_lead_mps    = 0.0;
    double lead_length_m = 0.0;   // [m] lead's own bounding-box length (L_lead)
    int    lead_id       = -1;    // scenario entity id -- also used by the maneuver to re-find the lead
    int    lead_osi_id   = -1;    // same vehicle in the OSI id space (control/common/OsiIdentity.hpp);
                                  // diagnostic only, -1 when has_lead is false
};

// Everything EvaluateOvertakeTrigger needs besides the lead sample itself (design doc section 3).
struct OvertakeTriggerInput
{
    double v_ego_mps          = 0.0;  // NOT used by EvaluateOvertakeTrigger's own math (design doc
                                       // section 3 never differentiates on raw ego speed, only on
                                       // v_desired_mps vs the lead's speed) -- carried here purely
                                       // so callers have one input struct to fill from telemetry;
                                       // see this .cpp's header comment for the confirmation.
    double v_desired_mps      = 0.0;  // v_pass = min(last_action_target_, v_ceiling) -- design doc section 3, NOT ResolveTargetSpeed()'s interpolated value
    double ego_length_m       = 0.0;  // [m] L_ego
    double return_clearance_m = 0.0;  // [m] g1 = lane_change_gap_min_m, reused verbatim (design doc section 3)
    double idm_desired_gap_m  = 0.0;  // [m] s* -- lead_idm::DesiredGap(...) (design doc section 3-2)
    double idm_follow_margin  = 1.5;  // LeadVehicleAwareConfig::follow_margin, reused verbatim (design doc section 3-2)
};

// design doc section 3's trigger + section 9-1's `overtake.considered/delta_v_mps/t_pass_s`
// telemetry fields folded into one result. `reason` is a FIXED vocabulary (never invent a new
// string outside this list): "" (considered==true) / "no_lead" / "free_flow" / "not_slower" /
// "pass_too_long" -- mirrors GapAcceptanceResult::reason's own "first failing condition" contract.
struct OvertakeTriggerResult
{
    bool        considered       = false;
    double      delta_v_mps      = 0.0;  // v_desired_mps - v_lead_mps
    double      t_pass_s         = 0.0;  // clear_distance_m / delta_v_mps; forced 0 when delta_v_mps<=0 (never negative/inf)
    double      clear_distance_m = 0.0;  // L_clear = g0 + g1 + L_ego + L_lead (design doc section 3)
    std::string reason;
};

// Design doc section 3, in the exact gate order the doc specifies:
//   1. !cfg.enabled || !lead.has_lead                              -> "no_lead"
//   2. lead.gap_lead_m > idm_follow_margin * idm_desired_gap_m      -> "free_flow" (not yet constrained -- no motive to overtake, design doc section 3-2)
//   3. delta_v_mps <= 0                                             -> "not_slower"
//   4. t_pass_s > cfg.max_pass_time_s                               -> "pass_too_long"
//   otherwise                                                        -> considered=true, reason=""
// delta_v_mps / clear_distance_m / t_pass_s are filled UNCONDITIONALLY whenever has_lead is true
// (i.e. even on a rejecting reason), so a rejected frame's telemetry still carries "how close" it
// came -- design doc section 9-1's "偽PASS" concern (a green with no diagnostic numbers behind it)
// applies just as much to a documented rejection as to a suspiciously-easy pass.
OvertakeTriggerResult EvaluateOvertakeTrigger(const OvertakeLeadSample&    lead,
                                              const OvertakeTriggerInput&  in,
                                              const OvertakeConfig&        cfg);

// design doc section 2's route-budget guard inputs. `dist_to_connection` and `n_back` follow
// RouteLanePlan's own "-1 == no onward connection" convention (RouteLanePlan.hpp), same sentinel
// LaneChangeInitiation.hpp's ShouldAttemptLaneChangeHop/ShouldSignalLaneChangeHop already use --
// but see EvaluateOvertakeRouteGuard's own comment below for why the polarity here differs again.
struct OvertakeRouteGuardInput
{
    bool   route_valid        = false;
    double dist_to_connection = -1.0;  // -1 == no onward connection to protect
    int    n_back              = 0;    // hops from the PASSING lane back to route_lane_status.target_lanes (design doc section 2-1 -- NOT the pre-overtake n_remaining)
    double v_pass_mps          = 0.0;
    double t_pass_s            = 0.0;  // OvertakeTriggerResult::t_pass_s
    double hop_duration_s      = 0.0;  // lc_merge_cfg_.duration_max_s -- ONE hop's ground time, used twice (out + back)
};

// design doc section 9-1's `overtake.required_m` / `overtake.route_budget_m` telemetry pair.
struct OvertakeRouteGuardResult
{
    bool   allowed    = false;
    double required_m = 0.0;
};

// design doc section 2's formula, verbatim:
//   required_m = 2*v_pass*hop_duration_s + v_pass*t_pass_s
//              + RequiredLaneChangeDistance(n_back, v_pass, lc_cfg) + lc_cfg.reserve_distance_m
//   allowed    = required_m <= dist_to_connection
//
// RequiredLaneChangeDistance(...) is called as-is (its own formula is n_remaining * max(v*lead_
// time_s, min_lead_distance_m) + reserve_distance_m for n_remaining>0, else 0 -- see
// LaneChangeInitiation.hpp) -- NOT reimplemented here. Note this means when n_back>=1, reserve_
// distance_m is added TWICE in total (once inside RequiredLaneChangeDistance's own return value,
// once again by this function's own formula): this is the design doc's literal section-2 formula,
// not an error introduced here -- flagged in this feature's implementation report as a point worth
// the design owner re-confirming, not silently "corrected" away.
//
// Two special cases, BOTH returning allowed=true, for DIFFERENT reasons (design doc section 2-3):
//   - !route_valid            -> no route to protect, so nothing can be sacrificed for a pass.
//   - dist_to_connection < 0  -> RouteLanePlan's "no onward connection from this band" sentinel:
//     there is no connection to miss, so the guard has nothing to gate. This is the SAME boolean
//     ShouldAttemptLaneChangeHop's own <0 handling produces (true, "nothing left to wait for") but
//     for an unrelated reason, and the OPPOSITE of ShouldSignalLaneChangeHop's own <0 handling
//     (false, "no distance to be close to" -- a naive <= against a negative sentinel would else
//     latch forever). Do not assume the three predicates' <0 handling generalizes from one to the
//     others; each has its own justification.
OvertakeRouteGuardResult EvaluateOvertakeRouteGuard(const OvertakeRouteGuardInput&    in,
                                                    const LaneChangeInitiationConfig& lc_cfg);

// An oncoming-traffic sample in the OPPOSING lane (design doc section 7-2), engine-dependent scan
// already performed by the caller (mirrors OvertakeLeadSample's own contract).
struct OncomingSample
{
    bool   has_oncoming   = false;
    double gap_m          = 0.0;  // [m] range to the nearest oncoming vehicle
    double v_oncoming_mps = 0.0;
    // The oncoming vehicle in the OSI id space (control/common/OsiIdentity.hpp), so a refusal
    // can name the car it refused for (design vd_intent_layer.md section 8-4). -1 when
    // has_oncoming is false. Same addition, same reason, as LaneChangeGapSample::lead_osi_id.
    int    oncoming_osi_id = -1;
};

// AcceptOncomingGap-with-its-working-shown (design vd_intent_layer.md section 8-2 (3)).
//
// The bare bool answers "may I go" but throws away the number that says HOW SHORT the gap was,
// which is exactly what an intent blocker needs to report ("42 m of room, 88 m needed"). That
// number was a local inside the predicate -- the same shape of loss PolicyDetail.hpp was
// created to fix for the policies.
struct OncomingGapResult
{
    bool   accepted       = false;
    double required_gap_m = 0.0;
};

// design doc section 7-2: an oncoming vehicle closes at (v_ego + v_oncoming), NOT at the same-
// direction headway rate EvaluateGapAcceptance's forward-gap condition assumes -- reusing that
// function's formula here would silently drop the oncoming vehicle's own closing speed and under-
// estimate the required gap ("危険側に大きく外す", design doc section 7-2's own wording). This is
// therefore a SEPARATE, small formula rather than a call into EvaluateGapAcceptance:
//   required_gap_m = (v_ego_mps + v_oncoming_mps) * t_total_s * cfg.oncoming_safety_factor
//   accepted       = gap_m >= required_gap_m
// t_total_s is the CALLER's responsibility to compute as hop_duration_s*2 + t_pass_s (design doc
// section 7-2's "対向車線の占有時間") -- not derived inside this function, so it stays a pure
// function of exactly the numbers named in its signature.
bool AcceptOncomingGap(const OncomingSample& sample, double v_ego_mps, double t_total_s, const OvertakeConfig& cfg);

// The same judgement, with required_gap_m handed back. AcceptOncomingGap above is implemented
// in terms of THIS function rather than the other way round, so there is exactly one copy of
// the formula and the reported requirement can never drift from the one that was applied.
OncomingGapResult AcceptOncomingGapDetailed(const OncomingSample& sample,
                                            double                v_ego_mps,
                                            double                t_total_s,
                                            const OvertakeConfig& cfg);

// design doc section 4: the passing lane is "same direction, one step toward lane id 0" (the
// centerline side) -- this single rule is correct under BOTH RHT (negative ids: -2 -> -1) and LHT
// (positive ids: +2 -> +1) without case-splitting on driving side, because "toward 0" and "toward
// the centerline" are the same statement in OpenDRIVE's signed lane-id convention regardless of
// which side of 0 the driving lanes sit on. Lane id 0 itself is the zero-width center lane, never a
// driving lane, so a return value of 0 means "no passing-lane candidate on this side" -- it does
// NOT assert that lane actually exists or is a driving lane; the CALLER must still confirm that
// against the live road network (this function has no engine dependency at all).
int OvertakePassingLaneId(int current_lane_id);

// design doc section 7-1: the opposing lane is the first driving lane on the OTHER side of the
// centerline, i.e. the lane id whose SIGN is flipped relative to current_lane_id (id 0 has no
// opposite side, hence 0 -> 0: "no candidate"). Only -1 and +1 have a defined flip in this scheme
// (the nearest lane to the centerline on either side); a caller already 2+ lanes out is expected to
// use OvertakePassingLaneId to work back toward the centerline first, not this function directly
// (design doc section 7-1 only ever discusses the single-lane-each-way case explicitly). Same
// engine-independence and "candidate, not proof" caveat as OvertakePassingLaneId above.
int OvertakeOpposingLaneId(int current_lane_id);

// design doc section 5-1's PASS -> SIGNAL_BACK transition condition: true once the ego has drawn
// `return_clearance_m` (g1) plus half of each vehicle's own length ahead of the lead's reference
// point, i.e. the ego could tuck back in front of the lead right now without violating the same g1
// floor the trigger's own clear_distance_m calculation uses. `relative_ds_m` is ego_s - lead_s
// (positive == ego ahead) for the SAME lead vehicle recorded when the overtake began -- the caller
// must not re-resolve "nearest same-lane vehicle" every frame here (design doc section 5-1's own
// warning: re-scanning would swap targets the instant the lead is passed).
bool HasClearedLead(double relative_ds_m, double ego_length_m, double lead_length_m, double return_clearance_m);

// design doc section 1's timer form of the pre-signal (replaces LaneChangeInitiation's forward-
// projected ShouldSignalLaneChangeHop for this feature specifically -- overtaking has no deadline
// to project against, design doc section 1's central claim). True once `lead_time_s` (the SAME
// lane_change_indicator_lead_time_s config key, reused verbatim) has elapsed since the signal was
// switched on. signal_start_time_s < 0 means "not signaling yet" and always returns false -- it is
// NOT a "signal since the beginning of time" sentinel.
bool SignalDwellSatisfied(double signal_start_time_s, double now_s, double lead_time_s);

}  // namespace gt_esmini
