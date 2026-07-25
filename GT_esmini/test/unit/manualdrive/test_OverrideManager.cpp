// feature:F7 unit tests for OverrideManager (auto<->manual bidirectional latch).
//
// Locks the existing AUTO->MANUAL latch behavior, then covers the new
// AUTO_RESUME button-edge path (manual -> auto) and same-frame reentry
// suppression that returns the mode as soon as the input drops.

#include <gtest/gtest.h>

#include "gt_esmini/control/manualdrive/OverrideManager.hpp"
#include "gt_esmini/control/manualdrive/ManualDriveConfig.hpp"
#include "gt_esmini/control/manualdrive/ManualDriveTypes.hpp"
#include "gt_esmini/control/common/VehicleCommand.hpp"

namespace gt_esmini
{
namespace
{

ManualDriveConfig MakeConfig(bool enabled = true,
                             const std::string& lat = "manual",
                             const std::string& lon = "manual")
{
    ManualDriveConfig cfg;
    cfg.override_cfg.enabled             = enabled;
    cfg.override_cfg.button_override     = true;
    cfg.override_cfg.steering_threshold  = 0.05;
    cfg.override_cfg.throttle_threshold  = 0.10;
    cfg.override_cfg.brake_threshold     = 0.10;
    cfg.override_cfg.auto_return_timeout = 0.0;
    cfg.domain.lateral                   = lat;
    cfg.domain.longitudinal              = lon;
    return cfg;
}

InputFrame MakeFrame(double steering = 0.0,
                     double throttle = 0.0,
                     double brake    = 0.0,
                     uint32_t buttons = 0)
{
    InputFrame f;
    PedalSteerCommand ps;
    ps.steering = steering;
    ps.throttle = throttle;
    ps.brake    = brake;
    ps.buttons  = buttons;
    f.pedal_steer = ps;
    f.connected   = true;
    return f;
}

}  // namespace

// --- Existing behavior: AUTO->MANUAL latch on threshold cross ---------------

TEST(OverrideManagerTest, DefaultsToAuto)
{
    OverrideManager m;
    m.Configure(MakeConfig());
    EXPECT_FALSE(m.IsAnyManual());
    EXPECT_FALSE(m.IsLateralManual());
    EXPECT_FALSE(m.IsLongitudinalManual());
    EXPECT_FALSE(m.JustTransitionedToManual());
}

TEST(OverrideManagerTest, SteeringOverThresholdLatchesLateralManual)
{
    OverrideManager m;
    m.Configure(MakeConfig());

    // Above threshold -> latch to MANUAL, transition edge fires.
    m.Update(MakeFrame(0.20), 0.02);
    EXPECT_TRUE(m.IsLateralManual());
    EXPECT_FALSE(m.IsLongitudinalManual());
    EXPECT_TRUE(m.JustTransitionedToManual());

    // Release the wheel — latch holds, edge does not re-fire.
    m.Update(MakeFrame(0.0), 0.02);
    EXPECT_TRUE(m.IsLateralManual());
    EXPECT_FALSE(m.JustTransitionedToManual());
}

TEST(OverrideManagerTest, PedalOverThresholdLatchesLongitudinalOnly)
{
    OverrideManager m;
    m.Configure(MakeConfig());

    m.Update(MakeFrame(0.0, 0.5, 0.0), 0.02);
    EXPECT_FALSE(m.IsLateralManual());
    EXPECT_TRUE(m.IsLongitudinalManual());

    m.Update(MakeFrame(0.0, 0.0, 0.5), 0.02);
    EXPECT_TRUE(m.IsLongitudinalManual());
}

TEST(OverrideManagerTest, OverrideButtonLatchesBothDomains)
{
    OverrideManager m;
    m.Configure(MakeConfig());

    m.Update(MakeFrame(0.0, 0.0, 0.0, ButtonBits::OVERRIDE), 0.02);
    EXPECT_TRUE(m.IsLateralManual());
    EXPECT_TRUE(m.IsLongitudinalManual());
}

TEST(OverrideManagerTest, ScenarioDomainStaysAuto)
{
    OverrideManager m;
    m.Configure(MakeConfig(true, "scenario", "manual"));

    // Steering way above threshold — scenario-locked lateral must ignore it.
    m.Update(MakeFrame(0.9, 0.5, 0.0), 0.02);
    EXPECT_FALSE(m.IsLateralManual());
    EXPECT_TRUE(m.IsLongitudinalManual());
}

TEST(OverrideManagerTest, DisabledOverrideForcesScenarioAuto)
{
    OverrideManager m;
    m.Configure(MakeConfig(false, "manual", "scenario"));

    // Override disabled: configured-manual domain sticks to MANUAL, scenario stays AUTO.
    m.Update(MakeFrame(0.9, 0.5, 0.0), 0.02);
    EXPECT_TRUE(m.IsLateralManual());
    EXPECT_FALSE(m.IsLongitudinalManual());
    EXPECT_FALSE(m.JustTransitionedToManual());
    EXPECT_FALSE(m.JustTransitionedToAuto());
}

// --- New feature:F7 — RESUME (manual -> auto) edge path ---------------------

TEST(OverrideManagerTest, ResumeButtonEdgeReturnsToAutoBothDomains)
{
    OverrideManager m;
    m.Configure(MakeConfig());

    // Latch to MANUAL via steering.
    m.Update(MakeFrame(0.30), 0.02);
    ASSERT_TRUE(m.IsLateralManual());

    // Release wheel, still MANUAL (latched).
    m.Update(MakeFrame(0.0), 0.02);
    ASSERT_TRUE(m.IsLateralManual());
    EXPECT_FALSE(m.JustTransitionedToAuto());

    // Press RESUME — rising edge returns both domains to AUTO, edge fires once.
    m.Update(MakeFrame(0.0, 0.0, 0.0, ButtonBits::AUTO_RESUME), 0.02);
    EXPECT_FALSE(m.IsAnyManual());
    EXPECT_TRUE(m.JustTransitionedToAuto());

    // Hold RESUME — no re-fire of the transition edge.
    m.Update(MakeFrame(0.0, 0.0, 0.0, ButtonBits::AUTO_RESUME), 0.02);
    EXPECT_FALSE(m.IsAnyManual());
    EXPECT_FALSE(m.JustTransitionedToAuto());
}

TEST(OverrideManagerTest, ResumeSuppressesSameFrameReintervention)
{
    OverrideManager m;
    m.Configure(MakeConfig());

    // Latch MANUAL.
    m.Update(MakeFrame(0.30), 0.02);
    ASSERT_TRUE(m.IsLateralManual());

    // Same frame: RESUME pressed AND steering still above threshold.
    // The RESUME edge must win: this frame must land in AUTO, not immediately
    // re-latch to MANUAL. This is the guarantee the smoke script relies on.
    m.Update(MakeFrame(0.30, 0.0, 0.0, ButtonBits::AUTO_RESUME), 0.02);
    EXPECT_FALSE(m.IsAnyManual());
    EXPECT_TRUE(m.JustTransitionedToAuto());

    // Next frame: RESUME released, input still above threshold — allowed to
    // re-latch (this is the "you're still holding the wheel" real-world case).
    m.Update(MakeFrame(0.30), 0.02);
    EXPECT_TRUE(m.IsLateralManual());
    EXPECT_TRUE(m.JustTransitionedToManual());
}

TEST(OverrideManagerTest, ResumeIsNoOpWhenAlreadyAuto)
{
    OverrideManager m;
    m.Configure(MakeConfig());

    // AUTO from the start; pressing RESUME should not fire the transition edge.
    m.Update(MakeFrame(0.0, 0.0, 0.0, ButtonBits::AUTO_RESUME), 0.02);
    EXPECT_FALSE(m.IsAnyManual());
    EXPECT_FALSE(m.JustTransitionedToAuto());
}

TEST(OverrideManagerTest, ResumeRequiresRisingEdge)
{
    OverrideManager m;
    m.Configure(MakeConfig());

    // Latch MANUAL.
    m.Update(MakeFrame(0.30), 0.02);
    ASSERT_TRUE(m.IsLateralManual());

    // If RESUME is already held from before the latch (no rising edge visible to
    // the manager since the previous frame had no button), we still consider
    // this a fresh edge. Model the *held* case: prime with RESUME held while in
    // AUTO, then latch to MANUAL, then keep holding — no rising edge -> no return.
    m.Configure(MakeConfig());
    m.Update(MakeFrame(0.0, 0.0, 0.0, ButtonBits::AUTO_RESUME), 0.02);   // holding, still AUTO
    m.Update(MakeFrame(0.30, 0.0, 0.0, ButtonBits::AUTO_RESUME), 0.02);  // manual input arrives while RESUME still held
    EXPECT_TRUE(m.IsLateralManual());
    EXPECT_FALSE(m.JustTransitionedToAuto());

    // Only when RESUME is released and pressed again does it fire.
    m.Update(MakeFrame(0.30, 0.0, 0.0, 0u), 0.02);  // release RESUME
    m.Update(MakeFrame(0.0, 0.0, 0.0, ButtonBits::AUTO_RESUME), 0.02);  // fresh press
    EXPECT_FALSE(m.IsAnyManual());
    EXPECT_TRUE(m.JustTransitionedToAuto());
}

// --- feature:F7 (F7b) — FFB torque-proxy intervention path -----------------
//
// The FFB target-tracking servo (SDLFFBSink target_track) exposes a "how hard
// is the driver pushing against the servo?" sample every frame. This block
// wires that sample into OverrideManager so a physical steering push against
// AD becomes a latch to MANUAL, subject to a debounce (spike §2d release-
// transient concerns) and the existing scenario / RESUME rules.
//
// Config lives under ffb.target_track (spike §3c) and only takes effect when
// UpdateFfbSample() is called with active=true (the servo is running).

namespace
{

ManualDriveConfig MakeConfigWithFfbThresholds(double force_thr = 0.20,
                                              double dev_thr   = 0.04,
                                              double sustain_s = 0.10,
                                              double target_rate_gate = 0.30,
                                              double derror_rate_gate = 0.10)
{
    ManualDriveConfig cfg = MakeConfig();
    cfg.ffb.target_track.override_steer_force_threshold      = force_thr;
    cfg.ffb.target_track.override_steer_dev_threshold        = dev_thr;
    cfg.ffb.target_track.override_sustain_time               = sustain_s;
    cfg.ffb.target_track.override_target_rate_gate           = target_rate_gate;
    cfg.ffb.target_track.override_position_error_rate_gate   = derror_rate_gate;
    return cfg;
}

// Fresh AUTO-mode frame with no manual input.
InputFrame QuietFrame() { return MakeFrame(0.0, 0.0, 0.0, 0u); }

}  // namespace

TEST(OverrideManagerTest, FfbSampleInactiveNeverLatches)
{
    OverrideManager m;
    m.Configure(MakeConfigWithFfbThresholds());

    FfbInterventionSample sample;
    sample.active           = false;   // servo off — sample must be ignored
    sample.commanded_force  = 1.0;     // way over threshold
    sample.position_error   = 1.0;     // way over threshold

    for (int i = 0; i < 100; ++i)  // 2 s at 50 Hz — plenty over any sustain
    {
        m.UpdateFfbSample(sample);
        m.Update(QuietFrame(), 0.02);
    }
    EXPECT_FALSE(m.IsLateralManual());
    EXPECT_FALSE(m.JustTransitionedToManual());
}

TEST(OverrideManagerTest, FfbSampleBelowThresholdsNeverLatches)
{
    OverrideManager m;
    m.Configure(MakeConfigWithFfbThresholds(/*force*/0.20, /*dev*/0.04));

    FfbInterventionSample sample;
    sample.active          = true;
    sample.commanded_force = 0.15;   // below 0.20
    sample.position_error  = 0.03;   // below 0.04

    for (int i = 0; i < 100; ++i)
    {
        m.UpdateFfbSample(sample);
        m.Update(QuietFrame(), 0.02);
    }
    EXPECT_FALSE(m.IsLateralManual());
}

TEST(OverrideManagerTest, FfbSampleShortBurstDoesNotLatch)
{
    // Over threshold for less than sustain time — must NOT latch.
    // Post-f723fa90: use a physically-realistic sample where the actual wheel
    // is deflected (satisfies the min_actual_wheel_deflection gate), so this
    // test is exercising the sustain timer specifically, not the wheel gate.
    OverrideManager m;
    m.Configure(MakeConfigWithFfbThresholds(0.20, 0.04, /*sustain=*/0.10));

    FfbInterventionSample s;
    s.active          = true;
    s.commanded_force = 0.50;   // above force threshold
    s.position_error  = 0.10;   // above dev threshold
    s.target_norm     = 0.00;   // actual = 0 - 0.10 = -0.10 → wheel engaged

    // 4 frames × 20 ms = 80 ms < 100 ms sustain
    for (int i = 0; i < 4; ++i)
    {
        m.UpdateFfbSample(s);
        m.Update(QuietFrame(), 0.02);
    }
    EXPECT_FALSE(m.IsLateralManual());
}

TEST(OverrideManagerTest, FfbSampleSustainedForceLatchesLateral)
{
    // Real driver holding the wheel at -0.10 (actual_norm) while AD wants +0.05
    // (target_norm) → position_error = +0.05 - (-0.10) = +0.15 → PID force
    // = Kp*|err| = 0.60 (saturated). Both thresholds crossed, wheel engaged.
    OverrideManager m;
    m.Configure(MakeConfigWithFfbThresholds(0.20, 0.04, 0.10));

    FfbInterventionSample s;
    s.active          = true;
    s.commanded_force = 0.60;
    s.position_error  = 0.15;
    s.target_norm     = 0.05;   // actual = 0.05 - 0.15 = -0.10 → wheel engaged

    // 6 × 20 ms = 120 ms > 100 ms sustain
    for (int i = 0; i < 6; ++i)
    {
        m.UpdateFfbSample(s);
        m.Update(QuietFrame(), 0.02);
    }
    EXPECT_TRUE(m.IsLateralManual());
    EXPECT_FALSE(m.IsLongitudinalManual());  // lateral only — pedal path untouched
}

TEST(OverrideManagerTest, FfbSampleSustainedDevAloneLatches)
{
    // dev channel alone (spike §2e fallback) must also fire.
    OverrideManager m;
    m.Configure(MakeConfigWithFfbThresholds(0.20, 0.04, 0.10));

    FfbInterventionSample s;
    s.active = true;
    s.commanded_force = 0.10;   // below force threshold
    s.position_error  = 0.10;   // above dev threshold

    for (int i = 0; i < 6; ++i)
    {
        m.UpdateFfbSample(s);
        m.Update(QuietFrame(), 0.02);
    }
    EXPECT_TRUE(m.IsLateralManual());
}

TEST(OverrideManagerTest, FfbSustainResetsWhenSignalDrops)
{
    // Signal above threshold, then drops (below threshold), then above again.
    // The sustain timer must reset on the drop; otherwise a series of short
    // pushes would eventually accumulate into a spurious latch.
    // Post-f723fa90: use physically-realistic samples where actual_norm is
    // deflected past the min gate (so the sustain-reset behavior — not the
    // wheel gate — is what's under test).
    OverrideManager m;
    m.Configure(MakeConfigWithFfbThresholds(0.20, 0.04, 0.10));

    FfbInterventionSample hi;
    hi.active          = true;
    hi.commanded_force = 0.50;
    hi.position_error  = 0.10;
    hi.target_norm     = 0.00;   // actual = -0.10 → wheel engaged
    FfbInterventionSample lo;
    lo.active          = true;
    lo.commanded_force = 0.05;
    lo.position_error  = 0.00;   // signal off; also wheel returns to rest
    lo.target_norm     = 0.00;

    // 3 × 20 ms hi = 60 ms
    for (int i = 0; i < 3; ++i) { m.UpdateFfbSample(hi); m.Update(QuietFrame(), 0.02); }
    // 3 × 20 ms lo — sustain resets
    for (int i = 0; i < 3; ++i) { m.UpdateFfbSample(lo); m.Update(QuietFrame(), 0.02); }
    // 4 × 20 ms hi = 80 ms — still under 100 ms
    for (int i = 0; i < 4; ++i) { m.UpdateFfbSample(hi); m.Update(QuietFrame(), 0.02); }

    EXPECT_FALSE(m.IsLateralManual());
}

TEST(OverrideManagerTest, FfbScenarioLateralIsImmuneToFfbIntervention)
{
    // domain.lateral="scenario" locks lateral to AUTO. FFB thresholds crossed
    // sustainably must NOT latch it to MANUAL.
    OverrideManager m;
    ManualDriveConfig cfg = MakeConfigWithFfbThresholds();
    cfg.domain.lateral = "scenario";
    m.Configure(cfg);

    FfbInterventionSample s;
    s.active = true;
    s.commanded_force = 1.0;
    s.position_error  = 1.0;

    for (int i = 0; i < 50; ++i)  // 1 s well past sustain
    {
        m.UpdateFfbSample(s);
        m.Update(QuietFrame(), 0.02);
    }
    EXPECT_FALSE(m.IsLateralManual());
}

TEST(OverrideManagerTest, FfbLatchHoldsAcrossReleaseTransient)
{
    // Spike §2d: after a firm hold, the PID overshoots for ~400 ms releasing.
    // Once latched, the existing OverrideManager latch model must keep MANUAL
    // regardless of what the FFB sample does — clearing goes through RESUME
    // (below), never through "sample dropped below threshold".
    // Post-f723fa90: hi sample now includes a non-zero position_error so
    // actual_norm = target - dev is deflected past the min-deflection gate.
    OverrideManager m;
    m.Configure(MakeConfigWithFfbThresholds(0.20, 0.04, 0.10));

    FfbInterventionSample hi;
    hi.active          = true;
    hi.commanded_force = 0.50;
    hi.position_error  = 0.10;
    hi.target_norm     = 0.00;   // actual = -0.10 → wheel engaged
    for (int i = 0; i < 6; ++i) { m.UpdateFfbSample(hi); m.Update(QuietFrame(), 0.02); }
    ASSERT_TRUE(m.IsLateralManual());

    // Simulate release: signal goes below threshold. Latch stays.
    FfbInterventionSample lo;
    lo.active          = true;
    lo.commanded_force = 0.03;
    lo.position_error  = 0.00;
    lo.target_norm     = 0.00;
    for (int i = 0; i < 50; ++i) { m.UpdateFfbSample(lo); m.Update(QuietFrame(), 0.02); }
    EXPECT_TRUE(m.IsLateralManual());

    // Simulate the release overshoot: signal spikes back above threshold.
    // Latch still stays (already MANUAL — nothing to change).
    FfbInterventionSample spike;
    spike.active          = true;
    spike.commanded_force = 0.55;
    spike.position_error  = 0.15;
    spike.target_norm     = 0.00;   // actual = -0.15
    for (int i = 0; i < 30; ++i) { m.UpdateFfbSample(spike); m.Update(QuietFrame(), 0.02); }
    EXPECT_TRUE(m.IsLateralManual());
}

TEST(OverrideManagerTest, FfbResumeEdgeWinsOverSustainedFfbSameFrame)
{
    // Sustained FFB (would latch on its own) + a RESUME rising edge on the
    // same frame: RESUME wins. The frame after RESUME releases, FFB is still
    // sustained (state carried over) — that frame IS allowed to re-latch.
    OverrideManager m;
    m.Configure(MakeConfigWithFfbThresholds(0.20, 0.04, 0.10));

    FfbInterventionSample hi;
    hi.active          = true;
    hi.commanded_force = 0.50;
    hi.position_error  = 0.10;
    hi.target_norm     = 0.00;   // actual = -0.10 → wheel engaged

    // Latch MANUAL via sustained FFB.
    for (int i = 0; i < 6; ++i) { m.UpdateFfbSample(hi); m.Update(QuietFrame(), 0.02); }
    ASSERT_TRUE(m.IsLateralManual());

    // Same frame: RESUME pressed AND FFB still sustained.
    m.UpdateFfbSample(hi);
    m.Update(MakeFrame(0.0, 0.0, 0.0, ButtonBits::AUTO_RESUME), 0.02);
    EXPECT_FALSE(m.IsAnyManual());               // RESUME edge wins
    EXPECT_TRUE(m.JustTransitionedToAuto());
}

// --- feature:F7 (F7b) — closed-loop feedback protection ---------------------
//
// Bug this block guards against (first surfaced on G29 real-machine test after
// the initial F7b commit, 1c2939a0):
//
//   1. AD commands non-zero steering; SetSteerTarget → servo pushes physical wheel.
//   2. Next frame SDL_JoystickUpdate reads the servo-moved axis position.
//   3. SDL2WheelInput::Poll returns pedal_steer.steering = <servo-moved value>.
//   4. OverrideManager::Update sees |steering| > steering_threshold (0.05).
//   5. lat_active=true → lat_mode=MANUAL → on frame 3 target_active_ goes false.
//   6. Servo dies. Wheel returns to center. User sees "no active following"
//      and "override never latches" (because it already silently latched to
//      MANUAL from the servo's own motion — the observed override transition
//      would already have fired on frame 2 without any driver push).
//
// The FIX: while the servo is active (ffb_sample_.active), the direct axis
// threshold on pedal_steer.steering must be SUPPRESSED — the torque proxy
// (position_error / commanded_force) is the correct intervention detector
// because it distinguishes "servo where AD wants" (position_error≈0, no push)
// from "driver fighting the servo" (position_error grows, force grows).
//
// The tests below are the ones the original F7b unit suite MISSED: they
// exercise the closed loop that the pedal_steer path implicitly closes.

TEST(OverrideManagerTest, FfbActiveSuppressesDirectSteeringThreshold)
{
    // Servo actively pushing wheel to non-zero position; driver is NOT pushing
    // back (torque-proxy sample is quiet). The wheel's axis is above the
    // steering_threshold ONLY because the servo drove it there. Direct-axis
    // path must be suppressed — otherwise MANUAL latches on frame 1.
    OverrideManager m;
    m.Configure(MakeConfigWithFfbThresholds(0.20, 0.04, 0.10));

    FfbInterventionSample s;
    s.active          = true;
    s.commanded_force = 0.10;   // BELOW force threshold — servo tracking OK
    s.position_error  = 0.02;   // BELOW dev threshold — driver not pushing

    // 100 frames = 2 s at 50 Hz, plenty over any sustain window.
    // pedal_steer.steering = 0.5 (well above the default 0.05 threshold) —
    // this is what the wheel reads back AFTER the servo moved it there.
    for (int i = 0; i < 100; ++i)
    {
        m.UpdateFfbSample(s);
        m.Update(MakeFrame(/*steering=*/0.5), 0.02);
    }
    EXPECT_FALSE(m.IsLateralManual());       // servo did NOT self-trip override
    EXPECT_FALSE(m.JustTransitionedToManual());
}

TEST(OverrideManagerTest, FfbInactiveKeepsDirectSteeringThreshold)
{
    // Regression guard: when the servo is OFF (target_track disabled, or
    // scenario without an FFB-capable input source), the pre-F7b direct-axis
    // behavior MUST be preserved. |pedal_steer.steering| > threshold latches
    // MANUAL. Otherwise a plain-old wheel push under ManualDrive stops working.
    OverrideManager m;
    m.Configure(MakeConfigWithFfbThresholds(0.20, 0.04, 0.10));

    FfbInterventionSample s;
    s.active = false;   // servo not running

    m.UpdateFfbSample(s);
    m.Update(MakeFrame(/*steering=*/0.10), 0.02);   // 0.10 > 0.05 threshold
    EXPECT_TRUE(m.IsLateralManual());               // pre-F7b behavior preserved
    EXPECT_TRUE(m.JustTransitionedToManual());
}

TEST(OverrideManagerTest, FfbClosedLoopServoDoesNotSelfTripOverride)
{
    // End-to-end simulation of the G29 real-machine bug: servo drives wheel
    // through varying positions (as it would while following AD steering
    // through a curve). Each frame the axis reads back the servo-driven value.
    // Without the fix, on the very first frame axis passes threshold the
    // manager latches MANUAL and the servo dies.
    OverrideManager m;
    m.Configure(MakeConfigWithFfbThresholds(0.20, 0.04, 0.10));

    FfbInterventionSample s;
    s.active          = true;
    s.commanded_force = 0.25;   // ABOVE force threshold on purpose — servo IS
                                // producing measurable force to move the wheel
    s.position_error  = 0.02;   // but position_error stays SMALL — the servo
                                // is tracking well, driver is NOT pushing.
                                // The torque-proxy path uses force OR dev, so
                                // to stay under the sustained-latch condition,
                                // force must ALSO be under threshold. Set it
                                // under to isolate the "closed-loop" bug — the
                                // separate "driver-push does latch" case is in
                                // FfbActiveWithHighPositionErrorLatchesViaTorqueProxy.
    s.commanded_force = 0.10;

    for (int i = 0; i < 500; ++i)   // 10 s
    {
        // Simulate: physical wheel oscillates ±0.6 as servo tracks a
        // varying target. Any of these values above the 0.05 direct threshold
        // WOULD trip MANUAL without the fix.
        const double axis = 0.6 * std::sin(static_cast<double>(i) * 0.1);
        m.UpdateFfbSample(s);
        m.Update(MakeFrame(axis), 0.02);
    }
    EXPECT_FALSE(m.IsLateralManual());
}

TEST(OverrideManagerTest, FfbActiveWithHighPositionErrorLatchesViaTorqueProxy)
{
    // The FIX must NOT break the real intervention case: driver pushing back
    // against the servo grows position_error (and commanded_force via the
    // PID), the torque-proxy path latches after sustain. Raw axis is above
    // threshold too, but that path is suppressed; torque-proxy is the one
    // that fires.
    OverrideManager m;
    m.Configure(MakeConfigWithFfbThresholds(0.20, 0.04, 0.10));

    FfbInterventionSample s;
    s.active          = true;
    s.commanded_force = 0.50;   // ABOVE force threshold
    s.position_error  = 0.10;   // ABOVE dev threshold
    // Target is STATIC (rate-gate below not tripped) so torque-proxy fires.

    for (int i = 0; i < 6; ++i)   // 120 ms > 100 ms sustain
    {
        m.UpdateFfbSample(s);
        m.Update(MakeFrame(/*steering=*/0.5), 0.02);
    }
    EXPECT_TRUE(m.IsLateralManual());
    // Torque-proxy path fired, not direct-axis path — either way the outcome
    // for the driver is a MANUAL latch when they actually push back.
}

// --- feature:F7 (F7b) — moving-target rate gate ----------------------------
//
// Second-order regression (found on real G29 after commit a43e4c67 shipped):
//   The Day-1 spike (scripts/ffb_spike/05_torque_proxy.py) calibrated the
//   |u|>0.20 / |dev|>0.04 / 100 ms-sustain torque-proxy thresholds against a
//   STATIC target (target=0). In real driving the AD target moves whenever
//   the ego needs to steer (curve, lane change), and the PID servo's normal
//   tracking lag creates non-zero position_error and non-zero commanded_force
//   even without any driver touch. The unguarded threshold check therefore
//   fires spuriously on every AD steering transient — MANUAL latches without
//   the driver moving a finger.
//
//   Fix: rate-gate on |d(target)/dt|. When the AD target is actively moving
//   above override_target_rate_gate (default 0.30 axis-frac/s), suppress the
//   torque-proxy detection AND reset the sustain accumulator. Detection re-
//   arms when the target settles. This lets the servo track transients
//   without the manager mistaking normal PID lag for intervention.

TEST(OverrideManagerTest, FfbMovingTargetSuppressesFalsePositive)
{
    // Simulates the false-positive real-machine scenario: AD is steering
    // through a curve (target ramps up), PID lags → position_error and
    // commanded_force stay above the raw thresholds throughout the transient.
    // Without the rate-gate the manager would latch MANUAL within 100 ms.
    // With the rate-gate: moving_target=true, sustain never accumulates.
    OverrideManager m;
    ManualDriveConfig cfg = MakeConfigWithFfbThresholds(0.20, 0.04, 0.10);
    cfg.ffb.target_track.override_target_rate_gate = 0.30;  // axis-frac / s
    m.Configure(cfg);

    // Target ramps 0 → 0.5 over 2 s = rate 0.25 axis-frac/s, then held.
    // 0.25 < 0.30 gate? Let's use a rate of 0.5/s to be firmly ABOVE gate.
    // (target 0 → 1.0 over 2 s @ 50 Hz = 100 frames)
    FfbInterventionSample s;
    s.active          = true;
    s.commanded_force = 0.50;    // ABOVE force threshold on every frame
    s.position_error  = 0.20;    // ABOVE dev threshold on every frame

    for (int i = 0; i < 100; ++i)   // 2 s ramp
    {
        // 0 → 1.0 linear ramp = rate 0.5 /s (well above 0.30 gate).
        s.target_norm = 0.01 * static_cast<double>(i);
        m.UpdateFfbSample(s);
        m.Update(QuietFrame(), 0.02);
    }
    // Even though force AND dev are always above thresholds, the moving
    // target rate-gates the accumulator and MANUAL never latches.
    EXPECT_FALSE(m.IsLateralManual());
    EXPECT_FALSE(m.JustTransitionedToManual());
}

TEST(OverrideManagerTest, FfbStableTargetAfterMotionAllowsLatch)
{
    // Realistic sequence: target moves (transient — must not latch), then
    // settles (target rate ≈ 0). If the driver is still blocking the wheel
    // at a position past target (magnitude opposition) at that point,
    // torque-proxy should latch shortly after target settles.
    // Post-f723fa90 (wheel-over-target gate): the driver's held wheel must
    // be PAST target (|actual| > |target|+ε) — a wheel between 0 and target
    // is servo behavior. Update fixtures accordingly: dev = -0.10 so that
    // actual = target - dev = 1.0 - (-0.10) = 1.10 (past target 1.0 + ε 0.05).
    OverrideManager m;
    ManualDriveConfig cfg = MakeConfigWithFfbThresholds(0.20, 0.04, 0.10);
    cfg.ffb.target_track.override_target_rate_gate = 0.30;
    m.Configure(cfg);

    FfbInterventionSample s;
    s.active          = true;
    s.commanded_force = 0.50;
    s.position_error  = -0.10;   // driver's wheel is PAST target by 0.10

    // Phase 1: 2 s ramp (rate 0.5 /s → above gate → no latch)
    for (int i = 0; i < 100; ++i)
    {
        s.target_norm = 0.01 * static_cast<double>(i);
        m.UpdateFfbSample(s);
        m.Update(QuietFrame(), 0.02);
    }
    ASSERT_FALSE(m.IsLateralManual());

    // Phase 2: target held at 1.0 (rate = 0). Driver still holds wheel at
    // +1.10 (dev/force persist above thresholds; |actual|=1.10 > 1.05).
    // Sustain accumulates and latches.
    for (int i = 0; i < 6; ++i)   // 120 ms > 100 ms sustain
    {
        s.target_norm = 1.0;
        m.UpdateFfbSample(s);
        m.Update(QuietFrame(), 0.02);
    }
    EXPECT_TRUE(m.IsLateralManual());
    EXPECT_TRUE(m.JustTransitionedToManual());
}

TEST(OverrideManagerTest, FfbRateGateResetsSustainOnTargetJerk)
{
    // Target settles for a while (sustain accumulates), then abruptly moves
    // (rate spikes above gate) BEFORE sustain-latch triggers. The gate must
    // reset the accumulator so that the transient doesn't ride out its final
    // fraction and false-latch mid-motion.
    OverrideManager m;
    ManualDriveConfig cfg = MakeConfigWithFfbThresholds(0.20, 0.04, 0.10);
    cfg.ffb.target_track.override_target_rate_gate = 0.30;
    m.Configure(cfg);

    FfbInterventionSample s;
    s.active          = true;
    s.commanded_force = 0.50;
    s.position_error  = 0.20;

    // Phase 1: target static at 0 for 60 ms with STATIC dev (both gates
    // settled). Sustain accumulates.
    for (int i = 0; i < 3; ++i)
    {
        s.target_norm    = 0.0;
        s.position_error = 0.20;    // dev also static → derror rate = 0
        m.UpdateFfbSample(s);
        m.Update(QuietFrame(), 0.02);
    }
    // Phase 2: single frame with large target jump → target rate spike
    s.target_norm    = 0.5;
    s.position_error = 0.20;
    m.UpdateFfbSample(s);
    m.Update(QuietFrame(), 0.02);
    // Phase 3: 60 ms more at target=0.5 with STATIC dev (both gates settled
    // again). If rate-gate correctly reset in phase 2, sustain rebuilds from
    // 0 and needs another 100 ms to latch.
    for (int i = 0; i < 3; ++i)
    {
        s.target_norm    = 0.5;
        s.position_error = 0.20;
        m.UpdateFfbSample(s);
        m.Update(QuietFrame(), 0.02);
    }
    EXPECT_FALSE(m.IsLateralManual());
}

// --- feature:F7 (F7b) — bootstrap + derror-rate gate -----------------------
//
// Third-order regression (found on real G29 after commit 549e5823 shipped
// the target-rate-gate fix):
//   Straight-drive scenario startup FALSE-POSITIVE LATCH. Diagnosis:
//     (a) On the very first active FFB sample, OverrideManager had no
//         history to compute d(target)/dt → treated rate=0 as "settled" →
//         permitted threshold check on the bootstrap frame.
//     (b) At startup the physical wheel is at rest but AD may command
//         non-zero steering (driver-model warmup, small lane-keep
//         corrections). PID servo hasn't moved the wheel yet → dev is
//         large. Target itself is essentially static (small AD command,
//         changing slowly) → target-rate gate does NOT suppress. Sustain
//         accumulates → MANUAL latches in 100 ms with no driver touch.
//
//   Fix (two-part, this session's third commit):
//     (i)  Bootstrap suppression: on the FIRST active sample, both rate
//          derivatives are unknown. Suppress accumulation until we have
//          two consecutive active samples.
//     (ii) Add a SECOND rate gate on |d(position_error)/dt| — while the
//          servo is actively catching up to the target, dev is changing.
//          Only when BOTH target rate AND dev rate are settled does the
//          detector allow accumulation. "Real block" = target static AND
//          dev static (persistent, non-changing) AND thresholds crossed.

TEST(OverrideManagerTest, FfbBootstrapDoesNotFalseLatch)
{
    // First active sample carries no history. Even if dev/force are above
    // thresholds, sustain must NOT accumulate on the bootstrap frame.
    // Without this fix, a single frame with dev>threshold at startup would
    // start counting sustain against a spurious rate=0.
    OverrideManager m;
    m.Configure(MakeConfigWithFfbThresholds(0.20, 0.04, 0.10));

    FfbInterventionSample s;
    s.active          = true;
    s.commanded_force = 0.50;   // ABOVE force threshold
    s.position_error  = 0.20;   // ABOVE dev threshold
    s.target_norm     = 0.05;   // small AD steering, but static

    // Only ONE frame with active sample. Bootstrap = no history = suppress.
    m.UpdateFfbSample(s);
    m.Update(QuietFrame(), 0.02);
    EXPECT_FALSE(m.IsLateralManual());
}

TEST(OverrideManagerTest, FfbServoTransientDoesNotFalseLatch)
{
    // Real-hardware startup shape: physical wheel at rest (axis=0), AD
    // commands small non-zero target (0.10). Servo starts pushing wheel,
    // dev decays from 0.10 → 0.04 over ~10 frames (200 ms). During the
    // whole decay, |d(dev)/dt| is non-zero (~0.35 /s max at 7%/frame decay)
    // → derror-rate gate (default 0.10 /s) suppresses → no latch. This is
    // Bug 3 (commit 549e5823 follow-up).
    OverrideManager m;
    m.Configure(MakeConfigWithFfbThresholds(0.20, 0.04, 0.10, 0.30, 0.10));

    FfbInterventionSample s;
    s.active      = true;
    s.target_norm = 0.10;   // AD command, static

    // Simulate the servo catching up over 20 frames (400 ms). Each frame
    // dev decays by ~7%. Force decays proportionally with Kp.
    double dev = 0.10;
    for (int i = 0; i < 20; ++i)
    {
        s.position_error  = dev;
        s.commanded_force = std::abs(4.0 * dev);   // Kp=4 (unclamped)
        m.UpdateFfbSample(s);
        m.Update(QuietFrame(), 0.02);
        dev *= 0.93;   // 7% per-frame decay toward 0
    }
    EXPECT_FALSE(m.IsLateralManual());
}

TEST(OverrideManagerTest, FfbSteadyStateBlockAfterTransientLatches)
{
    // The fix must NOT break real blocks. After a servo transient settles
    // (dev stops changing) at a persistent above-threshold value, torque-
    // proxy should latch after sustain. Post-f723fa90 semantics: the
    // wheel must be PAST target for the latch — this represents "driver
    // aggressively over-steered during transient and now holds past target".
    OverrideManager m;
    m.Configure(MakeConfigWithFfbThresholds(0.20, 0.04, 0.10, 0.30, 0.10));

    FfbInterventionSample s;
    s.active      = true;
    s.target_norm = 0.30;

    // Phase 1: transient — dev goes -0.10 → -0.15 over 10 frames (driver
    // pushed wheel PAST target and it's oscillating a bit).
    double dev = -0.10;
    for (int i = 0; i < 10; ++i)
    {
        s.position_error  = dev;                   // actual = 0.30 - dev = 0.40 → 0.45
        s.commanded_force = std::abs(4.0 * dev);
        m.UpdateFfbSample(s);
        m.Update(QuietFrame(), 0.02);
        dev = std::max(-0.15, dev - 0.007);
    }
    ASSERT_FALSE(m.IsLateralManual());   // transient → no latch yet

    // Phase 2: dev SETTLED at -0.15 (driver holding wheel past target).
    //   actual = 0.30 - (-0.15) = +0.45 > |target| + ε = 0.35 → engaged.
    // Both rates ≈ 0. Sustain accumulates. Bootstrap-safe budget: 10 frames.
    bool saw_manual_edge = false;
    for (int i = 0; i < 10; ++i)
    {
        s.position_error  = -0.15;
        s.commanded_force = 0.60;   // saturated
        m.UpdateFfbSample(s);
        m.Update(QuietFrame(), 0.02);
        if (m.JustTransitionedToManual()) saw_manual_edge = true;
    }
    EXPECT_TRUE(m.IsLateralManual());
    EXPECT_TRUE(saw_manual_edge);
}

// --- feature:F7 (F7b) — wheel-over-target opposition gate -------------------
//
// Fourth-order regression (found on real G29 after commit f723fa90 shipped
// the bootstrap + derror-rate gate fix). Reproducer scenario:
// virtual_driver_basic.xosc (straight → LC → curve). Anticipation scenarios
// (curve / right_turn) also affected. User report: "レーンチェンジ出来てない
// / カーブも交差点も曲がれていない".
//
// Diagnosis (two regimes, same root):
//   Regime A — small AD target (e.g. LC ±0.057): PID output ≈ Kp×|target| =
//     ~0.23, below G29 breakaway friction (F ~0.3 in normalized units on
//     a fresh column). Wheel stays at 0. In this stuck-at-rest state:
//       * position_error ≈ target (constant)
//       * commanded_force ≈ Kp·|target| (constant, over_force threshold)
//       * derror_rate = 0 (dev constant → gate settled)
//       * target_rate = 0 (target stopped changing → gate settled)
//     The prior two rate gates green-light the check, force/dev cross,
//     sustain latches within 100 ms → target_active flips off → servo dies
//     → cmd.steering = 0 (raw wheel) → ego cannot LC / turn.
//
//   Regime B — sustained larger target (curve): servo commands more force
//     (approaching max_force=0.6), just enough to overcome friction and
//     move the wheel VERY slowly toward target (real-G29-measured: ~5 %/s).
//     Over seconds, |actual| grows from 0 → some fraction of target. dev
//     stays elevated (target - creeping actual). All rate gates still
//     settle to near-zero. |actual| gradually crosses ANY absolute
//     threshold. Same latch mechanism fires late in the maneuver, killing
//     the servo mid-turn — real-G29 curve traversal completes only ~70%
//     of the intended heading change (measured before the fix).
//
//     Fix: distinguish "servo doing its job" from "driver opposing" using
//         the servo's own physical direction. Under servo alone, the wheel
//         always moves TOWARD target (same sign as target, |actual| ≤
//         |target|). A driver override manifests as either sign opposition
//         (actual and target opposite direction) or magnitude opposition
//         (|actual| exceeds |target| + epsilon). Anything else — the wheel
//         obediently between 0 and target — is servo behavior, not driver
//         behavior, regardless of how long it persists.
//
//     Documented trade-off: a driver who holds the wheel firmly at
//     0 < |actual| < |target| while AD tries to steer harder is NOT
//     detected — the signal is identical to servo creep. Users can express
//     this override via the RESUME button or the config's button-override.
//     This is a defensible loss: the alternative (a simple absolute-
//     deflection gate) confuses servo creep with user-holding and false-
//     latches on all sustained-steer scenarios.

TEST(OverrideManagerTest, FfbSmallTargetWheelStuckAtRestDoesNotLatch)
{
    // Reproduces the real-machine LC / curve / right-turn Regime A:
    // small AD target (~ ±0.06), servo commands ~0.24 force, wheel stuck at
    // 0. Force above threshold and dev above threshold, both rates settled
    // — but the wheel is same-sign as target and |actual|=0 ≤ |target|+ε,
    // so the servo-direction gate blocks the latch.
    OverrideManager m;
    m.Configure(MakeConfigWithFfbThresholds(0.20, 0.04, 0.10, 0.30, 0.10));

    FfbInterventionSample s;
    s.active          = true;
    s.target_norm     = -0.057;   // LC peak (AD command)
    s.position_error  = -0.057;   // wheel at 0 → dev = target - 0
    s.commanded_force = 0.228;    // |Kp × err| unclamped

    // 2 s = 100 frames, way past sustain window. Latch must not fire.
    for (int i = 0; i < 100; ++i)
    {
        m.UpdateFfbSample(s);
        m.Update(QuietFrame(), 0.02);
    }
    EXPECT_FALSE(m.IsLateralManual());
    EXPECT_FALSE(m.JustTransitionedToManual());
}

TEST(OverrideManagerTest, FfbCurveScenarioSaturatedForceWheelStuckDoesNotLatch)
{
    // Regime A on a larger target: servo saturates at max_force=0.6, wheel
    // still stuck at 0. Same reasoning — wheel same-sign as target and not
    // past it, so the servo-direction gate blocks the latch.
    OverrideManager m;
    m.Configure(MakeConfigWithFfbThresholds(0.20, 0.04, 0.10, 0.30, 0.10));

    FfbInterventionSample s;
    s.active          = true;
    s.target_norm     = -0.30;    // curve peak
    s.position_error  = -0.30;    // wheel at 0
    s.commanded_force = 0.60;     // saturated

    for (int i = 0; i < 100; ++i)
    {
        m.UpdateFfbSample(s);
        m.Update(QuietFrame(), 0.02);
    }
    EXPECT_FALSE(m.IsLateralManual());
}

TEST(OverrideManagerTest, FfbCurveCreepingWheelBelowTargetDoesNotLatch)
{
    // Real-G29 Regime B (measured on decelerate_for_curve.xosc under
    // sdl2_wheel + target_track=true): sustained target ~-0.13, servo
    // commands ~0.45 force, wheel slowly creeps from 0 toward target.
    // Even 22 seconds in, |actual| is still only ~0.03 (below ~25 % of
    // target). Wheel is same-sign as target and not past target →
    // servo-direction gate keeps the latch quiet.
    OverrideManager m;
    m.Configure(MakeConfigWithFfbThresholds(0.20, 0.04, 0.10, 0.30, 0.10));

    FfbInterventionSample s;
    s.active          = true;
    s.target_norm     = -0.133;
    s.position_error  = -0.103;   // actual = -0.030 (creeping)
    s.commanded_force = 0.440;

    // 5 s straight after target stabilised. Would false-latch under the
    // simple |actual|≥0.03 draft of this fix; must not under the servo-
    // direction gate.
    for (int i = 0; i < 250; ++i)
    {
        m.UpdateFfbSample(s);
        m.Update(QuietFrame(), 0.02);
    }
    EXPECT_FALSE(m.IsLateralManual());
}

TEST(OverrideManagerTest, FfbRightTurnStuckWheelWithColumnNoiseDoesNotLatch)
{
    // Real-G29 measurement on decelerate_for_right_turn.xosc (2026-07-25):
    // target grows to ~-0.83, servo saturates at max_force=0.6, wheel is
    // stuck by hardware friction and the physical axis sits at +0.011
    // (column mechanical offset / SDL2 noise floor is ~0.001). Without a
    // deadzone on the sign-opposition arm, +0.011 counts as "user pushing
    // opposite" and false-latches at t=13.55, killing the turn at 48° of
    // the intended 92°. The deadzone (|actual| >= ε) must suppress.
    OverrideManager m;
    m.Configure(MakeConfigWithFfbThresholds(0.20, 0.04, 0.10, 0.30, 0.10));

    FfbInterventionSample s;
    s.active          = true;
    s.target_norm     = -0.83;
    s.position_error  = -0.8410;   // actual = -0.83 - (-0.841) = +0.011
    s.commanded_force = 0.60;      // saturated

    for (int i = 0; i < 100; ++i)
    {
        m.UpdateFfbSample(s);
        m.Update(QuietFrame(), 0.02);
    }
    EXPECT_FALSE(m.IsLateralManual());
}

TEST(OverrideManagerTest, FfbDriverPushesOppositeToTargetLatches)
{
    // Legitimate override: driver holds wheel at +0.30 while AD wants
    // -0.05. Sign opposition (target × actual < 0) → gate passes.
    OverrideManager m;
    m.Configure(MakeConfigWithFfbThresholds(0.20, 0.04, 0.10, 0.30, 0.10));

    FfbInterventionSample s;
    s.active          = true;
    s.target_norm     = -0.05;
    s.position_error  = -0.35;    // actual = -0.05 - (-0.35) = +0.30
    s.commanded_force = 0.60;

    for (int i = 0; i < 6; ++i)   // 120 ms > 100 ms sustain
    {
        m.UpdateFfbSample(s);
        m.Update(QuietFrame(), 0.02);
    }
    EXPECT_TRUE(m.IsLateralManual());
    EXPECT_TRUE(m.JustTransitionedToManual());
}

TEST(OverrideManagerTest, FfbDriverPushesPastTargetLatches)
{
    // Legitimate override: driver over-steers in the same direction as AD.
    // AD wants -0.20; driver pushes wheel to -0.35 (|actual| > |target|+ε).
    OverrideManager m;
    m.Configure(MakeConfigWithFfbThresholds(0.20, 0.04, 0.10, 0.30, 0.10));

    FfbInterventionSample s;
    s.active          = true;
    s.target_norm     = -0.20;
    s.position_error  = 0.15;     // actual = -0.20 - 0.15 = -0.35
    s.commanded_force = 0.60;

    for (int i = 0; i < 6; ++i)
    {
        m.UpdateFfbSample(s);
        m.Update(QuietFrame(), 0.02);
    }
    EXPECT_TRUE(m.IsLateralManual());
    EXPECT_TRUE(m.JustTransitionedToManual());
}

TEST(OverrideManagerTest, FfbWheelOverTargetGateBoundary)
{
    // Boundary test for the magnitude-opposition arm. With default ε=0.05
    // and target=+0.20, the gate opens when |actual| > 0.25.
    ManualDriveConfig cfg = MakeConfigWithFfbThresholds(0.20, 0.04, 0.10);
    ASSERT_DOUBLE_EQ(cfg.ffb.target_track.override_wheel_over_target_epsilon, 0.05);

    // Below gate: target=+0.20, position_error=-0.04 → actual=+0.24.
    // Same sign as target, |actual|=0.24 < |target|+ε=0.25.
    // force=0.22 is above 0.20 threshold so over_force fires; the gate is
    // the only thing that should keep this from latching.
    {
        OverrideManager m; m.Configure(cfg);
        FfbInterventionSample s;
        s.active          = true;
        s.target_norm     = 0.20;
        s.position_error  = -0.04;    // actual = +0.24 (below gate)
        s.commanded_force = 0.22;     // above force threshold
        for (int i = 0; i < 100; ++i) { m.UpdateFfbSample(s); m.Update(QuietFrame(), 0.02); }
        EXPECT_FALSE(m.IsLateralManual());
    }
    // At gate: actual = +0.26 (target=+0.20, position_error=-0.06). Over ε.
    {
        OverrideManager m; m.Configure(cfg);
        FfbInterventionSample s;
        s.active          = true;
        s.target_norm     = 0.20;
        s.position_error  = -0.06;   // actual = +0.26 > 0.25 = |target| + ε
        s.commanded_force = 0.24;
        for (int i = 0; i < 6; ++i)   { m.UpdateFfbSample(s); m.Update(QuietFrame(), 0.02); }
        EXPECT_TRUE(m.IsLateralManual());
    }
}

}  // namespace gt_esmini
