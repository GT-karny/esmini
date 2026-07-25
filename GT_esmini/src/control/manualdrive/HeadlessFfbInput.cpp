#include "gt_esmini/control/manualdrive/HeadlessFfbInput.hpp"

#include "gt_esmini/control/manualdrive/FfbTargetServo.hpp"
#include "gt_esmini/control/manualdrive/IFFBSink.hpp"
#include "gt_esmini/control/manualdrive/ManualDriveConfig.hpp"
#include "logger.hpp"

#include <cmath>
#include <cstdlib>
#include <string>

namespace gt_esmini
{

// Nested synthetic FFB sink. Mirrors the calibrated pieces of SDLFFBSink that
// the closed-loop path touches — SetSteerTarget storage, servo PID, sample
// export — WITHOUT the SDL2 haptic side. The physical wheel is replaced by a
// simple analytic model (follower or frozen) so the wiring on the VD side
// (SetSteerTarget → Update → GetInterventionSample → OverrideManager.Update
// → next-frame Poll returns axis → OverrideManager sees closed loop) can be
// exercised end-to-end without SDL2 hardware.
class HeadlessFfbInput::SyntheticSink : public IFFBSink
{
public:
    void Configure(const ManualDriveConfig& config)
    {
        target_track_enabled_       = config.ffb.target_track.enabled;
        servo_cfg_.kp               = config.ffb.target_track.kp;
        servo_cfg_.kd               = config.ffb.target_track.kd;
        servo_cfg_.max_force        = config.ffb.target_track.max_force;
        servo_cfg_.hard_stop_zone   = config.ffb.target_track.hard_stop_zone;
        ResetSteerServo(servo_state_);
        target_norm_        = 0.0;
        target_active_      = false;
        target_active_prev_ = false;
        last_sample_        = {};

        // Env-var-driven synthetic physical-wheel model (test-only knobs).
        const char* mode_env   = std::getenv("GT_HEADLESS_FFB_MODE");
        mode_ = mode_env ? std::string(mode_env) : "follower";

        const char* frozen_env = std::getenv("GT_HEADLESS_FFB_FROZEN_AT");
        frozen_at_ = 0.0;
        if (frozen_env)
        {
            try { frozen_at_ = std::stod(frozen_env); }
            catch (...) { frozen_at_ = 0.0; }
        }

        LOG_INFO("HeadlessFfbSink: mode={} frozen_at={:.3f} target_track_enabled={} "
                 "kp={:.2f} kd={:.2f} max_force={:.2f}",
                 mode_, frozen_at_, target_track_enabled_,
                 servo_cfg_.kp, servo_cfg_.kd, servo_cfg_.max_force);
    }

    // Current synthetic physical wheel axis fraction [-1, +1].
    // Called by SyntheticSink itself for the servo error AND by
    // HeadlessFfbInput::Poll to feed pedal_steer.steering — one source of
    // truth, so the closed loop is genuinely closed.
    double CurrentAxis() const
    {
        if (mode_ == "frozen") return frozen_at_;
        // "follower" default
        return target_norm_;
    }

    // --- IFFBSink ---
    void Update(const osi3::HostVehicleData& /*hvd*/, double dt) override
    {
        if (target_active_)
        {
            const double actual_norm = CurrentAxis();
            const double u = ComputeSteerServoForce(target_norm_, actual_norm, dt,
                                                    servo_state_, servo_cfg_);
            last_sample_.commanded_force = std::abs(u);
            last_sample_.position_error  = target_norm_ - actual_norm;
            last_sample_.target_norm     = target_norm_;
            last_sample_.active          = true;
        }
        else
        {
            last_sample_ = {};
        }
    }

    void SetEnabled(bool /*enabled*/) override {}

    void SetSteerTarget(double target_norm, bool active) override
    {
        target_norm_   = target_norm;
        target_active_ = active && target_track_enabled_;
        if (target_active_ && !target_active_prev_)
            ResetSteerServo(servo_state_);
        target_active_prev_ = target_active_;
        if (!target_active_)
            last_sample_ = {};
    }

    FfbInterventionSample GetInterventionSample() const override
    {
        return last_sample_;
    }

private:
    std::string           mode_                 = "follower";
    double                frozen_at_            = 0.0;
    bool                  target_track_enabled_ = false;
    SteerServoConfig      servo_cfg_            = {};
    SteerServoState       servo_state_          = {};
    double                target_norm_          = 0.0;
    bool                  target_active_        = false;
    bool                  target_active_prev_   = false;
    FfbInterventionSample last_sample_          = {};
};

HeadlessFfbInput::HeadlessFfbInput()  = default;
HeadlessFfbInput::~HeadlessFfbInput() = default;

bool HeadlessFfbInput::Init(const ManualDriveConfig& config)
{
    sink_ = std::make_unique<SyntheticSink>();
    sink_->Configure(config);
    return true;
}

InputFrame HeadlessFfbInput::Poll(double /*dt*/)
{
    InputFrame f;
    f.connected = true;
    PedalSteerCommand ps;
    // Same synthetic axis the sink exposes as its "physical wheel" — closes
    // the loop that OverrideManager sees in real SDL2 hardware.
    ps.steering = sink_ ? sink_->CurrentAxis() : 0.0;
    ps.throttle = 0.0;
    ps.brake    = 0.0;
    ps.buttons  = 0;
    f.pedal_steer = ps;
    return f;
}

void HeadlessFfbInput::Shutdown()
{
    sink_.reset();
}

IFFBSink* HeadlessFfbInput::GetFFBSink()
{
    return sink_.get();
}

} // namespace gt_esmini
