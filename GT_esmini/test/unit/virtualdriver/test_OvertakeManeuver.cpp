// test_OvertakeManeuver.cpp -- OvertakeManeuver unit tests.
//
// Mirrors test_LaneChangeInitiation.cpp's convention: exercises the PURE numeric core with
// synthetic inputs -- no loaded road network required, since (unlike LaneChangeInitiation.hpp)
// every function in this layer is engine-independent (design doc overtake_maneuver.md's header
// comment: the lead/oncoming samples are always pre-scanned by the caller).
#include <gtest/gtest.h>

#include "gt_esmini/control/virtualdriver/OvertakeManeuver.hpp"
#include "gt_esmini/control/virtualdriver/policies/LeadVehicleAware.hpp"

using namespace gt_esmini;

// ─────────────────────────── OvertakePhaseName ───────────────────────────

TEST(OvertakePhaseName, CoversEveryEnumeratorWithTheDesignDocVocabulary)
{
    EXPECT_STREQ(OvertakePhaseName(OvertakePhase::IDLE), "idle");
    EXPECT_STREQ(OvertakePhaseName(OvertakePhase::SIGNAL_OUT), "signal_out");
    EXPECT_STREQ(OvertakePhaseName(OvertakePhase::MOVING_OUT), "out");
    EXPECT_STREQ(OvertakePhaseName(OvertakePhase::PASS), "pass");
    EXPECT_STREQ(OvertakePhaseName(OvertakePhase::SIGNAL_BACK), "signal_back");
    EXPECT_STREQ(OvertakePhaseName(OvertakePhase::MOVING_BACK), "back");
}

// ─────────────────────────── EvaluateOvertakeTrigger ───────────────────────────
// design doc overtake_maneuver.md section 3's four reason branches, each independently, plus the
// success case, plus the Δv_min boundary (section 3-1's derived-quantity claim).

namespace
{
// A baseline "constrained, slower, short pass" fixture that satisfies every gate (design doc
// section 3): gap_lead_m=10 is inside follow_margin*idm_desired_gap_m=1.5*20=30 (constrained, not
// free flow), delta_v=20-15=5 (slower), clear_distance_m=10+8+5+5=28, t_pass_s=28/5=5.6 <=
// max_pass_time_s=10 (default).
OvertakeTriggerInput BaselineTriggerInput()
{
    OvertakeTriggerInput in;
    in.v_ego_mps          = 18.0;  // NOT used by EvaluateOvertakeTrigger's own math -- see header comment
    in.v_desired_mps      = 20.0;
    in.ego_length_m       = 5.0;
    in.return_clearance_m = 8.0;  // g1
    in.idm_desired_gap_m  = 20.0;  // s*
    in.idm_follow_margin  = 1.5;
    return in;
}

OvertakeLeadSample BaselineLead()
{
    OvertakeLeadSample lead;
    lead.has_lead      = true;
    lead.gap_lead_m    = 10.0;  // g0
    lead.v_lead_mps    = 15.0;
    lead.lead_length_m = 5.0;
    lead.lead_id       = 42;
    return lead;
}
}  // namespace

TEST(EvaluateOvertakeTrigger, DisabledIsNoLeadEvenWithAQualifyingLead)
{
    OvertakeConfig cfg;  // enabled = false (default)
    ASSERT_FALSE(cfg.enabled);
    const auto result = EvaluateOvertakeTrigger(BaselineLead(), BaselineTriggerInput(), cfg);
    EXPECT_FALSE(result.considered);
    EXPECT_EQ(result.reason, "no_lead");
    // Nothing is computed when the frame is rejected at the enabled/has_lead gate itself -- these
    // fields must stay at their POD defaults, not leak a stale/partial computation.
    EXPECT_EQ(result.delta_v_mps, 0.0);
    EXPECT_EQ(result.clear_distance_m, 0.0);
    EXPECT_EQ(result.t_pass_s, 0.0);
}

TEST(EvaluateOvertakeTrigger, NoLeadVehicleIsNoLead)
{
    OvertakeConfig cfg;
    cfg.enabled = true;
    OvertakeLeadSample lead;  // has_lead = false (default)
    const auto result = EvaluateOvertakeTrigger(lead, BaselineTriggerInput(), cfg);
    EXPECT_FALSE(result.considered);
    EXPECT_EQ(result.reason, "no_lead");
}

