#include <gtest/gtest.h>

#include "gt_esmini/control/virtualdriver/policies/LeadVehicleAware.hpp"
#include "gt_esmini/control/virtualdriver/policies/TrafficLightAware.hpp"
#include "gt_esmini/control/virtualdriver/policies/StopYieldSignAware.hpp"
#include "gt_esmini/control/virtualdriver/policies/ConflictPointResolver.hpp"
#include "gt_esmini/control/virtualdriver/policies/RouteCrosswalkScan.hpp"
#include "gt_esmini/control/virtualdriver/policies/CrosswalkPedestrianAware.hpp"
#include "gt_esmini/control/virtualdriver/policies/JunctionStopGuard.hpp"

#include "RoadManager.hpp"  // roadmanager::LampIcon — the head-type tests speak in real icon names

#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <vector>

using namespace gt_esmini;

// ─────────────────────────── Phase 3a: IDM math ───────────────────────────

TEST(LeadIdm, DesiredGapGrowsWithSpeed)
{
    lead_idm::Params p;
    EXPECT_NEAR(lead_idm::DesiredGap(p, 0.0, 0.0), p.min_gap, 1e-6);  // standstill -> s0
    EXPECT_GT(lead_idm::DesiredGap(p, 15.0, 15.0), p.min_gap);        // moving -> larger
}

TEST(LeadIdm, FreeFlowAcceleratesWhenLeadFarAway)
{
    lead_idm::Params p;
    // Huge gap, ego well below v0 -> close to max comfortable acceleration.
    const double a = lead_idm::DesiredAccel(p, 10.0, 10.0, 1.0e6);
    EXPECT_GT(a, 0.0);
    EXPECT_NEAR(a, p.max_accel, 0.1);
}

TEST(LeadIdm, BrakesHardForStoppedLeadInsideGap)
{
    lead_idm::Params p;
    // Stopped lead 1 m ahead while doing 12 m/s -> strong deceleration.
    const double a = lead_idm::DesiredAccel(p, 12.0, 0.0, 1.0);
    EXPECT_LT(a, -p.comfort_decel);
}

// ───────────────────────── Phase 3b: light decision ───────────────────────

TEST(TrafficLightDecision, RedAlwaysStops)
{
    TrafficLightParams p;
    EXPECT_TRUE(TrafficLightShouldStop(TrafficLightPhase::RED, 5.0, 13.0, p));
    EXPECT_TRUE(TrafficLightShouldStop(TrafficLightPhase::RED, 50.0, 1.0, p));
}

TEST(TrafficLightDecision, GreenAndUnknownNeverStop)
{
    TrafficLightParams p;
    EXPECT_FALSE(TrafficLightShouldStop(TrafficLightPhase::GREEN, 5.0, 13.0, p));
    EXPECT_FALSE(TrafficLightShouldStop(TrafficLightPhase::UNKNOWN, 5.0, 13.0, p));
}

TEST(TrafficLightDecision, YellowStopsWhenRoomProceedsWhenClose)
{
    TrafficLightParams p;  // yellow_decel 4.0
    // braking distance at 10 m/s = 100 / (2*4) = 12.5 m.
    EXPECT_TRUE(TrafficLightShouldStop(TrafficLightPhase::YELLOW, 40.0, 10.0, p));   // far -> stop
    EXPECT_FALSE(TrafficLightShouldStop(TrafficLightPhase::YELLOW, 10.0, 10.0, p));  // close -> go
}

// ──────────────── Phase 3b: which heads govern the ego ────────────────────
// The route scan cannot separate a pedestrian head from a vehicle head: both are
// dynamic signals (so both promote to TrafficLight) and a pedestrian head may
// declare the ego's own driving lane in its <validity> — fabriksgatan_traffic_lights
// road 3 does exactly that for its two type-1000002 heads, which is how a red
// DONT_WALK lamp used to halt the ego at a green light. The lamp icons decide.

namespace
{
// Icon sets exactly as RoadManager's traffic_light_type_map bakes them per type.
std::vector<int> Icons(std::initializer_list<roadmanager::LampIcon> icons)
{
    std::vector<int> out;
    for (roadmanager::LampIcon i : icons) out.push_back(static_cast<int>(i));
    return out;
}
}  // namespace

TEST(TrafficLightHead, PedestrianHeadDoesNotGovern)
{
    // type 1000002 = 2 lamps {DONT_WALK, WALK}; 1000002.30 = 3 lamps.
    EXPECT_FALSE(IsVehicleTrafficLightHead(Icons({roadmanager::ICON_DONT_WALK, roadmanager::ICON_WALK})));
    EXPECT_FALSE(IsVehicleTrafficLightHead(
        Icons({roadmanager::ICON_DONT_WALK, roadmanager::ICON_DONT_WALK, roadmanager::ICON_WALK})));
    EXPECT_FALSE(IsVehicleTrafficLightHead(Icons({roadmanager::ICON_PEDESTRIAN})));
}

TEST(TrafficLightHead, VehicleHeadGoverns)
{
    // type 1000001 = the plain 3-lamp vehicle head; its icons are ICON_NONE.
    EXPECT_TRUE(IsVehicleTrafficLightHead(
        Icons({roadmanager::ICON_NONE, roadmanager::ICON_NONE, roadmanager::ICON_NONE})));
}

TEST(TrafficLightHead, ArrowHeadGoverns)
{
    // type 1000011.10 = 3-lamp left-arrow head. Direction matching is a separate
    // concern; an arrow head is unambiguously addressed to vehicles.
    EXPECT_TRUE(IsVehicleTrafficLightHead(
        Icons({roadmanager::ICON_ARROW_LEFT, roadmanager::ICON_ARROW_LEFT, roadmanager::ICON_ARROW_LEFT})));
}

TEST(TrafficLightHead, BicycleAndTramHeadsDoNotGovern)
{
    EXPECT_FALSE(IsVehicleTrafficLightHead(Icons({roadmanager::ICON_BICYCLE, roadmanager::ICON_BICYCLE})));
    EXPECT_FALSE(IsVehicleTrafficLightHead(Icons({roadmanager::ICON_TRAM})));
    EXPECT_FALSE(IsVehicleTrafficLightHead(Icons({roadmanager::ICON_PEDESTRIAN_AND_BICYCLE})));
    EXPECT_FALSE(IsVehicleTrafficLightHead(Icons({roadmanager::ICON_BUS_AND_TRAM})));
}

TEST(TrafficLightHead, MixedIconsGovern)
{
    // One vehicle-facing lamp is enough: a combined head still speaks to the ego.
    EXPECT_TRUE(IsVehicleTrafficLightHead(Icons({roadmanager::ICON_NONE, roadmanager::ICON_DONT_WALK})));
}

TEST(TrafficLightHead, EmptyLampListDoesNotGovern)
{
    // Unknown type/subtype combos leave nr_lamps_ == 0, so no phase can be read.
    // Such a head must not mask a real vehicle head standing behind it.
    EXPECT_FALSE(IsVehicleTrafficLightHead({}));
}

TEST(TrafficLightHead, UnclassifiedIconsStayVehicleFacing)
{
    // Fallback direction: anything not explicitly a non-vehicle icon keeps the
    // pre-filter behaviour (govern), so an unmapped icon cannot silently make the
    // ego ignore a real light.
    EXPECT_TRUE(IsVehicleLampIcon(roadmanager::ICON_UNKNOWN));
    EXPECT_TRUE(IsVehicleLampIcon(roadmanager::ICON_OTHER));
    EXPECT_TRUE(IsVehicleLampIcon(roadmanager::ICON_COUNTDOWN_SECONDS));
}

// ───────────── Phase 3b: don't block the box (junction stop guard) ─────────
//
// Reference geometry (the measured defect): ego on the approach road, a junction
// connecting road 17.7 m long, and a red head 3 m past its exit. Wanting to halt
// `stop_margin` short of that head puts the stop target INSIDE the junction, and
// the pre-guard policy emitted it there — the ego halted mid-intersection and
// never moved again.

namespace
{
JunctionStopGuardParams GuardParams()
{
    JunctionStopGuardParams p;  // stop_margin 3.0, exit_clearance 5.0, decel 2.0
    return p;
}

// A junction ahead of the ego: [entry, entry+length].
RouteJunctionSpan SpanAhead(double entry, double length, std::uint32_t id = 146)
{
    return {entry, entry + length, id, false};
}
}  // namespace

