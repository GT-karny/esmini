// test_LaneChangeInitiation.cpp -- LaneChangeInitiation unit tests.
//
// Mirrors test_TrafficPolicies.cpp's convention: exercises the PURE numeric core (gap acceptance,
// decision distance, hop planning, the enabled-gate predicate, the arm/disarm POD) with synthetic
// inputs -- no loaded road network required. ScanAdjacentLaneGap (the one engine-dependent
// function in this layer, mirroring LeadVehicleAware::Evaluate's own split from lead_idm::) is
// exercised indirectly through ControllerVirtualDriver integration/behavioural gates instead, same
// as LeadVehicleAware::Evaluate itself has no dedicated synthetic-Entities unit test here.
#include <gtest/gtest.h>

#include "gt_esmini/control/virtualdriver/LaneChangeInitiation.hpp"

#include <algorithm>

using namespace gt_esmini;

// ─────────────────────────── LaneHopPlan ───────────────────────────

TEST(LaneHopPlan, PlansTowardTheNearestTargetLane)
{
    // -1 -> -4 (three hops needed on the right side, e.g. highway_example_with_merge_and_split.xodr).
    const LaneHopPlan hop = ComputeLaneHopPlan(-1, {-4});
    EXPECT_TRUE(hop.valid);
    EXPECT_EQ(hop.next_hop_lane_id, -2);
    EXPECT_EQ(hop.direction_step, -1);
    EXPECT_EQ(hop.n_remaining, 3);
}

TEST(LaneHopPlan, PicksTheNearestOfSeveralTargetLanes)
{
    const LaneHopPlan hop = ComputeLaneHopPlan(-2, {-4, -1});
    EXPECT_TRUE(hop.valid);
    EXPECT_EQ(hop.n_remaining, 1);  // -1 is 1 hop away, -4 is 2 hops away
    EXPECT_EQ(hop.next_hop_lane_id, -1);
    EXPECT_EQ(hop.direction_step, 1);
}

TEST(LaneHopPlan, AlreadyOnATargetLaneIsInvalid)
{
    const LaneHopPlan hop = ComputeLaneHopPlan(-3, {-3, -4});
    EXPECT_FALSE(hop.valid);
}

TEST(LaneHopPlan, EmptyTargetLanesIsInvalid)
{
    const LaneHopPlan hop = ComputeLaneHopPlan(-1, {});
    EXPECT_FALSE(hop.valid);
}

// Regression pin for a ControllerVirtualDriver.cpp telemetry bug (found by the parent's
// integration run): while a hop is EXECUTING (armed==true), telemetry_.lane_change.n_remaining/
// required_m/dist_to_connection must stay real numbers, not collapse to 0/0.0/-1.0 -- those
// fields used to be filled ONLY in the "not armed, considering a new hop" branch, so they froze
// the instant a hop armed. The fix feeds ComputeLaneHopPlan the CURRENT HOP'S TARGET lane (not
// the ego's still-mid-transition current lane) as the reference while armed, so it reports what
// remains AFTER this hop finishes -- ControllerVirtualDriver.cpp itself is not practically
// unit-constructible (see test_ControllerVirtualDriverActivation.cpp's own header comment), so
// this pins the pure-function CONTRACT the controller's armed-frame diagnostic branch now relies
// on: computing from the in-progress hop's target lane yields a real, DIFFERENT (not stale/
// zeroed) plan than computing from the pre-hop lane would have.
TEST(LaneHopPlan, DuringAnInProgressHopTheTargetLaneIsAValidReferenceForRemainingDiagnostics)
{
    // Scenario: -1 -> -4 needs 3 hops; a hop from -1 to -2 has just been armed. While it is
    // executing, the "remaining after this hop" plan must be computed from -2 (the hop's own
    // target), not from -1 (the ego's still-physically-current lane) or from a sentinel/absence.
    const int              hop_target_lane_id = -2;
    const std::vector<int> route_target_lanes = {-4};

    const LaneHopPlan during_hop = ComputeLaneHopPlan(hop_target_lane_id, route_target_lanes);
    EXPECT_TRUE(during_hop.valid);
    EXPECT_EQ(during_hop.n_remaining, 2);          // -2 -> -4 is 2 more hops
    EXPECT_EQ(during_hop.next_hop_lane_id, -3);
    EXPECT_EQ(during_hop.direction_step, -1);

    // It must differ from the PRE-hop plan (computed from -1): using the wrong (pre-hop)
    // reference is exactly the class of mistake that produced the frozen/wrong telemetry.
    const LaneHopPlan before_hop = ComputeLaneHopPlan(-1, route_target_lanes);
    EXPECT_NE(during_hop.n_remaining, before_hop.n_remaining);
    EXPECT_EQ(before_hop.n_remaining, 3);

    // required_m derived from the DURING-hop plan is a real, strictly positive number at any
    // plausible speed -- never the 0.0 the bug produced.
    LaneChangeInitiationConfig cfg;  // shipped defaults
    const double required_m = RequiredLaneChangeDistance(during_hop.n_remaining, /*v_ego=*/15.0, cfg);
    EXPECT_GT(required_m, 0.0);
}

