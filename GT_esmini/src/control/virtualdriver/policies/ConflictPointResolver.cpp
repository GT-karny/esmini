#include "gt_esmini/control/virtualdriver/policies/ConflictPointResolver.hpp"

#include "Entities.hpp"
#include "RoadManager.hpp"
#include "gt_esmini/road/OdrSideModel.hpp"  // F3: GetJunctionPriorities (P5 side model)

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <vector>

using namespace scenarioengine;

namespace gt_esmini
{

// ─────────────────────────── junction priority (F3) ───────────────────────────
namespace junction_priority
{
Relation Resolve(const std::string&                                      ego_conn,
                 const std::string&                                      other_conn,
                 const std::vector<std::pair<std::string, std::string>>& high_low)
{
    // Empty / identical connecting-road ids can never form a HIGH↔LOW relation.
    if (ego_conn.empty() || other_conn.empty() || ego_conn == other_conn)
        return Relation::UNKNOWN;

    for (const auto& hl : high_low)
    {
        const std::string& high = hl.first;
        const std::string& low  = hl.second;
        if (high == ego_conn && low == other_conn)
            return Relation::EGO_PRIORITY;
        if (high == other_conn && low == ego_conn)
            return Relation::OTHER_PRIORITY;
    }
    return Relation::UNKNOWN;
}
}  // namespace junction_priority

// ─────────────────────────── pure geometry helpers ────────────────────────────
namespace conflict_geom
{
double CrossingAngleDeg(double abx, double aby, double cdx, double cdy)
{
    const double n1 = std::sqrt(abx * abx + aby * aby);
    const double n2 = std::sqrt(cdx * cdx + cdy * cdy);
    if (n1 < 1.0e-12 || n2 < 1.0e-12)
        return 0.0;
    double cosang = (abx * cdx + aby * cdy) / (n1 * n2);
    cosang        = std::max(-1.0, std::min(1.0, cosang));
    double deg    = std::acos(cosang) * 180.0 / M_PI;  // [0,180]
    if (deg > 90.0) deg = 180.0 - deg;                 // fold to [0,90]
    return deg;
}

bool SegmentsIntersect(double ax, double ay, double bx, double by,
                       double cx, double cy, double dx, double dy,
                       double& t, double& u, double& ix, double& iy)
{
    const double rx = bx - ax;  // AB direction
    const double ry = by - ay;
    const double sx = dx - cx;  // CD direction
    const double sy = dy - cy;

    const double denom = rx * sy - ry * sx;  // cross(r, s)
    if (std::fabs(denom) < 1.0e-12)
        return false;  // parallel or collinear -> no proper crossing

    const double qpx = cx - ax;
    const double qpy = cy - ay;

    t = (qpx * sy - qpy * sx) / denom;  // along AB
    u = (qpx * ry - qpy * rx) / denom;  // along CD

    if (t < 0.0 || t > 1.0 || u < 0.0 || u > 1.0)
        return false;

    ix = ax + t * rx;
    iy = ay + t * ry;
    return true;
}

double PolygonArea(const std::vector<Pt>& poly)
{
    const size_t n = poly.size();
    if (n < 3) return 0.0;
    double acc = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const Pt& p = poly[i];
        const Pt& q = poly[(i + 1) % n];
        acc += p[0] * q[1] - q[0] * p[1];  // shoelace cross terms
    }
    return std::fabs(acc) * 0.5;
}

double ForwardDistanceAlong(double px, double py, double rx, double ry,
                            double ax, double ay)
{
    const double n = std::sqrt(ax * ax + ay * ay);
    if (n < 1.0e-12) return 0.0;
    return ((px - rx) * ax + (py - ry) * ay) / n;  // dot(P-R, axis) / |axis|
}

namespace
{
// Signed area * 2 (keeps the winding sign) — used to orient the clip polygon.
double SignedArea2(const std::vector<Pt>& poly)
{
    const size_t n = poly.size();
    if (n < 3) return 0.0;
    double acc = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const Pt& p = poly[i];
        const Pt& q = poly[(i + 1) % n];
        acc += p[0] * q[1] - q[0] * p[1];
    }
    return acc;
}
}  // namespace