TEST(EvaluateOvertakeTrigger, FreeFlowWhenGapIsComfortablyFarPerLeadVehicleAwaresOwnDefinition)
{
    OvertakeConfig cfg;
    cfg.enabled = true;
    const OvertakeTriggerInput in = BaselineTriggerInput();  // follow_margin*s* = 1.5*20 = 30
    OvertakeLeadSample         lead = BaselineLead();
    lead.gap_lead_m = 35.0;  // > 30 -> free flow, no motive to overtake

    const auto result = EvaluateOvertakeTrigger(lead, in, cfg);
    EXPECT_FALSE(result.considered);
    EXPECT_EQ(result.reason, "free_flow");
    // Still filled even though rejected (design doc section 9-1's diagnosability requirement):
    EXPECT_NEAR(result.delta_v_mps, in.v_desired_mps - lead.v_lead_mps, 1e-9);
    EXPECT_NEAR(result.clear_distance_m, lead.gap_lead_m + in.return_clearance_m + in.ego_length_m + lead.lead_length_m, 1e-9);
}

TEST(EvaluateOvertakeTrigger, NotSlowerWhenDeltaVIsZeroOrNegative)
{
    OvertakeConfig cfg;
    cfg.enabled = true;
    OvertakeTriggerInput in   = BaselineTriggerInput();
    OvertakeLeadSample   lead = BaselineLead();  // gap_lead_m=10, constrained

    // delta_v == 0 exactly (v_desired == v_lead).
    in.v_desired_mps = lead.v_lead_mps;
    auto zero_delta  = EvaluateOvertakeTrigger(lead, in, cfg);
    EXPECT_FALSE(zero_delta.considered);
    EXPECT_EQ(zero_delta.reason, "not_slower");
    EXPECT_NEAR(zero_delta.delta_v_mps, 0.0, 1e-9);
    // t_pass_s must be forced to 0, never negative/inf, even though clear_distance_m is nonzero.
    EXPECT_NEAR(zero_delta.t_pass_s, 0.0, 1e-9);
    EXPECT_GT(zero_delta.clear_distance_m, 0.0);

    // delta_v < 0 (ego wants to go SLOWER than the lead -- overtaking is nonsensical).
    in.v_desired_mps = lead.v_lead_mps - 5.0;
    auto negative_delta = EvaluateOvertakeTrigger(lead, in, cfg);
    EXPECT_FALSE(negative_delta.considered);
    EXPECT_EQ(negative_delta.reason, "not_slower");
    EXPECT_LT(negative_delta.delta_v_mps, 0.0);
    EXPECT_NEAR(negative_delta.t_pass_s, 0.0, 1e-9);
}

TEST(EvaluateOvertakeTrigger, PassTooLongWhenEstimatedPassingTimeExceedsTheConfiguredMaximum)
{
    OvertakeConfig cfg;
    cfg.enabled         = true;
    cfg.max_pass_time_s = 10.0;
    OvertakeTriggerInput in   = BaselineTriggerInput();
    OvertakeLeadSample   lead = BaselineLead();
    // clear_distance_m = 28 (as in the baseline); pick delta_v so t_pass_s = 28/2 = 14 > 10.
    in.v_desired_mps = lead.v_lead_mps + 2.0;

    const auto result = EvaluateOvertakeTrigger(lead, in, cfg);
    EXPECT_FALSE(result.considered);
    EXPECT_EQ(result.reason, "pass_too_long");
    EXPECT_GT(result.t_pass_s, cfg.max_pass_time_s);
}

TEST(EvaluateOvertakeTrigger, ConsideredTrueWhenEveryGatePasses)
{
    OvertakeConfig cfg;
    cfg.enabled = true;
    const auto result = EvaluateOvertakeTrigger(BaselineLead(), BaselineTriggerInput(), cfg);
    EXPECT_TRUE(result.considered);
    EXPECT_EQ(result.reason, "");
    EXPECT_NEAR(result.delta_v_mps, 5.0, 1e-9);
    EXPECT_NEAR(result.clear_distance_m, 28.0, 1e-9);
    EXPECT_NEAR(result.t_pass_s, 28.0 / 5.0, 1e-9);
}

