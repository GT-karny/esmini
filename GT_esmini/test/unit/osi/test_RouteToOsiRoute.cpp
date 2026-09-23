/*
 * Unit tests for osi3::Route construction and L2 route progress (S4).
 *
 * Every test drives the real OpenDRIVE parser on a synthetic network written to a
 * temp file (the test_RouteLanePlan.cpp / test_OdrVirtualJunction.cpp pattern) and a
 * real roadmanager::Route built with Route::AddWaypoint, because the two things most
 * likely to be wrong here -- the travel direction of a road entered at its far end,
 * and lane ids renumbered across lane sections -- only exist in a parsed network.
 *
 * The osi3::Route half goes through BuildOsiRouteInto() with a GroundTruth and index
 * built by BuildOsiLogicalLanesInto() on the SAME network, so the reference-closure
 * assertion is comparing ids that were actually emitted, not ids a mock made up.
 *
 * Design: GT_esmini/docs/osi/logical_lane_and_route_design.md 2-5 / 2-6-2 / 5 / 9
 * Knowledge graph: spine-work:osi-logical-lane
 */
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "RoadManager.hpp"
#include "gt_esmini/control/virtualdriver/RouteLanePlan.hpp"
#include "gt_esmini/osi/GT_OsiLogicalLane.hpp"
#include "gt_esmini/osi/RouteToOsiRoute.hpp"
#include "osi_groundtruth.pb.h"
#include "osi_route.pb.h"

using namespace roadmanager;

