#pragma once

#include <cmath>
#include <vector>

namespace gt_esmini
{

enum class SpeedTransitionShape
{
    LINEAR,
    SINUSOIDAL,
    CUBIC,
    STEP
};

struct LonProfilePoint
{
    double t_offset;
    double v_target;
    double a_max;
    double j_max;
};

class ControllerRealDriver;

class LonProfilePlanner
{
public:
    /// Build a 20-point profile from current_speed toward smoothed_target_.
    std::vector<LonProfilePoint> BuildProfile(double current_speed) const;

    /// Set target speed with explicit transition dynamics (from SpeedAction).
    /// @param start_speed  If >= 0, override the transition start point with the actual vehicle speed
    ///                     to avoid PID tracking lag from stale smoothed_target_.
    void SetTargetWithDynamics(double target_speed, double duration,
                               SpeedTransitionShape shape = SpeedTransitionShape::LINEAR,
                               double start_speed = -1.0);

    /// Advance the internal timer and update smoothed_target_.
    /// @param dt           Frame time step [s]
    /// @param current_target  The controller's current setSpeed_ (detects external changes)
    void Advance(double dt, double current_target);

    /// Current smoothed target speed.
    double GetSmoothedTarget() const { return smoothed_target_; }

private:
    bool   initialized_          = false;
    double smoothed_target_      = 0.0;
    double transition_start_     = 0.0;
    double target_speed_         = 0.0;
    double transition_duration_  = 3.0;
    SpeedTransitionShape transition_shape_ = SpeedTransitionShape::LINEAR;
    double transition_elapsed_   = 0.0;

    static constexpr double kDefaultRate = 3.0;  // m/s²
};

} // namespace gt_esmini
