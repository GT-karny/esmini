// req-vd-ad:REQ-AD-025 / req-vd-ad:REQ-AD-028 / vd-func:FUNC-075
//
// Unit tests for AdasCoexistenceStack's pure decision core
// (ComputeManualAdasFrame / ComputeMeasuredDecel / DeriveFcwGateConfig).
// AdasCoexistenceStack itself needs real esmini Object*/Entities* (via
// AebSafety::Evaluate()), so it is deliberately NOT exercised here -- see
// AdasCoexistenceStack.hpp's "TWO-LAYER SPLIT" comment. Every test below
// drives the pure function/helpers directly with hand-built
// TrafficPolicySnapshot/PedalSteerCommand values, no engine involved.
//
// PHASE A, STEP 1 (RED): this file is the FULL test suite against the FINAL
// header contract. It is expected to fail against AdasCoexistenceStack.cpp's
// step-1 stubs (which ignore every input and return all-zero/false/empty
// results) -- see that file's header comment for exactly which assertions
// happen to hold vacuously anyway. Step 2 replaces the stub bodies; no
// changes to this file should be needed at that point.

#include <gtest/gtest.h>

#include "gt_esmini/control/manualdrive/AdasCoexistenceStack.hpp"
#include "gt_esmini/control/virtualdriver/PolicyDetail.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <string>

