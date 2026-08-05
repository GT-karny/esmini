// req-vd-ad:REQ-AD-027 / req-vd-ad:REQ-AD-028 / vd-func:FUNC-080
//
// Phase-D unit tests for the lateral assist: the correction law and its sign,
// the lateral envelope, the departure judgement (TLC + margin, with
// hysteresis), the speed band, the two suppressions, the warning_only
// structural separation, the steering-origin DriverOverride, and the two
// invariants the coordinator depends on (LKA vs FFB peer routing mutual
// exclusion, and the split-configuration UNAVAILABLE).
//
// WHY SOME OF THESE LOOK LIKE THEY ARE TESTING THE OBVIOUS. Three of them are
// pinning things that are only obvious once stated, and each corresponds to a
// documented failure mode this project has already paid for elsewhere:
//   * the SIGN of the correction (a sign error inverts the feature rather than
//     degrading it),
//   * warning_only producing a BIT-IDENTICAL command (a "mode" that leaks even
//     a tiny correction makes REQ-AD-027 step f false while every other
//     observable still looks right),
//   * the envelope's amplitude bound surviving the rate stage (the ordering
//     bug that keeps AdSteeringEnvelope's own jerk stage shipped-disabled).

#include "gt_esmini/control/manualdrive/AdasCoexistenceStack.hpp"
#include "gt_esmini/control/manualdrive/LaneKeepAssist.hpp"
#include "gt_esmini/control/virtualdriver/AdasFunctionReport.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <string>

using namespace gt_esmini;

namespace
{

constexpr double kDt = 0.05;

LaneKeepAssistConfig LkaOn()
{
    LaneKeepAssistConfig cfg;
    cfg.enabled = true;
    return cfg;
}

// A vehicle in a 3.5 m lane, 2.0 m wide, at 25 m/s, hands steady on the wheel.
// half width 1.75, vehicle half width 1.0 -> 0.75 m of margin at dead centre.
LkaFrameInput CentredInput()
{
    LkaFrameInput in;
    in.owns_lateral         = true;
    in.lane_valid           = true;
    in.lane_offset_m        = 0.0;
    in.lane_half_width_m    = 1.75;
    in.vehicle_half_width_m = 1.0;
    in.lateral_speed_mps    = 0.0;
    in.ego_speed_mps        = 25.0;
    in.driver_steering      = 0.0;
    in.indicator_active     = false;
    return in;
}

// Runs `frames` frames of the same input so a latch/timer-dependent verdict is
// reached the way it is in a real run, rather than asserted on frame 1.
LkaFrameOutput RunFrames(const LaneKeepAssistConfig& cfg,
                         const LkaFrameInput&        in,
                         LaneKeepAssistState&        state,
                         int                         frames)
{
    LkaFrameOutput out;
    for (int i = 0; i < frames; ++i) out = ComputeLaneKeepAssist(cfg, in, kDt, state);
    return out;
}

const AdasFunctionState* Find(const std::vector<AdasFunctionState>& r, const char* custom_name)
{
    for (const auto& f : r)
        if (f.custom_name == custom_name) return &f;
    return nullptr;
}

ManualAdasEnableFlags LateralFlags()
{
    ManualAdasEnableFlags f;
    f.lka = true;
    f.ldw = true;
    return f;
}

}  // namespace

// ===========================================================================
// The sign chain (LaneKeepAssist.hpp's SIGN CHAIN block)
// ===========================================================================

TEST(LaneKeepAssist, DriftingLeftProducesARightwardCorrection)
{
    // Vehicle-left-positive offset + moving left -> the assist must steer
    // RIGHT, and positive steering IS right (RealVehicle::StepLateralAndAttitude
    // negates it into the front-wheel angle). A negative correction here would
    // mean the assist is pushing the vehicle further out of the lane.
    LaneKeepAssistState state;
    auto                in  = CentredInput();
    in.lane_offset_m        = 0.60;   // left of centre
    in.lateral_speed_mps    = 0.35;   // still moving left

    const auto out = RunFrames(LkaOn(), in, state, 20);
    ASSERT_TRUE(out.departing);
    EXPECT_GT(out.correction, 0.0);
}

TEST(LaneKeepAssist, DriftingRightProducesALeftwardCorrectionOfEqualMagnitude)
{
    // The mirror case, asserted as an EQUAL MAGNITUDE rather than merely an
    // opposite sign: an asymmetry would show up in the E2E drift pair as one
    // side being held and the other not, which is far harder to attribute.
    LaneKeepAssistState left_state, right_state;

    auto left           = CentredInput();
    left.lane_offset_m  = 0.60;
    left.lateral_speed_mps = 0.35;

    auto right             = CentredInput();
    right.lane_offset_m    = -0.60;
    right.lateral_speed_mps = -0.35;

    const auto l = RunFrames(LkaOn(), left, left_state, 20);
    const auto r = RunFrames(LkaOn(), right, right_state, 20);

    ASSERT_TRUE(l.departing);
    ASSERT_TRUE(r.departing);
    EXPECT_LT(r.correction, 0.0);
    EXPECT_NEAR(l.correction, -r.correction, 1e-12);
}

