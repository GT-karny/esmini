// feature:F7 unit tests for OverrideManager (auto<->manual bidirectional latch).
//
// Locks the existing AUTO->MANUAL latch behavior, then covers the new
// AUTO_RESUME button-edge path (manual -> auto) and same-frame reentry
// suppression that returns the mode as soon as the input drops.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

#include "gt_esmini/control/manualdrive/OverrideManager.hpp"
#include "gt_esmini/control/manualdrive/FfbTargetServo.hpp"
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

// --- feature:F7 — FFB RESIDUAL intervention detector ------------------------
//
// The servo exposes, every frame, the force actually delivered to the wheel.
// The wheel's response to a force is a MEASURED property of the device
// (scripts/ffb_spike/CHARACTERIZATION.md §2/§3, real G29, 2026-07-25):
//   |f| <= 0.16          → displacement is exactly zero
//   |f| in 0.170..0.210  → breakaway (a wheel at rest starts moving)
//   moving               → v ≈ 3.35·(|f| − 0.16), saturating at ~1.0 /s
//   sign                 → positive force = wheel LEFT = axis NEGATIVE
// OverrideManager integrates that plant into a SHADOW: where the wheel would
// be right now if nobody were touching it. The detection signal is
// |actual − shadow|, sustained.
//
// WHY THE TESTS BELOW DRIVE A REAL CLOSED LOOP. The detector's inputs
// (effective force, position error, axis) are not independent numbers — they
// are three views of one physical loop. The previous generation of these
// tests hand-picked them individually, which let a test author reproduce the
// same sign/units mistake the production code made and still go green (that
// is exactly how commit 301a72ea shipped a backwards sign convention with
// 81/81 passing). Every test below instead runs the REAL
// ComputeSteerServoForce() against an explicit physical wheel plant, so the
// numbers can only be self-consistent. Hand-built samples appear in exactly
// two places, both justified in place: the servo-inactive cases (no loop
// exists) and the real-machine terminal-state replay (the numbers ARE a
// measurement).

namespace
{

// Only the detector's own knobs vary between tests; the shadow-plant
// constants stay at their shipped defaults so the suite exercises what
// actually ships.
ManualDriveConfig MakeConfigWithFfb(double sustain_s   = 0.10,
                                    double residual_thr = 0.08,
                                    double reanchor_tau = 1.5)
{
    ManualDriveConfig cfg = MakeConfig();
    cfg.ffb.target_track.override_sustain_time          = sustain_s;
    cfg.ffb.target_track.override_residual_threshold    = residual_thr;
    cfg.ffb.target_track.override_residual_reanchor_tau = reanchor_tau;
    // feature:F7 — 打ち手A（onset grace）+ 実測 θ/τ を有効にした構成でも
    // 受け入れマトリクスが通ることを、同じテスト資産で確認できるようにする。
    // 出荷既定は 0（合成プラント側の過渡が未実装で parity が壊れるため）なので、
    // 環境変数で切り替える。検出コストの数値はこの経路で取った。
    if (std::getenv("GT_F7_EVAL_AB")) {
        cfg.ffb.target_track.override_shadow_onset_grace  = 0.05;
        cfg.ffb.target_track.override_shadow_dead_time    = 0.041;
        cfg.ffb.target_track.override_shadow_velocity_tau = 0.018;
    }
    return cfg;
}

// Fresh AUTO-mode frame with no manual input.
InputFrame QuietFrame() { return MakeFrame(0.0, 0.0, 0.0, 0u); }

// The PHYSICAL wheel used to drive the tests — the real G29 characteristic,
// independent of (and deliberately not identical to) the detector's shadow
// constants. `breakaway` defaults to the MIDDLE of the measured 0.170-0.210
// band, so neither of the detector's two band-ends matches it exactly and
// the tests exercise a genuine model/plant mismatch rather than a tautology.
struct WheelPlant
{
    double breakaway = 0.19;   // measured band 0.170-0.210; mid-band default
    double kinetic   = 0.16;   // CHARACTERIZATION.md §3a
    double k         = 3.35;   // §3b force→velocity slope
    double v_max     = 1.0;    // §3b saturation
    double pos       = 0.0;
    bool   moving    = false;

    void Advance(double f, double dt)
    {
        if (!moving && std::abs(f) >= breakaway) moving = true;
        double v = 0.0;
        if (moving)
        {
            const double excess = std::abs(f) - kinetic;
            if (excess <= 0.0) moving = false;
            // Sign: positive force pushes the wheel LEFT = axis NEGATIVE.
            else v = ((f >= 0.0) ? -1.0 : 1.0) * std::min(k * excess, v_max);
        }
        pos = std::clamp(pos + v * dt, -1.0, 1.0);
    }
};

// Closed-loop rig: real servo + real plant + optional driver, producing the
// FfbInterventionSample exactly as SDLFFBSink / HeadlessFfbInput populate it.
class ServoRig
{
public:
    explicit ServoRig(double start_axis = 0.0) { plant_.pos = start_axis; }

    WheelPlant& Plant() { return plant_; }
    double      Axis() const { return plant_.pos; }

    // Report the FEEDBACK-ONLY force as the effective force — i.e. reproduce
    // the units trap described in IFFBSink.hpp. Used by exactly one test,
    // which asserts that doing so breaks the detector.
    void ReportFeedbackOnlyAsEffective(bool on) { feedback_only_ = on; }

    // Nobody is touching the wheel: it moves only under the servo's force.
    FfbInterventionSample StepHandsOff(double target, double dt)
    { return Step(target, dt, Driver::NONE, 0.0); }

