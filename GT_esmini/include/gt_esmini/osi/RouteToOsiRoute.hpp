/*
 * GT_esmini extension -- osi3::Route (HostVehicleData.route) construction, and the
 * route-progress quantity (L2) that OSI has no field for.
 *
 * Design: GT_esmini/docs/osi/logical_lane_and_route_design.md 2-5 / 2-6-2 / 5
 * Spec vs. current state: GT_esmini/docs/osi/logical_lane_and_route.md 2-4 / 4-2 / 4-3
 * Knowledge graph: spine-work:osi-logical-lane (capability_model.md 2.2a W4).
 *
 * THREE LAYERS, ON PURPOSE
 * ------------------------
 *   ExpandRouteLanePlan()   RouteLanePlan bands (one per ROAD, lanes evaluated at
 *                           the road's exit end) -> one segment per LANE SECTION,
 *                           clipped to "from the ego, to the destination waypoint".
 *                           Pure roadmanager, no protobuf.
 *   ComputeRouteProgress()  L2: distance travelled along that segment list. Pure.
 *   BuildOsiRouteInto()     segments + the logical-lane index -> osi3::Route.
 *
 * The split is what lets the VirtualDriver publish L2 in its telemetry without
 * touching protobuf, and what lets the unit gate exercise the expansion on real
 * xodr without standing up an OSIReporter (S1 handover 2: testing only the
 * wrapper steps over every line that matters).
 *
 * MODULE DEPENDENCY NOTE
 * ----------------------
 * This osi-module header includes a control-module header (RouteLanePlan.hpp),
 * which GT_esmini/CLAUDE.md 2's "osi -> core, scenario" does not list. There is no
 * legal home for a function that must see both RouteLanePlan and osi3::Route:
 * core sits below both, and duplicating RouteLaneBand into the osi module would
 * create two definitions of the same route band that have to be kept in step by
 * hand. RouteLanePlan.hpp itself depends only on roadmanager -- it is a road
 * topology helper, not the control pipeline -- so what is imported here is a leaf,
 * not the module. (The file-level cycle already exists: GT_OSIReporter_Moving.cpp
 * includes control/common/TransitionDynamics.hpp.)
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "gt_esmini/control/virtualdriver/RouteLanePlan.hpp"
#include "gt_esmini/osi/GT_OsiLogicalLane.hpp"

namespace roadmanager
{
class Route;
class Position;
}  // namespace roadmanager

// Forward declaration only -- see GT_OsiLogicalLane.hpp for why these headers stay
// protobuf-free. BuildOsiRouteInto() therefore writes through an out-parameter
// instead of returning osi3::Route by value.
namespace osi3
{
class Route;
}

namespace gt_esmini
{
namespace osi
{

// One traversed lane section of the route == one osi3::Route::RouteSegment.
//
// `start_s` / `end_s` are in TRAVEL order, which is the whole trap of osi_route.proto:
// a segment driven against the reference line has start_s > end_s. LogicalLane itself
// requires the opposite (end_s > start_s). Same two doubles, opposite requirement, two
// different messages -- and under right-hand traffic the ordinary case (a negative lane
// driven with +s) is the one that lands on the "forward" side, so a sign mix-up would
// publish every route as a wrong-way route while still looking perfectly well formed.
//
// `lanes` are OpenDRIVE lane ids VALID IN `section_idx`. The band they come from holds
// ids evaluated at the road's exit end only, so they are re-resolved section by section
// through the lane links: crossing a reference-lane change inside a road renumbers them.
struct RouteSectionSegment
{
    std::uint32_t    road_id     = 0;
    unsigned         section_idx = 0;
    double           start_s     = 0.0;
    double           end_s       = 0.0;
    std::vector<int> lanes;
};

// L2 -- where the ego is along the route (design 2-6-2).
//
// NOT osi3. OSI has no field for a route-relative longitudinal offset (current state
// 2-5), so this goes to the VirtualDriver telemetry route_lane block and nowhere near
// the standard message.
//
// Deliberately NOT Position::GetRouteS(): that value is only written by SetRoute() and
// TeleportTo(), so it freezes at its assignment-time value for any physically driven
// vehicle, and the call that would refresh it (CalcRoutePosition) mutates the route's
// own path_s_/waypoint_idx_/currentPos_ -- an observer must not move what it observes
// (current state 4-3).
struct RouteProgress
{
    // [m] from the first segment's start_s. -1.0 when on_route is false: filling it
    // with 0.0 off-route would be indistinguishable from "sitting at the start", which
    // is precisely the failure mode GetRouteS() already has.
    double      s_along_route = -1.0;
    double      route_length  = 0.0;  // [m] sum of all segment lengths
    std::size_t segment_index = 0;    // index into the segment list; meaningful only when on_route
    bool        on_route      = false;
};

// Identity of a route, for the "did the route actually change" cache (design 5).
//
// NOT the Route* and NOT a clone of it: Position::CopyRoute does `route_ = new Route`
// on every call, so a clone's address is fresh each frame and an address-keyed cache
// would miss unconditionally.
struct RouteSignature
{
    std::size_t   n_waypoints = 0;
    std::uint32_t first_road  = 0;
    std::uint32_t last_road   = 0;
    int           first_lane  = 0;
    int           last_lane   = 0;
    double        first_s     = 0.0;
    double        last_s      = 0.0;

    bool operator==(const RouteSignature& o) const;
    bool operator!=(const RouteSignature& o) const
    {
        return !(*this == o);
    }
};

RouteSignature MakeRouteSignature(const roadmanager::Route& route);

// Where the expanded segment list begins. The two consumers genuinely want different
// things and merging them would make one of them wrong:
//
//   EgoPosition  the route STILL AHEAD. This is what osi3::Route publishes -- a route
//                whose first segment starts behind the vehicle describes road the agent
//                is not going to drive.
//   RouteStart   the WHOLE route, from its first waypoint. This is what L2 measures
//                against; anchored at the ego instead, s_along_route would be zero on
//                every frame by construction and route_length would shrink as the
//                vehicle drove, which is not what either name means.
//
// Everything else about the two expansions is identical, which is why it is one function
// with a mode rather than two that can drift apart.
enum class RouteExpansionStart
{
    EgoPosition,
    RouteStart
};

// Expand a road-level lane plan into the lane-section-level segment list.
//
// `ego` IS THE CALLER'S CHOICE OF REFERENCE POINT, and both shipped call sites hand in
// the OSI one -- ResolveOsiReferencePoint's bounding-box centre, not the entity origin
// (design 2-6-1). This function only reads GetTrackId()/GetS() from it, so it cannot
// tell the two apart; getting it wrong costs one centre offset (1.4 m for the shipped
// catalogue car) of route that the vehicle has in fact already driven.
//
// - The first emitted segment starts at the ego's current s (EgoPosition; at the route's
//   own first waypoint s when the ego is not on any road of the plan) or at the route's
//   first waypoint s (RouteStart).
// - The last emitted segment ends at plan.bands.back().exit_s, which is the destination
//   waypoint's s.
// - In EgoPosition mode, roads the ego has already left are dropped.
//
// Returns empty for an invalid/empty plan, when no OpenDRIVE is loaded, or (EgoPosition)
// when the ego is already past the destination. Never mutates `route`, `ego` or
// roadmanager state, and never logs.
std::vector<RouteSectionSegment> ExpandRouteLanePlan(const roadmanager::Route&    route,
                                                    const roadmanager::Position& ego,
                                                    const RouteLanePlan&         plan,
                                                    RouteExpansionStart          start);

// L2. `ego` is matched against the segment list by road id and s range. Feed it a
// RouteStart expansion -- an EgoPosition one always puts the ego on the first segment's
// own start, i.e. s_along_route == 0 forever. Same reference-point contract as
// ExpandRouteLanePlan: hand in the OSI reference point, so that L2 and the
// LogicalLaneAssignment L1 that composes with it measure the same physical point.
RouteProgress ComputeRouteProgress(const std::vector<RouteSectionSegment>& segments, const roadmanager::Position& ego);

// segments + (road, section, lane) -> logical lane id index  ->  osi3::Route.
//
// `out` is cleared first. A segment whose lanes resolve to no logical lane at all is
// skipped rather than emitted empty: an OSI RouteSegment with no lane_segment says
// "there is space here but no lane", which is a different and false statement from
// "this stretch is not described". Lanes that individually fail to resolve are dropped
// from their segment, which is what keeps every emitted logical_lane_id a live
// reference into logical_lane[] (design 9, closure test).
void BuildOsiRouteInto(const std::vector<RouteSectionSegment>& segments,
                       const LogicalLaneIndex&                 index,
                       std::uint64_t                           route_id,
                       osi3::Route*                            out);

}  // namespace osi
}  // namespace gt_esmini
