#pragma once

#include "gt_esmini/control/manualdrive/IInputSource.hpp"

#include <memory>

namespace gt_esmini
{

class IFFBSink;
class ITransport;
struct ManualDriveConfig;

// feature:F7 (F7b) — headless-testable input source.
//
// Motivation: the initial F7b implementation (commit 1c2939a0) passed unit AND
// regression gates but died on the real G29 because the CLOSED LOOP formed by
// {SDLFFBSink servo → physical wheel → SDL_JoystickGetAxis → SDL2WheelInput::Poll
// → OverrideManager.pedal_steer.steering path} was never exercised by any
// automated test. The unit suite injected pedal_steer and ffb_sample_
// independently and thus could not surface the bug where the servo would trip
// its own override on frame 2 (fix landed in a43e4c67).
//
// This input source closes that loop headlessly: it provides an IFFBSink whose
// "physical wheel" is a synthetic model, and it echoes that same synthetic
// value back through pedal_steer.steering — exactly the shape the real
// SDL2 wheel presents. Any regression that recreates the closed-loop bug
// (or a similar wiring failure between VD Step and OverrideManager) will
// surface here without needing a G29 plugged in.
//
// Configured via env vars (test-only knobs, kept out of ManualDriveConfig):
//   GT_HEADLESS_FFB_MODE       : "follower" (default) | "frozen" | "lagging" | "pushback" | "plant"
//     - follower  : the synthetic axis mirrors target_norm_ exactly each
//                   frame. position_error≈0, commanded_force≈0 → the
//                   torque-proxy latch stays quiet AND the raw axis is
//                   whatever the servo commanded. Regression asserts:
//                   override.lateral MUST stay false through the whole run.
//     - frozen    : the synthetic axis is held at GT_HEADLESS_FFB_FROZEN_AT
//                   (default 0.0). Simulates a driver bracing the wheel.
//                   When AD steers the target away from frozen_at past
//                   dev_threshold, torque-proxy fires; assert MANUAL latch
//                   fires within ~sustain+dt after AD's first sizable turn.
//     - lagging   : the synthetic axis chases target_norm_ through a 1st-
//                   order low-pass (GT_HEADLESS_FFB_LAG_TAU) instead of
//                   instantly (follower) or not at all (frozen). This is the
//                   only mode where BOTH position_error and the wheel's own
//                   d(actual)/dt are simultaneously non-zero for an extended
//                   stretch with NO driver present — the shape a rate- or
//                   velocity-based FFB signal has to get right. See
//                   scripts/vd_ffb_notouch_parity.py FOLLOWER_MODES for why
//                   this is swept permanently in the no-touch parity check.
//   GT_HEADLESS_FFB_FROZEN_AT  : "<double>" (default 0.0) — axis position
//                                to hold in "frozen" mode.
//   GT_HEADLESS_FFB_LAG_TAU    : "<double>" seconds (default 0.30 — spike
//                                §1e G29 step response) — 1st-order low-pass
//                                time constant in "lagging" mode.
//
//     - pushback  : feature:F7c multi-cycle-override repro addendum. Neither
//                   "follower" (dev always ~0, can never latch) nor "frozen"
//                   (dev always large, latches unconditionally and can never
//                   model "driver lets go") can exercise a SCRIPTED sequence
//                   of intervene/release cycles within one process — team
//                   feedback (2026-07-25) was that a headless repro attempt
//                   using "frozen" only proved the degenerate always-latched
//                   case, not whether a genuine SECOND intervention re-latches.
//                   "pushback" approximates the real driver-vs-servo torque
//                   contest without a strict physical model: the synthetic
//                   axis follows target_norm_ (dev~=0, no false latch) UNLESS
//                   a "pushback" offset is currently injected, in which case
//                   axis = clamp(target_norm_ + pushback_norm_, -1, 1) — dev
//                   grows by exactly the injected amount, and returns to ~0
//                   the instant the injected value goes back to 0. The
//                   pushback value is read fresh every frame from a small
//                   dedicated UDP listener (same 44-byte PSTC wire format as
//                   NetworkInputBridge; only the "steering" field is used) on
//                   GT_HEADLESS_FFB_PUSHBACK_PORT — this is what lets a test
//                   script drive MULTIPLE push/release cycles inside a single
//                   process run (env vars like GT_HEADLESS_FFB_FROZEN_AT are
//                   read once at Configure() and fixed for the process).
//   GT_HEADLESS_FFB_PUSHBACK_PORT : "<int>" (default 9105) — UDP port the
//                                pushback listener binds to. Only opened when
//                                mode=="pushback"; no-op otherwise.
//
//     - plant     : task:F7 force-coupled plant mode (design spec
//                   test_results/f7_force_coupled_plant_spec.md, 2026-07-26).
//                   Unlike "lagging" (kinematic, target-only LPF with no
//                   force feedback) and "pushback" (algebraic axis offset),
//                   "plant" integrates a real stick-slip Coulomb-friction
//                   plant driven by NET FORCE = servo_force + driver_force:
//                     - servo_force is the FULL ComputeSteerServoForce()
//                       RETURN VALUE (u = u_fb + u_ff, clamped) -- NOT the
//                       out_feedback-only value used for last_sample_. Using
//                       the feedback-only value would systematically under-
//                       drive the plant (missing the friction feed-forward
//                       term, spec §2.2/§2.5).
//                     - driver_force is read live from the SAME UDP listener
//                       "pushback" mode uses (GT_HEADLESS_FFB_PUSHBACK_PORT),
//                       reinterpreted as a force injection rather than an
//                       axis offset. A Python test harness closes the loop
//                       (reads telemetry, computes next-frame driver_force)
//                       to construct any of the 4 detection quadrants or the
//                       5 false-positive conditions -- see spec §3.3/§3.4.
//                   Below breakaway force the plant does not move AT ALL
//                   (exact deadzone, no creep) -- this is the one behavior
//                   no other mode can produce, and is the reason this mode
//                   exists (spec §0).
//   GT_HEADLESS_FFB_PLANT_BREAKAWAY : "<double>" (default 0.19 -- spec §1.1,
//                                CHARACTERIZATION.md §2, real-G29 measured
//                                0.170-0.210 average) static-friction deadzone.
//   GT_HEADLESS_FFB_PLANT_KINETIC   : "<double>" (default 0.16 -- spec §1.2/
//                                1.3, CHARACTERIZATION.md §3) kinetic-friction
//                                floor once moving.
//   GT_HEADLESS_FFB_PLANT_SLOPE     : "<double>" (default 3.35 --
//                                CHARACTERIZATION.md §3b) force->velocity slope.
//   GT_HEADLESS_FFB_PLANT_VMAX      : "<double>" (default 1.0 -- spec §1.3,
//                                CHARACTERIZATION.md §3b) velocity saturation.
//
// INDEPENDENCE REQUIREMENT (do not refactor this away). This plant and
// OverrideManager's shadow model describe the same physical device, but they
// MUST NOT share code, constants, or a common helper. If they did, "hands off
// produces zero residual" would be a tautology about one shared function
// rather than evidence about the wheel: the detector would be validated
// against its own assumptions and every headless non-firing result would be
// worth nothing. Both derive their parameters independently from
// scripts/ffb_spike/CHARACTERIZATION.md, and the parity sweep deliberately
// varies this plant's breakaway and slope away from the shadow's so no run is
// a fixed point of both models at once.
//   GT_HEADLESS_FFB_PLANT_NOISE_AMP : "<double>" (default 0.0 = disabled --
//                                spec §1.4/FL6) uniform position noise
//                                amplitude added once per Update() call.
//                                Real-measured reference values: 0.001 (SDL2
//                                raw quantization floor, OverrideManager.cpp
//                                sign_opposition epsilon comment) or 0.005
//                                (mechanical jitter, ManualDriveConfig.hpp
//                                override_position_error_rate_gate comment).
//   GT_HEADLESS_FFB_PLANT_SEED      : "<uint>" (default 12345) RNG seed for
//                                the noise generator -- deterministic by
//                                default so plant-mode tests are reproducible.
//
// pedal_steer.steering carries the same synthetic axis in every mode, so the
// OverrideManager direct-axis path sees the same value the SDL2 wheel path
// would — which is the whole point of headless closed-loop testing.
class HeadlessFfbInput : public IInputSource
{
public:
    HeadlessFfbInput();
    ~HeadlessFfbInput() override;

    bool Init(const ManualDriveConfig& config) override;
    InputFrame Poll(double dt) override;
    void Shutdown() override;
    bool IsConnected() const override { return true; }
    IFFBSink* GetFFBSink() override;

private:
    class SyntheticSink;
    std::unique_ptr<SyntheticSink> sink_;
    ITransport* pushback_transport_ = nullptr;  // only opened for mode=="pushback" or "plant"
};

} // namespace gt_esmini