TEST(LaneKeepAssist, CorrectionIsAddedToTheDriverSteeringNeverReplacesIt)
{
    // "Assist", not "control": the human's own command must still be in the
    // output. Steering held at a constant value so the rate-based suppression
    // does not fire (that path has its own test).
    LaneKeepAssistState state;
    auto                in = CentredInput();
    in.lane_offset_m       = 0.60;
    in.lateral_speed_mps   = 0.35;
    in.driver_steering     = 0.20;

    const auto out = RunFrames(LkaOn(), in, state, 20);
    ASSERT_TRUE(out.correcting);
    EXPECT_NEAR(out.steer_out, 0.20 + out.correction, 1e-12);
}

// ===========================================================================
// The lateral envelope (design §5-2)
// ===========================================================================

TEST(LkaCorrectionEnvelope, ClampsAmplitudeAndNeverExceedsItAfterTheRateStage)
{
    // The ordering hazard AdSteeringEnvelope's jerk stage got wrong: a second
    // shaping stage must not be able to push the value back OUTSIDE the bound
    // the first stage imposed. Here the rate stage can only contract toward
    // `prev`, which was itself inside the bound -- asserted over a sweep rather
    // than at one point so a future reordering cannot pass by luck.
    for (int i = -30; i <= 30; ++i)
    {
        const double raw  = i * 0.1;   // far outside the 0.08 amplitude bound
        for (int j = -8; j <= 8; ++j)
        {
            const double prev = j * 0.01;
            const double out  = ApplyLkaCorrectionEnvelope(raw, prev, 0.08, 0.40, kDt);
            EXPECT_LE(std::fabs(out), 0.08 + 1e-12) << "raw=" << raw << " prev=" << prev;
        }
    }
}

TEST(LkaCorrectionEnvelope, RateLimitsTheStepFromThePreviousAppliedValue)
{
    // max_rate 0.40 /s at dt 0.05 -> 0.02 per frame.
    EXPECT_NEAR(ApplyLkaCorrectionEnvelope(0.08, 0.0, 0.08, 0.40, kDt), 0.02, 1e-12);
    EXPECT_NEAR(ApplyLkaCorrectionEnvelope(-0.08, 0.0, 0.08, 0.40, kDt), -0.02, 1e-12);
}

TEST(LkaCorrectionEnvelope, NonPositiveDtAppliesAmplitudeButNotRate)
{
    // No rate can be attributed to a non-positive time step (the same rule
    // ComputeMeasuredDecel and AdSteeringEnvelope follow). The amplitude bound
    // still holds -- it is not a rate claim.
    EXPECT_NEAR(ApplyLkaCorrectionEnvelope(0.50, 0.0, 0.08, 0.40, 0.0), 0.08, 1e-12);
    EXPECT_NEAR(ApplyLkaCorrectionEnvelope(0.50, 0.0, 0.08, 0.40, -1.0), 0.08, 1e-12);
}

TEST(LkaCorrectionEnvelope, ZeroAmplitudeYieldsExactlyZeroNotUnbounded)
{
    // A misconfigured limit must fail SAFE. This also makes correction_max=0 a
    // usable way to disable the output while leaving the judgement running.
    EXPECT_EQ(ApplyLkaCorrectionEnvelope(1.0, 0.5, 0.0, 0.40, kDt), 0.0);
    EXPECT_EQ(ApplyLkaCorrectionEnvelope(1.0, 0.5, -1.0, 0.40, kDt), 0.0);
}

TEST(LaneKeepAssist, CorrectionNeverExceedsTheConfiguredAuthorityOverALongDrift)
{
    // The property the requirement actually cares about ("the human can always
    // overpower the assist"), asserted on the COMPONENT rather than on the
    // envelope function: a law that grew without bound would still pass the
    // envelope's own test if a future edit routed around it.
    LaneKeepAssistState  state;
    LaneKeepAssistConfig cfg = LkaOn();
    auto                 in  = CentredInput();
    in.lane_offset_m         = 1.50;   // hard against the line
    in.lateral_speed_mps     = 2.00;   // and leaving fast

    for (int i = 0; i < 200; ++i)
    {
        const auto out = ComputeLaneKeepAssist(cfg, in, kDt, state);
        EXPECT_LE(std::fabs(out.correction), cfg.correction_max + 1e-12);
    }
}

// ===========================================================================
// Departure judgement: TLC, margin, hysteresis
// ===========================================================================

TEST(LaneKeepAssist, CentredAndStableIsNotADeparture)
{
    LaneKeepAssistState state;
    const auto          out = RunFrames(LkaOn(), CentredInput(), state, 20);
    EXPECT_TRUE(out.evaluated);
    EXPECT_FALSE(out.departing);
    EXPECT_FALSE(out.correcting);
    EXPECT_EQ(out.correction, 0.0);
}

TEST(LaneKeepAssist, TlcFiresBeforeTheMarginThresholdIsReached)
{
    // The whole reason TLC is the PRIMARY criterion: a vehicle can still be
    // comfortably inside the margin band and be about to leave. Here the margin
    // is 0.55 (well above margin_threshold 0.15) while TLC is 0.55/0.5 = 1.1 s
    // (below the 1.5 s threshold), so only the TLC arm can explain a departure.
    LaneKeepAssistState state;
    auto                in = CentredInput();
    in.lane_offset_m       = 0.20;
    in.lateral_speed_mps   = 0.50;

    const auto out = ComputeLaneKeepAssist(LkaOn(), in, kDt, state);
    EXPECT_NEAR(out.margin_m, 0.55, 1e-12);
    EXPECT_GT(out.margin_m, LkaOn().margin_threshold_m);
    EXPECT_NEAR(out.tlc_s, 1.1, 1e-9);
    EXPECT_TRUE(out.departing);
}

