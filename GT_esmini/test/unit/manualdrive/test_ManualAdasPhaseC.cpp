// req-vd-ad:REQ-AD-026 / REQ-AD-028 / REQ-AD-030 / REQ-AD-031
// vd-func:FUNC-079 / FUNC-081
//
// Phase-C integration tests for the pure decision core: the three-stage
// arbitration ORDER (ACC generate -> MSL limit -> AEB safety, design §3-1),
// the speed limiter itself, the ACC/MSL mutual exclusion (design §6) and the
// HVD row projection for the two new functions (design §8-2/§8-3).
//
// The stage-order tests are the reason this file exists separately from
// test_AccLonController.cpp: each stage is individually correct in its own
// unit test and the ORDER is still the thing that decides whether a limiter
// can veto a safety brake (it must not) or a floored accelerator can defeat a
// stop (it must, for MSL, and must not, for AEB with suppression off).

#include "gt_esmini/control/manualdrive/AdasCoexistenceStack.hpp"
#include "gt_esmini/control/manualdrive/SpeedLimiter.hpp"
#include "gt_esmini/control/virtualdriver/AdasFunctionReport.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

using namespace gt_esmini;

namespace
{

ManualAdasStackConfig PhaseCConfig(bool aeb, bool acc, bool msl)
{
    ManualAdasStackConfig cfg;
    cfg.aeb_enabled  = aeb;
    cfg.acc.enabled  = acc;
    cfg.msl.enabled  = msl;
    return cfg;
}

TrafficPolicySnapshot LeadStopSnapshot(double distance_m)
{
    TrafficPolicySnapshot snap;
    PolicyConstraint      c;
    c.kind   = PolicyConstraint::Kind::STOP_AT_S;
    c.s      = distance_m;
    c.source = "lead_vehicle";
    c.tier   = PolicyConstraint::Tier::COMFORT;
    snap.constraints.push_back(c);
    snap.valid = true;
    return snap;
}

TrafficPolicySnapshot AebStopSnapshot(double distance_m)
{
    TrafficPolicySnapshot snap;
    PolicyConstraint      c;
    c.kind   = PolicyConstraint::Kind::STOP_AT_S;
    c.s      = distance_m;
    c.source = "aeb";
    c.tier   = PolicyConstraint::Tier::SAFETY;
    snap.constraints.push_back(c);
    snap.valid = true;
    return snap;
}

PedalSteerCommand Pedals(double throttle, double brake, std::uint32_t buttons = 0)
{
    PedalSteerCommand cmd;
    cmd.throttle = throttle;
    cmd.brake    = brake;
    cmd.buttons  = buttons;
    return cmd;
}

bool DetailHas(const PolicyDetail& d, const std::string& key)
{
    return std::any_of(d.begin(), d.end(), [&](const auto& kv) { return kv.first == key; });
}

std::string DetailValue(const PolicyDetail& d, const std::string& key)
{
    for (const auto& kv : d)
        if (kv.first == key) return kv.second;
    return {};
}

// One frame through the real pure core, with all the phase-C state the caller
// normally owns held by the fixture.
struct Harness
{
    ManualAdasStackConfig cfg;
    KickdownDetector      kickdown{cfg.kickdown};
    PedalArbitrator       arbitrator{cfg.arbitrator};
    AccLonController      acc{cfg.acc};
    ManualAdasRuntime     runtime;

    explicit Harness(const ManualAdasStackConfig& c)
        : cfg(c), kickdown(c.kickdown), arbitrator(c.arbitrator), acc(c.acc)
    {
    }

    ManualAdasFrameResult Step(const PedalSteerCommand&     cmd,
                                double                       v,
                                const TrafficPolicySnapshot& acc_policy = {},
                                const TrafficPolicySnapshot& intervention = {},
                                double                       speed_limit = 0.0,
                                double                       dt = 0.05)
    {
        ManualAdasEnvironment env;
        env.speed_limit_mps = speed_limit;
        env.buttons         = cmd.buttons;
        return ComputeManualAdasFrame(cfg, /*owns_longitudinal=*/true, intervention, /*warning=*/{}, acc_policy, env,
                                       cmd, v, /*measured_decel_mps2=*/0.0, dt, kickdown, arbitrator, acc, runtime);
    }

