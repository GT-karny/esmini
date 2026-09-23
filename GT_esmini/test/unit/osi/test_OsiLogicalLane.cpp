/*
 * Unit tests for the OSI logical-lane post-pass (S0 flag contract + S1 model).
 *
 * The pass is exercised through BuildOsiLogicalLanesInto(), which takes the
 * GroundTruth explicitly. The wrapper BuildOsiLogicalLanes() resolves
 * obj_osi_internal.static_gt instead, and that pointer is only non-null once an
 * OSIReporter has been constructed -- standing one up here would drag in the
 * whole scenario engine for no extra coverage.
 *
 * NOT asserted here -- and this is the point: "the default is OFF" cannot be
 * proven inside the umbrella gate binary. The env read latches on the first
 * query, and gtest gives no order guarantee across the ~60 sources sharing this
 * process, so any test that called the setter first would make a later
 * "default" assertion vacuous. The default, the fact that the flag is read at
 * all rather than dead, and the OFF/ON identity of every pre-existing lane id
 * are measured per-process by scripts/probe_osi_logical_lane_size.py, which
 * runs each fixture in a fresh interpreter with the variable set and unset.
 *
 * Design: GT_esmini/docs/osi/logical_lane_and_route_design.md sections 2-1, 2-2, 3, 7, 9
 * Knowledge graph: spine-work:osi-logical-lane
 */
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "CommonMini.hpp"
#include "RoadManager.hpp"
#include "gt_esmini/osi/GT_OsiLogicalLane.hpp"
#include "osi_groundtruth.pb.h"

namespace
{
std::string LlRepoRoot()
{
#ifdef GT_ODR_REPO_ROOT
    return std::string(GT_ODR_REPO_ROOT);
#else
    return std::string();
#endif
}

std::string Xodr(const std::string& name)
{
    return LlRepoRoot() + "/resources/xodr/" + name;
}

// Load a fixture and run the post-pass over it. Returns false if the xodr did
// not load, so callers can ASSERT on it rather than assert inside a helper.
bool BuildFor(const std::string& xodr_path, osi3::GroundTruth* gt, gt_esmini::osi::LogicalLaneIndex* index)
{
    if (!roadmanager::Position::GetOpenDrive()->LoadOpenDriveFile(xodr_path.c_str(), true))
    {
        return false;
    }
    gt_esmini::osi::BuildOsiLogicalLanesInto(roadmanager::Position::GetOpenDrive(), gt, index);
    return true;
}

// Number of OpenDRIVE lanes excluding centre lanes -- the population the logical
// layer must be 1:1 with, junction connecting roads included.
unsigned CountNonCentreLanes(roadmanager::OpenDrive* od)
{
    unsigned n = 0;
    for (unsigned i = 0; i < od->GetNumOfRoads(); i++)
    {
        roadmanager::Road* road = od->GetRoadByIdx(i);
        for (unsigned j = 0; j < road->GetNumberOfLaneSections(); j++)
        {
            roadmanager::LaneSection* lsec = road->GetLaneSectionByIdx(j);
            for (unsigned k = 0; k < lsec->GetNumberOfLanes(); k++)
            {
                if (!lsec->GetLaneByIdx(k)->IsCenter())
                {
                    n++;
                }
            }
        }
    }
    return n;
}

constexpr int kNormal      = static_cast<int>(osi3::LogicalLane_Type_TYPE_NORMAL);
constexpr int kSidewalk    = static_cast<int>(osi3::LogicalLane_Type_TYPE_SIDEWALK);
constexpr int kMedian      = static_cast<int>(osi3::LogicalLane_Type_TYPE_MEDIAN);
constexpr int kCurb        = static_cast<int>(osi3::LogicalLane_Type_TYPE_CURB);
constexpr int kBorder      = static_cast<int>(osi3::LogicalLane_Type_TYPE_BORDER);
constexpr int kOther       = static_cast<int>(osi3::LogicalLane_Type_TYPE_OTHER);
constexpr int kIncreasingS = static_cast<int>(osi3::LogicalLane_MoveDirection_MOVE_DIRECTION_INCREASING_S);
constexpr int kDecreasingS = static_cast<int>(osi3::LogicalLane_MoveDirection_MOVE_DIRECTION_DECREASING_S);
constexpr int kBothAllowed = static_cast<int>(osi3::LogicalLane_MoveDirection_MOVE_DIRECTION_BOTH_ALLOWED);

using LT = roadmanager::Lane::LaneType;
}  // namespace

// ---------------------------------------------------------------------------
// flag contract (S0)
// ---------------------------------------------------------------------------

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

// The wrapper is the flag's only consumer, so OFF must reach neither the
// GroundTruth nor the index. It also has to survive a null static_gt, which is
// exactly the state a unit-test process is in.
TEST(OsiLogicalLane, WrapperIsANoOpWhenOffAndWithoutAReporter)
{
    gt_esmini::osi::SetUseOsiLogicalLane(false);
    gt_esmini::osi::BuildOsiLogicalLanes(roadmanager::Position::GetOpenDrive());
    EXPECT_TRUE(gt_esmini::osi::GetLogicalLaneIndex().empty()) << "OFF: nothing indexed";

    gt_esmini::osi::SetUseOsiLogicalLane(true);
    gt_esmini::osi::BuildOsiLogicalLanes(nullptr);
    EXPECT_TRUE(gt_esmini::osi::GetLogicalLaneIndex().empty()) << "null OpenDrive: nothing indexed";
    // ON but no OSIReporter in this process -> static_gt is null; the guard must
    // hold rather than crash (design 8-0 hand-off 3).
    gt_esmini::osi::BuildOsiLogicalLanes(roadmanager::Position::GetOpenDrive());
    EXPECT_TRUE(gt_esmini::osi::GetLogicalLaneIndex().empty()) << "no static GroundTruth: nothing indexed";

    gt_esmini::osi::SetUseOsiLogicalLane(false);
}

// ---------------------------------------------------------------------------
// S1 pre-check: do connecting-road lanes actually carry RoadManager OSI points?
//
// The S0 report first claimed there were "no existing OSI points to reuse" for
// junction lanes. That was a face mix-up: what is missing is the OSI MESSAGE
// (UpdateOSIRoadLane skips IsOSIIntersection() lanes and UpdateOSIIntersection
// fuses the junction into one TYPE_INTERSECTION lane), not the RoadManager
// geometry. The correction was made by reading OpenDrive::SetLaneOSIPoints,
// which has no junction skip -- this test is the measurement the correction
// rests on, because S1 (reference lines) and S3 (boundaries) may treat
// connecting roads exactly like ordinary roads only if it holds.
//
// A zero here would mean the whole approach has to change, so it is asserted,
// not just printed.
// ---------------------------------------------------------------------------
TEST(OsiLogicalLane, ConnectingRoadLanesCarryRoadManagerOsiPoints)
{
    const std::string xodr = Xodr("multi_intersections.xodr");
    ASSERT_TRUE(roadmanager::Position::GetOpenDrive()->LoadOpenDriveFile(xodr.c_str(), true)) << xodr;
    roadmanager::OpenDrive* od = roadmanager::Position::GetOpenDrive();

    unsigned junction_lanes = 0, junction_lanes_with_points = 0, junction_lane_points = 0;
    unsigned plain_lanes = 0, plain_lanes_with_points = 0;
    unsigned junction_sections = 0, junction_sections_with_refline = 0, junction_refline_points = 0;
    unsigned plain_sections = 0, plain_sections_with_refline = 0;

    for (unsigned i = 0; i < od->GetNumOfRoads(); i++)
    {
        roadmanager::Road* road = od->GetRoadByIdx(i);
        for (unsigned j = 0; j < road->GetNumberOfLaneSections(); j++)
        {
            roadmanager::LaneSection* lsec       = road->GetLaneSectionByIdx(j);
            bool                      in_osi_jct = false;
            for (unsigned k = 0; k < lsec->GetNumberOfLanes(); k++)
            {
                roadmanager::Lane* lane = lsec->GetLaneByIdx(k);
                if (lane->IsCenter())
                {
                    continue;
                }
                const unsigned n = lane->GetOSIPoints()->GetNumOfOSIPoints();
                if (lane->IsOSIIntersection())
                {
                    in_osi_jct = true;
                    junction_lanes++;
                    junction_lane_points += n;
                    if (n > 0)
                    {
                        junction_lanes_with_points++;
                    }
                }
                else
                {
                    plain_lanes++;
                    if (n > 0)
                    {
                        plain_lanes_with_points++;
                    }
                }
            }
            const unsigned nref = lsec->GetRefLineOSIPoints().GetNumOfOSIPoints();
            if (in_osi_jct)
            {
                junction_sections++;
                junction_refline_points += nref;
                if (nref > 0)
                {
                    junction_sections_with_refline++;
                }
            }
            else
            {
                plain_sections++;
                if (nref > 0)
                {
                    plain_sections_with_refline++;
                }
            }
        }
    }

    std::cout << "[S1-precheck] multi_intersections"
              << " junction_lanes=" << junction_lanes << " with_points=" << junction_lanes_with_points
              << " total_points=" << junction_lane_points << " | plain_lanes=" << plain_lanes
              << " with_points=" << plain_lanes_with_points << " | junction_sections=" << junction_sections
              << " with_refline=" << junction_sections_with_refline << " refline_points=" << junction_refline_points
              << " | plain_sections=" << plain_sections << " with_refline=" << plain_sections_with_refline << std::endl;

    ASSERT_GT(junction_lanes, 0u) << "fixture has no OSI-intersection lanes -- wrong xodr";
    EXPECT_EQ(junction_lanes_with_points, junction_lanes) << "some connecting-road lane has no RoadManager OSI points";
    EXPECT_EQ(junction_sections_with_refline, junction_sections) << "some connecting-road lane section has no reference-line OSI points";
    EXPECT_EQ(plain_lanes_with_points, plain_lanes);
    EXPECT_EQ(plain_sections_with_refline, plain_sections);
}

// ---------------------------------------------------------------------------
// 1:1 with OpenDRIVE lanes, junction connecting roads included
// ---------------------------------------------------------------------------