TEST(LaneKeepAssist, MovingBackTowardTheCentreIsNotADeparture)
{
    // Off-centre but RETURNING. Firing here would mean the assist fights a
    // driver who is already correcting -- and it is the case a naive |offset|
    // trigger gets wrong.
    LaneKeepAssistState state;
    auto                in = CentredInput();
    in.lane_offset_m       = 0.40;   // left of centre...
    in.lateral_speed_mps   = -0.50;  // ...moving right, i.e. back

    const auto out = ComputeLaneKeepAssist(LkaOn(), in, kDt, state);
    EXPECT_LT(out.tlc_s, 0.0) << "TLC must not be computed for an inward-moving vehicle";
    EXPECT_FALSE(out.departing);
}

TEST(LaneKeepAssist, MarginArmFiresWithNoLateralMotionAtAll)
{
    // The other arm: parked against the line with zero lateral speed has an
    // infinite TLC, and a TLC-only trigger would never fire on it.
    LaneKeepAssistState state;
    auto                in = CentredInput();
    in.lane_offset_m       = 0.60;   // margin 0.15 <= 0.15
    in.lateral_speed_mps   = 0.0;

    const auto out = ComputeLaneKeepAssist(LkaOn(), in, kDt, state);
    EXPECT_LT(out.tlc_s, 0.0);
    EXPECT_LE(out.margin_m, LkaOn().margin_threshold_m);
    EXPECT_TRUE(out.departing);
}

TEST(LaneKeepAssist, ReleaseNeedsTheFullHysteresisBandNotJustClearingTheTrigger)
{
    // Engage at margin <= 0.15, release only at margin >= 0.30. A vehicle
    // sitting at 0.25 -- past the trigger but inside the band -- must stay
    // latched, otherwise the row chatters between STANDBY and ACTIVE at frame
    // rate and the OSI state column becomes unreadable.
    LaneKeepAssistState state;
    auto                engage = CentredInput();
    engage.lane_offset_m       = 0.60;  // margin 0.15
    ASSERT_TRUE(ComputeLaneKeepAssist(LkaOn(), engage, kDt, state).departing);

    auto inside_band           = CentredInput();
    inside_band.lane_offset_m  = 0.50;  // margin 0.25: past the trigger, inside the band
    EXPECT_TRUE(ComputeLaneKeepAssist(LkaOn(), inside_band, kDt, state).departing);

    auto released              = CentredInput();
    released.lane_offset_m     = 0.20;  // margin 0.55 >= 0.30
    EXPECT_FALSE(ComputeLaneKeepAssist(LkaOn(), released, kDt, state).departing);
}

TEST(LaneKeepAssist, AlreadyPastTheLineReportsTlcZeroNotUnavailable)
{
    // A negative margin with outward motion means the crossing has ALREADY
    // happened. Reporting -1 (this component's "not applicable") would let the
    // stream say "no crossing expected" about a vehicle that has left the lane.
    LaneKeepAssistState state;
    auto                in = CentredInput();
    in.lane_offset_m       = 1.20;   // margin -0.45
    in.lateral_speed_mps   = 0.30;

    const auto out = ComputeLaneKeepAssist(LkaOn(), in, kDt, state);
    EXPECT_LT(out.margin_m, 0.0);
    EXPECT_EQ(out.tlc_s, 0.0);
}

// ===========================================================================
// Gates: config, ownership, lane validity, speed band
// ===========================================================================

TEST(LaneKeepAssist, DisabledOrUnownedOrLanelessProducesNothingAndEvaluatesNothing)
{
    // All three gates collapse to the same OUTPUT, which is why `evaluated`
    // exists: without it, "the assist looked and found nothing" and "the assist
    // never ran" would be the same observation downstream -- the absent-key
    // discipline AdasCoexistenceStack applies to its own bypass detail.
    auto departing         = CentredInput();
    departing.lane_offset_m = 0.60;

    {
        LaneKeepAssistConfig off = LkaOn();
        off.enabled              = false;
        LaneKeepAssistState state;
        const auto          out = ComputeLaneKeepAssist(off, departing, kDt, state);
        EXPECT_FALSE(out.evaluated);
        EXPECT_EQ(out.correction, 0.0);
        EXPECT_EQ(out.steer_out, departing.driver_steering);
    }
    {
        auto                unowned = departing;
        unowned.owns_lateral        = false;
        LaneKeepAssistState state;
        const auto          out = ComputeLaneKeepAssist(LkaOn(), unowned, kDt, state);
        EXPECT_FALSE(out.evaluated);
        EXPECT_EQ(out.correction, 0.0);
    }
    {
        auto                laneless = departing;
        laneless.lane_valid          = false;
        LaneKeepAssistState state;
        const auto          out = ComputeLaneKeepAssist(LkaOn(), laneless, kDt, state);
        EXPECT_FALSE(out.evaluated);
        EXPECT_EQ(out.correction, 0.0);
    }
}