    // The driver clamps the wheel at `hold`. Infinite stiffness is the right
    // idealisation here: a human arm is orders of magnitude stiffer than the
    // 0.6-unit force budget of the FFB device.
    FfbInterventionSample StepHold(double target, double dt, double hold)
    { return Step(target, dt, Driver::HOLD, hold); }

    // The driver moves the wheel at a fixed rate, independent of the servo.
    FfbInterventionSample StepRate(double target, double dt, double rate)
    { return Step(target, dt, Driver::RATE, rate); }

    // The wheel is MECHANICALLY stuck: it does not move whatever the force.
    FfbInterventionSample StepStuck(double target, double dt)
    { return Step(target, dt, Driver::STUCK, 0.0); }

private:
    enum class Driver { NONE, HOLD, RATE, STUCK };

    FfbInterventionSample Step(double target, double dt, Driver d, double arg)
    {
        double u_fb = 0.0;
        const double u = ComputeSteerServoForce(target, plant_.pos, dt, state_, cfg_, &u_fb);
        switch (d)
        {
            case Driver::NONE:  plant_.Advance(u, dt); break;
            case Driver::HOLD:  plant_.pos = arg; plant_.moving = false; break;
            case Driver::RATE:  plant_.pos = std::clamp(plant_.pos + arg * dt, -1.0, 1.0);
                                plant_.moving = true; break;
            case Driver::STUCK: break;
        }
        FfbInterventionSample s;
        s.active                 = true;
        s.target_norm            = target;
        s.position_error         = target - plant_.pos;
        s.commanded_force        = std::abs(u_fb);
        s.effective_force_signed = feedback_only_ ? u_fb : u;
        return s;
    }

    SteerServoState  state_;
    SteerServoConfig cfg_;
    WheelPlant       plant_;
    bool             feedback_only_ = false;
};

// The DELETED direction-based predicates, recomputed here so the acceptance
// tests can prove a case fires on the residual ALONE — i.e. that the old
// detector was structurally blind to it, not merely slower. ε is the old
// override_wheel_over_target_epsilon default (0.05).
bool LegacySignOpposition(double target, double actual, double eps = 0.05)
{
    return (target * actual) < 0.0 && std::abs(actual) >= eps;
}
bool LegacyMagnitudeOpposition(double target, double actual, double eps = 0.05)
{
    return std::abs(actual) > std::abs(target) + eps;
}

// Runs `frames` steps of `step`, returning the frame index the lateral latch
// fired on (-1 if it never did) and the peak residual observed.
struct RunResult
{
    int    latch_frame  = -1;
    double peak_residual = 0.0;
    double final_residual = 0.0;
    double final_shadow  = 0.0;
    double final_actual  = 0.0;
};

template <typename StepFn>
RunResult RunFrames(OverrideManager& m, int frames, double dt, StepFn step)
{
    RunResult r;
    for (int i = 0; i < frames; ++i)
    {
        m.UpdateFfbSample(step(i));
        m.Update(QuietFrame(), dt);
        const auto& d = m.GetFfbLatchDiagnostics();
        r.peak_residual  = std::max(r.peak_residual, d.residual);
        r.final_residual = d.residual;
        r.final_shadow   = d.shadow_norm;
        r.final_actual   = d.actual_norm;
        if (r.latch_frame < 0 && m.IsLateralManual()) r.latch_frame = i;
    }
    return r;
}

}  // namespace

// --- Servo-inactive / domain plumbing --------------------------------------

TEST(OverrideManagerTest, FfbSampleInactiveNeverLatches)
{
    OverrideManager m;
    m.Configure(MakeConfigWithFfb());

    // Hand-built sample: there is no servo loop to run when the servo is off.
    FfbInterventionSample s;
    s.active                 = false;   // servo off — must be ignored entirely
    s.position_error         = 1.0;
    s.effective_force_signed = 0.6;

    for (int i = 0; i < 100; ++i)  // 2 s at 50 Hz
    {
        m.UpdateFfbSample(s);
        m.Update(QuietFrame(), 0.02);
    }
    EXPECT_FALSE(m.IsLateralManual());
    EXPECT_FALSE(m.JustTransitionedToManual());
}

TEST(OverrideManagerTest, FfbScenarioLateralIsImmuneToFfbIntervention)
{
    // domain.lateral="scenario" locks lateral to AUTO. A driver blocking the
    // wheel hard enough to latch in every other test must not move it.
    OverrideManager m;
    ManualDriveConfig cfg = MakeConfigWithFfb();
    cfg.domain.lateral = "scenario";
    m.Configure(cfg);

    ServoRig rig(0.0);
    RunFrames(m, 100, 0.02, [&](int) { return rig.StepHold(0.40, 0.02, 0.0); });
    EXPECT_FALSE(m.IsLateralManual());
}

TEST(OverrideManagerTest, FfbBootstrapDoesNotFalseLatch)
{
    // The first active sample seeds the shadow from the measured axis, so the
    // residual is 0 by construction and nothing can latch on frame 1 — no
    // matter how extreme the state the servo is handed.
    OverrideManager m;
    m.Configure(MakeConfigWithFfb());

    ServoRig rig(0.0);
    m.UpdateFfbSample(rig.StepHold(0.80, 0.02, 0.0));
    m.Update(QuietFrame(), 0.02);

    EXPECT_FALSE(m.IsLateralManual());
    EXPECT_EQ(m.GetFfbLatchDiagnostics().block_reason,
              OverrideManager::FfbLatchDiagnostics::BlockReason::BOOTSTRAP);
    EXPECT_DOUBLE_EQ(m.GetFfbLatchDiagnostics().residual, 0.0);
}