TEST(OsiLogicalLane, OneLogicalLanePerOpenDriveLaneIncludingConnectingRoads)
{
    osi3::GroundTruth               gt;
    gt_esmini::osi::LogicalLaneIndex index;
    ASSERT_TRUE(BuildFor(Xodr("multi_intersections.xodr"), &gt, &index));
    roadmanager::OpenDrive* od = roadmanager::Position::GetOpenDrive();

    const unsigned expected = CountNonCentreLanes(od);
    std::cout << "[S1] multi_intersections logical_lane=" << gt.logical_lane_size() << " reference_line=" << gt.reference_line_size()
              << " expected_lanes=" << expected << " roads=" << od->GetNumOfRoads() << std::endl;

    EXPECT_EQ(static_cast<unsigned>(gt.logical_lane_size()), expected);
    EXPECT_EQ(index.size(), expected) << "the index must cover every emitted lane -- it is S2.5/S4's only entry point";
    EXPECT_EQ(static_cast<unsigned>(gt.reference_line_size()), od->GetNumOfRoads()) << "one reference line per road (design 2-1)";
    // S3 emits boundaries, and no orphans: every one of them is named by at least one
    // logical lane. An orphan is not a protocol error -- it just sits there costing
    // bytes -- so nothing else would ever notice it.
    EXPECT_GT(gt.logical_lane_boundary_size(), 0);
    std::set<std::uint64_t> referenced;
    for (const auto& ll : gt.logical_lane())
    {
        for (const auto& b : ll.left_boundary_id())
        {
            referenced.insert(b.value());
        }
        for (const auto& b : ll.right_boundary_id())
        {
            referenced.insert(b.value());
        }
    }
    EXPECT_EQ(referenced.size(), static_cast<std::size_t>(gt.logical_lane_boundary_size()))
        << "every emitted boundary must be referenced by some lane";

    // Every id unique, and every reference_line_id resolvable.
    std::set<std::uint64_t> ll_ids, rl_ids;
    for (const auto& rl : gt.reference_line())
    {
        EXPECT_TRUE(rl_ids.insert(rl.id().value()).second) << "duplicate reference line id " << rl.id().value();
        EXPECT_EQ(rl.type(), osi3::ReferenceLine_Type_TYPE_POLYLINE_WITH_T_AXIS);
        EXPECT_GE(rl.poly_line_size(), 2) << "OSI requires at least two points per reference line";
    }
    for (const auto& ll : gt.logical_lane())
    {
        EXPECT_TRUE(ll_ids.insert(ll.id().value()).second) << "duplicate logical lane id " << ll.id().value();
        EXPECT_EQ(rl_ids.count(ll.reference_line_id().value()), 1u) << "logical lane " << ll.id().value() << " references a missing reference line";
        EXPECT_GT(ll.end_s(), ll.start_s()) << "proto requires end_s > start_s";
        EXPECT_NE(static_cast<int>(ll.type()), static_cast<int>(osi3::LogicalLane_Type_TYPE_UNKNOWN))
            << "TYPE_UNKNOWN must not be used in ground truth";
        ASSERT_EQ(ll.source_reference_size(), 1);
        ASSERT_EQ(ll.source_reference(0).identifier_size(), 3);
        EXPECT_EQ(ll.source_reference(0).type(), "net.asam.opendrive");
        EXPECT_EQ(ll.source_reference(0).identifier(0).rfind("road_id:", 0), 0u);
        EXPECT_EQ(ll.source_reference(0).identifier(2).rfind("lane_id:", 0), 0u);
    }

    // Reference-line s must be strictly increasing (the lane-section seam is the
    // one place raw concatenation would break it) and never advance by less than
    // the 2D distance between the points, because s IS the OpenDRIVE arc length.
    for (const auto& rl : gt.reference_line())
    {
        for (int i = 1; i < rl.poly_line_size(); i++)
        {
            const auto& a = rl.poly_line(i - 1);
            const auto& b = rl.poly_line(i);
            ASSERT_GT(b.s_position(), a.s_position()) << "reference line " << rl.id().value() << " point " << i;
            const double dx   = b.world_position().x() - a.world_position().x();
            const double dy   = b.world_position().y() - a.world_position().y();
            const double dist = std::sqrt(dx * dx + dy * dy);
            EXPECT_GE(b.s_position() - a.s_position(), dist - 1e-6) << "reference line " << rl.id().value() << " point " << i;
        }
    }
}

// The headline of S1: junction lanes are individually visible for the first
// time. Today the physical layer fuses a whole junction into one
// TYPE_INTERSECTION lane, so the logical count must EXCEED the physical one by
// exactly the connecting-road lanes.
TEST(OsiLogicalLane, ConnectingRoadLanesAreEmittedIndividually)
{
    osi3::GroundTruth               gt;
    gt_esmini::osi::LogicalLaneIndex index;
    ASSERT_TRUE(BuildFor(Xodr("multi_intersections.xodr"), &gt, &index));
    roadmanager::OpenDrive* od = roadmanager::Position::GetOpenDrive();

    unsigned junction_lanes = 0;
    for (unsigned i = 0; i < od->GetNumOfRoads(); i++)
    {
        roadmanager::Road* road = od->GetRoadByIdx(i);
        for (unsigned j = 0; j < road->GetNumberOfLaneSections(); j++)
        {
            roadmanager::LaneSection* lsec = road->GetLaneSectionByIdx(j);
            for (unsigned k = 0; k < lsec->GetNumberOfLanes(); k++)
            {
                roadmanager::Lane* lane = lsec->GetLaneByIdx(k);
                if (!lane->IsCenter() && lane->IsOSIIntersection())
                {
                    junction_lanes++;
                    // Each of them is in the index under its own key.
                    EXPECT_EQ(index.count(gt_esmini::osi::LogicalLaneKey{road->GetId(), j, lane->GetId()}), 1u);
                }
            }
        }
    }
    ASSERT_GT(junction_lanes, 0u);

    // Every connecting-road logical lane points at the junction's fused physical
    // lane, never at its own RM global id (which names no osi3::Lane at all).
    // Several logical lanes sharing one physical lane is what the proto expects.
    std::map<std::uint64_t, unsigned> physical_fanout;
    for (const auto& ll : gt.logical_lane())
    {
        for (const auto& plr : ll.physical_lane_reference())
        {
            physical_fanout[plr.physical_lane_id().value()]++;
            EXPECT_GT(plr.end_s(), plr.start_s());
        }
    }
    unsigned shared_physical = 0;
    for (const auto& kv : physical_fanout)
    {
        if (kv.second > 1)
        {
            shared_physical++;
        }
    }
    std::cout << "[S1] multi_intersections connecting-road lanes=" << junction_lanes
              << " physical lanes referenced by >1 logical lane=" << shared_physical << std::endl;
    EXPECT_GT(shared_physical, 0u) << "no fused junction lane is shared -- physical_lane_reference is probably dangling";
}

// ---------------------------------------------------------------------------
// id discipline -- the most important invariant of the whole feature (design 3)
// ---------------------------------------------------------------------------

TEST(OsiLogicalLane, PostPassMovesNoExistingGlobalId)
{
    ASSERT_TRUE(roadmanager::Position::GetOpenDrive()->LoadOpenDriveFile(Xodr("multi_intersections.xodr").c_str(), true));
    roadmanager::OpenDrive* od = roadmanager::Position::GetOpenDrive();

    std::map<std::string, id_t> before;
    id_t                        max_existing = 0;
    for (unsigned i = 0; i < od->GetNumOfRoads(); i++)
    {
        roadmanager::Road* road = od->GetRoadByIdx(i);
        for (unsigned j = 0; j < road->GetNumberOfLaneSections(); j++)
        {
            roadmanager::LaneSection* lsec = road->GetLaneSectionByIdx(j);
            for (unsigned k = 0; k < lsec->GetNumberOfLanes(); k++)
            {
                roadmanager::Lane* lane = lsec->GetLaneByIdx(k);
                const std::string  key  = std::to_string(road->GetId()) + "/" + std::to_string(j) + "/" + std::to_string(lane->GetId());
                before[key]             = lane->GetGlobalId();
                if (lane->GetGlobalId() != ID_UNDEFINED)
                {
                    max_existing = std::max(max_existing, lane->GetGlobalId());
                }
            }
        }
    }
    ASSERT_FALSE(before.empty());

    osi3::GroundTruth               gt;
    gt_esmini::osi::LogicalLaneIndex index;
    gt_esmini::osi::BuildOsiLogicalLanesInto(od, &gt, &index);

    for (unsigned i = 0; i < od->GetNumOfRoads(); i++)
    {
        roadmanager::Road* road = od->GetRoadByIdx(i);
        for (unsigned j = 0; j < road->GetNumberOfLaneSections(); j++)
        {
            roadmanager::LaneSection* lsec = road->GetLaneSectionByIdx(j);
            for (unsigned k = 0; k < lsec->GetNumberOfLanes(); k++)
            {
                roadmanager::Lane* lane = lsec->GetLaneByIdx(k);
                const std::string  key  = std::to_string(road->GetId()) + "/" + std::to_string(j) + "/" + std::to_string(lane->GetId());
                EXPECT_EQ(before[key], lane->GetGlobalId()) << "lane " << key << " was renumbered by the post-pass";
            }
        }
    }

    // ... and every id the pass drew is a FRESH one from the shared counter, not
    // a reuse of something already handed out. Drawing ids during OpenDRIVE load
    // instead is what would renumber the lanes above, so this is the mechanism,
    // not just the symptom.
    for (const auto& rl : gt.reference_line())
    {
        EXPECT_GT(rl.id().value(), max_existing);
    }
    for (const auto& ll : gt.logical_lane())
    {
        EXPECT_GT(ll.id().value(), max_existing);
    }
}

// ---------------------------------------------------------------------------
// t_axis_yaw polarity (design 2-1, verification 9.1)
// ---------------------------------------------------------------------------

// The normal is "local +Y rotated by the point's pose", and +Y is the direction
// of increasing t. That is easy to state and easy to get backwards, and a flipped
// sign still draws a perfectly plausible road, so it is checked against world
// coordinates: step from a reference-line point along t_axis_yaw and the result
// must land on the POSITIVE-t side, not the negative one.
TEST(OsiLogicalLane, TAxisYawPointsTowardsIncreasingT)
{
    osi3::GroundTruth               gt;
    gt_esmini::osi::LogicalLaneIndex index;
    ASSERT_TRUE(BuildFor(Xodr("e6mini.xodr"), &gt, &index));
    roadmanager::OpenDrive* od = roadmanager::Position::GetOpenDrive();
    ASSERT_GT(gt.reference_line_size(), 0);

    const double            kT = 3.0;  // metres to the positive-t side
    roadmanager::Position   pos;
    unsigned                checked = 0, agree = 0, agree_if_flipped = 0;

    for (int r = 0; r < gt.reference_line_size(); r++)
    {
        roadmanager::Road* road = od->GetRoadByIdx(static_cast<unsigned>(r));
        const auto&        rl   = gt.reference_line(r);
        for (int i = 0; i < rl.poly_line_size(); i++)
        {
            const auto& p = rl.poly_line(i);
            // Where does OpenDRIVE itself put (s, +t)?
            if (pos.SetTrackPos(road->GetId(), p.s_position(), kT) != roadmanager::Position::ReturnCode::OK)
            {
                continue;
            }
            const double want_x = pos.GetX(), want_y = pos.GetY();
            // Where does t_axis_yaw put it?
            const double got_x     = p.world_position().x() + kT * std::cos(p.t_axis_yaw());
            const double got_y     = p.world_position().y() + kT * std::sin(p.t_axis_yaw());
            const double flip_x    = p.world_position().x() - kT * std::cos(p.t_axis_yaw());
            const double flip_y    = p.world_position().y() - kT * std::sin(p.t_axis_yaw());
            const double err       = std::hypot(got_x - want_x, got_y - want_y);
            const double err_flip  = std::hypot(flip_x - want_x, flip_y - want_y);
            checked++;
            if (err < 0.05)
            {
                agree++;
            }
            if (err_flip < 0.05)
            {
                agree_if_flipped++;
            }
        }
    }

    std::cout << "[S1] t_axis_yaw checked=" << checked << " agree=" << agree << " agree_if_sign_flipped=" << agree_if_flipped << std::endl;
    ASSERT_GT(checked, 0u);
    EXPECT_EQ(agree, checked) << "t_axis_yaw does not point towards increasing t";
    // The negative control: with the sign reversed the very same comparison must
    // FAIL everywhere. Without this the test would also pass on a road whose
    // reference line happens to be straight and symmetric about t.
    EXPECT_EQ(agree_if_flipped, 0u) << "the check cannot tell +t from -t -- it proves nothing";
}