namespace
{

std::string R2rRepoRoot()
{
#ifdef GT_ODR_REPO_ROOT
    return std::string(GT_ODR_REPO_ROOT);
#else
    return std::string();
#endif
}

std::filesystem::path R2rScratchDir()
{
    std::error_code   ec;
    const std::string root = R2rRepoRoot();
    if (!root.empty())
    {
        std::filesystem::path cand = std::filesystem::path(root) / "build" / "r2r_tests";
        std::filesystem::create_directories(cand, ec);
        if (!ec && std::filesystem::is_directory(cand))
        {
            return cand;
        }
    }
    std::filesystem::path tmp = std::filesystem::temp_directory_path(ec) / "r2r_tests";
    std::filesystem::create_directories(tmp, ec);
    return tmp;
}

bool WriteAndLoad(const std::string& name, const std::string& xodr)
{
    const std::filesystem::path p = R2rScratchDir() / name;
    std::ofstream               out(p, std::ios::binary);
    out << xodr;
    out.close();
    return Position::GetOpenDrive()->LoadOpenDriveFile(p.string().c_str(), true);
}

Position Wp(id_t road_id, int lane_id, double s, double h_rel = 0.0)
{
    Position wp;
    wp.SetLanePos(road_id, lane_id, s, 0.0);
    wp.SetHeadingRelative(h_rel);
    return wp;
}

// Route::AddWaypoint takes a NON-const Position&, so a temporary cannot be passed
// directly (it copies the waypoint internally, so the local is free to die here).
int AddWp(Route& route, id_t road_id, int lane_id, double s)
{
    Position wp = Wp(road_id, lane_id, s);
    return route.AddWaypoint(wp);
}

const char* kHeader =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<OpenDRIVE>\n"
    "  <header revMajor=\"1\" revMinor=\"7\" name=\"r2r\" version=\"1.00\" date=\"2026-09-24T00:00:00\""
    " north=\"0.0\" south=\"0.0\" east=\"0.0\" west=\"0.0\"/>\n";

// ---------------------------------------------------------------------------------------------
// N1 -- two roads joined the ordinary way (A's end to B's start). Travel is +s on both.
// Two right lanes carried straight through, so a route on lane -1 gives a one-lane band and a
// route on -2 gives another.
// ---------------------------------------------------------------------------------------------
std::string ForwardPairXodr()
{
    return std::string(kHeader) +
           "  <road name=\"A\" length=\"50.0\" id=\"1\" junction=\"-1\">\n"
           "    <link><successor elementType=\"road\" elementId=\"2\" contactPoint=\"start\"/></link>\n"
           "    <planView><geometry s=\"0.0\" x=\"0.0\" y=\"0.0\" hdg=\"0.0\" length=\"50.0\"><line/></geometry></planView>\n"
           "    <lanes>\n"
           "      <laneSection s=\"0.0\">\n"
           "        <center><lane id=\"0\" type=\"none\" level=\"false\"><link/></lane></center>\n"
           "        <right>\n"
           "          <lane id=\"-1\" type=\"driving\" level=\"false\"><link><successor id=\"-1\"/></link>"
           "<width sOffset=\"0.0\" a=\"3.5\" b=\"0.0\" c=\"0.0\" d=\"0.0\"/></lane>\n"
           "          <lane id=\"-2\" type=\"driving\" level=\"false\"><link><successor id=\"-2\"/></link>"
           "<width sOffset=\"0.0\" a=\"3.5\" b=\"0.0\" c=\"0.0\" d=\"0.0\"/></lane>\n"
           "        </right>\n"
           "      </laneSection>\n"
           "    </lanes>\n"
           "  </road>\n"
           "  <road name=\"B\" length=\"40.0\" id=\"2\" junction=\"-1\">\n"
           "    <link><predecessor elementType=\"road\" elementId=\"1\" contactPoint=\"end\"/></link>\n"
           "    <planView><geometry s=\"0.0\" x=\"50.0\" y=\"0.0\" hdg=\"0.0\" length=\"40.0\"><line/></geometry></planView>\n"
           "    <lanes>\n"
           "      <laneSection s=\"0.0\">\n"
           "        <center><lane id=\"0\" type=\"none\" level=\"false\"><link/></lane></center>\n"
           "        <right>\n"
           "          <lane id=\"-1\" type=\"driving\" level=\"false\"><link><predecessor id=\"-1\"/></link>"
           "<width sOffset=\"0.0\" a=\"3.5\" b=\"0.0\" c=\"0.0\" d=\"0.0\"/></lane>\n"
           "          <lane id=\"-2\" type=\"driving\" level=\"false\"><link><predecessor id=\"-2\"/></link>"
           "<width sOffset=\"0.0\" a=\"3.5\" b=\"0.0\" c=\"0.0\" d=\"0.0\"/></lane>\n"
           "        </right>\n"
           "      </laneSection>\n"
           "    </lanes>\n"
           "  </road>\n"
           // A third road, linked to nothing: somewhere to put an ego that is off the route.
           "  <road name=\"Away\" length=\"30.0\" id=\"9\" junction=\"-1\">\n"
           "    <link/>\n"
           "    <planView><geometry s=\"0.0\" x=\"0.0\" y=\"200.0\" hdg=\"0.0\" length=\"30.0\"><line/></geometry></planView>\n"
           "    <lanes>\n"
           "      <laneSection s=\"0.0\">\n"
           "        <center><lane id=\"0\" type=\"none\" level=\"false\"><link/></lane></center>\n"
           "        <right>\n"
           "          <lane id=\"-1\" type=\"driving\" level=\"false\"><link/>"
           "<width sOffset=\"0.0\" a=\"3.5\" b=\"0.0\" c=\"0.0\" d=\"0.0\"/></lane>\n"
           "        </right>\n"
           "      </laneSection>\n"
           "    </lanes>\n"
           "  </road>\n"
           "</OpenDRIVE>\n";
}

// ---------------------------------------------------------------------------------------------
// N2 -- the polarity network. A and B are authored pointing AT each other, so they meet end to
// end: A's successor is B at contactPoint "end", and B's successor is A at contactPoint "end".
// Driving A with +s and continuing onto B therefore means driving B with -s, on B's LEFT lane
// (+1). This is an everyday shape in authored maps, and it is the only way a segment with
// start_s > end_s can be produced -- osi_route.proto's inverted-direction convention (2-5).
// ---------------------------------------------------------------------------------------------
std::string HeadToHeadPairXodr()
{
    return std::string(kHeader) +
           "  <road name=\"A\" length=\"50.0\" id=\"1\" junction=\"-1\">\n"
           "    <link><successor elementType=\"road\" elementId=\"2\" contactPoint=\"end\"/></link>\n"
           "    <planView><geometry s=\"0.0\" x=\"0.0\" y=\"0.0\" hdg=\"0.0\" length=\"50.0\"><line/></geometry></planView>\n"
           "    <lanes>\n"
           "      <laneSection s=\"0.0\">\n"
           "        <center><lane id=\"0\" type=\"none\" level=\"false\"><link/></lane></center>\n"
           "        <right>\n"
           "          <lane id=\"-1\" type=\"driving\" level=\"false\"><link><successor id=\"1\"/></link>"
           "<width sOffset=\"0.0\" a=\"3.5\" b=\"0.0\" c=\"0.0\" d=\"0.0\"/></lane>\n"
           "        </right>\n"
           "      </laneSection>\n"
           "    </lanes>\n"
           "  </road>\n"
           // B is authored from x=90 back towards x=50, so its +s runs the other way.
           "  <road name=\"B\" length=\"40.0\" id=\"2\" junction=\"-1\">\n"
           "    <link><successor elementType=\"road\" elementId=\"1\" contactPoint=\"end\"/></link>\n"
           "    <planView><geometry s=\"0.0\" x=\"90.0\" y=\"0.0\" hdg=\"3.14159265358979\" length=\"40.0\"><line/></geometry></planView>\n"
           "    <lanes>\n"
           "      <laneSection s=\"0.0\">\n"
           "        <center><lane id=\"0\" type=\"none\" level=\"false\"><link/></lane></center>\n"
           "        <left>\n"
           "          <lane id=\"1\" type=\"driving\" level=\"false\"><link><successor id=\"-1\"/></link>"
           "<width sOffset=\"0.0\" a=\"3.5\" b=\"0.0\" c=\"0.0\" d=\"0.0\"/></lane>\n"
           "        </left>\n"
           "        <right>\n"
           "          <lane id=\"-1\" type=\"driving\" level=\"false\"><link/>"
           "<width sOffset=\"0.0\" a=\"3.5\" b=\"0.0\" c=\"0.0\" d=\"0.0\"/></lane>\n"
           "        </right>\n"
           "      </laneSection>\n"
           "    </lanes>\n"
           "  </road>\n"
           "</OpenDRIVE>\n";
}

// ---------------------------------------------------------------------------------------------
// N3 -- one road, three lane sections, with the lane id of the SAME physical lane changing from
// section to section because an extra lane opens and closes closer to the reference line.
//   section 0 (s 0..30):   -1
//   section 1 (s 30..60):  -1 (new, inner)  -2 (== section 0's -1)
//   section 2 (s 60..90):  -1 (== section 1's -2)
// So the lane a route is planned on is -1 at the road's exit, -2 in the middle section and -1
// again at the start: carrying one id across the whole road would name the WRONG lane in the
// middle. Followed by a second road so the plan has a hop and a real exit end.
// ---------------------------------------------------------------------------------------------
std::string RenumberingSectionsXodr()
{
    return std::string(kHeader) +
           "  <road name=\"M\" length=\"90.0\" id=\"1\" junction=\"-1\">\n"
           "    <link><successor elementType=\"road\" elementId=\"2\" contactPoint=\"start\"/></link>\n"
           "    <planView><geometry s=\"0.0\" x=\"0.0\" y=\"0.0\" hdg=\"0.0\" length=\"90.0\"><line/></geometry></planView>\n"
           "    <lanes>\n"
           "      <laneSection s=\"0.0\">\n"
           "        <center><lane id=\"0\" type=\"none\" level=\"false\"><link/></lane></center>\n"
           "        <right>\n"
           "          <lane id=\"-1\" type=\"driving\" level=\"false\"><link><successor id=\"-2\"/></link>"
           "<width sOffset=\"0.0\" a=\"3.5\" b=\"0.0\" c=\"0.0\" d=\"0.0\"/></lane>\n"
           "        </right>\n"
           "      </laneSection>\n"
           "      <laneSection s=\"30.0\">\n"
           "        <center><lane id=\"0\" type=\"none\" level=\"false\"><link/></lane></center>\n"
           "        <right>\n"
           "          <lane id=\"-1\" type=\"driving\" level=\"false\"><link/>"
           "<width sOffset=\"0.0\" a=\"3.5\" b=\"0.0\" c=\"0.0\" d=\"0.0\"/></lane>\n"
           "          <lane id=\"-2\" type=\"driving\" level=\"false\">"
           "<link><predecessor id=\"-1\"/><successor id=\"-1\"/></link>"
           "<width sOffset=\"0.0\" a=\"3.5\" b=\"0.0\" c=\"0.0\" d=\"0.0\"/></lane>\n"
           "        </right>\n"
           "      </laneSection>\n"
           "      <laneSection s=\"60.0\">\n"
           "        <center><lane id=\"0\" type=\"none\" level=\"false\"><link/></lane></center>\n"
           "        <right>\n"
           "          <lane id=\"-1\" type=\"driving\" level=\"false\">"
           "<link><predecessor id=\"-2\"/><successor id=\"-1\"/></link>"
           "<width sOffset=\"0.0\" a=\"3.5\" b=\"0.0\" c=\"0.0\" d=\"0.0\"/></lane>\n"
           "        </right>\n"
           "      </laneSection>\n"
           "    </lanes>\n"
           "  </road>\n"
           "  <road name=\"N\" length=\"20.0\" id=\"2\" junction=\"-1\">\n"
           "    <link><predecessor elementType=\"road\" elementId=\"1\" contactPoint=\"end\"/></link>\n"
           "    <planView><geometry s=\"0.0\" x=\"90.0\" y=\"0.0\" hdg=\"0.0\" length=\"20.0\"><line/></geometry></planView>\n"
           "    <lanes>\n"
           "      <laneSection s=\"0.0\">\n"
           "        <center><lane id=\"0\" type=\"none\" level=\"false\"><link/></lane></center>\n"
           "        <right>\n"
           "          <lane id=\"-1\" type=\"driving\" level=\"false\"><link><predecessor id=\"-1\"/></link>"
           "<width sOffset=\"0.0\" a=\"3.5\" b=\"0.0\" c=\"0.0\" d=\"0.0\"/></lane>\n"
           "        </right>\n"
           "      </laneSection>\n"
           "    </lanes>\n"
           "  </road>\n"
           "</OpenDRIVE>\n";
}

using gt_esmini::BuildRouteLanePlan;
using gt_esmini::RouteLanePlan;
using gt_esmini::osi::BuildOsiRouteInto;
using gt_esmini::osi::ComputeRouteProgress;
using gt_esmini::osi::ExpandRouteLanePlan;
using gt_esmini::osi::MakeRouteSignature;
using gt_esmini::osi::RouteExpansionStart;
using gt_esmini::osi::RouteSectionSegment;

}  // namespace