TEST(JunctionStopGuard, StopLineBeforeAJunctionIsUntouched)
{
    // The ordinary signalised approach: head at the road end, so the target lands
    // AT or BEFORE the junction entry. Must resolve HOLD, bit-identical target —
    // this is what every existing traffic-light gate exercises.
    const std::vector<RouteJunctionSpan> spans = {SpanAhead(24.0, 17.0)};

    const auto res = ResolveJunctionSafeStop(21.0, spans, 8.0, GuardParams());
    EXPECT_EQ(res.action, JunctionStopAction::HOLD);
    EXPECT_FALSE(res.blocked);
    EXPECT_NEAR(res.s_stop, 21.0, 1e-9);
}

TEST(JunctionStopGuard, NoJunctionsAtAllIsAlwaysHold)
{
    const auto res = ResolveJunctionSafeStop(12.0, {}, 10.0, GuardParams());
    EXPECT_EQ(res.action, JunctionStopAction::HOLD);
    EXPECT_NEAR(res.s_stop, 12.0, 1e-9);
}

TEST(JunctionStopGuard, TargetPastTheJunctionExitClearsIt)
{
    // Long block after the junction: the ego can stand well clear, so nothing to do.
    const std::vector<RouteJunctionSpan> spans = {SpanAhead(20.0, 17.0)};  // exit 37
    const auto res = ResolveJunctionSafeStop(50.0, spans, 10.0, GuardParams());
    EXPECT_EQ(res.action, JunctionStopAction::HOLD);
    EXPECT_FALSE(res.blocked);
}

TEST(JunctionStopGuard, TargetInsideTheJunctionPullsBackWhenReachable)
{
    // The measured geometry, seen from 60 m out at 10 m/s: braking distance
    // 100/(2*2) = 25 m, room to the pull-back point 60 - 3 = 57 m. Stop before it.
    const std::vector<RouteJunctionSpan> spans = {SpanAhead(60.0, 17.7)};
    const auto res = ResolveJunctionSafeStop(74.7, spans, 10.0, GuardParams());
    EXPECT_EQ(res.action, JunctionStopAction::PULL_BACK);
    EXPECT_TRUE(res.blocked);
    EXPECT_EQ(res.junction_id, 146u);
    EXPECT_NEAR(res.s_stop, 57.0, 1e-9);
}

TEST(JunctionStopGuard, TargetJustPastTheExitStillCountsAsBlocking)
{
    // 2 m past the exit is less than the 5 m stand-clear distance: the tail would
    // still be in the box.
    const std::vector<RouteJunctionSpan> spans = {SpanAhead(40.0, 15.0)};  // exit 55
    const auto res = ResolveJunctionSafeStop(57.0, spans, 8.0, GuardParams());
    EXPECT_EQ(res.action, JunctionStopAction::PULL_BACK);
    EXPECT_NEAR(res.s_stop, 37.0, 1e-9);
}

TEST(JunctionStopGuard, TooLateToStopShortSuppressesInsteadOfBraking)
{
    // 6 m from the entry at 10 m/s: braking distance 25 m >> the 3 m left. Braking
    // now would strand the ego in the box, so the constraint is dropped and the
    // ego clears the intersection.
    const std::vector<RouteJunctionSpan> spans = {SpanAhead(6.0, 17.7)};
    const auto res = ResolveJunctionSafeStop(20.7, spans, 10.0, GuardParams());
    EXPECT_EQ(res.action, JunctionStopAction::SUPPRESS);
    EXPECT_TRUE(res.blocked);
    EXPECT_EQ(res.junction_id, 146u);
}

TEST(JunctionStopGuard, AlreadyInsideTheJunctionAlwaysSuppresses)
{
    // THE reported defect. ego_inside, so there is no "short of it" left — not
    // even standing still (v=0 makes the braking distance 0, which would otherwise
    // read as "we can stop here" and re-park the ego in the box forever).
    const std::vector<RouteJunctionSpan> spans = {{0.0, 17.5, 146u, true}};

    for (double v : {10.0, 4.5, 0.0})
    {
        const auto res = ResolveJunctionSafeStop(14.7, spans, v, GuardParams());
        EXPECT_EQ(res.action, JunctionStopAction::SUPPRESS) << "v=" << v;
        EXPECT_TRUE(res.blocked) << "v=" << v;
    }
}

TEST(JunctionStopGuard, InsideAJunctionButStoppingWellBeyondItIsAllowed)
{
    // Clearing junction A and then stopping for a light far down the next block is
    // ordinary driving: the guard must not swallow that constraint too.
    const std::vector<RouteJunctionSpan> spans = {{0.0, 17.5, 146u, true}};
    const auto res = ResolveJunctionSafeStop(40.0, spans, 8.0, GuardParams());
    EXPECT_EQ(res.action, JunctionStopAction::HOLD);
    EXPECT_FALSE(res.blocked);
    EXPECT_NEAR(res.s_stop, 40.0, 1e-9);
}

TEST(JunctionStopGuard, CommittedHoldSurvivesTheShrinkingBrakingDistance)
{
    // Approaching the pull-back point at walking pace: 1.5 m left, braking
    // distance 4/(2*2) = 1 m -> still feasible, PULL_BACK either way.
    const std::vector<RouteJunctionSpan> spans = {SpanAhead(4.5, 17.7)};
    EXPECT_EQ(ResolveJunctionSafeStop(19.2, spans, 2.0, GuardParams()).action, JunctionStopAction::PULL_BACK);

    // Same geometry, but the ego has not slowed enough (5 m/s -> 6.25 m needed).
    // Uncommitted that reads as "too late" and releases into the box...
    EXPECT_EQ(ResolveJunctionSafeStop(19.2, spans, 5.0, GuardParams()).action, JunctionStopAction::SUPPRESS);
    // ...but once the caller has committed to holding here, the target stands.
    const auto committed = ResolveJunctionSafeStop(19.2, spans, 5.0, GuardParams(), true);
    EXPECT_EQ(committed.action, JunctionStopAction::PULL_BACK);
    EXPECT_NEAR(committed.s_stop, 1.5, 1e-9);
}

TEST(JunctionStopGuard, PullBackClampsAtTheEgoAndNeverGoesNegative)
{
    // Entry nearer than stop_margin: the target clamps to 0 (halt here) rather
    // than becoming a negative, behind-us distance.
    const std::vector<RouteJunctionSpan> spans = {SpanAhead(1.0, 17.7)};
    const auto res = ResolveJunctionSafeStop(15.7, spans, 0.0, GuardParams());
    EXPECT_EQ(res.action, JunctionStopAction::PULL_BACK);
    EXPECT_NEAR(res.s_stop, 0.0, 1e-9);
}

TEST(JunctionStopGuard, UnknownExitReadsAsUnclearable)
{
    // A junction still open at the scan horizon carries exit = +inf. Anything past
    // its entry blocks: an unknown exit must not be optimistically treated as near.
    const std::vector<RouteJunctionSpan> spans = {
        {30.0, std::numeric_limits<double>::infinity(), 7u, false}};
    const auto res = ResolveJunctionSafeStop(45.0, spans, 5.0, GuardParams());
    EXPECT_EQ(res.action, JunctionStopAction::PULL_BACK);
    EXPECT_NEAR(res.s_stop, 27.0, 1e-9);
}

TEST(JunctionStopGuard, ChainedJunctionsTighterThanTheClearanceSuppress)
{
    // Two junctions 4 m apart (< the 5 m stand-clear distance): pulling back out of
    // the second lands inside the first. There is no safe target, so emit none.
    const std::vector<RouteJunctionSpan> spans = {SpanAhead(20.0, 12.0, 1u),   // 20..32
                                                  SpanAhead(36.0, 12.0, 2u)};  // 36..48
    const auto res = ResolveJunctionSafeStop(50.0, spans, 5.0, GuardParams());
    EXPECT_EQ(res.action, JunctionStopAction::SUPPRESS);
    EXPECT_EQ(res.junction_id, 2u);
}

