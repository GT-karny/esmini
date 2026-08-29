// vd_intent_layer.md sections 3 / 6 / 8 / 9 -- the intent projection.
//
// This is the layer's main battleground and it needs no engine at all: the projection reads a
// finished VirtualDriverTelemetry and nothing else, so "the lane change has been waiting on a gap
// and still has not armed" is a POD assembled by hand. That was a design requirement (section
// 2-3), not a convenience -- a projection testable only through a driving scenario would be
// verified by the same runs it is supposed to explain.
//
// Structure follows section 9: the projection table (9-2 items 1-2), the stateful rules (3-5, 9,
// 10), then the negative controls (9-3) -- which are not optional. A layer checked only where it
// fires will happily contain a field that is always true.

#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "gt_esmini/control/virtualdriver/VdIntentProjection.hpp"

using namespace gt_esmini;

namespace
{

VdIntentConfig Cfg()
{
    VdIntentConfig cfg;
    cfg.enabled                  = true;
    cfg.eta_enabled              = true;
    cfg.min_dwell_s              = 1.0;
    cfg.turn_lookahead_m         = 0.0;
    cfg.abort_converged_offset_m = 0.3;
    return cfg;
}

PolicyConstraint Stop(double s, const char* source, PolicyConstraint::Tier tier = PolicyConstraint::Tier::COMPLIANCE)
{
    PolicyConstraint c;
    c.kind   = PolicyConstraint::Kind::STOP_AT_S;
    c.s      = s;
    c.source = source;
    c.tier   = tier;
    return c;
}

PolicyConstraint Cap(double v, double s, const char* source)
{
    PolicyConstraint c;
    c.kind   = PolicyConstraint::Kind::MAX_SPEED_TO_S;
    c.value  = v;
    c.s      = s;
    c.source = source;
    return c;
}

// A telemetry frame with just enough filled in to be meaningful: moving, brake lamp off, no
// maneuvers, no constraints.
VirtualDriverTelemetry Frame()
{
    VirtualDriverTelemetry t;
    t.speed                     = 13.9;
    t.brake_light_on            = false;
    t.policy.valid              = true;
    t.midlong.valid             = true;
    t.midlong.binding_constraint_index = -1;
    t.overtake.phase            = "idle";
    t.route_lane.valid          = false;
    t.lane_change.dist_to_connection = -1.0;
    return t;
}

// NOTE both helpers return a pointer INTO `frame`. Callers must keep the frame alive in a
// named local -- FindReason(ProjectVdIntents(...), ...) hands back a pointer into a temporary
// that dies at the end of the statement, and the resulting read is undefined. It bit this file
// once: the int fields still looked right and only the std::string members came back empty,
// which reads exactly like "the projection forgot to set source".
const VdIntentReason* FindReason(const VdIntentFrame& frame, IntentKind kind)
{
    for (const auto& r : frame.reasons)
    {
        if (r.kind == kind) return &r;
    }
    return nullptr;
}

const VdIntent* FindIntent(const VdIntentFrame& frame, IntentKind kind)
{
    for (const auto& i : frame.intents)
    {
        if (i.kind == kind) return &i;
    }
    return nullptr;
}

bool HasKind(const VdIntentFrame& frame, IntentKind kind)
{
    return FindReason(frame, kind) != nullptr;
}

}  // namespace

// ══════════════════════════ 9-2 item 2: the vocabulary as a SET ══════════════════════════
//
// Same shape as test_AdasSlotTable.cpp's pinning of the OSI enums. A controlled vocabulary
// carried as strings does not fail with a typo -- it fails with another reasonable word, and the
// symptom is a counter that never increments.

TEST(VdIntentVocabulary, EveryKindMapsToADistinctToken)
{
    const IntentKind all[] = {IntentKind::STOP,  IntentKind::SLOW,     IntentKind::LANE_CHANGE,
                              IntentKind::TURN,  IntentKind::OVERTAKE, IntentKind::YIELD};
    ASSERT_EQ(sizeof(all) / sizeof(all[0]), static_cast<size_t>(kIntentKindCount))
        << "kIntentKindCount and the enumerator list have drifted apart";

    std::set<std::string> tokens;
    for (IntentKind kind : all)
    {
        const std::string token = IntentKindName(kind);
        EXPECT_FALSE(token.empty()) << "an IntentKind serializes to the empty string";
        EXPECT_TRUE(tokens.insert(token).second) << "duplicate token: " << token;
    }
    EXPECT_EQ(tokens.size(), static_cast<size_t>(kIntentKindCount));
}

TEST(VdIntentVocabulary, EveryPhaseMapsToADistinctToken)
{
    const IntentPhase all[] = {IntentPhase::POSSIBLE,   IntentPhase::PLANNED,  IntentPhase::ANNOUNCED,
                               IntentPhase::EXECUTING,  IntentPhase::COMPLETING, IntentPhase::ABORTING,
                               IntentPhase::ABANDONED};
    ASSERT_EQ(sizeof(all) / sizeof(all[0]), static_cast<size_t>(kIntentPhaseCount));

    std::set<std::string> tokens;
    for (IntentPhase phase : all)
    {
        const std::string token = IntentPhaseName(phase);
        EXPECT_FALSE(token.empty());
        EXPECT_TRUE(tokens.insert(token).second) << "duplicate token: " << token;
    }
    EXPECT_EQ(tokens.size(), static_cast<size_t>(kIntentPhaseCount));
}

// where is the one vocabulary with a deliberately EMPTY token: NONE means "no position applies",
// not a fifth position. The four real values must still be distinct and non-empty.
TEST(VdIntentVocabulary, WhereHasFourPositionsPlusAnExplicitAbsence)
{
    const IntentWhere all[] = {IntentWhere::NONE, IntentWhere::FRONT, IntentWhere::SIDE,
                               IntentWhere::REAR, IntentWhere::ONCOMING};
    ASSERT_EQ(sizeof(all) / sizeof(all[0]), static_cast<size_t>(kIntentWhereCount));

    EXPECT_STREQ(IntentWhereName(IntentWhere::NONE), "");

    std::set<std::string> tokens;
    for (IntentWhere where : all)
    {
        if (where == IntentWhere::NONE) continue;
        const std::string token = IntentWhereName(where);
        EXPECT_FALSE(token.empty()) << "a real position serialized as the absence marker";
        EXPECT_TRUE(tokens.insert(token).second) << "duplicate token: " << token;
    }
    EXPECT_EQ(tokens.size(), 4u);
}

