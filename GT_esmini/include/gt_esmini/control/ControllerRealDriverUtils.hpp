#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>
#include "OSCPrivateAction.hpp"

namespace gt_esmini::realdetail
{
constexpr double kWaypointStep = 5.0;
constexpr double kWaypointTotalDistance = 500.0;
constexpr double kNearbyWaypointThreshold = 10.0;
constexpr std::size_t kAdasFunctionCount = 24;
constexpr double kSharpTurnWarnRad = 0.087;  // 5 deg

inline double NormalizeAngle(double angle)
{
    while (angle > M_PI) angle -= 2.0 * M_PI;
    while (angle < -M_PI) angle += 2.0 * M_PI;
    return angle;
}

inline double SmootherStep(double x)
{
    const double t = std::clamp(x, 0.0, 1.0);
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

inline double Distance2D(double x0, double y0, double x1, double y1)
{
    const double dx = x1 - x0;
    const double dy = y1 - y0;
    return std::sqrt(dx * dx + dy * dy);
}

inline int MapAdasFunctionNameToIndex(std::string name)
{
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });

    static constexpr std::array<std::string_view, kAdasFunctionCount> kAdasNames = {
        "BLIND_SPOT_WARNING",
        "FORWARD_COLLISION_WARNING",
        "LANE_DEPARTURE_WARNING",
        "PARKING_COLLISION_WARNING",
        "REAR_CROSS_TRAFFIC_WARNING",
        "AUTOMATIC_EMERGENCY_BRAKING",
        "AUTOMATIC_EMERGENCY_STEERING",
        "REVERSE_AUTOMATIC_EMERGENCY_BRAKING",
        "ADAPTIVE_CRUISE_CONTROL",
        "LANE_KEEPING_ASSIST",
        "ACTIVE_DRIVING_ASSISTANCE",
        "BACKUP_CAMERA",
        "SURROUND_VIEW_CAMERA",
        "NIGHT_VISION",
        "HEAD_UP_DISPLAY",
        "ACTIVE_PARKING_ASSISTANCE",
        "REMOTE_PARKING_ASSISTANCE",
        "TRAILER_ASSISTANCE",
        "AUTOMATIC_HIGH_BEAMS",
        "DRIVER_MONITORING",
        "URBAN_DRIVING",
        "HIGHWAY_AUTOPILOT",
        "CRUISE_CONTROL",
        "SPEED_LIMIT_CONTROL"};

    const std::string_view key(name);
    const auto it = std::find(kAdasNames.begin(), kAdasNames.end(), key);
    if (it == kAdasNames.end())
    {
        return -1;
    }
    return static_cast<int>(std::distance(kAdasNames.begin(), it));
}

inline double ComputeLaneOffsetTransitionDistance(
    scenarioengine::OSCPrivateAction::DynamicsDimension dimension,
    double paramValue,
    double speedForTime,
    double deltaOffset)
{
    switch (dimension)
    {
        case scenarioengine::OSCPrivateAction::DynamicsDimension::DISTANCE:
            return std::max(paramValue, 5.0);
        case scenarioengine::OSCPrivateAction::DynamicsDimension::TIME:
            return speedForTime * std::max(paramValue, 0.1);
        case scenarioengine::OSCPrivateAction::DynamicsDimension::RATE:
            return speedForTime * (deltaOffset / std::max(paramValue, 0.1));
        default:
            return 20.0;
    }
}
}  // namespace gt_esmini::realdetail
