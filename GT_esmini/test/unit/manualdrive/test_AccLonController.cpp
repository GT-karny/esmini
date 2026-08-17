// req-vd-ad:REQ-AD-026 / req-vd-ad:REQ-AD-031 / vd-func:FUNC-079
//
// Unit tests for the ACC generate stage (AccLonController) and the one-point
// policy ceiling (EvaluateAccCeiling). Pure logic, no engine: every claim
// REQ-AD-026's acceptance ladder makes about the STATE MACHINE and the
// SETTING-vs-EFFECT split is decided in this file, so it is where those claims
// are pinned. The end-to-end batch then shows the same behaviours survive
// contact with a real vehicle and a real road.

#include "gt_esmini/control/manualdrive/AccLonController.hpp"

#include <gtest/gtest.h>

#include <cmath>

using namespace gt_esmini;

namespace
{

AccLonControllerConfig EnabledConfig()
{
    AccLonControllerConfig cfg;
    cfg.enabled = true;
    return cfg;
}

AccFrameInput CruiseInput(double v)
{
    AccFrameInput in;
    in.ego_speed_mps = v;
    return in;
}

PolicyConstraint MakeConstraint(PolicyConstraint::Kind kind,
                                 double                 s,
                                 double                 value,
                                 const char*            source,
                                 PolicyConstraint::Tier tier = PolicyConstraint::Tier::COMFORT)
{
    PolicyConstraint c;
    c.kind   = kind;
    c.s      = s;
    c.value  = value;
    c.source = source;
    c.tier   = tier;
    return c;
}

// Presses one operating control for exactly one frame. Written as a helper
// because the controller consumes EDGES: a test that set the flag and left it
// set would be testing a level-triggered control that does not exist.
AccFrameOutput PressAndStep(AccLonController& acc, AccFrameInput in, bool AdasOperations::*op, double dt = 0.05)
{
    in.ops.*op = true;
    return acc.Step(in, dt);
}

}  // namespace

// ============================================================================
// EvaluateAccCeiling (design §4-2) -- the one-point ceiling
// ============================================================================

TEST(AccCeilingTest, NoConstraintsMeansNoCeiling)
{
    const AccCeiling c = EvaluateAccCeiling({}, 2.0);
    EXPECT_TRUE(std::isinf(c.ceiling_mps));
    EXPECT_FALSE(c.stop_requested);
}

TEST(AccCeilingTest, MaxSpeedConstraintBecomesTheCeiling)
{
    const AccCeiling c =
        EvaluateAccCeiling({MakeConstraint(PolicyConstraint::Kind::MAX_SPEED, 0.0, 12.0, "lead_vehicle")}, 2.0);
    EXPECT_DOUBLE_EQ(c.ceiling_mps, 12.0);
    EXPECT_FALSE(c.stop_requested);
}

TEST(AccCeilingTest, StopConstraintBecomesAKinematicCeilingAndRaisesStopRequested)
{
    // v_allow = sqrt(2 * a * d) = sqrt(2 * 2.0 * 25.0) = 10.0
    const AccCeiling c =
        EvaluateAccCeiling({MakeConstraint(PolicyConstraint::Kind::STOP_AT_S, 25.0, 0.0, "lead_vehicle")}, 2.0);
    EXPECT_NEAR(c.ceiling_mps, 10.0, 1e-9);
    EXPECT_TRUE(c.stop_requested);
    EXPECT_DOUBLE_EQ(c.stop_distance_m, 25.0);
}

TEST(AccCeilingTest, StrictestConstraintWinsAcrossKinds)
{
    const AccCeiling c = EvaluateAccCeiling({MakeConstraint(PolicyConstraint::Kind::MAX_SPEED, 0.0, 20.0, "a"),
                                             MakeConstraint(PolicyConstraint::Kind::STOP_AT_S, 25.0, 0.0, "b"),
                                             MakeConstraint(PolicyConstraint::Kind::MAX_SPEED, 0.0, 15.0, "c")},
                                            2.0);
    EXPECT_NEAR(c.ceiling_mps, 10.0, 1e-9);  // the stop's kinematic ceiling is the strictest
}

