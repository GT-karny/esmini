#include "gt_esmini/control/virtualdriver/VirtualDriverTelemetryJson.hpp"

#include <sstream>

namespace gt_esmini
{

// feature:F7 — WARNING for anyone replaying or differencing this record: ONE
// LINE HOLDS TWO INSTANTS. The top-level "ffb" block is the sink's sample
// AFTER this frame's FFB update, while "ffb.gates" is OverrideManager's
// diagnostic computed from the sample it was handed BEFORE that update — i.e.
// the sample that was written into the PREVIOUS line's "ffb" block. Measured
// on f7_realwheel_basic.jsonl: gates.actual_norm(N) equals
// (ffb.target_norm - ffb.position_error)(N-1) exactly, for every frame once
// the wheel is moving. While the wheel sits at 0 the two agree and the skew is
// invisible, which is precisely what makes it dangerous — a consumer that
// pairs ffb.*(N) with gates.*(N) validates fine on the stationary prologue and
// is silently one frame off for the whole part that matters.
// GT_esmini/test/tools/ffb_override_replay.cpp pairs them correctly; copy that
// alignment rather than re-deriving it.
std::string ToJson(const VirtualDriverTelemetry& t)
{
    std::ostringstream os;
    os.setf(std::ios::fixed);
    // feature:F7 — 9, not 4. At 4 decimals a normalized steering value has a
    // 1e-4 quantum, so a per-frame difference at dt=0.01 quantizes the STEERING
    // RATE to 0.01 /s and the STEERING JERK to 1.0 /s^2. That put the entire
    // normal-driving jerk distribution (median 0.0, p99 2.0 /s^2) inside the
    // first two quanta: every derived statistic there was reading the
    // instrument's own floor rather than the signal, and "basic's jerk is 1.0"
    // meant only "below the floor". The measuring device must be finer than the
    // thing measured; 9 decimals puts the jerk quantum at 1e-5 /s^2, five orders
    // below the smallest number anyone reasons about here. Cost is file size
    // (~1.6x) on an opt-in capture path.
    os.precision(9);
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
       << ",\"resume_pressed\":" << b(t.resume_pressed)
       << ",\"takeover_pressed\":" << b(t.takeover_pressed) << "}"
       // feature:F7 scenario-driven handover. Written directly from
       // SetUpControlOutputs()/TearDownControlOutputs(), so unlike every other
       // field in this record it is NOT frozen once the controller goes
       // inactive — it is the field that reports the deactivation itself.
       << ",\"vd_active\":" << b(t.vd_active)
       // feature:F7 S2 per-domain split: active but not integrating is a real,
       // correct state, so it needs its own field rather than being inferred.
       << ",\"domain_integrator\":" << b(t.domain_integrator)
       // feature:F7 (F7b) FFB target-track observability. Additive block;
       // consumers that predate it (existing overlay) simply ignore it.
       << ",\"ffb\":{\"target_active\":" << b(t.ffb_target_active)
       << ",\"commanded_force\":" << t.ffb_commanded_force
       << ",\"position_error\":" << t.ffb_position_error
       << ",\"target_norm\":" << t.ffb_target_norm
       // Raw sink force for this instant — see the one-row lag note at the top
       // of this file for why ffb.gates.effective_force is NOT interchangeable
       // with it.
       << ",\"sample_effective_force\":" << t.ffb_sample_effective_force
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
       << ",\"sustain_time\":" << t.ffb_gate_sustain_time
       << ",\"block_reason\":\"" << t.ffb_gate_block_reason << "\""
       // feature:F7 — re-anchor instrument (observational; additive at the
       // END of "gates" per the existing sustain_time precedent — consumers
       // that predate it ignore it). See
       // test_results/f7_reanchor_instrument_spec.md §2 (revised). free_residual
       // is the field to read first: "what would the residual be if the
       // detector's own shadow had never been forcibly re-synced?"
       // hard/soft delta accumulators are kept SEPARATE on purpose — summing
       // them would hide whether S3 (onset grace) or S4 (drift) is doing the
       // erasing, which is exactly the question §3-1 needs answered.
       << ",\"reanchor_hard_count\":" << t.ffb_gate_reanchor_hard_count
       << ",\"reanchor_soft_count\":" << t.ffb_gate_reanchor_soft_count
       << ",\"reanchor_delta\":" << t.ffb_gate_reanchor_delta
       << ",\"reanchor_hard_delta_abs_accum\":" << t.ffb_gate_reanchor_hard_delta_abs_accum
       << ",\"reanchor_soft_delta_abs_accum\":" << t.ffb_gate_reanchor_soft_delta_abs_accum
       << ",\"reanchor_source\":\"" << t.ffb_gate_reanchor_source << "\""
       << ",\"free_shadow_norm\":" << t.ffb_gate_free_shadow_norm
       << ",\"free_residual\":" << t.ffb_gate_free_residual
       // free_residual is NOT guaranteed >= residual on every frame (see the
       // field comment in VirtualDriverTypes.hpp) — this counts how often it
       // reads below instead of asserting it can't. Compare walk-level
       // max(free_residual) vs max(residual), not this per-frame count's sign.
       << ",\"free_below_real_count\":" << t.ffb_gate_free_below_real_count << "}}"
       // feature:F7 AD steering safety envelope observability. Additive block;
       // consumers that predate it simply ignore it. steer_in/steer_out let a
       // verifier see the envelope's actual effect: "driver":{"steer":...} below
       // stays the RAW pre-envelope AD proposal (untouched on purpose), while
       // envelope.steer_out is what was actually applied to the vehicle/FFB.
       << ",\"envelope\":{\"lateral_accel_active\":" << b(t.ad_envelope_lateral_accel_active)
       << ",\"yaw_rate_active\":" << b(t.ad_envelope_yaw_rate_active)
       << ",\"steer_rate_active\":" << b(t.ad_envelope_steer_rate_active)
       << ",\"steer_jerk_active\":" << b(t.ad_envelope_steer_jerk_active)
       << ",\"active\":" << b(t.ad_envelope_active)
       << ",\"steer_in\":" << t.ad_envelope_steer_in
       << ",\"steer_out\":" << t.ad_envelope_steer_out
       // feature:F7 — the envelope's own curvature cap, so a verifier need not
       // re-derive it from speed and wheelbase (see VirtualDriverTypes.hpp).
       << ",\"kappa_cmd\":" << t.ad_envelope_kappa_cmd
       << ",\"kappa_limit\":" << t.ad_envelope_kappa_limit
       // feature:F7 — and the APPLIED curvature, so the safety comparison
       // |kappa_out| <= kappa_limit has BOTH sides published by the envelope.
       // Serialized at the record's fixed 9 decimals like everything else:
       // kappa is O(1e-2) here, so the quantum is ~1e-7 RELATIVE, which is the
       // resolution floor of any comparison made from this file. A consumer
       // must not use an epsilon finer than 1e-9 absolute on these two.
       << ",\"kappa_out\":" << t.ad_envelope_kappa_out << "}"
       // feature:F7 resume-merge observability (design doc
       // resume_merge_trajectory_design.md section 8-6). Additive block;
       // consumers that predate it simply ignore it. fallback_reason is the
       // field to read first for "why did this frame fall back to the
       // current-lane anchor instead of merging" ("" == normal).
       << ",\"resume_merge\":{\"active\":" << b(t.resume_merge.active)
       << ",\"d0\":" << t.resume_merge.d0
       << ",\"v0_lat\":" << t.resume_merge.v0_lat
       << ",\"a0_lat\":" << t.resume_merge.a0_lat
       << ",\"a_bound\":" << t.resume_merge.a_bound
       << ",\"comfort_unmet\":" << b(t.resume_merge.comfort_unmet)
       << ",\"duration_s\":" << t.resume_merge.duration_s
       << ",\"progress\":" << t.resume_merge.progress
       << ",\"target_offset\":" << t.resume_merge.target_offset
       << ",\"route_track\":" << t.resume_merge.route_track
       << ",\"route_lane\":" << t.resume_merge.route_lane
       << ",\"fallback_reason\":\"" << t.resume_merge.fallback_reason << "\"}"
       << ",\"driver\":{\"throttle\":" << t.driver.throttle << ",\"brake\":" << t.driver.brake
       << ",\"steer\":" << t.driver.steer << ",\"lateral_error\":" << t.driver.lateral_error
       << ",\"heading_error\":" << t.driver.heading_error << ",\"speed_error\":" << t.driver.speed_error
       << ",\"lookahead\":" << t.driver.lookahead_dist << ",\"valid\":" << b(t.driver.valid) << "}"
       << ",\"indicator\":{\"left\":" << b(t.indicator.left_on) << ",\"right\":" << b(t.indicator.right_on) << "}"
       // req-vd-ad:REQ-AD-021 / vd-func:FUNC-061 junction-turn pre-arm (design doc
       // junction_turn_signal.md section 3-4). Additive block; consumers that
       // predate it simply ignore it. on_connector is the field to read first --
       // it is the only telemetry signal that "this road is junction-owned".
       << ",\"junction_turn\":{\"dir\":" << t.junction_turn.dir
       << ",\"dist_to_entry_m\":" << t.junction_turn.dist_to_entry_m
       << ",\"on_connector\":" << b(t.junction_turn.on_connector) << "}"
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
    os << "}}";

    // RouteLanePlan (control/virtualdriver/RouteLanePlan.hpp) -- route-lane
    // conformance diagnostic. Additive top-level block; consumers that predate it
    // simply ignore it. diagnostic/reason are the two fields to read first ("" ==
    // normal on both).
    os << ",\"route_lane\":{\"valid\":" << b(t.route_lane.valid)
       << ",\"road_id\":" << t.route_lane.road_id
       << ",\"ego_lane\":" << t.route_lane.ego_lane
       << ",\"ego_lane_raw\":" << t.route_lane.ego_lane_raw
       << ",\"target_lanes\":[";
    for (size_t i = 0; i < t.route_lane.target_lanes.size(); ++i)
    {
        if (i) os << ",";
        os << t.route_lane.target_lanes[i];
    }
    os << "]"
       << ",\"on_target_lane\":" << b(t.route_lane.on_target_lane)
       << ",\"dist_to_connection\":" << t.route_lane.dist_to_connection
       << ",\"deviation_count\":" << t.route_lane.deviation_count
       << ",\"last_deviation_road_id\":" << t.route_lane.last_deviation_road_id
       << ",\"rerouted\":" << b(t.route_lane.rerouted)
       << ",\"diagnostic\":\"" << t.route_lane.diagnostic << "\""
       << ",\"reason\":\"" << t.route_lane.reason << "\"}";

    // vd-func:FUNC-055 AD lane-change initiation (LaneChangeInitiation.hpp). Additive top-level
    // block; consumers that predate it simply ignore it. gap_reason is the field to read first
    // for "why hasn't it armed yet" ("" == last evaluated gap was accepted, or nothing evaluated).
    os << ",\"lane_change\":{\"armed\":" << b(t.lane_change.armed)
       << ",\"target_track_id\":" << t.lane_change.target_track_id
       << ",\"target_lane_id\":" << t.lane_change.target_lane_id
       << ",\"direction\":" << t.lane_change.direction
       << ",\"n_remaining\":" << t.lane_change.n_remaining
       << ",\"required_m\":" << t.lane_change.required_m
       << ",\"dist_to_connection\":" << t.lane_change.dist_to_connection
       << ",\"gap_accepted\":" << b(t.lane_change.gap_accepted)
       << ",\"gap_reason\":\"" << t.lane_change.gap_reason << "\""
       << ",\"signal_active\":" << b(t.lane_change.signal_active) << "}";

    // vd-func:FUNC-056 AD overtake maneuver (OvertakeManeuver.hpp). Additive top-level block;
    // consumers that predate it simply ignore it. `considered` is the field to read first --
    // design doc overtake_maneuver.md section 9-1's false-PASS guard: a run where it never goes
    // true never actually attempted an overtake, regardless of how green everything else looks.
    os << ",\"overtake\":{\"phase\":\"" << t.overtake.phase << "\""
       << ",\"considered\":" << b(t.overtake.considered)
       << ",\"lead_id\":" << t.overtake.lead_id
       // The same vehicle in the OSI id space -- the field to join on when
       // correlating this record with an OSI GroundTruth recording. Additive next
       // to lead_id, which stays the scenario entity index the maneuver itself
       // uses internally to re-find the lead.
       << ",\"lead_osi_id\":" << t.overtake.lead_osi_id
       << ",\"delta_v_mps\":" << t.overtake.delta_v_mps
       << ",\"t_pass_s\":" << t.overtake.t_pass_s
       << ",\"required_m\":" << t.overtake.required_m
       << ",\"route_budget_m\":" << t.overtake.route_budget_m
       << ",\"blocked_reason\":\"" << t.overtake.blocked_reason << "\""
       << ",\"cleared_lead\":" << b(t.overtake.cleared_lead) << "}";

    os << "}";

    return os.str();
}

}  // namespace gt_esmini