    // Presses one button for one frame, then releases it, so the edge decoder
    // sees a real press. Returns the PRESSED frame's result.
    ManualAdasFrameResult Press(std::uint32_t bit, double v, const TrafficPolicySnapshot& acc_policy = {})
    {
        const auto pressed = Step(Pedals(0.0, 0.0, bit), v, acc_policy);
        Step(Pedals(0.0, 0.0, 0), v, acc_policy);
        return pressed;
    }

    void EngageAcc(double v)
    {
        Press(ButtonBits::ACC_TOGGLE, v);
        Press(ButtonBits::ACC_SET_RESUME, v);
    }
};

}  // namespace

// ============================================================================
// SpeedLimiter (REQ-AD-030) -- the limit stage on its own
// ============================================================================

TEST(SpeedLimiterTest, DisabledPassesTheThrottleThrough)
{
    SpeedLimiterConfig cfg;  // enabled = false
    const auto         out = ApplySpeedLimiter(cfg, 1.0, 50.0, 0.0, false);
    EXPECT_DOUBLE_EQ(out.throttle_out, 1.0);
    EXPECT_FALSE(out.limiting);
}

TEST(SpeedLimiterTest, AboveTheCapTheThrottleIsShutButNoBrakeIsEverProduced)
{
    // The whole point of REQ-AD-030 step a's negative: a limiter clamps
    // throttle and NEVER brakes, so exceeding the cap (on a descent, say) is
    // an expected outcome rather than something to correct.
    SpeedLimiterConfig cfg;
    cfg.enabled       = true;
    cfg.set_speed_mps = 20.0;
    const auto out    = ApplySpeedLimiter(cfg, 1.0, 26.0, 0.0, false);
    EXPECT_DOUBLE_EQ(out.throttle_out, 0.0);
    EXPECT_TRUE(out.limiting);
    // SpeedLimiterResult has no brake field AT ALL -- the guarantee is
    // structural, and this test documents that fact for a future reader who
    // might be tempted to add one.
}

TEST(SpeedLimiterTest, WellBelowTheCapTheDriverKeepsFullAuthority)
{
    SpeedLimiterConfig cfg;
    cfg.enabled        = true;
    cfg.set_speed_mps  = 30.0;
    cfg.taper_band_mps = 2.0;
    const auto out     = ApplySpeedLimiter(cfg, 1.0, 10.0, 0.0, false);
    EXPECT_DOUBLE_EQ(out.throttle_out, 1.0);
    EXPECT_FALSE(out.limiting);
}

TEST(SpeedLimiterTest, TheClampIsOneSidedAndNeverRaisesAPedal)
{
    SpeedLimiterConfig cfg;
    cfg.enabled       = true;
    cfg.set_speed_mps = 30.0;
    const auto out    = ApplySpeedLimiter(cfg, 0.2, 5.0, 0.0, false);
    EXPECT_DOUBLE_EQ(out.throttle_out, 0.2);  // the driver asked for less; that stands
}

TEST(SpeedLimiterTest, KickdownLiftsTheCapAndSaysSo)
{
    SpeedLimiterConfig cfg;
    cfg.enabled       = true;
    cfg.set_speed_mps = 20.0;
    const auto out    = ApplySpeedLimiter(cfg, 1.0, 26.0, 0.0, /*kickdown_active=*/true);
    EXPECT_DOUBLE_EQ(out.throttle_out, 1.0);
    EXPECT_FALSE(out.limiting);
    // Reported separately: "not limiting because released" and "not limiting
    // because the cap was never reached" must not look the same from outside.
    EXPECT_TRUE(out.kickdown_released);
}

TEST(SpeedLimiterTest, LinkedModeUsesTheRoadLimitAndFallsBackWhenItIsUnavailable)
{
    SpeedLimiterConfig cfg;
    cfg.enabled            = true;
    cfg.set_speed_mps      = 30.0;
    cfg.speed_limit_linked = true;

    EXPECT_DOUBLE_EQ(ApplySpeedLimiter(cfg, 1.0, 25.0, /*speed_limit=*/14.0, false).cap_mps, 14.0);
    // An unavailable limit must NOT become a 0 cap that pins the throttle shut.
    const auto fallback = ApplySpeedLimiter(cfg, 1.0, 25.0, /*speed_limit=*/0.0, false);
    EXPECT_DOUBLE_EQ(fallback.cap_mps, 30.0);
    EXPECT_DOUBLE_EQ(fallback.throttle_out, 1.0);
}

