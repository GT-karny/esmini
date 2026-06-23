#pragma once

#include <array>
#include <vector>

#include "gt_esmini/control/virtualdriver/ITrafficPolicy.hpp"

namespace gt_esmini
{

// Pure geometry helpers for the conflict-corridor resolver. NO engine headers —
// these are unit-tested in isolation (test_TrafficPolicies.cpp). All inputs are
// plain doubles in world coordinates / SI units. A 2-D point is {x, y}; a polygon
// is a CCW (or CW — area is taken absolute) vector of points.
namespace conflict_geom
{
using Pt = std::array<double, 2>;

// Angle between two direction vectors (ab and cd), folded to [0,90] degrees.
// 90 = perpendicular, 0 = (anti)parallel. Used by the same-direction filter to
// reject near-parallel overlaps (those are LeadVehicleAware's job) and keep only
// genuine crossings.
double CrossingAngleDeg(double abx, double aby, double cdx, double cdy);

// Proper segment-segment intersection of AB and CD. Retained as a small,
// unit-tested primitive (the corridor model uses ConvexClip for the conflict
// region, but this is still handy for cheap pre-checks / tests).
//   t, u  parametric position of the intersection on AB (t) and CD (u), in [0,1].
//   ix,iy world intersection point.
// Returns false for parallel/collinear segments and for intersections that fall
// outside either segment.
bool SegmentsIntersect(double ax, double ay, double bx, double by,
                       double cx, double cy, double dx, double dy,
                       double& t, double& u, double& ix, double& iy);

// Sutherland–Hodgman polygon clipping. `clip` MUST be convex; `subject` may be
// any simple polygon (here always a convex quad). Returns the clipped polygon
// (the part of `subject` inside `clip`), which is empty when they are disjoint.
// Because both corridor quads are convex, the result is the EXACT intersection
// polygon. Vertex winding of `clip` is auto-detected so the half-plane tests use
// the correct inside sign.
std::vector<Pt> ConvexClip(const std::vector<Pt>& subject, const std::vector<Pt>& clip);

// Shoelace area of a polygon, absolute value (winding-agnostic). 0 for < 3 verts.
double PolygonArea(const std::vector<Pt>& poly);

}  // namespace conflict_geom

// Config for ConflictPointResolver. Flat keys are surfaced in VirtualDriverConfig
// (line-parsed); this struct is the materialized form the policy consumes. The
// model is a width-inflated path CORRIDOR (polygon ribbon) per vehicle; the
// conflict REGION is the TRUE polygon intersection of the ego corridor with an
// oncoming corridor, and timing reasons about WHEN each length-aware body
// occupies that region.
struct ConflictPointResolverConfig
{
    double lookahead           = 120.0;  // [m]   path prediction horizon (ego + others)
    double step                = 1.0;    // [m]   corridor sampling resolution (arc fidelity)
    double lane_margin         = 0.25;   // [m]   lateral safety added to each half-width
    double standoff            = 5.0;    // [m]   stop this far before the region entry
    double release_buffer      = 3.0;    // [m]   extra travel past the region exit before release
    double pet                 = 1.5;    // [s]   post-encroachment safety time
    double nominal_speed       = 5.0;    // [m/s] floor on v_ego for the arrival estimate (anti-chatter)
    double min_cross_angle_deg = 20.0;   // [deg] same-direction filter (reject near-parallel)
    double other_min_speed     = 0.5;    // [m/s] ignore (near-)stationary others not yet at their region
};

// Phase 3d (F2): yield at an unsignalised crossing conflict, modelled as a
// space-time corridor occupancy problem.
//
//   * Each vehicle's future motion is a width-inflated path CORRIDOR — a ribbon
//     of convex quads, one per predicted path segment, offset ±(half_width +
//     lane_margin) perpendicular to the segment.
//   * The conflict REGION is the TRUE polygon intersection of the ego corridor
//     and an oncoming corridor (an AREA, found by clipping every ego-quad against
//     every oncoming-quad with Sutherland–Hodgman; a pair conflicts when the
//     clipped area > eps). The cluster nearest the ego gives the region's ego
//     arc-length span [se_in, se_out] and the other's span [so_in, so_out].
//   * Timing is length-aware and constant-speed: the other's body occupies the
//     region over [t_onc_in, t_onc_out]; the ego (floored at nominal_speed for
//     anti-chatter) would occupy [t_ego_in, t_ego_out]. A conflict exists when
//     those windows overlap with a post-encroachment-time pad (pet).
//
// On conflict the resolver LATCHES and emits ONE STOP_AT_S a `standoff` before
// the region entry (se_in). The RELEASE is POSITIONAL/physical: the hold is held
// until the governing oncoming's body has travelled past its region exit (so_out)
// by `release_buffer` along its own route — robust to the ego crawling and to the
// timing windows collapsing on final approach. Crawl is allowed (the planner may
// bottom out ~1-2 m/s); the standoff is sized so the ego footprint never enters
// the region while the oncoming body is in it.
//
// Right-of-way *priority* (which turn must yield given the drive-side rule) is
// Phase 3 / F3; here the ego, as the turning/crossing vehicle, always yields to
// oncoming through-traffic. The road RoadRule is read and kept available for F3
// gating only (it does NOT change behaviour in this increment).
class ConflictPointResolver : public ITrafficPolicy
{
public:
    explicit ConflictPointResolver(const ConflictPointResolverConfig& cfg = {}) : cfg_(cfg) {}
    TrafficPolicySnapshot Evaluate(const TrafficPolicyContext& ctx) override;

private:
    ConflictPointResolverConfig cfg_;

    // Yield latch (positional release). Single-junction: tracks ONE active
    // conflict at a time (the governing/nearest region). Acceptable for this
    // increment — one crossing dominates the approach.
    bool   committed_              = false;  // currently holding a yield stop
    double committed_stop_s_       = 0.0;    // ego route-s to the governing region entry (refreshed while held)
    int    committed_other_id_     = -1;     // governing oncoming entity id
    // World XY of the governing other's region EXIT (the far edge of the conflict
    // region along the other's route). The hold releases once the other has driven
    // its body past this point by `release_buffer` — a robust positional test that
    // does not depend on the region staying inside the other's forward prediction.
    double committed_exit_x_       = 0.0;
    double committed_exit_y_       = 0.0;
};

}  // namespace gt_esmini
