#pragma once

#include "gt_esmini/control/virtualdriver/IDriverModel.hpp"

namespace gt_esmini
{

struct PIDPurePursuitConfig
{
    // --- Lateral: pure pursuit ---
    double lookahead_gain  = 0.5;    // Ld = clamp(gain * speed, min, max)
    double min_lookahead   = 4.0;    // [m]
    double max_lookahead   = 20.0;   // [m]
    // Physical max front-wheel angle [rad]. Equals RealVehicle steer_gain so the
    // normalized steering exactly inverts the physics mapping
    // (RealVehicle: wheelAngle = -steering * steer_gain).
    double max_steer_angle = 0.61;
    // Maps pure-pursuit wheel angle (δ, +left) → normalized steering. RealVehicle's
    // convention is +steering = right turn, so the default sign is negative.
    double steering_sign   = -1.0;

    // --- Longitudinal: speed PID ---
    double kp             = 0.6;
    double ki             = 0.2;
    double kd             = 0.0;
    double integral_limit = 2.0;     // clamp on the ki*integral contribution
    double max_throttle   = 1.0;
    double max_brake      = 1.0;
};

// Phase 1 default driver model.
//   Lateral      = pure pursuit on the trajectory preview
//   Longitudinal = PID on speed error toward the preview's target speed
class PIDPurePursuitDriver : public IDriverModel
{
public:
    explicit PIDPurePursuitDriver(const PIDPurePursuitConfig& cfg = {}) : cfg_(cfg) {}

    void Configure(const PIDPurePursuitConfig& cfg) { cfg_ = cfg; }
    void Reset();  // clear PID integrator / derivative history

    PedalSteerCommand Compute(const ShortPlannerSnapshot& plan,
                              const DriverState&          state,
                              double                      dt,
                              DriverModelSnapshot*        out_snapshot) override;

private:
    PIDPurePursuitConfig cfg_;
    double               integral_       = 0.0;
    double               prev_speed_err_ = 0.0;
    bool                 has_prev_       = false;
};

}  // namespace gt_esmini
