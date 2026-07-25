#include "gt_esmini/control/manualdrive/HeadlessFfbInput.hpp"

#include "gt_esmini/control/manualdrive/FfbTargetServo.hpp"
#include "gt_esmini/control/manualdrive/IFFBSink.hpp"
#include "gt_esmini/control/manualdrive/ITransport.hpp"
#include "gt_esmini/control/manualdrive/ManualDriveConfig.hpp"
#include "gt_esmini/control/manualdrive/UdpTransport.hpp"
#include "gt_esmini/control/common/VehicleCommand.hpp"
#include "logger.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>

namespace gt_esmini
{

// Wire format for the pushback listener — identical to NetworkInputBridge's
// PedalSteerCommand encoding (only the "steering" field is actually read
// here). Kept as a local duplicate rather than a shared include to avoid
// coupling this test-only class to NetworkInputBridge's internals.
static constexpr uint32_t MAGIC_PEDAL_STEER      = 0x50535443;  // "PSTC"
static constexpr size_t   PEDAL_STEER_WIRE_SIZE  = 44;

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
        servo_cfg_.friction_ff      = config.ffb.target_track.friction_ff;
        servo_cfg_.friction_ff_eps  = config.ffb.target_track.friction_ff_eps;
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
        const char* tau_env = std::getenv("GT_HEADLESS_FFB_LAG_TAU");
        lag_tau_ = 0.30;   // 300 ms default — spike §1e G29 step response
        if (tau_env)
        {
            try { lag_tau_ = std::stod(tau_env); }
            catch (...) { lag_tau_ = 0.30; }
        }
        lag_axis_ = 0.0;   // start at rest — models the real physical wheel
        pushback_norm_ = 0.0;

        LOG_INFO("HeadlessFfbSink: mode={} frozen_at={:.3f} lag_tau={:.3f}s "
                 "target_track_enabled={} kp={:.2f} kd={:.2f} max_force={:.2f}",
                 mode_, frozen_at_, lag_tau_, target_track_enabled_,
                 servo_cfg_.kp, servo_cfg_.kd, servo_cfg_.max_force);
    }

    const std::string& Mode() const { return mode_; }

    // "pushback" mode only: the driver-injected offset from target_norm_ for
    // THIS frame, read live by HeadlessFfbInput::Poll() from the UDP
    // listener (see class comment). 0.0 = not pushing = axis follows target.
    void SetPushback(double v) { pushback_norm_ = v; }

    // Current synthetic physical wheel axis fraction [-1, +1].
    // Called by SyntheticSink itself for the servo error AND by
    // HeadlessFfbInput::Poll to feed pedal_steer.steering — one source of
    // truth, so the closed loop is genuinely closed.
    // "lagging" mode uses a 1st-order low-pass to model wheel inertia:
    // during a target step the servo commands force, physical wheel takes
    // ~lag_tau seconds to catch up — position_error decays as the wheel
    // catches up. This is precisely the startup transient that surfaced
    // the derror-rate-gate bug on real G29 (commit 549e5823 → follow-up).
    double CurrentAxis() const
    {
        if (mode_ == "frozen")   return frozen_at_;
        if (mode_ == "lagging")  return lag_axis_;
        if (mode_ == "pushback") return std::clamp(target_norm_ + pushback_norm_, -1.0, 1.0);
        // "follower" default: perfect (no lag)
        return target_norm_;
    }

    // Advance the lagging axis model by dt seconds toward target_norm.
    // No-op unless mode == "lagging". Called from Update BEFORE the servo
    // computation so this frame's ComputeSteerServoForce sees the freshly-
    // advanced axis (matching the "physical wheel moved between polls" flow
    // of the real hardware).
    void AdvanceLag(double dt)
    {
        if (mode_ != "lagging" || dt <= 0.0) return;
        // Standard 1st-order LPF (impulse-invariant discretisation).
        const double alpha = 1.0 - std::exp(-dt / std::max(lag_tau_, 1e-3));
        lag_axis_ += alpha * (target_norm_ - lag_axis_);
    }

    // --- IFFBSink ---
    void Update(const osi3::HostVehicleData& /*hvd*/, double dt) override
    {
        // Advance the physical-wheel model FIRST (models "servo commanded
        // force on frame N-1 → wheel moved between then and now").
        AdvanceLag(dt);

        if (target_active_)
        {
            const double actual_norm = CurrentAxis();
            double u_feedback = 0.0;
            ComputeSteerServoForce(target_norm_, actual_norm, dt,
                                   servo_state_, servo_cfg_, &u_feedback);
            // Feedback-only, exactly as SDLFFBSink reports it — the headless
            // closed-loop tests are only meaningful if the detector sees the
            // same signal the real SDL2 path feeds it.
            last_sample_.commanded_force        = std::abs(u_feedback);
            last_sample_.commanded_force_signed = u_feedback;
            last_sample_.position_error         = target_norm_ - actual_norm;
            last_sample_.target_norm            = target_norm_;
            last_sample_.active                 = true;
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
    double                lag_tau_              = 0.30;
    double                lag_axis_             = 0.0;
    double                pushback_norm_        = 0.0;
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

    // "pushback" mode only: open the live pushback listener (see
    // HeadlessFfbInput.hpp class comment). No-op for every other mode.
    if (sink_->Mode() == "pushback")
    {
        int port = 9105;
        if (const char* port_env = std::getenv("GT_HEADLESS_FFB_PUSHBACK_PORT"))
        {
            try { port = std::stoi(port_env); }
            catch (...) { port = 9105; }
        }
        auto* udp = new UdpTransport();
        TransportConfig tc;
        tc.type        = "udp";
        tc.listen_port = port;
        tc.is_server   = true;
        if (udp->Open(tc))
        {
            pushback_transport_ = udp;
            LOG_INFO("HeadlessFfbSink: pushback listener on UDP port {}", port);
        }
        else
        {
            LOG_ERROR("HeadlessFfbSink: failed to open pushback listener on UDP port {}", port);
            delete udp;
        }
    }
    return true;
}

InputFrame HeadlessFfbInput::Poll(double /*dt*/)
{
    // "pushback" mode only: drain the listener, keep the latest packet's
    // "steering" field as this frame's pushback offset (hold-last-value, same
    // pattern as NetworkInputBridge — see class comment). Must run BEFORE
    // CurrentAxis() below so the freshest value is used this Step (Poll()
    // runs before ControllerVirtualDriver::Step's SetSteerTarget/Update —
    // see gt_esmini::ControllerVirtualDriver::Step).
    if (pushback_transport_ && sink_)
    {
        char buf[64];
        double latest_pushback = 0.0;
        bool   got_new = false;
        while (true)
        {
            int received = pushback_transport_->Recv(buf, sizeof(buf));
            if (received <= 0) break;
            if (static_cast<size_t>(received) < PEDAL_STEER_WIRE_SIZE) continue;
            uint32_t magic = 0;
            std::memcpy(&magic, buf, 4);
            if (magic != MAGIC_PEDAL_STEER) continue;
            std::memcpy(&latest_pushback, buf + 4, 8);  // "steering" field
            got_new = true;
        }
        if (got_new) sink_->SetPushback(latest_pushback);
    }

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
    if (pushback_transport_)
    {
        pushback_transport_->Close();
        delete pushback_transport_;
        pushback_transport_ = nullptr;
    }
}

IFFBSink* HeadlessFfbInput::GetFFBSink()
{
    return sink_.get();
}

} // namespace gt_esmini