// --- Closed-loop feedback protection (unchanged behaviour) ------------------
//
// While the servo is active the physical wheel is being DRIVEN by it, so the
// next frame's raw axis read is the servo's own motion. Running the direct
// |pedal_steer.steering| > threshold check there would latch MANUAL from the
// servo's own output and kill the servo (real-machine bug after 1c2939a0).

TEST(OverrideManagerTest, FfbActiveSuppressesDirectSteeringThreshold)
{
    OverrideManager m;
    m.Configure(MakeConfigWithFfb());

    // Hands-off run: the servo drives the wheel far past the 0.05 direct-axis
    // threshold. Neither path may latch.
    ServoRig rig(0.0);
    for (int i = 0; i < 250; ++i)
    {
        m.UpdateFfbSample(rig.StepHandsOff(0.50, 0.02));
        m.Update(MakeFrame(/*steering=*/rig.Axis()), 0.02);
    }
    ASSERT_GT(std::abs(rig.Axis()), 0.05);   // the axis really did cross it
    EXPECT_FALSE(m.IsLateralManual());
    EXPECT_FALSE(m.JustTransitionedToManual());
}

TEST(OverrideManagerTest, FfbInactiveKeepsDirectSteeringThreshold)
{
    // Servo OFF (target_track disabled / no FFB-capable input): the pre-F7b
    // direct-axis behaviour must be preserved exactly. This is also the
    // ffb_target_track_enabled=false invariant — with no active sample the
    // residual detector is inert and cannot change any outcome.
    OverrideManager m;
    m.Configure(MakeConfigWithFfb());

    FfbInterventionSample s;
    s.active = false;

    m.UpdateFfbSample(s);
    m.Update(MakeFrame(/*steering=*/0.10), 0.02);   // 0.10 > 0.05 threshold
    EXPECT_TRUE(m.IsLateralManual());
    EXPECT_TRUE(m.JustTransitionedToManual());
    EXPECT_EQ(m.GetFfbLatchDiagnostics().block_reason,
              OverrideManager::FfbLatchDiagnostics::BlockReason::INACTIVE);
}

// --- Acceptance matrix §3.1: what must be DETECTED --------------------------

TEST(OverrideManagerTest, AcceptanceA_DriverHoldsInsideAdTargetLatchesOnResidualAlone)
{
    // (a) The case the direction-based detector was structurally blind to:
    // the driver holds the wheel SHORT of the AD command — same direction,
    // smaller magnitude. AD wants +0.20; the driver refuses to let the wheel
    // past +0.05.
    //
    // The criterion is POSITIONAL and stated numerically: the wheel must stay
    // inside sign(actual) == sign(target) AND |actual| < |target| for the
    // whole run. ("Both legacy gates read false" is NOT a criterion — with
    // the direction-based gates deleted it is vacuously true and proves
    // nothing.) Staying inside that region is exactly what made the case
    // undetectable before: it is where an obedient, servo-driven wheel also
    // lives, so no position-only test can separate the two. The residual can,
    // because it compares against what the FORCE says should have happened.
    const double target = 0.20;
    const double hold   = 0.05;
    const double dt     = 0.02;

    OverrideManager m;
    m.Configure(MakeConfigWithFfb());

    ServoRig rig(hold);   // the driver already has the wheel when AD commands
    int    frames_inside_region = 0;
    int    frames_total         = 0;
    double max_abs_actual       = 0.0;
    const RunResult r = RunFrames(m, 100, dt, [&](int) {
        const FfbInterventionSample s = rig.StepHold(target, dt, hold);
        const double actual = s.target_norm - s.position_error;
        ++frames_total;
        max_abs_actual = std::max(max_abs_actual, std::abs(actual));
        if (actual * s.target_norm > 0.0 && std::abs(actual) < std::abs(s.target_norm))
            ++frames_inside_region;
        return s;
    });

    EXPECT_EQ(frames_inside_region, frames_total)
        << "the wheel must stay inside sign(actual)==sign(target) && |actual|<|target| "
           "for the whole run — otherwise this is case (b)/(c), not case (a)";
    ASSERT_GE(r.latch_frame, 0) << "residual detector failed to fire";
    std::cout << "[accept a] target=" << target << " actual=" << hold
              << " (same sign, |actual|/|target|=" << (max_abs_actual / std::abs(target))
              << ", inside-region frames " << frames_inside_region << "/" << frames_total << ")"
              << " latch_frame=" << r.latch_frame
              << " t=" << (r.latch_frame + 1) * dt << "s"
              << " peak_residual=" << r.peak_residual << "\n";
    EXPECT_TRUE(m.IsLateralManual());
}

TEST(OverrideManagerTest, AcceptanceB_DriverOvertakesAdTargetLatches)
{
    // (b) Driver turns PAST where AD wants — the legacy magnitude arm also
    // caught this one; the residual must not regress it.
    const double target = 0.20;
    const double hold   = 0.40;
    const double dt     = 0.02;

    OverrideManager m;
    m.Configure(MakeConfigWithFfb());
    ServoRig rig(hold);
    const RunResult r = RunFrames(m, 100, dt, [&](int) { return rig.StepHold(target, dt, hold); });

    ASSERT_GE(r.latch_frame, 0);
    std::cout << "[accept b] latch_frame=" << r.latch_frame
              << " t=" << (r.latch_frame + 1) * dt << "s"
              << " peak_residual=" << r.peak_residual << "\n";
    EXPECT_TRUE(LegacyMagnitudeOpposition(target, hold));   // legacy caught it too
}

