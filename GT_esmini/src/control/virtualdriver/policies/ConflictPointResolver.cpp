#include "gt_esmini/control/virtualdriver/policies/ConflictPointResolver.hpp"

#include "Entities.hpp"
#include "RoadManager.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace scenarioengine;

namespace gt_esmini
{

// ─────────────────────── pure geometry / timing helpers ───────────────────────
namespace conflict_geom
{
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
        return false;  // intersection of the infinite lines lies outside a segment

    ix = ax + t * rx;
    iy = ay + t * ry;
    return true;
}

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

GapAction GapDecision(double t_ego, double t_enter, double t_exit, double accept_gap)
{
    // YIELD if the ego would arrive while the zone is (or is about to be) occupied.
    if (t_ego >= t_enter - accept_gap && t_ego <= t_exit + accept_gap)
        return GapAction::YIELD;
    return GapAction::PROCEED;  // other clears with margin, or arrives long after the ego
}
}  // namespace conflict_geom

namespace
{
struct PathPoint
{
    double x     = 0.0;
    double y     = 0.0;
    double s_cum = 0.0;  // cumulative route distance from the start [m]
};

// Walk a vehicle's route forward into a polyline {x, y, s_cum}, mirroring the
// RouteSignalScan idiom (isolated Position copy + CopyRoute + MoveAlongS). Stops
// at end/off-route (MoveAlongS < 0) or once `lookahead` metres are covered.
std::vector<PathPoint> PredictPath(Object* obj, double lookahead, double step)
{
    std::vector<PathPoint> out;
    if (!obj) return out;

    step = std::max(0.5, step);

    roadmanager::Position pos;
    pos.Duplicate(obj->pos_);
    pos.CopyRoute(obj->pos_);

    out.push_back({pos.GetX(), pos.GetY(), 0.0});

    double traveled = 0.0;
    while (traveled < lookahead)
    {
        const int ret = static_cast<int>(pos.MoveAlongS(step));
        if (ret < 0) break;  // end of route / off-route
        traveled += step;
        out.push_back({pos.GetX(), pos.GetY(), traveled});
    }
    return out;
}

// Nearest-to-start proper crossing of two polylines. Returns true and fills the
// route distance of the crossing along each path (s_a, s_b) plus the local crossing
// angle, requiring the angle >= min_angle_deg to exclude near-parallel overlaps.
bool FindCrossing(const std::vector<PathPoint>& A, const std::vector<PathPoint>& B,
                  double min_angle_deg, double& s_a, double& s_b, double& angle_deg)
{
    bool   found = false;
    double best_sa = 0.0, best_sb = 0.0, best_ang = 0.0;

    for (size_t i = 0; i + 1 < A.size(); ++i)
    {
        const PathPoint& a0 = A[i];
        const PathPoint& a1 = A[i + 1];
        for (size_t j = 0; j + 1 < B.size(); ++j)
        {
            const PathPoint& b0 = B[j];
            const PathPoint& b1 = B[j + 1];

            double t, u, ix, iy;
            if (!conflict_geom::SegmentsIntersect(a0.x, a0.y, a1.x, a1.y,
                                                  b0.x, b0.y, b1.x, b1.y,
                                                  t, u, ix, iy))
                continue;

            const double ang = conflict_geom::CrossingAngleDeg(a1.x - a0.x, a1.y - a0.y,
                                                               b1.x - b0.x, b1.y - b0.y);
            if (ang < min_angle_deg) continue;  // near-parallel -> not a crossing conflict

            const double sa = a0.s_cum + t * (a1.s_cum - a0.s_cum);
            const double sb = b0.s_cum + u * (b1.s_cum - b0.s_cum);

            if (!found || sa < best_sa)
            {
                found    = true;
                best_sa  = sa;
                best_sb  = sb;
                best_ang = ang;
            }
        }
    }

    if (found)
    {
        s_a       = best_sa;
        s_b       = best_sb;
        angle_deg = best_ang;
    }
    return found;
}
}  // namespace

