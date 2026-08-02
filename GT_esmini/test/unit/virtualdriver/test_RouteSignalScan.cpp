#include <gtest/gtest.h>

#include "gt_esmini/control/virtualdriver/policies/RouteSignalScan.hpp"

#include <cstddef>
#include <optional>
#include <vector>

using namespace gt_esmini;

// ────────────── RouteSignalScan: FindPairedStopLine (pure) ──────────────
// signals is hand-rolled here, distance-ascending per ScanSignalsAhead's own
// output convention. FindPairedStopLine never dereferences ScannedSignal::signal,
// so every entry below leaves it at its default nullptr.

namespace
{
ScannedSignal Entry(double distance_ahead, bool is_stop_line)
{
    ScannedSignal s;
    s.distance_ahead = distance_ahead;
    s.is_stop_line   = is_stop_line;
    return s;
}
}  // namespace

TEST(FindPairedStopLine, OutOfRangeAnchorIsNone)
{
    std::vector<ScannedSignal> signals = {Entry(5.0, true)};
    EXPECT_FALSE(FindPairedStopLine(signals, 1, 10.0).has_value());
    EXPECT_FALSE(FindPairedStopLine(signals, 99, 10.0).has_value());
}

TEST(FindPairedStopLine, EmptySignalsIsNone)
{
    std::vector<ScannedSignal> signals;
    EXPECT_FALSE(FindPairedStopLine(signals, 0, 10.0).has_value());
}

TEST(FindPairedStopLine, AnchorNeverPairsWithItselfEvenIfClassifiedAsStopLine)
{
    std::vector<ScannedSignal> signals = {Entry(20.0, true)};
    EXPECT_FALSE(FindPairedStopLine(signals, 0, 10.0).has_value());
}

TEST(FindPairedStopLine, NoStopLineClassifiedEntryIsNone)
{
    std::vector<ScannedSignal> signals = {Entry(10.0, false), Entry(20.0, false)};
    EXPECT_FALSE(FindPairedStopLine(signals, 1, 10.0).has_value());
}

TEST(FindPairedStopLine, StopLineOutsideWindowIsNone)
{
    // 12 m short of the anchor; window is only 10 m -> falls back.
    std::vector<ScannedSignal> signals = {Entry(8.0, true), Entry(20.0, false)};
    EXPECT_FALSE(FindPairedStopLine(signals, 1, 10.0).has_value());
}

TEST(FindPairedStopLine, StopLineFartherThanAnchorIsNone)
{
    // A stop-line entry AHEAD of the anchor (larger distance_ahead) never pairs.
    std::vector<ScannedSignal> signals = {Entry(20.0, false), Entry(25.0, true)};
    EXPECT_FALSE(FindPairedStopLine(signals, 0, 10.0).has_value());
}

TEST(FindPairedStopLine, SameDistanceAsAnchorIsAcceptedRegardlessOfVectorOrder)
{
    // A tie at the SAME distance as the anchor must be accepted whether the
    // stop-line entry sits before or after anchor_index in the vector
    // (ScanSignalsAhead's std::sort is not stable, so either arrangement can
    // occur for tied distances).
    std::vector<ScannedSignal> stop_line_first = {Entry(20.0, true), Entry(20.0, false)};
    const auto                 first_result    = FindPairedStopLine(stop_line_first, 1, 10.0);
    ASSERT_TRUE(first_result.has_value());
    EXPECT_EQ(first_result.value(), static_cast<std::size_t>(0));

    std::vector<ScannedSignal> stop_line_second = {Entry(20.0, false), Entry(20.0, true)};
    const auto                 second_result    = FindPairedStopLine(stop_line_second, 0, 10.0);
    ASSERT_TRUE(second_result.has_value());
    EXPECT_EQ(second_result.value(), static_cast<std::size_t>(1));
}

TEST(FindPairedStopLine, WindowBoundaryDifferenceIsAccepted)
{
    // distance_ahead difference is EXACTLY window (10.0) -> accepted (<=).
    std::vector<ScannedSignal> signals = {Entry(10.0, true), Entry(20.0, false)};
    const auto                 result  = FindPairedStopLine(signals, 1, 10.0);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), static_cast<std::size_t>(0));
}

TEST(FindPairedStopLine, JustBeyondWindowBoundaryIsRejected)
{
    std::vector<ScannedSignal> signals = {Entry(9.999999, true), Entry(20.0, false)};
    EXPECT_FALSE(FindPairedStopLine(signals, 1, 10.0).has_value());
}

TEST(FindPairedStopLine, MultipleCandidatesInWindowReturnNearestToAnchor)
{
    std::vector<ScannedSignal> signals = {
        Entry(12.0, true),   // 8 m short of the anchor
        Entry(15.0, true),   // 5 m short
        Entry(18.0, true),   // 2 m short -> nearest, expected winner
        Entry(20.0, false),  // anchor (governing head / STOP sign)
    };
    const auto result = FindPairedStopLine(signals, 3, 10.0);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), static_cast<std::size_t>(2));
}