// ---------------------------------------------------------------------------
// move_direction, all four quadrants (design 2-2-2, verification 9.2)
// ---------------------------------------------------------------------------

TEST(OsiLogicalLane, MoveDirectionFlipsAcrossTrafficHandAndLaneSide)
{
    const int driving = static_cast<int>(LT::LANE_TYPE_DRIVING);

    // RHT: traffic that follows +s is on the negative lanes.
    EXPECT_EQ(gt_esmini::osi::MapMoveDirection(driving, -1, /*rht=*/true), kIncreasingS);
    EXPECT_EQ(gt_esmini::osi::MapMoveDirection(driving, +1, /*rht=*/true), kDecreasingS);
    // LHT: the two swap. Same lane ids, opposite answers.
    EXPECT_EQ(gt_esmini::osi::MapMoveDirection(driving, -1, /*rht=*/false), kDecreasingS);
    EXPECT_EQ(gt_esmini::osi::MapMoveDirection(driving, +1, /*rht=*/false), kIncreasingS);

    // Bidirectional and non-driving lanes ignore both axes.
    for (bool rht : {true, false})
    {
        for (int id : {-1, 1})
        {
            EXPECT_EQ(gt_esmini::osi::MapMoveDirection(static_cast<int>(LT::LANE_TYPE_BIDIRECTIONAL), id, rht), kBothAllowed);
            EXPECT_EQ(gt_esmini::osi::MapMoveDirection(static_cast<int>(LT::LANE_TYPE_SIDEWALK), id, rht), kBothAllowed);
            EXPECT_EQ(gt_esmini::osi::MapMoveDirection(static_cast<int>(LT::LANE_TYPE_MEDIAN), id, rht), kBothAllowed);
        }
    }
    // Ramps are driving-like, so they follow the same rule as a plain lane.
    EXPECT_EQ(gt_esmini::osi::MapMoveDirection(static_cast<int>(LT::LANE_TYPE_ON_RAMP), -2, true), kIncreasingS);
    EXPECT_EQ(gt_esmini::osi::MapMoveDirection(static_cast<int>(LT::LANE_TYPE_ON_RAMP), 2, true), kDecreasingS);
}

// The four types the physical Lane.Classification.Subtype enum cannot express are
// the reason the logical layer carries more information than osi_lane does.
TEST(OsiLogicalLane, LaneTypeMappingKeepsWhatTheSubtypeEnumDrops)
{
    EXPECT_EQ(gt_esmini::osi::MapLaneTypeToLogicalLaneType(static_cast<int>(LT::LANE_TYPE_DRIVING)), kNormal);
    EXPECT_EQ(gt_esmini::osi::MapLaneTypeToLogicalLaneType(static_cast<int>(LT::LANE_TYPE_BIDIRECTIONAL)), kNormal);
    EXPECT_EQ(gt_esmini::osi::MapLaneTypeToLogicalLaneType(static_cast<int>(LT::LANE_TYPE_SIDEWALK)), kSidewalk);
    EXPECT_EQ(gt_esmini::osi::MapLaneTypeToLogicalLaneType(static_cast<int>(LT::LANE_TYPE_BORDER)), kBorder);
    // osi_lane reports MEDIAN as SUBTYPE_OTHER and CURB as SUBTYPE_BORDER today.
    EXPECT_EQ(gt_esmini::osi::MapLaneTypeToLogicalLaneType(static_cast<int>(LT::LANE_TYPE_MEDIAN)), kMedian);
    EXPECT_EQ(gt_esmini::osi::MapLaneTypeToLogicalLaneType(static_cast<int>(LT::LANE_TYPE_CURB)), kCurb);
    EXPECT_NE(kMedian, kOther);
    EXPECT_NE(kCurb, kBorder);
    // Unmapped OpenDRIVE types fall to TYPE_OTHER, never to the forbidden
    // TYPE_UNKNOWN.
    EXPECT_EQ(gt_esmini::osi::MapLaneTypeToLogicalLaneType(static_cast<int>(LT::LANE_TYPE_ROADWORKS)), kOther);
    EXPECT_EQ(gt_esmini::osi::MapLaneTypeToLogicalLaneType(static_cast<int>(LT::LANE_TYPE_NONE)), kOther);
}

// ---------------------------------------------------------------------------
// non-driving lanes are emitted too (the spec forbids gaps in the road surface)
// ---------------------------------------------------------------------------

TEST(OsiLogicalLane, NonDrivingLanesAreEmittedAndTypedFromTheXodr)
{
    osi3::GroundTruth               gt;
    gt_esmini::osi::LogicalLaneIndex index;
    ASSERT_TRUE(BuildFor(Xodr("multi_intersections.xodr"), &gt, &index));
    roadmanager::OpenDrive* od = roadmanager::Position::GetOpenDrive();

    // Count, per OpenDRIVE lane type, what the road network has and what came out.
    std::map<int, unsigned> want, got;
    for (unsigned i = 0; i < od->GetNumOfRoads(); i++)
    {
        roadmanager::Road* road = od->GetRoadByIdx(i);
        for (unsigned j = 0; j < road->GetNumberOfLaneSections(); j++)
        {
            roadmanager::LaneSection* lsec = road->GetLaneSectionByIdx(j);
            for (unsigned k = 0; k < lsec->GetNumberOfLanes(); k++)
            {
                roadmanager::Lane* lane = lsec->GetLaneByIdx(k);
                if (!lane->IsCenter())
                {
                    want[gt_esmini::osi::MapLaneTypeToLogicalLaneType(static_cast<int>(lane->GetLaneType()))]++;
                }
            }
        }
    }
    for (const auto& ll : gt.logical_lane())
    {
        got[static_cast<int>(ll.type())]++;
    }

    std::cout << "[S1] logical lane types:";
    for (const auto& kv : got)
    {
        std::cout << " " << kv.first << "=" << kv.second;
    }
    std::cout << std::endl;

    EXPECT_EQ(want, got);
    unsigned non_driving = 0;
    for (const auto& kv : got)
    {
        if (kv.first != kNormal)
        {
            non_driving += kv.second;
        }
    }
    EXPECT_GT(non_driving, 0u) << "fixture has only driving lanes -- it cannot show that the surface has no gaps";

    // physical_lane_reference is omitted exactly for the four types with no
    // Lane.Classification.Subtype counterpart, and present for everything else.
    for (const auto& ll : gt.logical_lane())
    {
        const bool no_counterpart = static_cast<int>(ll.type()) == kMedian || static_cast<int>(ll.type()) == kCurb ||
                                    static_cast<int>(ll.type()) == static_cast<int>(osi3::LogicalLane_Type_TYPE_RAIL) ||
                                    static_cast<int>(ll.type()) == static_cast<int>(osi3::LogicalLane_Type_TYPE_TRAM);
        if (no_counterpart)
        {
            EXPECT_EQ(ll.physical_lane_reference_size(), 0) << "logical lane " << ll.id().value() << " type " << ll.type();
        }
        else
        {
            EXPECT_EQ(ll.physical_lane_reference_size(), 1) << "logical lane " << ll.id().value() << " type " << ll.type();
        }
    }
}

// ---------------------------------------------------------------------------
// speed limit unit (design 2-2; OSI has no m/s member for velocities)
// ---------------------------------------------------------------------------

// OSI's TrafficSignValue::Unit offers only km/h and mph for velocities, while
// RoadManager normalises every authored <speed> into m/s. The fixture below
// authors "50 km/h" verbatim, so the number that has to come back out is 50 --
// an unconverted emit would read 13.9 and still look like a speed limit.
TEST(OsiLogicalLane, SpeedLimitIsEmittedInKilometresPerHour)
{
    osi3::GroundTruth                gt;
    gt_esmini::osi::LogicalLaneIndex index;
    // virtual_junction_23.xodr is the only stock fixture with an authored road
    // <type><speed max="50" unit="km/h"/>.
    ASSERT_TRUE(BuildFor(Xodr("virtual_junction_23.xodr"), &gt, &index));
    roadmanager::OpenDrive* od       = roadmanager::Position::GetOpenDrive();
    const double            speed_ms = od->GetRoadByIdx(0)->GetSpeedByS(0.0);
    ASSERT_GT(speed_ms, SMALL_NUMBER) << "fixture no longer authors a road speed limit -- pick another one";
    EXPECT_NEAR(speed_ms, 50.0 / 3.6, 1e-6) << "RoadManager is expected to hand out m/s";

    unsigned with_rule = 0;
    for (const auto& ll : gt.logical_lane())
    {
        for (const auto& tr : ll.traffic_rule())
        {
            with_rule++;
            EXPECT_EQ(tr.traffic_rule_type(), osi3::LogicalLane_TrafficRule_TrafficRuleType_TRAFFIC_RULE_TYPE_SPEED_LIMIT);
            EXPECT_EQ(tr.speed_limit().speed_limit_value().value_unit(), osi3::TrafficSignValue_Unit_UNIT_KILOMETER_PER_HOUR);
            // The authored number, not the internal one.
            EXPECT_NEAR(tr.speed_limit().speed_limit_value().value(), 50.0, 1e-6)
                << "emitted " << tr.speed_limit().speed_limit_value().value() << " under a km/h tag; m/s is " << speed_ms;
            // Validity runs in the direction of travel.
            if (tr.has_traffic_rule_validity())
            {
                if (ll.move_direction() == osi3::LogicalLane_MoveDirection_MOVE_DIRECTION_INCREASING_S)
                {
                    EXPECT_LT(tr.traffic_rule_validity().start_s(), tr.traffic_rule_validity().end_s());
                }
                else
                {
                    EXPECT_GT(tr.traffic_rule_validity().start_s(), tr.traffic_rule_validity().end_s());
                }
            }
        }
        // Nothing drives on a sidewalk or a median, so a road speed limit there
        // would be noise.
        if (static_cast<int>(ll.type()) == kSidewalk || static_cast<int>(ll.type()) == kMedian)
        {
            EXPECT_EQ(ll.traffic_rule_size(), 0);
        }
    }
    std::cout << "[S1] virtual_junction_23 speed_limit m/s=" << speed_ms << " lanes with a traffic rule=" << with_rule << std::endl;
    EXPECT_GT(with_rule, 0u);
}

// ---------------------------------------------------------------------------
// the index is the only coupling to S2.5 / S4
// ---------------------------------------------------------------------------

