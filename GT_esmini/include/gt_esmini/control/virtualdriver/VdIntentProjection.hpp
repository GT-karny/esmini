#pragma once

// ============================================================================
// VD intent layer -- the projection itself.
// docs/virtualdriver/design/vd_intent_layer.md sections 2, 3, 6, 8.
//
// ONE FRAME OF FINISHED TELEMETRY IN, TWO ARRAYS OUT. This layer reads what the
// controller has already published and re-expresses it; it computes nothing the
// frame did not already know, and nothing it produces travels back into control.
// That one-directionality is why intent_enabled can default ON: the projection
// cannot change how the vehicle drives, only how it is described.
//
// WHY THE PROJECTION LIVES IN ONE PLACE (design section 2-1). The obvious
// alternative -- each feature naming its own intent -- turns the four vocabularies
// that exist today into six as soon as parking lands. Unifying a vocabulary is
// only enforceable at a single point of projection, which is the same reason
// BuildAdasFunctionReport (AdasFunctionReport.hpp) is one pure function rather
// than a method on each policy.
//
// NOT QUITE PURE, AND WHY. Stable ids and the minimum dwell both need memory, so
// the signature is (state, telemetry, dt) -> (mutated state, two arrays). What it
// deliberately does NOT depend on is the engine, the road network or OSI --
// "the lane change has been waiting on a gap for 3 s and still has not armed" is
// assertable from a hand-built VirtualDriverTelemetry with no map loaded, which is
// a design requirement (section 2-3), not a convenience.
// ============================================================================

#include <string>
#include <vector>

#include "gt_esmini/control/virtualdriver/VdIntent.hpp"
#include "gt_esmini/control/virtualdriver/VirtualDriverTypes.hpp"

namespace gt_esmini
{

// design section 10's config table, as the projection sees it. VirtualDriverConfig
// mirrors these into flat intent_* JSON keys, the same convention lane_change_* /
// overtake_* already follow.
struct VdIntentConfig
{
    // The projection re-reads numbers this frame already produced, so it is free;
    // hence ON by default. The one part that is NOT free is the junction
    // observation scan, and that is a separate key defaulting to 0 -- config
    // carries the distinction between the free projection and the scan you pay for.
    bool   enabled     = true;
    bool   eta_enabled = true;

    // [s] How long an intent that reached ANNOUNCED lingers as COMPLETING/ABANDONED
    // after its condition goes away, so a consumer sees the ending instead of a row
    // vanishing between frames.
    //
    // tier == "safety" is EXEMPT. AEB appearing and disappearing instantly is
    // correct behaviour, and holding its row on screen would read as "still
    // braking". It still gets its single ABANDONED frame -- the exemption is from
    // the dwell, not from being seen to end.
    double min_dwell_s = 1.0;

    // Mirrors VirtualDriverConfig::intent_turn_lookahead_m. Only used as a GATE
    // here: > 0 means telemetry.junction_turn_observed was actually populated this
    // frame, so a TURN may be reported as POSSIBLE. At 0 (the default) that block
    // is all-defaults and must not be read as "no turn ahead".
    double turn_lookahead_m = 0.0;

    // [m] |lane_offset| below which an aborted lateral motion counts as settled, so
    // ABORTING ends. Not "back in the original lane" -- the abort path only stops
    // the offset generator and nothing steers the body back (design section 3-2-2),
    // so where it settles is an observation, not a guarantee.
    double abort_converged_offset_m = 0.3;
};

// One intent as it persists ACROSS frames. Not part of the output; the output rows
// are rebuilt every frame from these.
struct VdIntentTrack
{
    int         id   = 0;
    IntentKind  kind = IntentKind::STOP;
    // (kind, source, subject_osi_id) is the identity. Two stops for two different
    // vehicles are two intents; the same stop for the same vehicle keeps its id as
    // it moves POSSIBLE -> ... -> EXECUTING, which is what makes the id joinable
    // across frames.
    std::string source;
    int         subject_osi_id = -1;

    IntentPhase phase = IntentPhase::POSSIBLE;
    std::string tier  = "comfort";

    // Once ANNOUNCED has happened it cannot un-happen: the signal WAS given. The
    // reported phase is therefore floored at ANNOUNCED from then on, and this flag
    // is also what decides whether the intent appears in intents[] at all.
    bool reached_announced = false;
    // Whether the intent was ever actually acted on. Decides COMPLETING vs
    // ABANDONED when it ends: a stop the ego actually made and then released is
    // COMPLETING, a stop that never got past being announced is ABANDONED.
    bool reached_executing = false;

