#include "gt_esmini/control/virtualdriver/TrajectoryShortPlanner.hpp"
#include "gt_esmini/control/common/TransitionDynamics.hpp"
#include "gt_esmini/control/virtualdriver/ResumeMergeProfile.hpp"

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
// [m] How far off a lane centre the trajectory's endpoint may sit and still be treated
// as "in a lane" for the road continuation. Half a lane plus a margin: a trajectory that
// ends mid-lane-change is still meaningfully in a lane, one that ends in a parking bay is
// not, and drawing a lane-following continuation from the latter would be invention.
constexpr double kMaxContinuationOffset = 3.0;

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

double TrajectoryShortPlanner::SampleCeiling(const ShortPlanContext& ctx, double s_ahead) const
{
    // The v_target CEILING (curvature / junction / speed-limit, comfort shaped) at
    // distance s_ahead. Returns a large sentinel when no mid/long profile is
    // supplied, so min(fallback, ceiling) == fallback (Phase 1 behavior).
    if (!(ctx.v_target && ctx.v_target->valid && !ctx.v_target->v_target_profile.empty()))
        return 1.0e9;

    const auto& prof = ctx.v_target->v_target_profile;
    if (s_ahead <= prof.front().first) return prof.front().second;
    if (s_ahead >= prof.back().first)  return prof.back().second;
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

double TrajectoryShortPlanner::SampleTargetSpeed(const ShortPlanContext& ctx, double s_ahead) const
{
    // Track min(commanded, ceiling): the ceiling only ever slows the car, so it
    // never fights the Phase 1 SpeedAction latch and re-acceleration resumes
    // automatically once the ceiling rises past the command again.
    return std::min(ctx.fallback_speed, SampleCeiling(ctx, s_ahead));
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
        double cur_off  = EvaluateTransitionShape(td->shape_, startVal, A, cur_p / P);
        bool   tb       = (td->dimension_ == OSCPrivateAction::DynamicsDimension::TIME ||
                           td->dimension_ == OSCPrivateAction::DynamicsDimension::RATE);
        double lane_sign = static_cast<double>(SIGN(obj->pos_.GetLaneId()));

        lat_actions.push_back({td->shape_, startVal, A, P, cur_p, cur_off, tb, lane_sign});
    }

    // --- A running FollowTrajectoryAction replaces the route as the path source ---
    for (auto* action : obj->getPrivateActions())
    {
        if (action->GetCurrentState() != StoryBoardElement::State::RUNNING) continue;
        if (action->action_type_ != OSCAction::ActionType::FOLLOW_TRAJECTORY) continue;

        auto* fta = static_cast<FollowTrajectoryAction*>(action);
        if (fta->traj_ != nullptr && fta->traj_->shape_ != nullptr && fta->traj_->GetLength() > 1e-6)
        {
            return PlanAlongTrajectory(ctx, fta);
        }
        break;  // a trajectory without a usable shape -> fall through to the route walk
    }

    // --- Walk the route at equal Δt ---
    double v0 = SampleTargetSpeed(ctx, 0.0);

    roadmanager::Position pos;
    pos.Duplicate(obj->pos_);
    pos.CopyRoute(obj->pos_);  // isolate route mutations from the shared object state

    // Forward control-point offset (P2 issue 2): advance the anchor along the
    // route to the vehicle FRONT (axle/bumper) so the lane-center target the
    // driver nulls out is taken at the front, not the origin (≈ rear). On a tight
    // turn the rear-anchored target let the front swing wide out of the lane.
    // Skipped during a storyboard lateral maneuver (LaneChange/LaneOffset): that
    // path keeps its own car-anchored base + displacement overlay below, and
    // moving the anchor would shift the maneuver phase. We echo the value we
    // actually used so the controller shifts the driver state by the SAME amount
    // (control point and anchor must stay on one route point — hard-won).
    double cp_applied = 0.0;
    if (lat_actions.empty() && ctx.control_point_offset > 1e-6)
    {
        // [Issue #31] straight-most deterministic overload (same rationale as the preview walk below).
        if (pos.MoveAlongS(ctx.control_point_offset, 0.0, 0.0, true,
                           roadmanager::Position::MoveDirectionMode::HEADING_DIRECTION, true) !=
            roadmanager::Position::ReturnCode::ERROR_GENERIC)
            cp_applied = ctx.control_point_offset;
    }
    snap.control_point_offset = cp_applied;

    // Anchor the preview to the CURRENT lane center (offset 0) ONLY when no
    // deliberate lateral maneuver is active. This removes an *unintended*
    // cross-track error (e.g. drift after a fast junction turn) so the driver,
    // which has no separate lane-centering term, steers back instead of tracking
    // parallel and off-center forever. During a LaneChange/LaneOffset we keep the
    // car-anchored base, because the overlay below adds the maneuver displacement
    // relative to the current position; zeroing the offset there would drop that
    // baseline and send the lateral target haywire. Recovery resumes once the
    // maneuver completes (lat_actions empties).
    //
    // CORRECTED COMMENT (feature:F7, resume_merge_trajectory_design.md section
    // 2-2): this anchors to pos.GetTrackId()/pos.GetLaneId(), which is the
    // CURRENT (physically occupied) lane, NOT "the routed lane" an earlier
    // version of this comment claimed -- pos.GetLaneId() tracks physical
    // occupancy (HVDStateApplier -> SetInertiaPos -> XYZ2TrackPos -> Track2Lane),
    // it never consults object->pos_.GetRoute(). The one exception is the
    // resume-merge feature below (ctx.merge_active, shipped default OFF):
    // while a merge is in progress, the anchor uses the CONTROLLER-resolved
    // ROUTE track/lane instead, so the preview centers on the lane the merge
    // is returning to, not whatever lane the vehicle happens to be occupying
    // mid-recovery. See AdSteeringEnvelope.hpp's own (likewise corrected)
    // quote of this comment.
    if (lat_actions.empty())
    {
        if (ctx.merge_active)
            pos.SetLanePos(ctx.merge_track_id, ctx.merge_lane_id, pos.GetS(), 0.0);
        else
            pos.SetLanePos(pos.GetTrackId(), pos.GetLaneId(), pos.GetS(), 0.0);
    }

    // First preview point = lane center (no maneuver) or the car's pos (maneuver),
    // giving the driver a real cross-track error to null out in the former case.
    //
    // feature:F7 resume-merge: apply this frame's merge offset
    // (ctx.merge_offset_now) to THIS point too. MoveAlongS preserves a set
    // offset (RoadManager.cpp:10973), so an offset applied ONLY at the anchor
    // above would turn the ENTIRE preview into a constant parallel-offset path
    // -- the exact "parallel and off-center forever" failure this function's
    // own comment warns about above -- instead of a converging merge.
    // lane_sign is NOT applied: ctx.merge_offset_now is already in the raw
    // +t-axis space Position::GetOffset()/SetLanePos's offset argument use
    // (design doc section 2-4/2-5); that is a DIFFERENT signed space from the
    // lane_sign correction the lat_actions overlay below needs (undoing
    // OSCPrivateAction's lane-sign-agnostic storage), and multiplying by
    // lane_sign here would invert the merge on one side of the road.
    double p0x = pos.GetX();
    double p0y = pos.GetY();
    // Pose fields (z/h/p/r) are read straight off the walked Position. They are inert
    // for the driver (PIDPurePursuitDriver only reads x/y/v) and exist so the OSI
    // planned-path publisher can report a 3D pose without re-deriving one.
    const double p0z = pos.GetZ();
    const double p0h = pos.GetH();
    const double p0p = pos.GetP();
    const double p0r = pos.GetR();
    if (ctx.merge_active && lat_actions.empty())
    {
        const double road_h = pos.GetHRoad();
        const double tx = -std::sin(road_h);
        const double ty =  std::cos(road_h);
        p0x += ctx.merge_offset_now * tx;
        p0y += ctx.merge_offset_now * ty;
    }
    snap.preview.push_back({p0x, p0y, v0, 0.0, p0z, p0h, p0p, p0r});

    // feature:F7 resume-merge: once the preview walk (below) leaves the
    // resolved route track, the merge target is no longer meaningful there --
    // sticky for the rest of this preview (design doc section 2 / handoff
    // item 2, "歩行中にpos.GetTrackId() != merge_track_idになったら以降は0.0").
    bool merge_left_route = false;

    double acc_dist = 0.0;
    for (int i = 1; i <= n_steps; ++i)
    {
        double v_here = SampleTargetSpeed(ctx, acc_dist);
        // Two floors, not one. min_step keeps a single sample from degenerating;
        // span_floor keeps the WHOLE preview from becoming shorter than the
        // driver's lookahead as the vehicle stops. Without the second one the
        // preview collapses to n_steps*min_step (1.5 m) while the lookahead
        // stays clamped at 4.0 m, and pure pursuit ends up amplifying the
        // standing cross-track error into a full-lock command — see
        // TrajectoryShortPlannerConfig::min_preview_span for the measurement.
        const double span_floor = cfg_.min_preview_span / static_cast<double>(n_steps);
        double       ds         = std::max({cfg_.min_step, span_floor, std::fabs(v_here) * dt});

        // [Issue #31] straight-most (0.0), not the -1.0 convenience overload. When the
        // isolated prediction is off-route the -1.0 path RANDOMIZES the connecting road, so the
        // driver-preview trajectory (snap.preview) flickers between the straight and turning
        // connectors frame-to-frame -- the reported "straight Traj / left Traj" alternation. A
        // valid on-route route still steers inside MoveToConnectingRoad.
        int ret = static_cast<int>(pos.MoveAlongS(ds, 0.0, 0.0, true,
                                                  roadmanager::Position::MoveDirectionMode::HEADING_DIRECTION, true));
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
                double future_off = EvaluateTransitionShape(la.shape, la.startVal, la.A, future_p / la.P);
                double delta_t = (future_off - la.current_off) * la.lane_sign;
                px += delta_t * tx;
                py += delta_t * ty;
            }
        }
        else if (ctx.merge_active && ctx.merge_state != nullptr)
        {
            // feature:F7 resume-merge: ABSOLUTE per-point offset (design doc
            // section 2-5), not the relative delta_t/lane_sign pattern above
            // -- that pattern is for the car-anchored lat_actions overlay;
            // this branch is for the lane-center-anchored path (mirrors the
            // anchor SetLanePos above), where the target is the merge
            // trajectory's absolute offset from the ROUTE lane center.
            // lane_sign is NOT applied here (ResumeMergeProfile.hpp's SIGN
            // CONVENTION doc / design doc section 2-4: merge_state's d(t) is
            // already in the raw +t-axis space, like Position::GetOffset()).
            if (pos.GetTrackId() != ctx.merge_track_id)
                merge_left_route = true;

            if (!merge_left_route)
            {
                const double road_h = pos.GetHRoad();
                const double tx = -std::sin(road_h);
                const double ty =  std::cos(road_h);
                const double d_i = EvaluateResumeMergeOffset(*ctx.merge_state, i * dt);
                px += d_i * tx;
                py += d_i * ty;
            }
        }

        snap.preview.push_back({px, py, v_here, i * dt, pos.GetZ(), pos.GetH(), pos.GetP(), pos.GetR()});
    }

    snap.valid = snap.preview.size() >= 2;

    // --- Optional coarse continuation past horizon_s (OSI future_trajectory) ---
    //
    // Runs strictly AFTER the preview loop and only reads the walk state it left
    // behind, so snap.preview is bit-identical to what it was before this block
    // existed whenever extension_horizon_s <= horizon_s (the default, 0).
    //
    // Two deliberate differences from the preview walk:
    //   - coarse dt (extension_dt), because the consumer wants reach, not resolution;
    //   - ds = v * dt with NO min_step / min_preview_span floor. Those floors keep the
    //     driver's pure-pursuit lookahead reachable at a standstill; here they would
    //     march the reported path straight PAST a planned stop. Without them, a v=0
    //     stretch piles samples on the stop position and the reported path visibly
    //     ENDS where the vehicle will end up -- which is the whole point.
    const double ext_horizon = ctx.extension_horizon_s;
    if (snap.valid && ext_horizon > ctx.horizon_s + 1e-6)
    {
        const double dt_ext = (ctx.extension_dt > 1e-3) ? ctx.extension_dt : 0.5;
        const double t0_ext = static_cast<double>(n_steps) * dt;  // time already covered
        const int    n_ext  = static_cast<int>(std::ceil((ext_horizon - t0_ext) / dt_ext));

        for (int k = 1; k <= n_ext; ++k)
        {
            const double v_here = SampleTargetSpeed(ctx, acc_dist);
            const double ds     = std::fabs(v_here) * dt_ext;

            if (ds > 1e-9)
            {
                // Same straight-most (0.0) selector as the preview walk: -1.0 would
                // randomize the connector off-route and make the reported path flicker.
                int ret = static_cast<int>(pos.MoveAlongS(ds, 0.0, 0.0, true,
                                                          roadmanager::Position::MoveDirectionMode::HEADING_DIRECTION, true));
                if (ret == static_cast<int>(roadmanager::Position::ReturnCode::ERROR_GENERIC))
                    break;
                acc_dist += ds;
            }
            // ds == 0 (planned standstill): emit the point anyway, at the same place.
            // The time axis is what tells a consumer "it is stopped here", and dropping
            // the samples would report a path that merely ends early.

            snap.extension.push_back({pos.GetX(), pos.GetY(), v_here, t0_ext + k * dt_ext,
                                      pos.GetZ(), pos.GetH(), pos.GetP(), pos.GetR()});
        }
    }

    return snap;
}

