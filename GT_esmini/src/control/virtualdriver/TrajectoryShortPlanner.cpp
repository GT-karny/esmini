#include "gt_esmini/control/virtualdriver/TrajectoryShortPlanner.hpp"

#include "Entities.hpp"
#include "OSCPrivateAction.hpp"
#include "RoadManager.hpp"
#include "CommonMini.hpp"

#include <cmath>
#include <algorithm>

using namespace scenarioengine;

namespace gt_esmini
{

namespace
{
// Evaluate a TransitionDynamics shape at arbitrary progress [0,1].
// (Mirrors the helper in ControllerKinematic.cpp.)
double EvalShape(OSCPrivateAction::DynamicsShape shape, double startVal, double A, double progress)
{
    progress = CLAMP(progress, 0.0, 1.0);
    switch (shape)
    {
        case OSCPrivateAction::DynamicsShape::SINUSOIDAL:
            return startVal - A * (std::cos(M_PI * progress) - 1.0) / 2.0;
        case OSCPrivateAction::DynamicsShape::CUBIC:
            return startVal + A * progress * progress * (3.0 - 2.0 * progress);
        case OSCPrivateAction::DynamicsShape::LINEAR:
            return startVal + A * progress;
        case OSCPrivateAction::DynamicsShape::STEP:
            return startVal + A;
        default:
            return startVal;
    }
}

struct LatInfo
{
    OSCPrivateAction::DynamicsShape shape;
    double startVal;
    double A;
    double P;
    double current_p;
    double current_off;
    bool   time_based;
    double lane_sign;
};
}  // namespace

double TrajectoryShortPlanner::SampleTargetSpeed(const ShortPlanContext& ctx, double s_ahead) const
{
    if (ctx.v_target && ctx.v_target->valid && !ctx.v_target->v_target_profile.empty())
    {
        // Piecewise-linear interpolation over (s, v_max) pairs.
        const auto& prof = ctx.v_target->v_target_profile;
        if (s_ahead <= prof.front().first) return prof.front().second;
        if (s_ahead >= prof.back().first) return prof.back().second;
        for (size_t i = 1; i < prof.size(); ++i)
        {
            if (s_ahead <= prof[i].first)
            {
                double s0 = prof[i - 1].first, v0 = prof[i - 1].second;
                double s1 = prof[i].first, v1 = prof[i].second;
                double w = (s1 > s0) ? (s_ahead - s0) / (s1 - s0) : 0.0;
                return v0 + w * (v1 - v0);
            }
        }
        return prof.back().second;
    }
    return ctx.fallback_speed;
}

ShortPlannerSnapshot TrajectoryShortPlanner::Plan(const ShortPlanContext& ctx)
{
    ShortPlannerSnapshot snap;
    snap.dt        = ctx.dt;
    snap.horizon_s = ctx.horizon_s;

    Object* obj = ctx.object;
    if (!obj) return snap;

    const double dt = (ctx.dt > 1e-3) ? ctx.dt : 0.1;
    const int    n_steps = std::max(1, static_cast<int>(std::ceil(ctx.horizon_s / dt)));
    const double abs_nominal = std::max(0.1, std::fabs(ctx.fallback_speed));

    // --- Collect active lateral actions (storyboard LaneChange / LaneOffset) ---
    std::vector<LatInfo> lat_actions;
    for (auto* action : obj->getPrivateActions())
    {
        if (action->GetCurrentState() != StoryBoardElement::State::RUNNING) continue;

        const OSCPrivateAction::TransitionDynamics* td = nullptr;
        if (action->action_type_ == OSCAction::ActionType::LAT_LANE_CHANGE)
            td = &static_cast<LatLaneChangeAction*>(action)->transition_;
        else if (action->action_type_ == OSCAction::ActionType::LAT_LANE_OFFSET)
            td = &static_cast<LatLaneOffsetAction*>(action)->transition_;

        if (!td || td->GetParamTargetVal() < 1e-6) continue;

        double startVal = td->GetStartVal();
        double A        = td->GetTargetVal() - startVal;
        double P        = td->GetParamTargetVal();
        double cur_p    = td->GetParamVal();
        double cur_off  = EvalShape(td->shape_, startVal, A, cur_p / P);
        bool   tb       = (td->dimension_ == OSCPrivateAction::DynamicsDimension::TIME ||
                           td->dimension_ == OSCPrivateAction::DynamicsDimension::RATE);
        double lane_sign = static_cast<double>(SIGN(obj->pos_.GetLaneId()));

        lat_actions.push_back({td->shape_, startVal, A, P, cur_p, cur_off, tb, lane_sign});
    }

    // --- Walk the route at equal Δt ---
    double v0 = SampleTargetSpeed(ctx, 0.0);

    roadmanager::Position pos;
    pos.Duplicate(obj->pos_);
    pos.CopyRoute(obj->pos_);  // isolate route mutations from the shared object state

    // Anchor the preview to the routed lane CENTER (offset 0) ONLY when no
    // deliberate lateral maneuver is active. This removes an *unintended*
    // cross-track error (e.g. drift after a fast junction turn) so the driver,
    // which has no separate lane-centering term, steers back instead of tracking
    // parallel and off-center forever. During a LaneChange/LaneOffset we keep the
    // car-anchored base, because the overlay below adds the maneuver displacement
    // relative to the current position; zeroing the offset there would drop that
    // baseline and send the lateral target haywire. Recovery resumes once the
    // maneuver completes (lat_actions empties).
    if (lat_actions.empty())
        pos.SetLanePos(pos.GetTrackId(), pos.GetLaneId(), pos.GetS(), 0.0);

    // First preview point = lane center (no maneuver) or the car's pos (maneuver),
    // giving the driver a real cross-track error to null out in the former case.
    snap.preview.push_back({pos.GetX(), pos.GetY(), v0, 0.0});

    double acc_dist = 0.0;
    for (int i = 1; i <= n_steps; ++i)
    {
        double v_here = SampleTargetSpeed(ctx, acc_dist);
        double ds     = std::max(cfg_.min_step, std::fabs(v_here) * dt);

        int ret = static_cast<int>(pos.MoveAlongS(ds));
        if (ret == static_cast<int>(roadmanager::Position::ReturnCode::ERROR_GENERIC))
            break;
        acc_dist += ds;

        double px = pos.GetX();
        double py = pos.GetY();

        if (!lat_actions.empty())
        {
            double road_h = pos.GetHRoad();
            double tx = -std::sin(road_h);  // +t axis in world frame
            double ty =  std::cos(road_h);
            for (auto& la : lat_actions)
            {
                double future_p = la.time_based ? (la.current_p + acc_dist / abs_nominal)
                                                : (la.current_p + acc_dist);
                future_p = std::min(future_p, la.P);
                double future_off = EvalShape(la.shape, la.startVal, la.A, future_p / la.P);
                double delta_t = (future_off - la.current_off) * la.lane_sign;
                px += delta_t * tx;
                py += delta_t * ty;
            }
        }

        snap.preview.push_back({px, py, v_here, i * dt});
    }

    snap.valid = snap.preview.size() >= 2;
    return snap;
}

}  // namespace gt_esmini