// ---------------------------------------------------------------------------
// band -> lane section expansion
// ---------------------------------------------------------------------------

TEST(RouteToOsiRoute, ExpandsOneSegmentPerLaneSectionInTravelOrder)
{
    ASSERT_FALSE(R2rRepoRoot().empty()) << "GT_ODR_REPO_ROOT not defined";
    ASSERT_TRUE(WriteAndLoad("r2r_forward.xodr", ForwardPairXodr()));

    Route route;
    ASSERT_EQ(AddWp(route, 1, -1, 10.0), 0);
    ASSERT_EQ(AddWp(route, 2, -1, 30.0), 0);
    const RouteLanePlan plan = BuildRouteLanePlan(route);
    ASSERT_TRUE(plan.valid) << plan.diagnostic;

    Position ego;
    ego.SetLanePos(1, -1, 10.0, 0.0);

    const std::vector<RouteSectionSegment> segs = ExpandRouteLanePlan(route, ego, plan, RouteExpansionStart::EgoPosition);

    // One lane section per road here, so one segment per road, in route order.
    ASSERT_EQ(segs.size(), 2u);
    EXPECT_EQ(segs[0].road_id, 1u);
    EXPECT_EQ(segs[0].section_idx, 0u);
    EXPECT_EQ(segs[1].road_id, 2u);
    ASSERT_EQ(segs[0].lanes.size(), 1u);
    EXPECT_EQ(segs[0].lanes[0], -1);

    // First segment starts at the ego, last ends at the destination waypoint.
    EXPECT_NEAR(segs[0].start_s, 10.0, 1e-9);
    EXPECT_NEAR(segs[0].end_s, 50.0, 1e-9) << "road A runs to its full length";
    EXPECT_NEAR(segs[1].start_s, 0.0, 1e-9);
    EXPECT_NEAR(segs[1].end_s, 30.0, 1e-9) << "clipped to the destination waypoint s";

    Position::GetOpenDrive()->Clear();
}

