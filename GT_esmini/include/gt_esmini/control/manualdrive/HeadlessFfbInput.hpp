#pragma once

#include "gt_esmini/control/manualdrive/IInputSource.hpp"

#include <memory>

namespace gt_esmini
{

class IFFBSink;
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
};

} // namespace gt_esmini