TEST(OsiLogicalLane, IndexResolvesEveryEmittedLaneAndNothingElse)
{
    osi3::GroundTruth               gt;
    gt_esmini::osi::LogicalLaneIndex index;
    ASSERT_TRUE(BuildFor(Xodr("fabriksgatan.xodr"), &gt, &index));
    roadmanager::OpenDrive* od = roadmanager::Position::GetOpenDrive();

    std::set<std::uint64_t> emitted;
    for (const auto& ll : gt.logical_lane())
    {
        emitted.insert(ll.id().value());
    }
    ASSERT_EQ(emitted.size(), static_cast<size_t>(gt.logical_lane_size()));

    std::set<std::uint64_t> indexed;
    for (const auto& kv : index)
    {
        EXPECT_EQ(emitted.count(kv.second), 1u) << "index points at a logical lane that was not emitted";
        indexed.insert(kv.second);
        // The centre lane has no logical lane, so it must never be a key.
        EXPECT_NE(std::get<2>(kv.first), 0);
    }
    EXPECT_EQ(indexed.size(), emitted.size()) << "some emitted lane is unreachable through the index";

    // Round-trip a concrete lane through the key the HVD route builder will use.
    roadmanager::Road*        road = od->GetRoadByIdx(0);
    roadmanager::LaneSection* lsec = road->GetLaneSectionByIdx(0);
    for (unsigned k = 0; k < lsec->GetNumberOfLanes(); k++)
    {
        roadmanager::Lane* lane = lsec->GetLaneByIdx(k);
        const auto         it   = index.find(gt_esmini::osi::LogicalLaneKey{road->GetId(), 0u, lane->GetId()});
        if (lane->IsCenter())
        {
            EXPECT_EQ(it, index.end());
        }
        else
        {
            ASSERT_NE(it, index.end()) << "road " << road->GetId() << " lane " << lane->GetId();
        }
    }
}

// A rebuild must not accumulate: the index is rebuilt from scratch every static
// ground-truth build, and a stale entry would silently point S4 at a lane id
// from a previous scenario.
TEST(OsiLogicalLane, RebuildReplacesTheIndexRatherThanAppending)
{
    osi3::GroundTruth               gt_a;
    gt_esmini::osi::LogicalLaneIndex index;
    ASSERT_TRUE(BuildFor(Xodr("multi_intersections.xodr"), &gt_a, &index));
    const size_t big = index.size();

    osi3::GroundTruth gt_b;
    ASSERT_TRUE(BuildFor(Xodr("e6mini.xodr"), &gt_b, &index));
    EXPECT_EQ(index.size(), static_cast<size_t>(gt_b.logical_lane_size()));
    EXPECT_LT(index.size(), big) << "the smaller network left the larger one's keys behind";
}

// ---------------------------------------------------------------------------
// lane-section seams (design 2-1)
// ---------------------------------------------------------------------------

// The reference line is one polyline per ROAD, concatenated from the per-lane-section
// point sets. RoadManager ends a section at (next section s - SMALL_NUMBER/2), so
// raw concatenation puts two points 5e-7 m apart at every internal seam -- legal
// arithmetic, illegal OSI (s must increase STRICTLY, and the duplicate carries no
// shape). The fixture is chosen for having multi-section roads: on a network where
// every road has exactly one section the collapse never runs and a monotonicity
// assertion alone would prove nothing.
TEST(OsiLogicalLane, LaneSectionSeamsCollapseToASinglePoint)
{
    osi3::GroundTruth                gt;
    gt_esmini::osi::LogicalLaneIndex index;
    ASSERT_TRUE(BuildFor(Xodr("soderleden.xodr"), &gt, &index));
    roadmanager::OpenDrive* od = roadmanager::Position::GetOpenDrive();
    ASSERT_EQ(static_cast<unsigned>(gt.reference_line_size()), od->GetNumOfRoads());

    unsigned raw_points = 0, emitted_points = 0, sections = 0, internal_seams = 0;
    for (unsigned i = 0; i < od->GetNumOfRoads(); i++)
    {
        roadmanager::Road* road = od->GetRoadByIdx(i);
        sections += road->GetNumberOfLaneSections();
        internal_seams += road->GetNumberOfLaneSections() - 1;
        for (unsigned j = 0; j < road->GetNumberOfLaneSections(); j++)
        {
            raw_points += road->GetLaneSectionByIdx(j)->GetRefLineOSIPoints().GetNumOfOSIPoints();
        }
        emitted_points += static_cast<unsigned>(gt.reference_line(static_cast<int>(i)).poly_line_size());
    }

    std::cout << "[S1] soderleden roads=" << od->GetNumOfRoads() << " sections=" << sections << " internal_seams=" << internal_seams
              << " raw_refline_points=" << raw_points << " emitted=" << emitted_points << std::endl;

    ASSERT_GT(internal_seams, 0u) << "fixture has no multi-section road -- it cannot exercise the seam collapse";
    EXPECT_EQ(raw_points - emitted_points, internal_seams) << "exactly one duplicate point per internal lane-section seam";

    for (const auto& rl : gt.reference_line())
    {
        for (int i = 1; i < rl.poly_line_size(); i++)
        {
            ASSERT_GT(rl.poly_line(i).s_position(), rl.poly_line(i - 1).s_position()) << "reference line " << rl.id().value() << " point " << i;
        }
    }
}

// ---------------------------------------------------------------------------
// S3 -- LogicalLaneBoundary (design 2-3)
//
// The heavy measurement -- deviation from the ideal edge, sampled through a second
// DLL at s values the construction never used -- lives in
// scripts/probe_osi_logical_lane_boundary.py. What is pinned here is the structure a
// consumer joins on, which is cheap to check and expensive to notice when it breaks:
// a boundary list that does not cover its lane, or two lanes that describe one edge
// with two ids, both produce perfectly plausible geometry.
// ---------------------------------------------------------------------------

namespace
{
std::map<std::uint64_t, const osi3::LogicalLaneBoundary*> BoundaryById(const osi3::GroundTruth& gt)
{
    std::map<std::uint64_t, const osi3::LogicalLaneBoundary*> out;
    for (const auto& b : gt.logical_lane_boundary())
    {
        out[b.id().value()] = &b;
    }
    return out;
}

// (road, road_s, lane) of a logical lane, read back out of its source_reference --
// the same join the lane_map parser and the probe use.
struct LaneAddr
{
    int         road_id = -1;
    std::string road_s;
    int         lane_id = 0;

    bool operator<(const LaneAddr& o) const
    {
        return std::tie(road_id, road_s, lane_id) < std::tie(o.road_id, o.road_s, o.lane_id);
    }
};

LaneAddr AddrOf(const osi3::LogicalLane& ll)
{
    LaneAddr a;
    for (const auto& sr : ll.source_reference())
    {
        for (const auto& ident : sr.identifier())
        {
            if (ident.rfind("road_id:", 0) == 0)
            {
                a.road_id = std::stoi(ident.substr(8));
            }
            else if (ident.rfind("road_s:", 0) == 0)
            {
                a.road_s = ident.substr(7);
            }
            else if (ident.rfind("lane_id:", 0) == 0)
            {
                a.lane_id = std::stoi(ident.substr(8));
            }
        }
    }
    return a;
}
}  // namespace

// "The boundaries together must cover the whole length of the lane (the range
// [start_s, end_s]) without gap or overlap. The boundaries must be stored in
// ascending order." A build that emitted one boundary per lane side and stopped
// passes every other structural check here and leaves a hole wherever the road
// marking changes inside a lane section.
TEST(OsiLogicalLane, BoundariesCoverEveryLaneWithoutGap)
{
    for (const char* fixture : {"e6mini.xodr", "multi_intersections.xodr", "fabriksgatan.xodr"})
    {
        osi3::GroundTruth                gt;
        gt_esmini::osi::LogicalLaneIndex index;
        ASSERT_TRUE(BuildFor(Xodr(fixture), &gt, &index)) << fixture;
        ASSERT_GT(gt.logical_lane_boundary_size(), 0) << fixture;

        const auto by_id = BoundaryById(gt);
        unsigned   sides_checked = 0;
        for (const auto& ll : gt.logical_lane())
        {
            for (int side = 0; side < 2; side++)
            {
                const auto& ids = (side == 0) ? ll.left_boundary_id() : ll.right_boundary_id();
                ASSERT_GT(ids.size(), 0) << fixture << " lane " << ll.id().value() << " side " << side;
                double reached = ll.start_s();
                for (const auto& bid : ids)
                {
                    const auto it = by_id.find(bid.value());
                    ASSERT_NE(it, by_id.end()) << fixture << " dangling boundary id " << bid.value();
                    ASSERT_GE(it->second->boundary_line_size(), 2) << fixture;
                    EXPECT_NEAR(it->second->boundary_line(0).s_position(), reached, 1e-6) << fixture;
                    reached = it->second->boundary_line(it->second->boundary_line_size() - 1).s_position();
                }
                EXPECT_NEAR(reached, ll.end_s(), 1e-6) << fixture << " lane " << ll.id().value() << " side " << side;
                sides_checked++;
            }
        }
        EXPECT_GT(sides_checked, 20u) << fixture;
    }
}

// Two lanes either side of an edge must name the SAME boundary, because that
// identity is what lets a consumer recover lateral adjacency from boundaries alone
// (design 8-alpha) -- and it is the property a per-lane construction would fail
// while producing byte-identical geometry.
//
// The proto carves out exactly one exception: "if two lanes have different Z heights
// (e.g. a driving lane is beside a sidewalk ...) then these lanes cannot share a
// boundary". BOTH outcomes are asserted, on fixtures chosen for it -- e6mini has no
// <height> anywhere, fabriksgatan's sidewalks sit 0.12 m up -- because a build that
// never split and one that always split each satisfy one half.
TEST(OsiLogicalLane, AdjacentLanesShareOneBoundaryUnlessTheySitAtDifferentHeights)
{
    for (const char* fixture : {"e6mini.xodr", "fabriksgatan.xodr"})
    {
        osi3::GroundTruth                gt;
        gt_esmini::osi::LogicalLaneIndex index;
        ASSERT_TRUE(BuildFor(Xodr(fixture), &gt, &index)) << fixture;

        const auto                                       by_id = BoundaryById(gt);
        std::map<LaneAddr, const osi3::LogicalLane*>      by_addr;
        for (const auto& ll : gt.logical_lane())
        {
            by_addr[AddrOf(ll)] = &ll;
        }

        unsigned shared = 0, split = 0;
        for (const auto& [addr, ll] : by_addr)
        {
            const int inner = addr.lane_id - ((addr.lane_id < 0) ? -1 : 1);
            if (inner == 0)
            {
                continue;  // the centre edge has no "inner neighbour" to compare with
            }
            LaneAddr nb_addr = addr;
            nb_addr.lane_id  = inner;
            const auto nb_it = by_addr.find(nb_addr);
            if (nb_it == by_addr.end())
            {
                continue;
            }
            // My side facing the centre vs the neighbour's side facing outwards.
            const auto& mine   = (addr.lane_id > 0) ? ll->right_boundary_id() : ll->left_boundary_id();
            const auto& theirs = (addr.lane_id > 0) ? nb_it->second->left_boundary_id() : nb_it->second->right_boundary_id();
            ASSERT_EQ(mine.size(), theirs.size()) << fixture;

            bool identical = true;
            for (int i = 0; i < mine.size(); i++)
            {
                identical = identical && (mine[i].value() == theirs[i].value());
            }
            if (identical)
            {
                shared++;
                continue;
            }
            // Not shared: then it must be the height exception -- same line in XY,
            // apart in Z, point for point.
            split++;
            for (int i = 0; i < mine.size(); i++)
            {
                const auto a = by_id.at(mine[i].value());
                const auto b = by_id.at(theirs[i].value());
                ASSERT_EQ(a->boundary_line_size(), b->boundary_line_size()) << fixture;
                double max_dz = 0.0;
                for (int k = 0; k < a->boundary_line_size(); k++)
                {
                    const auto& pa = a->boundary_line(k).position();
                    const auto& pb = b->boundary_line(k).position();
                    EXPECT_NEAR(pa.x(), pb.x(), 1e-9) << fixture;
                    EXPECT_NEAR(pa.y(), pb.y(), 1e-9) << fixture;
                    max_dz = std::max(max_dz, std::fabs(pa.z() - pb.z()));
                }
                EXPECT_GT(max_dz, 0.0) << fixture << ": two boundaries on one edge that do not differ in z";
            }
        }
        if (std::string(fixture) == "e6mini.xodr")
        {
            EXPECT_GT(shared, 5u) << "e6mini has no <height> -- every edge must be shared";
            EXPECT_EQ(split, 0u);
        }
        else
        {
            EXPECT_GT(shared, 0u) << fixture;
            EXPECT_GT(split, 0u) << fixture << ": the 0.12 m sidewalks must force the split";
        }
    }
}