// This is the separability claim REQ-AD-026 step d rests on: AEB's demand must
// NOT reach the ACC ceiling, or "AEB fired independently and safety won" would
// be unobservable -- the two stages would have reacted to one constraint.
TEST(AccCeilingTest, SafetyTierConstraintsAreExcludedFromTheAccCeiling)
{
    const AccCeiling c = EvaluateAccCeiling(
        {MakeConstraint(PolicyConstraint::Kind::STOP_AT_S, 5.0, 0.0, "aeb", PolicyConstraint::Tier::SAFETY)}, 2.0);
    EXPECT_TRUE(std::isinf(c.ceiling_mps));
    EXPECT_FALSE(c.stop_requested);
}

TEST(AccCeilingTest, ZeroStopDistanceSaturatesToZeroRatherThanNaN)
{
    const AccCeiling c =
        EvaluateAccCeiling({MakeConstraint(PolicyConstraint::Kind::STOP_AT_S, -3.0, 0.0, "lead_vehicle")}, 2.0);
    EXPECT_DOUBLE_EQ(c.ceiling_mps, 0.0);  // negative distance clamps to 0, not to a NaN sqrt
    EXPECT_TRUE(c.stop_requested);
}

// ============================================================================
// State machine (design §4-1, REQ-AD-026 steps b/c/f)
// ============================================================================

TEST(AccLonControllerTest, DisabledConfigNeverLeavesOffAndPassesPedalsThrough)
{
    AccLonController acc{AccLonControllerConfig{}};  // enabled = false
    AccFrameInput    in = CruiseInput(20.0);
    in.driver_throttle  = 0.4;
    in.driver_brake     = 0.1;
    in.ops.acc_toggle   = true;
    in.ops.acc_set_resume = true;

    const AccFrameOutput out = acc.Step(in, 0.05);

    EXPECT_EQ(out.state, AccState::OFF);
    EXPECT_DOUBLE_EQ(out.throttle, 0.4);
    EXPECT_DOUBLE_EQ(out.brake, 0.1);
    EXPECT_FALSE(out.engaged);
}

TEST(AccLonControllerTest, ToggleThenSetReachesActiveAndAdoptsCurrentSpeed)
{
    AccLonController acc{EnabledConfig()};
    PressAndStep(acc, CruiseInput(22.0), &AdasOperations::acc_toggle);
    EXPECT_EQ(acc.State(), AccState::STANDBY);

    const AccFrameOutput out = PressAndStep(acc, CruiseInput(22.0), &AdasOperations::acc_set_resume);
    EXPECT_EQ(out.state, AccState::ACTIVE);
    EXPECT_DOUBLE_EQ(out.set_speed_mps, 22.0);  // real-car standard: SET adopts the current speed
}

TEST(AccLonControllerTest, BrakeCancelsToStandbyAndRemembersTheSetting)
{
    AccLonController acc{EnabledConfig()};
    PressAndStep(acc, CruiseInput(22.0), &AdasOperations::acc_toggle);
    PressAndStep(acc, CruiseInput(22.0), &AdasOperations::acc_set_resume);

    AccFrameInput braking = CruiseInput(22.0);
    braking.driver_brake  = 0.4;
    const AccFrameOutput cancelled = acc.Step(braking, 0.05);

    EXPECT_EQ(cancelled.state, AccState::STANDBY);
    EXPECT_FALSE(cancelled.engaged);
    // REQ-AD-028 段b's brake-origin producer -- the reason the requirement's
    // step could not be evidenced before phase C.
    EXPECT_TRUE(cancelled.driver_override_brake);
    // The setting must SURVIVE the cancel, or "resume restores the previous
    // set speed" (step b) would be unverifiable.
    EXPECT_DOUBLE_EQ(cancelled.set_speed_mps, 22.0);
}