TEST(LaneKeepAssist, GatedFramesDoNotEvolveTheDepartureLatch)
{
    // "Leave the latches frozen, do not evolve them on data the assist is
    // refusing to act on" -- the same rule the longitudinal bypass follows. A
    // stretch of unowned frames must not be able to decide the first owned
    // frame after it.
    LaneKeepAssistState state;

    auto engage           = CentredInput();
    engage.lane_offset_m  = 0.60;
    ASSERT_TRUE(ComputeLaneKeepAssist(LkaOn(), engage, kDt, state).departing);

    auto unowned_centred          = CentredInput();
    unowned_centred.owns_lateral  = false;
    for (int i = 0; i < 40; ++i) ComputeLaneKeepAssist(LkaOn(), unowned_centred, kDt, state);

    // Still latched: the unowned frames neither released it nor advanced it.
    EXPECT_TRUE(state.departing_latched);
}

TEST(LaneKeepAssist, OutsideTheSpeedBandIsEvaluatedButNeverCorrects)
{
    // REQ-AD-027 step e. `evaluated` stays TRUE so the row reports STANDBY
    // rather than UNAVAILABLE: "out of range" is not "switched off", and the
    // three-value discipline is what keeps them apart.
    LaneKeepAssistConfig cfg = LkaOn();
    cfg.min_speed_mps        = 16.0;

    LaneKeepAssistState state;
    auto                slow = CentredInput();
    slow.ego_speed_mps       = 8.0;
    slow.lane_offset_m       = 0.60;
    slow.lateral_speed_mps   = 0.50;

    const auto out = RunFrames(cfg, slow, state, 20);
    EXPECT_TRUE(out.evaluated);
    EXPECT_FALSE(out.in_speed_band);
    EXPECT_FALSE(out.departing);
    EXPECT_EQ(out.correction, 0.0);

    // The SAME stimulus inside the band does correct -- without this half, the
    // test above would also pass on an assist that never works at all.
    LaneKeepAssistState in_band_state;
    auto                fast = slow;
    fast.ego_speed_mps       = 25.0;
    const auto in_band = RunFrames(cfg, fast, in_band_state, 20);
    EXPECT_TRUE(in_band.in_speed_band);
    EXPECT_TRUE(in_band.correcting);
}

TEST(LaneKeepAssist, MaxSpeedZeroMeansNoUpperBound)
{
    // Same convention as ACC's own band (REQ-AD-026 step f), by the shared
    // vocabulary decision. A literal 0 upper bound would silence the assist at
    // every speed, which is the failure this pins.
    LaneKeepAssistConfig cfg = LkaOn();
    cfg.max_speed_mps        = 0.0;

    LaneKeepAssistState state;
    auto                fast = CentredInput();
    fast.ego_speed_mps       = 60.0;
    fast.lane_offset_m       = 0.60;

    EXPECT_TRUE(RunFrames(cfg, fast, state, 5).in_speed_band);
}

// ===========================================================================
// Human steering priority (design §5-3)
// ===========================================================================

TEST(LaneKeepAssist, IndicatorSuppressesBothTheCorrectionAndTheWarning)
{
    // A driver who signalled is not departing, they are changing lanes.
    // Warning them is the false alarm every production LDW suppresses.
    LaneKeepAssistState state;
    auto                in = CentredInput();
    in.lane_offset_m       = 0.60;
    in.lateral_speed_mps   = 0.50;
    in.indicator_active    = true;

    const auto out = RunFrames(LkaOn(), in, state, 20);
    EXPECT_TRUE(out.departing) << "the judgement still runs -- only the outputs are suppressed";
    EXPECT_TRUE(out.suppressed_indicator);
    EXPECT_FALSE(out.warning);
    EXPECT_EQ(out.correction, 0.0);
}

TEST(LaneKeepAssist, DeliberateSteeringSuppressesTheCorrectionButNotTheWarning)
{
    // The asymmetry with the indicator case, and the reason the two flags are
    // separate: a driver hauling the wheel across a line WITHOUT signalling is
    // exactly who the warning is for.
    LaneKeepAssistConfig cfg = LkaOn();
    LaneKeepAssistState  state;

    auto in = CentredInput();
    in.lane_offset_m     = 0.60;
    in.lateral_speed_mps = 0.50;

    // Two frames of steadily-held wheel first, so the rate anchor is real.
    ComputeLaneKeepAssist(cfg, in, kDt, state);
    ComputeLaneKeepAssist(cfg, in, kDt, state);

    // Now a step well past steer_override_rate (0.06 /s * 0.05 s = 0.003).
    in.driver_steering = 0.30;
    const auto out     = ComputeLaneKeepAssist(cfg, in, kDt, state);

    EXPECT_TRUE(out.suppressed_steer);
    EXPECT_TRUE(out.warning) << "the warning survives a steering input";
    EXPECT_EQ(out.correction, 0.0);
}