// PASSING_RULE_UNKNOWN "must not be used in ground truth", and an unmarked edge
// falls to PASSING_RULE_OTHER, which is the value the proto itself names for the
// boundary "between LogicalLanes of TYPE_NORMAL and TYPE_CURB". Also asserts that
// more than one rule actually occurs: a build that answered OTHER for everything
// would pass a "never UNKNOWN" check on its own.
TEST(OsiLogicalLane, PassingRuleIsNeverUnknownAndNotAlwaysTheSame)
{
    osi3::GroundTruth                gt;
    gt_esmini::osi::LogicalLaneIndex index;
    ASSERT_TRUE(BuildFor(Xodr("e6mini.xodr"), &gt, &index));
    ASSERT_GT(gt.logical_lane_boundary_size(), 0);

    std::map<int, unsigned> seen;
    for (const auto& b : gt.logical_lane_boundary())
    {
        EXPECT_NE(b.passing_rule(), osi3::LogicalLaneBoundary_PassingRule_PASSING_RULE_UNKNOWN);
        seen[static_cast<int>(b.passing_rule())]++;
    }
    EXPECT_GE(seen.size(), 2u) << "every boundary reported the same passing rule -- the road mark is not being read";
}

// The emitted polyline must stay within 5 cm of the ideal lane edge, measured at s
// values the construction never used. The probe does this through a second DLL over
// five networks; this is the cheap version that stays in the gate, on the fixture
// whose arc makes a coarse polyline show up immediately.
TEST(OsiLogicalLane, BoundaryStaysWithinFiveCentimetresOfTheLaneEdge)
{
    osi3::GroundTruth                gt;
    gt_esmini::osi::LogicalLaneIndex index;
    ASSERT_TRUE(BuildFor(Xodr("curve_r100.xodr"), &gt, &index));

    roadmanager::OpenDrive* odr  = roadmanager::Position::GetOpenDrive();
    roadmanager::Road*      road = odr->GetRoadByIdx(0);
    ASSERT_NE(road, nullptr);

    const auto by_id = BoundaryById(gt);
    unsigned   samples = 0;
    double     worst   = 0.0;
    for (const auto& ll : gt.logical_lane())
    {
        const LaneAddr addr = AddrOf(ll);
        // The lane's OUTER edge, which is the one its own width polynomial moves.
        const auto& outer = (addr.lane_id > 0) ? ll.left_boundary_id() : ll.right_boundary_id();
        for (const auto& bid : outer)
        {
            const osi3::LogicalLaneBoundary* b = by_id.at(bid.value());
            const double                     s0 = b->boundary_line(0).s_position();
            const double                     s1 = b->boundary_line(b->boundary_line_size() - 1).s_position();
            roadmanager::LaneSection*        lsec = road->GetLaneSectionByIdx(road->GetLaneSectionIdxByS(0.5 * (s0 + s1)));
            ASSERT_NE(lsec, nullptr);

            roadmanager::Position probe;
            for (int i = 1; i < 40; i++)
            {
                // Golden-ratio spacing: a sample lands on a construction point only by
                // accident, never by construction.
                const double frac = std::fmod(i * 0.6180339887498949, 1.0);
                const double sv   = s0 + frac * (s1 - s0);
                const double w    = lsec->GetWidth(sv, addr.lane_id);
                const double sign = (addr.lane_id < 0) ? -1.0 : 1.0;
                if (probe.SetLanePos(road->GetId(), addr.lane_id, sv, sign * 0.5 * w) != roadmanager::Position::ReturnCode::OK)
                {
                    continue;
                }
                double best = 1e9;
                for (int k = 1; k < b->boundary_line_size(); k++)
                {
                    const auto&  a  = b->boundary_line(k - 1).position();
                    const auto&  c  = b->boundary_line(k).position();
                    const double dx = c.x() - a.x(), dy = c.y() - a.y();
                    const double den = dx * dx + dy * dy;
                    double       u   = (den <= 0.0) ? 0.0 : ((probe.GetX() - a.x()) * dx + (probe.GetY() - a.y()) * dy) / den;
                    u                = std::max(0.0, std::min(1.0, u));
                    best = std::min(best, PointDistance2D(probe.GetX(), probe.GetY(), a.x() + u * dx, a.y() + u * dy));
                }
                worst = std::max(worst, best);
                samples++;
            }
        }
    }
    EXPECT_GT(samples, 100u);
    EXPECT_LE(worst, 0.05) << "max lateral deviation " << worst << " m exceeds the proto's 5 cm";
}

// ---------------------------------------------------------------------------
// S2.5 -- LogicalLaneAssignment (design 2-6-1)
//
// Exercised through ComputeLogicalLaneAssignments(), which takes the index
// explicitly for the same reason BuildOsiLogicalLanesInto() takes the
// GroundTruth: the protobuf emit wrapper reads the module-level index, which is
// only populated by a real static ground-truth build, so testing the wrapper
// alone would run past every line that matters (S1 hand-off).
// ---------------------------------------------------------------------------

namespace
{
using gt_esmini::osi::LogicalLaneAssignmentEntry;
using gt_esmini::osi::ObjectBox;

// The shipped car_white catalogue box, rounded: 5 m long, 2 m wide, centre 1.4 m
// ahead of the entity origin. Concrete numbers rather than a fixture lookup, so
// the arithmetic in each expectation below can be read off the page.
constexpr ObjectBox kCarBox{5.0, 2.0, 1.4, 0.0};

// Position::SetLanePos returns an enum class, so the usual ", 0)" does not compile.
constexpr roadmanager::Position::ReturnCode kPosOk = roadmanager::Position::ReturnCode::OK;

std::vector<LogicalLaneAssignmentEntry> Assign(const roadmanager::Position&            pos,
                                               const gt_esmini::osi::LogicalLaneIndex& index,
                                               const ObjectBox&                        box = kCarBox)
{
    return gt_esmini::osi::ComputeLogicalLaneAssignments(pos, box, index);
}

std::set<std::uint64_t> LogicalLaneIdSet(const osi3::GroundTruth& gt)
{
    std::set<std::uint64_t> ids;
    for (const auto& ll : gt.logical_lane())
    {
        ids.insert(ll.id().value());
    }
    return ids;
}
}  // namespace

// S2.5b. osi_common.proto defines BaseMoving.position as "the center (x,y,z) of the
// bounding box" and LogicalLaneAssignment.s_position as the S of "the object
// reference point", so the two are the same point and neither is the entity origin.
// esmini places a vehicle by its origin -- the rear axle for the shipped catalogue --
// so reporting pos.GetS() put the assignment 1.4 m behind the box whose position the
// same message carries. Design 2-6-1 specified the origin; corrected here, doc updated.
//
// The straight-road case fixes the magnitude exactly: heading along the lane, the
// reference point is center_x further along s and at the same t.
TEST(OsiLogicalLane, AssignmentStIsMeasuredAtTheBoundingBoxCentre)
{
    osi3::GroundTruth                gt;
    gt_esmini::osi::LogicalLaneIndex index;
    ASSERT_TRUE(BuildFor(Xodr("straight_500m.xodr"), &gt, &index));

    roadmanager::Position pos;
    const id_t            road_id = roadmanager::Position::GetOpenDrive()->GetRoadByIdx(0)->GetId();
    for (double s : {5.0, 120.0, 300.0, 450.0})
    {
        for (double offset : {-0.4, 0.0, 0.3})
        {
            ASSERT_EQ(pos.SetLanePos(road_id, -1, s, offset), kPosOk) << "s=" << s;
            const std::vector<LogicalLaneAssignmentEntry> a = Assign(pos, index);
            ASSERT_FALSE(a.empty()) << "s=" << s << " offset=" << offset;
            for (const LogicalLaneAssignmentEntry& e : a)
            {
                EXPECT_TRUE(e.ref_point_ok);
                EXPECT_NEAR(e.s_position, pos.GetS() + kCarBox.center_x, 1e-6) << "s=" << s;
                EXPECT_NEAR(e.t_position, pos.GetT(), 1e-6) << "s=" << s;
                // The negative control, stated as its own expectation rather than left
                // implied by the tolerance above: the ORIGIN value must not be what
                // comes out. A build that ignored the box passes every other
                // assignment test in this file.
                EXPECT_GT(std::fabs(e.s_position - pos.GetS()), 1.0) << "s=" << s;
            }
            EXPECT_TRUE(a.front().is_anchor) << "entry [0] must be the lane Position reports";
            EXPECT_EQ(a.front().lane_id, pos.GetLaneId());
            const auto it = index.find(gt_esmini::osi::LogicalLaneKey{road_id, 0u, -1});
            ASSERT_NE(it, index.end());
            EXPECT_EQ(a.front().assigned_lane_id, it->second);
        }
    }
}

// A box whose centre IS the origin (pedestrians, and any catalogue entry authored
// that way) must reproduce the Position bit for bit -- this is the short-circuit that
// keeps the common degenerate case off the XY -> road search, and it is also what
// makes "the reported point moved" attributable to the box rather than to a round
// trip through the road solver.
TEST(OsiLogicalLane, AssignmentStCollapsesToTheOriginWhenTheBoxIsCentredOnIt)
{
    osi3::GroundTruth                gt;
    gt_esmini::osi::LogicalLaneIndex index;
    ASSERT_TRUE(BuildFor(Xodr("straight_500m.xodr"), &gt, &index));

    constexpr ObjectBox   kCentredBox{0.6, 0.6, 0.0, 0.0, 0.9};  // a pedestrian
    const id_t            road_id = roadmanager::Position::GetOpenDrive()->GetRoadByIdx(0)->GetId();
    roadmanager::Position pos;
    ASSERT_EQ(pos.SetLanePos(road_id, -1, 200.0, 0.2), kPosOk);
    pos.SetHeadingRelative(0.3);

    const std::vector<LogicalLaneAssignmentEntry> a = Assign(pos, index, kCentredBox);
    ASSERT_FALSE(a.empty());
    EXPECT_DOUBLE_EQ(a.front().s_position, pos.GetS());
    EXPECT_DOUBLE_EQ(a.front().t_position, pos.GetT());
}

