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

// IMidLongPlanner output (Phase 2). v_target as a function of route s.
struct MidLongPlannerSnapshot
{
    // (s [m], v_max [m/s]) sample pairs along the route ahead.
    std::vector<std::pair<double, double>> v_target_profile;
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
    Kind        kind  = Kind::NONE;
    double      s     = 0.0;   // route s the constraint applies at/until [m]
    double      value = 0.0;   // speed [m/s] or time [s] depending on kind
    std::string source;        // "lead_vehicle" | "traffic_light" | "stop_sign" | ...
};

// ITrafficPolicy output (Phase 3).
struct TrafficPolicySnapshot
{
    std::vector<PolicyConstraint> constraints;
    bool                          valid = false;
};

// IIndicatorPolicy output.
struct IndicatorSnapshot
{
    bool left_on  = false;
    bool right_on = false;
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

    // Override status (per domain).
    bool override_lateral      = false;
    bool override_longitudinal = false;

    // Per-layer snapshots.
    ShortPlannerSnapshot   short_plan;
    DriverModelSnapshot    driver;
    MidLongPlannerSnapshot midlong;   // Phase 2+
    TrafficPolicySnapshot  policy;    // Phase 3+
    IndicatorSnapshot      indicator;
};

}  // namespace gt_esmini
