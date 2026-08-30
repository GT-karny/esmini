#pragma once

// ============================================================================
// VD intent layer -- vocabulary and PODs.
// docs/virtualdriver/design/vd_intent_layer.md (the design of record; this
// header implements it, not the other way around).
//
// WHAT THIS LAYER IS. The VD stack already emits, every frame, everything
// needed to say "why am I slowing down" and "what am I about to do". What it
// does not have is a SHAPE: the same "announce -> execute" structure is spelled
// four different ways by four different features (design section 1-1), so every
// consumer re-learns it per feature and a new maneuver means a consumer change.
// This layer folds those four vocabularies into one, by PROJECTION -- it reads
// the finished telemetry and adds two arrays. It is not a new source of truth
// and it has no path back into control (design section 2).
//
// WHY THE VOCABULARY IS ENUMS AND NOT STRINGS (design section 3). A controlled
// vocabulary carried as free strings does not break on typos -- it breaks on
// "another perfectly reasonable word" (stopping vs stop, turn vs turning). That
// failure is silent: no exception, just a counter that never increments.
// Stringification is therefore confined to the three *Name() functions below,
// and test_VdIntent.cpp pins the whole value set in both directions so neither
// side can grow a member the other lacks.
//
// This header deliberately depends on NOTHING but <string>/<vector>: no engine,
// no OSI, no VirtualDriverTypes.hpp. LaneChangeInitiation.hpp includes it (for
// IntentBlocker, which EvaluateGapAcceptance produces) and so does
// VirtualDriverTypes.hpp, so it has to sit below both.
// ============================================================================

#include <string>
#include <vector>