TEST(JunctionStopGuard, TheNEARESTBlockingJunctionDecides)
{
    // A far target crossing two junctions must pull back before the FIRST one, not
    // the last: entering junction 1 already commits the ego to a box it cannot clear.
    const std::vector<RouteJunctionSpan> spans = {SpanAhead(30.0, 12.0, 1u),   // 30..42
                                                  SpanAhead(48.0, 12.0, 2u)};  // 48..60
    const auto res = ResolveJunctionSafeStop(44.0, spans, 6.0, GuardParams());
    EXPECT_EQ(res.action, JunctionStopAction::PULL_BACK);
    EXPECT_EQ(res.junction_id, 1u);
    EXPECT_NEAR(res.s_stop, 27.0, 1e-9);
}

TEST(JunctionStopGuard, ZeroClearanceStillGuardsTheJunctionItself)
{
    // exit_clearance 0 = "standing on the exit line counts as clear". The inside of
    // the junction is still off limits.
    JunctionStopGuardParams p = GuardParams();
    p.exit_clearance          = 0.0;
    const std::vector<RouteJunctionSpan> spans = {SpanAhead(40.0, 15.0)};  // 40..55

    EXPECT_EQ(ResolveJunctionSafeStop(50.0, spans, 5.0, p).action, JunctionStopAction::PULL_BACK);
    EXPECT_EQ(ResolveJunctionSafeStop(56.0, spans, 5.0, p).action, JunctionStopAction::HOLD);
}

// ─────────────────────── Phase 3c: STOP-sign FSM ──────────────────────────

TEST(StopFsm, ApproachHoldCreepClearSequence)
{
    stop_fsm::Params p;  // hold 1.5s, detect 0.3, tol 2.0, creep 2.0 m/s, advance 4.0 m -> creep 2.0s
    stop_fsm::State  st;

    // APPROACH while still moving: emit STOP, stay APPROACH.
    auto c = stop_fsm::Update(st, 10.0, 5.0, 0.0, p);
    EXPECT_EQ(c.kind, PolicyConstraint::Kind::STOP_AT_S);
    EXPECT_EQ(st.phase, stop_fsm::Phase::APPROACH);

    // Stopped at the line -> transition to HOLD.
    c = stop_fsm::Update(st, 1.0, 0.1, 1.0, p);
    EXPECT_EQ(c.kind, PolicyConstraint::Kind::STOP_AT_S);
    EXPECT_EQ(st.phase, stop_fsm::Phase::HOLD);

    // Still within the dwell.
    c = stop_fsm::Update(st, 0.5, 0.0, 2.0, p);
    EXPECT_EQ(c.kind, PolicyConstraint::Kind::STOP_AT_S);
    EXPECT_EQ(st.phase, stop_fsm::Phase::HOLD);

    // Dwell elapsed (>=1.5s) -> CREEP, emit a creep speed cap.
    c = stop_fsm::Update(st, 0.5, 0.0, 3.0, p);
    EXPECT_EQ(c.kind, PolicyConstraint::Kind::MAX_SPEED_TO_S);
    EXPECT_EQ(st.phase, stop_fsm::Phase::CREEP);
    EXPECT_NEAR(c.value, p.creep_speed, 1e-6);

    // Creeping.
    c = stop_fsm::Update(st, 0.3, 1.0, 4.0, p);
    EXPECT_EQ(c.kind, PolicyConstraint::Kind::MAX_SPEED_TO_S);

    // Creep time elapsed (>=2.0s) -> CLEARED, emit nothing.
    c = stop_fsm::Update(st, 0.1, 1.5, 5.5, p);
    EXPECT_EQ(c.kind, PolicyConstraint::Kind::NONE);
    EXPECT_EQ(st.phase, stop_fsm::Phase::CLEARED);

    // Stays cleared.
    c = stop_fsm::Update(st, 0.0, 2.0, 6.0, p);
    EXPECT_EQ(c.kind, PolicyConstraint::Kind::NONE);
}

// ───────────── P4 L2: semantics priority fallback classifier ───────────────
// ClassifyPriorityTypes is the pure classifier that StopYieldSignAware consults
// ONLY when the country catalog left a sign OSI-unclassified (catalog-first,
// decision 1). It maps the verbatim 1.9 <priority @type> strings to a behavioural
// class: stop/stopLine -> STOP, yield -> GIVE_WAY, everything else -> NONE
// (decision 2). The catalog-first gate itself lives in Evaluate() (only reached
// when GetOSIType() is neither TYPE_STOP nor TYPE_GIVE_WAY) and is exercised
// behaviourally by the phase3 batch (semantic_stop_sign_full_stop = green;
// catalog stop_sign_full_stop unchanged).

TEST(SemanticPriorityFallback, StopLineAndStopMapToStop)
{
    EXPECT_EQ(ClassifyPriorityTypes({"stopLine"}), PriorityClass::STOP);
    EXPECT_EQ(ClassifyPriorityTypes({"stop"}), PriorityClass::STOP);
}

TEST(SemanticPriorityFallback, YieldMapsToGiveWay)
{
    EXPECT_EQ(ClassifyPriorityTypes({"yield"}), PriorityClass::GIVE_WAY);
}

TEST(SemanticPriorityFallback, NonBehaviouralTypesAreNone)
{
    // trafficLight pairs with the P3 dynamic gate; the rest are informational in P4.
    EXPECT_EQ(ClassifyPriorityTypes({"trafficLight"}), PriorityClass::NONE);
    EXPECT_EQ(ClassifyPriorityTypes({"priorityRoad"}), PriorityClass::NONE);
    EXPECT_EQ(ClassifyPriorityTypes({"4way"}), PriorityClass::NONE);
    EXPECT_EQ(ClassifyPriorityTypes({"priorityToTheRightRule"}), PriorityClass::NONE);
}

TEST(SemanticPriorityFallback, EmptyPriorityListIsNone)
{
    // No <semantics> / no <priority> -> NONE, i.e. the raw catalog OSI type is kept
    // and the code path stays bit-identical to the pre-P4 behaviour.
    EXPECT_EQ(ClassifyPriorityTypes({}), PriorityClass::NONE);
}

TEST(SemanticPriorityFallback, FirstBehaviouralTypeInDocumentOrderWins)
{
    // Non-behavioural entries are skipped until the first behavioural one; earlier
    // document position decides between STOP and GIVE_WAY (assets do not mix them).
    EXPECT_EQ(ClassifyPriorityTypes({"trafficLight", "stopLine"}), PriorityClass::STOP);
    EXPECT_EQ(ClassifyPriorityTypes({"priorityRoad", "yield", "stop"}), PriorityClass::GIVE_WAY);
    EXPECT_EQ(ClassifyPriorityTypes({"stop", "yield"}), PriorityClass::STOP);
}

TEST(SemanticPriorityFallback, UnknownStringIsNone)
{
    // Defensive: a garbage / future @type value must not accidentally classify.
    EXPECT_EQ(ClassifyPriorityTypes({"notARealType"}), PriorityClass::NONE);
}

// ───────────────── Phase 3d: conflict-corridor geometry ────────────────────
// The resolver now models each vehicle as a width-inflated path CORRIDOR (strip
// of convex quads) and finds the TRUE polygon intersection of two corridors via
// Sutherland–Hodgman convex clipping + shoelace area. These pure helpers are the
// load-bearing geometry; the timing/latch is exercised behaviourally (gt_sim_test
// phase3d batch + scratch trace).

using conflict_geom::Pt;

TEST(ConflictGeom, PolygonAreaUnitSquare)
{
    std::vector<Pt> sq = {{0, 0}, {2, 0}, {2, 2}, {0, 2}};
    EXPECT_NEAR(conflict_geom::PolygonArea(sq), 4.0, 1e-9);
    // Winding-agnostic: reversed (CW) winding gives the same absolute area.
    std::vector<Pt> sq_cw = {{0, 0}, {0, 2}, {2, 2}, {2, 0}};
    EXPECT_NEAR(conflict_geom::PolygonArea(sq_cw), 4.0, 1e-9);
    // Degenerate (< 3 verts) -> 0.
    std::vector<Pt> two = {{0, 0}, {1, 1}};
    EXPECT_NEAR(conflict_geom::PolygonArea(two), 0.0, 1e-12);
}

TEST(ConflictGeom, PolygonAreaTriangle)
{
    std::vector<Pt> tri = {{0, 0}, {4, 0}, {0, 3}};
    EXPECT_NEAR(conflict_geom::PolygonArea(tri), 6.0, 1e-9);  // 0.5 * 4 * 3
}