TEST(LaneKeepAssist, SteeringSuppressionIsHeldAfterTheInputRateFallsBack)
{
    // Without the hold, a driver holding the wheel STEADILY over (rate zero
    // once they stop moving it) would have the assist come straight back and
    // fight them on the next frame.
    LaneKeepAssistConfig cfg   = LkaOn();
    cfg.steer_override_hold_s  = 1.0;

    LaneKeepAssistState state;
    auto                in = CentredInput();
    in.lane_offset_m       = 0.60;
    in.lateral_speed_mps   = 0.50;
    ComputeLaneKeepAssist(cfg, in, kDt, state);

    in.driver_steering = 0.30;                       // the fast input
    ASSERT_TRUE(ComputeLaneKeepAssist(cfg, in, kDt, state).suppressed_steer);

    // Held steady from here: the RATE is now zero, but the hold must stand.
    for (int i = 0; i < 10; ++i)  // 0.5 s
    {
        EXPECT_TRUE(ComputeLaneKeepAssist(cfg, in, kDt, state).suppressed_steer) << "frame " << i;
    }
    for (int i = 0; i < 20; ++i) ComputeLaneKeepAssist(cfg, in, kDt, state);  // past 1.0 s total
    EXPECT_FALSE(ComputeLaneKeepAssist(cfg, in, kDt, state).suppressed_steer);
}

TEST(LaneKeepAssist, ASuppressionStopsTheCorrectionOnTheSameFrame)
{
    // REQ-AD-027 step b's word is 即時中断. This started as the OPPOSITE test
    // -- a rate-limited "graceful release" -- and the requirement is what
    // overruled it: a release that takes several frames keeps the assist
    // pushing against a driver who has just declared they are steering
    // deliberately. Measured before the fix: 0.020 of correction still applied
    // on the frame after the driver's input.
    LaneKeepAssistConfig cfg = LkaOn();
    LaneKeepAssistState  state;

    auto in = CentredInput();
    in.lane_offset_m     = 0.60;
    in.lateral_speed_mps = 0.50;
    const auto engaged   = RunFrames(cfg, in, state, 20);
    ASSERT_GT(engaged.correction, 0.0);

    in.indicator_active       = true;  // suppress from here
    const auto first_suppressed = ComputeLaneKeepAssist(cfg, in, kDt, state);
    EXPECT_EQ(first_suppressed.correction, 0.0);
    EXPECT_FALSE(first_suppressed.correcting);
    EXPECT_EQ(first_suppressed.steer_out, in.driver_steering);
    // And the anchor is zeroed with it, so a re-engagement ramps up from zero
    // rather than resuming from a value the driver has already refused.
    EXPECT_EQ(state.prev_correction, 0.0);
}

TEST(LaneKeepAssist, AnEndedDepartureReleasesThroughTheRateLimitNotAsASnap)
{
    // The OTHER way a correction ends, and the one where the ramp-down is
    // right: the vehicle came back inside the hysteresis band on its own.
    // Nobody objected to the assist, so dropping its whole contribution in one
    // frame would just be a jolt -- the failure mode AdSteeringEnvelope's rate
    // stage exists to prevent on the AD side. Keeping the two endings distinct
    // is the point; collapsing them either makes the suppression late or makes
    // an ordinary release abrupt.
    LaneKeepAssistConfig cfg = LkaOn();
    LaneKeepAssistState  state;

    auto in = CentredInput();
    in.lane_offset_m     = 0.60;
    in.lateral_speed_mps = 0.50;
    const auto engaged   = RunFrames(cfg, in, state, 20);
    ASSERT_GT(engaged.correction, 0.0);

    auto recovered              = CentredInput();  // back to centre, latch releases
    const auto first_released   = ComputeLaneKeepAssist(cfg, recovered, kDt, state);
    ASSERT_FALSE(first_released.departing);
    EXPECT_GT(first_released.correction, 0.0) << "must not snap to zero in one frame";
    EXPECT_LT(first_released.correction, engaged.correction);
    EXPECT_NEAR(engaged.correction - first_released.correction, cfg.correction_rate_max * kDt, 1e-12);
}

// ===========================================================================
// warning_only (REQ-AD-027 step f) -- the structural claim
// ===========================================================================

TEST(LaneKeepAssist, WarningOnlyKeepsTheJudgementIdenticalAndTheCommandUntouched)
{
    // THE test for step f. Same stimulus, same frame count, two configurations:
    // every JUDGEMENT field must agree exactly, and warning_only's steering
    // command must be BIT-IDENTICAL to the driver's own input. Asserting the
    // judgement fields agree is what makes this a claim about the OUTPUT being
    // suppressed rather than about the whole function being switched off.
    auto in = CentredInput();
    in.lane_offset_m     = 0.60;
    in.lateral_speed_mps = 0.50;
    in.driver_steering   = 0.11;

    LaneKeepAssistConfig correcting = LkaOn();
    LaneKeepAssistConfig warning    = LkaOn();
    warning.warning_only            = true;

    LaneKeepAssistState s_correcting, s_warning;
    const auto          c = RunFrames(correcting, in, s_correcting, 20);
    const auto          w = RunFrames(warning, in, s_warning, 20);

    EXPECT_EQ(c.departing, w.departing);
    EXPECT_EQ(c.warning, w.warning);
    EXPECT_EQ(c.in_speed_band, w.in_speed_band);
    EXPECT_NEAR(c.margin_m, w.margin_m, 1e-12);
    EXPECT_NEAR(c.tlc_s, w.tlc_s, 1e-12);

    EXPECT_GT(c.correction, 0.0);
    EXPECT_EQ(w.correction, 0.0);
    EXPECT_EQ(w.steer_out, in.driver_steering) << "cmd.steering must be bit-identical under warning_only";
    EXPECT_NE(c.steer_out, in.driver_steering);
}

// ===========================================================================
// req-vd-ad:REQ-AD-028 段b -- the steering-origin DriverOverride producer
// ===========================================================================