// The final hop of a multi-hop route (armed hop's target IS the last target lane) must report
// n_remaining==0 -- not stay in a "no plan" (n_remaining==0 via absence) state indistinguishable
// from "not yet computed". Distinguishing this from the bug requires checking .valid is false
// (meaning "already there", a real answer) rather than merely reading a 0.
TEST(LaneHopPlan, TheLastHopsTargetLaneReportsNoRemainingHopsLeft)
{
    const LaneHopPlan during_last_hop = ComputeLaneHopPlan(/*hop_target_lane_id=*/-4, {-4});
    EXPECT_FALSE(during_last_hop.valid);  // already at the (only) target -- a real, meaningful answer
}

// ─────────────────────────── RequiredLaneChangeDistance ───────────────────────────
// design doc lane_change_initiation.md section 3: required_m = n_remaining *
// max(v*lead_time_s, min_lead_distance_m) + reserve_distance_m.

TEST(RequiredLaneChangeDistance, ScalesLinearlyWithRemainingHops)
{
    LaneChangeInitiationConfig cfg;
    cfg.lead_time_s         = 6.0;
    cfg.min_lead_distance_m = 40.0;
    cfg.reserve_distance_m  = 20.0;
    const double v = 10.0;  // v*lead_time_s = 60 > min_lead_distance_m -> per-hop term is 60

    const double d1 = RequiredLaneChangeDistance(1, v, cfg);
    const double d2 = RequiredLaneChangeDistance(2, v, cfg);
    const double d3 = RequiredLaneChangeDistance(3, v, cfg);

    EXPECT_NEAR(d1, 1 * 60.0 + 20.0, 1e-9);
    EXPECT_NEAR(d2, 2 * 60.0 + 20.0, 1e-9);
    EXPECT_NEAR(d3, 3 * 60.0 + 20.0, 1e-9);
    // Each additional hop adds exactly the per-hop term, not a scaled reserve.
    EXPECT_NEAR(d2 - d1, 60.0, 1e-9);
    EXPECT_NEAR(d3 - d2, 60.0, 1e-9);
}

TEST(RequiredLaneChangeDistance, UsesTheMinLeadDistanceFloorAtLowSpeed)
{
    LaneChangeInitiationConfig cfg;
    cfg.lead_time_s         = 6.0;
    cfg.min_lead_distance_m = 40.0;
    cfg.reserve_distance_m  = 20.0;
    // v*lead_time_s = 2*6 = 12 < min_lead_distance_m=40 -> the floor governs.
    EXPECT_NEAR(RequiredLaneChangeDistance(1, 2.0, cfg), 40.0 + 20.0, 1e-9);
}

TEST(RequiredLaneChangeDistance, NoRemainingHopsIsZero)
{
    LaneChangeInitiationConfig cfg;
    EXPECT_NEAR(RequiredLaneChangeDistance(0, 20.0, cfg), 0.0, 1e-9);
    EXPECT_NEAR(RequiredLaneChangeDistance(-1, 20.0, cfg), 0.0, 1e-9);
}