// Every blocker code must be distinct, and every one must have a producer somewhere in the
// codebase. The second half is a grep, not a test -- but the first half catches a copy-paste that
// would make two different obstacles indistinguishable to a matcher.
TEST(VdIntentVocabulary, EveryBlockerCodeIsDistinct)
{
    const char* codes[] = {kBlockerLeadGap,      kBlockerRearGap,       kBlockerRearTtc,
                           kBlockerSideOverlap,  kBlockerNoTargetLane,  kBlockerOncomingGap,
                           kBlockerRouteBudget,  kBlockerNoPassingLane, kBlockerSuppressed};
    std::set<std::string> seen;
    for (const char* code : codes)
    {
        EXPECT_TRUE(seen.insert(code).second) << "duplicate blocker code: " << code;
    }
    EXPECT_EQ(seen.size(), 9u);
}

// ══════════════════════════ 9-2 item 1: the projection table ══════════════════════════

TEST(VdIntentProjection, ATrafficLightStopClimbsPossiblePlannedAnnouncedExecuting)
{
    VdIntentState state;

    // POSSIBLE -- the constraint is there but the latch has not closed.
    VirtualDriverTelemetry t = Frame();
    t.policy.constraints     = {Stop(60.0, "traffic_light")};
    t.policy.detail          = {{"gt.traffic_light.committed", "false"}};
    auto frame               = ProjectVdIntents(state, t, 0.05, Cfg());
    const VdIntentReason* r  = FindReason(frame, IntentKind::STOP);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->phase, IntentPhase::POSSIBLE);
    EXPECT_EQ(r->source, "traffic_light");
    EXPECT_FALSE(r->committed);
    // Not observable yet, so it must not be in the verdict-facing array.
    EXPECT_EQ(FindIntent(frame, IntentKind::STOP), nullptr);

    // PLANNED -- committed, but nothing an outside observer could see yet.
    t.policy.detail = {{"gt.traffic_light.committed", "true"}};
    frame           = ProjectVdIntents(state, t, 0.05, Cfg());
    r               = FindReason(frame, IntentKind::STOP);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->phase, IntentPhase::PLANNED);
    EXPECT_TRUE(r->committed);
    EXPECT_EQ(FindIntent(frame, IntentKind::STOP), nullptr);

    // ANNOUNCED -- the brake lamp is on, but something else is setting the speed.
    t.brake_light_on = true;
    frame            = ProjectVdIntents(state, t, 0.05, Cfg());
    r                = FindReason(frame, IntentKind::STOP);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->phase, IntentPhase::ANNOUNCED);
    EXPECT_FALSE(r->binding_lon);
    EXPECT_NE(FindIntent(frame, IntentKind::STOP), nullptr) << "ANNOUNCED must reach intents[]";

    // EXECUTING -- this constraint is the one the planner is actually obeying.
    t.midlong.binding_constraint_index = 0;
    frame                              = ProjectVdIntents(state, t, 0.05, Cfg());
    r                                  = FindReason(frame, IntentKind::STOP);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->phase, IntentPhase::EXECUTING);
    EXPECT_TRUE(r->binding_lon);
}

// The id must survive the whole climb -- that is what makes it joinable across frames, and what
// lets intents[] and intent_reasons[] be matched up at all.
TEST(VdIntentProjection, TheIdIsStableAcrossPhases)
{
    VdIntentState          state;
    VirtualDriverTelemetry t = Frame();
    t.policy.constraints     = {Stop(60.0, "traffic_light")};

    const int id0 = ProjectVdIntents(state, t, 0.05, Cfg()).reasons.front().id;
    t.brake_light_on = true;
    const int id1 = ProjectVdIntents(state, t, 0.05, Cfg()).reasons.front().id;
    t.midlong.binding_constraint_index = 0;
    const auto frame = ProjectVdIntents(state, t, 0.05, Cfg());

    EXPECT_EQ(id0, id1);
    EXPECT_EQ(id1, frame.reasons.front().id);
    ASSERT_FALSE(frame.intents.empty());
    EXPECT_EQ(frame.intents.front().id, id0) << "intents[] and intent_reasons[] must join on id";
}

// design section 3-1-1's invariant, stated as a test because it is the thing a "tidier"
// implementation would break: the projection must NOT decide from WHICH POLICY raised the
// constraint. Giving way and stopping is two rows, at once.
TEST(VdIntentProjection, YieldingAndStoppingAppearAsTwoSimultaneousRows)
{
    VdIntentState          state;
    VirtualDriverTelemetry t = Frame();
    t.policy.constraints     = {
        Cap(2.0, 30.0, "yield_sign"),          // the sign only creeps
        Stop(28.0, "conflict_point"),          // the resolver is what actually stops
    };
    t.policy.detail = {{"gt.conflict_point.other_osi_id", "17"}};

    const auto frame = ProjectVdIntents(state, t, 0.05, Cfg());

    const VdIntentReason* yield = FindReason(frame, IntentKind::YIELD);
    const VdIntentReason* stop  = FindReason(frame, IntentKind::STOP);
    ASSERT_NE(yield, nullptr) << "the yield sign must project as YIELD, not vanish into the stop";
    ASSERT_NE(stop, nullptr);
    EXPECT_EQ(yield->source, "yield_sign");
    EXPECT_EQ(stop->source, "conflict_point");
    EXPECT_NE(yield->id, stop->id);
}

TEST(VdIntentProjection, TheSubjectComesFromThePolicysOwnDiagnostic)
{
    VdIntentState          state;
    VirtualDriverTelemetry t = Frame();
    t.policy.constraints     = {Stop(20.0, "lead_vehicle")};
    t.policy.detail          = {{"gt.lead_vehicle.lead_osi_id", "11"}};
    t.brake_light_on         = true;

    const auto      frame  = ProjectVdIntents(state, t, 0.05, Cfg());
    const VdIntent* intent = FindIntent(frame, IntentKind::STOP);
    ASSERT_NE(intent, nullptr);
    EXPECT_EQ(intent->subject_osi_id, 11);
}