// design doc section 3-1: Δv_min = L_clear / max_pass_time_s is a DERIVED quantity, not an
// independent threshold. Probe the exact boundary from both sides: at delta_v == Δv_min,
// t_pass_s == max_pass_time_s exactly (the ">" gate does not reject equality); a hair below
// Δv_min, t_pass_s exceeds max_pass_time_s and the frame is rejected.
TEST(EvaluateOvertakeTrigger, DeltaVMinIsADerivedBoundaryNotAnIndependentThreshold)
{
    OvertakeConfig cfg;
    cfg.enabled         = true;
    cfg.max_pass_time_s = 10.0;
    OvertakeTriggerInput in   = BaselineTriggerInput();
    OvertakeLeadSample   lead = BaselineLead();  // L_clear = 10+8+5+5 = 28 -> delta_v_min = 2.8

    const double l_clear      = lead.gap_lead_m + in.return_clearance_m + in.ego_length_m + lead.lead_length_m;
    const double delta_v_min  = l_clear / cfg.max_pass_time_s;
    ASSERT_NEAR(delta_v_min, 2.8, 1e-9);

    in.v_desired_mps = lead.v_lead_mps + delta_v_min;  // exactly at the boundary
    const auto at_boundary = EvaluateOvertakeTrigger(lead, in, cfg);
    EXPECT_TRUE(at_boundary.considered) << "t_pass_s should equal max_pass_time_s exactly, not exceed it";
    EXPECT_NEAR(at_boundary.t_pass_s, cfg.max_pass_time_s, 1e-6);

    in.v_desired_mps = lead.v_lead_mps + delta_v_min - 1.0e-3;  // just inside (slower closing speed)
    const auto just_inside = EvaluateOvertakeTrigger(lead, in, cfg);
    EXPECT_FALSE(just_inside.considered);
    EXPECT_EQ(just_inside.reason, "pass_too_long");
    EXPECT_GT(just_inside.t_pass_s, cfg.max_pass_time_s);
}

// design doc section 3-2 / this feature's report item #7: idm_desired_gap_m is meant to be fed
// from the REAL lead_idm::DesiredGap (LeadVehicleAware.cpp), not a value invented independently
// here. Pin that EvaluateOvertakeTrigger's free-flow gate lands exactly on that real s* value's
// boundary -- i.e. this trigger's constrained/free-flow split is the SAME split LeadVehicleAware
// itself would draw for the identical lead, not a look-alike approximation.
TEST(EvaluateOvertakeTrigger, FreeFlowBoundaryMatchesLeadVehicleAwaresRealIdmDesiredGap)
{
    lead_idm::Params  params;  // LeadVehicleAwareConfig::idm shipped defaults
    const double      v_ego_for_idm  = 15.0;
    const double      v_lead_for_idm = 10.0;
    const double      sstar          = lead_idm::DesiredGap(params, v_ego_for_idm, v_lead_for_idm);
    ASSERT_GT(sstar, 0.0);

    OvertakeConfig cfg;
    cfg.enabled = true;

    OvertakeTriggerInput in;
    in.idm_desired_gap_m  = sstar;                                    // the REAL IDM value, not re-derived
    in.idm_follow_margin  = 1.5;                                      // LeadVehicleAwareConfig::follow_margin default
    in.v_desired_mps      = 20.0;
    in.ego_length_m       = 5.0;
    in.return_clearance_m = 8.0;

    OvertakeLeadSample lead;
    lead.has_lead      = true;
    lead.v_lead_mps     = v_lead_for_idm;
    lead.lead_length_m = 5.0;

    lead.gap_lead_m = in.idm_follow_margin * sstar;  // exactly at the boundary -> constrained side
    const auto at_boundary = EvaluateOvertakeTrigger(lead, in, cfg);
    EXPECT_NE(at_boundary.reason, "free_flow");

    lead.gap_lead_m = in.idm_follow_margin * sstar + 1.0e-3;  // just over -> free flow
    const auto just_over = EvaluateOvertakeTrigger(lead, in, cfg);
    EXPECT_EQ(just_over.reason, "free_flow");
}

// ─────────────────────────── EvaluateOvertakeRouteGuard ───────────────────────────
// design doc section 2 (formula) and section 2-3 (the <0/route_valid polarity, the easiest place
// to get backwards).

namespace
{
LaneChangeInitiationConfig OvertakeLcCfg()
{
    LaneChangeInitiationConfig cfg;  // shipped defaults: lead_time_s=6.0, min_lead_distance_m=40.0, reserve_distance_m=20.0
    return cfg;
}
}  // namespace