TEST(AccLonControllerTest, AfterCancelItDoesNotReEngageUntilResume)
{
    AccLonController acc{EnabledConfig()};
    PressAndStep(acc, CruiseInput(22.0), &AdasOperations::acc_toggle);
    PressAndStep(acc, CruiseInput(22.0), &AdasOperations::acc_set_resume);

    AccFrameInput braking = CruiseInput(22.0);
    braking.driver_brake  = 0.4;
    acc.Step(braking, 0.05);

    // Pedal released: the function must STAY standby (this is the half of
    // step b a naive "cancel while the pedal is down" implementation fails).
    for (int i = 0; i < 20; ++i)
    {
        const AccFrameOutput out = acc.Step(CruiseInput(22.0), 0.05);
        EXPECT_EQ(out.state, AccState::STANDBY);
        EXPECT_FALSE(out.driver_override_brake);  // the override ends with the pedal
    }

    const AccFrameOutput resumed = PressAndStep(acc, CruiseInput(18.0), &AdasOperations::acc_set_resume);
    EXPECT_EQ(resumed.state, AccState::ACTIVE);
    EXPECT_DOUBLE_EQ(resumed.set_speed_mps, 22.0);  // RESUME, not a fresh SET at 18
}

TEST(AccLonControllerTest, AcceleratorOverrideKeepsActiveAndHandsThePedalsBack)
{
    AccLonController acc{EnabledConfig()};
    PressAndStep(acc, CruiseInput(20.0), &AdasOperations::acc_toggle);
    PressAndStep(acc, CruiseInput(20.0), &AdasOperations::acc_set_resume);

    AccFrameInput pressing = CruiseInput(20.0);
    pressing.driver_throttle = 0.6;
    const AccFrameOutput out = acc.Step(pressing, 0.05);

    EXPECT_EQ(out.state, AccState::ACTIVE);  // NOT a state transition (design §4-1)
    EXPECT_TRUE(out.driver_override_accel);
    EXPECT_FALSE(out.engaged);
    EXPECT_DOUBLE_EQ(out.throttle, 0.6);
    EXPECT_DOUBLE_EQ(out.brake, 0.0);  // ACC's own brake generation is suppressed

    // Releasing returns to following with NO resume needed.
    const AccFrameOutput released = acc.Step(CruiseInput(20.0), 0.05);
    EXPECT_EQ(released.state, AccState::ACTIVE);
    EXPECT_FALSE(released.driver_override_accel);
    EXPECT_TRUE(released.engaged);
}

TEST(AccLonControllerTest, PowerOffForgetsTheSettingSoResumeCannotRestoreIt)
{
    AccLonController acc{EnabledConfig()};
    PressAndStep(acc, CruiseInput(25.0), &AdasOperations::acc_toggle);
    PressAndStep(acc, CruiseInput(25.0), &AdasOperations::acc_set_resume);
    PressAndStep(acc, CruiseInput(25.0), &AdasOperations::acc_toggle);  // OFF
    EXPECT_EQ(acc.State(), AccState::OFF);

    PressAndStep(acc, CruiseInput(10.0), &AdasOperations::acc_toggle);  // STANDBY again
    const AccFrameOutput out = PressAndStep(acc, CruiseInput(10.0), &AdasOperations::acc_set_resume);
    EXPECT_DOUBLE_EQ(out.set_speed_mps, 10.0);  // a fresh SET, not the forgotten 25
}

// ---- availability band (REQ-AD-026 step f) ---------------------------------

TEST(AccLonControllerTest, LeavingTheAvailabilityBandDemotesToStandbyAndStopsOutput)
{
    AccLonControllerConfig cfg = EnabledConfig();
    cfg.min_speed_mps          = 8.0;
    AccLonController acc{cfg};

    PressAndStep(acc, CruiseInput(20.0), &AdasOperations::acc_toggle);
    PressAndStep(acc, CruiseInput(20.0), &AdasOperations::acc_set_resume);
    EXPECT_EQ(acc.State(), AccState::ACTIVE);

    AccFrameInput slow = CruiseInput(5.0);
    slow.driver_throttle = 0.2;
    const AccFrameOutput out = acc.Step(slow, 0.05);

    EXPECT_EQ(out.state, AccState::STANDBY);
    EXPECT_FALSE(out.engaged);
    EXPECT_DOUBLE_EQ(out.throttle, 0.2);  // the human's pedal, untouched

    // Back inside the band, RESUME restores the SAME target (step f's
    // "域内復帰で resume 可" -- demotion must not have destroyed the setting).
    const AccFrameOutput resumed = PressAndStep(acc, CruiseInput(12.0), &AdasOperations::acc_set_resume);
    EXPECT_EQ(resumed.state, AccState::ACTIVE);
    EXPECT_DOUBLE_EQ(resumed.set_speed_mps, 20.0);
}