std::vector<Pt> ConvexClip(const std::vector<Pt>& subject, const std::vector<Pt>& clip)
{
    if (subject.size() < 3 || clip.size() < 3) return {};

    // Inputs are convex quads (4 verts) and each Sutherland–Hodgman pass adds at
    // most one vertex per clip edge, so the working polygon never exceeds a small
    // bound. Use fixed-capacity stack buffers (no per-call heap churn — this runs
    // O(ego_quads * other_quads) times per vehicle per frame).
    constexpr size_t kCap = 16;
    if (subject.size() > kCap) return {};  // defensive; corridor quads are always 4

    // Orient the clip polygon CCW so that "inside" is consistently the LEFT side
    // of each directed clip edge (cross >= 0). Sutherland–Hodgman requires a
    // convex clip polygon (the corridor quads are convex).
    std::array<Pt, kCap> clip_ccw;
    const size_t         cn = clip.size();
    if (cn > kCap) return {};
    std::copy(clip.begin(), clip.end(), clip_ccw.begin());
    if (SignedArea2(clip) < 0.0)
        std::reverse(clip_ccw.begin(), clip_ccw.begin() + cn);

    std::array<Pt, kCap> output;
    size_t               out_n = subject.size();
    std::copy(subject.begin(), subject.end(), output.begin());

    std::array<Pt, kCap> input;

    for (size_t e = 0; e < cn && out_n > 0; ++e)
    {
        const Pt& A = clip_ccw[e];
        const Pt& B = clip_ccw[(e + 1) % cn];
        const double ex = B[0] - A[0];
        const double ey = B[1] - A[1];

        // inside(P) = cross(edge, A->P) >= 0  (left side of a CCW edge)
        auto inside = [&](const Pt& p) -> double {
            return ex * (p[1] - A[1]) - ey * (p[0] - A[0]);
        };
        // Intersection of segment P->Q with the (infinite) clip edge line.
        auto intersect = [&](const Pt& p, const Pt& q) -> Pt {
            const double dpx = q[0] - p[0];
            const double dpy = q[1] - p[1];
            const double denom = ex * dpy - ey * dpx;  // edge x (P->Q)
            if (std::fabs(denom) < 1.0e-15)
                return p;  // (near-)parallel; degenerate — return P
            // Solve A + s*edge = P + tt*(Q-P) for tt (reuse denom).
            const double tt = -(ex * (p[1] - A[1]) - ey * (p[0] - A[0])) / denom;
            return {p[0] + tt * dpx, p[1] + tt * dpy};
        };

        const size_t in = out_n;
        std::copy(output.begin(), output.begin() + in, input.begin());
        out_n = 0;
        for (size_t i = 0; i < in; ++i)
        {
            const Pt& cur  = input[i];
            const Pt& prev = input[(i + in - 1) % in];
            const bool cur_in  = inside(cur) >= 0.0;
            const bool prev_in = inside(prev) >= 0.0;
            if (cur_in)
            {
                if (!prev_in && out_n < kCap)
                    output[out_n++] = intersect(prev, cur);
                if (out_n < kCap) output[out_n++] = cur;
            }
            else if (prev_in && out_n < kCap)
            {
                output[out_n++] = intersect(prev, cur);
            }
        }
    }
    return std::vector<Pt>(output.begin(), output.begin() + out_n);
}
}  // namespace conflict_geom

namespace
{
using conflict_geom::Pt;

// One sample of a vehicle's predicted route polyline.
struct PathPoint
{
    double x     = 0.0;
    double y     = 0.0;
    double s_cum = 0.0;  // cumulative route distance from the start [m]
};

// Axis-aligned bounding box, for cheap overlap pre-rejects before the exact clip.
struct Aabb
{
    double xmin =  std::numeric_limits<double>::infinity();
    double ymin =  std::numeric_limits<double>::infinity();
    double xmax = -std::numeric_limits<double>::infinity();
    double ymax = -std::numeric_limits<double>::infinity();