// ============================================================================
// Stage order (design §3-1)
// ============================================================================

TEST(ManualAdasPhaseCTest, MslClampsAccOutputNotTheRawDriverPedal)
{
    // The limit stage sits AFTER generate. If it clamped the raw human pedal
    // instead, an ACC accelerating toward its set speed would sail past the
    // limiter's cap -- invisible on any test where the human is also on the
    // throttle.
    Harness h{PhaseCConfig(/*aeb=*/false, /*acc=*/true, /*msl=*/true)};
    h.EngageAcc(20.0);                       // ACC set to 20
    h.Press(ButtonBits::MSL_TOGGLE, 20.0);   // limiter armed, cap := 20 -- and ACC demoted

    // ACC is now suspended (later ON wins), so this frame's generate stage is
    // the human's own pedal; what matters here is that the LIMIT stage sees
    // the generate stage's output.
    //
    // 0.85, NOT 1.0: kickdown_threshold is 0.95, and a floored pedal RELEASES
    // the limiter (REQ-AD-030 step b). A stage-order test written at 1.0 would
    // measure the release path while claiming to measure the clamp.
    const auto out = h.Step(Pedals(0.85, 0.0), /*v=*/24.0);
    EXPECT_DOUBLE_EQ(out.acc.throttle, 0.85);  // generate stage passed the human's pedal through
    EXPECT_LT(out.msl.throttle_out, out.acc.throttle);
    EXPECT_DOUBLE_EQ(out.pedals.throttle_out, out.msl.throttle_out);
}

TEST(ManualAdasPhaseCTest, AebStillWinsOverAnAccThatIsAskingForThrottle)
{
    // REQ-AD-026 step d: safety is the LAST stage, so an AEB demand survives
    // whatever the generate stage proposed.
    Harness h{PhaseCConfig(/*aeb=*/true, /*acc=*/true, /*msl=*/false)};
    h.EngageAcc(20.0);

    // ACC wants to accelerate (well below its set speed), AEB wants a stop.
    const auto out = h.Step(Pedals(0.0, 0.0), /*v=*/20.0, /*acc_policy=*/{}, AebStopSnapshot(8.0));

    EXPECT_TRUE(out.decision.aeb_intervening);
    EXPECT_DOUBLE_EQ(out.pedals.throttle_out, 0.0);
    EXPECT_GT(out.pedals.brake_out, 0.0);
    // ACC's OWN proposal is preserved for inspection -- the two stages stayed
    // separable, which is what makes "AEB fired independently" observable.
    EXPECT_EQ(out.decision.acc_state, 2);
}

TEST(ManualAdasPhaseCTest, AnAccOnlyConfigDoesNotTakeThePhaseABypass)
{
    // The phase-A bypass tested `!cfg.aeb_enabled`. Left unchanged, an
    // ACC-only configuration would have produced nothing at all while still
    // reporting plausible-looking UNAVAILABLE rows -- a run that looks exactly
    // like a correctly-disabled one.
    Harness h{PhaseCConfig(/*aeb=*/false, /*acc=*/true, /*msl=*/false)};
    h.EngageAcc(10.0);
    const auto out = h.Step(Pedals(0.0, 0.0), /*v=*/4.0);
    EXPECT_EQ(out.decision.acc_state, 2);
    EXPECT_TRUE(out.acc.engaged);
    EXPECT_GT(out.acc.throttle, 0.0);
    EXPECT_TRUE(DetailHas(out.detail, "gt.acc.set_speed_mps"));
}