TEST(RouteToOsiRoute, DropsRoadsTheEgoHasAlreadyLeft)
{
    ASSERT_TRUE(WriteAndLoad("r2r_forward.xodr", ForwardPairXodr()));

    Route route;
    ASSERT_EQ(AddWp(route, 1, -1, 10.0), 0);
    ASSERT_EQ(AddWp(route, 2, -1, 30.0), 0);
    const RouteLanePlan plan = BuildRouteLanePlan(route);
    ASSERT_TRUE(plan.valid) << plan.diagnostic;

    Position ego;
    ego.SetLanePos(2, -1, 5.0, 0.0);  // already on the second road

    const std::vector<RouteSectionSegment> segs = ExpandRouteLanePlan(route, ego, plan, RouteExpansionStart::EgoPosition);
    ASSERT_EQ(segs.size(), 1u);
    EXPECT_EQ(segs[0].road_id, 2u);
    EXPECT_NEAR(segs[0].start_s, 5.0, 1e-9);
    EXPECT_NEAR(segs[0].end_s, 30.0, 1e-9);

    Position::GetOpenDrive()->Clear();
}

// THE trap of 2-5: start_s > end_s means "drive this segment against the reference line".
// Asserting only the forward case would pass just as well for code that always emits
// ascending s, so both polarities have to come out of the same real route.
TEST(RouteToOsiRoute, SegmentDirectionHasBothPolarities)
{
    ASSERT_TRUE(WriteAndLoad("r2r_head_to_head.xodr", HeadToHeadPairXodr()));

    Route route;
    ASSERT_EQ(AddWp(route, 1, -1, 5.0), 0);
    ASSERT_EQ(AddWp(route, 2, 1, 10.0), 0);
    const RouteLanePlan plan = BuildRouteLanePlan(route);
    ASSERT_TRUE(plan.valid) << plan.diagnostic;
    ASSERT_EQ(plan.bands.size(), 2u);
    EXPECT_TRUE(plan.bands[0].exit_at_road_end) << "road A is left at its end -- travel is +s";
    EXPECT_FALSE(plan.bands[1].exit_at_road_end) << "road B is entered at ITS end -- travel is -s";

    Position ego;
    ego.SetLanePos(1, -1, 5.0, 0.0);

    const std::vector<RouteSectionSegment> segs = ExpandRouteLanePlan(route, ego, plan, RouteExpansionStart::EgoPosition);
    ASSERT_EQ(segs.size(), 2u);

    // +s road: ascending.
    EXPECT_EQ(segs[0].road_id, 1u);
    EXPECT_LT(segs[0].start_s, segs[0].end_s);
    EXPECT_NEAR(segs[0].start_s, 5.0, 1e-9);
    EXPECT_NEAR(segs[0].end_s, 50.0, 1e-9);

    // -s road: DESCENDING. Its lane is the left one, which is what a -s traveller occupies.
    EXPECT_EQ(segs[1].road_id, 2u);
    EXPECT_GT(segs[1].start_s, segs[1].end_s) << "travelling against the reference line must invert the pair";
    EXPECT_NEAR(segs[1].start_s, 40.0, 1e-9) << "entered at road B's own end";
    EXPECT_NEAR(segs[1].end_s, 10.0, 1e-9) << "down to the destination waypoint s";
    ASSERT_EQ(segs[1].lanes.size(), 1u);
    EXPECT_EQ(segs[1].lanes[0], 1);

    Position::GetOpenDrive()->Clear();
}