TEST(ConflictClip, OverlappingQuadsGiveExpectedArea)
{
    // subject = [0,2]x[0,2] (area 4); clip = [1,3]x[1,3]. Intersection = [1,2]x[1,2]
    // -> a unit square of area 1.
    std::vector<Pt> subject = {{0, 0}, {2, 0}, {2, 2}, {0, 2}};
    std::vector<Pt> clip    = {{1, 1}, {3, 1}, {3, 3}, {1, 3}};
    auto out = conflict_geom::ConvexClip(subject, clip);
    EXPECT_GE(out.size(), 3u);
    EXPECT_NEAR(conflict_geom::PolygonArea(out), 1.0, 1e-9);
}

TEST(ConflictClip, DisjointQuadsGiveEmpty)
{
    std::vector<Pt> subject = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    std::vector<Pt> clip    = {{5, 5}, {6, 5}, {6, 6}, {5, 6}};
    auto out = conflict_geom::ConvexClip(subject, clip);
    EXPECT_NEAR(conflict_geom::PolygonArea(out), 0.0, 1e-9);
}

TEST(ConflictClip, SubjectFullyInsideClipReturnsSubject)
{
    // subject is wholly inside clip -> the clipped polygon is the subject itself.
    std::vector<Pt> subject = {{1, 1}, {2, 1}, {2, 2}, {1, 2}};
    std::vector<Pt> clip    = {{0, 0}, {4, 0}, {4, 4}, {0, 4}};
    auto out = conflict_geom::ConvexClip(subject, clip);
    EXPECT_NEAR(conflict_geom::PolygonArea(out), 1.0, 1e-9);
}

TEST(ConflictClip, RotatedCrossingCorridorQuads)
{
    // A horizontal corridor quad crossed by a diagonal one (mimics the ego turn
    // sweeping across an oncoming lane). subject is the unit-height horizontal
    // band [-2,2]x[-1,1]; clip is a 45° band. The clipped intersection is a
    // non-empty convex polygon with positive area, and it is contained in the
    // subject (area <= subject area = 8).
    std::vector<Pt> subject = {{-2, -1}, {2, -1}, {2, 1}, {-2, 1}};
    // Clip: a parallelogram crossing diagonally through the origin.
    std::vector<Pt> clip = {{-1, -2}, {1, 0}, {-1, 2}, {-3, 0}};
    auto out = conflict_geom::ConvexClip(subject, clip);
    const double area = conflict_geom::PolygonArea(out);
    EXPECT_GT(area, 0.0);
    EXPECT_LE(area, 8.0 + 1e-9);
}

TEST(ConflictClip, CwClipPolygonHandledLikeCcw)
{
    // ConvexClip must auto-orient the clip; a CW-wound clip gives the same result.
    std::vector<Pt> subject = {{0, 0}, {2, 0}, {2, 2}, {0, 2}};
    std::vector<Pt> clip_cw = {{1, 1}, {1, 3}, {3, 3}, {3, 1}};  // CW
    auto out = conflict_geom::ConvexClip(subject, clip_cw);
    EXPECT_NEAR(conflict_geom::PolygonArea(out), 1.0, 1e-9);
}

TEST(ConflictGeom, SegmentsIntersectCleanCrossing)
{
    // AB horizontal from (-1,0)->(1,0); CD vertical from (0,-1)->(0,1).
    // They cross at the origin: t=u=0.5.
    double t, u, ix, iy;
    EXPECT_TRUE(conflict_geom::SegmentsIntersect(-1, 0, 1, 0, 0, -1, 0, 1, t, u, ix, iy));
    EXPECT_NEAR(t, 0.5, 1e-9);
    EXPECT_NEAR(u, 0.5, 1e-9);
    EXPECT_NEAR(ix, 0.0, 1e-9);
    EXPECT_NEAR(iy, 0.0, 1e-9);
}

TEST(ConflictGeom, SegmentsIntersectParallelReturnsFalse)
{
    // Two parallel horizontal segments.
    double t, u, ix, iy;
    EXPECT_FALSE(conflict_geom::SegmentsIntersect(0, 0, 2, 0, 0, 1, 2, 1, t, u, ix, iy));
    // Collinear (same line) also rejected.
    EXPECT_FALSE(conflict_geom::SegmentsIntersect(0, 0, 2, 0, 3, 0, 5, 0, t, u, ix, iy));
}

TEST(ConflictGeom, NonOverlappingOnIntersectingLinesReturnsFalse)
{
    // Lines y=0 and y=x would meet at the origin, but neither segment reaches it:
    // AB is (1,0)->(3,0) (x>=1), CD is (1,1)->(3,3) (y>=1). No crossing within spans.
    double t, u, ix, iy;
    EXPECT_FALSE(conflict_geom::SegmentsIntersect(1, 0, 3, 0, 1, 1, 3, 3, t, u, ix, iy));
}

TEST(ConflictGeom, TTouchAtEndpointCounts)
{
    // AB (-1,0)->(1,0); CD starts ON AB at the origin and goes up: (0,0)->(0,2).
    // Endpoint touch lands inside [0,1] for both -> accepted, u=0 at the touch.
    double t, u, ix, iy;
    EXPECT_TRUE(conflict_geom::SegmentsIntersect(-1, 0, 1, 0, 0, 0, 0, 2, t, u, ix, iy));
    EXPECT_NEAR(t, 0.5, 1e-9);
    EXPECT_NEAR(u, 0.0, 1e-9);
}

TEST(ConflictGeom, CrossingAnglePerpendicularAndParallel)
{
    EXPECT_NEAR(conflict_geom::CrossingAngleDeg(1, 0, 0, 1), 90.0, 1e-6);  // perpendicular
    EXPECT_NEAR(conflict_geom::CrossingAngleDeg(1, 0, 1, 0), 0.0, 1e-6);   // parallel
    EXPECT_NEAR(conflict_geom::CrossingAngleDeg(1, 0, -1, 0), 0.0, 1e-6);  // anti-parallel -> folded to 0
}

// ───────────── Phase 3d: positional-release projection (turning other) ─────────
// The yield hold releases once the governing other's rear has cleared the region
// exit by release_buffer, measured as forward = dot(origin - exit, EXIT TANGENT).
// The fix stores the exit tangent (fixed direction the other left the region) and
// projects onto THAT, not the other's instantaneous heading — so a vehicle that
// turns at/after the conflict is still measured correctly. These tests exercise
// ForwardDistanceAlong, the exact primitive the release uses in Evaluate().

TEST(ConflictRelease, ForwardDistanceAlongAxis)
{
    // Exit at origin, tangent +x (unit). Origin 5 m down +x -> forward = 5.
    EXPECT_NEAR(conflict_geom::ForwardDistanceAlong(5, 0, 0, 0, 1, 0), 5.0, 1e-9);
    // Non-unit axis is normalized: axis (3,0) -> still 5 m, not 15.
    EXPECT_NEAR(conflict_geom::ForwardDistanceAlong(5, 0, 0, 0, 3, 0), 5.0, 1e-9);
    // Lateral-only displacement projects to 0 along the tangent.
    EXPECT_NEAR(conflict_geom::ForwardDistanceAlong(0, 4, 0, 0, 1, 0), 0.0, 1e-9);
    // Behind the exit -> negative.
    EXPECT_NEAR(conflict_geom::ForwardDistanceAlong(-2, 0, 0, 0, 1, 0), -2.0, 1e-9);
    // Degenerate axis -> 0 (no false release).
    EXPECT_NEAR(conflict_geom::ForwardDistanceAlong(5, 0, 0, 0, 0, 0), 0.0, 1e-12);
}

