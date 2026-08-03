#pragma once

#include <array>
#include <string>
#include <utility>
#include <vector>

#include "gt_esmini/control/virtualdriver/ITrafficPolicy.hpp"

namespace gt_esmini
{

// ─────────────────────────── junction priority (F3) ───────────────────────────
// Pure right-of-way resolution from OpenDRIVE <junction><priority high low>. NO
// engine headers — unit-tested in isolation (test_TrafficPolicies.cpp). The
// engine-coupled part (finding each vehicle's upcoming connecting road id + the
// shared junction) lives in the .cpp; this is the decision core it feeds.
namespace junction_priority
{
// Right-of-way of the EGO relative to ONE other vehicle at a shared junction.
enum class Relation
{
    UNKNOWN,        // no <priority> entry relates the two connecting roads (fall back to yield)
    EGO_PRIORITY,   // ego's connecting road is HIGH over the other's LOW -> ego proceeds
    OTHER_PRIORITY  // the other's connecting road is HIGH over the ego's LOW -> ego yields
};

// Resolve the ego↔other right-of-way from the junction's authored <priority> list.
// `ego_conn` / `other_conn` are the AUTHORED connecting-road id strings the two
// vehicles traverse through the junction; `high_low` is the (high, low) connecting
// road id pairs from every <priority high low> element on that junction.
//   * an entry (high==ego_conn, low==other_conn)   -> EGO_PRIORITY
//   * an entry (high==other_conn, low==ego_conn)   -> OTHER_PRIORITY
//   * neither (or empty ids / empty list)          -> UNKNOWN
// EGO_PRIORITY wins if any entry grants it (deterministic scan; conflicting
// authoring where both directions appear is degenerate and resolves to whichever
// the scan meets first — EGO_PRIORITY is checked per entry). Same-id or empty
// inputs never match.
Relation Resolve(const std::string&                                    ego_conn,
                 const std::string&                                    other_conn,
                 const std::vector<std::pair<std::string, std::string>>& high_low);
}  // namespace junction_priority

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

// Signed forward distance of point P past a reference point R, measured along a
// FIXED axis (ax, ay). = dot(P - R, axis) / |axis|. This is the positional-release
// primitive: the governing other's origin projected onto the exit tangent it left
// the conflict region along — robust to the other turning after the exit (its live
// heading would mis-project). Returns 0 for a degenerate axis.
double ForwardDistanceAlong(double px, double py, double rx, double ry,
                            double ax, double ay);

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
    double area_eps            = 0.10;   // [m^2] min clipped quad-pair area to call it a conflict

    // F3 junction priority (default OFF). When ON, a governing conflict against an
    // other the ego OUT-RANKS (ego's upcoming connecting road is HIGH over the
    // other's LOW in the shared junction's <priority> list) is NOT yielded to —
    // the ego proceeds. Others the ego does NOT out-rank (OTHER_PRIORITY /
    // UNKNOWN / no priority data / different junction) keep the base yield
    // behaviour. Requires the OpenDRIVE side model (P5) to carry <priority>.
    bool   junction_priority_enabled = false;
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
// Right-of-way *priority* (F3): when cfg_.junction_priority_enabled the resolver
// consults the shared junction's OpenDRIVE <priority high low> list (via the P5
// side model) and does NOT yield to a governing other the ego out-ranks (ego's
// upcoming connecting road HIGH over the other's LOW) — the ego proceeds. Others
// the ego does not out-rank keep the base yield. With the flag OFF (default) the
// ego, as the turning/crossing vehicle, always yields to oncoming/crossing traffic
// exactly as before. Junctions without <priority> data fall back to the base yield.
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
    int    committed_other_id_     = -1;     // governing oncoming entity id (scenario index; used to re-find the object)
    // The same governing other in the OSI id space (control/common/OsiIdentity.hpp;
    // -1 == gt_esmini::kNoOsiId, spelled literally here because this header stays
    // engine-header-free for the pure-decision unit tests). Captured at commit
    // rather than resolved at emit time, so the diagnostic still names the partner
    // on a frame where the entity has already left ctx.entities.
    int    committed_other_osi_id_ = -1;
    // World XY of the governing other's region EXIT (the far edge of the conflict
    // region along the other's route). The hold releases once the other has driven
    // its body past this point by `release_buffer` — a robust positional test that
    // does not depend on the region staying inside the other's forward prediction.
    double committed_exit_x_       = 0.0;
    double committed_exit_y_       = 0.0;
    // Unit tangent of the other's predicted PATH at the exit sample (its direction
    // of travel THROUGH the exit), captured at commit and refreshed while the
    // region is still found. The positional release projects the other's
    // displacement-past-exit onto THIS fixed tangent, not the other's instantaneous
    // heading — so a vehicle that turns at/after the conflict is still measured
    // as clearing along the direction it actually left the region.
    double committed_exit_tx_      = 1.0;
    double committed_exit_ty_      = 0.0;
};

}  // namespace gt_esmini