TEST(EvaluateOvertakeRouteGuard, NoOnwardConnectionIsAlwaysAllowed)
{
    // design doc section 2-3: dist_to_connection < 0 means "no connection to miss" -- allowed,
    // regardless of route_valid or how large n_back/v_pass/t_pass are (nothing should matter once
    // this sentinel is seen). This is the opposite polarity from ShouldSignalLaneChangeHop's own
    // <0 handling (which returns false there) -- see this feature's header comment.
    OvertakeRouteGuardInput in;
    in.route_valid        = true;
    in.dist_to_connection = -1.0;
    in.n_back             = 5;
    in.v_pass_mps         = 30.0;
    in.t_pass_s           = 20.0;
    in.hop_duration_s     = 10.0;

    const auto result = EvaluateOvertakeRouteGuard(in, OvertakeLcCfg());
    EXPECT_TRUE(result.allowed);
}

TEST(EvaluateOvertakeRouteGuard, NoValidRouteIsAlwaysAllowed)
{
    // design doc section 2-3: no route to protect -> nothing can be sacrificed for a pass. Uses a
    // NON-negative dist_to_connection to isolate this from the <0 case above.
    OvertakeRouteGuardInput in;
    in.route_valid        = false;
    in.dist_to_connection = 0.0;
    in.n_back             = 3;
    in.v_pass_mps         = 25.0;
    in.t_pass_s           = 15.0;
    in.hop_duration_s     = 8.0;

    const auto result = EvaluateOvertakeRouteGuard(in, OvertakeLcCfg());
    EXPECT_TRUE(result.allowed);
}

TEST(EvaluateOvertakeRouteGuard, RequiredDistanceDiffersByExactlyTheLaneChangeDistanceTerm)
{
    // design doc section 2-1: n_back is the hop count from the PASSING lane back to the route's
    // target lanes -- NOT the pre-overtake n_remaining. Isolate its contribution: n_back=0 makes
    // RequiredLaneChangeDistance return 0 (n_remaining<=0), so required_m(1) - required_m(0) must
    // equal RequiredLaneChangeDistance(1, v_pass, lc_cfg) exactly (everything else held fixed).
    const LaneChangeInitiationConfig lc_cfg = OvertakeLcCfg();

    OvertakeRouteGuardInput in;
    in.route_valid        = true;
    in.dist_to_connection = 10000.0;  // far away -- only required_m is under test here, not allowed
    in.v_pass_mps          = 20.0;
    in.t_pass_s            = 5.0;
    in.hop_duration_s      = 4.0;

    in.n_back = 0;
    const auto zero_back = EvaluateOvertakeRouteGuard(in, lc_cfg);

    in.n_back = 1;
    const auto one_back = EvaluateOvertakeRouteGuard(in, lc_cfg);

    const double expected_delta = RequiredLaneChangeDistance(1, in.v_pass_mps, lc_cfg);
    EXPECT_GT(expected_delta, 0.0);
    EXPECT_NEAR(one_back.required_m - zero_back.required_m, expected_delta, 1e-9);
}

TEST(EvaluateOvertakeRouteGuard, RequiredDistanceMatchesTheDesignDocFormulaAndFlipsAtTheBoundary)
{
    const LaneChangeInitiationConfig lc_cfg = OvertakeLcCfg();

    OvertakeRouteGuardInput in;
    in.route_valid    = true;
    in.n_back         = 0;  // isolates d_out + d_pass + d_back + reserve (d_route == 0)
    in.v_pass_mps     = 20.0;
    in.t_pass_s       = 5.0;
    in.hop_duration_s = 4.0;

    const double expected_required_m =
        2.0 * in.v_pass_mps * in.hop_duration_s + in.v_pass_mps * in.t_pass_s +
        RequiredLaneChangeDistance(in.n_back, in.v_pass_mps, lc_cfg) + lc_cfg.reserve_distance_m;

    in.dist_to_connection = expected_required_m;  // exactly at the boundary -> allowed
    const auto at_boundary = EvaluateOvertakeRouteGuard(in, lc_cfg);
    EXPECT_NEAR(at_boundary.required_m, expected_required_m, 1e-9);
    EXPECT_TRUE(at_boundary.allowed);

    in.dist_to_connection = expected_required_m - 1.0e-3;  // just short -> not allowed
    const auto just_short = EvaluateOvertakeRouteGuard(in, lc_cfg);
    EXPECT_FALSE(just_short.allowed);
}