TEST(AccLonControllerTest, MaxSpeedOfZeroMeansNoUpperBound)
{
    AccLonControllerConfig cfg = EnabledConfig();
    cfg.max_speed_mps          = 0.0;  // documented sentinel
    AccLonController acc{cfg};

    PressAndStep(acc, CruiseInput(60.0), &AdasOperations::acc_toggle);
    const AccFrameOutput out = PressAndStep(acc, CruiseInput(60.0), &AdasOperations::acc_set_resume);
    EXPECT_EQ(out.state, AccState::ACTIVE);  // 0 must not read as "cap at 0 m/s"
}

// ============================================================================
// Setting changes and the setting-vs-effect split (steps e/g/h)
// ============================================================================

TEST(AccLonControllerTest, SpeedUpAndDownMoveTheSettingByOneStepPerPress)
{
    AccLonControllerConfig cfg = EnabledConfig();
    cfg.set_speed_step_mps     = 1.39;
    AccLonController acc{cfg};

    PressAndStep(acc, CruiseInput(20.0), &AdasOperations::acc_toggle);
    PressAndStep(acc, CruiseInput(20.0), &AdasOperations::acc_set_resume);
    EXPECT_FALSE(acc.SettingEverChanged());

    const AccFrameOutput up = PressAndStep(acc, CruiseInput(20.0), &AdasOperations::acc_speed_up);
    EXPECT_NEAR(up.set_speed_mps, 21.39, 1e-9);
    EXPECT_TRUE(acc.SettingEverChanged());

    const AccFrameOutput down = PressAndStep(acc, CruiseInput(20.0), &AdasOperations::acc_speed_down);
    EXPECT_NEAR(down.set_speed_mps, 20.0, 1e-9);
}

TEST(AccLonControllerTest, EachDecodedPressMovesTheSettingExactlyOneStep)
{
    // WHERE THE "held button must not ramp" GUARANTEE ACTUALLY LIVES: not
    // here. AccLonController consumes ALREADY-DECODED edges, so a flag left
    // true for ten frames IS ten presses as far as this class is concerned,
    // and that is what this test pins -- one press, one step, ten times, with
    // no extra latch inside ACC swallowing or duplicating any of them.
    // The level-to-edge conversion is DecodeAdasOperations' job and is pinned
    // by AdasOperationsTest.OnlyRisingEdgesProduceOperations below. Splitting
    // the claim this way is deliberate: a second latch here would silently
    // change what "one press" means depending on which layer you asked.
    AccLonController acc{EnabledConfig()};
    PressAndStep(acc, CruiseInput(20.0), &AdasOperations::acc_toggle);
    PressAndStep(acc, CruiseInput(20.0), &AdasOperations::acc_set_resume);

    AccFrameInput held    = CruiseInput(20.0);
    held.ops.acc_speed_up = true;
    AccFrameOutput out;
    for (int i = 0; i < 10; ++i) out = acc.Step(held, 0.05);

    EXPECT_NEAR(out.set_speed_mps, 20.0 + 10 * 1.39, 1e-6);
}

