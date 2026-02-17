#include "gt_esmini/control/realdriver/LonProfilePlanner.hpp"

#include <algorithm>

namespace gt_esmini
{
std::vector<LonProfilePoint> LonProfilePlanner::BuildProfile(double current_speed, double target_speed) const
{
    constexpr double kHorizon = 3.0;
    constexpr int kPoints = 20;
    constexpr double kDt = 0.15;
    constexpr double kAMax = 3.0;
    constexpr double kJMax = 8.0;

    std::vector<LonProfilePoint> profile;
    profile.reserve(kPoints);

    const double v0 = current_speed;
    const double v1 = target_speed;

    for (int i = 0; i < kPoints; ++i)
    {
        const double t = i * kDt;
        const double alpha = std::clamp((kHorizon > 0.0) ? (t / kHorizon) : 1.0, 0.0, 1.0);
        const double v = v0 + (v1 - v0) * alpha;
        profile.push_back(LonProfilePoint{t, v, kAMax, kJMax});
    }

    return profile;
}
} // namespace gt_esmini