// Yaw moves the reference point sideways: the centre offset is in the ENTITY frame,
// so a vehicle angled across the lane has its box centre off to one side of its
// origin. This is the component a straight-ahead test cannot see, and it is largest
// during a lane change -- the same frames where the second assignment of L1-b
// appears.
TEST(OsiLogicalLane, AssignmentStFollowsTheBoxCentreThroughYaw)
{
    osi3::GroundTruth                gt;
    gt_esmini::osi::LogicalLaneIndex index;
    ASSERT_TRUE(BuildFor(Xodr("straight_500m.xodr"), &gt, &index));

    const id_t            road_id = roadmanager::Position::GetOpenDrive()->GetRoadByIdx(0)->GetId();
    roadmanager::Position pos;
    for (double h_rel : {-0.35, -0.10, 0.10, 0.35})
    {
        ASSERT_EQ(pos.SetLanePos(road_id, -1, 250.0, 0.0), kPosOk);
        pos.SetHeadingRelative(h_rel);

        const std::vector<LogicalLaneAssignmentEntry> a = Assign(pos, index);
        ASSERT_FALSE(a.empty()) << "h_rel=" << h_rel;
        // Exact on a straight road, where the road frame and the world frame differ by
        // a constant rotation only.
        EXPECT_NEAR(a.front().s_position, pos.GetS() + kCarBox.center_x * std::cos(h_rel), 1e-6) << "h_rel=" << h_rel;
        EXPECT_NEAR(a.front().t_position, pos.GetT() + kCarBox.center_x * std::sin(h_rel), 1e-6) << "h_rel=" << h_rel;
        // Both signs, because a build that dropped the sign of the lateral term still
        // matches one of them.
        EXPECT_EQ(a.front().t_position > pos.GetT(), h_rel > 0.0) << "h_rel=" << h_rel;
    }
}

// On a curve the reference point is NOT "origin s plus center_x": stepping along the
// vehicle own tangent leaves the reference line, so t moves even at zero yaw and s
// advances by slightly less than the offset. curve_r100.xodr road 0 runs straight to
// s=500 and then bends on an R=100 arc.
//
// The cross-check is an independently constructed Position driven from the world
// coordinates of the box centre, and the linear prediction is asserted to be WRONG by
// a measurable margin in the same test, so "close enough on a curve" cannot pass.
TEST(OsiLogicalLane, AssignmentStOnACurveIsNotALinearExtrapolation)
{
    osi3::GroundTruth                gt;
    gt_esmini::osi::LogicalLaneIndex index;
    ASSERT_TRUE(BuildFor(Xodr("curve_r100.xodr"), &gt, &index));

    const id_t            road_id = roadmanager::Position::GetOpenDrive()->GetRoadByIdx(0)->GetId();
    roadmanager::Position pos;
    ASSERT_EQ(pos.SetLanePos(road_id, -1, 560.0, 0.0), kPosOk) << "s=560 must be inside the arc";

    const std::vector<LogicalLaneAssignmentEntry> a = Assign(pos, index);
    ASSERT_FALSE(a.empty());

    // Independent resolve: the box centre in world coordinates, put on the road by a
    // Position this test builds itself.
    roadmanager::Position probe;
    ASSERT_EQ(probe.SetInertiaPos(pos.GetX() + kCarBox.center_x * std::cos(pos.GetH()),
                                  pos.GetY() + kCarBox.center_x * std::sin(pos.GetH()),
                                  pos.GetH()),
              0);
    EXPECT_EQ(probe.GetTrackId(), pos.GetTrackId());
    EXPECT_NEAR(a.front().s_position, probe.GetS(), 1e-6);
    EXPECT_NEAR(a.front().t_position, probe.GetT(), 1e-6);

    // Closed form, from the fixture's own geometry rather than from the code under
    // test: curve_r100.xodr declares curvature=1.0e-2 over [500, 657.08], so the
    // reference line is a circle of R = 100 m. A vehicle sitting at offset t rides a
    // concentric circle of radius r = R - t (the centre of curvature is on the +t
    // side), and stepping d along ITS OWN TANGENT lands on radius sqrt(r^2 + d^2),
    // having swept atan(d/r) -- which is R*atan(d/r) of ROAD s, not d.
    constexpr double kRefRadius = 100.0;
    const double     d          = kCarBox.center_x;
    const double     r          = kRefRadius - pos.GetT();
    const double     expect_s   = pos.GetS() + kRefRadius * std::atan(d / r);
    const double     expect_t   = kRefRadius - std::sqrt(r * r + d * d);
    EXPECT_NEAR(a.front().s_position, expect_s, 5e-4);
    EXPECT_NEAR(a.front().t_position, expect_t, 5e-4);

    // And the linear prediction -- "s plus center_x, t unchanged" -- is wrong by far
    // more than that tolerance in both coordinates. Without this pair the test above
    // would also pass a build that approximated, because 5e-4 m is not a number a
    // reader can weigh on sight.
    EXPECT_GT(std::fabs(a.front().t_position - pos.GetT()), 5e-3) << "t must move on a curve at zero yaw";
    EXPECT_GT(std::fabs(a.front().s_position - (pos.GetS() + d)), 5e-3) << "s advance is not the raw offset";
}

// The reference point is pinned to the entity OWN road, because s/t have to be on the
// reference line of the lane the assignment names. Past the end of a road the point
// therefore saturates at that road length instead of reappearing as a small s on
// whatever road comes next -- a frame or two per road transition, bounded by the
// centre offset, against a coordinate-frame switch that would look perfectly valid on
// the wire (design 10-15).
TEST(OsiLogicalLane, ReferencePointStaysOnTheEntitysOwnRoad)
{
    osi3::GroundTruth                gt;
    gt_esmini::osi::LogicalLaneIndex index;
    ASSERT_TRUE(BuildFor(Xodr("multi_intersections.xodr"), &gt, &index));

    roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();
    ASSERT_NE(odr, nullptr);

    unsigned checked = 0;
    for (unsigned i = 0; i < odr->GetNumOfRoads(); i++)
    {
        roadmanager::Road* road = odr->GetRoadByIdx(i);
        if (road == nullptr || road->GetLength() < 10.0 || road->GetNumberOfLaneSections() == 0)
        {
            continue;
        }
        roadmanager::LaneSection* lsec = road->GetLaneSectionByIdx(road->GetNumberOfLaneSections() - 1);
        roadmanager::Lane*        lane = (lsec == nullptr) ? nullptr : lsec->GetLaneById(-1);
        if (lane == nullptr || !lane->IsDriving())
        {
            continue;
        }

        roadmanager::Position pos;
        // Half a metre from the end, so the box centre lands 0.9 m past it.
        if (pos.SetLanePos(road->GetId(), -1, road->GetLength() - 0.5, 0.0) != kPosOk)
        {
            continue;
        }
        roadmanager::Position ref;
        ASSERT_TRUE(gt_esmini::osi::ResolveOsiReferencePoint(pos, kCarBox, &ref)) << "road " << road->GetId();
        EXPECT_EQ(ref.GetTrackId(), road->GetId()) << "road " << road->GetId();
        EXPECT_LE(ref.GetS(), road->GetLength() + 1e-6) << "road " << road->GetId();
        EXPECT_GE(ref.GetS(), pos.GetS() - 1e-6) << "road " << road->GetId();
        checked++;
    }
    EXPECT_GT(checked, 5u) << "fixture stopped providing roads long enough to test";
}

// angle_to_lane. Design 2-6-1 specified GetHRelative() raw; Position keeps that in
// [0, 2pi), so a vehicle yawed a hair to the RIGHT of the lane direction would be
// reported at ~6.28 rad while one yawed a hair left reads ~0.00 -- a 2pi step
// through the value the field exists to compare. Wrapped to [-pi, pi] like every
// other angle this reporter emits. Both signs are shown, because a build that
// always returned the raw value passes any single-sided check.
TEST(OsiLogicalLane, AssignmentAngleIsWrappedAndSigned)
{
    osi3::GroundTruth                gt;
    gt_esmini::osi::LogicalLaneIndex index;
    ASSERT_TRUE(BuildFor(Xodr("e6mini.xodr"), &gt, &index));

    roadmanager::Position pos;
    ASSERT_EQ(pos.SetLanePos(0, -3, 200.0, 0.0), kPosOk);

    pos.SetHeadingRelative(0.10);
    ASSERT_FALSE(Assign(pos, index).empty());
    EXPECT_NEAR(Assign(pos, index).front().angle_to_lane, 0.10, 1e-9);

    pos.SetHeadingRelative(-0.10);
    // The negative control: Position itself hands out 2pi-0.10 here.
    EXPECT_NEAR(pos.GetHRelative(), 2.0 * M_PI - 0.10, 1e-9) << "precondition: Position is unwrapped";
    ASSERT_FALSE(Assign(pos, index).empty());
    EXPECT_NEAR(Assign(pos, index).front().angle_to_lane, -0.10, 1e-9);
}

// L1-b, BOTH POLARITIES, and the 5 cm threshold either side of its own edge.
//
// e6mini road 0 lane -3 is 3.50 m wide, its inner neighbour -2 is 3.65 m. A 2.00 m
// body centred in -3 has 0.75 m of clearance each side, so the boundary between
// "one lane" and "two" sits at offset 0.75 + 0.05 = 0.80 m exactly. A build that
// only ever emits the driving lane passes the centred case and fails here; one
// that assigns on any touch at all fails the 0.79 m case.
TEST(OsiLogicalLane, AssignmentStraddleAppearsAndDisappearsAcrossTheFiveCentimetreEdge)
{
    osi3::GroundTruth                gt;
    gt_esmini::osi::LogicalLaneIndex index;
    ASSERT_TRUE(BuildFor(Xodr("e6mini.xodr"), &gt, &index));

    roadmanager::Position pos;
    const auto            lane_m2 = index.find(gt_esmini::osi::LogicalLaneKey{0u, 0u, -2});
    const auto            lane_m3 = index.find(gt_esmini::osi::LogicalLaneKey{0u, 0u, -3});
    ASSERT_NE(lane_m2, index.end());
    ASSERT_NE(lane_m3, index.end());

    struct Case
    {
        double      offset;
        size_t      expected;
        const char* why;
    };
    // 1 -> 2 -> 1: the lane change, and its return.
    const Case cases[] = {{0.00, 1, "centred in lane -3"},
                          {0.79, 1, "0.04 m into lane -2 -- at or below the 5 cm rule"},
                          {0.81, 2, "0.06 m into lane -2 -- above it"},
                          {1.20, 2, "0.45 m into lane -2"},
                          {1.75, 2, "body centre on the lane boundary"},
                          {0.00, 1, "back to the middle of lane -3"}};

    for (const Case& c : cases)
    {
        ASSERT_EQ(pos.SetLanePos(0, -3, 300.0, c.offset), kPosOk);
        const std::vector<LogicalLaneAssignmentEntry> a = Assign(pos, index);
        EXPECT_EQ(a.size(), c.expected) << "offset " << c.offset << " (" << c.why << ")";
        ASSERT_FALSE(a.empty());
        EXPECT_EQ(a.front().assigned_lane_id, lane_m3->second) << "anchor stays first at offset " << c.offset;
        if (a.size() == 2)
        {
            EXPECT_EQ(a[1].assigned_lane_id, lane_m2->second) << "offset " << c.offset;
            EXPECT_GT(a[1].overlap_m, gt_esmini::osi::kLogicalLaneOverlapThresholdM) << "offset " << c.offset;
        }
    }
}