TEST(ConflictRelease, TurningOtherClearsAlongStoredTangentNotHeading)
{
    // Region exit at (0,0); the other traversed it heading +x, so the stored exit
    // tangent is (1,0). After clearing, the other turns and now heads +y (its
    // instantaneous heading = (0,1)). Its origin has driven 6 m past the exit and
    // then curved up: origin ≈ (6, 3).
    const double exit_x = 0.0, exit_y = 0.0;
    const double tan_x  = 1.0, tan_y  = 0.0;   // fixed axis captured at the exit
    const double ox = 6.0, oy = 3.0;           // origin after turning up

    // Correct (fixed-tangent) forward distance: projects onto +x -> 6 m. With a
    // g_len/2 = 2.5 and release_buffer 3.0, rear_past_exit = 6 - 2.5 = 3.5 >= 3.0
    // -> RELEASED (correct).
    const double forward_fixed = conflict_geom::ForwardDistanceAlong(ox, oy, exit_x, exit_y, tan_x, tan_y);
    EXPECT_NEAR(forward_fixed, 6.0, 1e-9);
    EXPECT_GE(forward_fixed - 2.5, 3.0);  // releases

    // The OLD instantaneous-heading projection (heading now +y) would measure
    // forward = dot((6,3),(0,1)) = 3 m -> rear_past_exit = 0.5 < 3.0 -> hold NEVER
    // releases even though the other is well clear. Demonstrates the bug the fix
    // avoids.
    const double heading_x = 0.0, heading_y = 1.0;
    const double forward_heading = conflict_geom::ForwardDistanceAlong(ox, oy, exit_x, exit_y, heading_x, heading_y);
    EXPECT_NEAR(forward_heading, 3.0, 1e-9);
    EXPECT_LT(forward_heading - 2.5, 3.0);  // would (wrongly) stay held
}

TEST(ConflictRelease, StraightOtherHoldsUntilRearClears)
{
    // Sanity: for a NON-turning other, fixed tangent == heading, so the release
    // behaves as before. Exit at (10,0), tangent +x, other origin advancing along x.
    const double exit_x = 10.0, exit_y = 0.0, tx = 1.0, ty = 0.0, g_half = 2.5, buf = 3.0;
    // Origin at 12 -> forward 2, rear_past_exit = -0.5 -> held.
    double f = conflict_geom::ForwardDistanceAlong(12.0, 0.0, exit_x, exit_y, tx, ty);
    EXPECT_LT(f - g_half, buf);
    // Origin at 15.6 -> forward 5.6, rear_past_exit = 3.1 -> released.
    f = conflict_geom::ForwardDistanceAlong(15.6, 0.0, exit_x, exit_y, tx, ty);
    EXPECT_GE(f - g_half, buf);
}

// ─────────────── Phase 3d ext: crosswalk pedestrian-yield geometry ─────────────
// Pure helpers backing CrosswalkPedestrianAware: point-in-polygon (ray cast, works
// for non-convex outlines), point-to-polygon distance, box footprint construction,
// and lateral offset from a point to a windowed polyline (passage-band membership).
// crosswalk_geom::Pt is std::array<double,2>, same underlying type as the Pt alias
// already in scope above.

TEST(CrosswalkGeom, PointInPolygonConvexQuad)
{
    // Unit-ish square [0,4]x[0,4].
    std::vector<Pt> sq = {{0, 0}, {4, 0}, {4, 4}, {0, 4}};
    EXPECT_TRUE(crosswalk_geom::PointInPolygon(sq, 2.0, 2.0));    // centre inside
    EXPECT_FALSE(crosswalk_geom::PointInPolygon(sq, 5.0, 2.0));   // right of it
    EXPECT_FALSE(crosswalk_geom::PointInPolygon(sq, -1.0, 2.0));  // left of it
    EXPECT_FALSE(crosswalk_geom::PointInPolygon(sq, 2.0, 10.0));  // above
    // Just inside near an edge.
    EXPECT_TRUE(crosswalk_geom::PointInPolygon(sq, 0.01, 2.0));
    // Degenerate (< 3 verts) -> false.
    std::vector<Pt> two = {{0, 0}, {1, 1}};
    EXPECT_FALSE(crosswalk_geom::PointInPolygon(two, 0.5, 0.5));
}

TEST(CrosswalkGeom, PointInPolygonConcave)
{
    // A concave "arrow"/chevron: an L-shape-like non-convex polygon. Notch removed
    // from the top-right so a point in the notch is OUTSIDE despite being within the
    // bounding box.
    //   (0,0)-(4,0)-(4,4)-(2,4)-(2,2)-(0,2)  (CCW)
    std::vector<Pt> concave = {{0, 0}, {4, 0}, {4, 4}, {2, 4}, {2, 2}, {0, 2}};
    EXPECT_TRUE(crosswalk_geom::PointInPolygon(concave, 1.0, 1.0));   // in the wide base
    EXPECT_TRUE(crosswalk_geom::PointInPolygon(concave, 3.0, 3.0));   // in the tall arm
    EXPECT_FALSE(crosswalk_geom::PointInPolygon(concave, 1.0, 3.0));  // in the removed notch -> OUTSIDE
    EXPECT_FALSE(crosswalk_geom::PointInPolygon(concave, 5.0, 5.0));  // far outside
}

TEST(CrosswalkGeom, DistanceToPolygonInsideZeroOutsidePositive)
{
    std::vector<Pt> sq = {{0, 0}, {4, 0}, {4, 4}, {0, 4}};
    // Inside -> 0.
    EXPECT_NEAR(crosswalk_geom::DistanceToPolygon(sq, 2.0, 2.0), 0.0, 1e-9);
    // Outside, beside an edge: point (6,2) is 2 m to the right of edge x=4.
    EXPECT_NEAR(crosswalk_geom::DistanceToPolygon(sq, 6.0, 2.0), 2.0, 1e-9);
    // Outside past a corner: point (7,8) nearest to corner (4,4) -> dist 5 (3-4-5).
    EXPECT_NEAR(crosswalk_geom::DistanceToPolygon(sq, 7.0, 8.0), 5.0, 1e-9);
    // Just below the bottom edge.
    EXPECT_NEAR(crosswalk_geom::DistanceToPolygon(sq, 2.0, -1.5), 1.5, 1e-9);
}

TEST(CrosswalkGeom, BuildBoxFootprintAxisAligned)
{
    // Heading 0: length along +x, width along +y. Centre origin, len 4, wid 2.
    auto box = crosswalk_geom::BuildBoxFootprint(0.0, 0.0, 0.0, 4.0, 2.0);
    ASSERT_EQ(box.size(), 4u);
    // Corners: front-left (+2,+1), front-right (+2,-1), rear-right (-2,-1), rear-left (-2,+1).
    EXPECT_NEAR(box[0][0], 2.0, 1e-9);  EXPECT_NEAR(box[0][1], 1.0, 1e-9);
    EXPECT_NEAR(box[1][0], 2.0, 1e-9);  EXPECT_NEAR(box[1][1], -1.0, 1e-9);
    EXPECT_NEAR(box[2][0], -2.0, 1e-9); EXPECT_NEAR(box[2][1], -1.0, 1e-9);
    EXPECT_NEAR(box[3][0], -2.0, 1e-9); EXPECT_NEAR(box[3][1], 1.0, 1e-9);
    // Area = length * width = 8.
    EXPECT_NEAR(conflict_geom::PolygonArea(box), 8.0, 1e-9);
}

// Portable pi for the rotation tests (M_PI needs _USE_MATH_DEFINES on MSVC and
// the test target does not define it).
static const double kPi = std::acos(-1.0);

TEST(CrosswalkGeom, BuildBoxFootprintRotated90)
{
    // Heading 90°: length now runs along +y. Centre (10,0), len 4, wid 2.
    const double h = kPi / 2.0;
    auto box = crosswalk_geom::BuildBoxFootprint(10.0, 0.0, h, 4.0, 2.0);
    ASSERT_EQ(box.size(), 4u);
    // front-left = centre + 2*(along heading) + 1*(left of heading).
    // along = (0,1); left = (-1,0). So FL = (10 - 1, 0 + 2) = (9, 2).
    EXPECT_NEAR(box[0][0], 9.0, 1e-9);  EXPECT_NEAR(box[0][1], 2.0, 1e-9);
    // front-right = (10 + 1, 2) = (11, 2).
    EXPECT_NEAR(box[1][0], 11.0, 1e-9); EXPECT_NEAR(box[1][1], 2.0, 1e-9);
    EXPECT_NEAR(conflict_geom::PolygonArea(box), 8.0, 1e-9);
    // Centroid stays at the centre.
    double cx = 0, cy = 0;
    for (auto& p : box) { cx += p[0]; cy += p[1]; }
    EXPECT_NEAR(cx / 4.0, 10.0, 1e-9);
    EXPECT_NEAR(cy / 4.0, 0.0, 1e-9);
}