// The reference scenario from the design doc itself (section 3): 3 hops, defaults, dist_to_connection
// 190 m -> required_m works out to 270 m > 190 m, so the hop is due on frame 1.
TEST(RequiredLaneChangeDistance, MatchesTheDesignDocReferenceScenario)
{
    LaneChangeInitiationConfig cfg;  // shipped defaults
    const double required_m = RequiredLaneChangeDistance(/*n_remaining=*/3, /*v_ego=*/20.0, cfg);
    EXPECT_NEAR(required_m, 3.0 * std::max(20.0 * cfg.lead_time_s, cfg.min_lead_distance_m) + cfg.reserve_distance_m, 1e-9);
    EXPECT_GT(required_m, 190.0);
}

// ─────────────────────────── ShouldAttemptLaneChangeHop (enabled gate) ───────────────────────────

TEST(ShouldAttemptLaneChangeHop, DisabledNeverAttemptsRegardlessOfDistance)
{
    LaneChangeInitiationConfig cfg;  // enabled = false (default)
    ASSERT_FALSE(cfg.enabled);
    // Even with the hop overdue (dist_to_connection == 0) and a plausible n_remaining/v_ego, a
    // disabled config must never report "attempt this hop" -- this is the unit-testable half of
    // the design's "既定OFFでビット単位で不変" requirement (the controller's own
    // `if (lc_init_cfg_.enabled)` wrapper is the other half, exercised by the regression gate).
    EXPECT_FALSE(ShouldAttemptLaneChangeHop(3, 0.0, 20.0, cfg));
    EXPECT_FALSE(ShouldAttemptLaneChangeHop(1, -1.0, 20.0, cfg));
}

TEST(ShouldAttemptLaneChangeHop, EnabledAttemptsOnceWithinTheRequiredDistance)
{
    LaneChangeInitiationConfig cfg;
    cfg.enabled = true;
    const double required_m = RequiredLaneChangeDistance(1, 10.0, cfg);
    EXPECT_FALSE(ShouldAttemptLaneChangeHop(1, required_m + 1.0, 10.0, cfg));  // still far -> not yet
    EXPECT_TRUE(ShouldAttemptLaneChangeHop(1, required_m, 10.0, cfg));         // exactly at threshold -> due
    EXPECT_TRUE(ShouldAttemptLaneChangeHop(1, required_m - 1.0, 10.0, cfg));   // closer -> due
}

TEST(ShouldAttemptLaneChangeHop, UnknownDistanceIsAlwaysDue)
{
    LaneChangeInitiationConfig cfg;
    cfg.enabled = true;
    // dist_to_connection == -1.0 is RouteLaneStatus's "not applicable" (final band) convention --
    // there is no "later" to defer to.
    EXPECT_TRUE(ShouldAttemptLaneChangeHop(2, -1.0, 15.0, cfg));
}

TEST(ShouldAttemptLaneChangeHop, NoRemainingHopsNeverAttempts)
{
    LaneChangeInitiationConfig cfg;
    cfg.enabled = true;
    EXPECT_FALSE(ShouldAttemptLaneChangeHop(0, 0.0, 10.0, cfg));
}

// ─────────────────────────── ShouldSignalLaneChangeHop (design doc section 11-3) ───────────────────────────
// Note: ShouldSignalLaneChangeHop does NOT itself check cfg.enabled (see the header doc) -- unlike
// ShouldAttemptLaneChangeHop, whose enabled short-circuit is exercised above. The controller wraps
// the whole LC block (including its call to this function) in `if (lc_init_cfg_.enabled)`, so the
// disabled-never-signals invariant is exercised at that call site instead (not unit-testable here
// without the controller). Every test below therefore uses cfg.enabled = true (or leaves it at its
// irrelevant default) -- it is testing the DISTANCE predicate, not the feature gate.

