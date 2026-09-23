/*
 * HostVehicleData UDP fragmentation (design logical_lane_and_route_design.md section 6).
 *
 * Send() used to drop any HostVehicleData over 8192 B with a LOG_WARN, so the
 * split path is new code that nothing in the repo can reach yet -- HVD only grows
 * past one datagram once `route` is populated (S4). The split is therefore
 * factored into a pure planner so both polarities can be exercised here instead
 * of waiting for a payload big enough to trigger it in a live run.
 *
 * What the receivers require of the plan, and what these tests pin:
 *   - a message that fits stays a SINGLE packet with counter == 0 (unchanged
 *     behaviour for every existing consumer)
 *   - a split runs 1, 2, ... N with the LAST counter negated; both
 *     web/backend/services/osi_bridge.py and DriverScript/realdriver/udp_common.py
 *     start reassembly on |counter| == 1 and finish on counter < 0, so an
 *     off-by-one at either end silently strands the stream
 *   - offsets are contiguous and datasizes sum back to the message, or the
 *     reassembled protobuf is corrupt rather than absent
 */
#include <gtest/gtest.h>

#include "gt_esmini/osi/GT_HostVehicleReporter.hpp"

#include <numeric>

using gt_esmini::PlanHostVehicleUdpChunks;
using gt_esmini::UdpChunk;

namespace
{
// Every plan must be reassemblable: contiguous, complete, and within budget.
void ExpectCoversMessage(const std::vector<UdpChunk>& plan, unsigned int total, unsigned int max_payload)
{
    unsigned int expected_offset = 0;
    for (const UdpChunk& c : plan)
    {
        EXPECT_EQ(c.offset, expected_offset);
        EXPECT_GT(c.datasize, 0u) << "an empty packet would confuse the length check on the receive side";
        EXPECT_LE(c.datasize, max_payload);
        expected_offset += c.datasize;
    }
    EXPECT_EQ(expected_offset, total) << "the packets do not add back up to the message";
}
}  // namespace

// ---- polarity 1: it fits -- one packet, counter 0, exactly as before ----
TEST(HostVehicleUdpChunks, FittingMessageStaysOneCounterZeroPacket)
{
    for (unsigned int size : {1u, 100u, 8191u, 8192u})
    {
        const auto plan = PlanHostVehicleUdpChunks(size, 8192);
        ASSERT_EQ(plan.size(), 1u) << "size " << size;
        EXPECT_EQ(plan[0].counter, 0) << "size " << size << ": counter must stay 0 for a single packet";
        EXPECT_EQ(plan[0].offset, 0u);
        EXPECT_EQ(plan[0].datasize, size);
    }
}

// ---- polarity 2: one byte over -- it splits, and the last counter is negative ----
TEST(HostVehicleUdpChunks, OneByteOverBudgetSplitsIntoTwo)
{
    const auto plan = PlanHostVehicleUdpChunks(8193, 8192);
    ASSERT_EQ(plan.size(), 2u);
    EXPECT_EQ(plan[0].counter, 1);
    EXPECT_EQ(plan[0].datasize, 8192u);
    EXPECT_EQ(plan[1].counter, -2) << "the last packet is the negated index, not -1 and not +2";
    EXPECT_EQ(plan[1].datasize, 1u);
    ExpectCoversMessage(plan, 8193, 8192);
}

// ---- a realistic long route: many packets, 1..N with N negated ----
TEST(HostVehicleUdpChunks, LargeMessageNumbersOneToNWithLastNegated)
{
    const unsigned int total = 40000;  // ~ a 100-section route's worth of lane segments
    const auto         plan  = PlanHostVehicleUdpChunks(total, 8192);
    ASSERT_EQ(plan.size(), 5u);
    for (size_t i = 0; i + 1 < plan.size(); i++)
    {
        EXPECT_EQ(plan[i].counter, static_cast<int>(i + 1));
    }
    EXPECT_EQ(plan.back().counter, -static_cast<int>(plan.size()));
    ExpectCoversMessage(plan, total, 8192);
}

// ---- exact multiple: no trailing empty packet ----
TEST(HostVehicleUdpChunks, ExactMultipleOfBudgetHasNoEmptyTail)
{
    const auto plan = PlanHostVehicleUdpChunks(8192 * 3, 8192);
    ASSERT_EQ(plan.size(), 3u);
    EXPECT_EQ(plan[0].counter, 1);
    EXPECT_EQ(plan[1].counter, 2);
    EXPECT_EQ(plan[2].counter, -3);
    for (const UdpChunk& c : plan)
    {
        EXPECT_EQ(c.datasize, 8192u);
    }
    ExpectCoversMessage(plan, 8192 * 3, 8192);
}

// ---- degenerate inputs produce no packets rather than a malformed one ----
TEST(HostVehicleUdpChunks, EmptyMessageOrZeroBudgetPlansNothing)
{
    EXPECT_TRUE(PlanHostVehicleUdpChunks(0, 8192).empty());
    EXPECT_TRUE(PlanHostVehicleUdpChunks(100, 0).empty());
}
