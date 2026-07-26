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
       // F5: front-bumper (leading-edge) road localization, additive sibling of "ego".
       << ",\"front_bumper\":{\"x\":" << t.front_bumper.x << ",\"y\":" << t.front_bumper.y
       << ",\"road_id\":" << t.front_bumper.road_id << ",\"lane\":" << t.front_bumper.lane_id
       << ",\"s\":" << t.front_bumper.s << ",\"t\":" << t.front_bumper.t
       << ",\"offset\":" << t.front_bumper.offset << ",\"valid\":" << b(t.front_bumper.valid) << "}"
       << ",\"override\":{\"lateral\":" << b(t.override_lateral)
       << ",\"longitudinal\":" << b(t.override_longitudinal)
       << ",\"manual_transition\":" << b(t.manual_transition)
       << ",\"auto_transition\":" << b(t.auto_transition)
       << ",\"resume_pressed\":" << b(t.resume_pressed) << "}"
       // feature:F7 (F7b) FFB target-track observability. Additive block;
       // consumers that predate it (existing overlay) simply ignore it.
       << ",\"ffb\":{\"target_active\":" << b(t.ffb_target_active)
       << ",\"commanded_force\":" << t.ffb_commanded_force
       << ",\"position_error\":" << t.ffb_position_error
       << ",\"target_norm\":" << t.ffb_target_norm
       // feature:F7 (F7b, post-93b2c6c4) override-latch gate diagnostics.
       // Additive sub-object; consumers that predate it ignore it.
       // sustain_accum + block_reason are the two fields to check first when
       // diagnosing "why didn't it fire" on a real machine.
       << ",\"gates\":{\"over_force\":" << b(t.ffb_gate_over_force)
       << ",\"over_dev\":" << b(t.ffb_gate_over_dev)
       << ",\"moving_target\":" << b(t.ffb_gate_moving_target)
       << ",\"tracking_transient\":" << b(t.ffb_gate_tracking_transient)
       << ",\"target_rate\":" << t.ffb_gate_target_rate
       << ",\"derror_rate\":" << t.ffb_gate_derror_rate
       << ",\"actual_norm\":" << t.ffb_gate_actual_norm
       << ",\"shadow_norm\":" << t.ffb_gate_shadow_norm
       << ",\"residual\":" << t.ffb_gate_residual
       << ",\"residual_threshold\":" << t.ffb_gate_residual_threshold
       << ",\"effective_force\":" << t.ffb_gate_effective_force
       << ",\"shadow_moving\":" << b(t.ffb_gate_shadow_moving)
       << ",\"sustain_accum\":" << t.ffb_gate_sustain_accum
       << ",\"block_reason\":\"" << t.ffb_gate_block_reason << "\"}}"
       // feature:F7 AD steering safety envelope observability. Additive block;
       // consumers that predate it simply ignore it. steer_in/steer_out let a
       // verifier see the envelope's actual effect: "driver":{"steer":...} below
       // stays the RAW pre-envelope AD proposal (untouched on purpose), while
       // envelope.steer_out is what was actually applied to the vehicle/FFB.
       << ",\"envelope\":{\"lateral_accel_active\":" << b(t.ad_envelope_lateral_accel_active)
       << ",\"yaw_rate_active\":" << b(t.ad_envelope_yaw_rate_active)
       << ",\"steer_rate_active\":" << b(t.ad_envelope_steer_rate_active)
       << ",\"active\":" << b(t.ad_envelope_active)
       << ",\"steer_in\":" << t.ad_envelope_steer_in
       << ",\"steer_out\":" << t.ad_envelope_steer_out << "}"
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
    os << "]}";

    // Phase 3 traffic policies. constraints[] is the union emitted by the enabled
    // policies (lead-vehicle / traffic-light / stop-yield sign); the planner folds
    // them into the midlong ceiling above. Session B's overlay reads this shape.
    auto kind_str = [](PolicyConstraint::Kind k) -> const char* {
        switch (k)
        {
            case PolicyConstraint::Kind::STOP_AT_S:      return "stop_at_s";
            case PolicyConstraint::Kind::MAX_SPEED:      return "max_speed";
            case PolicyConstraint::Kind::MAX_SPEED_TO_S: return "max_speed_to_s";
            case PolicyConstraint::Kind::YIELD:          return "yield";
            case PolicyConstraint::Kind::WAIT_UNTIL:     return "wait_until";
            default:                                     return "none";
        }
    };
    // W2 (capability_model §2.2a): the arbitration tier must survive serialization —
    // without it the outcome of tier arbitration ("did AEB win as the SAFETY layer?")
    // is not observable outside the process. Additive field; consumers that predate it
    // (frontend PolicyConstraint interface, vd_metrics dict readers) ignore it.
    auto tier_str = [](PolicyConstraint::Tier t2) -> const char* {
        switch (t2)
        {
            case PolicyConstraint::Tier::SAFETY:     return "safety";
            case PolicyConstraint::Tier::COMPLIANCE: return "compliance";
            case PolicyConstraint::Tier::COURTESY:   return "courtesy";
            default:                                 return "comfort";
        }
    };
    os << ",\"policy\":{\"valid\":" << b(t.policy.valid) << ",\"constraints\":[";
    for (size_t i = 0; i < t.policy.constraints.size(); ++i)
    {
        const auto& c = t.policy.constraints[i];
        if (i) os << ",";
        os << "{\"kind\":\"" << kind_str(c.kind) << "\",\"s\":" << c.s
           << ",\"value\":" << c.value << ",\"source\":\"" << c.source
           << "\",\"tier\":\"" << tier_str(c.tier) << "\"}";
    }
    os << "]";
    // W3: policy diagnostics (gt.<policy>.<quantity>_<unit> -> string value; see
    // PolicyDetail.hpp). Emitted as a JSON object because the keys are unique by
    // construction. Values stay strings so this block is byte-identical to what
    // gets forwarded into OSI HostVehicleData custom_detail.
    os << ",\"detail\":{";
    for (size_t i = 0; i < t.policy.detail.size(); ++i)
    {
        if (i) os << ",";
        os << "\"" << t.policy.detail[i].first << "\":\"" << t.policy.detail[i].second << "\"";
    }
    os << "}}}";

    return os.str();
}

}  // namespace gt_esmini