TEST(CrosswalkGeom, BuildBoxFootprintRotated45)
{
    // Heading 45°: verify area is preserved and a known corner lands where expected.
    const double h = kPi / 4.0;
    auto box = crosswalk_geom::BuildBoxFootprint(0.0, 0.0, h, 2.0, 2.0);  // square 2x2
    ASSERT_EQ(box.size(), 4u);
    EXPECT_NEAR(conflict_geom::PolygonArea(box), 4.0, 1e-9);
    // A 2x2 box rotated 45° has corners at distance sqrt(2) from centre along the
    // diagonals; e.g. front-left = 1*along + 1*left; along=(c,s), left=(-s,c) with
    // c=s=√2/2 -> FL = (√2/2 - √2/2, √2/2 + √2/2) = (0, √2).
    EXPECT_NEAR(box[0][0], 0.0, 1e-9);
    EXPECT_NEAR(box[0][1], std::sqrt(2.0), 1e-9);
}

TEST(CrosswalkGeom, LateralOffsetToPolylineBesideStraightLine)
{
    // Straight polyline along +x from (0,0) to (10,0); s_cum == x.
    std::vector<Pt>     pts   = {{0, 0}, {5, 0}, {10, 0}};
    std::vector<double> s_cum = {0.0, 5.0, 10.0};

    double lat = -1, s_at = -1;
    // Point (4, 3): 3 m to the left of the line at x=4, well inside [0,10].
    ASSERT_TRUE(crosswalk_geom::LateralOffsetToPolyline(pts, s_cum, 4.0, 3.0, 0.0, 10.0, lat, s_at));
    EXPECT_NEAR(lat, 3.0, 1e-9);
    EXPECT_NEAR(s_at, 4.0, 1e-9);

    // Point on the line -> 0 lateral.
    ASSERT_TRUE(crosswalk_geom::LateralOffsetToPolyline(pts, s_cum, 7.0, 0.0, 0.0, 10.0, lat, s_at));
    EXPECT_NEAR(lat, 0.0, 1e-9);
    EXPECT_NEAR(s_at, 7.0, 1e-9);
}

TEST(CrosswalkGeom, LateralOffsetToPolylineWindowClipping)
{
    std::vector<Pt>     pts   = {{0, 0}, {10, 0}};
    std::vector<double> s_cum = {0.0, 10.0};

    // Point (2, 1) with window restricted to [5, 10]: the closest in-window point is
    // clamped to s=5 (x=5), so lateral = distance from (2,1) to (5,0) = sqrt(9+1).
    double lat = -1, s_at = -1;
    ASSERT_TRUE(crosswalk_geom::LateralOffsetToPolyline(pts, s_cum, 2.0, 1.0, 5.0, 10.0, lat, s_at));
    EXPECT_NEAR(s_at, 5.0, 1e-9);
    EXPECT_NEAR(lat, std::sqrt(10.0), 1e-9);

    // Window entirely off the polyline arc span (s in [20,30]) -> no segment selected.
    EXPECT_FALSE(crosswalk_geom::LateralOffsetToPolyline(pts, s_cum, 2.0, 1.0, 20.0, 30.0, lat, s_at));
}

TEST(CrosswalkGeom, LateralOffsetToPolylineDegenerateInputs)
{
    std::vector<Pt>     one   = {{0, 0}};
    std::vector<double> one_s = {0.0};
    double lat, s_at;
    EXPECT_FALSE(crosswalk_geom::LateralOffsetToPolyline(one, one_s, 1.0, 1.0, 0.0, 10.0, lat, s_at));
    // Mismatched sizes -> false.
    std::vector<Pt>     pts   = {{0, 0}, {10, 0}};
    std::vector<double> bad_s = {0.0};
    EXPECT_FALSE(crosswalk_geom::LateralOffsetToPolyline(pts, bad_s, 1.0, 1.0, 0.0, 10.0, lat, s_at));
    // Inverted window (s_hi < s_lo) -> false.
    std::vector<double> s_cum = {0.0, 10.0};
    EXPECT_FALSE(crosswalk_geom::LateralOffsetToPolyline(pts, s_cum, 1.0, 1.0, 8.0, 2.0, lat, s_at));
}

TEST(CrosswalkGeom, LateralOffsetToPolylineClampsDownIntoWindow)
{
    // Point beside the FAR end of the segment while the window only allows the
    // NEAR part: the closest point must be clamped DOWN to the window's upper
    // bound (the mirror of the clamp-up case above).
    std::vector<Pt>     pts   = {{0, 0}, {10, 0}};
    std::vector<double> s_cum = {0.0, 10.0};
    double lat = -1, s_at = -1;
    // Point (8,1): unclamped closest point is s=8, but window [0,5] caps at s=5
    // -> closest allowed point (5,0), lat = sqrt(3^2 + 1) = sqrt(10).
    ASSERT_TRUE(crosswalk_geom::LateralOffsetToPolyline(pts, s_cum, 8.0, 1.0, 0.0, 5.0, lat, s_at));
    EXPECT_NEAR(s_at, 5.0, 1e-9);
    EXPECT_NEAR(lat, std::sqrt(10.0), 1e-9);
}

TEST(CrosswalkGeom, LateralOffsetToPolylineMultiSegmentWindowStraddle)
{
    // Multi-segment polyline where the window covers only part of the FIRST
    // segment: the second segment (s 5..10) must be skipped entirely, and the
    // closest point on the first segment clamped into the window.
    std::vector<Pt>     pts   = {{0, 0}, {5, 0}, {10, 0}};
    std::vector<double> s_cum = {0.0, 5.0, 10.0};
    double lat = -1, s_at = -1;
    // Point (9,1) with window [0,3]: segment 2 (s 5..10) has no arc overlap ->
    // skipped; on segment 1 the closest in-window point is s=3 -> (3,0),
    // lat = sqrt(6^2 + 1) = sqrt(37).
    ASSERT_TRUE(crosswalk_geom::LateralOffsetToPolyline(pts, s_cum, 9.0, 1.0, 0.0, 3.0, lat, s_at));
    EXPECT_NEAR(s_at, 3.0, 1e-9);
    EXPECT_NEAR(lat, std::sqrt(37.0), 1e-9);
}

// ───────────────── Crosswalk route-span extraction (ComputeRouteSpan) ──────────
// Pure span helper backing ScanCrosswalksAhead: PIP pass with SAFE-SIDE entry
// bias (first inside sample minus one step — the sample overshoots the true edge
// in the unsafe direction, eroding the standoff), one-step exit pad, and a
// distance fallback with a DECOUPLED radius (not the sampling step).

namespace
{
// Straight sampled path along +x from x0 to x1 (inclusive) at `step`; s_cum == x - x0.
void BuildStraightPath(double x0, double x1, double step, std::vector<Pt>& pts, std::vector<double>& s)
{
    pts.clear();
    s.clear();
    for (double x = x0; x <= x1 + 1e-9; x += step)
    {
        pts.push_back({x, 0.0});
        s.push_back(x - x0);
    }
}
}  // namespace

TEST(CrosswalkSpan, EntrySafeSideBiasAndExitPad)
{
    std::vector<Pt>     pts;
    std::vector<double> s;
    BuildStraightPath(0.0, 20.0, 1.0, pts, s);  // samples at integer x

    // Footprint x in [5.5, 8.5]: inside samples are x = 6, 7, 8.
    std::vector<Pt> fp = {{5.5, -1}, {8.5, -1}, {8.5, 1}, {5.5, 1}};
    double s_entry = -1, s_exit = -1;
    ASSERT_TRUE(crosswalk_geom::ComputeRouteSpan(pts, s, fp, 1.0, 1.0, s_entry, s_exit));
    // Entry: first inside sample s=6, biased SAFE-SIDE by one step -> 5.0, which
    // is <= the true edge 5.5 (never past it).
    EXPECT_NEAR(s_entry, 5.0, 1e-9);
    EXPECT_LE(s_entry, 5.5);
    // Exit: last inside sample s=8 plus one step -> 9.0, >= the true edge 8.5.
    EXPECT_NEAR(s_exit, 9.0, 1e-9);
    EXPECT_GE(s_exit, 8.5);
}

