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

// ────────────── RouteSignalScan: FindPairedStopLineByDistance ──────────────
// Same pairing rule as the FindPairedStopLine (anchor_index) suite above, but
// the anchor is a bare route distance (e.g. RouteJunctionSpan::entry_ahead)
// rather than one of `signals`' own entries -- a distinct function, not a
// std::size_t/double overload of FindPairedStopLine (see RouteSignalScan.hpp
// for why: every anchor_index call site above passes a bare int literal, and
// adding a double overload under the same name makes those ambiguous).
// Boundary conditions mirror that suite one-for-one, minus
// AnchorNeverPairsWithItselfEvenIfClassifiedAsStopLine (there is no
// anchor_index here to exclude) plus its mirror image below: a bare distance
// that happens to coincide with a stop-line entry's own distance_ahead is a
// legitimate zero-offset pairing, not a self-reference to guard against.

TEST(FindPairedStopLineByDistance, EmptySignalsIsNone)
{
    std::vector<ScannedSignal> signals;
    EXPECT_FALSE(FindPairedStopLineByDistance(signals, 20.0, 10.0).has_value());
}

TEST(FindPairedStopLineByDistance, AnchorDistanceMatchingAStopLinesOwnDistanceStillPairs)
{
    std::vector<ScannedSignal> signals = {Entry(20.0, true)};
    const auto                 result  = FindPairedStopLineByDistance(signals, 20.0, 10.0);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), static_cast<std::size_t>(0));
}

TEST(FindPairedStopLineByDistance, NoStopLineClassifiedEntryIsNone)
{
    std::vector<ScannedSignal> signals = {Entry(10.0, false), Entry(20.0, false)};
    EXPECT_FALSE(FindPairedStopLineByDistance(signals, 20.0, 10.0).has_value());
}

TEST(FindPairedStopLineByDistance, StopLineOutsideWindowIsNone)
{
    // 12 m short of the anchor distance; window is only 10 m -> falls back.
    std::vector<ScannedSignal> signals = {Entry(8.0, true), Entry(20.0, false)};
    EXPECT_FALSE(FindPairedStopLineByDistance(signals, 20.0, 10.0).has_value());
}

TEST(FindPairedStopLineByDistance, StopLineFartherThanAnchorIsNone)
{
    // A stop-line entry AHEAD of the anchor distance (larger distance_ahead) never pairs.
    std::vector<ScannedSignal> signals = {Entry(20.0, false), Entry(25.0, true)};
    EXPECT_FALSE(FindPairedStopLineByDistance(signals, 20.0, 10.0).has_value());
}

TEST(FindPairedStopLineByDistance, TieAtSameDistanceKeepsFirstEncountered)
{
    // Mirrors the anchor_index suite's SameDistanceAsAnchorIsAcceptedRegardlessOfVectorOrder:
    // std::sort is not stable, so two stop-line entries tied at the winning
    // distance must resolve deterministically (first index encountered), not by
    // vector position relative to some anchor entry (there isn't one here).
    std::vector<ScannedSignal> signals = {Entry(18.0, true), Entry(18.0, true), Entry(20.0, false)};
    const auto                 result  = FindPairedStopLineByDistance(signals, 20.0, 10.0);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), static_cast<std::size_t>(0));
}

TEST(FindPairedStopLineByDistance, WindowBoundaryDifferenceIsAccepted)
{
    // distance_ahead difference is EXACTLY window (10.0) -> accepted (<=).
    std::vector<ScannedSignal> signals = {Entry(10.0, true), Entry(20.0, false)};
    const auto                 result  = FindPairedStopLineByDistance(signals, 20.0, 10.0);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), static_cast<std::size_t>(0));
}

TEST(FindPairedStopLineByDistance, JustBeyondWindowBoundaryIsRejected)
{
    std::vector<ScannedSignal> signals = {Entry(9.999999, true), Entry(20.0, false)};
    EXPECT_FALSE(FindPairedStopLineByDistance(signals, 20.0, 10.0).has_value());
}

TEST(FindPairedStopLineByDistance, MultipleCandidatesInWindowReturnNearestToAnchor)
{
    std::vector<ScannedSignal> signals = {
        Entry(12.0, true),   // 8 m short of the anchor
        Entry(15.0, true),   // 5 m short
        Entry(18.0, true),   // 2 m short -> nearest, expected winner
    };
    const auto result = FindPairedStopLineByDistance(signals, 20.0, 10.0);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), static_cast<std::size_t>(2));
}