namespace gt_esmini
{

// ---------------------------------------------------------------- IntentKind
// design section 3-1. One value per KIND of thing the ego intends, NOT per
// feature that implements it -- that collapse is the whole point of the layer.
//
// There is deliberately no RESUME (design section 11): its only material is
// derived (a constraint disappearing + positive speed error + throttle), where
// every value here has a first-class producer. Second stage.
//
// There is deliberately no value fed by PolicyConstraint::Kind::YIELD either.
// That enumerator has ZERO producers in the repository, and
// ApplyPolicyConstraints has no case for it at all, so even a future emitter
// would be silently ignored by the speed planner. YIELD below is projected from
// the MAX_SPEED_TO_S constraint StopYieldSignAware actually emits (source ==
// "yield_sign"), which is a real, observed thing. Binding the projection to the
// dead enumerator would have manufactured a vocabulary value that could never
// light up.
enum class IntentKind
{
    STOP,         // come to a full stop
    SLOW,         // reduce speed, without stopping
    LANE_CHANGE,  // move into an adjacent lane
    TURN,         // turn left/right at a junction
    OVERTAKE,     // pass a slower lead (the SUPERIOR intent over its two lane changes)
    YIELD,        // give way (not necessarily by stopping)
};
constexpr int kIntentKindCount = 6;

// --------------------------------------------------------------- IntentPhase
// design section 3-2. The ordering is meaningful: everything from ANNOUNCED
// onward is EXTERNALLY OBSERVABLE (an indicator, a brake lamp, actual motion),
// and that split is exactly the verdict boundary section 4 turns into two
// separate arrays. POSSIBLE/PLANNED are internal deliberation and never reach
// intents[].
//
// COMPLETING and ABORTING are NOT one value (design section 3-2-1). Both look
// like "lateral motion heading back", but one is the plan running to its end
// and the other is the plan being thrown away; an HMI says different words for
// them and verification asks different questions of them. Collapsed into one
// value, an overtake's return leg and a failed lane change would render
// identically.
//
// ABORTING vs ABANDONED is the "was anything already moving" split: ABANDONED
// is a cancellation with no motion behind it (signalled, then thought better of
// it), ABORTING is a cancellation whose lateral motion is still settling.
//
// IMPORTANT -- what ABORTING does NOT promise (design section 3-2-2): the
// controller's abort path only STOPS the offset generator; nothing steers the
// body back. So ABORTING means "the aborted lateral motion has not settled on
// any lane centre yet", not "returning to the original lane". Where it settles
// depends on which side of the boundary the body was on and who picked up the
// lateral domain. Actually guaranteeing a return is a separate task on
// vd-component:lane-change-initiation and is explicitly out of scope here.
enum class IntentPhase
{
    POSSIBLE,    // the requirement is visible; not decided            (not observable)
    PLANNED,     // decided; nothing done yet                          (not observable)
    ANNOUNCED,   // signalled (indicator / brake lamp)                 OBSERVABLE
    EXECUTING,   // under way                                          OBSERVABLE
    COMPLETING,  // running to the end (an overtake's return leg, ...) OBSERVABLE
    ABORTING,    // given up on; the motion it started has not settled OBSERVABLE
    ABANDONED,   // cancelled, with no motion behind it                OBSERVABLE (as a disappearance)
};
constexpr int kIntentPhaseCount = 7;

// --------------------------------------------------------------- IntentWhere
// design section 8-2 (2). WHERE a blocker is, in the driver's frame. Kept
// alongside code rather than instead of it because the two audiences differ: an
// HMI speaks in positions ("the car behind you"), verification matches on
// identifiers. Fold them into one and whichever lost has to carry a lookup
// table.
//
// NONE is the ABSENCE of a position, not a fifth position (agreed 2026-08-29;
// design section 1-4 覆った想定4). Four of section 8-4's blocker codes -- and
// they all have real producers -- are properties of the maneuver rather than of
// some other road user: no_target_lane, route_budget, no_passing_lane,
// suppressed. It serializes as "", the same "" == not applicable that
// gap_reason / blocked_reason / route_lane.diagnostic already use, so every
// element of blockers[] has the same shape and a consumer never branches on
// whether a key exists.
enum class IntentWhere
{
    NONE,      // no position applies (a property of the maneuver, not of another vehicle)
    FRONT,
    SIDE,      // alongside -- longitudinally overlapping (design section 8-3)
    REAR,
    ONCOMING,
};
constexpr int kIntentWhereCount = 5;

// The ONLY place these vocabularies become strings. Every enumerator maps to a
// distinct token and every token maps back (pinned by test_VdIntent.cpp, same
// shape as test_AdasSlotTable.cpp's pinning of the OSI enums).
const char* IntentKindName(IntentKind kind);
const char* IntentPhaseName(IntentPhase phase);
const char* IntentWhereName(IntentWhere where);

// ------------------------------------------------------------- blocker codes
// design section 8-4's code column, in full. Named constants rather than a
// fourth enum: unlike kind/phase/where these are produced at scattered call
// sites that already speak in strings (gap_reason, blocked_reason), and the
// constants give those sites compile-time checking without forcing a
// translation layer at every one. Same convention as kAbortReason* in
// LaneChangeInitiation.hpp and kDriverOverrideAccel in AdasFunctionReport.hpp.
// test_VdIntent.cpp asserts the set is distinct and complete.
inline constexpr const char* kBlockerLeadGap       = "lead_gap";         // front vehicle too close
inline constexpr const char* kBlockerRearGap       = "rear_gap";         // follower too close
inline constexpr const char* kBlockerRearTtc       = "rear_ttc";         // follower closing too fast
inline constexpr const char* kBlockerSideOverlap   = "side_overlap";     // bodies overlap longitudinally
inline constexpr const char* kBlockerNoTargetLane  = "no_target_lane";   // the route offers nowhere to go
inline constexpr const char* kBlockerOncomingGap   = "oncoming_gap";     // not enough room against oncoming traffic
inline constexpr const char* kBlockerRouteBudget   = "route_budget";     // the pass would cost the next connection
inline constexpr const char* kBlockerNoPassingLane = "no_passing_lane";  // no lane to pass in
inline constexpr const char* kBlockerSuppressed    = "suppressed";       // a higher-priority lateral owner holds the domain

// ------------------------------------------------------------ quantity names
// design section 8-2 (3): the unit lives in the KEY, never in the value, the
// same rule PolicyDetail.hpp applies to its own keys.
inline constexpr const char* kQuantityGapM    = "gap_m";
inline constexpr const char* kQuantityTtcS    = "ttc_s";
inline constexpr const char* kQuantityBudgetM = "budget_m";

// ------------------------------------------------------------- IntentBlocker
// design section 8-2. ONE reason the ego cannot do the thing it intends. A
// vector of these, not a single string, because "the front is blocked AND the
// rear is blocked" is precisely the state a driver most needs to distinguish,
// and a single string collapses it to whichever condition happened to be
// evaluated first.
//
// measured/required as a PAIR is the other half of the point: "the follower is
// too close" is worth much less than "the follower is 6.2 m away, 11.3 m
// needed", which tells you how much longer you are waiting.
struct IntentBlocker
{
    IntentWhere where          = IntentWhere::NONE;
    int         subject_osi_id = -1;  // OsiIdOf() -- the OSI id space, NOT the scenario entity index.
                                      // -1 = no vehicle is involved. The two numbers are genuinely
                                      // different (control/common/OsiIdentity.hpp), and only this
                                      // one joins against an OSI GroundTruth recording, i.e. only
                                      // this one lets a consumer point at the actual car on screen.
    std::string code;                 // one of the kBlocker* constants above
    // "" means this blocker has NO measurable quantity (no_passing_lane / suppressed /
    // no_target_lane). measured/required are then meaningless and MUST NOT be read as 0 --
    // the same absent-is-not-zero rule PolicyDetail::TryGetDetail enforces by returning false.
    std::string quantity;
    double      measured = 0.0;
    double      required = 0.0;
};

// ------------------------------------------------------------------ VdIntent
// design section 4-1's intents[] row -- the EXTERNAL FORM. Only intents that
// reached ANNOUNCED or beyond appear here, which is what makes this array
// verdict-usable: everything in it corresponds to something an outside observer
// could in principle have seen (an indicator, a brake lamp, the vehicle
// moving).
//
// Matchers may read this array. They may NOT read VdIntentReason below.
struct VdIntent
{
    int         id    = -1;  // stable across frames; joins this row to its VdIntentReason
    IntentKind  kind  = IntentKind::STOP;
    IntentPhase phase = IntentPhase::ANNOUNCED;

