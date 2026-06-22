#include <gtest/gtest.h>

#include "gt_esmini/control/virtualdriver/policies/LeadVehicleAware.hpp"
#include "gt_esmini/control/virtualdriver/policies/TrafficLightAware.hpp"
#include "gt_esmini/control/virtualdriver/policies/StopYieldSignAware.hpp"
#include "gt_esmini/control/virtualdriver/policies/ConflictPointResolver.hpp"

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

// ───────────────── Phase 3d: conflict-point geometry / timing ──────────────

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

TEST(ConflictGap, OtherArrivesWellAfterEgoProceeds)
{
    // Ego reaches the point at t=2s; other not entering until t=10s (and clears at 12).
    EXPECT_EQ(conflict_geom::GapDecision(2.0, 10.0, 12.0, 2.0), conflict_geom::GapAction::PROCEED);
}

TEST(ConflictGap, TemporalOverlapYields)
{
    // Ego arrives at t=5s while the other occupies [4,6] -> overlap -> YIELD.
    EXPECT_EQ(conflict_geom::GapDecision(5.0, 4.0, 6.0, 2.0), conflict_geom::GapAction::YIELD);
}

TEST(ConflictGap, OtherAlreadyClearedProceeds)
{
    // Other cleared at t=1s; ego arrives at t=5s, well past t_exit + accept_gap (3s).
    EXPECT_EQ(conflict_geom::GapDecision(5.0, 0.0, 1.0, 2.0), conflict_geom::GapAction::PROCEED);
}

TEST(ConflictGap, EgoArrivesJustInsideAcceptGapOfEnterYields)
{
    // Other enters at t=8s; ego arrives at t=6.5s = t_enter - 1.5s, inside accept_gap=2s -> YIELD.
    EXPECT_EQ(conflict_geom::GapDecision(6.5, 8.0, 10.0, 2.0), conflict_geom::GapAction::YIELD);
    // Just outside the margin (t_ego = t_enter - 2.5 < t_enter - accept_gap) -> PROCEED.
    EXPECT_EQ(conflict_geom::GapDecision(5.5, 8.0, 10.0, 2.0), conflict_geom::GapAction::PROCEED);
}

// --- hysteresis at the call-site (entry vs wider release margin) ------------
// GapDecision stays PURE; the resolver's latch calls it twice with the entry
// margin (accept_gap) and the release margin (accept_gap + release_extra). These
// lock in the two-margin intent: a borderline-late other that YIELDs on entry
// still YIELDs under the wider release margin, so a committed stop HOLDS instead
// of releasing the moment the entry test flickers to PROCEED.

TEST(ConflictGap, BorderlineLateYieldsOnEntryMargin)
{
    // Other cleared the zone at t=4s (just BEFORE the ego's floored arrival t=5s).
    // With the entry margin accept_gap=2s the clear band reaches t_exit+2 = 6s >= 5
    // -> still YIELD (the gap is too tight to commit).
    EXPECT_EQ(conflict_geom::GapDecision(5.0, 2.0, 4.0, 2.0), conflict_geom::GapAction::YIELD);
}

TEST(ConflictGap, SameGeometryStillYieldsUnderWiderReleaseMargin)
{
    // Identical geometry; widen by release_extra=1.5 (accept_gap 2 -> 3.5). The
    // clear band now reaches t_exit+3.5 = 7.5s, comfortably past the ego's t=5s
    // -> the committed yield HOLDS (no premature release / no chatter).
    constexpr double accept_gap   = 2.0;
    constexpr double release_extra = 1.5;
    EXPECT_EQ(conflict_geom::GapDecision(5.0, 2.0, 4.0, accept_gap + release_extra),
              conflict_geom::GapAction::YIELD);
}