namespace gt_esmini
{
namespace
{

PolicyConstraint MakeAebConstraint(double s)
{
    PolicyConstraint c;
    c.kind   = PolicyConstraint::Kind::STOP_AT_S;
    c.tier   = PolicyConstraint::Tier::SAFETY;
    c.source = "aeb";
    c.s      = s;
    c.value  = 0.0;
    return c;
}

// A snapshot carrying exactly one qualifying AEB constraint at distance `s`
// -- mirrors what AebSafety::Evaluate() emits on gate.triggered (see
// AebSafety.cpp's emit block).
TrafficPolicySnapshot MakeSnapshotWithConstraint(double s)
{
    TrafficPolicySnapshot snap;
    snap.constraints.push_back(MakeAebConstraint(s));
    snap.valid = true;
    return snap;
}

TrafficPolicySnapshot EmptySnapshot()
{
    return TrafficPolicySnapshot{};
}

PedalSteerCommand MakeDriverCmd(double throttle, double brake)
{
    PedalSteerCommand cmd;
    cmd.throttle = throttle;
    cmd.brake    = brake;
    return cmd;
}

bool DetailHas(const PolicyDetail& detail, const std::string& key, const std::string& value)
{
    return std::any_of(detail.begin(), detail.end(),
                        [&](const auto& kv) { return kv.first == key && kv.second == value; });
}

// Parses a numeric detail value back out (AddDetail's "%.3f" format). Used by
// the brake-composition tests below, which need to check a numeric relation
// (max), not just presence/exact-string equality.
bool TryGetDetailValue(const PolicyDetail& detail, const std::string& key, double& out)
{
    for (const auto& kv : detail)
    {
        if (kv.first == key)
        {
            out = std::stod(kv.second);
            return true;
        }
    }
    return false;
}

}  // namespace

// ============================================================================
// Domain / config authority -- pure pass-through, no arbitration at all
// ============================================================================

TEST(AdasCoexistenceStackTest, AebDisabledInConfigPassesDriverPedalsThroughAndDecisionAllFalse)
{
    ManualAdasStackConfig cfg;
    cfg.aeb_enabled = false;

    KickdownDetector detector(cfg.kickdown);
    PedalArbitrator  arb(cfg.arbitrator);

    // A firing constraint present in BOTH snapshots must still be ignored --
    // config authority: disabled must behave exactly like unowned, not
    // "usually zero". BOTH throttle and brake are non-zero: a brake of 0.0
    // here would make this assertion pass even against a broken
    // implementation that just returns a zeroed PedalArbitrationSnapshot
    // (indistinguishable from correct pass-through when driver_cmd.brake==0)
    // -- exactly the vacuous-pass failure mode the coordinator flagged.
    const TrafficPolicySnapshot intervention = MakeSnapshotWithConstraint(2.0);
    const TrafficPolicySnapshot warning       = MakeSnapshotWithConstraint(2.0);
    const PedalSteerCommand     driver_cmd    = MakeDriverCmd(0.3, 0.45);

    const auto result = ComputeManualAdasFrame(cfg, /*owns_longitudinal=*/true, intervention, warning, driver_cmd,
                                                /*ego_speed_mps=*/20.0, /*measured_decel_mps2=*/0.0, /*dt=*/0.02,
                                                detector, arb);

    EXPECT_EQ(result.pedals.throttle_out, driver_cmd.throttle);
    EXPECT_EQ(result.pedals.brake_out, driver_cmd.brake);
    EXPECT_FALSE(result.pedals.aeb_engaged);
    EXPECT_FALSE(result.pedals.aeb_suppressed);
    EXPECT_FALSE(result.decision.aeb_intervening);
    EXPECT_FALSE(result.decision.fcw_warning);
}

TEST(AdasCoexistenceStackTest, NotOwningLongitudinalDomainPassesThroughEvenWithFiringConstraint)
{
    ManualAdasStackConfig cfg;
    cfg.aeb_enabled = true;  // feature ON, but this instance does not own the domain

    KickdownDetector detector(cfg.kickdown);
    PedalArbitrator  arb(cfg.arbitrator);

    const TrafficPolicySnapshot intervention = MakeSnapshotWithConstraint(2.0);  // would otherwise fire hard
    const TrafficPolicySnapshot warning       = MakeSnapshotWithConstraint(2.0);
    // Non-zero brake too -- see the analogous note in the AebDisabled test
    // above: a zero driver_cmd.brake would make this pass even against a
    // stub that just zeroes the whole PedalArbitrationSnapshot.
    const PedalSteerCommand     driver_cmd    = MakeDriverCmd(0.5, 0.2);

    const auto result = ComputeManualAdasFrame(cfg, /*owns_longitudinal=*/false, intervention, warning, driver_cmd,
                                                /*ego_speed_mps=*/20.0, /*measured_decel_mps2=*/0.0, /*dt=*/0.02,
                                                detector, arb);

    EXPECT_EQ(result.pedals.throttle_out, driver_cmd.throttle);
    EXPECT_EQ(result.pedals.brake_out, driver_cmd.brake);
    EXPECT_FALSE(result.pedals.aeb_engaged);
    EXPECT_FALSE(result.decision.aeb_intervening);
    EXPECT_FALSE(result.decision.fcw_warning);
}

// ============================================================================
// a_req = v^2 / (2*d) derivation
// ============================================================================

TEST(AdasCoexistenceStackTest, QualifyingAebConstraintProducesExactRequiredDeceleration)
{
    ManualAdasStackConfig cfg;
    cfg.aeb_enabled = true;

    KickdownDetector detector(cfg.kickdown);
    PedalArbitrator  arb(cfg.arbitrator);

    const double v = 15.0;
    const double d = 20.0;  // well above kMinStopDistanceM
    const TrafficPolicySnapshot intervention = MakeSnapshotWithConstraint(d);
    const TrafficPolicySnapshot warning       = EmptySnapshot();
    const PedalSteerCommand     driver_cmd;  // quiet driver

    const auto result = ComputeManualAdasFrame(cfg, /*owns_longitudinal=*/true, intervention, warning, driver_cmd,
                                                v, /*measured_decel_mps2=*/0.0, /*dt=*/0.02, detector, arb);

    const double expected = (v * v) / (2.0 * d);
    EXPECT_NEAR(result.aeb_decel_request_mps2, expected, 1e-9);
    EXPECT_TRUE(result.decision.aeb_intervening);
}

TEST(AdasCoexistenceStackTest, DistanceAtOrBelowEpsilonSaturatesInsteadOfDividingByNearZero)
{
    ManualAdasStackConfig cfg;
    cfg.aeb_enabled = true;

    KickdownDetector detector(cfg.kickdown);
    PedalArbitrator  arb(cfg.arbitrator);

    // gap fully consumed by stop_margin -> AebSafety would clamp s to 0.0.
    const TrafficPolicySnapshot intervention = MakeSnapshotWithConstraint(0.0);
    const TrafficPolicySnapshot warning       = EmptySnapshot();
    const PedalSteerCommand     driver_cmd;

    const auto result = ComputeManualAdasFrame(cfg, /*owns_longitudinal=*/true, intervention, warning, driver_cmd,
                                                /*ego_speed_mps=*/10.0, /*measured_decel_mps2=*/0.0, /*dt=*/0.02,
                                                detector, arb);

    ASSERT_TRUE(std::isfinite(result.aeb_decel_request_mps2));
    // The saturating request must be strong enough to pin PedalArbitrator's
    // feedforward at full brake: ff = clamp(aeb_decel/full_brake, 0, 1).
    EXPECT_GE(result.aeb_decel_request_mps2, cfg.arbitrator.full_brake_decel_mps2);
    EXPECT_NEAR(result.pedals.brake_out, 1.0, 1e-6);
}

TEST(AdasCoexistenceStackTest, ZeroEgoSpeedProducesNoRequest)
{
    ManualAdasStackConfig cfg;
    cfg.aeb_enabled = true;

    KickdownDetector detector(cfg.kickdown);
    PedalArbitrator  arb(cfg.arbitrator);

    const TrafficPolicySnapshot intervention = MakeSnapshotWithConstraint(5.0);
    const TrafficPolicySnapshot warning       = EmptySnapshot();
    const PedalSteerCommand     driver_cmd    = MakeDriverCmd(0.0, 0.0);

    const auto result = ComputeManualAdasFrame(cfg, /*owns_longitudinal=*/true, intervention, warning, driver_cmd,
                                                /*ego_speed_mps=*/0.0, /*measured_decel_mps2=*/0.0, /*dt=*/0.02,
                                                detector, arb);

    EXPECT_EQ(result.aeb_decel_request_mps2, 0.0);
    EXPECT_FALSE(result.decision.aeb_intervening);
    EXPECT_FALSE(result.pedals.aeb_engaged);
}

TEST(AdasCoexistenceStackTest, ConstraintsWithWrongTierSourceOrKindAreIgnored)
{
    ManualAdasStackConfig cfg;
    cfg.aeb_enabled = true;

    KickdownDetector detector(cfg.kickdown);
    PedalArbitrator  arb(cfg.arbitrator);

    PolicyConstraint wrong_tier = MakeAebConstraint(5.0);
    wrong_tier.tier             = PolicyConstraint::Tier::COMFORT;

    PolicyConstraint wrong_source = MakeAebConstraint(5.0);
    wrong_source.source           = "lead_vehicle";

    PolicyConstraint wrong_kind = MakeAebConstraint(5.0);
    wrong_kind.kind             = PolicyConstraint::Kind::MAX_SPEED;

    TrafficPolicySnapshot intervention;
    intervention.constraints = {wrong_tier, wrong_source, wrong_kind};
    intervention.valid       = true;

    const TrafficPolicySnapshot warning    = EmptySnapshot();
    const PedalSteerCommand     driver_cmd = MakeDriverCmd(0.4, 0.0);

    const auto result = ComputeManualAdasFrame(cfg, /*owns_longitudinal=*/true, intervention, warning, driver_cmd,
                                                /*ego_speed_mps=*/10.0, /*measured_decel_mps2=*/0.0, /*dt=*/0.02,
                                                detector, arb);

    EXPECT_EQ(result.aeb_decel_request_mps2, 0.0);
    EXPECT_FALSE(result.decision.aeb_intervening);
    EXPECT_EQ(result.pedals.throttle_out, driver_cmd.throttle);
    EXPECT_EQ(result.pedals.brake_out, driver_cmd.brake);
}

TEST(AdasCoexistenceStackTest, SeveralQualifyingConstraintsTheStrictestWins)
{
    ManualAdasStackConfig cfg;
    cfg.aeb_enabled = true;

    KickdownDetector detector(cfg.kickdown);
    PedalArbitrator  arb(cfg.arbitrator);

    const double v = 12.0;
    TrafficPolicySnapshot intervention;
    intervention.constraints = {MakeAebConstraint(30.0), MakeAebConstraint(10.0), MakeAebConstraint(20.0)};
    intervention.valid       = true;
    const TrafficPolicySnapshot warning    = EmptySnapshot();
    const PedalSteerCommand     driver_cmd;

    const auto result = ComputeManualAdasFrame(cfg, /*owns_longitudinal=*/true, intervention, warning, driver_cmd,
                                                v, /*measured_decel_mps2=*/0.0, /*dt=*/0.02, detector, arb);

    // Smallest d (10.0) is the strictest -- largest a_req.
    const double expected = (v * v) / (2.0 * 10.0);
    EXPECT_NEAR(result.aeb_decel_request_mps2, expected, 1e-9);
}

// ============================================================================
// The superset property: aeb_intervening => fcw_warning
// ============================================================================

TEST(AdasCoexistenceStackTest, SupersetPropertyHoldsAcrossAllFourFireCombinations)
{
    ManualAdasStackConfig cfg;
    cfg.aeb_enabled = true;

    const TrafficPolicySnapshot fired  = MakeSnapshotWithConstraint(10.0);
    const TrafficPolicySnapshot silent = EmptySnapshot();

    // An implication (aeb_intervening => fcw_warning) is a vacuous check if
    // the antecedent is never true across the sweep -- exactly what happened
    // against the step-1 stub (which never intervenes). Track whether the
    // (intervention_fires=true, ...) combinations actually produced
    // aeb_intervening==true so this test cannot silently regress back to
    // vacuous once a real implementation exists.
    bool observed_intervening = false;

    for (bool intervention_fires : {false, true})
    {
        for (bool warning_fires : {false, true})
        {
            KickdownDetector detector(cfg.kickdown);
            PedalArbitrator  arb(cfg.arbitrator);

            const TrafficPolicySnapshot& intervention = intervention_fires ? fired : silent;
            const TrafficPolicySnapshot& warning       = warning_fires ? fired : silent;
            // v=10 (not the default-constructed 0.0): a request derives from
            // v>0, so the intervening branch is actually reachable -- see the
            // "no request when v<=0" rule.
            const PedalSteerCommand      driver_cmd;

            const auto result = ComputeManualAdasFrame(cfg, /*owns_longitudinal=*/true, intervention, warning,
                                                        driver_cmd, /*ego_speed_mps=*/10.0,
                                                        /*measured_decel_mps2=*/0.0, /*dt=*/0.02, detector, arb);

            if (result.decision.aeb_intervening)
            {
                observed_intervening = true;
                EXPECT_TRUE(result.decision.fcw_warning)
                    << "intervention_fires=" << intervention_fires << " warning_fires=" << warning_fires;
            }
        }
    }

    EXPECT_TRUE(observed_intervening)
        << "the sweep never produced aeb_intervening==true -- the implication above was checked "
           "vacuously (antecedent never satisfied), which proves nothing about the superset property";
}

TEST(AdasCoexistenceStackTest, InterventionWithoutWarningSnapshotIsRepairedNotTrusted)
{
    // A caller could hand in an inconsistent pair of snapshots where
    // intervention fired but the warning snapshot did not. Physically this
    // cannot happen when both AebSafety instances see the same candidate
    // (DeriveFcwGateConfig's looseness guarantees the warning gate is at
    // least as easy to trip as the intervention gate), so the SAFE choice on
    // an inconsistent input is to REPAIR: an actual intervention always
    // implies the warning flag, regardless of what the (buggy/inconsistent)
    // warning snapshot itself says. The alternative -- trusting the
    // inconsistent snapshot -- would let a real brake application go
    // unreported as a warning to face-3/UI consumers, which is worse than an
    // occasional over-eager warning.
    ManualAdasStackConfig cfg;
    cfg.aeb_enabled = true;

    KickdownDetector detector(cfg.kickdown);
    PedalArbitrator  arb(cfg.arbitrator);

    const TrafficPolicySnapshot intervention = MakeSnapshotWithConstraint(10.0);
    const TrafficPolicySnapshot warning       = EmptySnapshot();  // inconsistent: claims nothing
    const PedalSteerCommand     driver_cmd;

    const auto result = ComputeManualAdasFrame(cfg, /*owns_longitudinal=*/true, intervention, warning, driver_cmd,
                                                /*ego_speed_mps=*/10.0, /*measured_decel_mps2=*/0.0, /*dt=*/0.02,
                                                detector, arb);

    EXPECT_TRUE(result.decision.aeb_intervening);
    EXPECT_TRUE(result.decision.fcw_warning);
}

TEST(AdasCoexistenceStackTest, WarningOnlyFrameSetsFcwWithoutInterveningAndLeavesPedalsUntouched)
{
    ManualAdasStackConfig cfg;
    cfg.aeb_enabled = true;

    KickdownDetector detector(cfg.kickdown);
    PedalArbitrator  arb(cfg.arbitrator);

    const TrafficPolicySnapshot intervention = EmptySnapshot();               // tighter gate did NOT fire
    const TrafficPolicySnapshot warning       = MakeSnapshotWithConstraint(15.0);  // looser FCW gate did
    const PedalSteerCommand     driver_cmd    = MakeDriverCmd(0.2, 0.0);

    const auto result = ComputeManualAdasFrame(cfg, /*owns_longitudinal=*/true, intervention, warning, driver_cmd,
                                                /*ego_speed_mps=*/10.0, /*measured_decel_mps2=*/0.0, /*dt=*/0.02,
                                                detector, arb);

    EXPECT_TRUE(result.decision.fcw_warning);
    EXPECT_FALSE(result.decision.aeb_intervening);
    EXPECT_EQ(result.pedals.throttle_out, driver_cmd.throttle);
    EXPECT_EQ(result.pedals.brake_out, driver_cmd.brake);
    EXPECT_FALSE(result.pedals.aeb_engaged);
}

// ============================================================================
// Kickdown suppression (shared KickdownDetector)
// ============================================================================

TEST(AdasCoexistenceStackTest, KickdownActiveWithSuppressEnabledSuppressesAeb)
{
    ManualAdasStackConfig cfg;
    cfg.aeb_enabled               = true;
    cfg.kickdown_suppress_enabled = true;

    KickdownDetector detector(cfg.kickdown);
    PedalArbitrator  arb(cfg.arbitrator);

    const TrafficPolicySnapshot intervention = MakeSnapshotWithConstraint(10.0);
    const TrafficPolicySnapshot warning       = EmptySnapshot();
    const PedalSteerCommand     driver_cmd    = MakeDriverCmd(0.99, 0.0);  // floored, above engage threshold

    const auto result = ComputeManualAdasFrame(cfg, /*owns_longitudinal=*/true, intervention, warning, driver_cmd,
                                                /*ego_speed_mps=*/15.0, /*measured_decel_mps2=*/0.0, /*dt=*/0.02,
                                                detector, arb);

    EXPECT_TRUE(result.pedals.aeb_suppressed);
    EXPECT_FALSE(result.pedals.aeb_engaged);
    EXPECT_EQ(result.pedals.throttle_out, driver_cmd.throttle);
    EXPECT_EQ(result.pedals.brake_out, driver_cmd.brake);
    EXPECT_TRUE(DetailHas(result.detail, "gt.aeb.suppressed", "true"));
}

TEST(AdasCoexistenceStackTest, KickdownSuppressDisabledIgnoresKickdownAebStillIntervenes)
{
    ManualAdasStackConfig cfg;
    cfg.aeb_enabled               = true;
    cfg.kickdown_suppress_enabled = false;

    KickdownDetector detector(cfg.kickdown);
    PedalArbitrator  arb(cfg.arbitrator);

    const TrafficPolicySnapshot intervention = MakeSnapshotWithConstraint(10.0);
    const TrafficPolicySnapshot warning       = EmptySnapshot();
    const PedalSteerCommand     driver_cmd    = MakeDriverCmd(0.99, 0.0);  // floored -- detector would latch active

    const auto result = ComputeManualAdasFrame(cfg, /*owns_longitudinal=*/true, intervention, warning, driver_cmd,
                                                /*ego_speed_mps=*/15.0, /*measured_decel_mps2=*/0.0, /*dt=*/0.02,
                                                detector, arb);

    EXPECT_FALSE(result.pedals.aeb_suppressed);
    EXPECT_TRUE(result.pedals.aeb_engaged);
    EXPECT_GT(result.pedals.brake_out, 0.0);
    // The shared detector's RAW verdict must still be observable even though
    // suppression is configured off -- MSL (phase C) reads the same signal.
    EXPECT_TRUE(DetailHas(result.detail, "gt.aeb.kickdown", "true"));
}

// ============================================================================
// Detail merge rule
// ============================================================================

TEST(AdasCoexistenceStackTest, ResultDetailHasNoDuplicateKeysAndPreservesAebSafetyKeysVerbatim)
{
    ManualAdasStackConfig cfg;
    cfg.aeb_enabled = true;

    KickdownDetector detector(cfg.kickdown);
    PedalArbitrator  arb(cfg.arbitrator);

    TrafficPolicySnapshot intervention = MakeSnapshotWithConstraint(10.0);
    // Mirrors AebSafety::Evaluate()'s own W3 diagnostics (AebSafety.cpp).
    AddDetail(intervention.detail, "gt.aeb.gap_m", 12.000);
    AddDetail(intervention.detail, "gt.aeb.v_close_mps", 2.000);
    AddDetail(intervention.detail, "gt.aeb.lead_osi_id", 3);
    AddDetail(intervention.detail, "gt.aeb.ttc_s", 1.234);
    AddDetail(intervention.detail, "gt.aeb.a_req_mps2", 5.000);
    AddDetail(intervention.detail, "gt.aeb.triggered", true);

    const TrafficPolicySnapshot warning    = EmptySnapshot();
    const PedalSteerCommand     driver_cmd;

    const auto result = ComputeManualAdasFrame(cfg, /*owns_longitudinal=*/true, intervention, warning, driver_cmd,
                                                /*ego_speed_mps=*/10.0, /*measured_decel_mps2=*/0.0, /*dt=*/0.02,
                                                detector, arb);

    std::set<std::string> keys;
    for (const auto& kv : result.detail)
    {
        EXPECT_TRUE(keys.insert(kv.first).second) << "duplicate key: " << kv.first;
    }

    EXPECT_TRUE(DetailHas(result.detail, "gt.aeb.gap_m", "12.000"));
    EXPECT_TRUE(DetailHas(result.detail, "gt.aeb.v_close_mps", "2.000"));
    EXPECT_TRUE(DetailHas(result.detail, "gt.aeb.lead_osi_id", "3"));
    EXPECT_TRUE(DetailHas(result.detail, "gt.aeb.ttc_s", "1.234"));
    EXPECT_TRUE(DetailHas(result.detail, "gt.aeb.a_req_mps2", "5.000"));
    EXPECT_TRUE(DetailHas(result.detail, "gt.aeb.triggered", "true"));
}

// ============================================================================
// Observable brake composition (gt.aeb.driver_brake / gt.aeb.brake_out) --
// needed by the `brake_not_stacked` E2E matcher (verification plan §4-2,
// req-vd-ad:REQ-AD-025 step c) to reconstruct "was the human's brake topped
// up or kept" from the OSI stream alone, without re-reading the input
// profile file. See AdasCoexistenceStack.hpp's "RESULT DETAIL" block.
// ============================================================================

TEST(AdasCoexistenceStackTest, QuietFrameReportsDriverBrakeAndBrakeOutKeysEvenWhenAebDoesNotFire)
{
    ManualAdasStackConfig cfg;
    cfg.aeb_enabled = true;

    KickdownDetector detector(cfg.kickdown);
    PedalArbitrator  arb(cfg.arbitrator);

    const TrafficPolicySnapshot intervention = EmptySnapshot();  // nothing fires this frame
    const TrafficPolicySnapshot warning       = EmptySnapshot();
    const PedalSteerCommand     driver_cmd    = MakeDriverCmd(0.2, 0.35);

    const auto result = ComputeManualAdasFrame(cfg, /*owns_longitudinal=*/true, intervention, warning, driver_cmd,
                                                /*ego_speed_mps=*/10.0, /*measured_decel_mps2=*/0.0, /*dt=*/0.02,
                                                detector, arb);

    // Absent on a quiet frame would be ambiguous with "not reported" -- see
    // header comment -- so both keys must be present here, not just on a
    // firing frame.
    double driver_brake = -1.0;
    double brake_out    = -1.0;
    ASSERT_TRUE(TryGetDetailValue(result.detail, "gt.aeb.driver_brake", driver_brake));
    ASSERT_TRUE(TryGetDetailValue(result.detail, "gt.aeb.brake_out", brake_out));
    EXPECT_NEAR(driver_brake, driver_cmd.brake, 1e-3);
    EXPECT_NEAR(brake_out, result.pedals.brake_out, 1e-3);
}

TEST(AdasCoexistenceStackTest, FiringFrameBrakeOutEqualsMaxOfDriverBrakeAndBrakeRequestInDetail)
{
    ManualAdasStackConfig cfg;
    cfg.aeb_enabled = true;

    KickdownDetector detector(cfg.kickdown);
    PedalArbitrator  arb(cfg.arbitrator);

    const TrafficPolicySnapshot intervention = MakeSnapshotWithConstraint(10.0);
    const TrafficPolicySnapshot warning       = EmptySnapshot();
    const PedalSteerCommand     driver_cmd    = MakeDriverCmd(0.0, 0.1);  // light driver brake, below the AEB request

    const auto result = ComputeManualAdasFrame(cfg, /*owns_longitudinal=*/true, intervention, warning, driver_cmd,
                                                /*ego_speed_mps=*/15.0, /*measured_decel_mps2=*/0.0, /*dt=*/0.02,
                                                detector, arb);

    double driver_brake  = -1.0;
    double brake_out     = -1.0;
    double brake_request = -1.0;
    ASSERT_TRUE(TryGetDetailValue(result.detail, "gt.aeb.driver_brake", driver_brake));
    ASSERT_TRUE(TryGetDetailValue(result.detail, "gt.aeb.brake_out", brake_out));
    ASSERT_TRUE(TryGetDetailValue(result.detail, "gt.aeb.brake_request", brake_request));

    EXPECT_NEAR(driver_brake, driver_cmd.brake, 1e-3);
    // The max-composition claim (§3-1), restated in the observable domain.
    EXPECT_NEAR(brake_out, std::max(driver_brake, brake_request), 1e-3);
}

TEST(AdasCoexistenceStackTest, NotOwningDomainEmitsEmptyDetailNotZeroedBrakeKeys)
{
    ManualAdasStackConfig cfg;
    cfg.aeb_enabled = true;

    KickdownDetector detector(cfg.kickdown);
    PedalArbitrator  arb(cfg.arbitrator);

    const TrafficPolicySnapshot intervention = MakeSnapshotWithConstraint(10.0);
    const TrafficPolicySnapshot warning       = EmptySnapshot();
    const PedalSteerCommand     driver_cmd    = MakeDriverCmd(0.0, 0.3);

    const auto result = ComputeManualAdasFrame(cfg, /*owns_longitudinal=*/false, intervention, warning, driver_cmd,
                                                /*ego_speed_mps=*/15.0, /*measured_decel_mps2=*/0.0, /*dt=*/0.02,
                                                detector, arb);

    // Empty, not populated-with-zeros -- a zeroed gt.aeb.driver_brake/
    // brake_out pair would be indistinguishable from "the stack ran, drove
    // 0.0, and applied 0.0", a genuine (negative) observation. See header.
    EXPECT_TRUE(result.detail.empty());
}

TEST(AdasCoexistenceStackTest, AebDisabledEmitsEmptyDetailNotZeroedBrakeKeys)
{
    ManualAdasStackConfig cfg;
    cfg.aeb_enabled = false;

    KickdownDetector detector(cfg.kickdown);
    PedalArbitrator  arb(cfg.arbitrator);

    const TrafficPolicySnapshot intervention = MakeSnapshotWithConstraint(10.0);
    const TrafficPolicySnapshot warning       = EmptySnapshot();
    const PedalSteerCommand     driver_cmd    = MakeDriverCmd(0.0, 0.3);

    const auto result = ComputeManualAdasFrame(cfg, /*owns_longitudinal=*/true, intervention, warning, driver_cmd,
                                                /*ego_speed_mps=*/15.0, /*measured_decel_mps2=*/0.0, /*dt=*/0.02,
                                                detector, arb);

    EXPECT_TRUE(result.detail.empty());
}

// ============================================================================
// DeriveFcwGateConfig -- looseness guard
// ============================================================================

TEST(AdasCoexistenceStackTest, InvertedWarningThresholdsAreClampedToPreserveLooseness)
{
    ManualAdasStackConfig cfg;
    cfg.aeb.ttc_threshold        = 2.5;
    cfg.aeb.min_a_req            = 3.0;
    cfg.warning_ttc_threshold_s = 1.0;  // tighter than aeb -- inverted/misconfigured
    cfg.warning_min_a_req_mps2  = 5.0;  // tighter than aeb -- inverted/misconfigured

    const AebSafetyConfig fcw_cfg = DeriveFcwGateConfig(cfg);

    EXPECT_GE(fcw_cfg.ttc_threshold, cfg.aeb.ttc_threshold);
    EXPECT_LE(fcw_cfg.min_a_req, cfg.aeb.min_a_req);
}

TEST(AdasCoexistenceStackTest, NonInvertedWarningThresholdsPassThroughUnclampedAndSelectionParamsAreVerbatim)
{
    ManualAdasStackConfig cfg;
    cfg.aeb.ttc_threshold        = 2.5;
    cfg.aeb.min_a_req            = 3.0;
    cfg.warning_ttc_threshold_s = 3.3;  // properly looser
    cfg.warning_min_a_req_mps2  = 2.0;  // properly looser

    const AebSafetyConfig fcw_cfg = DeriveFcwGateConfig(cfg);

    EXPECT_NEAR(fcw_cfg.ttc_threshold, cfg.warning_ttc_threshold_s, 1e-9);
    EXPECT_NEAR(fcw_cfg.min_a_req, cfg.warning_min_a_req_mps2, 1e-9);
    // Candidate-selection parameters must be copied verbatim, not overridden
    // -- see header's "THE TWO AebSafety INSTANCES" rationale.
    EXPECT_NEAR(fcw_cfg.lookahead, cfg.aeb.lookahead, 1e-9);
    EXPECT_NEAR(fcw_cfg.lateral_tol, cfg.aeb.lateral_tol, 1e-9);
    EXPECT_NEAR(fcw_cfg.stop_margin, cfg.aeb.stop_margin, 1e-9);
}

// ============================================================================
// ComputeMeasuredDecel
// ============================================================================

TEST(ComputeMeasuredDecelTest, PositiveWhenSlowingDown)
{
    EXPECT_GT(ComputeMeasuredDecel(/*v_now=*/8.0, /*v_prev=*/10.0, /*dt=*/0.1), 0.0);
}

TEST(ComputeMeasuredDecelTest, NegativeWhenSpeedingUp)
{
    EXPECT_LT(ComputeMeasuredDecel(/*v_now=*/10.0, /*v_prev=*/8.0, /*dt=*/0.1), 0.0);
}

TEST(ComputeMeasuredDecelTest, ZeroWhenDtNonPositive)
{
    EXPECT_EQ(ComputeMeasuredDecel(8.0, 10.0, 0.0), 0.0);
    EXPECT_EQ(ComputeMeasuredDecel(8.0, 10.0, -0.1), 0.0);
}

TEST(ComputeMeasuredDecelTest, ExactArithmetic)
{
    // v_prev=10, v_now=8, dt=0.5 -> (10-8)/0.5 = 4.0
    EXPECT_NEAR(ComputeMeasuredDecel(8.0, 10.0, 0.5), 4.0, 1e-9);
}

}  // namespace gt_esmini
