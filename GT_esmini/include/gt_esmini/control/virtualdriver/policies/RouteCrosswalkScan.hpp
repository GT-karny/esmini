#pragma once

#include <array>
#include <vector>

namespace scenarioengine
{
class Object;
}
namespace roadmanager
{
class RMObject;
class Signal;
}

namespace gt_esmini
{

// Pure geometry helpers for the crosswalk scanner. NO engine headers — these are
// unit-tested in isolation (test_TrafficPolicies.cpp). All inputs are plain
// doubles in world coordinates / SI units. A 2-D point is {x, y}; a polygon is a
// vector of points (any winding; ray casting / distance are winding-agnostic).
//
// Deliberately SELF-CONTAINED: this namespace defines its own `Pt` and does NOT
// include ConflictPointResolver.hpp — the crosswalk footprint can be a non-convex
// outline, so it needs a general point-in-polygon (ray cast) and point-to-polygon
// distance, which the conflict corridor (convex clip) helpers do not provide.
namespace crosswalk_geom
{
using Pt = std::array<double, 2>;

// Even-odd ray-cast point-in-polygon test. Works for any simple polygon,
// including non-convex outlines. Points exactly on an edge may test either way
// (acceptable — the caller pairs this with a distance test and a hysteresis
// margin). Returns false for < 3 vertices.
bool PointInPolygon(const std::vector<Pt>& poly, double px, double py);

// Minimum Euclidean distance from (px,py) to the polygon: 0 if the point is
// inside (or on) the polygon, otherwise the least distance to any edge segment.
// Returns +inf semantics degrade to 0 for < 2 vertices (degenerate).
double DistanceToPolygon(const std::vector<Pt>& poly, double px, double py);

// Axis-aligned-in-local rectangle footprint centred at (cx,cy), with its LENGTH
// axis along `heading` (radians) and WIDTH perpendicular. Returns 4 corners in
// order (front-left, front-right, rear-right, rear-left) — a closed convex quad.
std::vector<Pt> BuildBoxFootprint(double cx, double cy, double heading, double length, double width);

// Minimum absolute lateral distance from point (px,py) to the polyline `pts`,
// restricted to the arc-length window [s_lo, s_hi] measured by `s_cum` (per-point
// cumulative arc length, same size as `pts`). Only polyline segments whose arc
// span intersects [s_lo, s_hi] are considered; the closest point is clamped into
// the window. On success writes `lateral_abs` (the min distance) and `s_at` (the
// arc length of the closest point) and returns true. Returns false if the window
// selects no segment or the inputs are degenerate.
bool LateralOffsetToPolyline(const std::vector<Pt>& pts, const std::vector<double>& s_cum,
                             double px, double py, double s_lo, double s_hi,
                             double& lateral_abs, double& s_at);

// Arc-length span [s_entry, s_exit] where the sampled polyline (`pts` with
// per-point cumulative arc lengths `s_cum`, same size) passes through `footprint`:
//   * primary pass: point-in-polygon on each sample;
//   * s_entry is recorded SAFE-SIDE as max(0, first_inside_s - step): the first
//     inside sample overshoots the true footprint edge by up to one step PAST the
//     edge (the unsafe direction — it would erode the stop standoff), so backing
//     off one step guarantees s_entry <= the true entry;
//   * s_exit = last_inside_s + step (pad one step past the last inside sample);
//   * fallback pass (only when NO sample tests inside — footprint offset from the
//     sampled line or narrower than the sampling): samples with
//     DistanceToPolygon < fallback_radius count as inside, same span rules.
//     fallback_radius is a separate knob (NOT the sampling step) so raising the
//     step cannot widen near-miss lateral false positives.
// If the polyline crosses the footprint more than once (possible with a concave
// outline or a curving path), the crossings are MERGED into one span from the
// first entry to the last exit. Returns true when a span was found.
bool ComputeRouteSpan(const std::vector<Pt>& pts, const std::vector<double>& s_cum,
                      const std::vector<Pt>& footprint, double step, double fallback_radius,
                      double& s_entry, double& s_exit);

}  // namespace crosswalk_geom

// One sample of the ego's walked route: world XY and how far ahead of the ego
// (route arc length) the sample sits.
struct RoutePathPoint
{
    double x     = 0.0;
    double y     = 0.0;
    double s_cum = 0.0;  // distance ahead of the ego along its route [m]
};

// A crosswalk the ego route passes through, with everything the policy needs.
struct ScannedCrosswalk
{
    roadmanager::RMObject*        object    = nullptr;
    unsigned int                  road_id   = 0;      // OpenDRIVE road the object lives on
    double                        s_entry   = 0.0;    // ego-route arc length entering the footprint [m]
    double                        s_exit    = 0.0;    // ego-route arc length leaving the footprint  [m]
    std::vector<crosswalk_geom::Pt> footprint;        // world-XY polygon (outline corners or box)
    // Nearest dynamic pedestrian signal (type "1000002") on the SAME road within
    // signal_link_radius of the object s. nullptr if none / not a dynamic signal.
    roadmanager::Signal*          ped_signal = nullptr;
};

// Result of one crosswalk scan: the walked ego path (for lateral-offset / passage
// band queries) and the crosswalks found ahead, sorted by s_entry.
struct CrosswalkScanResult
{
    std::vector<RoutePathPoint>   ego_path;
    std::vector<ScannedCrosswalk> crosswalks;
};

// Walk the ego's route forward (Duplicate + CopyRoute + MoveAlongS, the same
// idiom as RouteSignalScan / ConflictPointResolver::PredictPath) up to `lookahead`
// metres, recording every sample into ego_path. For each road the walk touches,
// collect OpenDRIVE objects of type CROSSWALK, build their world footprint once,
// and determine the ego-route arc-length span [s_entry, s_exit] where the walked
// path passes through the footprint (point-in-polygon on the samples, with a
// distance fallback for footprints offset from the lane centreline). Attaches the
// nearest linked pedestrian signal. Only crosswalks with s_entry <= lookahead are
// kept; results are sorted by s_entry.
CrosswalkScanResult ScanCrosswalksAhead(scenarioengine::Object* ego, double lookahead, double step,
                                        double signal_link_radius);

}  // namespace gt_esmini
