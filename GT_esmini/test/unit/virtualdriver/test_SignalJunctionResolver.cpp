// test_SignalJunctionResolver.cpp -- SignalJunctionResolver unit tests.
//
// Two pure-function groups (ResolveControllerChainJunctions, ResolveAheadLinkEnd) use
// hand-rolled data, no roadmanager types involved. The ResolveSignalJunction group loads
// real committed xodr assets through the real parser
// (roadmanager::Position::GetOpenDrive()->LoadOpenDriveFile), mirroring
// test_RouteLanePlan.cpp / test_OdrJunctionExtras.cpp's pattern in this same test tree.
// Path (b) (signal on its own connecting road) has no committed asset anywhere under
// resources/xodr/ (checked: no <signal> in this repo sits on a road with junction != "-1"),
// so that one case uses a synthetic network, written to a temp file and loaded through the
// same real parser -- mirroring test_RouteLanePlan.cpp's ThreeRoadJunctionXodr() pattern
// (same junction/connecting-road shape, a <signals> block added on the connector).
#include <gtest/gtest.h>

#include "gt_esmini/control/virtualdriver/policies/SignalJunctionResolver.hpp"

#include "RoadManager.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

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

roadmanager::Signal* FindSignalById(roadmanager::Road* road, int id)
{
    if (!road) return nullptr;
    const unsigned int n = road->GetNumberOfSignals();
    for (unsigned int i = 0; i < n; ++i)
    {
        roadmanager::Signal* sig = road->GetSignal(i);
        if (sig && sig->GetId() == id) return sig;
    }
    return nullptr;
}

// Scratch dir + temp writer (mirrors test_RouteLanePlan.cpp's RlpScratchDir/WriteRlpTemp).
std::filesystem::path SjrScratchDir()
{
    std::error_code   ec;
    const std::string root = RepoRoot();
    if (!root.empty())
    {
        std::filesystem::path cand = std::filesystem::path(root) / "build" / "sjr_tests";
        std::filesystem::create_directories(cand, ec);
        if (!ec && std::filesystem::is_directory(cand))
        {
            return cand;
        }
    }
    std::filesystem::path tmp = std::filesystem::temp_directory_path(ec) / "sjr_tests";
    std::filesystem::create_directories(tmp, ec);
    return tmp;
}

std::string WriteSjrTemp(const std::string& name, const std::string& content)
{
    const std::filesystem::path p = SjrScratchDir() / name;
    std::ofstream                out(p, std::ios::binary);
    out << content;
    out.close();
    return p.string();
}

// One junction (900), armA(1) -> connector(2, junction="900") -> armB(3), single driving
// lane throughout -- same shape as test_RouteLanePlan.cpp's ThreeRoadJunctionXodr(), plus a
// signal (id 500, orientation "+") placed ON the connecting road itself. No <controller>
// anywhere (path (a) must miss), and the connector's own successor link points to armB as
// elementType="road" (not "junction"), so path (c) must miss too -- isolating path (b): the
// signal's own road carries junction="900".
std::string ConnectingRoadSignalXodr()
{
    return std::string("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n") +
           "<OpenDRIVE>\n"
           "  <header revMajor=\"1\" revMinor=\"7\" name=\"sjr_connecting_road_signal\" version=\"1.00\" date=\"2026-08-02T00:00:00\""
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
           "    <signals>\n"
           "      <signal s=\"5.0\" t=\"-2.0\" id=\"500\" name=\"mid_junction_signal\" dynamic=\"yes\" orientation=\"+\""
           " zOffset=\"3.0\" type=\"1000001\" country=\"OpenDRIVE\" subtype=\"-1\" hOffset=\"0.0\" pitch=\"0.0\" roll=\"0.0\""
           " height=\"1.0\" width=\"0.3\"/>\n"
           "    </signals>\n"
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
           "  <junction name=\"j900\" id=\"900\">\n"
           "    <connection id=\"0\" incomingRoad=\"1\" connectingRoad=\"2\" contactPoint=\"start\">\n"
           "      <laneLink from=\"-1\" to=\"-1\"/>\n"
           "    </connection>\n"
           "  </junction>\n"
           "</OpenDRIVE>\n";
}
}  // namespace

// ────────────── ResolveControllerChainJunctions (pure) ──────────────
// Data below mirrors multi_intersections.xodr's real shape (controller "ctrl002" / id 2,
// bundling signals 291 + 281, referenced only by junction 146) without touching the file,
// so the two known holes (unreferenced controller, controller referenced by >1 junction)
// are exercised deterministically regardless of whether any real asset happens to contain
// them.