    // [m] to whatever the intent is about. -1 = unknown / not applicable (an
    // OVERTAKE has no single distance, a MAX_SPEED cap applies everywhere).
    double distance_m = -1.0;

    // [s] design section 6. -1 means "computed, and the answer does not exist"
    // -- NOT "not measured", and never to be read as 0. The map from distance to
    // time is cut off at the first planned stop, because beyond a stop the
    // arrival time depends on how long the stop lasts, which is unknown. Saying
    // nothing is the correct output there; putting a floor under the speed to
    // keep the arithmetic alive is the mistake this codebase already made once
    // (the reported future path marched past its own planned stop -- see
    // ShortPlannerSnapshot::extension in VirtualDriverTypes.hpp).
    double eta_s = -1.0;

    int subject_osi_id = -1;  // the other road user this intent is about; -1 = none

    // World position of the intent's anchor. has_position=false means "not
    // known", which (0,0) cannot express -- it is a legal coordinate.
    bool   has_position = false;
    double x            = 0.0;
    double y            = 0.0;
};

// ------------------------------------------------------------ VdIntentReason
// design section 4-1's intent_reasons[] row -- the INTERNAL JUDGEMENT. Carries
// every phase including POSSIBLE/PLANNED.
//
// WHY A SEPARATE ARRAY AND NOT A KEY PREFIX (design section 4-2). The existing
// verdict boundary is drawn with gt. / gt.dbg. key prefixes, which a lint can
// strip mechanically. Structured rows have no prefix to hang that on, so the
// array is physically split instead -- the check stays a single grep for "does
// any matcher mention intent_reasons".
//
// It is NOT named "debug" even though it maps onto signal_catalog's
// exposure=debug, because for an HMI this is the primary content: telling a
// person why the car is braking is not debugging. The name says what the thing
// is; the trust policy lives in the catalog and the lint.
//
// WHY blockers[] SITS ON THIS SIDE (design section 8-5): measured is the AD's
// OWN measurement of the world. A matcher wanting to verify "the AD correctly
// refused a 6 m gap" must measure that gap from GroundTruth itself -- reading
// the AD's number would make the check ratify the AD's mistakes.
struct VdIntentReason
{
    int         id    = -1;
    IntentKind  kind  = IntentKind::STOP;
    IntentPhase phase = IntentPhase::POSSIBLE;

