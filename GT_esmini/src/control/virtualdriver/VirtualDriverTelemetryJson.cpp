#include "gt_esmini/control/virtualdriver/VirtualDriverTelemetryJson.hpp"

#include <sstream>

namespace gt_esmini
{

std::string ToJson(const VirtualDriverTelemetry& t)
{
    std::ostringstream os;
    os.setf(std::ios::fixed);
    os.precision(4);
    auto b = [](bool v) { return v ? "true" : "false"; };

    os << "{\"sim_time\":" << t.sim_time
       << ",\"ego\":{\"x\":" << t.x << ",\"y\":" << t.y << ",\"z\":" << t.z
       << ",\"h\":" << t.h << ",\"speed\":" << t.speed
       << ",\"track\":" << t.track_id << ",\"lane\":" << t.lane_id
       << ",\"offset\":" << t.lane_offset << ",\"s\":" << t.s << "}"
       << ",\"override\":{\"lateral\":" << b(t.override_lateral)
       << ",\"longitudinal\":" << b(t.override_longitudinal) << "}"
       << ",\"driver\":{\"throttle\":" << t.driver.throttle << ",\"brake\":" << t.driver.brake
       << ",\"steer\":" << t.driver.steer << ",\"lateral_error\":" << t.driver.lateral_error
       << ",\"heading_error\":" << t.driver.heading_error << ",\"speed_error\":" << t.driver.speed_error
       << ",\"lookahead\":" << t.driver.lookahead_dist << ",\"valid\":" << b(t.driver.valid) << "}"
       << ",\"indicator\":{\"left\":" << b(t.indicator.left_on) << ",\"right\":" << b(t.indicator.right_on) << "}"
       << ",\"preview\":{\"dt\":" << t.short_plan.dt << ",\"valid\":" << b(t.short_plan.valid) << ",\"points\":[";
    for (size_t i = 0; i < t.short_plan.preview.size(); ++i)
    {
        const auto& p = t.short_plan.preview[i];
        if (i) os << ",";
        os << "{\"x\":" << p.x << ",\"y\":" << p.y << ",\"v\":" << p.v << ",\"t\":" << p.t << "}";
    }
    os << "]}";

    // Phase 2 mid/long planner. Shape must match the frontend MidLongProfile
    // contract (client.ts): v_target_profile is an array of [s, v] PAIRS (s =
    // distance ahead of the ego [m], v = max safe speed [m/s]); constraints carry
    // world XY so the scene can drop maneuver markers. Session B (V2) reads this.
    os << ",\"midlong\":{\"valid\":" << b(t.midlong.valid) << ",\"v_target_profile\":[";
    for (size_t i = 0; i < t.midlong.v_target_profile.size(); ++i)
    {
        const auto& pt = t.midlong.v_target_profile[i];
        if (i) os << ",";
        os << "[" << pt.first << "," << pt.second << "]";
    }
    os << "],\"constraints\":[";
    for (size_t i = 0; i < t.midlong.constraints.size(); ++i)
    {
        const auto& c = t.midlong.constraints[i];
        if (i) os << ",";
        os << "{\"s\":" << c.s << ",\"x\":" << c.x << ",\"y\":" << c.y
           << ",\"v\":" << c.v << ",\"kind\":\"" << c.kind << "\"}";
    }
    os << "]}}";

    return os.str();
}

}  // namespace gt_esmini