TEST(ResolveControllerChainJunctions, SingleControllerSingleJunctionMapsItsSignals)
{
    const std::vector<JunctionControllerRefs> junctions   = {{146, {2}}};
    const std::vector<ControllerSignals>      controllers = {{2, {291, 281}}};

    const auto result = ResolveControllerChainJunctions(junctions, controllers);

    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result.at(291), 146u);
    EXPECT_EQ(result.at(281), 146u);
}

TEST(ResolveControllerChainJunctions, ControllerNotReferencedByAnyJunctionYieldsNoEntries)
{
    // hole 1: controller 9 controls signal 500 but no junction lists it.
    const std::vector<JunctionControllerRefs> junctions   = {{146, {2}}};
    const std::vector<ControllerSignals>      controllers = {{2, {291}}, {9, {500}}};

    const auto result = ResolveControllerChainJunctions(junctions, controllers);

    EXPECT_EQ(result.count(500), 0u);
    ASSERT_EQ(result.count(291), 1u);
    EXPECT_EQ(result.at(291), 146u);
}

TEST(ResolveControllerChainJunctions, ControllerReferencedByMultipleJunctionsIsAmbiguous)
{
    // hole 2: controller 2 is listed by BOTH junction 146 and junction 200 -> must not
    // pick either one arbitrarily; neither of its signals may resolve via (a).
    const std::vector<JunctionControllerRefs> junctions   = {{146, {2}}, {200, {2}}};
    const std::vector<ControllerSignals>      controllers = {{2, {291, 281}}};

    const auto result = ResolveControllerChainJunctions(junctions, controllers);

    EXPECT_TRUE(result.empty());
}

TEST(ResolveControllerChainJunctions, AmbiguousControllerDoesNotPoisonOtherControllers)
{
    const std::vector<JunctionControllerRefs> junctions   = {{146, {2}}, {200, {2}}, {146, {3}}};
    const std::vector<ControllerSignals>      controllers = {{2, {291}}, {3, {999}}};

    const auto result = ResolveControllerChainJunctions(junctions, controllers);

    EXPECT_EQ(result.count(291), 0u);  // controller 2: ambiguous (146 vs 200)
    ASSERT_EQ(result.count(999), 1u);  // controller 3: unambiguous
    EXPECT_EQ(result.at(999), 146u);
}

TEST(ResolveControllerChainJunctions, SameJunctionListingAControllerTwiceIsNotAmbiguous)
{
    // Defensive: a junction whose controller list repeats an id (or two junction
    // entries that happen to agree) must not be flagged ambiguous.
    const std::vector<JunctionControllerRefs> junctions   = {{146, {2, 2}}};
    const std::vector<ControllerSignals>      controllers = {{2, {291}}};

    const auto result = ResolveControllerChainJunctions(junctions, controllers);

    ASSERT_EQ(result.count(291), 1u);
    EXPECT_EQ(result.at(291), 146u);
}

TEST(ResolveControllerChainJunctions, EmptyInputsYieldEmptyMap)
{
    EXPECT_TRUE(ResolveControllerChainJunctions({}, {}).empty());
}

TEST(ResolveControllerChainJunctions, ControllerWithNoSignalsContributesNothing)
{
    const std::vector<JunctionControllerRefs> junctions   = {{146, {2}}};
    const std::vector<ControllerSignals>      controllers = {{2, {}}};

    EXPECT_TRUE(ResolveControllerChainJunctions(junctions, controllers).empty());
}

// ────────────── ResolveAheadLinkEnd (pure) ──────────────

TEST(ResolveAheadLinkEnd, PositiveOrientationIsAlwaysSuccessorRegardlessOfDirection)
{
    EXPECT_EQ(ResolveAheadLinkEnd(SignalOrientation::POSITIVE, 1.0), SignalAheadEnd::SUCCESSOR);
    EXPECT_EQ(ResolveAheadLinkEnd(SignalOrientation::POSITIVE, -1.0), SignalAheadEnd::SUCCESSOR);
}

TEST(ResolveAheadLinkEnd, NegativeOrientationIsAlwaysPredecessorRegardlessOfDirection)
{
    EXPECT_EQ(ResolveAheadLinkEnd(SignalOrientation::NEGATIVE, -1.0), SignalAheadEnd::PREDECESSOR);
    EXPECT_EQ(ResolveAheadLinkEnd(SignalOrientation::NEGATIVE, 1.0), SignalAheadEnd::PREDECESSOR);
}