TEST(ShouldSignalLaneChangeHop, LeadsTheAttemptThresholdBySomeDistance)
{
    // At the exact distance ShouldAttemptLaneChangeHop first turns true, the pre-signal must
    // ALREADY have been true for the preceding indicator_lead_time_s * v_ego of travel: i.e. there
    // exists a distance strictly beyond required_m (attempt not yet due) where ShouldSignal is true
    // and ShouldAttempt is false.
    LaneChangeInitiationConfig cfg;
    cfg.enabled              = true;
    cfg.indicator_lead_time_s = 3.0;
    const int    n_remaining = 1;
    const double v_ego       = 10.0;  // lead distance = 3.0 * 10.0 = 30 m
    const double required_m  = RequiredLaneChangeDistance(n_remaining, v_ego, cfg);

    const double probe = required_m + 15.0;  // inside the 30 m lead band, outside required_m
    EXPECT_FALSE(ShouldAttemptLaneChangeHop(n_remaining, probe, v_ego, cfg));
    EXPECT_TRUE(ShouldSignalLaneChangeHop(n_remaining, probe, v_ego, cfg));
}

TEST(ShouldSignalLaneChangeHop, AttemptDueImpliesSignalDue)
{
    // Containment: at every distance ShouldAttemptLaneChangeHop is true (due), ShouldSignal must
    // also be true (the pre-signal band strictly contains the attempt band, since it is required_m
    // plus a non-negative lead term).
    LaneChangeInitiationConfig cfg;
    cfg.enabled = true;
    const int    n_remaining = 2;
    const double v_ego       = 15.0;
    const double required_m  = RequiredLaneChangeDistance(n_remaining, v_ego, cfg);

    for (double dist : {required_m, required_m - 1.0, 0.0})
    {
        ASSERT_TRUE(ShouldAttemptLaneChangeHop(n_remaining, dist, v_ego, cfg));
        EXPECT_TRUE(ShouldSignalLaneChangeHop(n_remaining, dist, v_ego, cfg))
            << "at dist_to_connection=" << dist;
    }
}

TEST(ShouldSignalLaneChangeHop, UnknownDistanceNeverSignals)
{
    // design doc section 11-3's explicit trap: dist_to_connection == -1.0 (RouteLaneStatus's
    // "unknown / final band has no onward connection" convention) must be false here, in CONTRAST
    // to ShouldAttemptLaneChangeHop, which treats the same -1.0 as always-due (see that function's
    // UnknownDistanceIsAlwaysDue test above). A naive `<=` against a negative sentinel is always
    // true and would latch the indicator on forever in the final band.
    LaneChangeInitiationConfig cfg;
    cfg.enabled = true;
    EXPECT_FALSE(ShouldSignalLaneChangeHop(2, -1.0, 15.0, cfg));
    ASSERT_TRUE(ShouldAttemptLaneChangeHop(2, -1.0, 15.0, cfg));  // contrast: attempt says "due"
}

TEST(ShouldSignalLaneChangeHop, NoRemainingHopsNeverSignals)
{
    LaneChangeInitiationConfig cfg;
    cfg.enabled = true;
    EXPECT_FALSE(ShouldSignalLaneChangeHop(0, 0.0, 10.0, cfg));
}

TEST(ShouldSignalLaneChangeHop, ZeroSpeedStillSignalsOnceWithinTheDistanceFloor)
{
    // v_ego == 0 collapses the lead-distance term (v_ego * indicator_lead_time_s == 0), so the
    // pre-signal threshold degenerates to exactly required_m (itself floored by
    // min_lead_distance_m, per RequiredLaneChangeDistance) -- it must not divide-by-zero, go
    // negative, or otherwise misbehave; it must simply equal the attempt threshold in this case.
    LaneChangeInitiationConfig cfg;
    cfg.enabled               = true;
    cfg.indicator_lead_time_s = 3.0;
    const int    n_remaining  = 1;
    const double v_ego        = 0.0;
    const double required_m   = RequiredLaneChangeDistance(n_remaining, v_ego, cfg);  // floored by min_lead_distance_m

    EXPECT_TRUE(ShouldSignalLaneChangeHop(n_remaining, required_m, v_ego, cfg));
    EXPECT_TRUE(ShouldAttemptLaneChangeHop(n_remaining, required_m, v_ego, cfg));
    EXPECT_FALSE(ShouldSignalLaneChangeHop(n_remaining, required_m + 1.0, v_ego, cfg));
}

// ─────────────────────────── EvaluateGapAcceptance ───────────────────────────
// design doc lane_change_initiation.md section 4's three conditions, each independently.