TEST(AccLonControllerTest, ThwCycleAdvancesThroughAllThreeStagesAndWraps)
{
    AccLonControllerConfig cfg = EnabledConfig();
    cfg.thw_default_stage      = 0;
    AccLonController acc{cfg};
    PressAndStep(acc, CruiseInput(20.0), &AdasOperations::acc_toggle);

    EXPECT_NEAR(acc.Config().thw_stages.AtStage(acc.ThwStage()), 1.0, 1e-9);
    EXPECT_NEAR(PressAndStep(acc, CruiseInput(20.0), &AdasOperations::acc_thw_cycle).thw_setting_s, 1.6, 1e-9);
    EXPECT_NEAR(PressAndStep(acc, CruiseInput(20.0), &AdasOperations::acc_thw_cycle).thw_setting_s, 2.2, 1e-9);
    EXPECT_NEAR(PressAndStep(acc, CruiseInput(20.0), &AdasOperations::acc_thw_cycle).thw_setting_s, 1.0, 1e-9);
}

TEST(AccLonControllerTest, EffectiveCapIsTheMinOfSettingCeilingAndSpeedLimit)
{
    AccLonControllerConfig cfg = EnabledConfig();
    cfg.respect_speed_limit    = true;
    AccLonController acc{cfg};

    PressAndStep(acc, CruiseInput(30.0), &AdasOperations::acc_toggle);
    PressAndStep(acc, CruiseInput(30.0), &AdasOperations::acc_set_resume);  // set speed 30

    AccFrameInput in       = CruiseInput(30.0);
    in.speed_limit_mps     = 25.0;
    in.policy.ceiling_mps  = 28.0;
    EXPECT_DOUBLE_EQ(acc.Step(in, 0.05).effective_cap_mps, 25.0);

    in.speed_limit_mps = 40.0;  // limit no longer binding -> ceiling wins
    EXPECT_DOUBLE_EQ(acc.Step(in, 0.05).effective_cap_mps, 28.0);
}

TEST(AccLonControllerTest, SpeedLimitIsIgnoredWhenRespectSpeedLimitIsOff)
{
    // The other pole of step g's two-configuration comparison. Without this
    // the positive case above could pass on a controller that ALWAYS honours
    // the limit and simply ignores the switch.
    AccLonControllerConfig cfg = EnabledConfig();
    cfg.respect_speed_limit    = false;
    AccLonController acc{cfg};

    PressAndStep(acc, CruiseInput(30.0), &AdasOperations::acc_toggle);
    PressAndStep(acc, CruiseInput(30.0), &AdasOperations::acc_set_resume);

    AccFrameInput in   = CruiseInput(30.0);
    in.speed_limit_mps = 15.0;
    EXPECT_DOUBLE_EQ(acc.Step(in, 0.05).effective_cap_mps, 30.0);
}

TEST(AccLonControllerTest, AnUnavailableSpeedLimitDoesNotBecomeAZeroCap)
{
    AccLonControllerConfig cfg = EnabledConfig();
    cfg.respect_speed_limit    = true;
    AccLonController acc{cfg};

    PressAndStep(acc, CruiseInput(20.0), &AdasOperations::acc_toggle);
    PressAndStep(acc, CruiseInput(20.0), &AdasOperations::acc_set_resume);

    AccFrameInput in   = CruiseInput(20.0);
    in.speed_limit_mps = 0.0;  // "unknown", NOT "stop"
    EXPECT_DOUBLE_EQ(acc.Step(in, 0.05).effective_cap_mps, 20.0);
}

// ============================================================================
// Speed loop
// ============================================================================

TEST(AccLonControllerTest, BelowTargetItAccelerates_AboveTargetItBrakes)
{
    AccLonController acc{EnabledConfig()};
    PressAndStep(acc, CruiseInput(20.0), &AdasOperations::acc_toggle);
    PressAndStep(acc, CruiseInput(20.0), &AdasOperations::acc_set_resume);

    const AccFrameOutput slow = acc.Step(CruiseInput(14.0), 0.05);
    EXPECT_GT(slow.throttle, 0.0);
    EXPECT_DOUBLE_EQ(slow.brake, 0.0);

    const AccFrameOutput fast = acc.Step(CruiseInput(26.0), 0.05);
    EXPECT_DOUBLE_EQ(fast.throttle, 0.0);
    EXPECT_GT(fast.brake, 0.0);
}