// ────────────── RouteSignalScan: ResolveStopLineAnchor (pure) ──────────────
// TrafficLightAware::Evaluate calls this once the junction a governing head
// governs is resolved on this route, to pick which distance
// FindPairedStopLineByDistance should search from: the junction's entry, or
// the head's own distance, whichever is nearer the ego. Pins the bug where an
// entry-only anchor can accept a stop-line farther from the ego than the head
// itself (a near-side head set back from the box, or any far-side head),
// pushing the stop target past the head and off design/stop_line_stop_target.md
// §5's scan-retention guarantee (the head must stay inside the forward scan
// while stopped, or the RED constraint vanishes and it reads as running the
// light).

TEST(ResolveStopLineAnchor, NearSideHeadBeforeEntryPicksHead)
{
    // Typical near-side placement: the head stands short of the junction it
    // governs, so its own distance is the nearer of the two.
    const StopLineAnchor anchor = ResolveStopLineAnchor(/*junction_entry_ahead=*/50.0, /*head_dist_ahead=*/48.0);
    EXPECT_DOUBLE_EQ(anchor.distance_ahead, 48.0);
    EXPECT_STREQ(anchor.token, "head");
}

TEST(ResolveStopLineAnchor, FarSideHeadBeyondEntryPicksJunctionEntry)
{
    // Far-side placement: the head stands beyond the junction it governs (a
    // mast arm across the intersection), so the entry is the nearer distance.
    const StopLineAnchor anchor = ResolveStopLineAnchor(/*junction_entry_ahead=*/40.0, /*head_dist_ahead=*/55.0);
    EXPECT_DOUBLE_EQ(anchor.distance_ahead, 40.0);
    EXPECT_STREQ(anchor.token, "junction_entry");
}

TEST(ResolveStopLineAnchor, TieBetweenEntryAndHeadPicksJunctionEntryToken)
{
    const StopLineAnchor anchor = ResolveStopLineAnchor(33.0, 33.0);
    EXPECT_DOUBLE_EQ(anchor.distance_ahead, 33.0);
    EXPECT_STREQ(anchor.token, "junction_entry");
}

TEST(ResolveStopLineAnchor, StopLineBehindHeadButBeforeEntryIsRejectedByPairing)
{
    // Reported regression: a near-side head (8 m short of the junction entry)
    // with a stop line only 3 m short of that same entry -- the stop line sits
    // AFTER the head, 5 m deeper into the approach. An entry-only anchor pairs
    // with it anyway (it is <= the entry and within window), moving the stop
    // target past the head. The min-clamped anchor must reject it instead.
    const double junction_entry_ahead = 100.0;
    const double head_dist_ahead      = 92.0;  // 8 m short of the entry
    const double stop_line_dist_ahead = 97.0;  // 3 m short of the entry, 5 m PAST the head

    const StopLineAnchor anchor = ResolveStopLineAnchor(junction_entry_ahead, head_dist_ahead);
    EXPECT_DOUBLE_EQ(anchor.distance_ahead, head_dist_ahead);
    EXPECT_STREQ(anchor.token, "head");

    std::vector<ScannedSignal> signals = {Entry(stop_line_dist_ahead, true)};
    EXPECT_FALSE(FindPairedStopLineByDistance(signals, anchor.distance_ahead, 10.0).has_value());

    // Confirms the scenario actually exercises the defect: pairing against the
    // entry alone (the pre-fix anchor) WOULD have accepted this same stop line.
    EXPECT_TRUE(FindPairedStopLineByDistance(signals, junction_entry_ahead, 10.0).has_value());
}

TEST(ResolveStopLineAnchor, StopLineAtOrBeforeHeadIsStillAcceptedByPairing)
{
    // Same near-side geometry as above, but the stop line is correctly placed
    // at-or-before the head (4 m short of it) -- the min-clamped anchor must
    // still accept a legitimately paired stop line.
    const double junction_entry_ahead = 100.0;
    const double head_dist_ahead      = 92.0;
    const double stop_line_dist_ahead = 88.0;  // 4 m short of the head

    const StopLineAnchor       anchor  = ResolveStopLineAnchor(junction_entry_ahead, head_dist_ahead);
    std::vector<ScannedSignal> signals = {Entry(stop_line_dist_ahead, true)};
    const auto                 result  = FindPairedStopLineByDistance(signals, anchor.distance_ahead, 10.0);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), static_cast<std::size_t>(0));
}