namespace
{
LaneChangeInitiationConfig GapCfg()
{
    LaneChangeInitiationConfig cfg;
    cfg.gap_min_m           = 8.0;
    cfg.gap_headway_lead_s  = 1.2;
    cfg.gap_headway_rear_s  = 1.0;
    cfg.gap_ttc_min_s       = 3.0;
    return cfg;
}
}  // namespace

TEST(EvaluateGapAcceptance, NoVehiclesInEitherDirectionIsAlwaysAccepted)
{
    const LaneChangeGapSample gap;  // has_lead=false, has_rear=false
    const auto result = EvaluateGapAcceptance(gap, 15.0, GapCfg());
    EXPECT_TRUE(result.accepted);
    EXPECT_EQ(result.reason, "");
}

TEST(EvaluateGapAcceptance, ForwardGapBoundary)
{
    const LaneChangeInitiationConfig cfg   = GapCfg();
    const double                     v_ego = 20.0;  // required_lead = max(8, 20*1.2) = 24
    LaneChangeGapSample              gap;
    gap.has_lead = true;

    gap.gap_lead_m = 24.0;  // exactly at the boundary -> accepted
    EXPECT_TRUE(EvaluateGapAcceptance(gap, v_ego, cfg).accepted);

    gap.gap_lead_m = 23.999;  // just short -> rejected
    const auto rejected = EvaluateGapAcceptance(gap, v_ego, cfg);
    EXPECT_FALSE(rejected.accepted);
    EXPECT_EQ(rejected.reason, "lead_gap");
}

TEST(EvaluateGapAcceptance, ForwardGapFloorsAtGapMinAtLowSpeed)
{
    const LaneChangeInitiationConfig cfg = GapCfg();
    LaneChangeGapSample               gap;
    gap.has_lead   = true;
    gap.gap_lead_m = 7.0;  // below gap_min_m=8 even though v*headway would be tiny
    EXPECT_FALSE(EvaluateGapAcceptance(gap, 1.0, cfg).accepted);
    gap.gap_lead_m = 8.0;
    EXPECT_TRUE(EvaluateGapAcceptance(gap, 1.0, cfg).accepted);
}

TEST(EvaluateGapAcceptance, RearGapBoundary)
{
    const LaneChangeInitiationConfig cfg = GapCfg();
    LaneChangeGapSample               gap;
    gap.has_rear   = true;
    gap.v_rear_mps = 10.0;  // slower than nothing to worry about TTC-wise if v_ego is high enough
    // required_rear = max(8, 10*1.0) = 10
    gap.gap_rear_m = 10.0;
    EXPECT_TRUE(EvaluateGapAcceptance(gap, /*v_ego=*/15.0, cfg).accepted);  // rear NOT closing (v_rear<v_ego)

    gap.gap_rear_m = 9.999;
    const auto rejected = EvaluateGapAcceptance(gap, 15.0, cfg);
    EXPECT_FALSE(rejected.accepted);
    EXPECT_EQ(rejected.reason, "rear_gap");
}

TEST(EvaluateGapAcceptance, RearTtcBoundaryWhenFollowerIsClosing)
{
    const LaneChangeInitiationConfig cfg = GapCfg();  // gap_ttc_min_s = 3.0
    LaneChangeGapSample               gap;
    gap.has_rear   = true;
    gap.v_rear_mps = 20.0;
    const double v_ego = 10.0;  // closing = 10 m/s
    // required_rear = max(8, 20*1.0) = 20; pick gap_rear_m well above that so only TTC governs.
    // ttc = gap_rear_m / closing = 3.0 at the boundary -> gap_rear_m = 30.
    gap.gap_rear_m = 30.0;
    EXPECT_TRUE(EvaluateGapAcceptance(gap, v_ego, cfg).accepted);

    gap.gap_rear_m = 29.9;  // ttc just under 3.0s -> rejected
    const auto rejected = EvaluateGapAcceptance(gap, v_ego, cfg);
    EXPECT_FALSE(rejected.accepted);
    EXPECT_EQ(rejected.reason, "rear_ttc");
}