// ─────────────────────────── AcceptOncomingGap ───────────────────────────
// design doc section 7-2's (v_ego + v_oncoming) closing-speed formula, deliberately NOT
// EvaluateGapAcceptance's same-direction headway formula.

TEST(AcceptOncomingGap, NoOncomingVehicleIsAlwaysAccepted)
{
    const OncomingSample sample;  // has_oncoming = false
    OvertakeConfig        cfg;
    EXPECT_TRUE(AcceptOncomingGap(sample, /*v_ego_mps=*/30.0, /*t_total_s=*/20.0, cfg));
}

TEST(AcceptOncomingGap, RequiredGapUsesTheSumOfBothClosingSpeedsAtTheExactBoundary)
{
    OvertakeConfig cfg;
    cfg.oncoming_safety_factor = 1.5;
    const double v_ego = 20.0, v_opp = 10.0, t_total = 5.0;
    const double required_gap_m = (v_ego + v_opp) * t_total * cfg.oncoming_safety_factor;  // 225.0

    OncomingSample sample;
    sample.has_oncoming   = true;
    sample.v_oncoming_mps = v_opp;

    sample.gap_m = required_gap_m;  // exactly at the boundary -> accepted
    EXPECT_TRUE(AcceptOncomingGap(sample, v_ego, t_total, cfg));

    sample.gap_m = required_gap_m - 1.0e-3;  // just short -> rejected
    EXPECT_FALSE(AcceptOncomingGap(sample, v_ego, t_total, cfg));
}

TEST(AcceptOncomingGap, HigherSafetyFactorRequiresMoreGap)
{
    OncomingSample sample;
    sample.has_oncoming   = true;
    sample.v_oncoming_mps = 10.0;
    sample.gap_m          = 230.0;
    const double v_ego = 20.0, t_total = 5.0;  // (v_ego+v_opp)*t_total = 150

    OvertakeConfig lax_cfg;
    lax_cfg.oncoming_safety_factor = 1.0;  // required = 150 <= 230 -> accepted
    EXPECT_TRUE(AcceptOncomingGap(sample, v_ego, t_total, lax_cfg));

    OvertakeConfig strict_cfg;
    strict_cfg.oncoming_safety_factor = 2.0;  // required = 300 > 230 -> rejected
    EXPECT_FALSE(AcceptOncomingGap(sample, v_ego, t_total, strict_cfg));
}

// The trap the design doc calls out (section 7-2): judging the oncoming gap by the ego's speed
// ALONE (as EvaluateGapAcceptance's forward-gap condition would, if wrongly reused) drops the
// oncoming vehicle's own closing contribution and can accept a gap the correct sum-of-speeds
// formula would reject.
TEST(AcceptOncomingGap, MustUseTheSumOfBothClosingSpeedsNotEgoSpeedAlone)
{
    OvertakeConfig cfg;
    cfg.oncoming_safety_factor = 1.0;
    OncomingSample sample;
    sample.has_oncoming   = true;
    sample.gap_m          = 50.0;
    sample.v_oncoming_mps = 25.0;  // fast oncoming vehicle
    const double v_ego = 5.0;      // slow ego
    const double t_total = 3.0;

    // The WRONG (ego-speed-alone) formula would compute required = v_ego*t_total*factor = 15.0,
    // and 50.0 >= 15.0 would ACCEPT.
    ASSERT_LT(v_ego * t_total * cfg.oncoming_safety_factor, sample.gap_m)
        << "sanity: the wrong (ego-alone) formula would have accepted this gap";

    // The CORRECT formula uses (v_ego+v_oncoming): required = 30*3*1 = 90.0, and 50.0 < 90.0 ->
    // REJECT -- exactly backwards from the wrong formula's answer.
    EXPECT_FALSE(AcceptOncomingGap(sample, v_ego, t_total, cfg));
}

// ─────────────────────────── OvertakePassingLaneId / OvertakeOpposingLaneId ───────────────────────────
// design doc section 4 (passing lane) / section 7-1 (opposing lane): one rule, no LHT/RHT
// case-split. RHT uses negative driving-lane ids, LHT uses positive ones.