// An absent diagnostic must stay -1, never 0 -- 0 is a legitimate OSI id.
TEST(VdIntentProjection, AnAbsentSubjectDiagnosticStaysMinusOne)
{
    VdIntentState          state;
    VirtualDriverTelemetry t = Frame();
    t.policy.constraints     = {Stop(20.0, "lead_vehicle")};  // no detail at all
    t.brake_light_on         = true;

    const VdIntentFrame frame  = ProjectVdIntents(state, t, 0.05, Cfg());
    const VdIntent*     intent = FindIntent(frame, IntentKind::STOP);
    ASSERT_NE(intent, nullptr);
    EXPECT_EQ(intent->subject_osi_id, -1);
}

TEST(VdIntentProjection, ARoadCeilingProjectsAsSlowWithItsOwnKindAsTheSource)
{
    VdIntentState          state;
    VirtualDriverTelemetry t = Frame();
    t.midlong.constraints    = {{40.0, 100.0, 200.0, 8.0, "curve"}};

    const auto            frame = ProjectVdIntents(state, t, 0.05, Cfg());
    const VdIntentReason* r     = FindReason(frame, IntentKind::SLOW);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->source, "curve");
    EXPECT_EQ(r->phase, IntentPhase::POSSIBLE);
    // binding_lon reports what the PLANNER decided, and the planner only ever names policy
    // constraints. Stretching it to cover a road ceiling would make the flag lie.
    EXPECT_FALSE(r->binding_lon);
}

TEST(VdIntentProjection, ARoadCeilingReachesExecutingOnlyWhenNoPolicyConstraintIsBinding)
{
    VdIntentState          state;
    VirtualDriverTelemetry t = Frame();
    t.midlong.constraints    = {{40.0, 100.0, 200.0, 8.0, "curve"}};
    t.brake_light_on         = true;

    // No policy constraint is binding -> the nearest road ceiling is what is doing it.
    t.midlong.binding_constraint_index = -1;
    EXPECT_EQ(FindReason(ProjectVdIntents(state, t, 0.05, Cfg()), IntentKind::SLOW)->phase,
              IntentPhase::EXECUTING);

    // A policy constraint IS binding -> the curve is not the reason for this braking.
    VdIntentState state2;
    t.policy.constraints               = {Stop(20.0, "traffic_light")};
    t.midlong.binding_constraint_index = 0;
    EXPECT_EQ(FindReason(ProjectVdIntents(state2, t, 0.05, Cfg()), IntentKind::SLOW)->phase,
              IntentPhase::ANNOUNCED);
}

TEST(VdIntentProjection, LaneChangeClimbsPossiblePlannedAnnouncedExecuting)
{
    VdIntentState          state;
    VirtualDriverTelemetry t          = Frame();
    t.route_lane.valid                = true;
    t.route_lane.on_target_lane       = false;
    t.lane_change.n_remaining         = 2;
    t.lane_change.required_m          = 100.0;
    t.lane_change.dist_to_connection  = 300.0;  // still far
    auto frame                        = ProjectVdIntents(state, t, 0.05, Cfg());
    ASSERT_NE(FindReason(frame, IntentKind::LANE_CHANGE), nullptr);
    EXPECT_EQ(FindReason(frame, IntentKind::LANE_CHANGE)->phase, IntentPhase::POSSIBLE);
    EXPECT_EQ(FindReason(frame, IntentKind::LANE_CHANGE)->source, "route");

    t.lane_change.dist_to_connection = 90.0;  // inside the decision distance, not armed
    frame                            = ProjectVdIntents(state, t, 0.05, Cfg());
    EXPECT_EQ(FindReason(frame, IntentKind::LANE_CHANGE)->phase, IntentPhase::PLANNED);
    EXPECT_EQ(FindIntent(frame, IntentKind::LANE_CHANGE), nullptr) << "PLANNED is not observable";

    t.lane_change.signal_active = true;
    frame                       = ProjectVdIntents(state, t, 0.05, Cfg());
    EXPECT_EQ(FindReason(frame, IntentKind::LANE_CHANGE)->phase, IntentPhase::ANNOUNCED);
    EXPECT_NE(FindIntent(frame, IntentKind::LANE_CHANGE), nullptr);

    t.lane_change.armed = true;
    frame               = ProjectVdIntents(state, t, 0.05, Cfg());
    EXPECT_EQ(FindReason(frame, IntentKind::LANE_CHANGE)->phase, IntentPhase::EXECUTING);
    EXPECT_TRUE(FindReason(frame, IntentKind::LANE_CHANGE)->binding_lat);
}

TEST(VdIntentProjection, OvertakePhasesMapOntoAnnouncedExecutingCompleting)
{
    struct Case
    {
        const char* phase;
        IntentPhase expected;
    };
    const Case cases[] = {
        {"signal_out", IntentPhase::ANNOUNCED},
        {"moving_out", IntentPhase::EXECUTING},
        {"pass", IntentPhase::EXECUTING},
        {"signal_back", IntentPhase::COMPLETING},
        {"moving_back", IntentPhase::COMPLETING},
    };

    for (const Case& c : cases)
    {
        VdIntentState          state;
        VirtualDriverTelemetry t = Frame();
        t.overtake.phase         = c.phase;
        t.overtake.considered    = true;
        t.overtake.lead_osi_id   = 11;

        const VdIntentFrame   frame = ProjectVdIntents(state, t, 0.05, Cfg());
        const VdIntentReason* r     = FindReason(frame, IntentKind::OVERTAKE);
        ASSERT_NE(r, nullptr) << c.phase;
        EXPECT_EQ(r->phase, c.expected) << c.phase;
        EXPECT_EQ(r->source, "slow_lead") << c.phase;
    }
}