TEST(OverrideManagerTest, AcceptanceC_DriverCountersteersLatches)
{
    // (c) Driver turns the OPPOSITE way — the legacy sign arm also caught it.
    const double target = 0.20;
    const double hold   = -0.30;
    const double dt     = 0.02;

    OverrideManager m;
    m.Configure(MakeConfigWithFfb());
    ServoRig rig(hold);
    const RunResult r = RunFrames(m, 100, dt, [&](int) { return rig.StepHold(target, dt, hold); });

    ASSERT_GE(r.latch_frame, 0);
    std::cout << "[accept c] latch_frame=" << r.latch_frame
              << " t=" << (r.latch_frame + 1) * dt << "s"
              << " peak_residual=" << r.peak_residual << "\n";
    EXPECT_TRUE(LegacySignOpposition(target, hold));
}

TEST(OverrideManagerTest, AcceptanceD_DriverGripsAtZeroWhileAdSteersLatches)
{
    // (d) DELIBERATE SPEC CHANGE. The driver grips the wheel at centre and
    // does not let it move while AD steers away. The old detector documented
    // this as undetectable ("user firmly at 0" was declared indistinguishable
    // from "wheel stuck at rest"); it is now detected, because the shadow
    // knows the servo is applying 0.6 of force and a free wheel would have
    // moved. The wheel never leaves |actual| = 0 — no position-only test can
    // tell this from a wheel that is merely stuck.
    const double target = 0.20;
    const double dt     = 0.02;

    OverrideManager m;
    m.Configure(MakeConfigWithFfb());
    ServoRig rig(0.0);
    double max_abs_actual = 0.0;
    const RunResult r = RunFrames(m, 100, dt, [&](int) {
        const FfbInterventionSample s = rig.StepHold(target, dt, 0.0);
        max_abs_actual = std::max(max_abs_actual, std::abs(s.target_norm - s.position_error));
        return s;
    });

    EXPECT_DOUBLE_EQ(max_abs_actual, 0.0) << "the wheel must never move in this case";
    ASSERT_GE(r.latch_frame, 0);
    std::cout << "[accept d] target=" << target << " actual held at 0 (max|actual|="
              << max_abs_actual << ") latch_frame=" << r.latch_frame
              << " t=" << (r.latch_frame + 1) * dt << "s"
              << " peak_residual=" << r.peak_residual << "\n";
}

TEST(OverrideManagerTest, AcceptanceA_SmallAdCommandStillLatches)
{
    // The weakest genuine intervention, and the one that sizes the re-anchor
    // time constant: a lane-change-scale AD command (|target| ≈ 0.05, see
    // CHARACTERIZATION.md §1 — the real lc profile peaks at 0.065) with the
    // driver gently holding short of it. The servo settles well below
    // saturation, so the shadow runs away slowly; the leak must not out-run
    // it. If this test starts failing, override_residual_reanchor_tau is too
    // small (or the threshold too large) — see the sizing note in
    // ManualDriveConfig.
    const double target = 0.05;
    const double hold   = 0.02;
    const double dt     = 0.02;

    OverrideManager m;
    m.Configure(MakeConfigWithFfb());
    ServoRig rig(hold);
    const RunResult r = RunFrames(m, 200, dt, [&](int) { return rig.StepHold(target, dt, hold); });

    ASSERT_GE(r.latch_frame, 0) << "weakest genuine intervention must still latch";
    std::cout << "[accept a-small] latch_frame=" << r.latch_frame
              << " t=" << (r.latch_frame + 1) * dt << "s"
              << " peak_residual=" << r.peak_residual << "\n";
}

TEST(OverrideManagerTest, MovingAdTargetDoesNotBlockDetection)
{
    // THE ORIGINAL F7 DEFECT. The AD steering safety envelope ramps its
    // target at up to ~2.5 axis-frac/s during the post-RESUME recovery — far
    // above the old 0.30 target-rate gate, which therefore blacked out
    // detection for the entire window in which a driver is most likely to
    // grab the wheel. The residual is target-motion-invariant by
    // construction, so a driver blocking the wheel during a fast ramp latches
    // normally.
    const double dt = 0.02;

    OverrideManager m;
    m.Configure(MakeConfigWithFfb());
    ServoRig rig(0.0);

    // Sampled AT the latch frame, not at the end of the run — by the end the
    // ramp has finished and the gate has settled, which would make the
    // assertion vacuous.
    int  latch_frame       = -1;
    bool gated_when_latched = false;
    for (int i = 0; i < 100; ++i)
    {
        const double target = std::min(0.6, 2.5 * (i * dt));   // 2.5 axis-frac/s ramp
        m.UpdateFfbSample(rig.StepHold(target, dt, 0.0));
        m.Update(QuietFrame(), dt);
        // Sampled over the whole window UP TO the latch, not only at the latch
        // frame. The point of this flag is "was the blackout condition present
        // while the driver was pushing", and the ramp that creates it finishes
        // at t=0.24s. Any change that adds latch latency (dead-time / inertia /
        // onset grace all do) pushes the latch past the ramp and would fail a
        // same-frame check for a reason that has nothing to do with detection.
        const auto& d = m.GetFfbLatchDiagnostics();
        if (latch_frame < 0 && (d.moving_target || d.tracking_transient))
            gated_when_latched = true;
        if (latch_frame < 0 && m.IsLateralManual())
            latch_frame = i;
    }

    ASSERT_GE(latch_frame, 0) << "a driver must be able to take over during an AD ramp";
    EXPECT_TRUE(gated_when_latched)
        << "the old rate gates must have been tripped at the moment of the latch — "
           "otherwise this test does not exercise the blackout it exists to prove gone";
    std::cout << "[ramp takeover] latch_frame=" << latch_frame
              << " t=" << (latch_frame + 1) * dt << "s"
              << " rate_gate_tripped_at_latch=" << gated_when_latched << "\n";
}

