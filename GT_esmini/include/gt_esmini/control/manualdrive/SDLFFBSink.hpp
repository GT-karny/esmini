#pragma once

#ifdef GT_ENABLE_SDL2

#include "gt_esmini/control/manualdrive/FfbTargetServo.hpp"
#include "gt_esmini/control/manualdrive/IFFBSink.hpp"

#include <SDL.h>

namespace gt_esmini
{

struct ManualDriveConfig;

class SDLFFBSink : public IFFBSink
{
public:
    SDLFFBSink();
    ~SDLFFBSink();

    bool Init(SDL_Joystick* joystick, const ManualDriveConfig& config);
    void Update(const osi3::HostVehicleData& state, double dt) override;
    void SetEnabled(bool enabled) override;
    void SetSteerTarget(double target_norm, bool active) override;
    FfbInterventionSample GetInterventionSample() const override { return last_sample_; }
    void Close();

    // feature:F7 — stop every running effect on this device, immediately.
    // Public because the process-wide emergency-release hooks (atexit / signal
    // / console-ctrl) must be able to call it on every live sink from outside
    // any instance. Deliberately minimal: no free, no log, no throw — it runs
    // from signal handlers. See SDLFFBSink.cpp.
    void SilenceDevice();

private:
    void UpdateConstantEffect(double force);
    void UpdateSpringEffect(double coefficient);
    void UpdateDamperEffect(double coefficient);
    void UpdateCombinedConstantForce(double lat_accel, double speed,
                                     double steering_pos, double steering_vel,
                                     double dt);

    // Read the raw physical wheel angle in normalized axis-fraction [-1, +1].
    // Returns 0 if no joystick — the servo term then evaluates to 0 too.
    double ReadPhysicalWheelNorm() const;

    // feature:F7 — UNATTENDED-RUN SAFETY WATCHDOG.
    //
    // A haptic wheel is a powered actuator. When a run is supervised, a human
    // is the last line of defence: they see the wheel fighting and pull the
    // plug. An unattended run has no such backstop, so the sink has to be its
    // own. Two independent trips, both of which ZERO the force and latch OFF
    // permanently (never re-arm inside a run — a watchdog that re-arms is a
    // watchdog that oscillates):
    //
    //   1. SUSTAINED SATURATION. The servo is designed to spend brief moments
    //      at max_force during a step, but never seconds: measured hands-off
    //      tracking of the most aggressive profile peaks around 0.29 (see
    //      CHARACTERIZATION.md §6). Force pinned at the cap for seconds means
    //      the loop is fighting something it cannot move, which is exactly the
    //      state that heats a G29's motor and, on a wheel nobody is holding,
    //      has nothing useful to accomplish.
    //   2. TOTAL RUNTIME. A bound on how long this sink may command ANY force
    //      in one process lifetime. Guards the case the watchdog above cannot
    //      see — a scenario that hangs mid-run with a modest but non-zero force
    //      applied, which would otherwise persist until someone notices.
    //
    // Both are OFF by default (0 = disabled) so supervised/interactive use and
    // every existing gate are bit-identical. The unattended runbook turns them
    // on. See ManualDriveConfig.ffb.safety.
    void UpdateSafetyWatchdog(double applied_force, double dt);

    // Emergency release: stop and destroy every effect on every live sink.
    // Registered with atexit()/signal()/console-ctrl so a crash, an abort, or a
    // Ctrl-C still silences the device instead of leaving a CONSTANT effect
    // running on hardware with nobody in the room. Static because a signal
    // handler cannot be handed an instance.
    static void RegisterEmergencyRelease(SDLFFBSink* sink);
    static void UnregisterEmergencyRelease(SDLFFBSink* sink);

    SDL_Joystick* joystick_ = nullptr;  // NOT owned — SDL2WheelInput owns it
    SDL_Haptic*   haptic_   = nullptr;
    bool          enabled_  = true;

    // Effect IDs (-1 = not created)
    int constant_effect_id_ = -1;
    int spring_effect_id_   = -1;
    int damper_effect_id_   = -1;

    // Capability flags
    bool has_constant_ = false;
    bool has_spring_   = false;
    bool has_damper_   = false;

    // Fallback: emulate spring/damper via constant force
    bool emulate_via_constant_ = false;

    // Config — FFB v5
    double sat_gain_            = 0.08;
    double sat_centering_gain_  = 1.50;
    double friction_base_       = 0.12;
    double friction_speed_gain_ = 0.04;
    double damper_base_         = 0.02;
    double damper_speed_gain_   = 0.06;
    double soft_stop_gain_      = 0.5;
    double lock_angle_          = 0.7;
    double assist_low_speed_    = 0.90;
    double assist_high_speed_   = 0.20;
    double max_force_           = 1.0;

    // State for emulation
    double prev_steering_ = 0.0;

    // feature:F7 — unattended-run safety watchdog (see UpdateSafetyWatchdog).
    double safety_max_saturation_s_ = 0.0;   // 0 = disabled
    double safety_max_runtime_s_    = 0.0;   // 0 = disabled
    double safety_saturation_ratio_ = 0.95;  // |f| >= ratio * max_force counts as saturated
    double safety_saturation_accum_ = 0.0;
    double safety_runtime_accum_    = 0.0;
    bool   safety_tripped_          = false; // latched; never re-arms within a run

    // feature:F7 (F7b) — target-tracking (AD wheel following)
    bool                  target_track_enabled_ = false;  // config: master on/off
    // Scales SAT / friction / damping while the servo is active (0 = servo owns
    // the wheel). Full feel returns the tick the driver's override latches,
    // because that clears target_active_. See UpdateCombinedConstantForce §0.
    double                feel_ratio_           = 0.0;
    SteerServoConfig      servo_cfg_            = {};
    SteerServoState       servo_state_          = {};
    double                target_norm_          = 0.0;
    bool                  target_active_        = false;  // per-tick input from AD
    bool                  target_active_prev_   = false;
    FfbInterventionSample last_sample_          = {};
};

} // namespace gt_esmini

#endif // GT_ENABLE_SDL2