TEST(ResolveAheadLinkEnd, NoneOrientationFollowsTravelDirection)
{
    EXPECT_EQ(ResolveAheadLinkEnd(SignalOrientation::NONE, 1.0), SignalAheadEnd::SUCCESSOR);
    EXPECT_EQ(ResolveAheadLinkEnd(SignalOrientation::NONE, -1.0), SignalAheadEnd::PREDECESSOR);
}

TEST(ResolveAheadLinkEnd, NoneOrientationZeroDirectionDefaultsToSuccessor)
{
    EXPECT_EQ(ResolveAheadLinkEnd(SignalOrientation::NONE, 0.0), SignalAheadEnd::SUCCESSOR);
}

// ────────────── ResolveSignalJunction (real assets) ──────────────
// Every test reloads its own xodr with replace=true: roadmanager::Position::GetOpenDrive()
// is a process-wide singleton also used by other test files in this binary, so no test may
// assume what is currently loaded.

TEST(ResolveSignalJunction, ControllerChainResolvesFarSideSignalOnMultiIntersections)
{
#ifdef GT_ODR_REPO_ROOT
    const std::string root = RepoRoot();
    ASSERT_FALSE(root.empty());
    roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();
    ASSERT_TRUE(odr->LoadOpenDriveFile((root + "/resources/xodr/multi_intersections.xodr").c_str(), true));

    // Ground truth re-confirmed directly against the file (2026-08-02): road 196's signal
    // 291 (orientation "-") is bundled by controller "ctrl002" (id 2) together with signal
    // 281; controller 2 is referenced only by junction 146's <controller> list. road 196's
    // own predecessor link also happens to be junction 146 (path (c) would agree here too),
    // so this test's real assertion is that (a) is the path actually used -- see the source
    // check below.
    roadmanager::Road* road196 = odr->GetRoadById(196);
    ASSERT_NE(road196, nullptr);
    roadmanager::Signal* sig291 = FindSignalById(road196, 291);
    ASSERT_NE(sig291, nullptr);

    SignalJunctionSource source{};
    const auto           result = ResolveSignalJunction(odr, sig291, -1.0, &source);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 146u);
    EXPECT_EQ(source, SignalJunctionSource::CONTROLLER_CHAIN);
#else
    GTEST_SKIP() << "GT_ODR_REPO_ROOT not defined";
#endif
}

TEST(ResolveSignalJunction, RoadLinkResolvesApproachSignalOnFabriksgatanTrafficLights)
{
#ifdef GT_ODR_REPO_ROOT
    const std::string root = RepoRoot();
    ASSERT_FALSE(root.empty());
    roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();
    ASSERT_TRUE(odr->LoadOpenDriveFile((root + "/resources/xodr/fabriksgatan_traffic_lights.xodr").c_str(), true));
    ASSERT_EQ(odr->GetNumberOfControllers(), 0u);  // this asset has none -> must fall through (a)

    // Ground truth re-confirmed directly against the file: road 3's signal 1 (orientation
    // "+", governs +s travel) sits ahead of road 3's successor link, which is junction 4.
    roadmanager::Road* road3 = odr->GetRoadById(3);
    ASSERT_NE(road3, nullptr);
    roadmanager::Signal* sig1 = FindSignalById(road3, 1);
    ASSERT_NE(sig1, nullptr);

    SignalJunctionSource source{};
    const auto           result = ResolveSignalJunction(odr, sig1, 1.0, &source);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 4u);
    EXPECT_EQ(source, SignalJunctionSource::ROAD_LINK);
#else
    GTEST_SKIP() << "GT_ODR_REPO_ROOT not defined";
#endif
}

