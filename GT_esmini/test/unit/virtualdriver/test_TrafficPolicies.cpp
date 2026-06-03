#include <gtest/gtest.h>

#include "gt_esmini/control/virtualdriver/policies/LeadVehicleAware.hpp"
#include "gt_esmini/control/virtualdriver/policies/TrafficLightAware.hpp"
#include "gt_esmini/control/virtualdriver/policies/StopYieldSignAware.hpp"

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
