#pragma once

#include <vector>
#include <string>
#include <utility>

// ============================================================================
// VirtualDriver telemetry structs — the cross-session contract.
//
// These are pure-data snapshots produced by each layer of the VirtualDriver
// driver stack and aggregated into VirtualDriverTelemetry, which is exposed via
// GT_GetVirtualDriverTelemetry() (C-API, JSON). Session B's visualization
// overlay reads this shape, so changes here must be coordinated.
//
// Header-only, no engine dependency (only std types), so it can be included
// from interfaces, the controller, and the C-API serializer alike.
// ============================================================================

namespace gt_esmini
{

// One sampled point of the short-horizon trajectory preview.
// Sampling is equal-Δt (see IShortPlanner): t = i * dt.
struct TrajectoryPoint
{
    double x = 0.0;  // world position [m]
    double y = 0.0;  // world position [m]
    double v = 0.0;  // target speed at this point [m/s]
    double t = 0.0;  // time offset from "now" [s]
};

// IShortPlanner output + telemetry.
struct ShortPlannerSnapshot
{
    std::vector<TrajectoryPoint> preview;     // future points, equal Δt
    double                       dt        = 0.1;    // sampling step [s]
    double                       horizon_s = 0.0;    // total horizon covered [s]
    bool                         valid     = false;
    // Forward control-point offset [m] the planner ACTUALLY applied when anchoring
    // the preview (the requested ShortPlanContext::control_point_offset, clamped to
    // 0 during a storyboard lateral maneuver). The controller shifts the driver
    // state forward by this exact value so the control point and the lane-center
    // anchor sit on the same route point (P2 issue 2). See ControllerVirtualDriver.
    double                       control_point_offset = 0.0;
};

// IDriverModel internal state + telemetry (inverse-control diagnostics).
struct DriverModelSnapshot
{
    double throttle       = 0.0;  // commanded [0,1]
    double brake          = 0.0;  // commanded [0,1]
    double steer          = 0.0;  // commanded normalized [-1,1]
    double lateral_error  = 0.0;  // cross-track error to preview [m]
    double heading_error  = 0.0;  // [rad]
    double speed_error    = 0.0;  // v_target - v_current [m/s]
    double lookahead_dist = 0.0;  // pure-pursuit lookahead used [m]
    bool   valid          = false;
};

// A labelled constraint point selected by the mid/long planner (Phase 2).
// Carries world XY so the viewer can drop a maneuver marker on the scene.
struct MidLongConstraint
{
    double      s = 0.0;   // route s the constraint applies at [m] (ahead of ego)
    double      x = 0.0;   // world position [m]
    double      y = 0.0;   // world position [m]
    double      v = 0.0;   // target speed at the constraint [m/s]
    std::string kind;      // "curve" | "junction" | "speed_limit" | "stop"
};

// IMidLongPlanner output (Phase 2). v_target as a function of route s.
struct MidLongPlannerSnapshot
{
    // (s [m], v_max [m/s]) sample pairs along the route ahead.
    std::vector<std::pair<double, double>> v_target_profile;
    // Labelled constraint points (curve / junction / speed-limit) with world XY.
    std::vector<MidLongConstraint>         constraints;
    bool                                   valid = false;
};

// A single constraint emitted by a traffic policy (Phase 3).
struct PolicyConstraint
{
    enum class Kind
    {
        NONE,
        STOP_AT_S,       // come to a full stop at route s
        MAX_SPEED,       // cap speed everywhere
        MAX_SPEED_TO_S,  // cap speed until route s
        YIELD,           // yield zone
        WAIT_UNTIL       // hold until sim time = value
    };
    // Arbitration tier (AEB phase 1). Governs which deceleration profile the
    // mid/long planner uses to shape the approach to a STOP_AT_S constraint —
    // see ManeuverAwareSpeedPlanner's ApplyPolicyConstraints(). Every existing
    // emitter (LeadVehicleAware/TrafficLightAware/StopYieldSignAware/
    // ConflictPointResolver/CrosswalkPedestrianAware) leaves this at its default
    // (COMFORT), so pre-AEB behavior is bit-identical. Only AebSafety emits
    // SAFETY.
    enum class Tier
    {
        COMFORT,
        COURTESY,
        COMPLIANCE,
        SAFETY
    };
    Kind        kind  = Kind::NONE;
    double      s     = 0.0;   // route s the constraint applies at/until [m]
    double      value = 0.0;   // speed [m/s] or time [s] depending on kind
    std::string source;        // "lead_vehicle" | "traffic_light" | "stop_sign" | ...
    Tier        tier  = Tier::COMFORT;
};

// Policy diagnostics: string key/value pairs carrying the numeric internals a
// policy used to discard (AEB's TTC / required decel, ...). String-typed on
// purpose — this is the exact shape of OSI 3.7.0's only generic slot for such
// quantities (HostVehicleData custom_detail, repeated KeyValuePair), so the
// same values reach the telemetry JSON and OSI without a translation step.
// Key naming convention and the AddDetail() helpers live in PolicyDetail.hpp.
using PolicyDetail = std::vector<std::pair<std::string, std::string>>;

// ITrafficPolicy output (Phase 3).
struct TrafficPolicySnapshot
{
    std::vector<PolicyConstraint> constraints;
    bool                          valid = false;
    // Why the policy did (or did not) emit a constraint this frame. Populated
    // whether or not `constraints` is empty — that is the point: it makes the
    // NEGATIVE decision observable too. Keys are namespaced per policy
    // (gt.aeb.*), so concatenating snapshots from several policies is safe.
    PolicyDetail                  detail;
};

// IIndicatorPolicy output.
struct IndicatorSnapshot
{
    bool left_on  = false;
    bool right_on = false;
};

// Front-bumper road-frame localization (F5). The telemetry ego block localizes
// the vehicle origin (≈ rear axle); this localizes the leading edge of the
// bounding box, so a viewer/verifier can reason about where the car's nose sits
// on the road (e.g. "front already entered the junction / crossed the stop line")
// independently of the origin. World XY is included alongside the road frame.
struct FrontBumperSnapshot
{
    double x       = 0.0;   // world position [m]
    double y       = 0.0;   // world position [m]
    int    road_id = 0;     // OpenDRIVE road id the bumper localizes to
    int    lane_id = 0;     // lane id at the bumper
    double s       = 0.0;   // road s [m]
    double t       = 0.0;   // road t [m] (signed lateral from road reference line)
    double offset  = 0.0;   // lateral offset from lane center [m]
    bool   valid   = false; // false if localization failed (off-road / no road loaded)
};

// Aggregate telemetry, exposed via GT_GetVirtualDriverTelemetry().
struct VirtualDriverTelemetry
{
    double sim_time = 0.0;

