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