TEST(ManualAdasPhaseCTest, NotOwningTheDomainStillBypassesEverythingInPhaseC)
{
    ManualAdasStackConfig cfg = PhaseCConfig(true, true, true);
    KickdownDetector      kd{cfg.kickdown};
    PedalArbitrator       arb{cfg.arbitrator};
    AccLonController      acc{cfg.acc};
    ManualAdasRuntime     runtime;

    ManualAdasEnvironment env;
    const auto            out =
        ComputeManualAdasFrame(cfg, /*owns_longitudinal=*/false, AebStopSnapshot(3.0), {}, LeadStopSnapshot(3.0), env,
                                Pedals(0.4, 0.15), 20.0, 0.0, 0.05, kd, arb, acc, runtime);

    EXPECT_DOUBLE_EQ(out.pedals.throttle_out, 0.4);
    EXPECT_DOUBLE_EQ(out.pedals.brake_out, 0.15);
    EXPECT_TRUE(out.detail.empty());  // empty, not zeroed (phase-A rule, still holds)
}

// ============================================================================
// ACC / MSL mutual exclusion (design §6)
// ============================================================================

TEST(ManualAdasPhaseCTest, TurningMslOnDemotesAnActiveAccToStandby)
{
    Harness h{PhaseCConfig(false, true, true)};
    h.EngageAcc(20.0);
    EXPECT_EQ(h.Step(Pedals(0.0, 0.0), 20.0).decision.acc_state, 2);

    const auto after = h.Press(ButtonBits::MSL_TOGGLE, 20.0);
    EXPECT_EQ(after.decision.acc_state, 1);  // STANDBY -- demoted, not OFF
    EXPECT_GE(after.decision.msl_state, 1);
}

TEST(ManualAdasPhaseCTest, ATogglePressedOnADemotedActiveAccReclaimsItRatherThanSwitchingItOff)
{
    // The behaviour the first phase-C measurement pass exposed: a plain toggle
    // on a STANDBY function powers it OFF, which under exclusivity is the wrong
    // reading of "the driver pressed ACC while the limiter held the stalk" --
    // and it makes "the later ON wins" untestable in the second direction.
    // Measured before this fix: gt.acc went ACTIVE -> STANDBY -> UNAVAILABLE.
    Harness h{PhaseCConfig(false, true, true)};
    h.EngageAcc(20.0);
    h.Press(ButtonBits::MSL_TOGGLE, 20.0);   // ACC demoted to STANDBY
    EXPECT_EQ(h.Step(Pedals(0.0, 0.0), 20.0).decision.acc_state, 1);

    h.Press(ButtonBits::ACC_TOGGLE, 20.0);   // reclaim, NOT power-off
    const auto out = h.Step(Pedals(0.0, 0.0), 20.0);
    EXPECT_EQ(out.decision.acc_state, 1);    // still ON (STANDBY, awaiting resume)
    EXPECT_EQ(out.decision.msl_state, 1);    // and the limiter is the demoted one now

    // ...and a RESUME then puts it back in control, which is what makes the
    // reclaim useful rather than merely non-destructive.
    h.Press(ButtonBits::ACC_SET_RESUME, 20.0);
    EXPECT_EQ(h.Step(Pedals(0.0, 0.0), 20.0).decision.acc_state, 2);
}

TEST(ManualAdasPhaseCTest, TurningAccOnDemotesAnArmedMsl)
{
    Harness h{PhaseCConfig(false, true, true)};
    h.Press(ButtonBits::MSL_TOGGLE, 20.0);
    EXPECT_GE(h.Step(Pedals(0.0, 0.0), 20.0).decision.msl_state, 1);

    h.Press(ButtonBits::ACC_TOGGLE, 20.0);
    // 0.85 for the same reason as the stage-order test above: at 1.0 the shared
    // kickdown latch would release the limiter, so "it stopped clamping" would
    // be true for a reason that has nothing to do with the demotion.
    const auto out = h.Step(Pedals(0.85, 0.0), 25.0);
    EXPECT_EQ(out.decision.msl_state, 1);      // demoted to STANDBY
    EXPECT_FALSE(out.msl.limiting);            // and it really stopped clamping
    EXPECT_DOUBLE_EQ(out.msl.throttle_out, 0.85);
}

TEST(ManualAdasPhaseCTest, ATogglePressedOnADemotedButSwitchedOffAccStillSwitchesItOn)
{
    // The narrowing on the reclaim branch (acc.State() != OFF). Switching the
    // limiter on suspends ACC even though ACC was never switched on; the next
    // ACC_TOGGLE must therefore be a real power-on, not a reclaim that gets
    // swallowed and leaves the driver pressing twice.
    Harness h{PhaseCConfig(false, true, true)};
    h.Press(ButtonBits::MSL_TOGGLE, 20.0);
    h.Press(ButtonBits::ACC_TOGGLE, 20.0);
    EXPECT_EQ(h.Step(Pedals(0.0, 0.0), 20.0).decision.acc_state, 1);  // STANDBY, i.e. ON
}