    void Expand(double x, double y)
    {
        xmin = std::min(xmin, x); xmax = std::max(xmax, x);
        ymin = std::min(ymin, y); ymax = std::max(ymax, y);
    }
    bool Overlaps(const Aabb& o) const
    {
        return xmin <= o.xmax && o.xmin <= xmax && ymin <= o.ymax && o.ymin <= ymax;
    }
};

// One corridor quad: the width-inflated ribbon segment from path[i] to path[i+1],
// tagged with the path arc-length span it covers.
struct CorridorQuad
{
    std::vector<Pt> poly;  // 4 verts (CCW-ish), the inflated segment
    double          s0 = 0.0;
    double          s1 = 0.0;
    Aabb            box;   // AABB of poly (per-quad pre-reject)
};

// Whole-corridor AABB (union of every quad box) — coarse pre-reject.
Aabb CorridorBounds(const std::vector<CorridorQuad>& corr)
{
    Aabb b;
    for (const auto& q : corr)
    {
        b.xmin = std::min(b.xmin, q.box.xmin); b.xmax = std::max(b.xmax, q.box.xmax);
        b.ymin = std::min(b.ymin, q.box.ymin); b.ymax = std::max(b.ymax, q.box.ymax);
    }
    return b;
}

// Walk a vehicle's route forward into a {x, y, s_cum} polyline, mirroring the
// RouteSignalScan idiom (isolated Position copy + CopyRoute + MoveAlongS). Stops
// at end/off-route (MoveAlongS < 0) or once `lookahead` metres are covered.
std::vector<PathPoint> PredictPath(Object* obj, double lookahead, double step)
{
    std::vector<PathPoint> out;
    if (!obj) return out;

    step = std::max(0.25, step);
    out.reserve(static_cast<size_t>(lookahead / step) + 2);  // samples ≈ lookahead/step + start

    roadmanager::Position pos;
    pos.Duplicate(obj->pos_);
    pos.CopyRoute(obj->pos_);

    out.push_back({pos.GetX(), pos.GetY(), 0.0});

    double traveled = 0.0;
    while (traveled < lookahead)
    {
        // [Issue #31] junctionSelectorAngle = 0.0 (straight-most, deterministic), NOT the
        // convenience overload's -1.0 which RANDOMIZES the connecting road whenever the
        // isolated prediction is off-route (on_route_ == false). Random selection makes the
        // ego corridor flicker between the straight and turning connectors frame-to-frame
        // (Issue #31 "straight/left Traj alternation"). A valid on-route route still wins
        // inside MoveToConnectingRoad. Mirrors RouteCrosswalkScan / FindUpcomingConnectingRoad.
        const int ret = static_cast<int>(pos.MoveAlongS(step, 0.0, 0.0, true,
                                                        roadmanager::Position::MoveDirectionMode::HEADING_DIRECTION, true));
        if (ret < 0) break;  // end of route / off-route
        traveled += step;
        out.push_back({pos.GetX(), pos.GetY(), traveled});
    }
    return out;
}

// ── F3 junction-priority engine glue ──────────────────────────────────────────
// Walk `obj`'s route forward to the FIRST connecting road (a road whose
// Road::GetJunction() is set) and report its authored id string plus the owning
// junction's authored id string. Uses the same isolated Position + CopyRoute idiom
// as PredictPath; junctionSelectorAngle=0 makes an UN-routed MoveAlongS go straight
// (deterministic — avoids the random junction-branch trap noted in the F3 brief).
// Routed vehicles (the 08 scenes assign routes) follow their route regardless.
// Returns false when no connecting road is reached within `lookahead`, or the
// junction has no resolvable string id.
bool FindUpcomingConnectingRoad(Object* obj, double lookahead, double step,
                                std::string& conn_id, std::string& junction_id)
{
    if (!obj) return false;
    roadmanager::OpenDrive* od = roadmanager::Position::GetOpenDrive();
    if (!od) return false;

    step = std::max(0.25, step);

    roadmanager::Position pos;
    pos.Duplicate(obj->pos_);
    pos.CopyRoute(obj->pos_);

    double traveled = 0.0;
    for (;;)
    {
        roadmanager::Road* road = od->GetRoadById(pos.GetTrackId());
        if (road && road->GetJunction() != ID_UNDEFINED)
        {
            roadmanager::Junction* jn = od->GetJunctionById(road->GetJunction());
            if (!jn) return false;
            conn_id     = road->GetIdStr();
            junction_id = jn->GetIdStr();
            return !conn_id.empty() && !junction_id.empty();
        }
        if (traveled >= lookahead) break;
        const int ret = static_cast<int>(pos.MoveAlongS(
            step, 0.0, 0.0, true, roadmanager::Position::MoveDirectionMode::HEADING_DIRECTION, true));
        if (ret < 0) break;  // end of route / off-route
        traveled += step;
    }
    return false;
}

// Fetch the <priority high low> pairs for `junction_id` from the P5 side model,
// flattened into the (high, low) form junction_priority::Resolve consumes. Empty
// when there is no side model / no priority data for that junction.
std::vector<std::pair<std::string, std::string>> LoadJunctionPriorities(const std::string& junction_id)
{
    std::vector<std::pair<std::string, std::string>> out;
    const void* key = roadmanager::Position::GetOpenDrive();
    if (!key || junction_id.empty()) return out;
    std::vector<gt_esmini::odr::OdrJunctionPriority> pr;
    if (!gt_esmini::odr::GetJunctionPriorities(key, junction_id, pr)) return out;
    out.reserve(pr.size());
    for (const auto& p : pr) out.emplace_back(p.high, p.low);
    return out;
}

// Build a corridor (strip of convex quads) from a path polyline, offsetting each
// segment by ±half_extent perpendicular to it.
std::vector<CorridorQuad> BuildCorridor(const std::vector<PathPoint>& path, double half_extent)
{
    std::vector<CorridorQuad> quads;
    if (path.size() < 2) return quads;
    quads.reserve(path.size() - 1);

    for (size_t i = 0; i + 1 < path.size(); ++i)
    {
        const PathPoint& a = path[i];
        const PathPoint& b = path[i + 1];
        const double dx = b.x - a.x;
        const double dy = b.y - a.y;
        const double len = std::sqrt(dx * dx + dy * dy);
        if (len < 1.0e-9) continue;
        const double nx = -dy / len;  // left-perpendicular unit
        const double ny = dx / len;

        CorridorQuad q;
        q.s0 = a.s_cum;
        q.s1 = b.s_cum;
        // CCW: a-left, b-left, b-right, a-right
        q.poly = {
            Pt{a.x + nx * half_extent, a.y + ny * half_extent},
            Pt{b.x + nx * half_extent, b.y + ny * half_extent},
            Pt{b.x - nx * half_extent, b.y - ny * half_extent},
            Pt{a.x - nx * half_extent, a.y - ny * half_extent},
        };
        for (const Pt& p : q.poly) q.box.Expand(p[0], p[1]);
        quads.push_back(std::move(q));
    }
    return quads;
}

// World XY of the point on `path` at cumulative arc-length `s` (clamped to the
// path span, linearly interpolated within the bracketing segment).
bool PathPointAtS(const std::vector<PathPoint>& path, double s, double& x, double& y)
{
    if (path.empty()) return false;
    if (s <= path.front().s_cum) { x = path.front().x; y = path.front().y; return true; }
    if (s >= path.back().s_cum)  { x = path.back().x;  y = path.back().y;  return true; }
    for (size_t i = 0; i + 1 < path.size(); ++i)
    {
        if (s <= path[i + 1].s_cum)
        {
            const double seg = path[i + 1].s_cum - path[i].s_cum;
            const double f   = seg > 1.0e-9 ? (s - path[i].s_cum) / seg : 0.0;
            x = path[i].x + f * (path[i + 1].x - path[i].x);
            y = path[i].y + f * (path[i + 1].y - path[i].y);
            return true;
        }
    }
    return false;
}

// Result of intersecting the ego corridor against ONE other corridor: the conflict
// region's arc-length spans on each path (nearest cluster along the ego path).
struct RegionSpan
{
    bool   found  = false;
    double se_in  = 0.0;  // ego region entry arc-length [m]
    double se_out = 0.0;  // ego region exit  arc-length [m]
    double so_in  = 0.0;  // other region entry arc-length [m]
    double so_out = 0.0;  // other region exit  arc-length [m]
};

// True polygon intersection of the two corridors -> nearest-to-ego conflict
// region. A quad pair conflicts when their convex-clip area exceeds `eps`. The
// "cluster nearest the ego" is taken as all conflicting ego-quads contiguous (in
// ego arc-length) with the nearest conflicting one; from those quads we take the
// min/max ego and other arc-lengths. `gap_tol` allows a 1-step seam between
// conflicting quads to stay in the same cluster (corridor sampling can skip a
// quad at a grazing corner).
RegionSpan FindConflictRegion(const std::vector<CorridorQuad>& ego,
                              const std::vector<CorridorQuad>& other,
                              double eps, double gap_tol)
{
    RegionSpan span;

    // (0) Coarse whole-corridor AABB reject: if the corridors' bounding boxes are
    // disjoint they cannot intersect — skip the O(N*M) clip entirely.
    if (!CorridorBounds(ego).Overlaps(CorridorBounds(other)))
        return span;

    // Collect every conflicting (ego s-mid) with the other-span it overlaps.
    struct Hit
    {
        double e_s0, e_s1, o_s0, o_s1;
    };
    std::vector<Hit> hits;
    for (const auto& eq : ego)
    {
        for (const auto& oq : other)
        {
            // Per-quad AABB pre-check before the exact (heavier) convex clip.
            if (!eq.box.Overlaps(oq.box)) continue;
            const double area = conflict_geom::PolygonArea(conflict_geom::ConvexClip(eq.poly, oq.poly));
            if (area > eps)
                hits.push_back({eq.s0, eq.s1, oq.s0, oq.s1});
        }
    }
    if (hits.empty()) return span;

    // Nearest cluster: sort by ego entry, then grow a run while the gap between
    // consecutive ego spans stays within gap_tol.
    std::sort(hits.begin(), hits.end(),
              [](const Hit& a, const Hit& b) { return a.e_s0 < b.e_s0; });

    double e_in  = hits[0].e_s0;
    double e_out = hits[0].e_s1;
    double o_in  = hits[0].o_s0;
    double o_out = hits[0].o_s1;
    for (size_t k = 1; k < hits.size(); ++k)
    {
        if (hits[k].e_s0 > e_out + gap_tol)
            break;  // a separate, farther cluster — keep only the nearest
        e_out = std::max(e_out, hits[k].e_s1);
        e_in  = std::min(e_in, hits[k].e_s0);
        o_in  = std::min(o_in, hits[k].o_s0);
        o_out = std::max(o_out, hits[k].o_s1);
    }

    span.found  = true;
    span.se_in  = e_in;
    span.se_out = e_out;
    span.so_in  = o_in;
    span.so_out = o_out;
    return span;
}

// Local heading (unit direction) of a path at a given arc-length, for the
// same-direction filter. Returns false for an empty/degenerate path.
bool PathHeadingAtS(const std::vector<PathPoint>& path, double s, double& dx, double& dy)
{
    if (path.size() < 2) return false;
    for (size_t i = 0; i + 1 < path.size(); ++i)
    {
        if (s <= path[i + 1].s_cum || i + 2 == path.size())
        {
            double ex = path[i + 1].x - path[i].x;
            double ey = path[i + 1].y - path[i].y;
            const double len = std::sqrt(ex * ex + ey * ey);
            if (len < 1.0e-9) return false;
            dx = ex / len;
            dy = ey / len;
            return true;
        }
    }
    return false;
}
}  // namespace