TEST(LaneKeepAssist, SteeringOverrideTracksTheSuppressionNotTheSquashedFrame)
{
    // Phase B's decision, applied to the third producer: the override channel
    // reports "the driver is holding this function off", which is a property of
    // the driver's input over the whole suppression, not of the one frame on
    // which a correction happened to be squashed.
    LaneKeepAssistConfig cfg = LkaOn();
    LaneKeepAssistState  state;

    auto in = CentredInput();
    in.lane_offset_m     = 0.60;
    in.lateral_speed_mps = 0.50;
    ComputeLaneKeepAssist(cfg, in, kDt, state);

    in.driver_steering = 0.30;
    ASSERT_TRUE(ComputeLaneKeepAssist(cfg, in, kDt, state).driver_override_steering);

    // Wheel now held steady: no new "squash" event, but the assist is still
    // being held off, so the override must remain raised.
    for (int i = 0; i < 10; ++i)
    {
        EXPECT_TRUE(ComputeLaneKeepAssist(cfg, in, kDt, state).driver_override_steering) << "frame " << i;
    }
}

TEST(LaneKeepAssist, IndicatorSuppressionDoesNotRaiseTheSteeringOverride)
{
    // OSI's Reason enum has exactly two values and the indicator is not a
    // steering input; reporting it as REASON_STEERING_INPUT would misreport
    // which control the human used. It stays observable as its own flag.
    LaneKeepAssistState state;
    auto                in = CentredInput();
    in.lane_offset_m       = 0.60;
    in.lateral_speed_mps   = 0.50;
    in.indicator_active    = true;

    const auto out = RunFrames(LkaOn(), in, state, 20);
    EXPECT_TRUE(out.suppressed_indicator);
    EXPECT_FALSE(out.driver_override_steering);
}

TEST(LaneKeepAssist, TheSteeringOverrideDoesNotBlinkWithTheGeometry)
{
    // This test used to assert the OPPOSITE (no departure -> no override), on
    // the reading that there has to BE an assist to hold off. Running
    // md_lka_human_steer overruled it: while the driver steers across the road
    // the vehicle passes through the CENTRE of each lane it crosses, and on the
    // one frame it did (t=7.30, offset 0.115 m) the departure verdict went false
    // and the override channel blinked off for 50 ms -- with the driver's hand
    // doing exactly the same thing before and after.
    //
    // That is the failure mode phase B rejected for the accelerator producer,
    // reproduced verbatim: OSI's DriverOverride asks whether the driver has
    // overridden the FUNCTION, which is a property of their INPUT. So the
    // override follows the suppression alone, and a momentarily-centred vehicle
    // does not clear it.
    LaneKeepAssistConfig cfg = LkaOn();
    LaneKeepAssistState  state;

    auto departing = CentredInput();
    departing.lane_offset_m     = 0.60;
    departing.lateral_speed_mps = 0.50;
    ComputeLaneKeepAssist(cfg, departing, kDt, state);

    departing.driver_steering = 0.30;  // the fast input
    ASSERT_TRUE(ComputeLaneKeepAssist(cfg, departing, kDt, state).driver_override_steering);

    // Same driver input, but the vehicle is momentarily back at the lane centre
    // so the departure verdict clears. The override must NOT clear with it.
    auto centred           = CentredInput();
    centred.driver_steering = 0.30;
    const auto out          = ComputeLaneKeepAssist(cfg, centred, kDt, state);
    EXPECT_FALSE(out.departing);
    EXPECT_TRUE(out.suppressed_steer);
    EXPECT_TRUE(out.driver_override_steering);
}

// ===========================================================================
// The HVD rows (design §8-2), including the split configuration
// ===========================================================================

TEST(ManualAdasLateralReport, RowsCarryTheirNativeOsiNamesNeverOther)
{
    ManualAdasDecision decision;
    const auto         rows = BuildManualAdasFunctionReport(LateralFlags(),
                                                    /*owns_longitudinal_domain=*/false,
                                                    /*owns_lateral_domain=*/true,
                                                    decision,
                                                    {});

    const auto* lka = Find(rows, "gt.lka");
    const auto* ldw = Find(rows, "gt.ldw");
    ASSERT_NE(lka, nullptr);
    ASSERT_NE(ldw, nullptr);
    EXPECT_EQ(lka->name, osi_adas::NAME_LANE_KEEPING_ASSIST);
    EXPECT_EQ(ldw->name, osi_adas::NAME_LANE_DEPARTURE_WARNING);
    EXPECT_NE(lka->name, osi_adas::NAME_OTHER);
    EXPECT_NE(ldw->name, osi_adas::NAME_OTHER);
}

