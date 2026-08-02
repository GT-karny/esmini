#pragma once

// vd-func:FUNC-055 -- autonomous lane-change INITIATION. See
// docs/virtualdriver/design/lane_change_initiation.md (the design of record; this header
// implements it, not the other way around).
//
// This layer answers the question RouteLanePlan.hpp deliberately leaves open: given "you are on
// the wrong lane for the route" (RouteLaneStatus::on_target_lane == false), WHEN should the AD
// start moving toward the target lane, and IS the adjacent lane currently safe to enter?
//
// Same convention as RouteLanePlan.hpp (docs/virtualdriver/design/route_lane_plan_design.md
// section 3-2, reused here per lane_change_initiation.md section 4's "出力の型を PolicyConstraint
// にしない"): a free-function + POD layer living directly under virtualdriver/ (not policies/,
// not an ITrafficPolicy -- "in this gap, may I enter" is not a longitudinal constraint), never
// logs (diagnosis is returned, not logged -- the calling controller decides whether/how to log),
// and is pure wherever the engine allows it, so the numeric core is testable without a loaded
// road network (only ScanAdjacentLaneGap below needs one, mirroring LeadVehicleAware's own split
// between its pure lead_idm:: namespace and its engine-dependent Evaluate()).
//
// What this layer does NOT do (design doc section 1's "覆った想定2" and section 9's scope table):
//   - It does not generate a trajectory. ResumeMergeProfile.hpp (ArmResumeMerge/AdvanceResumeMerge/
//     EvaluateResumeMergeOffset/DisarmResumeMerge) is reused AS-IS by the controller, on a SEPARATE
//     ResumeMergeConfig/ResumeMergeState instance from resume-merge's own (never shared storage --
//     design doc section 8 tail). This layer only decides WHEN to arm that second instance and
//     WHICH single adjacent lane to anchor it to.
//   - It does not use ConflictPointResolver (design doc section 1's "覆った想定1": that resolver's
//     own same-direction filter -- min_cross_angle_deg -- structurally excludes the near-parallel
//     relative motion a lane change is made of).
//   - It moves exactly ONE lane per arm (design doc section 3 "1回に1レーンだけ動く"). A route that
//     needs three lane changes gets three separate arms, each re-evaluating the gap.

#include <string>
#include <vector>

#include "CommonMini.hpp"

namespace scenarioengine
{
class Object;
class Entities;
}  // namespace scenarioengine