// The bands carry lane ids evaluated at the road's EXIT end only. Re-resolving them per lane
// section is the difference between naming the route's lane and naming whatever lane happens to
// wear that id there.
TEST(RouteToOsiRoute, LaneIdsAreReResolvedPerLaneSection)
{
    ASSERT_TRUE(WriteAndLoad("r2r_renumber.xodr", RenumberingSectionsXodr()));

    Route route;
    ASSERT_EQ(AddWp(route, 1, -1, 5.0), 0);
    ASSERT_EQ(AddWp(route, 2, -1, 15.0), 0);
    const RouteLanePlan plan = BuildRouteLanePlan(route);
    ASSERT_TRUE(plan.valid) << plan.diagnostic;
    ASSERT_EQ(plan.bands.size(), 2u);
    ASSERT_EQ(plan.bands[0].lanes.size(), 1u);
    EXPECT_EQ(plan.bands[0].lanes[0], -1) << "at road M's exit (section 2) the route lane is -1";

    Position ego;
    ego.SetLanePos(1, -1, 5.0, 0.0);

    const std::vector<RouteSectionSegment> segs = ExpandRouteLanePlan(route, ego, plan, RouteExpansionStart::EgoPosition);
    ASSERT_EQ(segs.size(), 4u) << "3 lane sections on road M + 1 on road N";

    EXPECT_EQ(segs[0].section_idx, 0u);
    ASSERT_EQ(segs[0].lanes.size(), 1u);
    EXPECT_EQ(segs[0].lanes[0], -1);

    EXPECT_EQ(segs[1].section_idx, 1u);
    ASSERT_EQ(segs[1].lanes.size(), 1u);
    EXPECT_EQ(segs[1].lanes[0], -2) << "the SAME physical lane is -2 in the middle section";

    EXPECT_EQ(segs[2].section_idx, 2u);
    ASSERT_EQ(segs[2].lanes.size(), 1u);
    EXPECT_EQ(segs[2].lanes[0], -1);

    EXPECT_EQ(segs[3].road_id, 2u);

    Position::GetOpenDrive()->Clear();
}

