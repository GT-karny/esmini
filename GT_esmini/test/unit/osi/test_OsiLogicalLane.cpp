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
#include <vector>

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
    // S1 emits no boundaries; S3 does.
    EXPECT_EQ(gt.logical_lane_boundary_size(), 0);

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