    // No candidate this frame; the track is being held so its ending is visible.
    bool   expiring     = false;
    double dwell_left_s = 0.0;

    // The blockers seen on the last frame this intent was live. Kept because "why
    // was it given up on" is "what was in the way just before it was given up on" --
    // emptying the list at the moment of cancellation deletes the answer
    // (design section 8-8).
    std::vector<IntentBlocker> blockers;
    std::string                cancel_reason;
};

// Everything the projection remembers between frames. Owned by the controller.
struct VdIntentState
{
    int                        next_id = 1;
    std::vector<VdIntentTrack> tracks;
};

// One frame of output (design section 4-1). Two physically separate arrays, which
// is the verdict boundary: matchers read `intents`, never `reasons`. Structured
// rows cannot carry the gt. / gt.dbg. key prefix the rest of the codebase draws
// that line with, and splitting the array is the same strength of separation --
// the check is one grep for "does a matcher mention intent_reasons".
struct VdIntentFrame
{
    std::vector<VdIntent>       intents;  // external form; ANNOUNCED-or-beyond only
    std::vector<VdIntentReason> reasons;  // internal judgement; every phase
};

// The projection. See the file header for the contract.
//
// dt is the frame step [s], used only for the dwell countdown.
//
// With cfg.enabled false this returns two empty arrays AND clears `state`, so
// turning the layer off leaves nothing behind to be published on a later frame.
VdIntentFrame ProjectVdIntents(VdIntentState&                state,
                               const VirtualDriverTelemetry& telemetry,
                               double                        dt,
                               const VdIntentConfig&         cfg);

// ---------------------------------------------------------------------------
// Exposed only so the eta arithmetic can be tested directly (design section 6).
// ---------------------------------------------------------------------------

// The frame's single distance -> time map, built once and shared by every intent.
//
// Built once, deliberately: if each feature computed its own seconds, two intents
// aimed at the same stop line would report two different times. (Same rule
// PolicyDetail.hpp states for reading a policy channel back -- one measurement,
// one owner.)
struct VdEtaMap
{
    // Cumulative route distance [m] and the time [s] the plan reaches it.
    std::vector<double> s;
    std::vector<double> t;
    // [m] The first PLANNED STOP. Beyond it the map is meaningless: how long the
    // stop lasts is unknown, so "the right turn past this red light is 18 s away"
    // is not a statement the data supports. -1 = no planned stop within the horizon.
    //
    // The alternative -- putting a floor under the speed so the integral keeps
    // going -- is the mistake this codebase already made once, when a control-side
    // floor leaked into the reported future path and drew it past its own planned
    // stop (VirtualDriverTypes.hpp, ShortPlannerSnapshot::extension). Control floors
    // do not belong in outputs. Saying nothing is the correct answer here.
    double s_stop_cutoff = -1.0;
    bool   valid         = false;
};

// preview supplies the first ~3 s exactly (it carries real per-point times);
// beyond it the mid/long v_target_profile is integrated with the trapezoidal rule,
//
//     t(s_k) = sum of 2*ds / (v_i + v_i+1)
//
// i.e. dividing by the segment's MEAN speed. Dividing by one end's speed instead
// always comes out short across a deceleration -- the same error the
// future_trajectory work already made once (signal:ego_planned_path correction ii).
//
// Note what the second stage actually is: v_target_profile is a CEILING, not the
// planned speed (VirtualDriverTypes.hpp), so times past the preview horizon mean
// "when the ego would arrive if it ran at the speed limit the whole way" -- always
// optimistic. That is precisely why the preview wins where it exists.
VdEtaMap BuildVdEtaMap(const VirtualDriverTelemetry& telemetry);

// Time to reach `distance_m` [s], or -1 when the answer does not exist -- past the
// planned stop, outside the map, or with no map at all. -1 is "asked and there is
// no answer", never "not measured", and a consumer must not read it as 0.
double VdEtaAt(const VdEtaMap& map, double distance_m);

// Time to a full stop `distance_m` ahead, under constant deceleration [s].
//
// Exact rather than integrated: decelerating from v to 0 over d has mean speed v/2,
// so t = 2d/v -- and it matches how the planner actually approaches a stop, which
// closes on it as sqrt(2*a*(s_stop - s)) (ManeuverAwareSpeedPlanner). The
// integral is the wrong tool here anyway: its denominator goes to zero at exactly
// the point of interest.
//
// -1 when the ego is already stopped (v_ego <= 0) or the distance is negative:
// a stationary vehicle reaches a point ahead of it at no computable time.
double VdEtaToStop(double distance_m, double v_ego_mps);

}  // namespace gt_esmini