// ---------------------------------------------------------------------------
// route identity / caching key
// ---------------------------------------------------------------------------

TEST(RouteToOsiRoute, RouteSignatureSurvivesCloningAndSeparatesRoutes)
{
    ASSERT_TRUE(WriteAndLoad("r2r_forward.xodr", ForwardPairXodr()));

    // HEAP, not the stack: Position::SetRoute() takes the pointer and ~Position()
    // deletes it unconditionally (GT_RoadManager.cpp Position::~Position). Handing it a
    // stack Route corrupts the heap on scope exit -- measured, not assumed.
    Route* owned = new Route;
    ASSERT_EQ(AddWp(*owned, 1, -1, 10.0), 0);
    ASSERT_EQ(AddWp(*owned, 2, -1, 30.0), 0);

    Position ego;
    ego.SetLanePos(1, -1, 10.0, 0.0);
    ASSERT_EQ(ego.SetRoute(owned), 0);  // ego now owns `owned`

    // The trap of design 5: CopyRoute allocates a NEW Route, so pointer identity says
    // "different route" on every single frame while the route has not changed at all.
    Position clone;
    clone.Duplicate(ego);
    clone.CopyRoute(ego);
    ASSERT_NE(clone.GetRoute(), nullptr);
    EXPECT_NE(clone.GetRoute(), ego.GetRoute()) << "the clone must be a different object -- that is the trap";
    EXPECT_TRUE(MakeRouteSignature(*clone.GetRoute()) == MakeRouteSignature(*ego.GetRoute()));

    const Route& route = *owned;
    Route        other;
    ASSERT_EQ(AddWp(other, 1, -1, 10.0), 0);
    ASSERT_EQ(AddWp(other, 2, -2, 30.0), 0);  // different destination LANE
    EXPECT_TRUE(MakeRouteSignature(other) != MakeRouteSignature(route));

    Route further;
    ASSERT_EQ(AddWp(further, 1, -1, 10.0), 0);
    ASSERT_EQ(AddWp(further, 2, -1, 35.0), 0);  // different destination S
    EXPECT_TRUE(MakeRouteSignature(further) != MakeRouteSignature(route));

    Position::GetOpenDrive()->Clear();
}

// ---------------------------------------------------------------------------
// osi3::Route
// ---------------------------------------------------------------------------