TrafficPolicySnapshot ConflictPointResolver::Evaluate(const TrafficPolicyContext& ctx)
{
    TrafficPolicySnapshot snap;
    if (!ctx.ego || !ctx.entities) return snap;

    Object*          ego   = ctx.ego;
    const double     v_ego = ego->GetSpeed();
    constexpr double eps      = 1.0e-3;
    const double     area_eps = cfg_.area_eps;  // [m^2] min clipped area to call a quad pair conflicting

    const double ego_len      = ego->boundingbox_.dimensions_.length_;
    const double ego_half_w   = 0.5 * ego->boundingbox_.dimensions_.width_;
    const double ego_half_ext = ego_half_w + cfg_.lane_margin;

    // Ego predicted route polyline + corridor.
    std::vector<PathPoint>    ego_path     = PredictPath(ego, cfg_.lookahead, cfg_.step);
    std::vector<CorridorQuad> ego_corridor = BuildCorridor(ego_path, ego_half_ext);
    if (ego_corridor.empty())
    {
        committed_ = false;  // no path -> drop any held yield
        return snap;
    }

    // Drive-side rule of the ego's current road. Retained as a diagnostic; F3
    // right-of-way is taken from the OpenDRIVE <priority> list directly (which
    // encodes the HIGH/LOW connecting roads independent of RHT/LHT), so it does not
    // gate behaviour here.
    roadmanager::Road::RoadRule ego_rule = roadmanager::Road::RoadRule::RIGHT_HAND_TRAFFIC;
    if (roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive())
        if (roadmanager::Road* road = odr->GetRoadById(ego->pos_.GetTrackId()))
            ego_rule = road->GetRule();
    (void)ego_rule;

    // ── F3 junction-priority pre-pass (resolved ONCE per frame). Find the ego's
    // upcoming connecting road + its junction's <priority> list. When the flag is
    // off, no connecting road is ahead, or the junction carries no <priority> data,
    // `have_ego_priority` stays false and the gate below is inert — the base yield
    // behaviour is preserved byte-for-byte.
    bool                                             have_ego_priority = false;
    std::string                                      ego_conn;
    std::string                                      ego_junction;
    std::vector<std::pair<std::string, std::string>> ego_priorities;
    if (cfg_.junction_priority_enabled &&
        FindUpcomingConnectingRoad(ego, cfg_.lookahead, cfg_.step, ego_conn, ego_junction))
    {
        ego_priorities    = LoadJunctionPriorities(ego_junction);
        have_ego_priority = !ego_priorities.empty();
    }

    // ── Per-frame scan: find the nearest governing space-time conflict. ────────
    bool   gov_found    = false;
    double gov_se_in    = 0.0;  // ego region entry arc-length of the governing conflict
    double gov_exit_x   = 0.0;  // governing other's region-exit world point (release ref)
    double gov_exit_y   = 0.0;
    double gov_exit_tx  = 1.0;  // other's path tangent at the exit (fixed release axis)
    double gov_exit_ty  = 0.0;
    int    gov_other_id = -1;

    // Cache of the scan pass keyed on the CURRENTLY-latched governing id, so the
    // latch block below reuses this frame's PredictPath/region instead of redoing
    // all of it for the same entity (they are identical). Populated only when the
    // latched governing id is visited AND yields a found region here.
    bool                   have_gov_scan = false;
    std::vector<PathPoint> gov_scan_path;
    RegionSpan             gov_scan_rgn;

    for (auto* other : ctx.entities->object_)
    {
        if (!other || other == ego) continue;
        if (other->GetType() != Object::Type::VEHICLE) continue;  // crossings = vehicles

        const double oth_len      = other->boundingbox_.dimensions_.length_;
        const double oth_half_ext = 0.5 * other->boundingbox_.dimensions_.width_ + cfg_.lane_margin;

        // Cheap Euclidean pre-filter before PredictPath()/BuildCorridor(), which walk the road network
        // for EVERY vehicle on the map every frame and dominate the policy cost (~0.1 ms/vehicle).
        // Provably non-behavioural: each corridor extends at most `lookahead` metres ALONG the road from
        // its origin, inflated by half_ext, and a road path is never shorter than the straight line
        // (arc >= chord) -- so every point of a corridor lies within (lookahead + half_ext) Euclidean of
        // its own vehicle. Two corridors can therefore only share a point when the vehicles are within
        // the sum of those radii. Anything farther cannot produce a conflict region.
        const double reach    = 2.0 * cfg_.lookahead + ego_half_ext + oth_half_ext;
        const double dx       = other->pos_.GetX() - ego->pos_.GetX();
        const double dy       = other->pos_.GetY() - ego->pos_.GetY();
        if (dx * dx + dy * dy > reach * reach) continue;

        std::vector<PathPoint> oth_path = PredictPath(other, cfg_.lookahead, cfg_.step);
        if (oth_path.size() < 2) continue;

        std::vector<CorridorQuad> oth_corridor = BuildCorridor(oth_path, oth_half_ext);
        if (oth_corridor.empty()) continue;

        // (c) True polygon conflict region (nearest cluster).
        const RegionSpan rgn = FindConflictRegion(ego_corridor, oth_corridor, area_eps,
                                                  std::max(2.0, 2.0 * cfg_.step));
        if (!rgn.found) continue;

        // (F3) Junction priority: if the ego OUT-RANKS this other at their shared
        // junction (ego's upcoming connecting road is HIGH over the other's LOW in
        // the <priority> list), the ego has right of way and does NOT yield to it —
        // drop the conflict entirely (it never becomes governing, never latches).
        // Others the ego does not out-rank (OTHER_PRIORITY / UNKNOWN / a different
        // junction / no priority data) fall through to the base yield below.
        if (have_ego_priority)
        {
            std::string oth_conn;
            std::string oth_junction;
            if (FindUpcomingConnectingRoad(other, cfg_.lookahead, cfg_.step, oth_conn, oth_junction) &&
                oth_junction == ego_junction &&
                junction_priority::Resolve(ego_conn, oth_conn, ego_priorities) ==
                    junction_priority::Relation::EGO_PRIORITY)
            {
                continue;
            }
        }

        // Cache for the latch block: this is the same PredictPath+region the latch
        // would otherwise recompute for the currently-latched governing entity.
        if (committed_ && other->GetId() == committed_other_id_)
        {
            have_gov_scan = true;
            gov_scan_path = oth_path;
            gov_scan_rgn  = rgn;
        }

        // (a) Same-direction filter: if the two corridors run nearly parallel /
        // same-heading through the region, it is a following relationship, not a
        // crossing — leave it to LeadVehicleAware.
        double edx, edy, odx, ody;
        if (PathHeadingAtS(ego_path, 0.5 * (rgn.se_in + rgn.se_out), edx, edy) &&
            PathHeadingAtS(oth_path, 0.5 * (rgn.so_in + rgn.so_out), odx, ody))
        {
            if (conflict_geom::CrossingAngleDeg(edx, edy, odx, ody) < cfg_.min_cross_angle_deg)
                continue;  // near-parallel -> not a crossing conflict
        }

        const double v_o = other->GetSpeed();

        // (d) Near-stationary other that is NOT yet at its region -> no imminent
        // conflict (it won't reach the region soon). If it already sits at/past
        // its region entry it still blocks us, so fall through.
        const double so_front = std::max(0.0, rgn.so_in - oth_len * 0.5);
        if (v_o < cfg_.other_min_speed && so_front > eps)
            continue;

        // (e) Footprint timing, constant speed (length-aware).
        const double t_onc_in  = std::max(0.0, (rgn.so_in - oth_len * 0.5)) / std::max(v_o, eps);
        const double t_onc_out = (rgn.so_out + oth_len * 0.5) / std::max(v_o, eps);
        const double v_ego_eff = std::max(v_ego, cfg_.nominal_speed);  // anti-chatter floor
        const double t_ego_in  = std::max(0.0, (rgn.se_in - ego_len * 0.5)) / v_ego_eff;
        const double t_ego_out = (rgn.se_out + ego_len * 0.5) / v_ego_eff;

        // (f) Space-time overlap with a post-encroachment-time pad.
        const bool overlap = (t_ego_in <= t_onc_out + cfg_.pet) &&
                             (t_onc_in - cfg_.pet <= t_ego_out);
        if (!overlap) continue;

        if (!gov_found || rgn.se_in < gov_se_in)
        {
            double ex = 0.0, ey = 0.0;
            PathPointAtS(oth_path, rgn.so_out, ex, ey);  // region-exit world point on the other's route
            double tx = 1.0, ty = 0.0;
            PathHeadingAtS(oth_path, rgn.so_out, tx, ty);  // fixed release axis (path tangent at exit)
            gov_found    = true;
            gov_se_in    = rgn.se_in;
            gov_exit_x   = ex;
            gov_exit_y   = ey;
            gov_exit_tx  = tx;
            gov_exit_ty  = ty;
            gov_other_id = other->GetId();
        }
    }

    // ── Latch (positional release, crawl-robust). ─────────────────────────────
    if (committed_)
    {
        // Locate the governing other by id.
        Object* gov = nullptr;
        for (auto* o : ctx.entities->object_)
            if (o && o->GetId() == committed_other_id_) { gov = o; break; }

        bool released = true;
        if (gov)
        {
            const double g_half_ext = 0.5 * gov->boundingbox_.dimensions_.width_ + cfg_.lane_margin;
            const double g_len      = gov->boundingbox_.dimensions_.length_;

            // Re-derive the governing other's region from the CURRENT geometry so
            // the ego stop stays pinned to a fixed absolute location even as the
            // ego crawls. Reuse this frame's scan result for the governing entity
            // when the scan already found it (identical PredictPath+region), else
            // recompute (the scan may have filtered it before recording a region).
            std::vector<PathPoint> gpath;
            RegionSpan             rgn;
            if (have_gov_scan)
            {
                gpath = std::move(gov_scan_path);
                rgn   = gov_scan_rgn;
            }
            else
            {
                gpath = PredictPath(gov, cfg_.lookahead, cfg_.step);
                std::vector<CorridorQuad> g_corr = BuildCorridor(gpath, g_half_ext);
                if (!g_corr.empty())
                    rgn = FindConflictRegion(ego_corridor, g_corr, area_eps,
                                             std::max(2.0, 2.0 * cfg_.step));
            }

            if (rgn.found)
            {
                committed_stop_s_ = rgn.se_in;  // pin the stop to the region entry
                double ex = committed_exit_x_, ey = committed_exit_y_;
                if (PathPointAtS(gpath, rgn.so_out, ex, ey))
                {
                    committed_exit_x_ = ex;
                    committed_exit_y_ = ey;
                }
                // Refresh the fixed release axis: the other's path tangent AT the
                // exit (its direction of travel leaving the region), not its
                // instantaneous heading — robust to the other turning near/after
                // the exit.
                double tx = committed_exit_tx_, ty = committed_exit_ty_;
                if (PathHeadingAtS(gpath, rgn.so_out, tx, ty))
                {
                    committed_exit_tx_ = tx;
                    committed_exit_ty_ = ty;
                }
            }

            // Positional release: the other's body has physically cleared the
            // stored region-exit world point. Signed distance of the other's
            // CURRENT origin past the exit along the EXIT TANGENT (the fixed
            // direction it left the region), cleared once the rear is beyond the
            // exit by `release_buffer`. Using the stored tangent instead of the
            // other's live heading keeps the measure correct when the other turns
            // at/after the conflict (its heading would otherwise mis-project the
            // displacement -> yield held forever or released early).
            //   forward = dot(origin - exit, exit_tangent)
            //   rear_past_exit = forward - g_len/2
            const double ox  = gov->pos_.GetX();
            const double oy  = gov->pos_.GetY();
            const double forward = conflict_geom::ForwardDistanceAlong(
                ox, oy, committed_exit_x_, committed_exit_y_, committed_exit_tx_, committed_exit_ty_);
            const double rear_past_exit = forward - g_len * 0.5;
            released = (rear_past_exit >= cfg_.release_buffer);
        }

        if (released)
        {
            if (gov_found)
            {
                // Re-commit straight to a fresh governing conflict (e.g. the next
                // oncoming).
                committed_          = true;
                committed_stop_s_   = gov_se_in;
                committed_other_id_ = gov_other_id;
                committed_exit_x_   = gov_exit_x;
                committed_exit_y_   = gov_exit_y;
                committed_exit_tx_  = gov_exit_tx;
                committed_exit_ty_  = gov_exit_ty;
            }
            else
            {
                committed_ = false;
            }
        }
    }
    else if (gov_found)
    {
        committed_          = true;
        committed_stop_s_   = gov_se_in;
        committed_other_id_ = gov_other_id;
        committed_exit_x_   = gov_exit_x;
        committed_exit_y_   = gov_exit_y;
        committed_exit_tx_  = gov_exit_tx;
        committed_exit_ty_  = gov_exit_ty;
    }

    if (!committed_) return snap;  // free to proceed — no constraint

    PolicyConstraint c;
    c.kind   = PolicyConstraint::Kind::STOP_AT_S;
    c.s      = std::max(0.0, committed_stop_s_ - cfg_.standoff);
    c.value  = 0.0;
    c.source = "conflict_point";
    snap.constraints.push_back(c);
    snap.valid = true;
    return snap;
}

}  // namespace gt_esmini