TEST(AccLonControllerTest, InsideTheDeadbandNeitherPedalIsCommanded)
{
    AccLonControllerConfig cfg = EnabledConfig();
    cfg.speed_deadband_mps     = 0.2;
    AccLonController acc{cfg};
    PressAndStep(acc, CruiseInput(20.0), &AdasOperations::acc_toggle);
    PressAndStep(acc, CruiseInput(20.0), &AdasOperations::acc_set_resume);

    const AccFrameOutput out = acc.Step(CruiseInput(20.1), 0.05);
    EXPECT_DOUBLE_EQ(out.throttle, 0.0);
    EXPECT_DOUBLE_EQ(out.brake, 0.0);
    EXPECT_TRUE(out.engaged);  // engaged and deliberately quiet is not "not engaged"
}

// ============================================================================
// Stop&Go (REQ-AD-031 段a)
// ============================================================================

namespace
{
// Drives the controller to ACTIVE and then to a standing stop against a stop
// constraint, returning the frame the hold engaged on.
AccFrameOutput ReachStopHold(AccLonController& acc)
{
    PressAndStep(acc, CruiseInput(10.0), &AdasOperations::acc_toggle);
    PressAndStep(acc, CruiseInput(10.0), &AdasOperations::acc_set_resume);

    AccFrameInput stopped        = CruiseInput(0.0);
    stopped.policy.stop_requested = true;
    stopped.policy.ceiling_mps    = 0.0;
    return acc.Step(stopped, 0.05);
}
}  // namespace

TEST(AccLonControllerTest, StoppedAgainstAStopTargetEngagesTheHoldBrake)
{
    AccLonControllerConfig cfg   = EnabledConfig();
    cfg.stop_and_go.hold_brake   = 0.3;
    AccLonController acc{cfg};

    const AccFrameOutput held = ReachStopHold(acc);
    EXPECT_TRUE(held.stop_hold);
    EXPECT_DOUBLE_EQ(held.throttle, 0.0);
    EXPECT_DOUBLE_EQ(held.brake, 0.3);
}

TEST(AccLonControllerTest, StoppedWithNoStopTargetDoesNotEngageTheHold)
{
    // The distinguishing observation: "the car is at 0 m/s" and "the car is
    // being held at 0 m/s BY the function" are different facts. Without the
    // stop_requested precondition the hold would latch at every stop the
    // driver made by themselves.
    AccLonController acc{EnabledConfig()};
    PressAndStep(acc, CruiseInput(10.0), &AdasOperations::acc_toggle);
    PressAndStep(acc, CruiseInput(10.0), &AdasOperations::acc_set_resume);

    AccFrameInput stopped_freely = CruiseInput(0.0);
    stopped_freely.policy.stop_requested = false;
    EXPECT_FALSE(acc.Step(stopped_freely, 0.05).stop_hold);
}

TEST(AccLonControllerTest, TheHoldPersistsWhileTheDriverDoesNothing)
{
    AccLonController acc{EnabledConfig()};
    ReachStopHold(acc);

    AccFrameInput idle        = CruiseInput(0.0);
    idle.policy.stop_requested = true;
    for (int i = 0; i < 40; ++i)
    {
        const AccFrameOutput out = acc.Step(idle, 0.05);
        EXPECT_TRUE(out.stop_hold) << "frame " << i;
        EXPECT_DOUBLE_EQ(out.throttle, 0.0);
    }
}

TEST(AccLonControllerTest, OnlyTheHumanAcceleratorReleasesTheHold)
{
    AccLonControllerConfig cfg               = EnabledConfig();
    cfg.stop_and_go.restart_accel_threshold  = 0.10;
    AccLonController acc{cfg};
    ReachStopHold(acc);

    // The stop target CLEARING is not a restart trigger: 段a says the human is
    // the only one, and the lead pulling away must not launch the car.
    AccFrameInput cleared = CruiseInput(0.0);
    cleared.policy.stop_requested = false;
    EXPECT_TRUE(acc.Step(cleared, 0.05).stop_hold);

    // A press below the RESTART threshold is not a trigger either -- and this
    // is the case that pins the ordering inside Step(): 0.06 is above the
    // generic accel_override_threshold (0.05) and below restart_accel_threshold
    // (0.10). If the override branch were consulted first, this frame would end
    // the hold and restart_accel_threshold would be dead config whose value
    // changes nothing.
    AccFrameInput feather   = cleared;
    feather.driver_throttle = 0.06;
    EXPECT_TRUE(acc.Step(feather, 0.05).stop_hold);

    AccFrameInput press   = cleared;
    press.driver_throttle = 0.4;
    const AccFrameOutput released = acc.Step(press, 0.05);
    EXPECT_FALSE(released.stop_hold);
}