    // MOTIVE -- why the ego is doing this. Always exactly one (design section 8).
    // For policy-driven intents this is PolicyConstraint::source verbatim
    // ("traffic_light" / "lead_vehicle" / "conflict_point" / "crosswalk" /
    // "stop_sign" / "yield_sign" / "aeb"). The intents that are not policy-driven
    // use these tokens, deliberately NOT reused from that set:
    //   "route"      -- LANE_CHANGE and TURN: the route requires it.
    //   "slow_lead"  -- OVERTAKE. Not "lead_vehicle": that string means "the
    //                   lead-vehicle POLICY emitted a constraint", a different
    //                   fact that can hold or not hold independently of an
    //                   overtake being considered.
    //   "curve" / "speed_limit" / "junction" -- SLOW from a road-geometry
    //                   ceiling (MidLongConstraint::kind verbatim).
    std::string source;

    // PolicyConstraint::Tier as a token ("comfort"/"courtesy"/"compliance"/"safety").
    // Non-policy intents report "comfort". safety is load-bearing, not
    // decoration: it exempts the intent from the minimum dwell (see
    // VdIntentConfig::min_dwell_s).
    std::string tier = "comfort";

    // design section 5. WHICH intent is actually setting the speed / the lateral
    // motion right now -- the winner ApplyPolicyConstraints' min() fold used to
    // throw away. Two flags and not one, because longitudinal and lateral are
    // legitimately decided by different intents at the same instant (stopped at
    // a red light while signalling a lane change is a normal state). At most one
    // intent per frame has each flag.
    //
    // NOTE binding_lat here is "which INTENT owns the lateral motion", a question
    // strictly inside the AD. It is unrelated to OverrideManager's domain
    // ownership (docs/virtualdriver/design/domain_split_ownership.md), which asks
    // whether the AD or the human owns the domain at all.
    bool binding_lon = false;
    bool binding_lat = false;

    // The decision is latched and will not be reconsidered this approach. Only
    // TrafficLightAware actually has such a latch today
    // (gt.traffic_light.committed); everything else reports false.
    bool committed = false;

    // OBSTRUCTION -- why the ego cannot do it. Zero or more (design section 8-2).
    std::vector<IntentBlocker> blockers;

    // CANCELLATION -- why the ego gave up. Non-empty only in ABANDONED/ABORTING.
    std::string cancel_reason;
};

// design section 8-8's cancel_reason for "the thing it was reacting to stopped
// existing" (the light went green, the pedestrian finished crossing). Spelled
// out rather than left as "" so a cleared constraint stays distinguishable from
// an unfilled field.
inline constexpr const char* kCancelConstraintCleared = "constraint_cleared";
// The route itself changed under the intent (route_lane.rerouted).
inline constexpr const char* kCancelRerouted = "rerouted";

}  // namespace gt_esmini