    // Ego kinematic state at this frame.
    double x     = 0.0;
    double y     = 0.0;
    double z     = 0.0;
    double h     = 0.0;  // heading [rad]
    double speed = 0.0;  // [m/s]

    // Ego road-frame localization (after physics writes pos_). Lets a viewer /
    // verifier check "stays in the routed lane" vs drifted onto a sidewalk.
    int    track_id    = 0;    // OpenDRIVE road id the ego is localized to
    int    lane_id     = 0;    // current lane id
    double lane_offset = 0.0;  // lateral offset from lane center [m]
    double s           = 0.0;  // road s [m]

    // Override status (per domain).
    bool override_lateral      = false;
    bool override_longitudinal = false;

    // feature:F7 mode-transition edges. True only on the single frame the
    // AUTO<->MANUAL flip occurred, so the web overlay / logging pipeline can
    // record a "human took over" or "resumed AD" event without diffing the
    // per-domain flags itself. Both false in steady state and while a domain
    // is scenario-locked. Never simultaneously true.
    bool manual_transition = false;  // AUTO -> MANUAL this frame
    bool auto_transition   = false;  // MANUAL -> AUTO this frame (resume-button or timeout)

    // feature:F7 (F7b) — FFB target-tracking servo state. Populated only when
    // the SDLFFBSink servo is running (ffb.target_track.enabled + AD lateral):
    // ffb_target_active is the servo-on gate, commanded_force is the last |u|
    // the PID emitted (axis-fraction units, [0..target_track.max_force]),
    // position_error is target - physical actual (axis-fraction, signed).
    // All zero when the servo is idle. Additive fields; JSON serializer emits
    // them as ffb.{target_active,commanded_force,position_error}.
    bool   ffb_target_active   = false;
    double ffb_commanded_force = 0.0;
    double ffb_position_error  = 0.0;