TEST(AccLonControllerTest, StopAndGoDisabledNeverHolds)
{
    AccLonControllerConfig cfg = EnabledConfig();
    cfg.stop_and_go.enabled    = false;
    AccLonController acc{cfg};
    EXPECT_FALSE(ReachStopHold(acc).stop_hold);
}

// ============================================================================
// Exclusivity demotion (design §6) -- the ACC half; the MSL half and the
// "later ON wins" arbitration itself live in test_ManualAdasPhaseC.cpp.
// ============================================================================

TEST(AccLonControllerTest, SuspensionDemotesActiveToStandbyButKeepsTheSetting)
{
    AccLonController acc{EnabledConfig()};
    PressAndStep(acc, CruiseInput(20.0), &AdasOperations::acc_toggle);
    PressAndStep(acc, CruiseInput(20.0), &AdasOperations::acc_set_resume);

    AccFrameInput suspended = CruiseInput(20.0);
    suspended.suspended     = true;
    const AccFrameOutput out = acc.Step(suspended, 0.05);

    EXPECT_EQ(out.state, AccState::STANDBY);  // demoted, not switched OFF
    EXPECT_FALSE(out.engaged);
    EXPECT_DOUBLE_EQ(out.set_speed_mps, 20.0);

    const AccFrameOutput resumed = PressAndStep(acc, CruiseInput(20.0), &AdasOperations::acc_set_resume);
    EXPECT_EQ(resumed.state, AccState::ACTIVE);
    EXPECT_DOUBLE_EQ(resumed.set_speed_mps, 20.0);
}

// ============================================================================
// Button decoding
// ============================================================================

TEST(AdasOperationsTest, OnlyRisingEdgesProduceOperations)
{
    const std::uint32_t none = 0;
    const std::uint32_t up   = ButtonBits::ACC_SPEED_UP;

    EXPECT_TRUE(DecodeAdasOperations(up, none).acc_speed_up);
    EXPECT_FALSE(DecodeAdasOperations(up, up).acc_speed_up);    // held, not a new press
    EXPECT_FALSE(DecodeAdasOperations(none, up).acc_speed_up);  // release is not a press
}

TEST(AdasOperationsTest, EachBitMapsToItsOwnOperationAndNoOther)
{
    // A transposed bit would be a silent, permanent mis-mapping of the stalk;
    // asserting each bit in isolation is what catches it.
    EXPECT_TRUE(DecodeAdasOperations(ButtonBits::ACC_TOGGLE, 0).acc_toggle);
    EXPECT_FALSE(DecodeAdasOperations(ButtonBits::ACC_TOGGLE, 0).msl_toggle);
    EXPECT_TRUE(DecodeAdasOperations(ButtonBits::ACC_SET_RESUME, 0).acc_set_resume);
    EXPECT_TRUE(DecodeAdasOperations(ButtonBits::ACC_SPEED_DOWN, 0).acc_speed_down);
    EXPECT_TRUE(DecodeAdasOperations(ButtonBits::ACC_THW_CYCLE, 0).acc_thw_cycle);
    EXPECT_TRUE(DecodeAdasOperations(ButtonBits::MSL_TOGGLE, 0).msl_toggle);
    // The phase-A/B bits must not leak into the ADAS stalk.
    const AdasOperations from_lights = DecodeAdasOperations(ButtonBits::HEADLIGHT | ButtonBits::AUTO_RESUME, 0);
    EXPECT_FALSE(from_lights.acc_toggle);
    EXPECT_FALSE(from_lights.acc_set_resume);
    EXPECT_FALSE(from_lights.msl_toggle);
}