TEST(EvaluateGapAcceptance, TtcIsSkippedWhenTheFollowerIsNotClosing)
{
    const LaneChangeInitiationConfig cfg = GapCfg();
    LaneChangeGapSample               gap;
    gap.has_rear   = true;
    gap.v_rear_mps = 5.0;   // slower than ego -> can never close the gap
    gap.gap_rear_m = 20.0;  // >= required_rear = max(8, 5*1.0)=8, so the gap condition alone passes
    // If TTC were (wrongly) evaluated anyway with a non-positive/huge denominator this would be
    // either a crash (div by ~0) or a spurious reject; it must simply not run.
    EXPECT_TRUE(EvaluateGapAcceptance(gap, /*v_ego=*/25.0, cfg).accepted);
}

// The trap the design doc calls out explicitly (section 4): "後方ギャップに後続車の速度を使うのは
// ... 自車速度で測ると ... 危険側へ倒れる". Construct a case where judging the REAR GAP by the
// ego's (slow) speed would wrongly ACCEPT a gap that is actually too short for a MUCH faster
// follower closing on it -- the correct (rear-speed) computation must REJECT it.
TEST(EvaluateGapAcceptance, RearGapMustUseTheFollowersSpeedNotTheEgosSpeed)
{
    const LaneChangeInitiationConfig cfg = GapCfg();  // gap_min_m=8, headway_rear_s=1.0
    LaneChangeGapSample               gap;
    gap.has_rear   = true;
    gap.gap_rear_m = 15.0;
    gap.v_rear_mps = 20.0;  // fast-closing follower
    const double v_ego = 5.0;  // ego itself is slow

    // The WRONG formula (judging the rear gap against the ego's own speed) would compute
    // required_rear = max(8, v_ego*headway_rear_s) = max(8, 5.0) = 8.0, and 15.0 >= 8.0 would
    // ACCEPT -- exactly backwards, since it is the FOLLOWER's closing speed that determines how
    // fast the 15 m gap disappears, not the ego's.
    ASSERT_LT(std::max(cfg.gap_min_m, v_ego * cfg.gap_headway_rear_s), gap.gap_rear_m)
        << "sanity: the wrong (ego-speed) formula would have accepted this gap";

    // The CORRECT formula uses v_rear_mps: required_rear = max(8, 20*1.0) = 20.0, and 15.0 < 20.0
    // -> REJECT.
    const auto result = EvaluateGapAcceptance(gap, v_ego, cfg);
    EXPECT_FALSE(result.accepted);
    EXPECT_EQ(result.reason, "rear_gap");
}

// ─────────────────────────── LaneChangeInitiationState arm/disarm ───────────────────────────

TEST(LaneChangeInitiationState, ArmSetsAllFieldsAndClearsTheGapReason)
{
    LaneChangeInitiationState state;
    state.last_gap_reason = "lead_gap";  // stale from a previous rejected attempt

    ArmLaneChangeHop(state, /*hop_track_id=*/7, /*hop_target_lane_id=*/-2, /*direction_step=*/-1,
                     /*direction_indicator=*/-1);

    EXPECT_TRUE(state.armed);
    EXPECT_EQ(state.hop_track_id, 7u);
    EXPECT_EQ(state.hop_target_lane_id, -2);
    EXPECT_EQ(state.direction_step, -1);
    EXPECT_EQ(state.direction_indicator, -1);
    EXPECT_EQ(state.last_gap_reason, "");
}

TEST(LaneChangeInitiationState, DisarmClearsOnlyTheArmedFlag)
{
    LaneChangeInitiationState state;
    ArmLaneChangeHop(state, 7, -2, -1, -1);

    DisarmLaneChangeHop(state);

    EXPECT_FALSE(state.armed);
    // The rest is kept as a "what was this hop" breadcrumb (mirrors DisarmResumeMerge).
    EXPECT_EQ(state.hop_track_id, 7u);
    EXPECT_EQ(state.hop_target_lane_id, -2);
}

TEST(LaneChangeInitiationState, DefaultConstructedIsNotArmed)
{
    const LaneChangeInitiationState state;
    EXPECT_FALSE(state.armed);
    EXPECT_EQ(state.direction_indicator, 0);
}
