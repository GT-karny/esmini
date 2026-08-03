// test_RouteLanePlan.cpp -- RouteLanePlan unit tests.
//
// Synthetic xodr networks are written to a temp file and loaded through the real parser
// (roadmanager::Position::GetOpenDrive()->LoadOpenDriveFile), mirroring test_OdrVirtualJunction.cpp's
// pattern. Routes are built the same way the rest of the codebase does: roadmanager::Route +
// AddWaypoint, relying on AddWaypoint's own RoadPath-driven intermediate-waypoint insertion
// (confirmed behavior -- it auto-inserts a junction's connecting road as a waypoint, but never
// validates that the requested target LANE is actually reachable; that gap is exactly what
// RouteLanePlan exists to close).
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "RoadManager.hpp"
#include "gt_esmini/control/virtualdriver/RouteLanePlan.hpp"

using namespace roadmanager;
using namespace gt_esmini;

namespace
{

std::string RepoRoot()
{
#ifdef GT_ODR_REPO_ROOT
    return std::string(GT_ODR_REPO_ROOT);
#else
    return std::string();
#endif
}

// Scratch dir + temp writer (mirrors test_OdrVirtualJunction.cpp's VjScratchDir/WriteVjTemp).
std::filesystem::path RlpScratchDir()
{
    std::error_code   ec;
    const std::string root = RepoRoot();
    if (!root.empty())
    {
        std::filesystem::path cand = std::filesystem::path(root) / "build" / "rlp_tests";
        std::filesystem::create_directories(cand, ec);
        if (!ec && std::filesystem::is_directory(cand))
        {
            return cand;
        }
    }
    std::filesystem::path tmp = std::filesystem::temp_directory_path(ec) / "rlp_tests";
    std::filesystem::create_directories(tmp, ec);
    return tmp;
}

std::string WriteRlpTemp(const std::string& name, const std::string& content)
{
    const std::filesystem::path p = RlpScratchDir() / name;
    std::ofstream                out(p, std::ios::binary);
    out << content;
    out.close();
    return p.string();
}

bool LoadXodr(const std::string& abs_path)
{
    return Position::GetOpenDrive()->LoadOpenDriveFile(abs_path.c_str(), true);
}

// Builds a Route waypoint the way the rest of the codebase does for route construction: a
// default-constructed Position, SetLanePos to the road/lane/s, then an explicit +s heading (the
// route-building precedent in test_OdrVirtualJunction.cpp's MakeVjRoute helper -- every road in
// every network below is authored assuming +s travel through its successor links).
Position MakeWaypoint(id_t road_id, int lane_id, double s)
{
    Position wp;
    wp.SetLanePos(road_id, lane_id, s, 0.0);
    wp.SetHeadingRelative(0.0);
    return wp;
}

// ---------------------------------------------------------------------------------------------
// Network 1: two directly-linked roads (no junction), 2 driving lanes each, lanes carried
// through in parallel (-1 -> -1, -2 -> -2). Requirement 1: a target lane on road 2 must narrow
// road 1's band to just the one lane that actually leads there, not "every driving lane".
// ---------------------------------------------------------------------------------------------
std::string TwoRoadStraightXodr()
{
    return std::string("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n") +
           "<OpenDRIVE>\n"
           "  <header revMajor=\"1\" revMinor=\"7\" name=\"rlp_straight\" version=\"1.00\" date=\"2026-08-02T00:00:00\""
           " north=\"0.0\" south=\"0.0\" east=\"0.0\" west=\"0.0\"/>\n"
           "  <road name=\"roadA\" length=\"50.0\" id=\"1\" junction=\"-1\">\n"
           "    <link><successor elementType=\"road\" elementId=\"2\" contactPoint=\"start\"/></link>\n"
           "    <planView>\n"
           "      <geometry s=\"0.0\" x=\"0.0\" y=\"0.0\" hdg=\"0.0\" length=\"50.0\"><line/></geometry>\n"
           "    </planView>\n"
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
           "  <road name=\"roadB\" length=\"50.0\" id=\"2\" junction=\"-1\">\n"
           "    <link><predecessor elementType=\"road\" elementId=\"1\" contactPoint=\"end\"/></link>\n"
           "    <planView>\n"
           "      <geometry s=\"0.0\" x=\"50.0\" y=\"0.0\" hdg=\"0.0\" length=\"50.0\"><line/></geometry>\n"
           "    </planView>\n"
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
           "</OpenDRIVE>\n";
}

// ---------------------------------------------------------------------------------------------
// Network 2: armA(1) --junction 900--> connector(2) --> armB(3), plus a standalone road(4) that
// is on the loaded network but never on the route (for the off_plan_road case). armA carries 2
// driving lanes (-1, -2); the junction's ONE authored connection links only armA's lane -1
// through to the connector. This is the CORE case (requirement 2): armA's band must narrow to
// {-1}, not {-1, -2}, purely from the junction's laneLink data.
// ---------------------------------------------------------------------------------------------
std::string ThreeRoadJunctionXodr()
{
    return std::string("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n") +
           "<OpenDRIVE>\n"
           "  <header revMajor=\"1\" revMinor=\"7\" name=\"rlp_junction\" version=\"1.00\" date=\"2026-08-02T00:00:00\""
           " north=\"0.0\" south=\"0.0\" east=\"0.0\" west=\"0.0\"/>\n"
           "  <road name=\"armA\" length=\"50.0\" id=\"1\" junction=\"-1\">\n"
           "    <link><successor elementType=\"junction\" elementId=\"900\"/></link>\n"
           "    <planView>\n"
           "      <geometry s=\"0.0\" x=\"0.0\" y=\"0.0\" hdg=\"0.0\" length=\"50.0\"><line/></geometry>\n"
           "    </planView>\n"
           "    <lanes>\n"
           "      <laneSection s=\"0.0\">\n"
           "        <center><lane id=\"0\" type=\"none\" level=\"false\"><link/></lane></center>\n"
           "        <right>\n"
           "          <lane id=\"-1\" type=\"driving\" level=\"false\"><link/>"
           "<width sOffset=\"0.0\" a=\"3.5\" b=\"0.0\" c=\"0.0\" d=\"0.0\"/></lane>\n"
           "          <lane id=\"-2\" type=\"driving\" level=\"false\"><link/>"
           "<width sOffset=\"0.0\" a=\"3.5\" b=\"0.0\" c=\"0.0\" d=\"0.0\"/></lane>\n"
           "        </right>\n"
           "      </laneSection>\n"
           "    </lanes>\n"
           "  </road>\n"
           "  <road name=\"connector\" length=\"20.0\" id=\"2\" junction=\"900\">\n"
           "    <link>\n"
           "      <predecessor elementType=\"road\" elementId=\"1\" contactPoint=\"end\"/>\n"
           "      <successor elementType=\"road\" elementId=\"3\" contactPoint=\"start\"/>\n"
           "    </link>\n"
           "    <planView>\n"
           "      <geometry s=\"0.0\" x=\"50.0\" y=\"0.0\" hdg=\"0.0\" length=\"20.0\"><line/></geometry>\n"
           "    </planView>\n"
           "    <lanes>\n"
           "      <laneSection s=\"0.0\">\n"
           "        <center><lane id=\"0\" type=\"none\" level=\"false\"><link/></lane></center>\n"
           "        <right>\n"
           "          <lane id=\"-1\" type=\"driving\" level=\"false\"><link><predecessor id=\"-1\"/><successor id=\"-1\"/></link>"
           "<width sOffset=\"0.0\" a=\"3.5\" b=\"0.0\" c=\"0.0\" d=\"0.0\"/></lane>\n"
           "        </right>\n"
           "      </laneSection>\n"
           "    </lanes>\n"
           "  </road>\n"
           "  <road name=\"armB\" length=\"50.0\" id=\"3\" junction=\"-1\">\n"
           "    <link><predecessor elementType=\"junction\" elementId=\"900\"/></link>\n"
           "    <planView>\n"
           "      <geometry s=\"0.0\" x=\"70.0\" y=\"0.0\" hdg=\"0.0\" length=\"50.0\"><line/></geometry>\n"
           "    </planView>\n"
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
           "  <road name=\"standalone\" length=\"30.0\" id=\"4\" junction=\"-1\">\n"
           "    <link/>\n"
           "    <planView>\n"
           "      <geometry s=\"0.0\" x=\"0.0\" y=\"100.0\" hdg=\"0.0\" length=\"30.0\"><line/></geometry>\n"
           "    </planView>\n"
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
           "  <junction name=\"j900\" id=\"900\">\n"
           "    <connection id=\"0\" incomingRoad=\"1\" connectingRoad=\"2\" contactPoint=\"start\">\n"
           "      <laneLink from=\"-1\" to=\"-1\"/>\n"
           "    </connection>\n"
           "  </junction>\n"
           "</OpenDRIVE>\n";
}

// ---------------------------------------------------------------------------------------------
// Network 3: two directly-linked roads (no junction), ONE driving lane each, and that lane
// carries NO <link> at all (zero lane-level connectivity, even though the road-level link is
// intact). Requirement 6's "not a single lane connects" case.
// ---------------------------------------------------------------------------------------------
std::string ZeroConnectivityXodr()
{
    return std::string("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n") +
           "<OpenDRIVE>\n"
           "  <header revMajor=\"1\" revMinor=\"7\" name=\"rlp_zero_connectivity\" version=\"1.00\" date=\"2026-08-02T00:00:00\""
           " north=\"0.0\" south=\"0.0\" east=\"0.0\" west=\"0.0\"/>\n"
           "  <road name=\"roadA\" length=\"50.0\" id=\"1\" junction=\"-1\">\n"
           "    <link><successor elementType=\"road\" elementId=\"2\" contactPoint=\"start\"/></link>\n"
           "    <planView>\n"
           "      <geometry s=\"0.0\" x=\"0.0\" y=\"0.0\" hdg=\"0.0\" length=\"50.0\"><line/></geometry>\n"
           "    </planView>\n"
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
           "  <road name=\"roadB\" length=\"50.0\" id=\"2\" junction=\"-1\">\n"
           "    <link><predecessor elementType=\"road\" elementId=\"1\" contactPoint=\"end\"/></link>\n"
           "    <planView>\n"
           "      <geometry s=\"0.0\" x=\"50.0\" y=\"0.0\" hdg=\"0.0\" length=\"50.0\"><line/></geometry>\n"
           "    </planView>\n"
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

}  // namespace

// ---------------------------------------------------------------------------------------------
// Requirement 1: straight two-road network, no junction, multiple driving lanes -- the backward
// propagation from the target lane on road 2 must narrow road 1's band to just the connecting
// lane, not every driving lane on road 1.
// ---------------------------------------------------------------------------------------------
TEST(RouteLanePlan, StraightTwoRoadNarrowsToConnectingLane)
{
    ASSERT_FALSE(RepoRoot().empty()) << "GT_ODR_REPO_ROOT not defined";
    ASSERT_TRUE(LoadXodr(WriteRlpTemp("rlp_straight.xodr", TwoRoadStraightXodr())));

    Route    route;
    Position wp0 = MakeWaypoint(1, -1, 10.0);
    Position wp1 = MakeWaypoint(2, -1, 40.0);
    ASSERT_EQ(route.AddWaypoint(wp0), 0);
    ASSERT_EQ(route.AddWaypoint(wp1), 0);
    ASSERT_TRUE(route.IsValid());

    const RouteLanePlan plan = BuildRouteLanePlan(route);

    ASSERT_TRUE(plan.valid) << "diagnostic: " << plan.diagnostic;
    EXPECT_TRUE(plan.diagnostic.empty());
    ASSERT_EQ(plan.bands.size(), 2u);
    EXPECT_EQ(plan.bands[0].road_id, 1u);
    ASSERT_EQ(plan.bands[0].lanes.size(), 1u) << "road 1 has 2 driving lanes but only -1 leads to the requested target lane";
    EXPECT_EQ(plan.bands[0].lanes[0], -1);
    EXPECT_EQ(plan.bands[1].road_id, 2u);
    ASSERT_EQ(plan.bands[1].lanes.size(), 1u);
    EXPECT_EQ(plan.bands[1].lanes[0], -1);

    Position::GetOpenDrive()->Clear();
}

// ---------------------------------------------------------------------------------------------
// Requirement 2 (core): armA has 2 driving lanes; the junction only links armA's lane -1 through
// to the connecting road. armA's band must come out as {-1}, not {-1, -2}.
// ---------------------------------------------------------------------------------------------
TEST(RouteLanePlan, JunctionArmNarrowsToSingleConnectingLane)
{
    ASSERT_FALSE(RepoRoot().empty());
    ASSERT_TRUE(LoadXodr(WriteRlpTemp("rlp_junction.xodr", ThreeRoadJunctionXodr())));

    Route    route;
    Position wp0 = MakeWaypoint(1, -1, 10.0);
    Position wp1 = MakeWaypoint(3, -1, 40.0);
    ASSERT_EQ(route.AddWaypoint(wp0), 0);
    ASSERT_EQ(route.AddWaypoint(wp1), 0);
    ASSERT_TRUE(route.IsValid());

    const RouteLanePlan plan = BuildRouteLanePlan(route);

    ASSERT_TRUE(plan.valid) << "diagnostic: " << plan.diagnostic;
    EXPECT_FALSE(plan.rerouted) << "the direct skeleton should resolve cleanly without needing the reroute recovery";
    ASSERT_EQ(plan.bands.size(), 3u) << "armA(1), connector(2), armB(3)";
    EXPECT_EQ(plan.bands[0].road_id, 1u);
    ASSERT_EQ(plan.bands[0].lanes.size(), 1u) << "armA has 2 driving lanes but the junction only links -1 through";
    EXPECT_EQ(plan.bands[0].lanes[0], -1);
    EXPECT_EQ(plan.bands[1].road_id, 2u);
    ASSERT_EQ(plan.bands[1].lanes.size(), 1u);
    EXPECT_EQ(plan.bands[1].lanes[0], -1);
    EXPECT_EQ(plan.bands[2].road_id, 3u);
    ASSERT_EQ(plan.bands[2].lanes.size(), 1u);
    EXPECT_EQ(plan.bands[2].lanes[0], -1);

    Position::GetOpenDrive()->Clear();
}

// ---------------------------------------------------------------------------------------------
// Requirement 3: EvaluateRouteLaneStatus on an ego sitting in armA's OTHER lane (-2, excluded
// from the plan above) must report on_target_lane == false and the correct distance to the
// junction entry (armA's band connects at s = road length = 50).
// ---------------------------------------------------------------------------------------------
TEST(RouteLaneStatus, OffTargetLaneReportsFalseWithDistance)
{
    ASSERT_FALSE(RepoRoot().empty());
    ASSERT_TRUE(LoadXodr(WriteRlpTemp("rlp_junction_status.xodr", ThreeRoadJunctionXodr())));

    Route    route;
    Position wp0 = MakeWaypoint(1, -1, 10.0);
    Position wp1 = MakeWaypoint(3, -1, 40.0);
    ASSERT_EQ(route.AddWaypoint(wp0), 0);
    ASSERT_EQ(route.AddWaypoint(wp1), 0);
    const RouteLanePlan plan = BuildRouteLanePlan(route);
    ASSERT_TRUE(plan.valid) << "diagnostic: " << plan.diagnostic;

    Position ego;
    ego.SetLanePos(1, -2, 20.0, 0.0);  // armA, the lane EXCLUDED from the plan's band
    const RouteLaneStatus status = EvaluateRouteLaneStatus(plan, ego);

    EXPECT_TRUE(status.valid);
    EXPECT_TRUE(status.reason.empty());
    EXPECT_EQ(status.road_id, 1u);
    EXPECT_EQ(status.ego_lane_raw, -2);
    EXPECT_EQ(status.ego_lane, -2);  // single lane section -> normalization is identity
    ASSERT_EQ(status.target_lanes.size(), 1u);
    EXPECT_EQ(status.target_lanes[0], -1);
    EXPECT_FALSE(status.on_target_lane);
    EXPECT_NEAR(status.dist_to_connection, 30.0, 1e-6) << "50 (road length, the junction entry) - 20 (ego s)";

    Position::GetOpenDrive()->Clear();
}

// ---------------------------------------------------------------------------------------------
// Requirement 4: an invalid (default) plan must report reason == "no_plan", without touching
// roadmanager state at all -- no xodr load needed for this one.
// ---------------------------------------------------------------------------------------------
TEST(RouteLaneStatus, InvalidPlanReportsNoPlan)
{
    RouteLanePlan plan;  // default-constructed: valid == false
    Position      pos;

    const RouteLaneStatus status = EvaluateRouteLaneStatus(plan, pos);

    EXPECT_FALSE(status.valid);
    EXPECT_EQ(status.reason, "no_plan");
}

// ---------------------------------------------------------------------------------------------
// Requirement 5: ego sitting on a road that IS on the loaded network but is NOT one of the
// plan's bands must report reason == "off_plan_road".
// ---------------------------------------------------------------------------------------------
TEST(RouteLaneStatus, EgoOffRouteRoadReportsOffPlanRoad)
{
    ASSERT_FALSE(RepoRoot().empty());
    ASSERT_TRUE(LoadXodr(WriteRlpTemp("rlp_junction_offplan.xodr", ThreeRoadJunctionXodr())));

    Route    route;
    Position wp0 = MakeWaypoint(1, -1, 10.0);
    Position wp1 = MakeWaypoint(3, -1, 40.0);
    ASSERT_EQ(route.AddWaypoint(wp0), 0);
    ASSERT_EQ(route.AddWaypoint(wp1), 0);
    const RouteLanePlan plan = BuildRouteLanePlan(route);
    ASSERT_TRUE(plan.valid) << "diagnostic: " << plan.diagnostic;

    Position ego;
    ego.SetLanePos(4, -1, 10.0, 0.0);  // the standalone road, never part of the route
    const RouteLaneStatus status = EvaluateRouteLaneStatus(plan, ego);

    EXPECT_FALSE(status.valid);
    EXPECT_EQ(status.reason, "off_plan_road");
    EXPECT_EQ(status.road_id, 4u);

    Position::GetOpenDrive()->Clear();
}

// ---------------------------------------------------------------------------------------------
// Requirement 6: road 1's only driving lane carries NO lane-level link at all, even though the
// road-level link to road 2 is intact -- RoadPath::Calculate (and hence Route::AddWaypoint)
// never notices this, because a 2-road route's only hop lands directly on the target road, the
// ONE case its own CheckRoad-based lane validation skips (RoadManager.cpp RoadPath::Calculate,
// `nextRoad == targetRoad` fast path). BuildRouteLanePlan's own backward propagation DOES catch
// it (road 1's band comes out empty -> "lane_discontinuity", discontinuity_road_id == 1), but
// per the design, exactly one LaneIndependentRouter reroute is then attempted before giving up.
// Traced through LaneIndependentRouter::GetConnectingLanes/CalculatePath: since road 1's lane
// truly has zero laneLinks, the reroute's very first expansion also finds nothing (connecting
// road candidates require a non-zero Road::GetConnectingLaneId, same underlying data), so
// CalculatePath returns empty too. Per the design's step 7 ("空だったら diagnostic=
// "reroute_failed""), the diagnostic that survives to the caller is "reroute_failed", not
// "lane_discontinuity" -- "lane_discontinuity" only survives as a FINAL diagnostic if the
// reroute finds a path that itself hits a (different) gap on retry, which cannot happen here
// (there is no alternate lane or alternate road for the router to find at all). This test
// pins that end-to-end outcome, and additionally confirms the lane_discontinuity detection
// itself fired (discontinuity_road_id is left populated as a diagnostic breadcrumb even though
// the top-level diagnostic string was overwritten -- see RouteLanePlan.hpp/.cpp).
// ---------------------------------------------------------------------------------------------
TEST(RouteLanePlan, ZeroLaneConnectivityExhaustsRerouteAndReportsRerouteFailed)
{
    ASSERT_FALSE(RepoRoot().empty());
    ASSERT_TRUE(LoadXodr(WriteRlpTemp("rlp_zero_connectivity.xodr", ZeroConnectivityXodr())));

    Route    route;
    Position wp0 = MakeWaypoint(1, -1, 10.0);
    Position wp1 = MakeWaypoint(2, -1, 40.0);
    ASSERT_EQ(route.AddWaypoint(wp0), 0) << "road-level link exists -- RoadPath::Calculate's lane-blind "
                                            "target-road fast path must still accept this route";
    ASSERT_EQ(route.AddWaypoint(wp1), 0);
    ASSERT_TRUE(route.IsValid());

    const RouteLanePlan plan = BuildRouteLanePlan(route);

    EXPECT_FALSE(plan.valid);
    EXPECT_EQ(plan.diagnostic, "reroute_failed");
    EXPECT_EQ(plan.discontinuity_road_id, 1u) << "the pre-reroute lane_discontinuity was detected on road 1";
    EXPECT_FALSE(plan.rerouted) << "rerouted is only set true when CalculatePath finds a path to redo steps 5-6 on";

    Position::GetOpenDrive()->Clear();
}