TrafficPolicySnapshot ConflictPointResolver::Evaluate(const TrafficPolicyContext& ctx)
{
    TrafficPolicySnapshot snap;
    if (!ctx.ego || !ctx.entities) return snap;

    Object*      ego   = ctx.ego;
    const double v_ego = ego->GetSpeed();
    constexpr double eps = 1.0e-3;

    // Ego predicted route polyline.
    std::vector<PathPoint> ego_path = PredictPath(ego, cfg_.lookahead, cfg_.step);
    if (ego_path.size() < 2) return snap;  // no path to project a crossing onto

    // Drive-side rule of the ego's current road. Kept available for F3 priority
    // gating (which turns must yield given RHT/LHT); the geometric crossing + the
    // timing gate handle detection in this minimal increment, so the rule is read
    // but does NOT hard-fail or change behaviour here. F3 will use it to skip
    // crossings the ego has right-of-way over (e.g. RHT: a straight-through ego
    // need not yield to a left-turner) instead of yielding to every crossing.
    roadmanager::Road::RoadRule ego_rule = roadmanager::Road::RoadRule::RIGHT_HAND_TRAFFIC;
    if (roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive())
        if (roadmanager::Road* road = odr->GetRoadById(ego->pos_.GetTrackId()))
            ego_rule = road->GetRule();
    (void)ego_rule;  // detection-only in 3d; consumed by F3 priority logic.

    // Scan all others for crossings, classifying each against TWO gap margins so
    // the latch has hysteresis:
    //   ENTRY    (accept_gap)               — the gap that DECIDES to start yielding.
    //   BLOCKING (accept_gap + release_extra) — the wider gap that HOLDS the stop.
    // The EGO arrival time is estimated against a speed FLOOR (nominal_speed): this
    // answers "if I commit, when do I arrive" and stays stable while braking — a
    // raw s/v_ego blows up as v_ego->0 and was the source of the STOP/GO flipping.
    // The OTHER vehicle's real speed is kept for t_enter/t_exit.
    bool   entry_yield = false;  // nearest crossing that yields under the entry margin
    double entry_s     = 0.0;    // its ego route-s
    bool   blocking    = false;  // nearest crossing that still yields under the wider margin
    double block_s     = 0.0;    // its ego route-s

    for (auto* other : ctx.entities->object_)
    {
        if (!other || other == ego) continue;
        if (other->GetType() != Object::Type::VEHICLE) continue;  // crossings = vehicles

        std::vector<PathPoint> oth_path = PredictPath(other, cfg_.lookahead, cfg_.step);
        if (oth_path.size() < 2) continue;

        double s_ego_c = 0.0, s_oth_c = 0.0, angle = 0.0;
        if (!FindCrossing(ego_path, oth_path, cfg_.min_cross_angle_deg, s_ego_c, s_oth_c, angle))
            continue;  // paths don't genuinely cross
        (void)angle;  // crossing angle is used as the FindCrossing gate; kept for F3 priority diag.

        const double v_o     = other->GetSpeed();
        const double oth_len = other->boundingbox_.dimensions_.length_;

        // Near-stationary other that is NOT already inside the conflict zone -> no
        // imminent conflict (it isn't going to reach the crossing soon). If it sits
        // ON the crossing it still blocks us, so fall through.
        const bool other_in_zone = (s_oth_c <= cfg_.zone_half);
        if (v_o < cfg_.other_min_speed && !other_in_zone)
            continue;

        // Floored ego arrival estimate (anti-chatter): independent of the braking
        // transient. Other-vehicle times use the real speed.
        const double t_ego   = s_ego_c / std::max(v_ego, cfg_.nominal_speed);
        const double t_enter = std::max(0.0, (s_oth_c - cfg_.zone_half)) / std::max(v_o, eps);
        const double t_exit  = (s_oth_c + cfg_.zone_half + oth_len) / std::max(v_o, eps);

        if (conflict_geom::GapDecision(t_ego, t_enter, t_exit, cfg_.accept_gap)
            == conflict_geom::GapAction::YIELD)
        {
            if (!entry_yield || s_ego_c < entry_s)
            {
                entry_yield = true;
                entry_s     = s_ego_c;
            }
        }

        if (conflict_geom::GapDecision(t_ego, t_enter, t_exit,
                                       cfg_.accept_gap + cfg_.release_extra)
            == conflict_geom::GapAction::YIELD)
        {
            if (!blocking || s_ego_c < block_s)
            {
                blocking = true;
                block_s  = s_ego_c;
            }
        }
    }

    // Latch transitions (hysteresis): enter on the entry margin, hold on the wider
    // release margin, release only when the oncoming stream has clearly cleared.
    if (entry_yield)
    {
        committed_        = true;
        committed_stop_s_ = entry_s;
    }
    else if (committed_ && blocking)
    {
        committed_stop_s_ = block_s;   // still blocked: track the governing crossing
    }
    else if (committed_ && !blocking)
    {
        committed_ = false;            // oncoming cleared by the wider margin -> release
    }

    if (!committed_) return snap;  // free to proceed — no constraint

    PolicyConstraint c;
    c.kind   = PolicyConstraint::Kind::STOP_AT_S;
    c.s      = std::max(0.0, committed_stop_s_ - cfg_.stop_margin);
    c.value  = 0.0;
    c.source = "conflict_point";
    snap.constraints.push_back(c);
    snap.valid = true;
    return snap;
}

}  // namespace gt_esmini
