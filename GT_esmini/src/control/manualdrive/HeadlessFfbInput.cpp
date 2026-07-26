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
#include <deque>
#include <random>
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

        // "plant" mode (task:F7 force-coupled plant, spec
        // test_results/f7_force_coupled_plant_spec.md). Real-measured
        // defaults (CHARACTERIZATION.md); see HeadlessFfbInput.hpp for the
        // per-var citations. No-op unless mode_=="plant".
        plant_position_ = 0.0;
        plant_velocity_ = 0.0;
        plant_moving_   = false;
        driver_force_norm_ = 0.0;

        plant_breakaway_ = 0.19;
        if (const char* v = std::getenv("GT_HEADLESS_FFB_PLANT_BREAKAWAY"))
        {
            try { plant_breakaway_ = std::stod(v); } catch (...) { plant_breakaway_ = 0.19; }
        }
        plant_kinetic_ = 0.16;
        if (const char* v = std::getenv("GT_HEADLESS_FFB_PLANT_KINETIC"))
        {
            try { plant_kinetic_ = std::stod(v); } catch (...) { plant_kinetic_ = 0.16; }
        }
        // 過渡（2026-07-26 実機同定: theta 中央値 0.0408s / tau 中央値 0.0179s）。
        // シャドウ側は公称値を使うが、こちらは **範囲で振る** のが役割である。
        // 同じ値を両側に入れると一致は構成上の必然になり何も証明しない
        // （INDEPENDENCE REQUIREMENT）。合成プラントの仕事は「シャドウと一致すること」
        // ではなく「シャドウが現実のばらつきに対して頑健であることを試すこと」。
        plant_dead_time_ = 0.0;
        if (const char* v = std::getenv("GT_HEADLESS_FFB_PLANT_DEAD_TIME"))
        {
            try { plant_dead_time_ = std::stod(v); } catch (...) { plant_dead_time_ = 0.0; }
        }
        plant_velocity_tau_ = 0.0;
        if (const char* v = std::getenv("GT_HEADLESS_FFB_PLANT_VELOCITY_TAU"))
        {
            try { plant_velocity_tau_ = std::stod(v); } catch (...) { plant_velocity_tau_ = 0.0; }
        }
        plant_force_history_.clear();

        plant_slope_ = 3.35;
        if (const char* v = std::getenv("GT_HEADLESS_FFB_PLANT_SLOPE"))
        {
            try { plant_slope_ = std::stod(v); } catch (...) { plant_slope_ = 3.35; }
        }
        plant_vmax_ = 1.0;
        if (const char* v = std::getenv("GT_HEADLESS_FFB_PLANT_VMAX"))
        {
            try { plant_vmax_ = std::stod(v); } catch (...) { plant_vmax_ = 1.0; }
        }
        plant_noise_amp_ = 0.0;   // default OFF
        if (const char* v = std::getenv("GT_HEADLESS_FFB_PLANT_NOISE_AMP"))
        {
            try { plant_noise_amp_ = std::stod(v); } catch (...) { plant_noise_amp_ = 0.0; }
        }
        unsigned plant_seed = 12345u;
        if (const char* v = std::getenv("GT_HEADLESS_FFB_PLANT_SEED"))
        {
            try { plant_seed = static_cast<unsigned>(std::stoul(v)); } catch (...) { plant_seed = 12345u; }
        }
        plant_rng_.seed(plant_seed);   // deterministic by default (fixed seed)

        LOG_INFO("HeadlessFfbSink: mode={} frozen_at={:.3f} lag_tau={:.3f}s "
                 "target_track_enabled={} kp={:.2f} kd={:.2f} max_force={:.2f} "
                 "plant_breakaway={:.3f} plant_kinetic={:.3f} plant_slope={:.3f} plant_vmax={:.3f} "
                 "plant_noise_amp={:.4f} plant_seed={}",
                 mode_, frozen_at_, lag_tau_, target_track_enabled_,
                 servo_cfg_.kp, servo_cfg_.kd, servo_cfg_.max_force,
                 plant_breakaway_, plant_kinetic_, plant_slope_, plant_vmax_, plant_noise_amp_, plant_seed);
    }

    const std::string& Mode() const { return mode_; }

    // "pushback" mode only: the driver-injected offset from target_norm_ for
    // THIS frame, read live by HeadlessFfbInput::Poll() from the UDP
    // listener (see class comment). 0.0 = not pushing = axis follows target.
    void SetPushback(double v) { pushback_norm_ = v; }

    // "plant" mode only: the driver-injected FORCE for this frame (same
    // physical units AND sign convention as servo_force -- normalized
    // axis-fraction, POSITIVE = pushing the wheel LEFT = toward NEGATIVE
    // axis, see FfbTargetServo.hpp), read live from the SAME UDP listener
    // "pushback" reuses (see HeadlessFfbInput::Poll). 0.0 = hands off.
    // Unlike "pushback"'s axis-offset formula, this is a genuine force that
    // gets summed with servo_force and passed through stick-slip friction --
    // see AdvancePlant().
    void SetDriverForce(double v) { driver_force_norm_ = v; }

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
        if (mode_ == "plant")    return plant_position_;
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

    // "plant" mode only: stick-slip Coulomb-friction integration driven by
    // NET FORCE = servo_force (passed in, already computed once by the
    // caller from the FULL ComputeSteerServoForce return value -- see
    // Update() below and spec §2.2/§2.5) + driver_force_norm_ (live-injected,
    // §3). No-op unless mode_=="plant" (mirrors AdvanceLag's guard).
    //
    // State machine (spec §2.3, CHARACTERIZATION.md §2/§3):
    //   at rest, |net_force| < breakaway  -> stays EXACTLY at rest (no
    //     creep, no noise floor -- §2 "0.16 以下では変位が厳密に 0.0000").
    //   at rest, |net_force| >= breakaway -> starts moving this frame.
    //   moving -> kinetic friction opposes net_force; once the force can no
    //     longer overcome kinetic friction, stops and returns to the static
    //     (at-rest) regime next frame (hysteresis: static threshold is
    //     "breakaway", kinetic floor is "kinetic", kinetic < breakaway by
    //     construction of the default values, §1.2's measured 0.02-0.03 band).
    //   moving, force>kinetic -> speed = min(3.35*(|f|-kinetic), v_max)
    //     (§1.3's real-G29 force->velocity regression, hard-capped at v_max).
    //
    // SIGN (CHARACTERIZATION.md §8c-4, FfbTargetServo.cpp:24-26): a POSITIVE
    // force pushes the wheel LEFT, which is the NEGATIVE axis direction. The
    // axis velocity is therefore -sign(net_force) * speed, not +. Getting this
    // backwards makes the plant DIVERGE from the target under a converging
    // servo (the servo returns u = -(kp*err+...), so err>0 gives u<0, which
    // must move the axis UP toward the target).
    void AdvancePlant(double dt, double servo_force)
    {
        if (mode_ != "plant" || dt <= 0.0) return;

        // --- 輸送遅れ theta: theta 秒前の力で駆動する ---
        // 実機同定でステップ応答 0.0408s / 反転遅れ 0.0445s と独立2系統が一致した。
        // 遅れの正体は輸送遅れであって一次遅れではない。
        plant_force_history_.push_back({servo_force + driver_force_norm_, dt});
        double net_force = plant_force_history_.back().force;
        if (plant_dead_time_ > 0.0)
        {
            double age = 0.0;
            for (auto it = plant_force_history_.rbegin(); it != plant_force_history_.rend(); ++it)
            {
                net_force = it->force;
                age += it->dt;
                if (age >= plant_dead_time_) break;
            }
        }
        {
            double total = 0.0;
            for (const auto& e : plant_force_history_) total += e.dt;
            while (plant_force_history_.size() > 1 &&
                   total - plant_force_history_.front().dt > plant_dead_time_ + 0.5)
            {
                total -= plant_force_history_.front().dt;
                plant_force_history_.pop_front();
            }
        }

        if (!plant_moving_)
        {
            if (std::abs(net_force) < plant_breakaway_)
            {
                plant_velocity_ = 0.0;
            }
            else
            {
                plant_moving_ = true;   // breaks static friction this frame
            }
        }
        if (plant_moving_)
        {
            const double effective = std::abs(net_force) - plant_kinetic_;
            if (effective <= 0.0)
            {
                // Kinetic friction has fully absorbed the driving force —
                // stops now; next frame re-enters the static/breakaway test.
                plant_velocity_ = 0.0;
                plant_moving_   = false;
            }
            else
            {
                const double speed = std::min(plant_slope_ * effective, plant_vmax_);
                // -sign(net_force): positive force = wheel left = axis negative.
                const double v_target = (net_force >= 0.0) ? -speed : speed;
                // 慣性（一次遅れ）。実機同定 tau 中央値 0.0179s。0 = 無効（従来挙動）。
                if (plant_velocity_tau_ > 1e-9)
                {
                    const double alpha = 1.0 - std::exp(-dt / plant_velocity_tau_);
                    plant_velocity_ += alpha * (v_target - plant_velocity_);
                }
                else
                {
                    plant_velocity_ = v_target;
                }
            }
        }

        plant_position_ += plant_velocity_ * dt;

        if (plant_noise_amp_ > 0.0)
        {
            std::uniform_real_distribution<double> dist(-plant_noise_amp_, plant_noise_amp_);
            plant_position_ += dist(plant_rng_);
        }
        plant_position_ = std::clamp(plant_position_, -1.0, 1.0);
    }

    // --- IFFBSink ---
    void Update(const osi3::HostVehicleData& /*hvd*/, double dt) override
    {
        // Advance the physical-wheel model FIRST for the target-only-driven
        // modes (models "servo commanded force on frame N-1 → wheel moved
        // between then and now"). No-op for "plant" (handled below instead:
        // its motion needs THIS frame's servo force as an input, computed
        // once from the pre-motion position read here).
        AdvanceLag(dt);

        // Snapshot the axis BEFORE any "plant" motion this frame — for every
        // other mode this is already the frame's final value (AdvanceLag
        // above already updated it, and frozen/follower/pushback derive
        // purely from target_norm_/frozen_at_/pushback_norm_, none of which
        // AdvancePlant touches), so re-reading CurrentAxis() again after
        // AdvancePlant (below) is provably a no-op for those 4 modes.
        const double actual_norm_pre = CurrentAxis();

        double u = 0.0, u_feedback = 0.0;
        if (target_active_)
        {
            // Capture the FULL return value u (=u_fb+u_ff, clamped) — NOT
            // just out_feedback — so "plant" mode's physics see the same
            // force a real SDL2 device would receive (spec §2.2/§2.5:
            // out_feedback alone systematically under-drives the plant by
            // omitting the Coulomb friction feed-forward term).
            u = ComputeSteerServoForce(target_norm_, actual_norm_pre, dt,
                                       servo_state_, servo_cfg_, &u_feedback);
        }

        // "plant" mode only (no-op otherwise, same guard pattern as
        // AdvanceLag): integrate this frame's motion using the just-computed
        // full servo force. Called even when !target_active_ (u=0.0 in that
        // case) so a live driver_force_norm_ can still move the plant while
        // the servo is inactive (e.g. already MANUAL) — a real physical
        // wheel does not freeze just because the servo stopped commanding.
        AdvancePlant(dt, u);

        if (target_active_)
        {
            const double actual_norm = CurrentAxis();   // post-motion for "plant"; identical to actual_norm_pre for all other modes
            // Feedback-only, exactly as SDLFFBSink reports it — the headless
            // closed-loop tests are only meaningful if the detector sees the
            // same signal the real SDL2 path feeds it.
            last_sample_.commanded_force        = std::abs(u_feedback);
            // The EFFECTIVE force: headless has no sat/friction/damping/
            // soft_stop terms, so the full servo return value u IS what a real
            // device would receive. Mirrors SDLFFBSink's post-clamp `total`.
            // Feeding u_feedback here instead would under-drive the residual
            // detector's shadow plant by friction_ff (0.15) against a 0.19
            // breakaway — see IFFBSink.hpp effective_force_signed.
            last_sample_.effective_force_signed = u;
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

    // "plant" mode state (task:F7 force-coupled plant, spec
    // test_results/f7_force_coupled_plant_spec.md). All default-initialized
    // here purely as a fallback; Configure() always re-initializes them —
    // see Configure() for the authoritative reset-on-(re)configure values.
    double                plant_position_       = 0.0;
    double                plant_velocity_       = 0.0;
    bool                  plant_moving_         = false;   // stick-slip: static vs kinetic friction regime
    double                driver_force_norm_    = 0.0;     // live-injected driver force (§3)
    double                plant_breakaway_      = 0.19;
    double                plant_kinetic_        = 0.16;
    double                plant_dead_time_      = 0.0;    // 実機同定の範囲で振る
    double                plant_velocity_tau_   = 0.0;
    struct PlantForce { double force; double dt; };
    std::deque<PlantForce> plant_force_history_;
    double                plant_slope_          = 3.35;
    double                plant_vmax_           = 1.0;
    double                plant_noise_amp_      = 0.0;      // default OFF
    std::mt19937          plant_rng_;
};

HeadlessFfbInput::HeadlessFfbInput()  = default;
HeadlessFfbInput::~HeadlessFfbInput() = default;

bool HeadlessFfbInput::Init(const ManualDriveConfig& config)
{
    sink_ = std::make_unique<SyntheticSink>();
    sink_->Configure(config);

    // "pushback" and "plant" modes only: open the live injection listener
    // (see HeadlessFfbInput.hpp class comment) — "plant" reuses the exact
    // same UDP wire format/port, reinterpreting the "steering" field as a
    // driver FORCE instead of an axis offset (see Poll() below). No-op for
    // every other mode.
    if (sink_->Mode() == "pushback" || sink_->Mode() == "plant")
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
    // "pushback"/"plant" modes only: drain the listener, keep the latest
    // packet's "steering" field as this frame's injected value (hold-last-
    // value, same pattern as NetworkInputBridge — see class comment). Must
    // run BEFORE CurrentAxis() below so the freshest value is used this Step
    // (Poll() runs before ControllerVirtualDriver::Step's SetSteerTarget/
    // Update — see gt_esmini::ControllerVirtualDriver::Step). Routed to
    // SetPushback (axis offset) or SetDriverForce (force injection)
    // depending on which mode is active — same wire value, different
    // physical meaning per mode (see HeadlessFfbInput.hpp class comment).
    if (pushback_transport_ && sink_)
    {
        char buf[64];
        double latest_value = 0.0;
        bool   got_new = false;
        while (true)
        {
            int received = pushback_transport_->Recv(buf, sizeof(buf));
            if (received <= 0) break;
            if (static_cast<size_t>(received) < PEDAL_STEER_WIRE_SIZE) continue;
            uint32_t magic = 0;
            std::memcpy(&magic, buf, 4);
            if (magic != MAGIC_PEDAL_STEER) continue;
            std::memcpy(&latest_value, buf + 4, 8);  // "steering" field
            got_new = true;
        }
        if (got_new)
        {
            if (sink_->Mode() == "plant") sink_->SetDriverForce(latest_value);
            else                          sink_->SetPushback(latest_value);
        }
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