TEST(ResolveSignalJunction, ConnectingRoadResolvesSignalPlacedOnItsOwnConnectingRoad)
{
#ifdef GT_ODR_REPO_ROOT
    ASSERT_FALSE(RepoRoot().empty()) << "GT_ODR_REPO_ROOT not defined";
    roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();
    ASSERT_TRUE(odr->LoadOpenDriveFile(WriteSjrTemp("sjr_connecting_road_signal.xodr", ConnectingRoadSignalXodr()).c_str(), true));
    ASSERT_EQ(odr->GetNumberOfControllers(), 0u);  // no controller anywhere -> must fall through (a)

    roadmanager::Road* connector = odr->GetRoadById(2);
    ASSERT_NE(connector, nullptr);
    ASSERT_EQ(connector->GetJunction(), 900u);
    // The connector's own successor link is elementType="road" (armB, id 3), not a junction --
    // confirms path (c) has nothing to find here before path (b) is asked to.
    ASSERT_NE(connector->GetLink(roadmanager::LinkType::SUCCESSOR), nullptr);
    ASSERT_EQ(connector->GetLink(roadmanager::LinkType::SUCCESSOR)->GetElementType(), roadmanager::RoadLink::ELEMENT_TYPE_ROAD);

    roadmanager::Signal* sig500 = FindSignalById(connector, 500);
    ASSERT_NE(sig500, nullptr);

    SignalJunctionSource source{};
    const auto           result = ResolveSignalJunction(odr, sig500, 1.0, &source);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 900u);
    EXPECT_EQ(source, SignalJunctionSource::CONNECTING_ROAD);

    Position::GetOpenDrive()->Clear();
#else
    GTEST_SKIP() << "GT_ODR_REPO_ROOT not defined";
#endif
}

TEST(ResolveSignalJunction, UnresolvableSignalOnAJunctionlessStraightRoadReturnsNullopt)
{
#ifdef GT_ODR_REPO_ROOT
    const std::string root = RepoRoot();
    ASSERT_FALSE(root.empty());
    roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();
    ASSERT_TRUE(odr->LoadOpenDriveFile((root + "/resources/xodr/straight_stop_sign.xodr").c_str(), true));
    ASSERT_EQ(odr->GetNumOfJunctions(), 0u);  // no junction anywhere in this asset

    roadmanager::Road* road1 = odr->GetRoadById(1);
    ASSERT_NE(road1, nullptr);
    roadmanager::Signal* sig10 = FindSignalById(road1, 10);
    ASSERT_NE(sig10, nullptr);

    SignalJunctionSource source{};
    EXPECT_FALSE(ResolveSignalJunction(odr, sig10, 1.0, &source).has_value());
#else
    GTEST_SKIP() << "GT_ODR_REPO_ROOT not defined";
#endif
}

// Regression pin for the cache-invalidation design: reload a DIFFERENT xodr into the SAME
// (singleton) OpenDrive* mid-test and confirm the second resolution is fresh, not a stale
// answer left over from the first file. A cache keyed on OpenDrive* pointer identity alone
// would incorrectly keep resolving fabriksgatan's signal against multi_intersections' old
// controller/junction data (junction 146 does not even exist in fabriksgatan).
TEST(ResolveSignalJunction, CacheDoesNotServeStaleAnswersAfterReloadingADifferentXodr)
{
#ifdef GT_ODR_REPO_ROOT
    const std::string root = RepoRoot();
    ASSERT_FALSE(root.empty());
    roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();

    ASSERT_TRUE(odr->LoadOpenDriveFile((root + "/resources/xodr/multi_intersections.xodr").c_str(), true));
    roadmanager::Signal* sig291 = FindSignalById(odr->GetRoadById(196), 291);
    ASSERT_NE(sig291, nullptr);
    SignalJunctionSource source_a{};
    const auto           result_a = ResolveSignalJunction(odr, sig291, -1.0, &source_a);
    ASSERT_TRUE(result_a.has_value());
    EXPECT_EQ(result_a.value(), 146u);
    EXPECT_EQ(source_a, SignalJunctionSource::CONTROLLER_CHAIN);

    ASSERT_TRUE(odr->LoadOpenDriveFile((root + "/resources/xodr/fabriksgatan_traffic_lights.xodr").c_str(), true));
    ASSERT_EQ(odr->GetNumberOfControllers(), 0u);

    roadmanager::Signal* sig1 = FindSignalById(odr->GetRoadById(3), 1);
    ASSERT_NE(sig1, nullptr);
    SignalJunctionSource source_b{};
    const auto           result_b = ResolveSignalJunction(odr, sig1, 1.0, &source_b);
    ASSERT_TRUE(result_b.has_value());
    EXPECT_EQ(result_b.value(), 4u);
    EXPECT_EQ(source_b, SignalJunctionSource::ROAD_LINK);  // NOT CONTROLLER_CHAIN -- (a) must miss cleanly
#else
    GTEST_SKIP() << "GT_ODR_REPO_ROOT not defined";
#endif
}