// --- Acceptance matrix §3.4: the re-anchor blind spot -----------------------

TEST(OverrideManagerTest, Acceptance34_MinimumDetectableDriverRampRate)
{
    // The drift control of §2.3 has a necessary dual: a push slow enough for
    // the re-anchor to follow is never detected. This measures where that
    // floor actually is instead of asserting it away — the same class of hole
    // as the original defect, so it gets a number in the acceptance matrix.
    //
    // Setup isolates the mechanism: AD holds a STATIC target equal to where
    // the wheel already sits, so the servo contributes nothing and the whole
    // residual comes from the driver's own motion. The driver then turns the
    // wheel away at a fixed rate. Anything faster than the floor latches;
    // anything slower is tracked by the re-anchor and never does.
    //
    // Predicted floor (see ManualDriveConfig sizing note): the residual
    // settles at r* = rate * tau, so detection needs
    //     rate > residual_threshold / reanchor_tau = 0.08 / 1.5 = 0.053 /s.
    // In practice the measured floor is LOWER, because a driver moving away
    // from target grows the tracking error, which grows the servo force,
    // which starts the shadow moving the OTHER way — the divergence is
    // self-amplifying once |f| clears breakaway (err ≳ 0.015).
    const double dt = 0.02;

    double slowest_detected = -1.0;
    double fastest_missed   = -1.0;
    for (double rate : {0.005, 0.010, 0.020, 0.030, 0.040, 0.053, 0.080, 0.150, 0.300})
    {
        OverrideManager m;
        m.Configure(MakeConfigWithFfb());
        ServoRig rig(0.0);
        const RunResult r = RunFrames(m, 1000, dt, [&](int) {   // 20 s of pushing
            return rig.StepRate(/*target=*/0.0, dt, rate);
        });
        const bool detected = r.latch_frame >= 0;
        std::cout << "[accept 3.4] driver_ramp=" << rate << " axis-frac/s"
                  << " detected=" << detected
                  << " latch_t=" << (detected ? (r.latch_frame + 1) * dt : -1.0)
                  << " peak_residual=" << r.peak_residual
                  << " final|actual|=" << std::abs(r.final_actual) << "\n";
        if (detected) { if (slowest_detected < 0.0 || rate < slowest_detected) slowest_detected = rate; }
        else          { if (rate > fastest_missed) fastest_missed = rate; }
    }
    std::cout << "[accept 3.4] SUMMARY-rate slowest_detected=" << slowest_detected
              << " axis-frac/s, fastest_missed=" << fastest_missed << " axis-frac/s\n";

    ASSERT_GT(slowest_detected, 0.0) << "no ramp rate was detected at all";
    EXPECT_LE(slowest_detected, 0.08 / 1.5)
        << "detection floor exceeded threshold/tau — the re-anchor is masking pushes";
    EXPECT_LT(fastest_missed, slowest_detected) << "detection must be monotone in rate";

    // MEASURED RESULT, and it is not the shape the analytic bound predicted:
    // there is effectively NO rate floor. Even 0.005 axis-frac/s is caught,
    // because a driver who MOVES the wheel away from target grows the tracking
    // error, which grows the servo force, which starts the shadow moving the
    // other way — the divergence is self-amplifying, so the re-anchor never
    // gets to keep up. The analytic floor of threshold/tau only applies to a
    // divergence that stays at a constant rate, which this one does not.
    //
    // The real blind spot is therefore a DISPLACEMENT, not a rate: a driver
    // who blocks the wheel so close to the AD target that the servo force
    // never reaches breakaway. There the shadow correctly does not move (an
    // unloaded wheel would not either), so the residual stays at zero for as
    // long as that holds. Measured below.
    double largest_missed_offset  = 0.0;
    double smallest_detected_offset = -1.0;
    for (double offset : {0.005, 0.010, 0.015, 0.020, 0.030, 0.050})
    {
        // AD wants `offset` more than where the driver is clamping the wheel.
        OverrideManager m;
        m.Configure(MakeConfigWithFfb());
        ServoRig rig(0.0);
        double peak_force = 0.0;
        const RunResult r = RunFrames(m, 1000, dt, [&](int) {   // 20 s of blocking
            const FfbInterventionSample s = rig.StepHold(offset, dt, 0.0);
            peak_force = std::max(peak_force, std::abs(s.effective_force_signed));
            return s;
        });
        const bool detected = r.latch_frame >= 0;
        std::cout << "[accept 3.4] block_offset=" << offset
                  << " peak|f|=" << peak_force
                  << " detected=" << detected
                  << " latch_t=" << (detected ? (r.latch_frame + 1) * dt : -1.0)
                  << " peak_residual=" << r.peak_residual << "\n";
        if (detected) { if (smallest_detected_offset < 0.0) smallest_detected_offset = offset; }
        else          { largest_missed_offset = offset; }
    }
    std::cout << "[accept 3.4] SUMMARY-displacement largest_missed=" << largest_missed_offset
              << " smallest_detected=" << smallest_detected_offset << " axis-frac\n";

    ASSERT_GT(smallest_detected_offset, 0.0) << "no blocking offset was detected at all";
    // The undetectable band must stay small enough to be operationally
    // irrelevant: 0.02 axis-frac is ~9 deg of a 900 deg wheel. A driver
    // resisting by less than that is not overriding anything.
    EXPECT_LE(largest_missed_offset, 0.02)
        << "the undetectable blocking band has grown beyond a wheel's own slack";
    EXPECT_LT(largest_missed_offset, smallest_detected_offset)
        << "detection must be monotone in blocking offset";
}

