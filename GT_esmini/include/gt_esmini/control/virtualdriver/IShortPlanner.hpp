#pragma once

#include "gt_esmini/control/virtualdriver/VirtualDriverTypes.hpp"
#include "gt_esmini/control/virtualdriver/ResumeMergeProfile.hpp"

namespace scenarioengine
{
class Object;
}

namespace gt_esmini
{

// Per-frame inputs for the short planner.
struct ShortPlanContext
{
    scenarioengine::Object* object    = nullptr;  // ego; route lives in object->pos_
    double                  sim_time  = 0.0;
    double                  horizon_s = 3.0;        // seconds to look ahead
    double                  dt        = 0.1;        // sampling step [s]
    // Speed boundary from the mid/long planner (Phase 2). When null (Phase 1),
    // the planner falls back to fallback_speed (the ego's commanded target).
    const MidLongPlannerSnapshot* v_target = nullptr;
    double                        fallback_speed = 0.0;  // [m/s], used when v_target == nullptr
    // Forward control-point offset [m]: advance the preview anchor this far ahead
    // along the route so the lane-center anchor sits at the vehicle's front
    // (axle/bumper) rather than its origin (≈ rear). The planner ignores it while
    // a storyboard lateral maneuver is active and echoes the value it used back in
    // ShortPlannerSnapshot::control_point_offset. 0 = anchor at the origin (P2 issue 2).
    double                        control_point_offset = 0.0;

    // feature:F7 resume-merge (docs/virtualdriver/resume_merge_trajectory_design.md).
    // The CONTROLLER resolves the ROUTE lane and drives the ResumeMergeProfile
    // state machine (ArmResumeMerge/AdvanceResumeMerge/DisarmResumeMerge); this
    // planner only CONSUMES the result -- no route/lane resolution or state
    // machine here (design doc section 4: "コントローラがルート車線解決・状態機械・
    // テレメトリを持つ。プランナーは解決済みアンカーとオフセット列を消費するだけ").
    // All defaults below preserve today's behavior: merge_active=false means
    // TrajectoryShortPlanner's pre-existing current-lane-anchor path runs
    // completely unchanged (HARD INVARIANT when resume_merge_enabled=false).
    bool         merge_active   = false;  // true while a resume-merge is in progress this frame
    unsigned int merge_track_id = 0;      // resolved ROUTE track id this frame (id_t's underlying type; only meaningful when merge_active)
    int          merge_lane_id  = 0;      // resolved ROUTE lane id this frame (only meaningful when merge_active)
    // This frame's merge offset target AT "now" (t_ahead=0) -- the controller
    // computes EvaluateResumeMergeOffset(*merge_state, 0.0) AFTER advancing
    // merge_state for this frame, and hands it here for the FIRST preview
    // point (index 0). Later preview points (i>=1) are evaluated fresh from
    // merge_state at t_ahead = i*dt -- both are the same function applied at
    // different look-ahead times (see EvaluateResumeMergeOffset's doc).
    double       merge_offset_now = 0.0;
    // Captured hand-over state + selected trajectory (design doc section 8-3).
    // Non-null only when merge_active is true; owned by the controller
    // (resume_merge_state_ member) and only READ here.
    const ResumeMergeState* merge_state = nullptr;
};

// Short-horizon trajectory planner.
//
// Produces an equal-Δt (x,y,v,t) preview that the IDriverModel tracks. The
// geometry (x,y) is read by walking the route ahead; the speed profile (v) is
// taken from v_target when available, else fallback_speed. Time-domain sampling
// is intentional: long-horizon speed shaping (e.g. slowing for a turn) is the
// job of IMidLongPlanner, which works in the route s domain.
class IShortPlanner
{
public:
    virtual ~IShortPlanner() = default;
    virtual ShortPlannerSnapshot Plan(const ShortPlanContext& ctx) = 0;
};

}  // namespace gt_esmini