TEST(CrosswalkSpan, DisjointDoubleCrossingMergesIntoOneSpan)
{
    // PINNED MERGE SEMANTICS: when the path crosses the footprint twice (possible
    // with a concave outline), ComputeRouteSpan merges the crossings into ONE span
    // from the first entry to the last exit — the gap in between is treated as
    // part of the crosswalk. Conservative (the stop is placed before the first
    // crossing) and simpler than tracking multiple spans per object.
    std::vector<Pt>     pts;
    std::vector<double> s;
    BuildStraightPath(0.0, 10.0, 1.0, pts, s);

    // U-shaped footprint: two vertical bars (x [1.5,2.5] and x [6.5,7.5], up to
    // y=1) joined by a base strip below the path (y in [-3,-2]). The path (y=0)
    // is inside only within the bars: samples x=2 and x=7.
    std::vector<Pt> u_shape = {{1.5, -3}, {7.5, -3}, {7.5, 1}, {6.5, 1},
                               {6.5, -2}, {2.5, -2}, {2.5, 1}, {1.5, 1}};
    double s_entry = -1, s_exit = -1;
    ASSERT_TRUE(crosswalk_geom::ComputeRouteSpan(pts, s, u_shape, 1.0, 1.0, s_entry, s_exit));
    EXPECT_NEAR(s_entry, 1.0, 1e-9);  // first inside x=2 -> 2-1
    EXPECT_NEAR(s_exit, 8.0, 1e-9);   // last inside x=7 -> 7+1
}

TEST(CrosswalkSpan, NarrowFootprintCapturedByFallback)
{
    // Footprint narrower than the sampling step and squeezed between two samples:
    // the primary PIP pass finds nothing; the distance fallback captures it.
    std::vector<Pt>     pts;
    std::vector<double> s;
    BuildStraightPath(0.0, 10.0, 1.0, pts, s);

    // Footprint x in [4.3, 4.7] (no integer sample inside), straddling the path.
    std::vector<Pt> fp = {{4.3, -2}, {4.7, -2}, {4.7, 2}, {4.3, 2}};
    double s_entry = -1, s_exit = -1;
    ASSERT_TRUE(crosswalk_geom::ComputeRouteSpan(pts, s, fp, 1.0, 1.0, s_entry, s_exit));
    // Fallback captures samples x=4 and x=5 (distance 0.3 < radius 1.0);
    // x=3 / x=6 are 1.3 away -> not captured.
    EXPECT_NEAR(s_entry, 3.0, 1e-9);  // 4 - step
    EXPECT_NEAR(s_exit, 6.0, 1e-9);   // 5 + step
}

TEST(CrosswalkSpan, NearMissParallelFootprintNotCapturedWithDecoupledRadius)
{
    // A footprint running PARALLEL to the path, 2.5 m to the side, with a COARSE
    // sampling step of 5 m. With the old fallback (radius == step == 5) this
    // near-miss would be falsely captured; with the decoupled radius (2.0 m,
    // derived from the ego half-width) it must NOT be.
    std::vector<Pt>     pts   = {{0, 0}, {5, 0}, {10, 0}};
    std::vector<double> s     = {0.0, 5.0, 10.0};
    std::vector<Pt>     fp    = {{2, 2.5}, {8, 2.5}, {8, 4}, {2, 4}};

    double s_entry = -1, s_exit = -1;
    EXPECT_FALSE(crosswalk_geom::ComputeRouteSpan(pts, s, fp, 5.0, 2.0, s_entry, s_exit));
    // Sanity: the stale coupling (radius = step) WOULD capture it.
    EXPECT_TRUE(crosswalk_geom::ComputeRouteSpan(pts, s, fp, 5.0, 5.0, s_entry, s_exit));
}

// ───────────────── Crosswalk pedestrian-signal phase fold (FoldPedPhase) ───────

using crosswalk_decide::FoldPedPhase;
using crosswalk_decide::LampReading;
using crosswalk_decide::PedPhase;

namespace
{
LampReading MakeLamp(bool constant, bool flashing, LampReading::Color color, bool broken = false)
{
    LampReading r;
    r.constant = constant;
    r.flashing = flashing;
    r.broken   = broken;
    r.color    = color;
    return r;
}
}  // namespace

TEST(CrosswalkPhase, ConstantRedIsRed)
{
    std::vector<LampReading> lamps = {MakeLamp(true, false, LampReading::Color::RED),
                                      MakeLamp(false, false, LampReading::Color::GREEN)};  // green OFF
    EXPECT_EQ(FoldPedPhase(lamps), PedPhase::RED);
}

TEST(CrosswalkPhase, ConstantGreenIsGreen)
{
    std::vector<LampReading> lamps = {MakeLamp(false, false, LampReading::Color::RED),  // red OFF
                                      MakeLamp(true, false, LampReading::Color::GREEN)};
    EXPECT_EQ(FoldPedPhase(lamps), PedPhase::GREEN);
}

TEST(CrosswalkPhase, FlashingIsAmbiguous)
{
    // Flashing green = clearance, not a firm may/may-not-cross -> ambiguous (the
    // waiting rule stays active — safe side).
    std::vector<LampReading> lamps = {MakeLamp(false, true, LampReading::Color::GREEN)};
    EXPECT_EQ(FoldPedPhase(lamps), PedPhase::AMBIGUOUS);
}

TEST(CrosswalkPhase, MixedRedGreenIsAmbiguous)
{
    std::vector<LampReading> lamps = {MakeLamp(true, false, LampReading::Color::RED),
                                      MakeLamp(true, false, LampReading::Color::GREEN)};
    EXPECT_EQ(FoldPedPhase(lamps), PedPhase::AMBIGUOUS);
}

TEST(CrosswalkPhase, UnexpectedColorIsAmbiguous)
{
    std::vector<LampReading> lamps = {MakeLamp(true, false, LampReading::Color::OTHER)};
    EXPECT_EQ(FoldPedPhase(lamps), PedPhase::AMBIGUOUS);
}

TEST(CrosswalkPhase, NoLampsIsAmbiguousAndBrokenIgnored)
{
    // Empty (unreadable signal) -> ambiguous.
    EXPECT_EQ(FoldPedPhase({}), PedPhase::AMBIGUOUS);
    // A broken red lamp is ignored entirely; the remaining constant green decides.
    std::vector<LampReading> lamps = {MakeLamp(true, false, LampReading::Color::RED, /*broken=*/true),
                                      MakeLamp(true, false, LampReading::Color::GREEN)};
    EXPECT_EQ(FoldPedPhase(lamps), PedPhase::GREEN);
}

// ───────────────── Crosswalk blocking classifier (CrosswalkBlocked) ────────────
// Pure two-layer classifier. Fixture: straight ego path along +x (s == x),
// footprint square x [8,12] x y [-3,3] with span [8,12]; ego half-width 1.0,
// wait margin 2.0, release lateral margin 0.5 -> passage band = 1.5 m.

namespace
{
using crosswalk_decide::BlockParams;
using crosswalk_decide::CrosswalkBlocked;
using crosswalk_decide::PedState;

struct ClassifierFixture
{
    std::vector<Pt>     path;
    std::vector<double> s;
    std::vector<Pt>     fp = {{8, -3}, {12, -3}, {12, 3}, {8, 3}};
    double              s_entry = 8.0;
    double              s_exit  = 12.0;
    BlockParams         params;

    ClassifierFixture()
    {
        BuildStraightPath(0.0, 20.0, 1.0, path, s);
        params.ego_half_width         = 1.0;
        params.wait_margin            = 2.0;
        params.release_lateral_margin = 0.5;  // band = 1.5
        params.waiting_rule_active    = true;
        params.committed              = false;
        params.ego_inside_footprint   = false;
    }

    bool Blocked(const std::vector<PedState>& peds) const
    {
        return CrosswalkBlocked(peds, fp, path, s, s_entry, s_exit, params).blocked;
    }

    // Same call, but keeping the identity of the ped that did the blocking.
    crosswalk_decide::BlockResult BlockedBy(const std::vector<PedState>& peds) const
    {
        return CrosswalkBlocked(peds, fp, path, s, s_entry, s_exit, params);
    }
};

PedState MakePed(double x, double y, double vx = 0.0, double vy = 0.0, int osi_id = -1)
{
    PedState p;
    p.x      = x;
    p.y      = y;
    p.vx     = vx;
    p.vy     = vy;
    p.osi_id = osi_id;
    return p;
}
}  // namespace