// --- Acceptance matrix §3.2: what must NOT fire -----------------------------

TEST(OverrideManagerTest, FalsePositive1_StuckWheelWithinMeasuredBandDoesNotLatch)
{
    // (1) Mechanically stuck wheel — b6dc58f0. A wheel that will not move is
    // only physically consistent with a force at or below the top of the
    // measured breakaway band (0.210); above that the device is measured to
    // move. The detector must stay quiet across the WHOLE band, which it does
    // because the bottom-of-band arm additionally requires observed motion.
    //
    // Scope, stated honestly: a wheel held immobile under a force ABOVE the
    // band is indistinguishable from acceptance case (d) — "something is
    // stopping the wheel" is the entire measurement, and (d) requires that to
    // latch. The two cannot both be satisfied; §3.1(d) chose detection. Below
    // the band there is no such conflict, and that is where the real-machine
    // stuck states were measured.
    const double dt = 0.02;
    for (double target : {0.010, 0.012, 0.015})
    {
        OverrideManager m;
        m.Configure(MakeConfigWithFfb());
        ServoRig rig(0.0);
        double peak_force = 0.0;
        const RunResult r = RunFrames(m, 250, dt, [&](int) {
            const FfbInterventionSample s = rig.StepStuck(target, dt);
            peak_force = std::max(peak_force, std::abs(s.effective_force_signed));
            return s;
        });
        std::cout << "[fp1] target=" << target << " peak|f|=" << peak_force
                  << " peak_residual=" << r.peak_residual
                  << " latch_frame=" << r.latch_frame << "\n";
        ASSERT_LE(peak_force, 0.2101) << "fixture must stay inside the measured band";
        EXPECT_LT(r.latch_frame, 0) << "stuck wheel inside the breakaway band must not latch";
    }
}

TEST(OverrideManagerTest, FalsePositive2_StartupAndServoTransientDoesNotLatch)
{
    // (2) Startup / servo-tracking transient — f8a5ce56. Wheel at rest, AD
    // commands a step, the servo accelerates the wheel from zero. The shadow
    // is driven by the same force that accelerates the real wheel, so they
    // move together and the residual never builds.
    const double dt = 0.02;
    for (double target : {0.05, 0.15, 0.30, 0.60})
    {
        OverrideManager m;
        m.Configure(MakeConfigWithFfb());
        ServoRig rig(0.0);
        const RunResult r = RunFrames(m, 250, dt, [&](int) { return rig.StepHandsOff(target, dt); });
        std::cout << "[fp2] target=" << target
                  << " peak_residual=" << r.peak_residual
                  << " final actual=" << r.final_actual << " shadow=" << r.final_shadow << "\n";
        EXPECT_LT(r.latch_frame, 0) << "hands-off step response must not latch (target=" << target << ")";
    }
}

TEST(OverrideManagerTest, FalsePositive3_HandsOffMovingTargetDoesNotLatch)
{
    // (3) The torque-proxy / closed-loop false-positive class — 549e5823.
    // Hands-off through a continuously moving AD target (curve + lane change
    // shape). This is the case that used to need the rate gates.
    const double dt = 0.02;

    OverrideManager m;
    m.Configure(MakeConfigWithFfb());
    ServoRig rig(0.0);
    const RunResult r = RunFrames(m, 1500, dt, [&](int i) {     // 30 s
        const double t = i * dt;
        // Slow curve (0.13 amplitude, 8 s period) with a lane change riding
        // on top (0.06 amplitude, 2.5 s period) — the two real AD profiles
        // measured in CHARACTERIZATION.md §1.
        const double target = 0.13 * std::sin(2.0 * 3.14159265358979 * t / 8.0) +
                              0.06 * std::sin(2.0 * 3.14159265358979 * t / 2.5);
        return rig.StepHandsOff(target, dt);
    });
    std::cout << "[fp3] peak_residual=" << r.peak_residual
              << " threshold=0.08 latch_frame=" << r.latch_frame << "\n";
    EXPECT_LT(r.latch_frame, 0) << "30 s of hands-off AD steering must not latch";
}

TEST(OverrideManagerTest, FalsePositive4_ForceCoupledPlantAcrossMeasuredBandDoesNotLatch)
{
    // (4) Servo lag with hands off, under the force-coupled plant. The plant
    // is swept across the whole measured breakaway band and ±10 % on the
    // force→velocity slope, because those are the real calibration
    // uncertainties. (The old `lagging` mode was a target-only low-pass that
    // never looked at force at all, so it could not represent this at all —
    // see scripts/vd_ffb_notouch_parity.py.)
    const double dt = 0.02;
    for (double breakaway : {0.17, 0.19, 0.21})
    {
        for (double k : {3.0, 3.35, 3.7})
        {
            OverrideManager m;
            m.Configure(MakeConfigWithFfb());
            ServoRig rig(0.0);
            rig.Plant().breakaway = breakaway;
            rig.Plant().k         = k;

            const RunResult r = RunFrames(m, 750, dt, [&](int i) {   // 15 s
                const double t = i * dt;
                const double target = 0.30 * std::sin(2.0 * 3.14159265358979 * t / 6.0);
                return rig.StepHandsOff(target, dt);
            });
            std::cout << "[fp4] breakaway=" << breakaway << " k=" << k
                      << " peak_residual=" << r.peak_residual
                      << " latch_frame=" << r.latch_frame << "\n";
            EXPECT_LT(r.latch_frame, 0)
                << "hands-off force-coupled plant must not latch (breakaway=" << breakaway
                << " k=" << k << ")";
        }
    }
}