TEST(OvertakePassingLaneId, RhtNegativeIdsStepTowardZero)
{
    EXPECT_EQ(OvertakePassingLaneId(-3), -2);
    EXPECT_EQ(OvertakePassingLaneId(-2), -1);
    EXPECT_EQ(OvertakePassingLaneId(-1), 0);  // already adjacent to the centerline -- no candidate
}

TEST(OvertakePassingLaneId, LhtPositiveIdsStepTowardZero)
{
    EXPECT_EQ(OvertakePassingLaneId(3), 2);
    EXPECT_EQ(OvertakePassingLaneId(2), 1);
    EXPECT_EQ(OvertakePassingLaneId(1), 0);  // already adjacent to the centerline -- no candidate
}

TEST(OvertakePassingLaneId, CenterLaneHasNoCandidate)
{
    EXPECT_EQ(OvertakePassingLaneId(0), 0);
}

TEST(OvertakeOpposingLaneId, FlipsSignForTheInnermostLaneOnEitherSide)
{
    EXPECT_EQ(OvertakeOpposingLaneId(-1), 1);  // RHT innermost -> LHT-side opposing lane
    EXPECT_EQ(OvertakeOpposingLaneId(1), -1);  // LHT innermost -> RHT-side opposing lane
}

TEST(OvertakeOpposingLaneId, NoCandidateForTheCenterLaneOrLanesNotAdjacentToIt)
{
    EXPECT_EQ(OvertakeOpposingLaneId(0), 0);
    EXPECT_EQ(OvertakeOpposingLaneId(-2), 0);
    EXPECT_EQ(OvertakeOpposingLaneId(2), 0);
}

// ─────────────────────────── HasClearedLead ───────────────────────────
// design doc section 5-1: cleared once relative_ds_m >= return_clearance_m + (ego+lead)/2.

TEST(HasClearedLead, BoundaryIsInclusive)
{
    const double return_clearance_m = 8.0, ego_length_m = 5.0, lead_length_m = 5.0;
    const double threshold = return_clearance_m + (ego_length_m + lead_length_m) / 2.0;  // 13.0

    EXPECT_TRUE(HasClearedLead(threshold, ego_length_m, lead_length_m, return_clearance_m));
    EXPECT_FALSE(HasClearedLead(threshold - 1.0e-3, ego_length_m, lead_length_m, return_clearance_m));
}

TEST(HasClearedLead, AsymmetricVehicleLengthsAreEachHalved)
{
    // A longer lead vehicle requires MORE relative_ds_m to clear (half of ITS length counts too),
    // not just half of the ego's length.
    const double return_clearance_m = 8.0, ego_length_m = 4.0;
    const double threshold_short_lead = return_clearance_m + (ego_length_m + 4.0) / 2.0;
    const double threshold_long_lead  = return_clearance_m + (ego_length_m + 12.0) / 2.0;
    EXPECT_GT(threshold_long_lead, threshold_short_lead);

    EXPECT_TRUE(HasClearedLead(threshold_long_lead, ego_length_m, 12.0, return_clearance_m));
    EXPECT_FALSE(HasClearedLead(threshold_short_lead, ego_length_m, 12.0, return_clearance_m));
}

// ─────────────────────────── SignalDwellSatisfied ───────────────────────────
// design doc section 1's timer form: "合図 -> ドウェル T 秒 -> 発起", replacing FUNC-055's
// forward-projected pre-signal for this feature specifically.

TEST(SignalDwellSatisfied, BoundaryIsInclusive)
{
    const double lead_time_s = 3.0, signal_start_time_s = 10.0;
    EXPECT_TRUE(SignalDwellSatisfied(signal_start_time_s, /*now_s=*/13.0, lead_time_s));
    EXPECT_FALSE(SignalDwellSatisfied(signal_start_time_s, /*now_s=*/13.0 - 1.0e-3, lead_time_s));
}

TEST(SignalDwellSatisfied, NotYetSignalingIsAlwaysFalse)
{
    // signal_start_time_s < 0 means "not signaling yet" -- must not be misread as "signaling since
    // the dawn of time" (which a naive `now_s - signal_start_time_s >= lead_time_s` would do, since
    // a very negative start time makes the LHS enormous and always >= lead_time_s).
    EXPECT_FALSE(SignalDwellSatisfied(/*signal_start_time_s=*/-1.0, /*now_s=*/1.0e6, /*lead_time_s=*/3.0));
}