    // feature:F7 (F7b, follow-up post-93b2c6c4) — override-latch gate
    // diagnostics (real-machine "why didn't it fire" debugging). Mirrors
    // OverrideManager::FfbLatchDiagnostics; all false/zero while
    // ffb_target_active is false. See OverrideManager.hpp for what each gate
    // means. ffb_gate_sustain_accum is the single most useful field to watch
    // live: is it growing toward sustain_time, or stuck/resetting?
    // ffb_gate_block_reason is a single human-readable identifier for why the
    // accumulator isn't advancing this frame ("none" when it is advancing or
    // already latched): "inactive" | "bootstrap" | "below_residual".
    bool        ffb_gate_over_force           = false;
    bool        ffb_gate_over_dev             = false;
    bool        ffb_gate_moving_target        = false;
    bool        ffb_gate_tracking_transient   = false;
    double      ffb_gate_target_rate          = 0.0;
    double      ffb_gate_derror_rate          = 0.0;
    double      ffb_gate_actual_norm          = 0.0;
    // feature:F7 residual detector — the fields to read first.
    double      ffb_gate_shadow_norm          = 0.0;   // predicted unheld-wheel axis
    double      ffb_gate_residual             = 0.0;   // |actual - shadow| — the detection signal
    double      ffb_gate_residual_threshold   = 0.0;
    double      ffb_gate_effective_force      = 0.0;   // signed force driving the shadow
    bool        ffb_gate_shadow_moving        = false; // shadow plant in kinetic regime
    double      ffb_gate_sustain_accum        = 0.0;
    std::string ffb_gate_block_reason         = "none";

    // feature:F7 — AD steering safety envelope observability (AdSteeringEnvelope.hpp).
    // Which physical constraint(s) clamped this frame's AD-commanded steering
    // (or none). All false when the envelope is disabled or nothing clipped.
    // Verification uses this to show "normal driving never trips the envelope".
    bool ad_envelope_lateral_accel_active = false;
    bool ad_envelope_yaw_rate_active      = false;
    bool ad_envelope_steer_rate_active    = false;
    bool ad_envelope_active               = false;  // OR of the three
    // Normalized steering [-1,1] the envelope actually saw/produced this
    // frame. driver.steer (DriverModelSnapshot below) is the raw AD proposal
    // BEFORE the envelope — it is intentionally left untouched by the
    // envelope so "what AD wanted" stays observable. ad_envelope_steer_in is
    // the same raw value restated here (redundant with driver.steer today,
    // but keeps the envelope block self-contained if driver.steer's meaning
    // ever changes) and ad_envelope_steer_out is the CLAMPED value that was
    // actually sent to the physics backend / FFB target servo. Comparing the
    // two is how verification sees the envelope's effect at all — without
    // steer_out, "did the envelope change anything" is unobservable from
    // telemetry even though the vehicle's actual behavior did change.
    double ad_envelope_steer_in  = 0.0;
    double ad_envelope_steer_out = 0.0;

    // Per-layer snapshots.
    ShortPlannerSnapshot   short_plan;
    DriverModelSnapshot    driver;
    MidLongPlannerSnapshot midlong;   // Phase 2+
    TrafficPolicySnapshot  policy;    // Phase 3+
    IndicatorSnapshot      indicator;
    FrontBumperSnapshot    front_bumper;  // F5: leading-edge road localization
};

}  // namespace gt_esmini
