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

// feature:F7 resume-merge (docs/virtualdriver/resume_merge_trajectory_design.md
// section 8-6). Controller-owned merge state-machine snapshot -- deliberately
// NOT part of ShortPlannerSnapshot above (that one is the cross-session
// contract this feature must not touch; see this file's header comment).
// All zero/false/empty while resume_merge_enabled is false (the shipped
// default) or before the first arm. d0/v0_lat/a0_lat/a_bound/duration_s/
// comfort_unmet retain their last-armed values across a disarm (mirrors
// ResumeMergeState's own "harmless convenience" doc), so they stay readable
// as "what the last merge was" one frame after it stops being active.
struct ResumeMergeSnapshot
{
    bool   active        = false;  // merge in progress this frame
    double d0            = 0.0;    // [m] captured initial lateral offset (route-relative, raw +t-axis)
    double v0_lat        = 0.0;    // [m/s] captured initial lateral velocity
    double a0_lat        = 0.0;    // [m/s^2] captured initial lateral acceleration (== initial curvature proxy)
    double a_bound       = 0.0;    // [m/s^2] bound actually enforced this arm: max(a_lat_comfort, |a0_lat|)
    bool   comfort_unmet = false;  // true if even duration_max_s could not bring max|d''| under a_bound
    double duration_s    = 0.0;    // [s] selected T (fixed for the life of this arm)
    double progress      = 0.0;    // elapsed_s / duration_s, clamped to [0,1]; 0 while inactive
    double target_offset = 0.0;    // this frame's d(u) [m] -- the value applied to the preview anchor
    int    route_track   = 0;      // resolved ROUTE track id (falls back to the current track when not merging)
    int    route_lane    = 0;      // resolved ROUTE lane id (falls back to the current lane when not merging)
    // "" = normal; non-empty = which route-resolution step failed this frame
    // ("no_route" | "off_route" | "track_mismatch"). Surfaces the two silent
    // failure modes design doc section 2-0-1 identifies in Route::SetTrackS.
    std::string fallback_reason;
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

    // feature:F7 scenario-driven handover — mirrors Controller::Active() at the
    // instant SetUpControlOutputs()/TearDownControlOutputs() ran (see
    // ControllerVirtualDriver.cpp). Written directly from those two functions,
    // NOT from Step() — Step() stops running the moment the controller goes
    // inactive (ScenarioEngine only steps active controllers), so this is the
    // ONLY telemetry field guaranteed to reflect a deactivation: every other
    // field in this struct is frozen at its last-active-frame value once the
    // controller is torn down. true only between a completed setup and the
    // next teardown; false before the first activation and after any
    // teardown. See docs/virtualdriver/scenario_control_handoff_design.md §5.1.4.
    bool vd_active = false;

    // feature:F7 mode-transition edges. True only on the single frame the
    // AUTO<->MANUAL flip occurred, so the web overlay / logging pipeline can
    // record a "human took over" or "resumed AD" event without diffing the
    // per-domain flags itself. Both false in steady state and while a domain
    // is scenario-locked. Never simultaneously true.
    bool manual_transition = false;  // AUTO -> MANUAL this frame
    bool auto_transition   = false;  // MANUAL -> AUTO this frame (resume-button or timeout)
    // AUTO_RESUME pressed this frame, EVEN IF it changed nothing. Without it a
    // failed takeover attempt leaves no trace at all (auto_transition needs the
    // domain to have been manual), so a session cannot be segmented into
    // attempts. See OverrideManager::JustPressedResume().
    bool resume_pressed    = false;

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
    // AD-commanded wheel target this frame [-1,+1] axis-fraction. Exposed so a
    // per-frame capture carries the servo's INPUT alongside its output — needed
    // to tell 'AD asked for nothing' apart from 'AD asked and nothing happened'.
    double ffb_target_norm     = 0.0;
    // The sink sample's RAW signed force, belonging to this same instant.
    // Deliberately not the same number as ffb.gates.effective_force: that one
    // is the detector's diagnostic, one frame behind this block AND already
    // dead-time-delayed. Replays need the raw, undelayed force to feed back
    // in; without this field they can only be reconstructed from gates, which
    // is correct only for recordings made at dead_time=0.
    double ffb_sample_effective_force = 0.0;

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
    // feature:F7 — configured sustain_time (OverrideManager::FfbLatchDiagnostics
    // ::sustain_time), for context. Lets a live consumer compute
    // sustain_accum/sustain_time (0..1, latches at 1.0) without reading config.
    // Zeroed alongside the rest of the diagnostic block while
    // ffb_gate_block_reason=="inactive" (see OverrideManager.cpp: ffb_diag_={}).
    double      ffb_gate_sustain_time          = 0.0;
    std::string ffb_gate_block_reason         = "none";

