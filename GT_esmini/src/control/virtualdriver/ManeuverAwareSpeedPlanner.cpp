#include "gt_esmini/control/virtualdriver/ManeuverAwareSpeedPlanner.hpp"

#include "Entities.hpp"
#include "RoadManager.hpp"
#include "CommonMini.hpp"

#include <algorithm>
#include <cmath>

using namespace scenarioengine;

namespace gt_esmini
{

namespace
{
// Sentinel for "no constraint here" — large enough that min() with any real
// commanded/limit speed always picks the real value, small enough that the
// backward-pass sqrt(v^2 + 2*a*ds) never overflows.
constexpr double kUnconstrained = 1.0e6;
}  // namespace

MidLongPlannerSnapshot ManeuverAwareSpeedPlanner::Plan(const MidLongContext& ctx)
{
    MidLongPlannerSnapshot snap;

    Object* obj = ctx.object;
    if (!obj) return snap;

    roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();
    if (!odr) return snap;

    const double step      = std::max(0.5, cfg_.scan_step);
    const double scan_dist = std::max(step, ctx.scan_dist);

    // Walk a copy of the ego route forward, isolated from the shared object
    // state (same pattern as TrajectoryShortPlanner / DetectJunctionTurn).
    roadmanager::Position pos;
    pos.Duplicate(obj->pos_);
    pos.CopyRoute(obj->pos_);

    auto& prof = snap.v_target_profile;
    double s_ahead = 0.0;

    while (s_ahead <= scan_dist)
    {
        // Curvature ceiling: v = sqrt(a_lat / |kappa|). Straights -> kappa ~ 0
        // -> unconstrained. Junction connecting roads are sharp arcs -> low v.
        const double kappa   = pos.GetCurvature();
        double       v_ceil  = kUnconstrained;
        if (std::fabs(kappa) > 1.0e-4)
            v_ceil = std::sqrt(cfg_.max_lateral_accel / std::fabs(kappa));

        // Speed-limit ceiling (esmini returns a sane heuristic if unauthored).
        if (cfg_.respect_speed_limit)
            v_ceil = std::min(v_ceil, pos.GetSpeedLimit());

        // Junction connecting road: belt-and-suspenders cap for gently-modelled
        // junctions whose geometry curvature alone would not slow the ego enough.
        roadmanager::Road* road = odr->GetRoadById(pos.GetTrackId());
        if (road && road->GetJunction() != ID_UNDEFINED)
            v_ceil = std::min(v_ceil, cfg_.turn_speed);

        v_ceil = std::clamp(v_ceil, cfg_.min_speed, kUnconstrained);
        prof.emplace_back(s_ahead, v_ceil);

        const int ret = static_cast<int>(pos.MoveAlongS(step));
        if (ret < 0) break;  // end of route/road or off-route — stop scanning
        s_ahead += step;
    }

    // Backward comfort-deceleration pass: ensure each point is reachable from the
    // next under comfort_decel, so a low ceiling ahead propagates back and braking
    // starts early (anticipatory) instead of as a late hard stop.
    for (int i = static_cast<int>(prof.size()) - 2; i >= 0; --i)
    {
        const double ds          = prof[i + 1].first - prof[i].first;
        const double v_reachable = std::sqrt(prof[i + 1].second * prof[i + 1].second +
                                             2.0 * cfg_.comfort_decel * ds);
        prof[i].second = std::min(prof[i].second, v_reachable);
    }

    snap.valid = prof.size() >= 2;
    return snap;
}

}  // namespace gt_esmini
