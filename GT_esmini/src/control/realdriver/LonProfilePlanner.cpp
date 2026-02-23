#include "gt_esmini/control/realdriver/LonProfilePlanner.hpp"

#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace gt_esmini
{

void LonProfilePlanner::SetTargetWithDynamics(double target_speed, double duration,
                                               SpeedTransitionShape shape)
{
    if (!initialized_)
    {
        // First explicit target: snap smoothed_target_ to current level before ramping
        initialized_ = true;
    }
    transition_start_    = smoothed_target_;
    target_speed_        = target_speed;
    transition_duration_ = std::max(duration, 0.01);
    transition_shape_    = shape;
    transition_elapsed_  = 0.0;
}

void LonProfilePlanner::Advance(double dt, double current_target)
{
    // First call: snap immediately to current target (Init SpeedAction)
    if (!initialized_)
    {
        initialized_        = true;
        smoothed_target_    = current_target;
        transition_start_   = current_target;
        target_speed_       = current_target;
        transition_elapsed_ = transition_duration_;
        return;
    }

    // Detect external target change not set via SetTargetWithDynamics
    if (std::abs(current_target - target_speed_) > 0.01)
    {
        transition_start_ = smoothed_target_;
        target_speed_     = current_target;
        transition_elapsed_ = 0.0;
        const double diff = std::abs(target_speed_ - smoothed_target_);
        transition_duration_ = (diff > 0.01) ? diff / kDefaultRate : 0.01;
        transition_shape_    = SpeedTransitionShape::LINEAR;
    }

    // Advance elapsed time
    transition_elapsed_ = std::min(transition_elapsed_ + dt, transition_duration_);
    const double alpha = transition_elapsed_ / transition_duration_;

    switch (transition_shape_)
    {
    case SpeedTransitionShape::LINEAR:
        smoothed_target_ = transition_start_ + alpha * (target_speed_ - transition_start_);
        break;
    case SpeedTransitionShape::SINUSOIDAL:
        smoothed_target_ = transition_start_ -
            (target_speed_ - transition_start_) * (std::cos(M_PI * alpha) - 1.0) / 2.0;
        break;
    case SpeedTransitionShape::CUBIC:
        smoothed_target_ = transition_start_ +
            (target_speed_ - transition_start_) * alpha * alpha * (3.0 - 2.0 * alpha);
        break;
    case SpeedTransitionShape::STEP:
        smoothed_target_ = target_speed_;
        break;
    }
}

std::vector<LonProfilePoint> LonProfilePlanner::BuildProfile(double current_speed) const
{
    constexpr double kHorizon = 3.0;
    constexpr int    kPoints  = 20;
    constexpr double kDt      = 0.15;
    constexpr double kAMax    = 3.0;
    constexpr double kJMax    = 8.0;

    std::vector<LonProfilePoint> profile;
    profile.reserve(kPoints);

    const double v0 = current_speed;
    const double v1 = smoothed_target_;

    for (int i = 0; i < kPoints; ++i)
    {
        const double t = i * kDt;
        const double a = std::clamp((kHorizon > 0.0) ? (t / kHorizon) : 1.0, 0.0, 1.0);
        const double v = v0 + (v1 - v0) * a;
        profile.push_back(LonProfilePoint{t, v, kAMax, kJMax});
    }

    return profile;
}

} // namespace gt_esmini