TEST(VdIntentProjection, TurnIsAnnouncedByTheSignalScanAndExecutingOnTheConnector)
{
    VdIntentState          state;
    VirtualDriverTelemetry t   = Frame();
    t.junction_turn.dir        = 1;
    t.junction_turn.dist_to_entry_m = 28.0;

    auto frame = ProjectVdIntents(state, t, 0.05, Cfg());
    ASSERT_NE(FindReason(frame, IntentKind::TURN), nullptr);
    EXPECT_EQ(FindReason(frame, IntentKind::TURN)->phase, IntentPhase::ANNOUNCED);
    ASSERT_NE(FindIntent(frame, IntentKind::TURN), nullptr);
    EXPECT_DOUBLE_EQ(FindIntent(frame, IntentKind::TURN)->distance_m, 28.0);

    t.junction_turn.on_connector = true;
    frame                        = ProjectVdIntents(state, t, 0.05, Cfg());
    EXPECT_EQ(FindReason(frame, IntentKind::TURN)->phase, IntentPhase::EXECUTING);
}

// design section 4-3: the long-range observation is POSSIBLE, i.e. it reaches the HMI through
// intent_reasons[] but never becomes a verdict-facing row. The statutory signal is what does.
TEST(VdIntentProjection, TheObservationScanProducesAPossibleTurnThatStaysOutOfIntents)
{
    VdIntentState          state;
    VirtualDriverTelemetry t             = Frame();
    t.junction_turn_observed.dir         = -1;
    t.junction_turn_observed.dist_to_entry_m = 320.0;

    VdIntentConfig cfg      = Cfg();
    cfg.turn_lookahead_m    = 400.0;
    const auto frame        = ProjectVdIntents(state, t, 0.05, cfg);

    ASSERT_NE(FindReason(frame, IntentKind::TURN), nullptr);
    EXPECT_EQ(FindReason(frame, IntentKind::TURN)->phase, IntentPhase::POSSIBLE);
    EXPECT_EQ(FindIntent(frame, IntentKind::TURN), nullptr)
        << "a POSSIBLE turn is not externally observable and must not be verdict-facing";
}

// ══════════════════════ 9-2 items 9/10: COMPLETING vs ABORTING ══════════════════════
//
// A MATCHED PAIR, deliberately. The two inputs are identical except for aborted_reason -- which is
// the only thing that separates them anywhere in the system -- so a test of one alone would pass
// against an implementation that hard-coded the answer.

TEST(VdIntentProjection, AnUnconvergedHopIsCompletingWhenNothingWasAborted)
{
    VdIntentState          state;
    VirtualDriverTelemetry t = Frame();
    t.route_lane.valid       = true;
    t.lane_change.armed      = true;
    t.lane_change.signal_active = true;
    ProjectVdIntents(state, t, 0.05, Cfg());  // EXECUTING

    t.lane_change.armed          = false;
    t.lane_change.aborted_reason = "";    // completed
    t.lane_offset                = 1.2;   // still off centre
    t.route_lane.on_target_lane  = true;

    const VdIntentFrame   frame = ProjectVdIntents(state, t, 0.05, Cfg());
    const VdIntentReason* r     = FindReason(frame, IntentKind::LANE_CHANGE);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->phase, IntentPhase::COMPLETING);
    EXPECT_EQ(r->cancel_reason, "");
}

TEST(VdIntentProjection, TheSameInputWithAnAbortedReasonIsAborting)
{
    VdIntentState          state;
    VirtualDriverTelemetry t = Frame();
    t.route_lane.valid       = true;
    t.lane_change.armed      = true;
    t.lane_change.signal_active = true;
    ProjectVdIntents(state, t, 0.05, Cfg());  // EXECUTING

    t.lane_change.armed          = false;
    t.lane_change.aborted_reason = "manual_lateral";  // the ONLY difference
    t.lane_offset                = 1.2;
    t.route_lane.on_target_lane  = true;

    const VdIntentFrame   frame = ProjectVdIntents(state, t, 0.05, Cfg());
    const VdIntentReason* r     = FindReason(frame, IntentKind::LANE_CHANGE);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->phase, IntentPhase::ABORTING);
    EXPECT_EQ(r->cancel_reason, "manual_lateral");
}

// 9-2 item 10: ABORTING is defined by the motion not having settled, so it must END when the
// motion settles -- not persist as a latch. Both sides of the threshold, same run.
TEST(VdIntentProjection, AbortingEndsWhenTheLateralOffsetConverges)
{
    VdIntentState          state;
    VdIntentConfig         cfg = Cfg();  // abort_converged_offset_m = 0.3
    VirtualDriverTelemetry t   = Frame();
    t.route_lane.valid         = true;
    t.lane_change.armed        = true;
    t.lane_change.signal_active = true;
    ProjectVdIntents(state, t, 0.05, cfg);

    t.lane_change.armed          = false;
    t.lane_change.aborted_reason = "storyboard";
    t.route_lane.on_target_lane  = true;
    // Clear the indicator too. Leaving it latched is not a neutral simplification: the
    // projection would -- correctly -- keep reporting ANNOUNCED, because an indicator that is
    // still on IS still an announcement.
    t.lane_change.signal_active  = false;

    t.lane_offset = 0.31;  // just outside -> still settling
    EXPECT_EQ(FindReason(ProjectVdIntents(state, t, 0.05, cfg), IntentKind::LANE_CHANGE)->phase,
              IntentPhase::ABORTING);

    t.lane_offset = 0.29;  // just inside -> settled, so the intent ends
    const auto frame = ProjectVdIntents(state, t, 0.05, cfg);
    const VdIntentReason* r = FindReason(frame, IntentKind::LANE_CHANGE);
    ASSERT_NE(r, nullptr) << "the intent must be SEEN to end, not silently dropped";
    EXPECT_NE(r->phase, IntentPhase::ABORTING);
    EXPECT_EQ(r->phase, IntentPhase::COMPLETING) << "it reached EXECUTING, so it ends as COMPLETING";
}

// ══════════════════════ 9-2 items 3/4: dwell and the ABANDONED frame ══════════════════════

TEST(VdIntentProjection, AVanishedIntentIsHeldAsAbandonedRatherThanDropped)
{
    VdIntentState          state;
    VirtualDriverTelemetry t = Frame();
    t.policy.constraints     = {Stop(40.0, "crosswalk")};
    t.brake_light_on         = true;
    ProjectVdIntents(state, t, 0.05, Cfg());  // ANNOUNCED

    t.policy.constraints.clear();  // the pedestrian finished crossing
    const auto            frame = ProjectVdIntents(state, t, 0.05, Cfg());
    const VdIntentReason* r     = FindReason(frame, IntentKind::STOP);
    ASSERT_NE(r, nullptr) << "the intent disappeared between frames with no ending";
    EXPECT_EQ(r->phase, IntentPhase::ABANDONED);
    EXPECT_EQ(r->cancel_reason, "constraint_cleared")
        << "an empty cancel_reason cannot be told apart from an unfilled field";
    EXPECT_NE(FindIntent(frame, IntentKind::STOP), nullptr) << "it reached ANNOUNCED, so the ending is observable";
}

