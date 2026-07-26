#ifdef GT_ENABLE_SDL2

#include "gt_esmini/control/manualdrive/SDLFFBSink.hpp"
#include "gt_esmini/control/manualdrive/ManualDriveConfig.hpp"
#include "CommonMini.hpp"
#include "logger.hpp"

#include <algorithm>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <mutex>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

namespace gt_esmini
{

SDLFFBSink::SDLFFBSink() = default;

SDLFFBSink::~SDLFFBSink()
{
    Close();
}

bool SDLFFBSink::Init(SDL_Joystick* joystick, const ManualDriveConfig& config)
{
    if (!joystick)
    {
        return false;
    }
    joystick_ = joystick;   // NOT owned — needed to read physical wheel angle in target-track servo

    sat_gain_            = config.ffb.sat_gain;
    sat_centering_gain_  = config.ffb.sat_centering_gain;
    friction_base_       = config.ffb.friction_base;
    friction_speed_gain_ = config.ffb.friction_speed_gain;
    damper_base_         = config.ffb.damper_base;
    damper_speed_gain_   = config.ffb.damper_speed_gain;
    soft_stop_gain_      = config.ffb.soft_stop_gain;
    lock_angle_          = config.ffb.lock_angle;
    assist_low_speed_    = config.ffb.assist_low_speed;
    assist_high_speed_   = config.ffb.assist_high_speed;
    max_force_           = config.ffb.max_force;

    // feature:F7 (F7b) target-tracking config.
    target_track_enabled_        = config.ffb.target_track.enabled;
    servo_cfg_.kp                = config.ffb.target_track.kp;
    servo_cfg_.kd                = config.ffb.target_track.kd;
    servo_cfg_.max_force         = config.ffb.target_track.max_force;
    servo_cfg_.hard_stop_zone    = config.ffb.target_track.hard_stop_zone;
    servo_cfg_.friction_ff       = config.ffb.target_track.friction_ff;
    servo_cfg_.friction_ff_eps   = config.ffb.target_track.friction_ff_eps;
    feel_ratio_                  = config.ffb.target_track.feel_ratio;
    // feature:F7 unattended-run safety watchdog (both 0 = disabled by default,
    // so supervised behaviour is unchanged). See SDLFFBSink.hpp.
    safety_max_saturation_s_ = config.ffb.safety.max_saturation_seconds;
    safety_max_runtime_s_    = config.ffb.safety.max_runtime_seconds;
    safety_saturation_ratio_ = config.ffb.safety.saturation_ratio;
    safety_saturation_accum_ = 0.0;
    safety_runtime_accum_    = 0.0;
    safety_tripped_          = false;
    ResetSteerServo(servo_state_);
    target_norm_        = 0.0;
    target_active_      = false;
    target_active_prev_ = false;
    last_sample_        = {};

    LOG_INFO("SDLFFBSink: Config loaded — sat_gain={:.3f} centering={:.3f} fric_base={:.3f} fric_spd={:.3f} "
             "damp_base={:.3f} damp_spd={:.3f} assist_lo={:.2f} assist_hi={:.2f} max_force={:.2f}",
             sat_gain_, sat_centering_gain_, friction_base_, friction_speed_gain_,
             damper_base_, damper_speed_gain_, assist_low_speed_, assist_high_speed_, max_force_);
    LOG_INFO("SDLFFBSink: target_track enabled={} kp={:.2f} kd={:.2f} max_force={:.2f} hard_stop_zone={:.2f} "
             "friction_ff={:.3f} (eps={:.3f}) feel_ratio={:.2f}",
             target_track_enabled_, servo_cfg_.kp, servo_cfg_.kd,
             servo_cfg_.max_force, servo_cfg_.hard_stop_zone,
             servo_cfg_.friction_ff, servo_cfg_.friction_ff_eps, feel_ratio_);

    if (!SDL_JoystickIsHaptic(joystick))
    {
        LOG_INFO("SDLFFBSink: Joystick does not support haptic feedback");
        return false;
    }

    haptic_ = SDL_HapticOpenFromJoystick(joystick);
    if (!haptic_)
    {
        LOG_WARN("SDLFFBSink: Failed to open haptic: {}", SDL_GetError());
        return false;
    }

    // Register for emergency release BEFORE any effect is created, so even a
    // crash during effect setup leaves the device silenced.
    RegisterEmergencyRelease(this);
    {
        // Log the ABSOLUTE trip level, not just the ratio. A ratio alone hides
        // the defect this line exists to make impossible: a trip level above
        // the force the servo can actually produce, i.e. a watchdog that can
        // never fire. The runbook's abort check reads this line.
        const double reachable_cap = ReachableForceCap();
        const double sat_level = safety_saturation_ratio_ * reachable_cap;
        LOG_INFO("SDLFFBSink: safety watchdog max_saturation={:.1f}s max_runtime={:.1f}s "
                 "(0 = disabled) saturation_ratio={:.2f} reachable_cap={:.2f} "
                 "-> trips at |force| >= {:.3f}",
                 safety_max_saturation_s_, safety_max_runtime_s_,
                 safety_saturation_ratio_, reachable_cap, sat_level);
        if (safety_max_saturation_s_ > 0.0 && sat_level > reachable_cap)
        {
            LOG_WARN("SDLFFBSink: SAFETY MISCONFIGURED — saturation trip {:.3f} exceeds the "
                     "reachable cap {:.3f}; this watchdog can never fire",
                     sat_level, reachable_cap);
        }
    }

    // spike §1e: script 04 (constant-force PID servo, the calibration source for
    // Kp/Kd) explicitly set gain to 100 (max). Without this call, SDL uses a
    // driver-dependent default that may attenuate CONSTANT-force level below
    // what the servo Kp expects, leaving the wheel too weak to overcome
    // SAT/friction. Set once at Init so downstream force commands land at the
    // gain the spike measured against.
    SDL_HapticSetGain(haptic_, 100);

    // Query device capabilities
    unsigned int caps = SDL_HapticQuery(haptic_);
    has_constant_ = (caps & SDL_HAPTIC_CONSTANT) != 0;
    has_spring_   = (caps & SDL_HAPTIC_SPRING) != 0;
    has_damper_   = (caps & SDL_HAPTIC_DAMPER) != 0;

    LOG_INFO("SDLFFBSink: Haptic opened — constant={}, spring={}, damper={}",
             has_constant_, has_spring_, has_damper_);

    // Create constant force effect (self-aligning torque)
    if (has_constant_)
    {
        SDL_HapticEffect effect = {};
        effect.type = SDL_HAPTIC_CONSTANT;
        effect.constant.direction.type = SDL_HAPTIC_CARTESIAN;
        effect.constant.direction.dir[0] = 1;  // X-axis (steering)
        effect.constant.length = SDL_HAPTIC_INFINITY;
        effect.constant.level = 0;
        effect.constant.attack_length = 0;
        effect.constant.fade_length = 0;
        constant_effect_id_ = SDL_HapticNewEffect(haptic_, &effect);
        if (constant_effect_id_ >= 0)
        {
            SDL_HapticRunEffect(haptic_, constant_effect_id_, 1);
        }
        else
        {
            LOG_WARN("SDLFFBSink: Failed to create constant effect: {}", SDL_GetError());
        }
    }

    // When target-tracking is on we already know the combined-constant path is
    // the only force channel (see the emulate_via_constant_ decision below), so
    // do not create SPRING/DAMPER at all.
    //
    // Leaving them merely *running* is not free on a G29: with the CARTESIAN
    // direction fix these effects now succeed in being created, and DirectInput
    // then mixes three live effects and shares the device's force budget between
    // them. Measured on the rig: with them running, a commanded CONSTANT of
    // 0.269 held for 5 s did not move the wheel at all, even though a bare 0.22
    // constant force turns it at ~0.2 axis-frac/s (script 07). The servo tracked
    // only 77% of a curve and 53% of a right turn; with them not created it
    // reaches the values the characterization predicted.
    const bool constant_only = target_track_enabled_ && has_constant_ && constant_effect_id_ >= 0;
    if (constant_only)
    {
        has_spring_ = false;
        has_damper_ = false;
        LOG_INFO("SDLFFBSink: target_track on — skipping SPRING/DAMPER creation "
                 "(CONSTANT is the sole force channel; concurrent effects measurably "
                 "attenuate it on G29)");
    }

    // Create spring effect (centering)
    if (has_spring_)
    {
        SDL_HapticEffect effect = {};
        effect.type = SDL_HAPTIC_SPRING;
        // spike §1b / §3b: G29 rejects condition-effect creation with
        // "Unable to create effect" unless direction.type is CARTESIAN and
        // direction.dir[0]=1, even though SDL docs treat direction as ignored
        // for condition effects. Missing this silently drops us to constant-
        // force emulation, which currently masks the bug but is not the
        // intended behaviour on hardware that DOES support SPRING natively.
        effect.condition.direction.type   = SDL_HAPTIC_CARTESIAN;
        effect.condition.direction.dir[0] = 1;
        effect.condition.length = SDL_HAPTIC_INFINITY;
        effect.condition.right_coeff[0] = 0;
        effect.condition.left_coeff[0] = 0;
        effect.condition.right_sat[0] = 0x7FFF;
        effect.condition.left_sat[0] = 0x7FFF;
        spring_effect_id_ = SDL_HapticNewEffect(haptic_, &effect);
        if (spring_effect_id_ < 0)
        {
            LOG_WARN("SDLFFBSink: Spring effect unsupported: {}", SDL_GetError());
            has_spring_ = false;
        }
        else
        {
            SDL_HapticRunEffect(haptic_, spring_effect_id_, 1);
        }
    }

    // Create damper effect (steering resistance)
    if (has_damper_)
    {
        SDL_HapticEffect effect = {};
        effect.type = SDL_HAPTIC_DAMPER;
        effect.condition.direction.type   = SDL_HAPTIC_CARTESIAN;   // spike §3b, see SPRING above
        effect.condition.direction.dir[0] = 1;
        effect.condition.length = SDL_HAPTIC_INFINITY;
        effect.condition.right_coeff[0] = 0;
        effect.condition.left_coeff[0] = 0;
        effect.condition.right_sat[0] = 0x7FFF;
        effect.condition.left_sat[0] = 0x7FFF;
        damper_effect_id_ = SDL_HapticNewEffect(haptic_, &effect);
        if (damper_effect_id_ < 0)
        {
            LOG_WARN("SDLFFBSink: Damper effect unsupported: {}", SDL_GetError());
            has_damper_ = false;
        }
        else
        {
            SDL_HapticRunEffect(haptic_, damper_effect_id_, 1);
        }
    }

    // If spring/damper failed but constant works, emulate via constant force.
    // Also force this path when target_track is enabled — the spike (§1c/§3a)
    // validated target-following on the CONSTANT-only combined-force channel;
    // running SPRING natively pulls the wheel toward axis=0 and would compete
    // with the servo's attempt to move it toward the AD-commanded target.
    // Keep the CONSTANT path as the single source of steering wheel force.
    if (has_constant_ && constant_effect_id_ >= 0 &&
        (!has_spring_ || !has_damper_ || target_track_enabled_))
    {
        emulate_via_constant_ = true;
        LOG_INFO("SDLFFBSink: Emulating spring/damper via constant force "
                 "(reason: {} target_track_enabled={} has_spring={} has_damper={})",
                 target_track_enabled_ ? "target_track pin (spike §1c)"
                                        : "spring/damper native creation failed",
                 target_track_enabled_, has_spring_, has_damper_);
    }

    return true;
}

void SDLFFBSink::Update(const osi3::HostVehicleData& hvd, double dt)
{
    if (!haptic_ || !enabled_)
    {
        return;
    }

    // Extract vehicle state from HVD
    double speed = 0.0;
    double lat_accel = 0.0;
    double steering_pos = 0.0;

    if (hvd.has_location())
    {
        const auto& loc = hvd.location();
        if (loc.has_velocity())
        {
            double vx = loc.velocity().x();
            double vy = loc.velocity().y();
            speed = std::sqrt(vx * vx + vy * vy);
        }
    }
    // Lateral acceleration for the FFB force must be BODY frame. The spec home
    // for that is vehicle_motion (G6 fix; RealVehicleBackend fills it). Legacy
    // producers that only fill location (external NetworkPhysicsBridge peers)
    // historically wrote body-frame values there, so the fallback keeps the old
    // interpretation for them.
    if (hvd.has_vehicle_motion() && hvd.vehicle_motion().has_acceleration())
    {
        lat_accel = hvd.vehicle_motion().acceleration().y();
    }
    else if (hvd.has_location() && hvd.location().has_acceleration())
    {
        lat_accel = hvd.location().acceleration().y();
    }
    if (hvd.has_vehicle_steering() && hvd.vehicle_steering().has_vehicle_steering_wheel())
    {
        steering_pos = hvd.vehicle_steering().vehicle_steering_wheel().angle();
    }

    if (emulate_via_constant_)
    {
        double steering_vel = (steering_pos - prev_steering_) / std::max(dt, 0.001);
        prev_steering_ = steering_pos;

        UpdateCombinedConstantForce(lat_accel, speed, steering_pos, steering_vel, dt);
        return;
    }

    // Native effects path — use SAT via constant, spring/damper as available
    double speed_factor = std::clamp(speed / 30.0, 0.0, 1.0);

    if (has_constant_ && constant_effect_id_ >= 0)
    {
        double assist_ratio = assist_low_speed_ + (assist_high_speed_ - assist_low_speed_) * speed_factor;
        // Same feel-authority rule as the combined path — kept in sync so the
        // two branches behave identically (see UpdateCombinedConstantForce §0).
        // Unreachable while target-tracking is on today (Init pins
        // emulate_via_constant_), but the branches must not diverge silently.
        double force = -lat_accel * sat_gain_ * (1.0 - assist_ratio) *
                       (target_active_ ? feel_ratio_ : 1.0);

        // feature:F7 (F7b) — target-track servo term rides on the CONSTANT
        // channel in the native path too. The SPRING/DAMPER CARTESIAN fix
        // above lets G29 create native effects successfully, which switches
        // this branch on; without adding the servo term here it would silently
        // stop working. Keep behavior aligned with the emulate_via_constant
        // branch (see UpdateCombinedConstantForce §5) so target-tracking is
        // identical regardless of which path is taken.
        if (target_active_)
        {
            const double actual_norm = ReadPhysicalWheelNorm();
            double u_feedback = 0.0;
            const double u = ComputeSteerServoForce(target_norm_, actual_norm, dt,
                                                    servo_state_, servo_cfg_, &u_feedback);
            force += u;
            last_sample_.commanded_force        = std::abs(u_feedback);  // see combined path
            last_sample_.position_error         = target_norm_ - actual_norm;
            last_sample_.target_norm            = target_norm_;
            last_sample_.active                 = true;
        }

        force = std::clamp(force, -max_force_, max_force_);
        UpdateConstantEffect(force);
    }

    if (has_spring_ && spring_effect_id_ >= 0)
    {
        double coeff = friction_base_ + friction_speed_gain_ * speed_factor;
        UpdateSpringEffect(coeff);
    }

    if (has_damper_ && damper_effect_id_ >= 0)
    {
        double coeff = damper_base_ + damper_speed_gain_ * speed_factor;
        UpdateDamperEffect(coeff);
    }
}

void SDLFFBSink::SetEnabled(bool enabled)
{
    enabled_ = enabled;
    if (!enabled && haptic_)
    {
        SDL_HapticStopAll(haptic_);
    }
}

void SDLFFBSink::SetSteerTarget(double target_norm, bool active)
{
    // feature:F7 (F7b) — AD hands the servo a fresh target every frame.
    // Master gate is target_track_enabled_ (config); the caller can still
    // pause per-frame via active=false without touching the config.
    target_norm_       = target_norm;
    // If the config disables target-tracking, force active=false regardless of caller intent.
    target_active_     = active && target_track_enabled_;
    // If the servo just transitioned OFF -> ON, re-prime the derivative so
    // the first D step is 0 (avoids a bogus initial spike from stale prev_err).
    if (target_active_ && !target_active_prev_)
    {
        ResetSteerServo(servo_state_);
    }
    target_active_prev_ = target_active_;
    // If the servo is OFF, expose an inert sample so OverrideManager never
    // latches on stale readings (matches FfbSampleInactiveNeverLatches).
    if (!target_active_)
    {
        last_sample_ = {};
    }
}

double SDLFFBSink::ReadPhysicalWheelNorm() const
{
    if (!joystick_) return 0.0;
    // Axis 0 is the steering axis; normalize to [-1, +1] exactly like
    // SDL2WheelInput::NormalizeAxis so target and actual live in one unit space.
    const int raw = SDL_JoystickGetAxis(joystick_, 0);
    return static_cast<double>(raw) / 32767.0;
}

// --- feature:F7 unattended-run safety -------------------------------------
//
// Emergency release. A CONSTANT effect on a G29 keeps pulling until something
// stops it; process death releases the DirectInput device, but a hang, an
// abort() or a Ctrl-C in between leaves the wheel loaded with nobody in the
// room. These hooks close that window.
namespace
{
std::vector<SDLFFBSink*>& LiveSinks()
{
    static std::vector<SDLFFBSink*> sinks;
    return sinks;
}
std::mutex& LiveSinksMutex()
{
    static std::mutex m;
    return m;
}
void ReleaseAllHaptics()
{
    // Deliberately minimal: stop effects, do not free, do not log, do not
    // throw. This runs from atexit and from signal handlers.
    for (SDLFFBSink* s : LiveSinks())
    {
        if (s) s->SilenceDevice();
    }
}
void SignalRelease(int sig)
{
    ReleaseAllHaptics();
    // Restore the default action and re-raise so the process still dies the
    // way it was going to — swallowing the signal would be worse than the
    // stuck force we are preventing.
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}
#ifdef _WIN32
BOOL WINAPI ConsoleRelease(DWORD)
{
    ReleaseAllHaptics();
    return FALSE;   // let the default handler continue terminating us
}

// STRUCTURED EXCEPTIONS — the hole signal(SIGSEGV) does not cover.
//
// On MSVC, an access violation or a stack overflow is delivered as a
// STRUCTURED exception. The CRT only synthesises SIGSEGV for a subset of
// cases, and a stack overflow in particular unwinds through the SEH
// machinery without the signal handler ever running. On a supervised run that
// just means a crash dialog; on an UNATTENDED run with a powered wheel it
// means the CONSTANT effect keeps pulling until somebody walks in. So the SEH
// path gets closed too, at both ends:
//
//   - a VECTORED handler (first chance, runs before any __except in any
//     frame, so a library that swallows the exception cannot hide it from us)
//   - SetUnhandledExceptionFilter (last chance, for anything the vectored
//     handler declined)
//
// Only unambiguously fatal codes are acted on. C++ exceptions (0xE06D7363)
// and breakpoints are normal control flow and are ignored — reacting to those
// would silence the wheel during ordinary operation. Both handlers ALWAYS
// pass the exception on (CONTINUE_SEARCH / chain to the previous filter), so
// the process still crashes exactly as it would have; the only change is that
// the device is quiet when it does.
//
// Residual risk, stated: calling into SDL from an exception handler is not
// formally safe, and a first-chance hit that some frame goes on to handle
// would leave FFB stopped for the rest of the run. Both are strictly better
// than a wheel left under load with nobody present — a stopped servo is a
// degraded run, a stuck force is a hazard.
bool IsFatalSehCode(DWORD code)
{
    switch (code)
    {
        case EXCEPTION_ACCESS_VIOLATION:
        case EXCEPTION_STACK_OVERFLOW:
        case EXCEPTION_ILLEGAL_INSTRUCTION:
        case EXCEPTION_PRIV_INSTRUCTION:
        case EXCEPTION_IN_PAGE_ERROR:
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:
        case EXCEPTION_NONCONTINUABLE_EXCEPTION:
            return true;
        default:
            return false;
    }
}

LONG CALLBACK VectoredRelease(EXCEPTION_POINTERS* info)
{
    if (info && info->ExceptionRecord && IsFatalSehCode(info->ExceptionRecord->ExceptionCode))
    {
        ReleaseAllHaptics();
    }
    return EXCEPTION_CONTINUE_SEARCH;   // never alter the outcome
}

LPTOP_LEVEL_EXCEPTION_FILTER g_prev_seh_filter = nullptr;

LONG WINAPI UnhandledRelease(EXCEPTION_POINTERS* info)
{
    ReleaseAllHaptics();
    if (g_prev_seh_filter) return g_prev_seh_filter(info);
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif
}  // namespace

void SDLFFBSink::SilenceDevice()
{
    if (haptic_)
    {
        SDL_HapticStopAll(haptic_);
    }
}

void SDLFFBSink::RegisterEmergencyRelease(SDLFFBSink* sink)
{
    std::lock_guard<std::mutex> lk(LiveSinksMutex());
    static bool hooks_installed = false;
    if (!hooks_installed)
    {
        std::atexit(&ReleaseAllHaptics);
        std::signal(SIGINT,  &SignalRelease);
        std::signal(SIGTERM, &SignalRelease);
        std::signal(SIGABRT, &SignalRelease);
        std::signal(SIGSEGV, &SignalRelease);
#ifdef _WIN32
        SetConsoleCtrlHandler(&ConsoleRelease, TRUE);
        // SEH — the path signal(SIGSEGV) misses on MSVC. See VectoredRelease.
        AddVectoredExceptionHandler(1 /*call first*/, &VectoredRelease);
        g_prev_seh_filter = SetUnhandledExceptionFilter(&UnhandledRelease);
#endif
        hooks_installed = true;
    }
    LiveSinks().push_back(sink);
}

void SDLFFBSink::UnregisterEmergencyRelease(SDLFFBSink* sink)
{
    std::lock_guard<std::mutex> lk(LiveSinksMutex());
    auto& v = LiveSinks();
    v.erase(std::remove(v.begin(), v.end(), sink), v.end());
}

double SDLFFBSink::ReachableForceCap() const
{
    return target_track_enabled_ ? std::min(max_force_, servo_cfg_.max_force)
                                 : max_force_;
}

void SDLFFBSink::UpdateSafetyWatchdog(double applied_force, double dt)
{
    if (safety_tripped_ || dt <= 0.0) return;

    if (safety_max_runtime_s_ > 0.0)
    {
        safety_runtime_accum_ += dt;
        if (safety_runtime_accum_ >= safety_max_runtime_s_)
        {
            safety_tripped_ = true;
            LOG_WARN("SDLFFBSink SAFETY: total runtime {:.1f}s reached the configured "
                     "limit {:.1f}s — force disabled for the rest of this process",
                     safety_runtime_accum_, safety_max_runtime_s_);
        }
    }

    if (safety_max_saturation_s_ > 0.0)
    {
        // SATURATION MUST BE MEASURED AGAINST A REACHABLE FORCE.
        //
        // The obvious reference, max_force_, is the CLAMP on the combined
        // output (ffb.max_force, shipped 1.0) — not the largest force this
        // sink can actually sustain. While the target-track servo owns the
        // channel (feel_ratio 0 suppresses sat/friction/damping) the only
        // continuous contributor is the servo, capped at
        // target_track.max_force = 0.6. Referencing max_force_ therefore puts
        // the trip at 0.95, which the servo can never reach: soft_stop would
        // have to add another 0.35 on top, i.e. the wheel would have to be
        // jammed against its lock — and that is S2's job, not this one.
        //
        // The result was a watchdog that could never fire in exactly the
        // situation it exists for ("the servo is pushing and the wheel is not
        // moving"). Reference the achievable cap instead, which also puts
        // this in agreement with the supervisor's independently-derived
        // 0.95 x 0.6 = 0.57.
        const double reachable_cap = ReachableForceCap();
        const double sat_level = safety_saturation_ratio_ * reachable_cap;
        if (std::abs(applied_force) >= sat_level)
        {
            safety_saturation_accum_ += dt;
            if (safety_saturation_accum_ >= safety_max_saturation_s_)
            {
                safety_tripped_ = true;
                LOG_WARN("SDLFFBSink SAFETY: |force| stayed >= {:.3f} for {:.1f}s "
                         "(limit {:.1f}s) — the servo is straining against something it "
                         "cannot move; force disabled for the rest of this process",
                         sat_level, safety_saturation_accum_, safety_max_saturation_s_);
            }
        }
        else
        {
            safety_saturation_accum_ = 0.0;   // must be CONTINUOUS to count
        }
    }

    if (safety_tripped_)
    {
        UpdateConstantEffect(0.0);
        SDL_HapticStopAll(haptic_);
    }
}

void SDLFFBSink::Close()
{
    UnregisterEmergencyRelease(this);
    if (haptic_)
    {
        SDL_HapticStopAll(haptic_);
        if (constant_effect_id_ >= 0)
        {
            SDL_HapticDestroyEffect(haptic_, constant_effect_id_);
            constant_effect_id_ = -1;
        }
        if (spring_effect_id_ >= 0)
        {
            SDL_HapticDestroyEffect(haptic_, spring_effect_id_);
            spring_effect_id_ = -1;
        }
        if (damper_effect_id_ >= 0)
        {
            SDL_HapticDestroyEffect(haptic_, damper_effect_id_);
            damper_effect_id_ = -1;
        }
        SDL_HapticClose(haptic_);
        haptic_ = nullptr;
    }
}

void SDLFFBSink::UpdateConstantEffect(double force)
{
    // force: -1.0 ~ 1.0
    Sint16 level = static_cast<Sint16>(std::clamp(force, -1.0, 1.0) * 32767.0);

    SDL_HapticEffect effect = {};
    effect.type = SDL_HAPTIC_CONSTANT;
    effect.constant.direction.type = SDL_HAPTIC_CARTESIAN;
    effect.constant.direction.dir[0] = 1;
    effect.constant.length = SDL_HAPTIC_INFINITY;
    effect.constant.level = level;

    SDL_HapticUpdateEffect(haptic_, constant_effect_id_, &effect);
}

void SDLFFBSink::UpdateSpringEffect(double coefficient)
{
    // coefficient: 0.0 ~ 1.0
    Uint16 coeff = static_cast<Uint16>(std::clamp(coefficient, 0.0, 1.0) * 32767.0);

    SDL_HapticEffect effect = {};
    effect.type = SDL_HAPTIC_SPRING;
    effect.condition.length = SDL_HAPTIC_INFINITY;
    effect.condition.right_coeff[0] = coeff;
    effect.condition.left_coeff[0]  = coeff;
    effect.condition.right_sat[0]   = 0x7FFF;
    effect.condition.left_sat[0]    = 0x7FFF;

    SDL_HapticUpdateEffect(haptic_, spring_effect_id_, &effect);
}

void SDLFFBSink::UpdateDamperEffect(double coefficient)
{
    Uint16 coeff = static_cast<Uint16>(std::clamp(coefficient, 0.0, 1.0) * 32767.0);

    SDL_HapticEffect effect = {};
    effect.type = SDL_HAPTIC_DAMPER;
    effect.condition.length = SDL_HAPTIC_INFINITY;
    effect.condition.right_coeff[0] = coeff;
    effect.condition.left_coeff[0]  = coeff;
    effect.condition.right_sat[0]   = 0x7FFF;
    effect.condition.left_sat[0]    = 0x7FFF;

    SDL_HapticUpdateEffect(haptic_, damper_effect_id_, &effect);
}

void SDLFFBSink::UpdateCombinedConstantForce(double lat_accel, double speed,
                                              double steering_pos, double steering_vel,
                                              double dt)
{
    // === FFB Model v5: Physics-Inspired ===
    //
    // Four components:
    //   1. SAT:      Self-aligning torque from lateral acceleration (replaces centering)
    //   2. Friction:  Coulomb friction — opposes steering motion (steering weight)
    //   3. Damping:   Velocity-proportional resistance (viscous)
    //   4. SoftStop:  Progressive resistance near steering lock
    //
    // No artificial centering force. Centering IS the SAT.

    double speed_factor = std::clamp(speed / 30.0, 0.0, 1.0);

    // --- 0. Feel authority while the target-track servo owns the wheel ---
    //
    // SAT / friction / damping are all computed from `steering_pos`, which is
    // the SIMULATED wheel angle out of HVD. While target-tracking is active the
    // servo's target IS that same simulated angle, so these terms oppose the
    // servo by construction: the harder AD steers, the harder they centre away
    // from where the servo is trying to go. With the G29's breakaway force at
    // only ~0.19, the measured cost is severe — the wheel reached just 28.8% of
    // a commanded lane change and 34.2% of a right turn (real-rig replay of
    // recorded AD steering, CHARACTERIZATION.md §6).
    //
    // Sourcing them from the physical wheel instead (the obvious fix) only
    // reaches 72%: the reactive-SAT term is a function of lat_accel, not of
    // wheel angle, so swapping the position source cannot remove it.
    //
    // So while the servo is active the feel terms are scaled by feel_ratio
    // (default 0 = the servo owns the wheel, which is what a hands-off
    // AD-driven wheel physically is). This is NOT a permanent loss of road
    // feel: target_active_ is driven by `active=!lat_manual` from
    // ControllerVirtualDriver, so the instant the driver's push latches the
    // override to MANUAL, target_active_ goes false and the full SAT/friction/
    // damping model returns on the very next tick.
    const double feel = target_active_ ? feel_ratio_ : 1.0;

    // --- 1. SAT (Self-Aligning Torque) ---
    // Two components:
    //   Predictive: steering angle → slip angle → Fy → SAT (immediate response)
    //   Reactive:   lat_accel as Fy/m proxy (delayed, carries grip-limit info)

    // Power assist: high assist at low speed (light parking), low at high speed (heavy, stable)
    double assist_ratio = assist_low_speed_ + (assist_high_speed_ - assist_low_speed_) * speed_factor;
    double manual_ratio = 1.0 - assist_ratio;

    // Caster trail centering: geometric effect from caster angle + mechanical trail.
    // Any forward motion + nonzero steering angle → restoring torque.
    // NOT affected by power assist (it's a geometric/tire effect, not column torque).
    // Gentle onset: begins at walking speed (~1 m/s), full effect by ~5 m/s.
    double caster_onset = std::clamp(speed / 5.0, 0.0, 1.0);
    double sat_predictive = -steering_pos * sat_centering_gain_ * caster_onset * feel;

    // Reactive SAT: from actual lateral acceleration (richer dynamics, grip-limit lightening).
    double slip_proxy = std::clamp(std::abs(lat_accel) / 9.81, 0.0, 1.0);
    double trail_factor = std::max(0.0, 1.0 - slip_proxy * slip_proxy);
    double sat_reactive = -lat_accel * sat_gain_ * trail_factor * manual_ratio * feel;

    double sat = sat_predictive + sat_reactive;

    // --- 2. Friction (Coulomb) ---
    // Opposes steering velocity in both directions — this is the "weight" of steering.
    // Increases slightly with speed for highway stability.
    double friction_mag = friction_base_ + friction_speed_gain_ * speed_factor;
    double friction = -std::tanh(steering_vel * 3.0) * friction_mag * feel;

    // --- 3. Damping (viscous) ---
    // Velocity-proportional resistance. More damping at speed for stability.
    double damping_coeff = damper_base_ + damper_speed_gain_ * speed_factor;
    double damping = -steering_vel * damping_coeff * feel;

    // --- 4. Soft Stop ---
    // Progressive resistance near steering lock to prevent hard slam.
    // Deliberately NOT scaled by `feel`: this is end-stop protection, not road
    // feel, and safety limiters must not be attenuated by a comfort setting.
    double soft_stop = 0.0;
    double stop_zone = 0.1;  // ramp-up zone width [rad]
    double overshoot = std::abs(steering_pos) - (lock_angle_ - stop_zone);
    if (overshoot > 0.0)
    {
        double normalized = std::clamp(overshoot / stop_zone, 0.0, 1.0);
        soft_stop = -std::copysign(normalized * normalized * soft_stop_gain_, steering_pos);
    }

    // --- 5. Target-track (F7b) ---
    // Drives the physical wheel toward the AD-commanded angle via a PID servo
    // against the physical axis (spike script 04). Default OFF so existing
    // ManualDrive-only behavior is unchanged. Also feeds OverrideManager the
    // "how hard is the driver pushing back?" sample. Units throughout are
    // NORMALIZED axis-fraction — matches spike Kp calibration; unrelated to
    // the sim wheel radians used above for SAT/friction/damping.
    // target_norm is exposed in the sample so OverrideManager can rate-gate
    // its threshold check (see IFFBSink.hpp / OverrideManager::Update).
    double target_track = 0.0;
    if (target_active_)
    {
        const double actual_norm = ReadPhysicalWheelNorm();
        double u_feedback = 0.0;
        target_track = ComputeSteerServoForce(target_norm_, actual_norm, dt,
                                              servo_state_, servo_cfg_, &u_feedback);
        // Feedback-only: the friction feed-forward is plant compensation, not
        // driver resistance, and must not consume the detector's margin.
        last_sample_.commanded_force        = std::abs(u_feedback);
        last_sample_.position_error         = target_norm_ - actual_norm;
        last_sample_.target_norm            = target_norm_;
        last_sample_.active                 = true;
    }

    // Combine
    double total = sat + friction + damping + soft_stop + target_track;
    total = std::clamp(total, -max_force_, max_force_);

    // feature:F7 — the EFFECTIVE force, i.e. what the device is actually told
    // to produce this frame. This (not the feedback-only value recorded above)
    // is what OverrideManager's shadow plant must integrate; see
    // IFFBSink.hpp's effective_force_signed comment for why the distinction is
    // load-bearing. Written after the combine+clamp precisely so it can never
    // drift from the value handed to UpdateConstantEffect below.
    if (target_active_)
    {
        last_sample_.effective_force_signed = total;
    }

    static int log_counter = 0;
    if (++log_counter % 50 == 0 || log_counter <= 5)
    {
        LOG_INFO("SDLFFBSink v5: total={:.3f} (sat_p={:.3f} sat_r={:.3f} fric={:.3f} damp={:.3f} stop={:.3f} tt={:.3f}) steer={:.3f} lat_a={:.2f} v={:.1f}",
                 total, sat_predictive, sat_reactive, friction, damping, soft_stop, target_track, steering_pos, lat_accel, speed);
    }

    // feature:F7 unattended-run safety: evaluate BEFORE commanding, and hold
    // the force at zero once tripped. Placed here (not inside
    // UpdateConstantEffect) so the trip sees the same value the device would
    // have received, and so the emulated spring/damper path cannot bypass it.
    if (safety_tripped_) total = 0.0;
    UpdateConstantEffect(total);
    UpdateSafetyWatchdog(total, dt);
}

} // namespace gt_esmini

#endif // GT_ENABLE_SDL2
