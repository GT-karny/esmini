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
                                              double sustain_s = 0.10)
{
    ManualDriveConfig cfg = MakeConfig();
    cfg.ffb.target_track.override_steer_force_threshold = force_thr;
    cfg.ffb.target_track.override_steer_dev_threshold   = dev_thr;
    cfg.ffb.target_track.override_sustain_time          = sustain_s;
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
    OverrideManager m;
    m.Configure(MakeConfigWithFfbThresholds(0.20, 0.04, /*sustain=*/0.10));

    FfbInterventionSample s;
    s.active = true;
    s.commanded_force = 0.50;   // above force threshold
    s.position_error  = 0.00;

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
    OverrideManager m;
    m.Configure(MakeConfigWithFfbThresholds(0.20, 0.04, 0.10));

    FfbInterventionSample s;
    s.active = true;
    s.commanded_force = 0.50;
    s.position_error  = 0.00;

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
    OverrideManager m;
    m.Configure(MakeConfigWithFfbThresholds(0.20, 0.04, 0.10));

    FfbInterventionSample hi; hi.active = true; hi.commanded_force = 0.50;
    FfbInterventionSample lo; lo.active = true; lo.commanded_force = 0.05;

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
    OverrideManager m;
    m.Configure(MakeConfigWithFfbThresholds(0.20, 0.04, 0.10));

    FfbInterventionSample hi; hi.active = true; hi.commanded_force = 0.50;
    for (int i = 0; i < 6; ++i) { m.UpdateFfbSample(hi); m.Update(QuietFrame(), 0.02); }
    ASSERT_TRUE(m.IsLateralManual());

    // Simulate release: signal goes below threshold. Latch stays.
    FfbInterventionSample lo; lo.active = true; lo.commanded_force = 0.03;
    for (int i = 0; i < 50; ++i) { m.UpdateFfbSample(lo); m.Update(QuietFrame(), 0.02); }
    EXPECT_TRUE(m.IsLateralManual());

    // Simulate the release overshoot: signal spikes back above threshold.
    // Latch still stays (already MANUAL — nothing to change).
    FfbInterventionSample spike; spike.active = true; spike.commanded_force = 0.55;
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

    FfbInterventionSample hi; hi.active = true; hi.commanded_force = 0.50;

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
    // at that point (position_error persists), torque-proxy should latch
    // shortly after target settles.
    OverrideManager m;
    ManualDriveConfig cfg = MakeConfigWithFfbThresholds(0.20, 0.04, 0.10);
    cfg.ffb.target_track.override_target_rate_gate = 0.30;
    m.Configure(cfg);

    FfbInterventionSample s;
    s.active          = true;
    s.commanded_force = 0.50;
    s.position_error  = 0.20;

    // Phase 1: 2 s ramp (rate 0.5 /s → above gate → no latch)
    for (int i = 0; i < 100; ++i)
    {
        s.target_norm = 0.01 * static_cast<double>(i);
        m.UpdateFfbSample(s);
        m.Update(QuietFrame(), 0.02);
    }
    ASSERT_FALSE(m.IsLateralManual());

    // Phase 2: target held at 1.0 (rate = 0). Driver still frozen (dev/force
    // persist above thresholds). Sustain now accumulates.
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

    // Phase 1: target static at 0 for 60 ms (accumulate but < 100 ms sustain)
    for (int i = 0; i < 3; ++i)
    {
        s.target_norm = 0.0;
        m.UpdateFfbSample(s);
        m.Update(QuietFrame(), 0.02);
    }
    // Phase 2: single frame with large target jump → rate spike
    s.target_norm = 0.5;   // (0.5 - 0.0)/0.02 = 25 /s → gate trips → reset
    m.UpdateFfbSample(s);
    m.Update(QuietFrame(), 0.02);
    // Phase 3: 60 ms more at target=0.5 static.
    // If rate-gate correctly reset the accumulator in phase 2, sustain
    // rebuilds from 0 and would need another 100 ms of stability to latch.
    for (int i = 0; i < 3; ++i)
    {
        s.target_norm = 0.5;
        m.UpdateFfbSample(s);
        m.Update(QuietFrame(), 0.02);
    }
    // Total time at "over threshold + stable" = phase 1 (60 ms) + phase 3
    // (60 ms) = 120 ms, but phase 2's jerk reset the accumulator, so effective
    // sustain window is only 60 ms (phase 3) < 100 ms sustain time → no latch.
    EXPECT_FALSE(m.IsLateralManual());
}

}  // namespace gt_esmini
