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

    // feature:F7 (F7b) — target-tracking (AD wheel following)
    bool                  target_track_enabled_ = false;  // config: master on/off
    SteerServoConfig      servo_cfg_            = {};
    SteerServoState       servo_state_          = {};
    double                target_norm_          = 0.0;
    bool                  target_active_        = false;  // per-tick input from AD
    bool                  target_active_prev_   = false;
    FfbInterventionSample last_sample_          = {};
};

} // namespace gt_esmini

#endif // GT_ENABLE_SDL2