TEST(CrosswalkClassify, CrossingPedInBandBlocks)
{
    ClassifierFixture f;
    // On the footprint, inside the passage band -> blocks regardless of velocity.
    EXPECT_TRUE(f.Blocked({MakePed(10.0, 0.5)}));
    EXPECT_TRUE(f.Blocked({MakePed(10.0, 0.5, 0.0, 5.0)}));  // even moving away
}

TEST(CrosswalkClassify, CrossingOutOfBandMovingAwayExempt)
{
    ClassifierFixture f;
    // On the footprint but 2.5 m off the path (> band 1.5) and walking AWAY
    // (+y, matching the away direction) at 1.5 m/s -> exempt (world velocity is
    // consumed directly, not reconstructed from heading).
    EXPECT_FALSE(f.Blocked({MakePed(10.0, 2.5, 0.0, 1.5)}));
    // Same spot but stationary -> a body on the roadway; blocks.
    EXPECT_TRUE(f.Blocked({MakePed(10.0, 2.5)}));
    // Same spot but approaching the path (-y) -> blocks.
    EXPECT_TRUE(f.Blocked({MakePed(10.0, 2.5, 0.0, -1.0)}));
}

TEST(CrosswalkClassify, WaitingPedWithinMarginBlocks)
{
    ClassifierFixture f;
    // 1.5 m outside the footprint edge (y=4.5 vs edge y=3), out of the passage
    // band -> waiting block.
    EXPECT_TRUE(f.Blocked({MakePed(10.0, 4.5)}));
    // Beyond the wait margin (dist 3.0 > 2.0) -> free.
    EXPECT_FALSE(f.Blocked({MakePed(10.0, 6.0)}));
}

TEST(CrosswalkClassify, WaitingSuppressedByGate)
{
    ClassifierFixture f;
    f.params.waiting_rule_active = false;  // RED ped phase / yield_to_waiting off
    EXPECT_FALSE(f.Blocked({MakePed(10.0, 4.5)}));
    // The CROSSING rule is never gated: a ped ON the footprint still blocks.
    EXPECT_TRUE(f.Blocked({MakePed(10.0, 0.5)}));
}

TEST(CrosswalkClassify, WaitingSuppressedWhileEgoOnFootprint)
{
    ClassifierFixture f;
    f.params.ego_inside_footprint = true;  // ego must clear the crosswalk, not park on it
    EXPECT_FALSE(f.Blocked({MakePed(10.0, 4.5)}));
    // CROSSING still active (emergency braking never suppressed).
    EXPECT_TRUE(f.Blocked({MakePed(10.0, 0.5)}));
}

TEST(CrosswalkClassify, WaitingHysteresisWidensBandWhileCommitted)
{
    ClassifierFixture f;
    // Ped 2.3 m off the footprint: outside the base margin (2.0) but inside the
    // committed margin (2.5) -> blocked only while this crosswalk is the latched one.
    const PedState hovering = MakePed(10.0, 5.3);
    f.params.committed = false;
    EXPECT_FALSE(f.Blocked({hovering}));
    f.params.committed = true;
    EXPECT_TRUE(f.Blocked({hovering}));
}

// ── Identity carry-through: WHICH pedestrian blocked (OSI id space) ──────────
// The classifier flattens entities into anonymous positions, so before this the
// policy could report that a crosswalk held the ego but never which body did it.
// The id is opaque to the classifier — it must come back out unchanged.

TEST(CrosswalkClassify, CrossingBlockNamesTheBlockingPed)
{
    ClassifierFixture f;
    const auto r = f.BlockedBy({MakePed(10.0, 0.5, 0.0, 0.0, /*osi_id=*/57)});
    EXPECT_TRUE(r.blocked);
    EXPECT_EQ(r.ped_osi_id, 57);
}

TEST(CrosswalkClassify, WaitingBlockNamesTheBlockingPed)
{
    ClassifierFixture f;
    const auto r = f.BlockedBy({MakePed(10.0, 4.5, 0.0, 0.0, /*osi_id=*/58)});
    EXPECT_TRUE(r.blocked);
    EXPECT_EQ(r.ped_osi_id, 58);
}

TEST(CrosswalkClassify, UnblockedResultCarriesNoPedId)
{
    ClassifierFixture f;
    // Beyond the wait margin -> free, and no subject to name.
    const auto r = f.BlockedBy({MakePed(10.0, 6.0, 0.0, 0.0, /*osi_id=*/59)});
    EXPECT_FALSE(r.blocked);
    EXPECT_EQ(r.ped_osi_id, -1);
}

TEST(CrosswalkClassify, ExemptPedIsNotNamedWhenAnotherBlocks)
{
    ClassifierFixture f;
    // First ped is on the footprint but out of band and departing (exempt, see
    // CrossingOutOfBandMovingAwayExempt); the second one blocks. The reported id
    // must be the blocker's, not merely the first ped scanned.
    const auto r = f.BlockedBy({MakePed(10.0, 2.5, 0.0, 1.5, /*osi_id=*/60),
                                MakePed(10.0, 0.5, 0.0, 0.0, /*osi_id=*/61)});
    EXPECT_TRUE(r.blocked);
    EXPECT_EQ(r.ped_osi_id, 61);
}

// ─────────────── Phase 3e (F3): junction priority resolution ────────────────
// Pure right-of-way from OpenDRIVE <priority high low> connecting-road ids.
using junction_priority::Relation;
using junction_priority::Resolve;

namespace
{
// The 4way_priority__main_ns fixture: conn 100 (N-S through) HIGH over 101..105.
const std::vector<std::pair<std::string, std::string>> k4wayPriorities = {
    {"100", "101"}, {"100", "102"}, {"100", "103"}, {"100", "104"}, {"100", "105"}};
}  // namespace

TEST(JunctionPriority, EgoOnHighRoadOutranksLowCrosser)
{
    // Ego on conn 100 (high) vs crosser on conn 101 (low) -> ego proceeds.
    EXPECT_EQ(Resolve("100", "101", k4wayPriorities), Relation::EGO_PRIORITY);
    EXPECT_EQ(Resolve("100", "104", k4wayPriorities), Relation::EGO_PRIORITY);
}

TEST(JunctionPriority, EgoOnLowRoadYieldsToHighCrosser)
{
    // Ego on conn 101 (low) vs crosser on conn 100 (high) -> ego yields.
    EXPECT_EQ(Resolve("101", "100", k4wayPriorities), Relation::OTHER_PRIORITY);
    EXPECT_EQ(Resolve("105", "100", k4wayPriorities), Relation::OTHER_PRIORITY);
}

TEST(JunctionPriority, NoRelationBetweenTwoLowRoadsIsUnknown)
{
    // Two low connecting roads (turn-vs-turn) share no <priority> entry -> UNKNOWN
    // (falls back to the base conflict yield).
    EXPECT_EQ(Resolve("102", "103", k4wayPriorities), Relation::UNKNOWN);
    EXPECT_EQ(Resolve("104", "105", k4wayPriorities), Relation::UNKNOWN);
}

TEST(JunctionPriority, EmptyOrIdenticalIdsNeverMatch)
{
    EXPECT_EQ(Resolve("", "101", k4wayPriorities), Relation::UNKNOWN);
    EXPECT_EQ(Resolve("100", "", k4wayPriorities), Relation::UNKNOWN);
    EXPECT_EQ(Resolve("100", "100", k4wayPriorities), Relation::UNKNOWN);
}

TEST(JunctionPriority, EmptyPriorityListFallsBackToUnknown)
{
    // Junction with no <priority> data -> everyone yields (base behaviour).
    EXPECT_EQ(Resolve("100", "101", {}), Relation::UNKNOWN);
}

TEST(JunctionPriority, UnrelatedConnectingRoadIsUnknown)
{
    // A connecting road id that appears in no entry -> UNKNOWN either way.
    EXPECT_EQ(Resolve("100", "999", k4wayPriorities), Relation::UNKNOWN);
    EXPECT_EQ(Resolve("999", "100", k4wayPriorities), Relation::UNKNOWN);
}