TEST(ManualAdasPhaseCTest, ADemotedLimiterKeepsItsCapAndResumesWhenAccIsSwitchedOff)
{
    Harness h{PhaseCConfig(false, true, true)};
    h.Press(ButtonBits::MSL_TOGGLE, 20.0);  // cap := 20
    h.Press(ButtonBits::ACC_TOGGLE, 20.0);  // ACC on -> MSL demoted
    h.Press(ButtonBits::ACC_TOGGLE, 20.0);  // ACC off again

    const auto out = h.Step(Pedals(0.85, 0.0), 25.0);  // below kickdown_threshold, see above
    EXPECT_EQ(out.decision.msl_state, 2);    // limiting again
    EXPECT_DOUBLE_EQ(out.msl.cap_mps, 20.0);  // with the SAME cap it was set to
}

// ============================================================================
// Observables (design §8-4) -- the setting-vs-effect split reaches the stream
// ============================================================================

TEST(ManualAdasPhaseCTest, AccDetailCarriesBothTheSettingAndTheEffectiveValue)
{
    // The config has to be finished BEFORE the Harness is built: AccLonController
    // takes its config BY VALUE at construction, so a later h.cfg.acc.* write
    // would reach ComputeManualAdasFrame's `cfg` but not the controller that
    // actually computes the cap.
    ManualAdasStackConfig cfg   = PhaseCConfig(false, true, false);
    cfg.acc.respect_speed_limit = true;
    Harness h{cfg};
    h.EngageAcc(30.0);

    const auto out = h.Step(Pedals(0.0, 0.0), 30.0, /*acc_policy=*/{}, /*intervention=*/{}, /*speed_limit=*/22.0);

    EXPECT_EQ(DetailValue(out.detail, "gt.acc.set_speed_mps"), "30.000");
    EXPECT_EQ(DetailValue(out.detail, "gt.acc.effective_cap_mps"), "22.000");
    EXPECT_TRUE(DetailHas(out.detail, "gt.acc.thw_setting_s"));
    EXPECT_TRUE(DetailHas(out.detail, "gt.acc.thw_actual_s"));
}

TEST(ManualAdasPhaseCTest, SettingChangedStartsFalseAndLatchesOnTheFirstChange)
{
    // The structural precondition of the setting_reflected matcher: a run
    // where nobody touched the stalk must be distinguishable from one where
    // they did, or a constant field would pass the matcher vacuously.
    Harness h{PhaseCConfig(false, true, false)};
    h.EngageAcc(20.0);
    EXPECT_EQ(DetailValue(h.Step(Pedals(0.0, 0.0), 20.0).detail, "gt.acc.setting_changed"), "false");

    h.Press(ButtonBits::ACC_SPEED_UP, 20.0);
    EXPECT_EQ(DetailValue(h.Step(Pedals(0.0, 0.0), 20.0).detail, "gt.acc.setting_changed"), "true");
}

TEST(ManualAdasPhaseCTest, ThwActualIsDerivedFromTheFollowingPolicysOwnGap)
{
    Harness h{PhaseCConfig(false, true, false)};
    h.EngageAcc(20.0);

    TrafficPolicySnapshot lead;
    lead.detail.emplace_back("gt.lead_vehicle.gap_m", "40.000");
    const auto out = h.Step(Pedals(0.0, 0.0), /*v=*/20.0, lead);

    EXPECT_EQ(DetailValue(out.detail, "gt.acc.thw_actual_s"), "2.000");  // 40 / 20
}

TEST(ManualAdasPhaseCTest, MslDetailIsAbsentWhenTheFunctionIsNotConfigured)
{
    // Absent-is-not-zero, applied to the phase-C keys: a disabled limiter must
    // not emit gt.msl.cap_mps=0.000, which a consumer could not tell apart
    // from a real 0 cap.
    Harness h{PhaseCConfig(true, false, false)};
    const auto out = h.Step(Pedals(0.2, 0.0), 20.0);
    EXPECT_FALSE(DetailHas(out.detail, "gt.msl.cap_mps"));
    EXPECT_FALSE(DetailHas(out.detail, "gt.acc.set_speed_mps"));
}