// 9-2 item 4's second half. Why the blockers are kept: "why was it given up on" IS "what was in
// the way just before it was given up on". Clearing the list at the moment of cancellation
// deletes the answer.
TEST(VdIntentProjection, TheAbandonedFrameKeepsTheBlockersAndNamesTheFirstAsTheCancelReason)
{
    VdIntentState          state;
    VirtualDriverTelemetry t = Frame();
    t.route_lane.valid       = true;
    t.lane_change.signal_active = true;
    t.lane_change.blockers   = {{IntentWhere::REAR, 12, kBlockerRearGap, kQuantityGapM, 6.2, 11.3},
                                {IntentWhere::FRONT, 11, kBlockerLeadGap, kQuantityGapM, 9.8, 16.7}};
    ProjectVdIntents(state, t, 0.05, Cfg());  // ANNOUNCED, blocked front and rear

    t.route_lane.valid          = false;  // the plan went away while it was still waiting
    t.lane_change.signal_active = false;
    t.lane_change.blockers.clear();

    const VdIntentFrame   frame = ProjectVdIntents(state, t, 0.05, Cfg());
    const VdIntentReason* r     = FindReason(frame, IntentKind::LANE_CHANGE);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->phase, IntentPhase::ABANDONED);
    ASSERT_EQ(r->blockers.size(), 2u) << "the reason it was waiting was erased at the moment it mattered";
    EXPECT_EQ(r->cancel_reason, kBlockerRearGap);
}

TEST(VdIntentProjection, TheDwellHoldsAnEndedIntentForMinDwellSeconds)
{
    VdIntentState          state;
    VdIntentConfig         cfg = Cfg();  // min_dwell_s = 1.0
    VirtualDriverTelemetry t   = Frame();
    t.policy.constraints       = {Stop(40.0, "crosswalk")};
    t.brake_light_on           = true;
    ProjectVdIntents(state, t, 0.1, cfg);

    t.policy.constraints.clear();
    int held = 0;
    for (int i = 0; i < 40; ++i)
    {
        if (!HasKind(ProjectVdIntents(state, t, 0.1, cfg), IntentKind::STOP)) break;
        ++held;
    }
    // 1.0 s at dt=0.1 -> the ending frame plus ten more.
    EXPECT_GE(held, 10);
    EXPECT_LE(held, 12) << "the dwell over-ran its configured length";
}

// The SAFETY exemption, as a pair with the case above. AEB appearing and disappearing instantly is
// correct behaviour; holding the row would read as "still braking". It still gets ONE frame, so
// the ending is not invisible -- the exemption is from the dwell, not from being seen to end.
TEST(VdIntentProjection, ASafetyTierIntentIsExemptFromTheDwellButStillShowsItsEnding)
{
    VdIntentState          state;
    VdIntentConfig         cfg = Cfg();
    VirtualDriverTelemetry t   = Frame();
    t.policy.constraints       = {Stop(15.0, "aeb", PolicyConstraint::Tier::SAFETY)};
    t.brake_light_on           = true;
    ProjectVdIntents(state, t, 0.1, cfg);

    t.policy.constraints.clear();

    const auto ending = ProjectVdIntents(state, t, 0.1, cfg);
    const VdIntentReason* r = FindReason(ending, IntentKind::STOP);
    ASSERT_NE(r, nullptr) << "even an exempt intent must be seen to end";
    EXPECT_EQ(r->phase, IntentPhase::ABANDONED);
    EXPECT_EQ(r->tier, "safety");

    EXPECT_FALSE(HasKind(ProjectVdIntents(state, t, 0.1, cfg), IntentKind::STOP))
        << "a safety-tier intent lingered past its single ending frame";
}

// The counterpart: a COMFORT intent in the same situation is held. Without this pair, an
// implementation with no dwell at all would pass the exemption test.
TEST(VdIntentProjection, AComfortTierIntentInTheSameSituationIsHeld)
{
    VdIntentState          state;
    VdIntentConfig         cfg = Cfg();
    VirtualDriverTelemetry t   = Frame();
    t.policy.constraints       = {Stop(15.0, "lead_vehicle", PolicyConstraint::Tier::COMFORT)};
    t.brake_light_on           = true;
    ProjectVdIntents(state, t, 0.1, cfg);

    t.policy.constraints.clear();
    ProjectVdIntents(state, t, 0.1, cfg);  // the ending frame
    EXPECT_TRUE(HasKind(ProjectVdIntents(state, t, 0.1, cfg), IntentKind::STOP))
        << "a comfort-tier intent was dropped as fast as a safety-tier one";
}

// An intent that never became observable ends without ever entering intents[] -- otherwise the
// verdict-facing array would carry rows for things nobody could have seen.
TEST(VdIntentProjection, AnIntentThatNeverReachedAnnouncedNeverEntersIntents)
{
    VdIntentState          state;
    VirtualDriverTelemetry t = Frame();
    t.policy.constraints     = {Stop(200.0, "traffic_light")};  // far away, no braking
    auto frame               = ProjectVdIntents(state, t, 0.05, Cfg());
    EXPECT_TRUE(frame.intents.empty());
    EXPECT_FALSE(frame.reasons.empty());

    t.policy.constraints.clear();
    frame = ProjectVdIntents(state, t, 0.05, Cfg());
    EXPECT_EQ(FindReason(frame, IntentKind::STOP)->phase, IntentPhase::ABANDONED);
    EXPECT_TRUE(frame.intents.empty()) << "an unobservable intent leaked into the verdict array";
}

// ══════════════════════════ 9-2 item 5: eta ══════════════════════════

