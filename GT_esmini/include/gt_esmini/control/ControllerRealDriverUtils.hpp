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

// One entry of the fixed 24-slot ADAS frame used by RealDriver / PythonDriver /
// ManualDrive.
//
// The slot INDEX is a wire contract in both directions: inbound HVD maps
// custom_name -> index, outbound writes index -> label. Slots must therefore never
// be reordered to line up with the OSI enum. Each slot instead carries the OSI
// Name value it denotes, because the two orders genuinely differ -- `index + 2`
// holds only for slots 0..12, then NIGHT_VISION / HEAD_UP_DISPLAY are swapped and
// slots 15..19 shift again.
//
// osi_name mirrors osi_hostvehicledata.proto (OSI 3.7.0); the values are
// static_assert-ed against the real enum in GT_esminiLib.cpp, the one place that
// sees both, since `control` must not depend on `osi` (GT_esmini/CLAUDE.md §2).
struct AdasSlot
{
    std::string_view label;
    int              osi_name;
};

inline constexpr std::array<AdasSlot, kAdasFunctionCount> kAdasSlots = {{
    {"BLIND_SPOT_WARNING", 2},
    {"FORWARD_COLLISION_WARNING", 3},
    {"LANE_DEPARTURE_WARNING", 4},
    {"PARKING_COLLISION_WARNING", 5},
    {"REAR_CROSS_TRAFFIC_WARNING", 6},
    {"AUTOMATIC_EMERGENCY_BRAKING", 7},
    {"AUTOMATIC_EMERGENCY_STEERING", 8},
    {"REVERSE_AUTOMATIC_EMERGENCY_BRAKING", 9},
    {"ADAPTIVE_CRUISE_CONTROL", 10},
    {"LANE_KEEPING_ASSIST", 11},
    {"ACTIVE_DRIVING_ASSISTANCE", 12},
    {"BACKUP_CAMERA", 13},
    {"SURROUND_VIEW_CAMERA", 14},
    {"NIGHT_VISION", 21},
    {"HEAD_UP_DISPLAY", 20},
    {"ACTIVE_PARKING_ASSISTANCE", 15},
    {"REMOTE_PARKING_ASSISTANCE", 16},
    {"TRAILER_ASSISTANCE", 17},
    {"AUTOMATIC_HIGH_BEAMS", 18},
    {"DRIVER_MONITORING", 19},
    {"URBAN_DRIVING", 22},
    {"HIGHWAY_AUTOPILOT", 23},
    {"CRUISE_CONTROL", 24},
    {"SPEED_LIMIT_CONTROL", 25},
}};

inline int MapAdasFunctionNameToIndex(std::string name)
{
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });

    const std::string_view key(name);
    const auto it = std::find_if(kAdasSlots.begin(), kAdasSlots.end(), [key](const AdasSlot& slot) {
        return slot.label == key;
    });
    if (it == kAdasSlots.end())
    {
        return -1;
    }
    return static_cast<int>(std::distance(kAdasSlots.begin(), it));
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
