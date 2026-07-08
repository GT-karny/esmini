#include "gt_esmini/control/virtualdriver/policies/RouteCrosswalkScan.hpp"

#include "Entities.hpp"
#include "RoadManager.hpp"
#include "gt_esmini/road/OdrSideModel.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <unordered_set>

using namespace scenarioengine;

namespace gt_esmini
{

// ─────────────────────────── pure geometry helpers ────────────────────────────
namespace crosswalk_geom
{
bool PointInPolygon(const std::vector<Pt>& poly, double px, double py)
{
    const size_t n = poly.size();
    if (n < 3) return false;

    // Even-odd ray casting: cast a ray in +x and count edge crossings. Works for
    // convex and non-convex simple polygons alike.
    bool inside = false;
    for (size_t i = 0, j = n - 1; i < n; j = i++)
    {
        const double xi = poly[i][0], yi = poly[i][1];
        const double xj = poly[j][0], yj = poly[j][1];
        // Does the horizontal ray at py cross edge (j->i)?
        const bool straddles = (yi > py) != (yj > py);
        if (straddles)
        {
            const double x_cross = xi + (py - yi) * (xj - xi) / (yj - yi);
            if (px < x_cross) inside = !inside;
        }
    }
    return inside;
}

namespace
{
// Distance from point P to segment AB (both endpoints world XY).
double DistancePointSegment(double px, double py, double ax, double ay, double bx, double by)
{
    const double dx = bx - ax;
    const double dy = by - ay;
    const double len2 = dx * dx + dy * dy;
    if (len2 < 1.0e-18)
        return std::sqrt((px - ax) * (px - ax) + (py - ay) * (py - ay));  // degenerate segment
    double t = ((px - ax) * dx + (py - ay) * dy) / len2;
    t = std::max(0.0, std::min(1.0, t));
    const double cx = ax + t * dx;
    const double cy = ay + t * dy;
    return std::sqrt((px - cx) * (px - cx) + (py - cy) * (py - cy));
}
}  // namespace

double DistanceToPolygon(const std::vector<Pt>& poly, double px, double py)
{
    const size_t n = poly.size();
    if (n < 2) return 0.0;
    if (n >= 3 && PointInPolygon(poly, px, py)) return 0.0;

    double best = std::numeric_limits<double>::infinity();
    for (size_t i = 0, j = n - 1; i < n; j = i++)
        best = std::min(best, DistancePointSegment(px, py, poly[j][0], poly[j][1], poly[i][0], poly[i][1]));
    return best;
}

std::vector<Pt> BuildBoxFootprint(double cx, double cy, double heading, double length, double width)
{
    const double c = std::cos(heading);
    const double s = std::sin(heading);
    const double hl = 0.5 * length;  // half length (along heading)
    const double hw = 0.5 * width;   // half width  (perpendicular, left = +90°)
    // Local corner offsets (u along heading, v to the left), then rotate to world.
    auto world = [&](double u, double v) -> Pt {
        return {cx + u * c - v * s, cy + u * s + v * c};
    };
    return {
        world(+hl, +hw),  // front-left
        world(+hl, -hw),  // front-right
        world(-hl, -hw),  // rear-right
        world(-hl, +hw),  // rear-left
    };
}

bool LateralOffsetToPolyline(const std::vector<Pt>& pts, const std::vector<double>& s_cum,
                             double px, double py, double s_lo, double s_hi,
                             double& lateral_abs, double& s_at)
{
    const size_t n = pts.size();
    if (n < 2 || s_cum.size() != n || s_hi < s_lo) return false;

    double best      = std::numeric_limits<double>::infinity();
    double best_s    = 0.0;
    bool   any       = false;

    for (size_t i = 0; i + 1 < n; ++i)
    {
        const double sa = s_cum[i];
        const double sb = s_cum[i + 1];
        const double seg_lo = std::min(sa, sb);
        const double seg_hi = std::max(sa, sb);
        // Skip segments whose arc span does not overlap the query window.
        if (seg_hi < s_lo || seg_lo > s_hi) continue;

        const double ax = pts[i][0], ay = pts[i][1];
        const double bx = pts[i + 1][0], by = pts[i + 1][1];
        const double dx = bx - ax;
        const double dy = by - ay;
        const double len2 = dx * dx + dy * dy;

        double t;  // param of the closest point along the segment
        if (len2 < 1.0e-18)
            t = 0.0;
        else
            t = ((px - ax) * dx + (py - ay) * dy) / len2;
        t = std::max(0.0, std::min(1.0, t));

        // Clamp the closest point into the arc-length window as well: convert the
        // window bounds to segment params and intersect. (Arc length grows/shrinks
        // linearly with t over a straight segment.)
        const double s_at_t0 = sa;
        const double s_at_t1 = sb;
        const double ds      = s_at_t1 - s_at_t0;
        if (std::fabs(ds) > 1.0e-12)
        {
            const double t_at_lo = (s_lo - s_at_t0) / ds;
            const double t_at_hi = (s_hi - s_at_t0) / ds;
            const double tw_lo   = std::max(0.0, std::min(t_at_lo, t_at_hi));
            const double tw_hi   = std::min(1.0, std::max(t_at_lo, t_at_hi));
            if (tw_lo <= tw_hi)
                t = std::max(tw_lo, std::min(tw_hi, t));
        }

        const double cx = ax + t * dx;
        const double cy = ay + t * dy;
        const double d  = std::sqrt((px - cx) * (px - cx) + (py - cy) * (py - cy));
        if (d < best)
        {
            best   = d;
            best_s = s_at_t0 + t * ds;
            any    = true;
        }
    }

    if (!any) return false;
    lateral_abs = best;
    s_at        = best_s;
    return true;
}

bool ComputeRouteSpan(const std::vector<Pt>& pts, const std::vector<double>& s_cum,
                      const std::vector<Pt>& footprint, double step, double fallback_radius,
                      double& s_entry, double& s_exit)
{
    const size_t n = pts.size();
    if (n == 0 || s_cum.size() != n || footprint.size() < 3) return false;

    // Shared span accumulator: multiple crossings merge into [first entry, last exit].
    bool have = false;
    auto accumulate = [&](double sample_s) {
        if (!have)
        {
            // SAFE-SIDE entry: the first inside sample can overshoot the true edge
            // by up to one step in the unsafe direction; back off one step.
            s_entry = std::max(0.0, sample_s - step);
            s_exit  = sample_s + step;
            have    = true;
        }
        else
        {
            s_exit = sample_s + step;
        }
    };

    // Primary pass: point-in-polygon on each sample.
    for (size_t i = 0; i < n; ++i)
        if (PointInPolygon(footprint, pts[i][0], pts[i][1]))
            accumulate(s_cum[i]);

    // Fallback pass (footprint offset from the sampled line / narrower than the
    // sampling): distance test with the DECOUPLED radius, same span rules.
    if (!have)
    {
        for (size_t i = 0; i < n; ++i)
            if (DistanceToPolygon(footprint, pts[i][0], pts[i][1]) < fallback_radius)
                accumulate(s_cum[i]);
    }

    return have;
}
}  // namespace crosswalk_geom

// ─────────────────────────── scan implementation ──────────────────────────────
namespace
{
using crosswalk_geom::Pt;

// Build the world-XY footprint polygon of a crosswalk object. Prefer a closed
// outline with >= 3 corners (corner world coords via GetPos); otherwise fall back
// to a box from (s, t, hdg, length, width). World center + heading of the box are
// re-derived here from the road geometry — pos.SetTrackPos(road, s, t) → GetX/GetY
// and GetHRoad() — exactly as RoadManager.cpp derives an object's world pose when
// parsing (SetTrackPos then GetX/GetY/GetHRoad; object world heading = road heading
// at (s,t) + object hdg offset). See RoadManager.cpp ~5050-5131 / RMObject ctor.
std::vector<Pt> BuildFootprint(roadmanager::RMObject* obj, unsigned int road_id)
{
    std::vector<Pt> poly;
    if (!obj) return poly;

    // (1) Closed outline with enough corners → use the exact authored polygon.
    for (unsigned int oi = 0; oi < obj->GetNumberOfOutlines(); ++oi)
    {
        roadmanager::Outline* outline = obj->GetOutline(oi);
        if (!outline || !outline->closed_) continue;
        if (outline->corner_.size() < 3) continue;

        poly.reserve(outline->corner_.size());
        for (roadmanager::OutlineCorner* corner : outline->corner_)
        {
            if (!corner) continue;
            double x = 0.0, y = 0.0, z = 0.0;
            corner->GetPos(x, y, z);  // world XY (cornerRoad and cornerLocal both resolve to world)
            poly.push_back({x, y});
        }
        if (poly.size() >= 3) return poly;
        poly.clear();  // malformed; fall through to box
    }

    // (2) Box fallback from (s, t, hdg, length, width). Derive world center +
    // heading from the road geometry (matches core object parsing).
    double length = obj->GetLength();
    double width  = obj->GetWidth();
    if (length < 1.0e-3) length = 1.0;  // guard against unspecified dims
    if (width  < 1.0e-3) width  = 1.0;

    roadmanager::Position pos;
    pos.SetTrackPos(road_id, obj->GetS(), obj->GetT());
    const double cx      = pos.GetX();
    const double cy      = pos.GetY();
    const double heading = pos.GetHRoad() + obj->GetHOffset();  // road heading + object hdg offset

    return crosswalk_geom::BuildBoxFootprint(cx, cy, heading, length, width);
}

// Nearest dynamic pedestrian signal on `road` linked to a crosswalk at object_s.
// Iterates road->GetSignal(i) DIRECTLY (not ScanSignalsAhead): pedestrian signals
// face the sidewalk, so the orientation / lane-validity filters in ScanSignalsAhead
// would reject them. Type must be "1000002" (OpenDRIVE pedestrian signal) and the
// signal must be a dynamic roadmanager::TrafficLight (castable) — a matching type
// whose cast fails is treated as "no signal" (static / unreadable).
roadmanager::Signal* FindLinkedPedSignal(roadmanager::Road* road, double object_s, double link_radius)
{
    if (!road) return nullptr;
    roadmanager::Signal* best      = nullptr;
    double               best_dist = link_radius;

    const unsigned int n = road->GetNumberOfSignals();
    for (unsigned int i = 0; i < n; ++i)
    {
        roadmanager::Signal* sig = road->GetSignal(i);
        if (!sig) continue;
        if (sig->GetType() != "1000002") continue;
        const double d = std::fabs(sig->GetS() - object_s);
        if (d > best_dist) continue;
        // Must be a dynamic signal (TrafficLight) to read a phase from.
        auto* tl = dynamic_cast<roadmanager::TrafficLight*>(sig);
        if (!tl) continue;
        // Crash-proofing: reject lights whose type/subtype combo upstream could not
        // initialize. TrafficLight::SetTrafficLightInfo (RoadManager.cpp ~429-458)
        // sets light_type_ = TYPE_UNDEFINED for unknown combos and LEAVES nr_lamps_
        // UNINITIALIZED with lamps_ empty — GetNrLamps() would return garbage and
        // GetLamp() (lamps_.at) would throw. Treat as "no linked signal": the
        // waiting rule then stays active UNGATED, which is the safe side.
        if (tl->GetTrafficLightType() == roadmanager::TrafficLightType::TYPE_UNDEFINED) continue;
        best      = sig;
        best_dist = d;
    }
    return best;
}
}  // namespace

CrosswalkScanResult ScanCrosswalksAhead(Object* ego, double lookahead, double step, double signal_link_radius)
{
    CrosswalkScanResult result;
    if (!ego) return result;

    roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();
    if (!odr) return result;

    step = std::max(0.25, step);

    // Isolated copy of the ego route (same idiom as RouteSignalScan / PredictPath).
    roadmanager::Position pos;
    pos.Duplicate(ego->pos_);
    pos.CopyRoute(ego->pos_);

    // Walk forward, recording every sample AND the set of roads the walk touches.
    // Detection here is sample-in-polygon on the world footprint, NOT per-road
    // s-window bookkeeping. So the VD-7 road-boundary residual subtlety that
    // RouteSignalScan needs (a signal in the last <step m of a road flickering in
    // and out with the sampling phase) does NOT arise: a footprint straddling a
    // road boundary is tested by the SAME world samples regardless of which road
    // the sample localizes to — we just gather candidate roads from the samples and
    // test their crosswalk objects against every sample. The only residual risk is
    // a footprint fully skipped between two samples, mitigated by the distance
    // fallback in ComputeRouteSpan (DistanceToPolygon < fallback_radius).
    result.ego_path.reserve(static_cast<size_t>(lookahead / step) + 2);
    result.ego_path.push_back({pos.GetX(), pos.GetY(), 0.0});

    std::vector<unsigned int> roads_touched;
    auto noteRoad = [&roads_touched](unsigned int rid) {
        if (std::find(roads_touched.begin(), roads_touched.end(), rid) == roads_touched.end())
            roads_touched.push_back(rid);
    };
    noteRoad(pos.GetTrackId());

    double traveled = 0.0;
    while (traveled < lookahead)
    {
        // Deterministic junction choice: the convenience MoveAlongS(ds) overload
        // passes junctionSelectorAngle = -1.0, which RANDOMIZES the connecting road
        // when the position has no valid route (Position::MoveToConnectingRoad,
        // RoadManager.cpp ~10156-10159). A routeless ego would then re-roll the
        // branch EVERY frame, so a crosswalk on one branch would flicker in and out
        // of the scan and chatter the latch via the not-found release. Call the
        // explicit overload with junctionSelectorAngle = 0.0 = the straight-most
        // exit (smallest |heading difference| vs. driving direction). A valid route
        // still takes precedence: MoveToConnectingRoad checks GetRoute()->IsValid()
        // FIRST (~10081) and only falls back to the angle selector without one.
        // Remaining args match the convenience overload's defaults
        // (dLaneOffset 0, actualDistance true, HEADING_DIRECTION, updateRoute true).
        const int ret = static_cast<int>(pos.MoveAlongS(step,
                                                        0.0,
                                                        0.0,  // junctionSelectorAngle: straight-most, deterministic
                                                        true,
                                                        roadmanager::Position::MoveDirectionMode::HEADING_DIRECTION,
                                                        true));
        if (ret < 0) break;  // end of route / off-route
        traveled += step;
        result.ego_path.push_back({pos.GetX(), pos.GetY(), traveled});
        noteRoad(pos.GetTrackId());
    }

    const std::vector<RoutePathPoint>& path = result.ego_path;
    if (path.size() < 1) return result;

    // Pt + s_cum views of the walked path for the pure span helper.
    std::vector<Pt>     path_pts;
    std::vector<double> path_s;
    path_pts.reserve(path.size());
    path_s.reserve(path.size());
    for (const RoutePathPoint& p : path)
    {
        path_pts.push_back({p.x, p.y});
        path_s.push_back(p.s_cum);
    }

    // Fallback capture radius, DECOUPLED from the sampling step (a coarser step
    // must not widen near-miss lateral false positives): half the ego width plus
    // 0.5 m slack, floored at half a step.
    const double fallback_radius =
        std::max(0.5 * step, 0.5 * ego->boundingbox_.dimensions_.width_ + 0.5);

    // Collect crosswalk objects across all touched roads, deduped by (road,object).
    std::unordered_set<uint64_t> seen;  // (road_id << 32) | object_id
    for (unsigned int rid : roads_touched)
    {
        roadmanager::Road* road = odr->GetRoadById(rid);
        if (!road) continue;

        const unsigned int nobj = road->GetNumberOfObjects();
        for (unsigned int j = 0; j < nobj; ++j)
        {
            roadmanager::RMObject* obj = road->GetRoadObject(j);
            if (!obj) continue;
            if (obj->GetType() != roadmanager::RMObject::ObjectType::CROSSWALK) continue;

            // P8: invalidated (1.9) -> excluded from the VD crosswalk scan (see gt_roadmanager_patches.md P8).
            // P5-synthesized crossPath CROSSWALKs (ids >= 900000000) have NO authored object entry, so
            // GetObjectExtras returns nullptr and they are (correctly) never skipped here.
            const std::string odr_road_id =
                road->GetIdStr().empty() ? std::to_string(road->GetId()) : road->GetIdStr();
            const gt_esmini::odr::OdrObjectExtras* ox =
                gt_esmini::odr::GetObjectExtras(odr, odr_road_id, std::to_string(obj->GetId()));
            if (ox != nullptr && ox->invalidated) continue;

            const uint64_t key = (static_cast<uint64_t>(rid) << 32) | static_cast<uint64_t>(obj->GetId());
            if (!seen.insert(key).second) continue;  // already handled

            std::vector<Pt> footprint = BuildFootprint(obj, rid);
            if (footprint.size() < 3) continue;

            // Ego-route arc-length span [s_entry, s_exit] where the walked path
            // passes through the footprint (pure helper: PIP pass with safe-side
            // entry bias, distance fallback with the decoupled radius).
            double s_entry = 0.0;
            double s_exit  = 0.0;
            if (!crosswalk_geom::ComputeRouteSpan(path_pts, path_s, footprint, step, fallback_radius, s_entry, s_exit))
                continue;                           // crosswalk not on our path — skip
            if (s_entry > lookahead) continue;      // beyond horizon

            ScannedCrosswalk sc;
            sc.object     = obj;
            sc.road_id    = rid;
            sc.s_entry    = s_entry;
            sc.s_exit     = s_exit;
            sc.footprint  = std::move(footprint);
            sc.ped_signal = FindLinkedPedSignal(road, obj->GetS(), signal_link_radius);
            result.crosswalks.push_back(std::move(sc));
        }
    }

    std::sort(result.crosswalks.begin(), result.crosswalks.end(),
              [](const ScannedCrosswalk& a, const ScannedCrosswalk& b) { return a.s_entry < b.s_entry; });
    return result;
}

}  // namespace gt_esmini