TEST(RouteToOsiRoute, EveryLogicalLaneIdInTheRouteExistsInTheGroundTruth)
{
    ASSERT_TRUE(WriteAndLoad("r2r_renumber.xodr", RenumberingSectionsXodr()));

    osi3::GroundTruth                 gt;
    gt_esmini::osi::LogicalLaneIndex  index;
    gt_esmini::osi::BuildOsiLogicalLanesInto(Position::GetOpenDrive(), &gt, &index);
    ASSERT_GT(gt.logical_lane_size(), 0);

    Route route;
    ASSERT_EQ(AddWp(route, 1, -1, 5.0), 0);
    ASSERT_EQ(AddWp(route, 2, -1, 15.0), 0);
    const RouteLanePlan plan = BuildRouteLanePlan(route);
    ASSERT_TRUE(plan.valid) << plan.diagnostic;

    Position ego;
    ego.SetLanePos(1, -1, 5.0, 0.0);
    const std::vector<RouteSectionSegment> segs = ExpandRouteLanePlan(route, ego, plan, RouteExpansionStart::EgoPosition);

    osi3::Route out;
    BuildOsiRouteInto(segs, index, 7, &out);

    ASSERT_TRUE(out.has_route_id());
    EXPECT_EQ(out.route_id().value(), 7u);
    ASSERT_EQ(out.route_segment_size(), static_cast<int>(segs.size()));

    std::set<std::uint64_t> emitted;
    for (const osi3::LogicalLane& ll : gt.logical_lane())
    {
        emitted.insert(ll.id().value());
    }

    int n_lane_segments = 0;
    for (int i = 0; i < out.route_segment_size(); i++)
    {
        const osi3::Route_RouteSegment& rs = out.route_segment(i);
        EXPECT_GT(rs.lane_segment_size(), 0) << "an empty RouteSegment claims drivable space with no lane in it";
        for (const osi3::Route_LogicalLaneSegment& ls : rs.lane_segment())
        {
            n_lane_segments++;
            EXPECT_EQ(emitted.count(ls.logical_lane_id().value()), 1u)
                << "route references logical lane " << ls.logical_lane_id().value() << " which was never emitted";
            // The s pair is copied through unchanged -- the travel-order convention is
            // established in the expansion and must not be re-normalised here.
            EXPECT_NEAR(ls.start_s(), segs[static_cast<size_t>(i)].start_s, 1e-9);
            EXPECT_NEAR(ls.end_s(), segs[static_cast<size_t>(i)].end_s, 1e-9);
        }
    }
    EXPECT_EQ(n_lane_segments, 4);

    Position::GetOpenDrive()->Clear();
}

// The mechanism behind "GT_OSI_LOGICAL_LANE=0 -> no route": with no logical lanes there is
// nothing to reference, and the builder must drop the segments rather than emit ids that
// resolve to nothing. (The env flag itself latches per process and is measured by
// scripts/probe_hvd_route.py, not here -- same reason as test_OsiLogicalLane.cpp's note.)
TEST(RouteToOsiRoute, EmptyLogicalLaneIndexProducesNoSegments)
{
    ASSERT_TRUE(WriteAndLoad("r2r_forward.xodr", ForwardPairXodr()));

    Route route;
    ASSERT_EQ(AddWp(route, 1, -1, 10.0), 0);
    ASSERT_EQ(AddWp(route, 2, -1, 30.0), 0);
    const RouteLanePlan plan = BuildRouteLanePlan(route);
    ASSERT_TRUE(plan.valid) << plan.diagnostic;

    Position ego;
    ego.SetLanePos(1, -1, 10.0, 0.0);
    const std::vector<RouteSectionSegment> segs = ExpandRouteLanePlan(route, ego, plan, RouteExpansionStart::EgoPosition);
    ASSERT_FALSE(segs.empty());

    const gt_esmini::osi::LogicalLaneIndex empty;
    osi3::Route                            out;
    BuildOsiRouteInto(segs, empty, 1, &out);
    EXPECT_EQ(out.route_segment_size(), 0);
    EXPECT_TRUE(out.has_route_id()) << "the route message still exists -- 'no segments' and 'no route' differ";

    Position::GetOpenDrive()->Clear();
}

// ---------------------------------------------------------------------------
// L2 route progress
// ---------------------------------------------------------------------------

