/*
 * S0 scaffolding tests for the OSI logical-lane post-pass.
 *
 * Deliberately small: S0 emits nothing, so there is nothing about the OSI model
 * to assert yet. What CAN be pinned here is the flag contract and the empty
 * index, and those are the two things S1 will build on.
 *
 * NOT asserted here -- and this is the point: "the default is OFF" cannot be
 * proven inside the umbrella gate binary. The env read latches on the first
 * query, and gtest gives no order guarantee across the ~60 sources sharing this
 * process, so any test that called the setter first would make a later
 * "default" assertion vacuous. The default, and the fact that the flag is read
 * at all rather than dead, are measured per-process by
 * scripts/probe_osi_logical_lane_size.py, which runs each fixture in a fresh
 * interpreter with the variable set and unset.
 *
 * Design: GT_esmini/docs/osi/logical_lane_and_route_design.md section 7
 * Knowledge graph: spine-work:osi-logical-lane
 */
#include <gtest/gtest.h>

#include "gt_esmini/osi/GT_OsiLogicalLane.hpp"

// ---- flag: both polarities round-trip through the setter ----
TEST(OsiLogicalLane, FeatureFlagSetterBothPolarities)
{
    gt_esmini::osi::SetUseOsiLogicalLane(true);
    EXPECT_TRUE(gt_esmini::osi::GetUseOsiLogicalLane());

    gt_esmini::osi::SetUseOsiLogicalLane(false);
    EXPECT_FALSE(gt_esmini::osi::GetUseOsiLogicalLane());

    // Leave the process in the default state: this binary shares one address
    // space with every other unit source, and the flag is a global.
    gt_esmini::osi::SetUseOsiLogicalLane(false);
}

// ---- S0 invariant: the pass emits nothing and leaves the index empty, ON or OFF ----
TEST(OsiLogicalLane, S0PostPassLeavesIndexEmpty)
{
    gt_esmini::osi::SetUseOsiLogicalLane(false);
    gt_esmini::osi::BuildOsiLogicalLanes(nullptr);
    EXPECT_TRUE(gt_esmini::osi::GetLogicalLaneIndex().empty()) << "OFF: nothing indexed";

    gt_esmini::osi::SetUseOsiLogicalLane(true);
    gt_esmini::osi::BuildOsiLogicalLanes(nullptr);
    EXPECT_TRUE(gt_esmini::osi::GetLogicalLaneIndex().empty())
        << "S0 ON: the pass is wired but emits nothing, so the index stays empty until S1";

    gt_esmini::osi::SetUseOsiLogicalLane(false);
}