namespace gt_esmini
{

// Runtime tuning (design doc section 8's config table -- this struct's field names/defaults are
// the single C++-side source of truth; VirtualDriverConfig.hpp mirrors them into flat
// lane_change_* JSON keys, same convention as ResumeMergeConfig / AdSteeringEnvelopeConfig).
struct LaneChangeInitiationConfig
{
    bool   enabled              = false;  // default OFF (design doc section 8's "設計要件")
    double lead_time_s          = 6.0;    // [s] per-hop decision horizon (design doc section 3)
    double min_lead_distance_m  = 40.0;   // [m] floor on the per-hop distance term
    double reserve_distance_m   = 20.0;   // [m] fixed margin added once, not per hop
    double gap_min_m            = 8.0;    // [m] absolute floor on both gap conditions
    double gap_headway_lead_s   = 1.2;    // [s] ego-speed headway for the forward gap
    double gap_headway_rear_s   = 1.0;    // [s] REAR-VEHICLE-speed headway for the rear gap
    double gap_ttc_min_s        = 3.0;    // [s] minimum time-to-close against an approaching follower
};

// The single next lane to move into, and how many such hops remain to the nearest lane in
// target_lanes (design doc section 3's required_m formula consumes n_remaining directly).
// Pure function of (current_lane_id, target_lanes) -- no engine dependency.
struct LaneHopPlan
{
    bool valid            = false;  // false when target_lanes is empty or already reached (best_dist==0)
    int  next_hop_lane_id = 0;      // the ONE adjacent lane id this hop targets
    int  direction_step   = 0;      // +1 or -1: next_hop_lane_id - current_lane_id, in RAW lane-id space
    int  n_remaining      = 0;      // hop count from current_lane_id to the nearest lane in target_lanes
};

// Nearest-by-lane-id-distance target in `target_lanes`, ties broken toward the first listed
// (target_lanes is small and, per RouteLanePlan's own contract, sorted ascending/deduplicated, so
// this is deterministic in practice). Assumes adjacent driving lanes on one side of a road differ
// by exactly 1 in id (the OpenDRIVE numbering convention every lane_change_initiation.md example
// road follows); a caller feeding a target on the OPPOSITE side of the centerline is out of this
// function's contract (RouteLanePlan bands never mix signs within one road in practice).
LaneHopPlan ComputeLaneHopPlan(int current_lane_id, const std::vector<int>& target_lanes);

// Decision distance (design doc section 3): required_m = n_remaining * max(v_ego*lead_time_s,
// min_lead_distance_m) + reserve_distance_m. n_remaining<=0 => 0.0 (nothing left to do).
double RequiredLaneChangeDistance(int n_remaining, double v_ego, const LaneChangeInitiationConfig& cfg);

// Folds the enabled gate and the decision-distance test (design doc section 3) into one pure,
// testable predicate: false whenever cfg.enabled is false or n_remaining<=0; otherwise true once
// dist_to_connection has counted down to RequiredLaneChangeDistance(...) or below.
// dist_to_connection < 0.0 is RouteLaneStatus's "not applicable" convention (the final band has no
// onward connection to measure against, per RouteLanePlan.hpp) -- treated as due NOW, since there
// is no "later" left to wait for. This is the negative-control surface for
// lane_change_initiation_enabled=false: the CONTROLLER also wraps its entire lane-change block in
// `if (lc_init_cfg_.enabled)`, but this function's own independent enabled check is what makes
// "disabled never initiates" assertable from a unit test with no engine at all.
bool ShouldAttemptLaneChangeHop(int    n_remaining,
                                double dist_to_connection,
                                double v_ego,
                                const LaneChangeInitiationConfig& cfg);

// One adjacent-lane gap sample (design doc section 4): nearest vehicle ahead / behind in the ONE
// lane a hop is considering, with bumper-to-bumper gap and that vehicle's own speed. has_lead/
// has_rear false => no vehicle in range on that side (gap conditions on that side are then
// trivially satisfied -- see EvaluateGapAcceptance).
struct LaneChangeGapSample
{
    bool   has_lead   = false;
    double gap_lead_m = 0.0;
    double v_lead_mps = 0.0;
    bool   has_rear   = false;
    double gap_rear_m = 0.0;
    double v_rear_mps = 0.0;
};

// ScanAdjacentLaneGap's engine-dependent scan, factored out here as an ENGINE-INDEPENDENT pure
// function of the sample + v_ego + config (design doc section 4's table, verbatim): tests exercise
// this directly with synthetic samples, no loaded road network required (mirrors
// LeadVehicleAware's lead_idm:: split). reason is one of "" (accepted) / "lead_gap" / "rear_gap" /
// "rear_ttc" -- the FIRST condition that failed (design doc's three conditions are evaluated
// front-to-back; a caller only needs to know why once).
struct GapAcceptanceResult
{
    bool        accepted = true;
    std::string reason;
};
GapAcceptanceResult EvaluateGapAcceptance(const LaneChangeGapSample&        gap,
                                          double                            v_ego,
                                          const LaneChangeInitiationConfig& cfg);

// The engine-dependent half: scans `entities` for the nearest vehicle strictly `direction_step`
// lanes away from `ego` (design doc section 4's "diff.dLaneId が目標方向へ ±1" -- Road::Delta's
// dLaneId is already signed "increasing left and decreasing to the right", i.e. exactly
// target_lane_id - current_lane_id in raw lane-id space for two lanes on one side of a road, so
// filtering on dLaneId == direction_step IS filtering on "in the hop's target lane"), split into
// nearest-ahead / nearest-behind by the sign of diff.ds. `bothDirections=true` is passed to
// Position::Delta explicitly (LeadVehicleAware passes false and only ever looks ahead;
// lane_change_initiation.md section 1's "覆った想定1" is precisely that the backward search was
// already there in the API, unused). lookahead bounds both directions symmetrically.
LaneChangeGapSample ScanAdjacentLaneGap(const scenarioengine::Object&   ego,
                                        const scenarioengine::Entities& entities,
                                        int                             direction_step,
                                        double                          lookahead);

// The armed/idle latch (design doc section 3 "1回に1レーンだけ動く"; POD, same convention as
// ResumeMergeState). While armed, the CONTROLLER drives a SEPARATE ResumeMergeState/Config
// instance (design doc section 8 tail) to actually generate the hop's trajectory -- this struct
// only remembers WHICH hop is in progress and its LATCHED indicator direction (design doc section
// 6: resolved once at arm time, held for the hop's life, same reason DetectManeuverDir's
// storyboard-LC latch exists -- see ControllerVirtualDriver.cpp).
struct LaneChangeInitiationState
{
    bool         armed               = false;
    unsigned int hop_track_id        = 0;  // id_t's underlying type; the road this hop is on
    int          hop_target_lane_id  = 0;  // the ONE adjacent lane this hop is moving into
    int          direction_step      = 0;  // +1 or -1, raw lane-id space (mirrors LaneHopPlan)
    int          direction_indicator = 0;  // +1 left / -1 right (LaneChangeIndicatorDir convention), latched at arm
    // Diagnostics only: the reason the MOST RECENTLY EVALUATED gap check gave, kept across arm/
    // disarm so telemetry can show "why not yet" even while armed==false. "" means the last
    // evaluated gap was accepted (or no gap has been evaluated yet this run).
    std::string  last_gap_reason;
};

// Begin a hop: latches state.armed=true and every field above. Does NOT touch any ResumeMerge*
// instance -- the caller arms its own separate ResumeMergeState right alongside this call (design
// doc section 8 tail: two independent state machines, one per concern).
void ArmLaneChangeHop(LaneChangeInitiationState& state,
                      unsigned int               hop_track_id,
                      int                        hop_target_lane_id,
                      int                        direction_step,
                      int                        direction_indicator);

// End the current hop (completion, or a suppression per design doc section 2's priority order:
// storyboard lateral action running / resume-merge active / manual lateral override). Only `armed`
// is cleared -- the rest is left in place as a "what was this hop" breadcrumb, same convention as
// DisarmResumeMerge.
void DisarmLaneChangeHop(LaneChangeInitiationState& state);

}  // namespace gt_esmini