// The negative control of design 9-9. Position::GetRouteS() is only written by SetRoute() and
// TeleportTo(), so a vehicle moved the way every GT driver moves it -- SetInertiaPos -- leaves it
// frozen at its assignment value. Showing s_along_route rise is not enough on its own: it has to
// be shown next to the field that does NOT, or the next person will reach for GetRouteS() again.
TEST(RouteToOsiRoute, ProgressAdvancesWhileGetRouteSFreezesUnderInertialDriving)
{
    ASSERT_TRUE(WriteAndLoad("r2r_forward.xodr", ForwardPairXodr()));

    // Heap-allocated for the same ownership reason as the signature test above.
    Route* owned = new Route;
    ASSERT_EQ(AddWp(*owned, 1, -1, 5.0), 0);
    ASSERT_EQ(AddWp(*owned, 2, -1, 30.0), 0);
    const Route&        route = *owned;
    const RouteLanePlan plan  = BuildRouteLanePlan(route);
    ASSERT_TRUE(plan.valid) << plan.diagnostic;

    Position ego;
    ego.SetLanePos(1, -1, 5.0, 0.0);
    ASSERT_EQ(ego.SetRoute(owned), 0);  // ego now owns `owned`
    const double route_s_at_assignment = ego.GetRouteS();

    // Lane -1 of a 3.5 m-wide right side sits at y = -1.75 on this straight road, so the
    // vehicle is moved exactly the way every GT driver moves it: SetInertiaPos, in world
    // coordinates, with no route bookkeeping of its own.
    std::vector<double> progress_samples;
    std::vector<double> route_s_samples;
    double              route_length = 0.0;
    for (double x = 5.0; x <= 75.0; x += 5.0)
    {
        ego.SetInertiaPos(x, -1.75, 0.0);
        const std::vector<RouteSectionSegment> segs =
            ExpandRouteLanePlan(route, ego, plan, RouteExpansionStart::RouteStart);
        const gt_esmini::osi::RouteProgress p = ComputeRouteProgress(segs, ego);
        ASSERT_TRUE(p.on_route) << "x=" << x << " road=" << ego.GetTrackId() << " s=" << ego.GetS();
        progress_samples.push_back(p.s_along_route);
        route_s_samples.push_back(ego.GetRouteS());
        route_length = p.route_length;
    }

    ASSERT_GE(progress_samples.size(), 5u);
    // Route: road A from s=5 to its end (45 m) + road B from s=0 to s=30 (30 m) = 75 m.
    EXPECT_NEAR(route_length, 75.0, 1e-6);
    EXPECT_NEAR(progress_samples.front(), 0.0, 1e-6) << "the first sample sits at the route start";
    for (size_t i = 1; i < progress_samples.size(); i++)
    {
        EXPECT_GT(progress_samples[i], progress_samples[i - 1])
            << "s_along_route must strictly increase while driving forward (sample " << i << ")";
    }
    EXPECT_NEAR(progress_samples.back(), 70.0, 1e-6) << "x=75 is 70 m along the route";

    // The negative control. Every sample above came from the SAME drive, so this is not a
    // separate experiment: the field GT does not use stayed where it was assigned while the
    // field GT does use moved 70 m.
    for (const double s_frozen : route_s_samples)
    {
        EXPECT_DOUBLE_EQ(s_frozen, route_s_at_assignment)
            << "GetRouteS() is frozen for an inertially driven vehicle -- that is why L2 exists";
    }

    Position::GetOpenDrive()->Clear();
}

TEST(RouteToOsiRoute, ProgressReportsOffRouteOutsideThePlan)
{
    ASSERT_TRUE(WriteAndLoad("r2r_forward.xodr", ForwardPairXodr()));

    Route route;
    ASSERT_EQ(AddWp(route, 1, -1, 10.0), 0);
    ASSERT_EQ(AddWp(route, 2, -1, 30.0), 0);
    const RouteLanePlan plan = BuildRouteLanePlan(route);
    ASSERT_TRUE(plan.valid) << plan.diagnostic;

    Position ego;
    ego.SetLanePos(9, -1, 10.0, 0.0);  // the unconnected road

    const gt_esmini::osi::RouteProgress p = ComputeRouteProgress(ExpandRouteLanePlan(route, ego, plan, RouteExpansionStart::EgoPosition), ego);
    EXPECT_FALSE(p.on_route);
    EXPECT_DOUBLE_EQ(p.s_along_route, -1.0) << "0.0 off-route would be indistinguishable from 'at the start'";

    Position::GetOpenDrive()->Clear();
}