TEST(ManualAdasLateralReport, LateralRowsFollowLateralOwnershipNotLongitudinal)
{
    // md-split-no-double-equipment for the phase-D rows. The reverse split
    // (lat=VD, lon=manual) must silence LKA while leaving the longitudinal
    // functions alone -- and phase A-C's single ownership flag could not have
    // expressed that.
    ManualAdasDecision decision;
    decision.lka_correcting = true;
    decision.ldw_warning    = true;

    const auto owned = BuildManualAdasFunctionReport(LateralFlags(),
                                                     /*owns_longitudinal_domain=*/false,
                                                     /*owns_lateral_domain=*/true,
                                                     decision,
                                                     {});
    EXPECT_EQ(Find(owned, "gt.lka")->state, osi_adas::STATE_ACTIVE);
    EXPECT_EQ(Find(owned, "gt.ldw")->state, osi_adas::STATE_ACTIVE);

    const auto split = BuildManualAdasFunctionReport(LateralFlags(),
                                                     /*owns_longitudinal_domain=*/true,
                                                     /*owns_lateral_domain=*/false,
                                                     decision,
                                                     {});
    EXPECT_EQ(Find(split, "gt.lka")->state, osi_adas::STATE_UNAVAILABLE);
    EXPECT_EQ(Find(split, "gt.ldw")->state, osi_adas::STATE_UNAVAILABLE);
}

TEST(ManualAdasLateralReport, DisabledConfigEmitsNoLateralRowsAtAll)
{
    // A phase-A/B/C-era config must keep producing exactly the rows it always
    // did, so the committed ManualDrive baselines stay unmoved.
    ManualAdasDecision decision;
    const auto         rows = BuildManualAdasFunctionReport(ManualAdasEnableFlags{},
                                                    /*owns_longitudinal_domain=*/true,
                                                    /*owns_lateral_domain=*/true,
                                                    decision,
                                                    {});
    EXPECT_EQ(Find(rows, "gt.lka"), nullptr);
    EXPECT_EQ(Find(rows, "gt.ldw"), nullptr);
}

TEST(ManualAdasLateralReport, SteeringOverridePopulatesTheOsiReasonNotACustomState)
{
    // REASON_STEERING_INPUT EXISTS in OSI, unlike an accelerator value -- so
    // using custom_state here would be inventing a private channel over a
    // standard one, and would make the observation incomparable across stacks.
    ManualAdasDecision decision;
    decision.lka_driver_override_steering = true;

    const auto  rows = BuildManualAdasFunctionReport(LateralFlags(),
                                                    /*owns_longitudinal_domain=*/false,
                                                    /*owns_lateral_domain=*/true,
                                                    decision,
                                                    {});
    const auto* lka  = Find(rows, "gt.lka");
    ASSERT_NE(lka, nullptr);
    EXPECT_TRUE(lka->driver_override.reported);
    EXPECT_TRUE(lka->driver_override.active);
    ASSERT_EQ(lka->driver_override.reasons.size(), 1u);
    EXPECT_EQ(lka->driver_override.reasons[0], osi_adas::REASON_STEERING_INPUT);
    EXPECT_TRUE(lka->custom_state.empty());

    // The LDW row is the in-run negative control: same frame, same run,
    // evaluated but not overridden.
    const auto* ldw = Find(rows, "gt.ldw");
    ASSERT_NE(ldw, nullptr);
    EXPECT_TRUE(ldw->driver_override.reported);
    EXPECT_FALSE(ldw->driver_override.active);
}

TEST(ManualAdasLateralReport, ClosedGateLeavesTheOverrideChannelUnwritten)
{
    // "A function that was never running cannot have been overridden" -- the
    // same rule as the longitudinal rows, and what lets the negative direction
    // of driver_override_reported SKIP rather than pass vacuously.
    ManualAdasDecision decision;
    decision.lka_driver_override_steering = true;

    const auto  rows = BuildManualAdasFunctionReport(LateralFlags(),
                                                    /*owns_longitudinal_domain=*/true,
                                                    /*owns_lateral_domain=*/false,
                                                    decision,
                                                    {});
    const auto* lka  = Find(rows, "gt.lka");
    ASSERT_NE(lka, nullptr);
    EXPECT_FALSE(lka->driver_override.reported);
    EXPECT_FALSE(lka->driver_override.active);
}

TEST(ManualAdasLateralReport, LkaDetailRoutesToTheLkaRowByPrefix)
{
    ManualAdasDecision decision;
    PolicyDetail       detail;
    AddDetail(detail, "gt.lka.offset_m", 0.42);
    AddDetail(detail, "gt.lka.correction", 0.03);
    AddDetail(detail, "gt.aeb.ttc_s", 1.0);  // must NOT land on a lateral row

    const auto  rows = BuildManualAdasFunctionReport(LateralFlags(),
                                                    /*owns_longitudinal_domain=*/false,
                                                    /*owns_lateral_domain=*/true,
                                                    decision,
                                                    detail);
    const auto* lka  = Find(rows, "gt.lka");
    ASSERT_NE(lka, nullptr);
    EXPECT_EQ(lka->detail.size(), 2u);
    for (const auto& kv : lka->detail) EXPECT_EQ(kv.first.rfind("gt.lka.", 0), 0u);
}

// ===========================================================================
// The coordinator invariants
// ===========================================================================

TEST(ManualAdasLateralWiring, LkaCorrectionAndFfbPeerRoutingAreMutuallyExclusive)
{
    // ManualDriveCoordinator::RunFrame step 6a routes the FFB servo target from
    // the LATERAL OWNER, and its branch condition is literally
    // `!ledger.IsOwner(obj_id, &c, OwnedDomain::LATERAL)` -- restated here as
    // `!owns_lateral`. The LKA correction reaches cmd.steering exactly when
    // ManualLkaArbitrates() is true. If both could hold on one frame, the same
    // intervention would be applied twice: once into the physics through the
    // command, and again as a force pushing the physical wheel.
    for (bool owns_lateral : {false, true})
    {
        for (bool enabled : {false, true})
        {
            for (bool warning_only : {false, true})
            {
                const bool lka_writes_command = ManualLkaArbitrates(owns_lateral, enabled, warning_only);
                const bool ffb_routes_from_peer = !owns_lateral;  // the coordinator's own condition
                EXPECT_FALSE(lka_writes_command && ffb_routes_from_peer)
                    << "owns_lateral=" << owns_lateral << " enabled=" << enabled
                    << " warning_only=" << warning_only;
            }
        }
    }
}

