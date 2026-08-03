#pragma once

// This layer answers "which lane(s) of each road on the route must the ego occupy to stay on
// the route past the next road/junction transition?" esmini's Route::AddWaypoint (via
// RoadPath::Calculate) auto-inserts the intermediate road-level waypoints of a route -- including
// a junction's connecting road -- but RoadPath::Calculate's own lane bookkeeping only ever
// validates the SPECIFIC lane chosen at each step; the one hop that lands on the caller's target
// road skips lane validation entirely (see RoadManager.cpp RoadPath::Calculate's
// `nextRoad == targetRoad` fast path vs. the `else CheckRoad(...)` branch), so a route whose
// requested target lane needs a lane change to actually be reachable still reports success.
// RouteLanePlan recovers the missing lane-level picture by propagating backward from the route's
// final waypoint lane, road by road, keeping at each road only the lanes -- at the end where it
// connects onward -- that actually lead into the next road's (already-resolved) lane set.
//
// Both entry points are pure: they take the route/position by const reference, never mutate
// roadmanager state, and never log. Diagnosis (RouteLanePlan::diagnostic) is returned, not
// logged -- whether/how to log a degraded plan is the calling controller's call to make.

#include <string>
#include <vector>

#include "CommonMini.hpp"

namespace roadmanager
{
class Route;
class Position;
}  // namespace roadmanager

namespace gt_esmini
{

// One route road's "must be in one of these lanes" band, evaluated at the end of that road where
// it connects onward to the next road on the route (for the route's final road, at the target
// waypoint's own s instead -- see BuildRouteLanePlan).
struct RouteLaneBand
{
    id_t             road_id = ID_UNDEFINED;
    std::vector<int> lanes;                    // ascending, de-duplicated
    bool             exit_at_road_end = true;  // true: the connecting end is at s=length; false: at s=0
    double           exit_s           = 0.0;   // s of that end (0.0 or the road length)
};

// A full route lane plan: one band per road, in route (travel) order.
struct RouteLanePlan
{
    bool                       valid = false;
    std::vector<RouteLaneBand> bands;  // route (travel) order

    // Empty = normal. Otherwise a fixed-vocabulary reason the plan is missing or incomplete:
    // "no_route" / "invalid_route" / "empty_waypoints" / "link_not_found" /
    // "lane_discontinuity" / "reroute_failed" / "no_opendrive"
    // ("no_route" is never set by BuildRouteLanePlan itself -- it is reserved for a caller that
    // decides not to invoke this layer at all because the object carries no route.)
    std::string diagnostic;

    // The road at which lane-level continuity broke. Meaningful when diagnostic ==
    // "lane_discontinuity"; also left populated -- as a diagnostic breadcrumb -- when the
    // subsequent one-shot LaneIndependentRouter reroute then fails too and diagnostic becomes
    // "reroute_failed".
    id_t discontinuity_road_id = ID_UNDEFINED;

    // Whether this plan was resolved via the LaneIndependentRouter recovery pass rather than
    // directly from the route's own (minimal_waypoints_-derived) road/lane skeleton.
    bool rerouted = false;
};

// Ego position matched against a RouteLanePlan.
struct RouteLaneStatus
{
    bool             valid        = false;  // plan valid AND ego is on one of its bands' roads
    id_t             road_id      = ID_UNDEFINED;
    int              ego_lane     = 0;  // ego's current lane, normalized to the matched band's exit end
    int              ego_lane_raw = 0;  // ego's current lane before normalization
    std::vector<int> target_lanes;      // the matched band's lanes
    bool             on_target_lane     = false;  // ego_lane is one of target_lanes
    double           dist_to_connection = -1.0;   // [m] to the band's exit end; -1 = unknown
    std::string      reason;                      // empty = normal; else "no_plan" / "off_plan_road"
};

// Build a lane plan from a route's already-resolved road-level waypoints
// (route.minimal_waypoints_), propagating backward from the final waypoint's lane so that every
// band records only the lanes that actually connect onward to the next road on the route. On a
// lane-level gap the plan first tries exactly one LaneIndependentRouter reroute before giving up
// (see the .cpp for the full step-by-step). Never mutates `route`.
RouteLanePlan BuildRouteLanePlan(const roadmanager::Route& route);

// Match `pos` (never mutated) against `plan`.
RouteLaneStatus EvaluateRouteLaneStatus(const RouteLanePlan& plan, const roadmanager::Position& pos);

}  // namespace gt_esmini