// ===================================================================================
// Preview along a running FollowTrajectoryAction (see the header for why).
// ===================================================================================
ShortPlannerSnapshot TrajectoryShortPlanner::PlanAlongTrajectory(const ShortPlanContext& ctx, void* follow_traj_action) const
{
    ShortPlannerSnapshot snap;
    snap.dt        = ctx.dt;
    snap.horizon_s = ctx.horizon_s;

    Object* obj = ctx.object;
    auto*   fta = static_cast<FollowTrajectoryAction*>(follow_traj_action);
    auto*   traj = fta->traj_;

    const double dt       = (ctx.dt > 1e-3) ? ctx.dt : 0.1;
    const int    n_steps  = std::max(1, static_cast<int>(std::ceil(ctx.horizon_s / dt)));
    const double dt_ext   = (ctx.extension_dt > 1e-3) ? ctx.extension_dt : 0.5;
    const double t0_ext   = static_cast<double>(n_steps) * dt;
    const int    n_ext    = (ctx.extension_horizon_s > ctx.horizon_s + 1e-6)
                              ? static_cast<int>(std::ceil((ctx.extension_horizon_s - t0_ext) / dt_ext))
                              : 0;
    const double traj_len = traj->GetLength();

    // A timed trajectory (TimeReference other than <None/>) dictates WHEN the vehicle
    // is where, so its own timing is the speed source and the mid/long speed ceiling
    // must not be applied -- slowing for a policy would break the timing the scenario
    // asked for. A shape-only trajectory says nothing about speed, so the ordinary
    // ceiling (lead vehicle, traffic light, AEB, ...) applies exactly as on the route.
    const bool timed = (fta->timing_domain_ != FollowTrajectoryAction::TimingDomain::NONE);

    // Continuation state for the stretch past the end of the trajectory. Resolved
    // lazily, on the first sample that runs off the end.
    roadmanager::Position cont;
    bool                  cont_ready   = false;
    bool                  cont_failed  = false;

    auto ResolveContinuation = [&]()
    {
        roadmanager::TrajVertex end_v;
        if (traj->shape_->Evaluate(traj_len, roadmanager::Shape::TRAJ_PARAM_TYPE_S, end_v) != 0)
        {
            cont_failed = true;
            return;
        }
        if (cont.SetInertiaPos(end_v.x, end_v.y, end_v.h) != 0)
        {
            cont_failed = true;  // not on the road network at all
            return;
        }
        // Snapping always finds SOME lane, so distance decides whether it is meaningful.
        // Beyond this the endpoint is not in a lane (a parking-lot manoeuvre, say) and
        // there is no honest continuation to draw.
        if (std::fabs(cont.GetOffset()) > kMaxContinuationOffset)
        {
            cont_failed = true;
            return;
        }
        // Route-preferred (design decision): if the endpoint's road is on the ego's
        // route the walk follows the route through junctions; otherwise it just holds
        // the lane it landed in. Handing MoveAlongS a route it has already left would
        // steer the reported path onto a road the vehicle is not going to take.
        const roadmanager::Route* route = obj->pos_.GetRoute();
        if (route != nullptr && route->IsValid())
        {
            for (const auto& wp : route->minimal_waypoints_)
            {
                if (wp.GetTrackId() == cont.GetTrackId())
                {
                    cont.CopyRoute(obj->pos_);
                    break;
                }
            }
        }
        cont_ready = true;
    };

    // One sample. Returns false once nothing further can be drawn.
    double acc_dist = 0.0;  // distance travelled since "now", for the speed ceiling
    double traj_s   = obj->pos_.GetTrajectoryS();

    auto Floor = [&](double ds, bool floors) -> double
    {
        if (!floors) return ds;
        // Same two floors as the route walk: they keep the driver's pure-pursuit
        // lookahead reachable at a standstill. The extension deliberately drops them so
        // a planned stop is not walked past.
        const double span_floor = cfg_.min_preview_span / static_cast<double>(n_steps);
        return std::max({cfg_.min_step, span_floor, ds});
    };

    auto Sample = [&](double t_ahead, double step_dt, bool floors, std::vector<TrajectoryPoint>& out) -> bool
    {
        roadmanager::TrajVertex v;

        if (timed)
        {
            // Position is a function of time and the speed falls out of the trajectory.
            const double scale  = (fta->timing_domain_ == FollowTrajectoryAction::TimingDomain::TIMING_RELATIVE)
                                      ? fta->timing_scale_
                                      : 1.0;
            const double traj_t = fta->time_ + fta->timing_offset_ + t_ahead * scale;
            if (traj_t <= traj->GetStartTime() + traj->GetDuration() + 1e-6 &&
                traj->shape_->Evaluate(traj_t, roadmanager::Shape::TRAJ_PARAM_TYPE_TIME, v) == 0)
            {
                // Keep acc_dist tracking real distance even here: the ceiling is not
                // applied while ON a timed trajectory, but the continuation past its end
                // uses acc_dist to look the ceiling up, and that lookup has to be at the
                // right distance ahead.
                acc_dist += std::max(0.0, v.s - traj_s);
                traj_s = v.s;
                out.push_back({v.x, v.y, std::fabs(v.speed), t_ahead, v.z, v.h, v.pitch, v.r});
                return true;
            }
        }
        else
        {
            // Shape only: the speed is whatever the longitudinal side commands, so the
            // mid/long ceiling (lead vehicle, traffic light, AEB) applies as on the route.
            const double v_here = SampleTargetSpeed(ctx, acc_dist);
            const double ds     = Floor(std::fabs(v_here) * step_dt, floors);
            if (traj_s + ds <= traj_len &&
                traj->shape_->Evaluate(traj_s + ds, roadmanager::Shape::TRAJ_PARAM_TYPE_S, v) == 0)
            {
                traj_s += ds;
                acc_dist += ds;
                out.push_back({v.x, v.y, v_here, t_ahead, v.z, v.h, v.pitch, v.r});
                return true;
            }
        }

        // Past the end of the trajectory: continue along the road from its endpoint.
        if (!cont_ready && !cont_failed)
        {
            ResolveContinuation();
        }
        if (cont_failed)
        {
            return false;  // nothing honest to draw -- stop emitting points entirely
        }

        const double v_cont = SampleTargetSpeed(ctx, acc_dist);
        const double ds     = Floor(std::fabs(v_cont) * step_dt, floors);
        acc_dist += ds;
        if (ds > 1e-9)
        {
            const int ret = static_cast<int>(cont.MoveAlongS(ds, 0.0, 0.0, true,
                                                             roadmanager::Position::MoveDirectionMode::HEADING_DIRECTION, true));
            if (ret == static_cast<int>(roadmanager::Position::ReturnCode::ERROR_GENERIC))
            {
                cont_failed = true;
                return false;
            }
        }
        out.push_back({cont.GetX(), cont.GetY(), v_cont, t_ahead, cont.GetZ(), cont.GetH(), cont.GetP(), cont.GetR()});
        return true;
    };

    // First point = where the vehicle is on the trajectory right now.
    {
        roadmanager::TrajVertex v0;
        if (traj->shape_->Evaluate(traj_s, roadmanager::Shape::TRAJ_PARAM_TYPE_S, v0) == 0)
        {
            const double v_now = timed ? std::fabs(v0.speed) : SampleTargetSpeed(ctx, 0.0);
            snap.preview.push_back({v0.x, v0.y, v_now, 0.0, v0.z, v0.h, v0.pitch, v0.r});
        }
    }

    for (int i = 1; i <= n_steps; ++i)
    {
        if (!Sample(i * dt, dt, true, snap.preview))
        {
            break;
        }
    }

    snap.valid = snap.preview.size() >= 2;

    for (int k = 1; k <= n_ext && snap.valid; ++k)
    {
        if (!Sample(t0_ext + k * dt_ext, dt_ext, false, snap.extension))
        {
            break;
        }
    }

    return snap;
}

}  // namespace gt_esmini