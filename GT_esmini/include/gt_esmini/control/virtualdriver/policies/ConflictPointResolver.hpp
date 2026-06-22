#pragma once

#include "gt_esmini/control/virtualdriver/ITrafficPolicy.hpp"

namespace gt_esmini
{

// Pure geometry / timing helpers for the conflict-point resolver. NO engine
// headers — these are unit-tested in isolation (test_TrafficPolicies.cpp). All
// inputs are plain doubles in world coordinates / SI units.
namespace conflict_geom
{
// Proper segment-segment intersection of AB and CD.
//   t, u  parametric position of the intersection on AB (t) and CD (u), in [0,1].
//   ix,iy world intersection point.
// Returns false for parallel/collinear segments and for intersections that fall
// outside either segment (i.e. only a *proper* crossing within both spans counts;
// endpoint touches are accepted when they land inside [0,1]).
bool SegmentsIntersect(double ax, double ay, double bx, double by,
                       double cx, double cy, double dx, double dy,
                       double& t, double& u, double& ix, double& iy);

// Angle between two direction vectors (ab and cd), folded to [0,90] degrees.
// 90 = perpendicular, 0 = (anti)parallel. Used to reject near-parallel overlaps
// (those are LeadVehicleAware's job) and keep only genuine crossings.
double CrossingAngleDeg(double abx, double aby, double cdx, double cdy);

enum class GapAction
{
    PROCEED,
    YIELD
};

// The temporal gap decision (step 6 of the algorithm), kept pure for testing.
//   t_ego    ego time-to-arrive at the conflict point [s]
//   t_enter  time the other vehicle ENTERS the conflict zone [s]
//   t_exit   time the other vehicle has fully CLEARED the zone [s]
//   accept_gap  clear-time margin required to commit [s]
// YIELD when the ego would arrive while the zone is (or is about to be) occupied:
//   t_enter - accept_gap <= t_ego <= t_exit + accept_gap.
// Otherwise PROCEED (other arrives well after the ego has passed, or has already
// cleared with margin).
GapAction GapDecision(double t_ego, double t_enter, double t_exit, double accept_gap);
}  // namespace conflict_geom

// Config for ConflictPointResolver. Flat keys are surfaced in VirtualDriverConfig
// (line-parsed); this struct is the materialized form the policy consumes.
struct ConflictPointResolverConfig
{
    double lookahead           = 120.0;  // [m]   path prediction horizon (ego + others)
    double stop_margin         = 4.0;    // [m]   stop this far before the crossing point
    double zone_half           = 3.0;    // [m]   half-length of the crossing conflict zone
    double accept_gap          = 2.0;    // [s]   clear-time margin required to commit
    double min_cross_angle_deg = 20.0;   // [deg] reject near-parallel overlaps below this
    double other_min_speed     = 0.5;    // [m/s] ignore (near-)stationary others (unless already in zone)
    double step                = 2.0;    // [m]   MoveAlongS sampling resolution for both polylines
    double nominal_speed       = 5.0;    // [m/s] floor on v_ego for the arrival estimate (anti-chatter)
    double release_extra       = 1.5;    // [s]   extra clear margin a committed stop holds until (hysteresis)
};

// Phase 3d: yield at an unsignalised crossing conflict. Predicts the ego route
// polyline and each other vehicle's route polyline, finds the nearest genuine
// crossing, and (via the pure GapDecision) decides whether the oncoming/crossing
// stream occupies the conflict zone in the window the ego would traverse it. If
// any conflicting vehicle yields the ego, emits ONE STOP_AT_S a stop_margin
// before the governing crossing point; the planner handles the decel ramp + hard
// stop band (it BYPASSES the min_speed floor for STOP_AT_S).
//
// Anti-chatter (this increment): the ego arrival estimate is computed against a
// SPEED FLOOR (nominal_speed) so braking to yield doesn't blow t_ego up and flip
// the decision; and the yield decision is LATCHED with hysteresis — once the ego
// commits to a stop it HOLDS that stop until the oncoming stream has cleared by
// the wider (accept_gap + release_extra) margin, then releases once. Together
// these remove the per-frame STOP/GO limit cycle near the junction.
//
// Right-of-way *priority* (which turn must yield given the drive-side rule) is
// Phase 3 / F3; here the geometric crossing + timing gate handles detection and
// the road RoadRule is read and kept available for that future gating only.
class ConflictPointResolver : public ITrafficPolicy
{
public:
    explicit ConflictPointResolver(const ConflictPointResolverConfig& cfg = {}) : cfg_(cfg) {}
    TrafficPolicySnapshot Evaluate(const TrafficPolicyContext& ctx) override;

private:
    ConflictPointResolverConfig cfg_;

    // Yield latch (hysteresis). NOTE: this is a single-junction latch — it tracks
    // ONE active conflict at a time (the governing/nearest crossing). That is
    // acceptable for this increment (one crossing dominates the approach); a
    // multi-conflict latch would key state per conflicting entity/crossing.
    bool   committed_       = false;  // currently holding a yield stop
    double committed_stop_s_ = 0.0;   // ego route-s of the crossing being held for
};

}  // namespace gt_esmini
