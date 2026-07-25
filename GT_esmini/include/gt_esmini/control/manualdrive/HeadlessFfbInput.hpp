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
//   GT_HEADLESS_FFB_MODE       : "follower" (default) | "frozen"
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
//   GT_HEADLESS_FFB_FROZEN_AT  : "<double>" (default 0.0) — axis position
//                                to hold in "frozen" mode.
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
    ITransport* pushback_transport_ = nullptr;  // only opened for mode=="pushback"
};

} // namespace gt_esmini