// ============================================================================
// HVD row projection (design §8-2/§8-3, REQ-AD-028 段a/段b)
// ============================================================================

TEST(ManualAdasReportPhaseCTest, PhaseABConfigStillProducesExactlyTheTwoOriginalRows)
{
    // Baseline invariance: adding ACC/MSL must not change what an existing
    // ManualDrive run emits, or every committed phase-A/B expectation moves.
    ManualAdasEnableFlags flags;
    flags.aeb = flags.fcw = true;
    ManualAdasDecision decision;

    const auto rows = BuildManualAdasFunctionReport(flags, /*owns_longitudinal_domain=*/true, decision, {});
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].custom_name, "gt.aeb");
    EXPECT_EQ(rows[1].custom_name, "gt.fcw");
}

TEST(ManualAdasReportPhaseCTest, AccRowUsesTheNativeOsiNameAndTheThreeValueStates)
{
    ManualAdasEnableFlags flags;
    flags.acc = true;
    ManualAdasDecision decision;

    decision.acc_state = 0;
    auto rows = BuildManualAdasFunctionReport(flags, true, decision, {});
    ASSERT_EQ(rows.size(), 3u);
    EXPECT_EQ(rows[2].custom_name, "gt.acc");
    EXPECT_EQ(rows[2].name, osi_adas::NAME_ADAPTIVE_CRUISE_CONTROL);  // never NAME_OTHER (段a)
    EXPECT_EQ(rows[2].state, osi_adas::STATE_UNAVAILABLE);

    decision.acc_state = 1;
    rows = BuildManualAdasFunctionReport(flags, true, decision, {});
    EXPECT_EQ(rows[2].state, osi_adas::STATE_STANDBY);

    decision.acc_state = 2;
    rows = BuildManualAdasFunctionReport(flags, true, decision, {});
    EXPECT_EQ(rows[2].state, osi_adas::STATE_ACTIVE);
}

TEST(ManualAdasReportPhaseCTest, MslRowUsesTheNativeOsiName)
{
    ManualAdasEnableFlags flags;
    flags.msl = true;
    ManualAdasDecision decision;
    decision.msl_state = 2;

    const auto rows = BuildManualAdasFunctionReport(flags, true, decision, {});
    ASSERT_EQ(rows.size(), 3u);
    EXPECT_EQ(rows[2].custom_name, "gt.msl");
    EXPECT_EQ(rows[2].name, osi_adas::NAME_SPEED_LIMIT_CONTROL);
    EXPECT_EQ(rows[2].state, osi_adas::STATE_ACTIVE);
}

TEST(ManualAdasReportPhaseCTest, BrakeOriginOverrideReachesTheAccRowAsAReasonEnum)
{
    // req-vd-ad:REQ-AD-028 段b's REASON_BRAKE_PEDAL half -- the producer that
    // did not exist before phase C.
    ManualAdasEnableFlags flags;
    flags.acc = true;
    ManualAdasDecision decision;
    decision.acc_state                 = 1;
    decision.acc_driver_override_brake = true;

    const auto rows = BuildManualAdasFunctionReport(flags, true, decision, {});
    const auto& acc = rows[2];
    EXPECT_TRUE(acc.driver_override.reported);
    EXPECT_TRUE(acc.driver_override.active);
    ASSERT_EQ(acc.driver_override.reasons.size(), 1u);
    EXPECT_EQ(acc.driver_override.reasons[0], osi_adas::REASON_BRAKE_PEDAL);
    EXPECT_TRUE(acc.custom_state.empty());  // a brake override is NOT the accel token
}