// Yaw widens the body's lateral footprint: a 5 m long car at 20 deg to the lane
// reaches 0.5*(5*sin20 + 2*cos20) = 1.80 m to each side instead of 1.00 m. Taking
// only the width would under-report exactly during a lane change, which is when the
// second assignment matters. Same position, same box, only the heading differs.
TEST(OsiLogicalLane, AssignmentWidthAccountsForYawRelativeToTheLane)
{
    osi3::GroundTruth                gt;
    gt_esmini::osi::LogicalLaneIndex index;
    ASSERT_TRUE(BuildFor(Xodr("e6mini.xodr"), &gt, &index));

    roadmanager::Position pos;
    ASSERT_EQ(pos.SetLanePos(0, -3, 300.0, 0.0), kPosOk);

    pos.SetHeadingRelative(0.0);
    EXPECT_EQ(Assign(pos, index).size(), 1u) << "aligned with the lane: 1.00 m of body each side of centre";

    pos.SetHeadingRelative(20.0 * M_PI / 180.0);
    const std::vector<LogicalLaneAssignmentEntry> yawed = Assign(pos, index);
    EXPECT_GT(yawed.size(), 1u) << "20 deg yaw reaches 1.80 m each side and must pick up a neighbour";
    for (const LogicalLaneAssignmentEntry& e : yawed)
    {
        std::cout << "[S2.5] yawed 20deg -> lane " << e.lane_id << " overlap " << e.overlap_m << " m" << std::endl;
    }
}

// The one acceptance criterion that only the logical face can satisfy. Inside an OSI
// intersection the PHYSICAL lanes are fused into one TYPE_INTERSECTION lane carrying
// the junction's global id, which is why signal:ego_lane cannot join there. The
// logical layer keeps one lane per connecting-road lane, so the assignment resolves
// to the connecting lane itself.
//
// The physical face is asserted in the same test, unchanged: fixing only one of the
// two would break replayer's osi_receiver and the upstream UDP samples, which read
// the fused id on purpose (design 2-6-1, trap 2).
TEST(OsiLogicalLane, AssignmentOnAConnectingRoadNamesTheConnectingLaneNotTheJunction)
{
    osi3::GroundTruth                gt;
    gt_esmini::osi::LogicalLaneIndex index;
    ASSERT_TRUE(BuildFor(Xodr("multi_intersections.xodr"), &gt, &index));
    roadmanager::OpenDrive* od = roadmanager::Position::GetOpenDrive();

    unsigned              checked = 0;
    roadmanager::Position pos;
    for (unsigned i = 0; i < od->GetNumOfRoads(); i++)
    {
        roadmanager::Road* road = od->GetRoadByIdx(i);
        if (road->GetJunction() == ID_UNDEFINED)
        {
            continue;
        }
        roadmanager::Junction* junction = od->GetJunctionById(road->GetJunction());
        if (junction == nullptr || !junction->IsOsiIntersection())
        {
            continue;
        }
        roadmanager::LaneSection* lsec = road->GetLaneSectionByIdx(0);
        ASSERT_NE(lsec, nullptr);
        for (unsigned k = 0; k < lsec->GetNumberOfLanes(); k++)
        {
            roadmanager::Lane* lane = lsec->GetLaneByIdx(k);
            if (lane->IsCenter() || !lane->IsDriving())
            {
                continue;
            }
            ASSERT_EQ(pos.SetLanePos(road->GetId(), lane->GetId(), road->GetLength() / 2.0, 0.0), kPosOk);
            const std::vector<LogicalLaneAssignmentEntry> a = Assign(pos, index);
            ASSERT_FALSE(a.empty()) << "road " << road->GetId() << " lane " << lane->GetId();

            const auto it = index.find(gt_esmini::osi::LogicalLaneKey{road->GetId(), 0u, lane->GetId()});
            ASSERT_NE(it, index.end());

            // logical face: the connecting lane itself
            EXPECT_EQ(a.front().assigned_lane_id, it->second);
            EXPECT_NE(a.front().assigned_lane_id, static_cast<std::uint64_t>(junction->GetGlobalId()));
            // physical face: still the fused junction, unchanged by this stage
            EXPECT_EQ(pos.GetLaneGlobalId(), junction->GetGlobalId());
            checked++;
        }
    }
    std::cout << "[S2.5] multi_intersections connecting-road driving lanes checked=" << checked << std::endl;
    ASSERT_GT(checked, 0u) << "fixture exposed no OSI-intersection connecting road -- the check never ran";
}

// Reference closure for the assignment face (design 9 check 6): sweep every lane of
// a network and require that every id handed out exists in the emitted
// logical_lane[]. A dangling id serialises perfectly well, so this is the only thing
// standing between "looks right" and "is right".
TEST(OsiLogicalLane, EveryAssignedLogicalLaneIdExists)
{
    for (const char* name : {"e6mini.xodr", "fabriksgatan.xodr", "multi_intersections.xodr"})
    {
        osi3::GroundTruth                gt;
        gt_esmini::osi::LogicalLaneIndex index;
        ASSERT_TRUE(BuildFor(Xodr(name), &gt, &index)) << name;
        const std::set<std::uint64_t> live = LogicalLaneIdSet(gt);
        roadmanager::OpenDrive*       od   = roadmanager::Position::GetOpenDrive();

        unsigned              assignments = 0, straddles = 0;
        roadmanager::Position pos;
        for (unsigned i = 0; i < od->GetNumOfRoads(); i++)
        {
            roadmanager::Road* road = od->GetRoadByIdx(i);
            for (unsigned j = 0; j < road->GetNumberOfLaneSections(); j++)
            {
                roadmanager::LaneSection* lsec = road->GetLaneSectionByIdx(j);
                const double              end_s =
                    (j + 1 < road->GetNumberOfLaneSections()) ? road->GetLaneSectionByIdx(j + 1)->GetS() : road->GetLength();
                const double s = 0.5 * (lsec->GetS() + end_s);
                for (unsigned k = 0; k < lsec->GetNumberOfLanes(); k++)
                {
                    roadmanager::Lane* lane = lsec->GetLaneByIdx(k);
                    if (lane->IsCenter())
                    {
                        continue;
                    }
                    if (pos.SetLanePos(road->GetId(), lane->GetId(), s, 0.0) != roadmanager::Position::ReturnCode::OK)
                    {
                        continue;
                    }
                    const std::vector<LogicalLaneAssignmentEntry> a = Assign(pos, index);
                    if (a.size() > 1)
                    {
                        straddles++;
                    }
                    for (const LogicalLaneAssignmentEntry& e : a)
                    {
                        ASSERT_EQ(live.count(e.assigned_lane_id), 1u)
                            << name << " road " << road->GetId() << " lane " << lane->GetId() << " -> " << e.assigned_lane_id;
                        assignments++;
                    }
                }
            }
        }
        std::cout << "[S2.5] " << name << " assignments=" << assignments << " positions_with_straddle=" << straddles << std::endl;
        EXPECT_GT(assignments, 0u) << name;
    }
}

// An empty index is what every consumer sees while the feature is OFF (the post-pass
// clears it unconditionally before the flag check). Nothing may be emitted then --
// not a dangling id, not a zero.
TEST(OsiLogicalLane, AssignmentIsEmptyWithoutAnIndex)
{
    osi3::GroundTruth                gt;
    gt_esmini::osi::LogicalLaneIndex index;
    ASSERT_TRUE(BuildFor(Xodr("e6mini.xodr"), &gt, &index));

    roadmanager::Position pos;
    ASSERT_EQ(pos.SetLanePos(0, -3, 300.0, 0.0), kPosOk);
    ASSERT_FALSE(Assign(pos, index).empty()) << "precondition: this position does resolve with an index";

    const gt_esmini::osi::LogicalLaneIndex empty;
    EXPECT_TRUE(Assign(pos, empty).empty());
}

// ---------------------------------------------------------------------------
// S2 -- connectivity (design 2-4)
// ---------------------------------------------------------------------------

namespace
{
std::map<std::uint64_t, const osi3::LogicalLane*> LaneById(const osi3::GroundTruth& gt)
{
    std::map<std::uint64_t, const osi3::LogicalLane*> out;
    for (const auto& ll : gt.logical_lane())
    {
        out[ll.id().value()] = &ll;
    }
    return out;
}

// Does `list` name `other`, and with which flag? -1 absent, 0 present with
// at_begin false, 1 present with at_begin true.
int FindConnection(const google::protobuf::RepeatedPtrField<osi3::LogicalLane_LaneConnection>& list, std::uint64_t other)
{
    for (const auto& c : list)
    {
        if (c.other_lane_id().value() == other)
        {
            return c.at_begin_of_other_lane() ? 1 : 0;
        }
    }
    return -1;
}
}  // namespace

// THE TRAP THIS PINS. "'End' is relative to the reference line, so connections at
// #end_s." Both fields are in reference-line direction, so on a lane driven towards
// decreasing s the successor is the connection BEHIND the vehicle. A build that read
// successor as "ahead" would swap the two fields on exactly half the lanes and still
// produce a graph that walks, closes, and passes every count.
//
// The two populations are counted so the assertion cannot pass having only ever seen
// the lanes whose driving direction agrees with the reference line -- the case the
// wrong reading also gets right.
TEST(OsiLogicalLane, SuccessorIsTheHigherSNeighbourOnBothMoveDirections)
{
    unsigned checked_increasing = 0, checked_decreasing = 0;

    for (const char* fixture : {"soderleden.xodr", "highway_example_with_merge_and_split.xodr"})
    {
        osi3::GroundTruth                gt;
        gt_esmini::osi::LogicalLaneIndex index;
        ASSERT_TRUE(BuildFor(Xodr(fixture), &gt, &index)) << fixture;

        const auto by_id = LaneById(gt);
        for (const auto& ll : gt.logical_lane())
        {
            const LaneAddr addr = AddrOf(ll);
            for (const auto& c : ll.successor_lane())
            {
                const auto it = by_id.find(c.other_lane_id().value());
                ASSERT_NE(it, by_id.end()) << fixture;
                if (AddrOf(*it->second).road_id != addr.road_id)
                {
                    continue;  // across a road boundary s restarts; only comparable inside one road
                }
                EXPECT_GE(it->second->start_s(), ll.end_s() - 1e-6)
                    << fixture << " road " << addr.road_id << " lane " << addr.lane_id
                    << ": successor sits at lower s than the lane's own end";
                EXPECT_TRUE(c.at_begin_of_other_lane())
                    << fixture << ": a successor inside a road is entered at the next section's beginning";
                if (ll.move_direction() == osi3::LogicalLane_MoveDirection_MOVE_DIRECTION_INCREASING_S)
                {
                    checked_increasing++;
                }
                else if (ll.move_direction() == osi3::LogicalLane_MoveDirection_MOVE_DIRECTION_DECREASING_S)
                {
                    checked_decreasing++;
                }
            }
        }
    }
    EXPECT_GT(checked_increasing, 0u);
    EXPECT_GT(checked_decreasing, 0u) << "no lane driven towards decreasing s was covered -- the check is vacuous";
}