    // feature:F7 — re-anchor instrument (test_results/f7_reanchor_instrument_spec.md,
    // revised). Purely observational; mirrors OverrideManager::FfbLatchDiagnostics'
    // reanchor_*/free_* fields. Zeroed the same way as the rest of the gate
    // block while ffb_gate_block_reason=="inactive", EXCEPT the four
    // cumulative counters (reanchor_hard_count/soft_count/
    // reanchor_{hard,soft}_delta_abs_accum/free_below_real_count), which are
    // "since run start" and survive that reset — see OverrideManager.cpp.
    // hard = S1 (always) + S3 (only if it moved the shadow); soft = S4 only
    // if it moved the shadow. Kept as SEPARATE accumulators (not summed) —
    // that split is the whole point: §3-1 asks whether S3 or S4 is
    // responsible for a given frame's erasure, which a combined total cannot
    // answer.
    int         ffb_gate_reanchor_hard_count           = 0;
    int         ffb_gate_reanchor_soft_count           = 0;
    double      ffb_gate_reanchor_delta                = 0.0;  // this frame's shadow displacement from a re-anchor; 0 if none
    double      ffb_gate_reanchor_hard_delta_abs_accum = 0.0;  // cumulative |delta| from S1+S3 (hard) re-anchors
    double      ffb_gate_reanchor_soft_delta_abs_accum = 0.0;  // cumulative |delta| from S4 (soft) drift correction
    std::string ffb_gate_reanchor_source               = "none";  // none|seed|onset_grace|drift|resume|inactive_rearm
    double      ffb_gate_free_shadow_norm         = 0.0;  // shadow integrated WITHOUT re-anchoring (S2 only)
    double      ffb_gate_free_residual            = 0.0;  // |actual - free_shadow| — "what if nothing had been erased"
    // NOT guaranteed to stay >= ffb_gate_residual on every frame (see
    // OverrideManager::FfbLatchDiagnostics::free_residual comment for the
    // counter-example) — this counts how often it reads below instead.
    // The meaningful comparison is walk-level max(free_residual) vs
    // max(residual) over the whole run, not this per-frame count's sign.
    int         ffb_gate_free_below_real_count    = 0;

    // feature:F7 — AD steering safety envelope observability (AdSteeringEnvelope.hpp).
    // Which physical constraint(s) clamped this frame's AD-commanded steering
    // (or none). All false when the envelope is disabled or nothing clipped.
    // Verification uses this to show "normal driving never trips the envelope".
    bool ad_envelope_lateral_accel_active = false;
    bool ad_envelope_yaw_rate_active      = false;
    bool ad_envelope_steer_rate_active    = false;
    // feature:F7 — the steering-JERK stage (a further narrowing of the rate
    // window). Reported separately from steer_rate_active so a run can answer
    // "did the jerk cap constrain ordinary driving" on its own: the unit tests
    // pin the STEADY-state case only (they seed prev_steer_rate_norm), so the
    // transition case is checked here, on real runs, instead.
    bool ad_envelope_steer_jerk_active    = false;
    bool ad_envelope_active               = false;  // OR of the four
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
    ResumeMergeSnapshot    resume_merge;  // feature:F7 resume-merge state machine
};

}  // namespace gt_esmini