TEST(VdEta, TheStopFormIsExactUnderConstantDeceleration)
{
    // Decelerating from 20 m/s to 0 over 100 m: mean speed 10 -> 10 s.
    EXPECT_DOUBLE_EQ(VdEtaToStop(100.0, 20.0), 10.0);
    // A stationary vehicle never arrives -- and that is not "0 seconds".
    EXPECT_DOUBLE_EQ(VdEtaToStop(100.0, 0.0), -1.0);
    EXPECT_DOUBLE_EQ(VdEtaToStop(-1.0, 20.0), -1.0);
}

// The trap section 6-1 names explicitly: dividing by one END of a segment rather than its MEAN
// always comes out short across a deceleration. Same error the reported future path already made
// once.
TEST(VdEta, TheIntegralUsesTheSegmentMeanSpeedNotAnEndpoint)
{
    VirtualDriverTelemetry t = Frame();
    // 20 m/s at s=0 falling linearly to 10 m/s at s=60. Exact time for constant deceleration over
    // that stretch is 2*60/(20+10) = 4.0 s.
    t.midlong.v_target_profile = {{0.0, 20.0}, {60.0, 10.0}, {120.0, 10.0}};

    const VdEtaMap map = BuildVdEtaMap(t);
    ASSERT_TRUE(map.valid);
    EXPECT_NEAR(VdEtaAt(map, 60.0), 4.0, 1.0e-6);
    // Dividing by the entry speed would give 60/20 = 3.0; by the exit speed, 6.0. Both wrong,
    // and the first is wrong in the DANGEROUS direction (arriving sooner than reality).
    EXPECT_GT(VdEtaAt(map, 60.0), 3.5);
}

// section 6-2. The map stops at the planned stop, and everything past it reports -1. This is the
// direct evidence that no speed floor was introduced to keep the arithmetic alive.
TEST(VdEta, TheMapIsCutOffAtThePlannedStopAndReportsMinusOneBeyondIt)
{
    VirtualDriverTelemetry t = Frame();
    t.midlong.v_target_profile = {{0.0, 12.0}, {30.0, 8.0}, {60.0, 0.0}, {90.0, 6.0}, {120.0, 10.0}};

    const VdEtaMap map = BuildVdEtaMap(t);
    ASSERT_TRUE(map.valid);
    EXPECT_NEAR(map.s_stop_cutoff, 60.0, 1.0e-9);

    EXPECT_GT(VdEtaAt(map, 30.0), 0.0) << "before the stop the answer exists";
    EXPECT_DOUBLE_EQ(VdEtaAt(map, 90.0), -1.0) << "how long the stop lasts is unknown, so this has no answer";
    EXPECT_DOUBLE_EQ(VdEtaAt(map, 120.0), -1.0);
}

TEST(VdEta, DisablingEtaLeavesEveryIntentAtMinusOne)
{
    VdIntentState          state;
    VdIntentConfig         cfg = Cfg();
    cfg.eta_enabled            = false;
    VirtualDriverTelemetry t   = Frame();
    t.policy.constraints       = {Stop(40.0, "traffic_light")};
    t.brake_light_on           = true;
    t.midlong.v_target_profile = {{0.0, 13.9}, {100.0, 13.9}};

    const auto frame = ProjectVdIntents(state, t, 0.05, cfg);
    ASSERT_FALSE(frame.intents.empty());
    for (const auto& intent : frame.intents)
    {
        EXPECT_DOUBLE_EQ(intent.eta_s, -1.0);
    }
}

// ══════════════════════════ 9-3: the negative controls ══════════════════════════
//
// Not optional. Checking only that a thing fires would pass an implementation whose fields are
// always true, which is how a field stays dead for months without anyone noticing.

TEST(VdIntentNegativeControls, DisabledProducesTwoEmptyArraysAndRetainsNothing)
{
    VdIntentState          state;
    VdIntentConfig         cfg = Cfg();
    VirtualDriverTelemetry t   = Frame();
    t.policy.constraints       = {Stop(40.0, "traffic_light")};
    t.brake_light_on           = true;
    t.junction_turn.dir        = 1;
    t.route_lane.valid         = true;
    t.lane_change.signal_active = true;

    // Build up some state first, so this also proves the disable CLEARS rather than freezes.
    ProjectVdIntents(state, t, 0.05, cfg);
    ASSERT_FALSE(state.tracks.empty());

    cfg.enabled      = false;
    const auto frame = ProjectVdIntents(state, t, 0.05, cfg);
    EXPECT_TRUE(frame.intents.empty());
    EXPECT_TRUE(frame.reasons.empty());
    EXPECT_TRUE(state.tracks.empty()) << "a disabled layer must not keep rows that could resurface";
}

TEST(VdIntentNegativeControls, DrivingStraightThroughProducesNoTurn)
{
    VdIntentState          state;
    VirtualDriverTelemetry t = Frame();
    // Everything else present; only the junction lookahead is quiet.
    t.policy.constraints = {Stop(40.0, "traffic_light")};
    t.brake_light_on     = true;

    EXPECT_FALSE(HasKind(ProjectVdIntents(state, t, 0.05, Cfg()), IntentKind::TURN));
}

// The version of the control above that ACTUALLY BITES, and the reason it is here: the first one
// only says "no turn when nowhere near a junction", which every implementation passes. Driving
// STRAIGHT ACROSS a junction is the case that separates them -- the ego really is on a connector
// (on_connector=true) and there really is no turn (dir==0).
//
// A real run caught this; the unit test above did not. An always-true field is indistinguishable
// from a correct one until something makes it say no, and "nowhere near a junction" was never
// going to be that something.
TEST(VdIntentNegativeControls, BeingOnAStraightConnectorIsNotATurn)
{
    VdIntentState          state;
    VirtualDriverTelemetry t     = Frame();
    t.junction_turn.on_connector = true;   // physically inside the intersection...
    t.junction_turn.dir          = 0;      // ...going straight across it

    EXPECT_FALSE(HasKind(ProjectVdIntents(state, t, 0.05, Cfg()), IntentKind::TURN))
        << "merely being on a junction connector was reported as a TURN";

    // Positive control, same run: give the connector a direction and the TURN must appear. Without
    // this pair the fix could be "never report TURN at all" and still look green.
    VdIntentState state2;
    t.junction_turn.dir = -1;
    const VdIntentFrame   frame = ProjectVdIntents(state2, t, 0.05, Cfg());
    const VdIntentReason* r     = FindReason(frame, IntentKind::TURN);
    ASSERT_NE(r, nullptr) << "a real turn stopped being reported";
    EXPECT_EQ(r->phase, IntentPhase::EXECUTING);
}