TEST(OverrideManagerTest, FalsePositive5_RealMachineTerminalStateDoesNotLatch)
{
    // (5) THE ONLY REAL-MEASUREMENT acceptance case. Source:
    // test_results/f7_realwheel_stuck_check.log, 2026-07-26 G29 session,
    // decelerate_for_right_turn.xosc run hands-off to completion. Sampled
    // stretch t=17.49-24.99 (7.5 s, 16 samples):
    //     effective force  |f| = 0.166 .. 0.180 (mean 0.1715), sign NEGATIVE
    //     measured wheel displacement = 0 (exactly)
    //     steady tracking error err = 0.0112 .. 0.0120
    // Hand-built sample by design: these numbers ARE the measurement.
    //
    // The err figure is the one solving |u| = 4.0*err + 0.15*tanh(err/0.010)
    // for the observed |u|. (An earlier revision of this case quoted
    // err ≈ 0.005 by assuming the tanh had saturated; with
    // friction_ff_eps = 0.010 it has not — tanh(0.5) = 0.462 — and that
    // error understated |u| by 2.3x. Left recorded here because asserting
    // the wrong pair would have frozen a physically impossible state into a
    // permanent test.)
    //
    // WHY NON-FIRING IS CORRECT: the force is negative, i.e. pushing the
    // wheel RIGHT, and the right-hand breakaway band starts at 0.190. Every
    // observed frame sits at or below 0.180 — clear of onset by >= 0.010 in
    // BOTH arms (the unconditional 0.210 and the right-hand band bottom
    // 0.190). The wheel is in a region where an unloaded wheel does not move
    // either, so the shadow stays parked with it and the residual is zero.
    // This is also why the kinetic floor (0.16) must not be used as the
    // deadzone: 0.1715 is above it, and a 0.16 deadzone would predict
    // 3.35*(0.1715-0.16)*7.5 s = 0.29 axis-frac of motion that never happened.
    OverrideManager m;
    m.Configure(MakeConfigWithFfb());

    // Sweep the measured |f| range; every frame must stay quiet.
    for (double f_mag : {0.166, 0.1715, 0.180})
    {
        OverrideManager mm;
        mm.Configure(MakeConfigWithFfb());
        FfbInterventionSample s;
        s.active                 = true;
        s.target_norm            = -0.830;
        s.position_error         = 0.0116;               // measured steady err
        s.commanded_force        = 4.0 * 0.0116;         // feedback-only term
        s.effective_force_signed = -f_mag;               // NEGATIVE = pushing right

        for (int i = 0; i < 500; ++i)   // 10 s, longer than the measured 7.5 s
        {
            mm.UpdateFfbSample(s);
            mm.Update(QuietFrame(), 0.02);
        }
        const auto& d = mm.GetFfbLatchDiagnostics();
        std::cout << "[fp5] |f|=" << f_mag << " residual=" << d.residual
                  << " shadow=" << d.shadow_norm << " actual=" << d.actual_norm
                  << " shadow_moving=" << d.shadow_moving << "\n";
        EXPECT_FALSE(mm.IsLateralManual());
        EXPECT_FALSE(d.shadow_moving) << "|f|=" << f_mag << " is below the right-hand "
                                         "breakaway band bottom (0.190) — the shadow must not move";
        EXPECT_DOUBLE_EQ(d.residual, 0.0);
    }
    (void)m;
}

// --- The §2.2 units trap: effective force vs feedback-only force ------------

TEST(OverrideManagerTest, FeedingFeedbackOnlyForceToTheShadowFalseLatches)
{
    // Regression guard for the trap documented on
    // FfbInterventionSample::effective_force_signed. commanded_force* exclude
    // the Coulomb friction feed-forward (0.15) on purpose. That is not a
    // rounding difference against a 0.17-0.21 breakaway: it decides whether
    // the shadow moves AT ALL.
    //
    // Same hands-off scenario, run twice. With the EFFECTIVE force the
    // detector is quiet; with the FEEDBACK-ONLY force it false-latches,
    // because the shadow is told the wheel cannot move while the real wheel
    // tracks the ramp. This trap is invisible to call-graph review — only a
    // prediction-vs-measurement comparison exposes it.
    const double dt = 0.02;
    auto run = [&](bool feedback_only) {
        OverrideManager m;
        m.Configure(MakeConfigWithFfb());
        ServoRig rig(0.0);
        rig.ReportFeedbackOnlyAsEffective(feedback_only);
        const RunResult r = RunFrames(m, 400, dt, [&](int i) {   // 8 s
            const double target = std::min(0.30, 0.10 * (i * dt));   // gentle 0.10/s ramp
            return rig.StepHandsOff(target, dt);
        });
        return r;
    };

    const RunResult correct = run(false);
    const RunResult trapped = run(true);
    std::cout << "[units trap] effective: peak_residual=" << correct.peak_residual
              << " latch_frame=" << correct.latch_frame
              << " | feedback-only: peak_residual=" << trapped.peak_residual
              << " latch_frame=" << trapped.latch_frame << "\n";

    EXPECT_LT(correct.latch_frame, 0) << "effective force: hands-off must stay quiet";
    EXPECT_GE(trapped.latch_frame, 0)
        << "feedback-only force must visibly break the detector — if this stops "
           "failing, the two forces have converged and the guard is dead";
}

// --- Latch / release semantics ---------------------------------------------