// Every meeting has to be walkable from both ends, and with the EXACT opposite flag:
// an entry in A's successor_lane offered A's end, so the far lane must name A with
// at_begin false; an entry in A's predecessor_lane offered A's beginning, so with
// at_begin true.
//
// This is what says the mirror the post-pass emits is right rather than merely
// present: OpenDRIVE declares a junction meeting from the incoming side only, so
// half of these entries exist because the pass wrote them.
TEST(OsiLogicalLane, EveryConnectionIsMirroredWithTheOppositeEnd)
{
    unsigned checked = 0;
    for (const char* fixture : {"fabriksgatan.xodr", "multi_intersections.xodr", "soderleden.xodr"})
    {
        osi3::GroundTruth                gt;
        gt_esmini::osi::LogicalLaneIndex index;
        ASSERT_TRUE(BuildFor(Xodr(fixture), &gt, &index)) << fixture;

        const auto by_id = LaneById(gt);
        for (const auto& ll : gt.logical_lane())
        {
            for (int side = 0; side < 2; side++)
            {
                const auto& list = (side == 0) ? ll.predecessor_lane() : ll.successor_lane();
                // Which of MY ends this list describes: predecessor_lane is my beginning.
                const bool my_end_is_begin = (side == 0);
                for (const auto& c : list)
                {
                    const auto it = by_id.find(c.other_lane_id().value());
                    ASSERT_NE(it, by_id.end()) << fixture << ": dangling other_lane_id";
                    const auto& back =
                        c.at_begin_of_other_lane() ? it->second->predecessor_lane() : it->second->successor_lane();
                    const int flag = FindConnection(back, ll.id().value());
                    ASSERT_NE(flag, -1) << fixture << ": lane " << AddrOf(ll).road_id << "/" << AddrOf(ll).lane_id
                                        << " is not named back by " << AddrOf(*it->second).road_id << "/"
                                        << AddrOf(*it->second).lane_id;
                    EXPECT_EQ(flag, my_end_is_begin ? 1 : 0) << fixture << ": the mirror names the wrong end";
                    checked++;
                }
            }
        }
    }
    EXPECT_GT(checked, 0u);
}

// "'Right' is in definition direction (not driving direction), so right lanes have
// smaller T coordinates" -- and t grows with the OpenDRIVE lane id. The same proto
// FILE also carries centerline_is_driving_direction, which DOES flip with the road
// rule, so the two are easy to conflate. This runs one map in both hands: the
// adjacency must be identical and the move directions must not be, or the fixture
// pair proves nothing.
TEST(OsiLogicalLane, AdjacencyIsInReferenceLineDirectionInBothTrafficHands)
{
    std::map<std::pair<int, int>, std::pair<int, int>> adjacency_by_hand[2];  // (road,lane) -> (right,left)
    std::map<std::pair<int, int>, int>                 move_direction_by_hand[2];

    const char* fixtures[2] = {"e6mini.xodr", "e6mini-lht.xodr"};
    for (int h = 0; h < 2; h++)
    {
        osi3::GroundTruth                gt;
        gt_esmini::osi::LogicalLaneIndex index;
        ASSERT_TRUE(BuildFor(Xodr(fixtures[h]), &gt, &index)) << fixtures[h];

        const auto by_id = LaneById(gt);
        for (const auto& ll : gt.logical_lane())
        {
            const LaneAddr addr  = AddrOf(ll);
            int            right = 0, left = 0;
            for (const auto& rel : ll.right_adjacent_lane())
            {
                const auto it = by_id.find(rel.other_lane_id().value());
                ASSERT_NE(it, by_id.end()) << fixtures[h];
                right = AddrOf(*it->second).lane_id;
                EXPECT_LT(right, addr.lane_id) << fixtures[h] << ": the right neighbour must have the smaller lane id";
            }
            for (const auto& rel : ll.left_adjacent_lane())
            {
                const auto it = by_id.find(rel.other_lane_id().value());
                ASSERT_NE(it, by_id.end()) << fixtures[h];
                left = AddrOf(*it->second).lane_id;
                EXPECT_GT(left, addr.lane_id) << fixtures[h] << ": the left neighbour must have the larger lane id";
            }
            adjacency_by_hand[h][{addr.road_id, addr.lane_id}]      = {right, left};
            move_direction_by_hand[h][{addr.road_id, addr.lane_id}] = static_cast<int>(ll.move_direction());
        }
    }

    ASSERT_FALSE(adjacency_by_hand[0].empty());
    EXPECT_EQ(adjacency_by_hand[0], adjacency_by_hand[1]) << "adjacency changed with the traffic hand";
    // The negative control: the two fixtures really are the same road in opposite
    // hands, so something that DOES depend on the hand has to differ.
    EXPECT_NE(move_direction_by_hand[0], move_direction_by_hand[1])
        << "move_direction is identical in both fixtures -- the LHT fixture is not what it claims to be";
}

// The centre lane carries no logical lane, so "the next lane id" is not always the
// next integer: lane -1 and lane +1 are each other's neighbours across it.
TEST(OsiLogicalLane, LaneMinusOneAndPlusOneAreNeighboursAcrossTheCentreLane)
{
    osi3::GroundTruth                gt;
    gt_esmini::osi::LogicalLaneIndex index;
    ASSERT_TRUE(BuildFor(Xodr("e6mini.xodr"), &gt, &index));

    std::map<std::pair<int, int>, const osi3::LogicalLane*> by_lane;
    for (const auto& ll : gt.logical_lane())
    {
        const LaneAddr a                = AddrOf(ll);
        by_lane[{a.road_id, a.lane_id}] = &ll;
    }

    unsigned pairs = 0;
    for (const auto& entry : by_lane)
    {
        if (entry.first.second != -1 || by_lane.find({entry.first.first, 1}) == by_lane.end())
        {
            continue;
        }
        const osi3::LogicalLane* minus_one = entry.second;
        const osi3::LogicalLane* plus_one  = by_lane.at({entry.first.first, 1});
        ASSERT_EQ(minus_one->left_adjacent_lane_size(), 1) << "lane -1 must have exactly one left neighbour";
        EXPECT_EQ(minus_one->left_adjacent_lane(0).other_lane_id().value(), plus_one->id().value());
        ASSERT_EQ(plus_one->right_adjacent_lane_size(), 1);
        EXPECT_EQ(plus_one->right_adjacent_lane(0).other_lane_id().value(), minus_one->id().value());
        pairs++;
    }
    EXPECT_GT(pairs, 0u);
}

// Lanes appear and disappear only at lane-section borders and a logical lane is cut
// at exactly those borders, so a neighbour is a neighbour for the lane's whole
// length. Also pins the ordering requirement, which is trivially met while each list
// holds at most one entry -- and this is what would notice if that stopped holding.
TEST(OsiLogicalLane, AdjacencySpansTheWholeLaneAndIsAtMostOnePerSide)
{
    for (const char* fixture : {"fabriksgatan.xodr", "highway_example_with_merge_and_split.xodr"})
    {
        osi3::GroundTruth                gt;
        gt_esmini::osi::LogicalLaneIndex index;
        ASSERT_TRUE(BuildFor(Xodr(fixture), &gt, &index)) << fixture;

        const auto by_id = LaneById(gt);
        for (const auto& ll : gt.logical_lane())
        {
            EXPECT_LE(ll.right_adjacent_lane_size(), 1) << fixture;
            EXPECT_LE(ll.left_adjacent_lane_size(), 1) << fixture;
            for (int side = 0; side < 2; side++)
            {
                for (const auto& rel : (side == 0) ? ll.right_adjacent_lane() : ll.left_adjacent_lane())
                {
                    const auto it = by_id.find(rel.other_lane_id().value());
                    ASSERT_NE(it, by_id.end()) << fixture;
                    EXPECT_DOUBLE_EQ(rel.start_s(), ll.start_s()) << fixture;
                    EXPECT_DOUBLE_EQ(rel.end_s(), ll.end_s()) << fixture;
                    EXPECT_DOUBLE_EQ(rel.start_s_other(), it->second->start_s()) << fixture;
                    EXPECT_DOUBLE_EQ(rel.end_s_other(), it->second->end_s()) << fixture;
                    EXPECT_GT(rel.end_s(), rel.start_s()) << fixture << ": LaneRelation requires end_s > start_s";
                }
            }
        }
    }
}

// "Both lanes have a non-zero width at the connection point." A lane that has
// tapered to nothing at a merge or a split still carries an OpenDRIVE <link>, and
// connecting it would tell a planner it can drive through a lane of zero width.
TEST(OsiLogicalLane, ALaneEndOfZeroWidthCarriesNoConnection)
{
    unsigned zero_width_ends = 0;
    for (const char* fixture : {"highway_example_with_merge_and_split.xodr", "soderleden.xodr", "multi_intersections.xodr"})
    {
        osi3::GroundTruth                gt;
        gt_esmini::osi::LogicalLaneIndex index;
        ASSERT_TRUE(BuildFor(Xodr(fixture), &gt, &index)) << fixture;
        roadmanager::OpenDrive* od = roadmanager::Position::GetOpenDrive();

        for (const auto& ll : gt.logical_lane())
        {
            const LaneAddr     addr = AddrOf(ll);
            roadmanager::Road* road = od->GetRoadById(static_cast<id_t>(addr.road_id));
            ASSERT_NE(road, nullptr) << fixture;
            roadmanager::LaneSection* lsec = nullptr;
            for (unsigned j = 0; j < road->GetNumberOfLaneSections(); j++)
            {
                if (std::fabs(road->GetLaneSectionByIdx(j)->GetS() - ll.start_s()) < 1e-6)
                {
                    lsec = road->GetLaneSectionByIdx(j);
                    break;
                }
            }
            ASSERT_NE(lsec, nullptr) << fixture << " road " << addr.road_id << " s " << ll.start_s();

            if (lsec->GetWidth(ll.start_s(), addr.lane_id) <= SMALL_NUMBER)
            {
                zero_width_ends++;
                EXPECT_EQ(ll.predecessor_lane_size(), 0)
                    << fixture << " road " << addr.road_id << " lane " << addr.lane_id
                    << ": zero width at start_s but a predecessor is attached";
            }
            if (lsec->GetWidth(ll.end_s(), addr.lane_id) <= SMALL_NUMBER)
            {
                zero_width_ends++;
                EXPECT_EQ(ll.successor_lane_size(), 0)
                    << fixture << " road " << addr.road_id << " lane " << addr.lane_id
                    << ": zero width at end_s but a successor is attached";
            }
        }
    }
    EXPECT_GT(zero_width_ends, 0u) << "no lane end of zero width in any fixture -- the rule was never exercised";
}