// Same trap on the observation-scan side: the long-range scan reports on_connector too, and a
// straight crossing seen 300 m out is no more a turn than one being driven through now.
TEST(VdIntentNegativeControls, AStraightCrossingSeenByTheObservationScanIsNotATurn)
{
    VdIntentConfig cfg   = Cfg();
    cfg.turn_lookahead_m = 400.0;

    VdIntentState          state;
    VirtualDriverTelemetry t              = Frame();
    t.junction_turn_observed.on_connector = true;
    t.junction_turn_observed.dir          = 0;
    EXPECT_FALSE(HasKind(ProjectVdIntents(state, t, 0.05, cfg), IntentKind::TURN));

    VdIntentState state2;
    t.junction_turn_observed.dir             = 1;
    t.junction_turn_observed.on_connector    = false;
    t.junction_turn_observed.dist_to_entry_m = 280.0;
    const VdIntentFrame   frame = ProjectVdIntents(state2, t, 0.05, cfg);
    const VdIntentReason* r     = FindReason(frame, IntentKind::TURN);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->phase, IntentPhase::POSSIBLE);
}

TEST(VdIntentNegativeControls, AGreenLightProducesNoStop)
{
    VdIntentState          state;
    VirtualDriverTelemetry t = Frame();  // no constraints at all -- the light is green
    t.speed                  = 13.9;

    const auto frame = ProjectVdIntents(state, t, 0.05, Cfg());
    EXPECT_FALSE(HasKind(frame, IntentKind::STOP));
    EXPECT_TRUE(frame.intents.empty());
}

// design section 3-1-1: while an overtake is live it owns the lane changes it is made of, so the
// same lateral movement is not drawn twice.
TEST(VdIntentNegativeControls, AnActiveOvertakeSuppressesTheLaneChangeRow)
{
    VdIntentState          state;
    VirtualDriverTelemetry t = Frame();
    t.overtake.phase         = "moving_out";
    t.overtake.considered    = true;
    t.overtake.lead_osi_id   = 11;
    // Everything a LANE_CHANGE row would need is present...
    t.route_lane.valid          = true;
    t.route_lane.on_target_lane = false;
    t.lane_change.armed         = true;
    t.lane_change.signal_active = true;

    const auto frame = ProjectVdIntents(state, t, 0.05, Cfg());
    EXPECT_TRUE(HasKind(frame, IntentKind::OVERTAKE));
    EXPECT_FALSE(HasKind(frame, IntentKind::LANE_CHANGE))
        << "one lateral movement was reported as two intents";
}

// design section 9-3. With the scan off, junction_turn_observed is all-defaults -- and the
// projection must read that as "the scan was off", not "there is a turn at distance -1".
TEST(VdIntentNegativeControls, WithTheScanOffThereIsNoPossibleTurnButAnnouncedStillWorks)
{
    VdIntentState          state;
    VdIntentConfig         cfg = Cfg();
    cfg.turn_lookahead_m       = 0.0;  // the default

    VirtualDriverTelemetry t = Frame();
    t.junction_turn_observed.dir = 1;  // stale/garbage: must be ignored while the scan is off
    EXPECT_FALSE(HasKind(ProjectVdIntents(state, t, 0.05, cfg), IntentKind::TURN));

    // ...and the statutory signal path is untouched by any of this.
    VdIntentState state2;
    t.junction_turn.dir = 1;
    t.junction_turn.dist_to_entry_m = 28.0;
    const VdIntentFrame   frame2 = ProjectVdIntents(state2, t, 0.05, cfg);
    const VdIntentReason* r      = FindReason(frame2, IntentKind::TURN);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->phase, IntentPhase::ANNOUNCED);
}

TEST(VdIntentNegativeControls, AnEmptyAdjacentLaneLeavesTheBlockerListEmpty)
{
    VdIntentState          state;
    VirtualDriverTelemetry t = Frame();
    t.route_lane.valid       = true;
    t.route_lane.on_target_lane  = false;
    t.lane_change.gap_accepted   = true;
    t.lane_change.blockers.clear();

    const VdIntentFrame   frame = ProjectVdIntents(state, t, 0.05, Cfg());
    const VdIntentReason* r     = FindReason(frame, IntentKind::LANE_CHANGE);
    ASSERT_NE(r, nullptr);
    EXPECT_TRUE(r->blockers.empty());
}

// design section 8-6. A turn cannot be "blocked" -- waiting to turn is a STOP with its own motive,
// and attaching that motive to the TURN as well would book the same fact twice.
//
// "always empty" is indistinguishable from a dead field unless something is shown to be there
// INSTEAD, so this also asserts the STOP row carrying the real reason is present in the same frame.
TEST(VdIntentNegativeControls, ATurnHasNoBlockersAndTheWaitIsAStopRowInstead)
{
    VdIntentState          state;
    VirtualDriverTelemetry t = Frame();
    t.junction_turn.dir          = 1;
    t.junction_turn.on_connector = true;
    t.policy.constraints         = {Stop(6.0, "conflict_point")};
    t.policy.detail              = {{"gt.conflict_point.other_osi_id", "23"}};
    t.brake_light_on             = true;
    t.midlong.binding_constraint_index = 0;

    const auto            frame = ProjectVdIntents(state, t, 0.05, Cfg());
    const VdIntentReason* turn  = FindReason(frame, IntentKind::TURN);
    const VdIntentReason* stop  = FindReason(frame, IntentKind::STOP);

    ASSERT_NE(turn, nullptr);
    EXPECT_TRUE(turn->blockers.empty());

    ASSERT_NE(stop, nullptr) << "the TURN blockers are empty and NOTHING explains the wait -- "
                                "that is a dead field, not a design decision";
    EXPECT_EQ(stop->source, "conflict_point");
    EXPECT_EQ(stop->phase, IntentPhase::EXECUTING);
    const VdIntent* stop_out = FindIntent(frame, IntentKind::STOP);
    ASSERT_NE(stop_out, nullptr);
    EXPECT_EQ(stop_out->subject_osi_id, 23);
}