TEST(ManualAdasReportPhaseCTest, AcceleratorOriginOverrideUsesTheCustomStateTokenNotAReason)
{
    ManualAdasEnableFlags flags;
    flags.acc = true;
    ManualAdasDecision decision;
    decision.acc_state                 = 2;
    decision.acc_driver_override_accel = true;

    // NB: bind the vector, then index it. `const auto& x = f(...)[2]` binds a
    // reference into a TEMPORARY vector that dies at the end of the full
    // expression -- the reads afterwards are then undefined behaviour that
    // happens to look like "the field was never populated".
    const auto  rows = BuildManualAdasFunctionReport(flags, true, decision, {});
    const auto& acc  = rows[2];
    EXPECT_TRUE(acc.driver_override.active);
    EXPECT_TRUE(acc.driver_override.reasons.empty());  // OSI has no accelerator Reason
    EXPECT_EQ(acc.custom_state, std::string(kDriverOverrideAccel));
}

TEST(ManualAdasReportPhaseCTest, BothOverrideOriginsCanBeReportedAtOnce)
{
    // A driver with one foot on each pedal produces two true statements; the
    // row must not have to choose.
    ManualAdasEnableFlags flags;
    flags.acc = true;
    ManualAdasDecision decision;
    decision.acc_state                 = 1;
    decision.acc_driver_override_brake = true;
    decision.acc_driver_override_accel = true;

    const auto  rows = BuildManualAdasFunctionReport(flags, true, decision, {});
    const auto& acc  = rows[2];
    EXPECT_EQ(acc.driver_override.reasons.size(), 1u);
    EXPECT_EQ(acc.custom_state, std::string(kDriverOverrideAccel));
}

TEST(ManualAdasReportPhaseCTest, NotOwningTheDomainReportsUnavailableWithNoOverrideChannel)
{
    ManualAdasEnableFlags flags;
    flags.acc = flags.msl = true;
    ManualAdasDecision decision;
    decision.acc_state                 = 2;
    decision.acc_driver_override_brake = true;

    const auto rows = BuildManualAdasFunctionReport(flags, /*owns_longitudinal_domain=*/false, decision, {});
    ASSERT_EQ(rows.size(), 4u);
    for (const auto& r : rows)
    {
        EXPECT_EQ(r.state, osi_adas::STATE_UNAVAILABLE);
        // A function that was not running cannot have been overridden -- the
        // channel must be ABSENT, not an explicit "no override" that would
        // look like a measurement (phase B's `reported` discipline).
        EXPECT_FALSE(r.driver_override.reported);
    }
}

TEST(ManualAdasReportPhaseCTest, DriverOffAccStillReportsAnEvaluatedOverrideChannel)
{
    // The compensating observation for the header's documented state collapse:
    // acc_state 0 reports UNAVAILABLE like a config-disabled function, but the
    // override channel being PRESENT is what still separates "installed and
    // switched off by the driver" from "not installed at all".
    ManualAdasEnableFlags flags;
    flags.acc = true;
    ManualAdasDecision decision;
    decision.acc_state = 0;

    const auto  rows = BuildManualAdasFunctionReport(flags, true, decision, {});
    const auto& acc  = rows[2];
    EXPECT_EQ(acc.state, osi_adas::STATE_UNAVAILABLE);
    EXPECT_TRUE(acc.driver_override.reported);
    EXPECT_FALSE(acc.driver_override.active);
}

TEST(ManualAdasReportPhaseCTest, DetailKeysRouteToTheirOwnRowOnly)
{
    ManualAdasEnableFlags flags;
    flags.aeb = flags.fcw = flags.acc = flags.msl = true;
    ManualAdasDecision decision;

    PolicyDetail detail;
    detail.emplace_back("gt.aeb.ttc_s", "1.500");
    detail.emplace_back("gt.acc.set_speed_mps", "20.000");
    detail.emplace_back("gt.msl.cap_mps", "18.000");

    const auto rows = BuildManualAdasFunctionReport(flags, true, decision, detail);
    ASSERT_EQ(rows.size(), 4u);
    EXPECT_EQ(rows[0].detail.size(), 1u);  // gt.aeb
    EXPECT_EQ(rows[1].detail.size(), 0u);  // gt.fcw
    ASSERT_EQ(rows[2].detail.size(), 1u);  // gt.acc
    EXPECT_EQ(rows[2].detail[0].first, "gt.acc.set_speed_mps");
    ASSERT_EQ(rows[3].detail.size(), 1u);  // gt.msl
    EXPECT_EQ(rows[3].detail[0].first, "gt.msl.cap_mps");
}