TEST(ManualAdasLateralWiring, ManualLkaArbitratesIsFalseInEveryNonCorrectingConfiguration)
{
    // The complement of the above, asserted directly so a future edit that made
    // ManualLkaArbitrates() unconditionally true would fail HERE (with an
    // obvious message) rather than only in the mutual-exclusion test.
    EXPECT_TRUE(ManualLkaArbitrates(true, true, false));
    EXPECT_FALSE(ManualLkaArbitrates(true, true, true)) << "warning_only never writes the command";
    EXPECT_FALSE(ManualLkaArbitrates(true, false, false));
    EXPECT_FALSE(ManualLkaArbitrates(false, true, false)) << "not owning lateral never writes the command";
}

// ===========================================================================
// The lateral section inside ComputeManualAdasFrame
// ===========================================================================

namespace
{

ManualAdasEnvironment DriftingEnv()
{
    ManualAdasEnvironment env;
    env.lane_valid           = true;
    env.lane_offset_m        = 0.60;
    env.lane_half_width_m    = 1.75;
    env.vehicle_half_width_m = 1.0;
    env.lateral_speed_mps    = 0.50;
    env.lane_id              = -1;
    return env;
}

bool HasDetailKey(const PolicyDetail& detail, const char* key)
{
    for (const auto& kv : detail)
        if (kv.first == key) return true;
    return false;
}

}  // namespace

TEST(ManualAdasLateralSection, RunsEvenWhenTheLongitudinalDomainIsBypassed)
{
    // The reason the longitudinal bypass became a SECTION skip rather than an
    // early return. Under lat=manual / lon=VD, an early return would have
    // silenced LKA on every frame -- the exact reverse of what design §2-3 asks
    // for.
    ManualAdasStackConfig cfg;
    cfg.aeb_enabled  = false;
    cfg.acc.enabled  = false;
    cfg.msl.enabled  = false;
    cfg.lka.enabled  = true;

    KickdownDetector  kickdown{cfg.kickdown};
    PedalArbitrator   arbitrator{cfg.arbitrator};
    AccLonController  acc{cfg.acc};
    ManualAdasRuntime runtime;

    PedalSteerCommand driver_cmd;
    driver_cmd.throttle = 0.30;

    ManualAdasFrameResult result;
    for (int i = 0; i < 20; ++i)
    {
        result = ComputeManualAdasFrame(cfg,
                                        /*owns_longitudinal=*/false,
                                        /*owns_lateral=*/true,
                                        /*intervention=*/{},
                                        /*warning=*/{},
                                        /*acc_policy=*/{},
                                        DriftingEnv(),
                                        driver_cmd,
                                        /*ego_speed_mps=*/25.0,
                                        /*measured_decel_mps2=*/0.0,
                                        kDt,
                                        kickdown,
                                        arbitrator,
                                        acc,
                                        runtime);
    }

    EXPECT_TRUE(result.lka.correcting);
    EXPECT_TRUE(result.decision.lka_correcting);
    // The pedals still passed through untouched -- the two sections really are
    // independent, not merely both reachable.
    EXPECT_EQ(result.pedals.throttle_out, driver_cmd.throttle);
    EXPECT_TRUE(HasDetailKey(result.detail, "gt.lka.offset_m"));
    EXPECT_TRUE(HasDetailKey(result.detail, "gt.lka.lane_id"));
}

TEST(ManualAdasLateralSection, EmitsNoLkaDetailOnAFrameTheJudgementDidNotRun)
{
    // Absent, not zeroed -- the same discipline the longitudinal bypass follows.
    // A reported gt.lka.offset_m of 0.000 on a frame nobody measured is
    // indistinguishable from a perfectly centred vehicle, and the E2E matchers
    // have to be able to tell "did not look" from "looked, found nothing".
    ManualAdasStackConfig cfg;
    cfg.lka.enabled = true;

    KickdownDetector  kickdown{cfg.kickdown};
    PedalArbitrator   arbitrator{cfg.arbitrator};
    AccLonController  acc{cfg.acc};
    ManualAdasRuntime runtime;

    const auto result = ComputeManualAdasFrame(cfg,
                                               /*owns_longitudinal=*/true,
                                               /*owns_lateral=*/false,  // <- gated
                                               {},
                                               {},
                                               {},
                                               DriftingEnv(),
                                               PedalSteerCommand{},
                                               25.0,
                                               0.0,
                                               kDt,
                                               kickdown,
                                               arbitrator,
                                               acc,
                                               runtime);

    EXPECT_FALSE(result.lka.evaluated);
    EXPECT_FALSE(HasDetailKey(result.detail, "gt.lka.offset_m"));
    EXPECT_FALSE(HasDetailKey(result.detail, "gt.lka.correction"));
}