TEST(OverrideManagerTest, FfbLatchHoldsUntilResume)
{
    // Once latched, MANUAL is held regardless of what the FFB sample does.
    // Release goes through AUTO_RESUME only (spike §2d) — never through
    // "the signal dropped".
    const double dt = 0.02;
    OverrideManager m;
    m.Configure(MakeConfigWithFfb());

    ServoRig rig(0.0);
    RunFrames(m, 100, dt, [&](int) { return rig.StepHold(0.40, dt, 0.0); });
    ASSERT_TRUE(m.IsLateralManual());

    // Driver lets go; the servo is still active and now tracks freely.
    for (int i = 0; i < 250; ++i)
    {
        m.UpdateFfbSample(rig.StepHandsOff(0.40, dt));
        m.Update(QuietFrame(), dt);
    }
    EXPECT_TRUE(m.IsLateralManual());

    m.Update(MakeFrame(0.0, 0.0, 0.0, ButtonBits::AUTO_RESUME), dt);
    EXPECT_FALSE(m.IsAnyManual());
    EXPECT_TRUE(m.JustTransitionedToAuto());
}

TEST(OverrideManagerTest, FfbResumeEdgeWinsOverSustainedFfbSameFrame)
{
    const double dt = 0.02;
    OverrideManager m;
    m.Configure(MakeConfigWithFfb());

    ServoRig rig(0.0);
    RunFrames(m, 100, dt, [&](int) { return rig.StepHold(0.40, dt, 0.0); });
    ASSERT_TRUE(m.IsLateralManual());

    // Same frame: RESUME pressed AND the driver still blocking the wheel.
    m.UpdateFfbSample(rig.StepHold(0.40, dt, 0.0));
    m.Update(MakeFrame(0.0, 0.0, 0.0, ButtonBits::AUTO_RESUME), dt);
    EXPECT_FALSE(m.IsAnyManual());
    EXPECT_TRUE(m.JustTransitionedToAuto());
}

TEST(OverrideManagerTest, FfbMultiCycleInterventionResumeReintervention)
{
    // §3.3 — THE USER'S ORIGINAL DEFECT. Intervene, RESUME, intervene AGAIN,
    // RESUME, intervene a THIRD time. Testing a single cycle is what let the
    // re-intervention failure ship: the shadow retained the reference it had
    // been driven to during the first push, so the second push was measured
    // against a meaningless baseline and never crossed the threshold.
    //
    // The servo is switched OFF while MANUAL, exactly as
    // ControllerVirtualDriver does (lat_manual → target_active=false), so this
    // also exercises the shadow's re-seed path on servo re-activation.
    const double dt     = 0.02;
    const double target = 0.30;

    OverrideManager m;
    m.Configure(MakeConfigWithFfb());
    ServoRig rig(0.0);

    for (int cycle = 1; cycle <= 3; ++cycle)
    {
        // Hands-off settle: AD steers, the wheel follows, nothing latches.
        const RunResult settle =
            RunFrames(m, 150, dt, [&](int) { return rig.StepHandsOff(target, dt); });
        ASSERT_LT(settle.latch_frame, 0) << "cycle " << cycle << ": hands-off settle false-latched";

        // Driver grabs the wheel and holds it short of the AD target.
        const RunResult push =
            RunFrames(m, 150, dt, [&](int) { return rig.StepHold(target, dt, 0.05); });
        ASSERT_GE(push.latch_frame, 0) << "cycle " << cycle << ": intervention did NOT latch";
        ASSERT_TRUE(m.IsLateralManual());
        std::cout << "[multi-cycle] cycle " << cycle << " latch_frame=" << push.latch_frame
                  << " t=" << (push.latch_frame + 1) * dt << "s"
                  << " peak_residual=" << push.peak_residual << "\n";

        // MANUAL: the controller drops the servo, so the sample goes inactive.
        FfbInterventionSample off;
        off.active = false;
        for (int i = 0; i < 50; ++i) { m.UpdateFfbSample(off); m.Update(QuietFrame(), dt); }
        EXPECT_TRUE(m.IsLateralManual());

        // Driver presses RESUME and lets go.
        m.UpdateFfbSample(off);
        m.Update(MakeFrame(0.0, 0.0, 0.0, ButtonBits::AUTO_RESUME), dt);
        ASSERT_FALSE(m.IsAnyManual()) << "cycle " << cycle << ": RESUME did not return to AUTO";
        rig = ServoRig(rig.Axis());   // fresh servo state, wheel where it was
    }
}

TEST(OverrideManagerTest, FfbShadowReanchorPreventsLongRunDrift)
{
    // The shadow is an integrator, so an unbounded-drift bug would only show
    // up on a long run. 120 s of hands-off driving with a plant that is
    // deliberately mis-calibrated against the detector's model (bottom-of-band
    // breakaway, 10 % slow) must still end with the residual well under the
    // threshold — not merely un-latched.
    const double dt = 0.02;
    OverrideManager m;
    m.Configure(MakeConfigWithFfb());
    ServoRig rig(0.0);
    rig.Plant().breakaway = 0.17;
    rig.Plant().k         = 3.0;

    const RunResult r = RunFrames(m, 6000, dt, [&](int i) {   // 120 s
        const double t = i * dt;
        const double target = 0.20 * std::sin(2.0 * 3.14159265358979 * t / 10.0);
        return rig.StepHandsOff(target, dt);
    });
    std::cout << "[drift] 120 s peak_residual=" << r.peak_residual
              << " final_residual=" << r.final_residual << "\n";
    EXPECT_LT(r.latch_frame, 0);
    EXPECT_LT(r.peak_residual, 0.08);
}

}  // namespace gt_esmini