// section 9-3's last row: through a run with no interruption, aborted_reason stays empty, so
// ABORTING never appears. Asserted over a completed lane change from arm to settled.
TEST(VdIntentNegativeControls, ACompletedLaneChangeNeverProducesAborting)
{
    VdIntentState          state;
    VirtualDriverTelemetry t = Frame();
    t.route_lane.valid       = true;
    t.route_lane.on_target_lane = false;
    t.lane_change.signal_active = true;
    t.lane_change.dist_to_connection = 50.0;
    t.lane_change.required_m         = 100.0;

    bool saw_aborting  = false;
    bool saw_executing = false;
    bool saw_completing = false;

    ProjectVdIntents(state, t, 0.05, Cfg());  // ANNOUNCED
    t.lane_change.armed = true;
    for (int i = 0; i < 5; ++i)
    {
        const auto* r = FindReason(ProjectVdIntents(state, t, 0.05, Cfg()), IntentKind::LANE_CHANGE);
        if (r && r->phase == IntentPhase::EXECUTING) saw_executing = true;
        if (r && r->phase == IntentPhase::ABORTING) saw_aborting = true;
    }

    t.lane_change.armed         = false;  // hop complete, aborted_reason stays ""
    t.route_lane.on_target_lane = true;
    t.lane_change.signal_active = false;  // the indicator goes out with the hop
    for (double offset : {1.5, 1.0, 0.5, 0.2, 0.0})
    {
        t.lane_offset = offset;
        const auto* r = FindReason(ProjectVdIntents(state, t, 0.05, Cfg()), IntentKind::LANE_CHANGE);
        if (r && r->phase == IntentPhase::ABORTING) saw_aborting = true;
        if (r && r->phase == IntentPhase::COMPLETING) saw_completing = true;
    }

    EXPECT_TRUE(saw_executing);
    EXPECT_TRUE(saw_completing);
    EXPECT_FALSE(saw_aborting) << "a lane change that was never aborted reported ABORTING";
}

// ══════════════════ the verdict boundary itself (section 4) ══════════════════

// Everything in intents[] must be at a phase section 3-2 marks observable. If a POSSIBLE or
// PLANNED row could reach it, the array would stop being a statement about what an outside
// observer could have seen -- which is the entire basis for matchers trusting it.
TEST(VdIntentVerdictBoundary, IntentsNeverCarryAnUnobservablePhase)
{
    VdIntentState  state;
    VdIntentConfig cfg = Cfg();
    cfg.turn_lookahead_m = 400.0;

    // A busy frame: everything live at once, at assorted phases.
    VirtualDriverTelemetry t = Frame();
    t.policy.constraints     = {Stop(200.0, "traffic_light"), Cap(2.0, 30.0, "yield_sign"),
                                Stop(20.0, "lead_vehicle")};
    t.midlong.constraints    = {{40.0, 1.0, 2.0, 8.0, "curve"}, {90.0, 3.0, 4.0, 9.0, "speed_limit"}};
    t.midlong.binding_constraint_index = 2;
    t.brake_light_on         = true;
    t.route_lane.valid       = true;
    t.route_lane.on_target_lane = false;
    t.lane_change.dist_to_connection = 400.0;
    t.junction_turn_observed.dir     = 1;
    t.overtake.considered            = true;

    const auto frame = ProjectVdIntents(state, t, 0.05, cfg);
    ASSERT_FALSE(frame.intents.empty());
    ASSERT_GT(frame.reasons.size(), frame.intents.size())
        << "nothing was held back -- the two arrays would be redundant";

    for (const auto& intent : frame.intents)
    {
        EXPECT_NE(intent.phase, IntentPhase::POSSIBLE) << IntentKindName(intent.kind);
        EXPECT_NE(intent.phase, IntentPhase::PLANNED) << IntentKindName(intent.kind);
    }
}

// Every id in intents[] must exist in intent_reasons[]: the two arrays are one dataset seen from
// two sides, and a consumer joins them on the id.
TEST(VdIntentVerdictBoundary, EveryIntentHasAMatchingReason)
{
    VdIntentState          state;
    VirtualDriverTelemetry t = Frame();
    t.policy.constraints     = {Stop(20.0, "lead_vehicle"), Stop(60.0, "traffic_light")};
    t.brake_light_on         = true;
    t.junction_turn.dir      = -1;
    t.route_lane.valid       = true;
    t.route_lane.on_target_lane = false;
    t.lane_change.signal_active = true;

    const auto frame = ProjectVdIntents(state, t, 0.05, Cfg());
    ASSERT_FALSE(frame.intents.empty());

    for (const auto& intent : frame.intents)
    {
        const auto it = std::find_if(frame.reasons.begin(), frame.reasons.end(),
                                     [&intent](const VdIntentReason& r) { return r.id == intent.id; });
        ASSERT_NE(it, frame.reasons.end()) << "intents[] row " << intent.id << " has no reason row";
        EXPECT_EQ(it->kind, intent.kind);
        EXPECT_EQ(it->phase, intent.phase);
    }
}

// At most one intent owns each domain per frame (section 5-3). Longitudinal and lateral can be
// owned by DIFFERENT intents at the same instant -- stopped at a red light while signalling a
// lane change is a normal state -- which is why they are two flags and not one.
TEST(VdIntentVerdictBoundary, AtMostOneIntentBindsEachDomain)
{
    VdIntentState          state;
    VirtualDriverTelemetry t = Frame();
    t.policy.constraints     = {Stop(20.0, "lead_vehicle"), Stop(60.0, "traffic_light")};
    t.midlong.binding_constraint_index = 0;
    t.brake_light_on         = true;
    t.route_lane.valid       = true;
    t.route_lane.on_target_lane = false;
    t.lane_change.armed      = true;
    t.junction_turn.on_connector = true;

    const auto frame = ProjectVdIntents(state, t, 0.05, Cfg());

    int lon = 0;
    int lat = 0;
    for (const auto& r : frame.reasons)
    {
        if (r.binding_lon) ++lon;
        if (r.binding_lat) ++lat;
    }
    EXPECT_LE(lon, 1);
    EXPECT_LE(lat, 1);
    EXPECT_EQ(lon, 1) << "something is setting the speed and nothing claimed it";
    EXPECT_EQ(lat, 1) << "an armed hop is moving the car sideways and nothing claimed it";
}
