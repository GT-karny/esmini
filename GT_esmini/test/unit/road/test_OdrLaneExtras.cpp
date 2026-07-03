// test_OdrLaneExtras.cpp -- behavioral proofs for the P2 lane-detail features (plan P2),
// driven through the REAL parser (roadmanager::Position::GetOpenDrive()->LoadOpenDriveFile)
// with tiny deterministic temp xodr files, mirroring test_OdrForkPatches.cpp:
//
//   - LaneExtrasStorage        : cluster 3 + 16 L1 storage -- lane@type source strings
//                                (walking/curb), 1.8 lane attributes, <speed>/<access>/<rule>/
//                                roadMark <sway>/<border> records, and SPARSENESS (a plain
//                                driving lane produces NO lane_extras entry).
//   - BorderWidthNormalization : the border->width normalization (Ex_Lane-Border false-green
//                                fix) -- widths synthesized from <border> algebra are visible
//                                through Road::GetLaneWidthByS, and authored <width> prevails
//                                when both are present (spec).
//   - LaneSpeedLookup          : GetLaneSpeedLimit L2 lookup -- section + sOffset resolution,
//                                km/h / mph / default-unit conversion, absent lane/road -> 0.
//   - DefaultPathNoExtras      : legacy 1.4 asset with no P2 features -> empty lane_extras and
//                                GetLaneSpeedLimit == 0 (bit-identical default path).
//
// Temp files are written under GT_ODR_REPO_ROOT/build (a scratch dir) when available, else
// std::filesystem::temp_directory_path(); content is deterministic. Each load uses
// replace=true, so the singleton OpenDrive (and its side-model registry entry) is REPLACED
// per test; every test ends with Clear() like the fork-patch suite.
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "RoadManager.hpp"
#include "gt_esmini/road/OdrSideModel.hpp"

namespace fs = std::filesystem;

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

// A scratch directory for the temp xodr files. Prefer <repo>/build (gitignored) so nothing
// lands in the source tree; fall back to the OS temp dir.
fs::path ScratchDir()
{
    const std::string root = RepoRoot();
    std::error_code   ec;
    if (!root.empty())
    {
        fs::path cand = fs::path(root) / "build" / "odr_lane_extras_tests";
        fs::create_directories(cand, ec);
        if (!ec && fs::is_directory(cand))
        {
            return cand;
        }
    }
    fs::path tmp = fs::temp_directory_path(ec) / "odr_lane_extras_tests";
    fs::create_directories(tmp, ec);
    return tmp;
}

// Write `content` to <scratch>/<name> and return the absolute path.
std::string WriteTemp(const std::string& name, const std::string& content)
{
    const fs::path p = ScratchDir() / name;
    std::ofstream  out(p, std::ios::binary);
    out << content;
    out.close();
    return p.string();
}

// Load an xodr into the singleton OpenDrive (replace=true -> fresh network). Returns load ok.
bool LoadXodr(const std::string& abs_path)
{
    return roadmanager::Position::GetOpenDrive()->LoadOpenDriveFile(abs_path.c_str(), true);
}

// Find the lane_extras entry for (road_id, section_index, lane_id); nullptr if absent.
const gt_esmini::odr::OdrLaneExtras* FindLaneExtras(const gt_esmini::odr::OdrSideModel* m,
                                                    const std::string&                  road_id,
                                                    int                                 section_index,
                                                    int                                 lane_id)
{
    if (m == nullptr)
    {
        return nullptr;
    }
    for (const gt_esmini::odr::OdrLaneExtras& ex : m->lane_extras)
    {
        if (ex.road_id == road_id && ex.section_index == section_index && ex.lane_id == lane_id)
        {
            return &ex;
        }
    }
    return nullptr;
}

std::string Header18()
{
    return std::string("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n") +
           "<OpenDRIVE>\n"
           "  <header revMajor=\"1\" revMinor=\"8\" name=\"lane_extras_test\" version=\"1.00\" date=\"2026-07-03T00:00:00\""
           " north=\"0.0\" south=\"0.0\" east=\"0.0\" west=\"0.0\"/>\n";
}

}  // namespace

// 1. Cluster 3 + 16 L1 storage: exact source strings, record counts/values, sparseness.
TEST(OdrLaneExtras, LaneExtrasStorage)
{
    // One road, ONE laneSection. Left lane 2 = walking, left lane 1 = driving carrying ALL the
    // P2 attributes/children, right lane -1 = plain driving (sparseness probe), right lane -2 = curb.
    const std::string xodr = Header18() +
                             "  <road name=\"main\" length=\"150.0\" id=\"1\" junction=\"-1\">\n"
                             "    <link/>\n"
                             "    <planView>\n"
                             "      <geometry s=\"0.0\" x=\"0.0\" y=\"0.0\" hdg=\"0.0\" length=\"150.0\"><line/></geometry>\n"
                             "    </planView>\n"
                             "    <lanes>\n"
                             "      <laneSection s=\"0.0\">\n"
                             "        <left>\n"
                             "          <lane id=\"2\" type=\"walking\" level=\"false\"><link/>"
                             "<width sOffset=\"0.0\" a=\"2.0\" b=\"0.0\" c=\"0.0\" d=\"0.0\"/></lane>\n"
                             "          <lane id=\"1\" type=\"driving\" level=\"false\" direction=\"reversed\" advisory=\"outer\""
                             " dynamicLaneDirection=\"true\" dynamicLaneType=\"true\" roadWorks=\"true\"><link/>\n"
                             "            <width sOffset=\"0.0\" a=\"3.5\" b=\"0.0\" c=\"0.0\" d=\"0.0\"/>\n"
                             "            <roadMark sOffset=\"0.0\" type=\"solid\" weight=\"standard\" color=\"standard\" width=\"0.12\">\n"
                             "              <sway ds=\"0\" a=\"0\" b=\"0.05\" c=\"0\" d=\"0\"/>\n"
                             "            </roadMark>\n"
                             "            <speed sOffset=\"0\" max=\"50\" unit=\"km/h\"/>\n"
                             "            <speed sOffset=\"60\" max=\"30\" unit=\"km/h\"/>\n"
                             "            <access sOffset=\"0\" rule=\"deny\" restriction=\"passengerCar\">"
                             "<restriction type=\"bus\"/></access>\n"
                             "            <rule sOffset=\"0\" value=\"no stopping\"/>\n"
                             "          </lane>\n"
                             "        </left>\n"
                             "        <center><lane id=\"0\" type=\"none\" level=\"false\"><link/></lane></center>\n"
                             "        <right>\n"
                             "          <lane id=\"-1\" type=\"driving\" level=\"false\"><link/>"
                             "<width sOffset=\"0.0\" a=\"3.5\" b=\"0.0\" c=\"0.0\" d=\"0.0\"/></lane>\n"
                             "          <lane id=\"-2\" type=\"curb\" level=\"false\"><link/>"
                             "<width sOffset=\"0.0\" a=\"0.5\" b=\"0.0\" c=\"0.0\" d=\"0.0\"/></lane>\n"
                             "        </right>\n"
                             "      </laneSection>\n"
                             "    </lanes>\n"
                             "  </road>\n"
                             "</OpenDRIVE>\n";
    const std::string path = WriteTemp("lane_extras_storage.xodr", xodr);
    ASSERT_TRUE(LoadXodr(path)) << "load failed: " << path;

    roadmanager::OpenDrive*             odr = roadmanager::Position::GetOpenDrive();
    const gt_esmini::odr::OdrSideModel* sm  = gt_esmini::odr::GetSideModel(odr);
    ASSERT_NE(sm, nullptr) << "side model missing after load (fork hook did not run?)";

    // SPARSENESS: exactly 3 entries -- lane 2 (walking), lane 1 (attrs+records), lane -2 (curb).
    // The plain driving lane -1 and the plain center lane 0 must NOT produce entries.
    EXPECT_EQ(sm->lane_extras.size(), 3u) << "only lanes carrying >=1 P2 datum may have a lane_extras entry";
    EXPECT_EQ(FindLaneExtras(sm, "1", 0, -1), nullptr) << "plain driving lane -1 must have NO entry (sparseness)";

    // walking lane: type source string kept; fork patch maps the runtime enum to SIDEWALK.
    {
        const gt_esmini::odr::OdrLaneExtras* ex = FindLaneExtras(sm, "1", 0, 2);
        ASSERT_NE(ex, nullptr) << "walking lane 2 must have an entry (type_str is a P2 datum)";
        EXPECT_EQ(ex->side, "left");
        EXPECT_DOUBLE_EQ(ex->section_s, 0.0);
        EXPECT_EQ(ex->type_str, "walking");
    }

    // curb lane: same contract on the right side.
    {
        const gt_esmini::odr::OdrLaneExtras* ex = FindLaneExtras(sm, "1", 0, -2);
        ASSERT_NE(ex, nullptr) << "curb lane -2 must have an entry (type_str is a P2 datum)";
        EXPECT_EQ(ex->side, "right");
        EXPECT_EQ(ex->type_str, "curb");
    }

    // [GT_ODR:lane-types] runtime mapping: walking -> SIDEWALK, curb -> CURB (not NONE fallback).
    roadmanager::Road* road = odr->GetRoadByIdx(0);
    ASSERT_NE(road, nullptr);
    EXPECT_EQ(road->GetLaneTypeByS(10.0, 2), roadmanager::Lane::LANE_TYPE_SIDEWALK);
    EXPECT_EQ(road->GetLaneTypeByS(10.0, -2), roadmanager::Lane::LANE_TYPE_CURB);

    // The fully loaded lane 1: 1.8 attributes verbatim + all cluster 16 record types.
    {
        const gt_esmini::odr::OdrLaneExtras* ex = FindLaneExtras(sm, "1", 0, 1);
        ASSERT_NE(ex, nullptr) << "lane 1 carries P2 attributes/children and must have an entry";
        EXPECT_EQ(ex->side, "left");
        EXPECT_EQ(ex->type_str, "driving");
        EXPECT_EQ(ex->direction, "reversed");
        EXPECT_EQ(ex->advisory, "outer");
        EXPECT_EQ(ex->dynamic_lane_direction, "true");
        EXPECT_EQ(ex->dynamic_lane_type, "true");
        EXPECT_EQ(ex->road_works, "true");

        ASSERT_EQ(ex->speeds.size(), 2u);
        EXPECT_DOUBLE_EQ(ex->speeds[0].s_offset, 0.0);
        EXPECT_DOUBLE_EQ(ex->speeds[0].max, 50.0);
        EXPECT_EQ(ex->speeds[0].unit, "km/h");
        EXPECT_DOUBLE_EQ(ex->speeds[1].s_offset, 60.0);
        EXPECT_DOUBLE_EQ(ex->speeds[1].max, 30.0);
        EXPECT_EQ(ex->speeds[1].unit, "km/h");

        ASSERT_EQ(ex->accesses.size(), 1u);
        EXPECT_DOUBLE_EQ(ex->accesses[0].s_offset, 0.0);
        EXPECT_EQ(ex->accesses[0].rule, "deny");
        EXPECT_EQ(ex->accesses[0].restriction, "passengerCar");  // <=1.5 attribute form
        ASSERT_EQ(ex->accesses[0].restrictions.size(), 1u);      // 1.6+ child form
        EXPECT_EQ(ex->accesses[0].restrictions[0], "bus");

        ASSERT_EQ(ex->rules.size(), 1u);
        EXPECT_DOUBLE_EQ(ex->rules[0].s_offset, 0.0);
        EXPECT_EQ(ex->rules[0].value, "no stopping");

        ASSERT_EQ(ex->sways.size(), 1u);
        EXPECT_DOUBLE_EQ(ex->sways[0].ds, 0.0);
        EXPECT_DOUBLE_EQ(ex->sways[0].a, 0.0);
        EXPECT_DOUBLE_EQ(ex->sways[0].b, 0.05);
        EXPECT_DOUBLE_EQ(ex->sways[0].c, 0.0);
        EXPECT_DOUBLE_EQ(ex->sways[0].d, 0.0);

        EXPECT_TRUE(ex->borders.empty());
    }

    roadmanager::Position::GetOpenDrive()->Clear();
}

// 2. Border->width normalization (Ex_Lane-Border shape) + "width prevails" (spec).
TEST(OdrLaneExtras, BorderWidthNormalization)
{
    // Two lane sections. Section 0 (s=0..50): plain width-authored lanes 1/-1 (no lane -2).
    // Section 1 (s=50..100): border-ONLY lanes replicating the ASAM Ex_Lane-Border shape:
    //   lane  1 border const  3.57            -> width = +( 3.57 - 0)      = 3.57
    //   lane -1 border const -3.57            -> width = -(-3.57 - 0)      = 3.57
    //   lane -2 borders: ds<20: -3.57 - 0.026775*ds^2 + 0.00089250*ds^3; ds>=20: const -7.14
    //     -> width(ds) = -(border_-2 - border_-1) = 0.026775*ds^2 - 0.00089250*ds^3 (ds<20),
    //        then const -(-7.14 + 3.57) = 3.57 (ds>=20), continuous at ds=20.
    const std::string xodr = Header18() +
                             "  <road name=\"border\" length=\"100.0\" id=\"1\" junction=\"-1\">\n"
                             "    <link/>\n"
                             "    <planView>\n"
                             "      <geometry s=\"0.0\" x=\"0.0\" y=\"0.0\" hdg=\"0.0\" length=\"100.0\"><line/></geometry>\n"
                             "    </planView>\n"
                             "    <lanes>\n"
                             "      <laneSection s=\"0.0\">\n"
                             "        <left><lane id=\"1\" type=\"driving\" level=\"false\"><link/>"
                             "<width sOffset=\"0.0\" a=\"3.57\" b=\"0.0\" c=\"0.0\" d=\"0.0\"/></lane></left>\n"
                             "        <center><lane id=\"0\" type=\"none\" level=\"false\"><link/></lane></center>\n"
                             "        <right><lane id=\"-1\" type=\"driving\" level=\"false\"><link/>"
                             "<width sOffset=\"0.0\" a=\"3.57\" b=\"0.0\" c=\"0.0\" d=\"0.0\"/></lane></right>\n"
                             "      </laneSection>\n"
                             "      <laneSection s=\"50.0\">\n"
                             "        <left><lane id=\"1\" type=\"driving\" level=\"false\"><link/>"
                             "<border sOffset=\"0.0\" a=\"3.57\" b=\"0.0\" c=\"0.0\" d=\"0.0\"/></lane></left>\n"
                             "        <center><lane id=\"0\" type=\"none\" level=\"false\"><link/></lane></center>\n"
                             "        <right>\n"
                             "          <lane id=\"-1\" type=\"driving\" level=\"false\"><link/>"
                             "<border sOffset=\"0.0\" a=\"-3.57\" b=\"0.0\" c=\"0.0\" d=\"0.0\"/></lane>\n"
                             "          <lane id=\"-2\" type=\"driving\" level=\"false\"><link/>\n"
                             "            <border sOffset=\"0.0\" a=\"-3.57\" b=\"0.0\" c=\"-0.026775\" d=\"0.00089250\"/>\n"
                             "            <border sOffset=\"20.0\" a=\"-7.14\" b=\"0.0\" c=\"0.0\" d=\"0.0\"/>\n"
                             "          </lane>\n"
                             "        </right>\n"
                             "      </laneSection>\n"
                             "    </lanes>\n"
                             "  </road>\n"
                             "</OpenDRIVE>\n";
    const std::string path = WriteTemp("border_width.xodr", xodr);
    ASSERT_TRUE(LoadXodr(path)) << "load failed: " << path;

    roadmanager::Road* road = roadmanager::Position::GetOpenDrive()->GetRoadByIdx(0);
    ASSERT_NE(road, nullptr);
    ASSERT_EQ(road->GetNumberOfLaneSections(), 2u);

    // Lanes 1 / -1: constant 3.57 everywhere (section 0 authored width, section 1 from border).
    for (double s : {10.0, 55.0, 75.0, 95.0})
    {
        EXPECT_NEAR(road->GetLaneWidthByS(s, 1), 3.57, 1e-6) << "lane 1 at s=" << s;
        EXPECT_NEAR(road->GetLaneWidthByS(s, -1), 3.57, 1e-6) << "lane -1 at s=" << s;
    }

    // Lane -2 (section base s=50): width(ds) = 0.026775*ds^2 - 0.00089250*ds^3 for ds < 20.
    for (double ds : {5.0, 10.0, 19.0})
    {
        const double expected = 0.026775 * ds * ds - 0.00089250 * ds * ds * ds;
        EXPECT_NEAR(road->GetLaneWidthByS(50.0 + ds, -2), expected, 1e-6) << "lane -2 at ds=" << ds;
    }
    // Second border piece (ds >= 20): constant 3.57.
    EXPECT_NEAR(road->GetLaneWidthByS(75.0, -2), 3.57, 1e-6);
    EXPECT_NEAR(road->GetLaneWidthByS(95.0, -2), 3.57, 1e-6);

    // Section 0 has no lane -2 -> zero width there.
    EXPECT_DOUBLE_EQ(road->GetLaneWidthByS(25.0, -2), 0.0) << "lane -2 must not exist in section 0";

    // WidthPrevails: a lane with BOTH <width> and <border> keeps its authored width (spec).
    const std::string xodr_wp = Header18() +
                                "  <road name=\"wp\" length=\"100.0\" id=\"1\" junction=\"-1\">\n"
                                "    <link/>\n"
                                "    <planView>\n"
                                "      <geometry s=\"0.0\" x=\"0.0\" y=\"0.0\" hdg=\"0.0\" length=\"100.0\"><line/></geometry>\n"
                                "    </planView>\n"
                                "    <lanes>\n"
                                "      <laneSection s=\"0.0\">\n"
                                "        <center><lane id=\"0\" type=\"none\" level=\"false\"><link/></lane></center>\n"
                                "        <right><lane id=\"-1\" type=\"driving\" level=\"false\"><link/>\n"
                                "          <width sOffset=\"0.0\" a=\"4.0\" b=\"0.0\" c=\"0.0\" d=\"0.0\"/>\n"
                                "          <border sOffset=\"0.0\" a=\"-3.0\" b=\"0.0\" c=\"0.0\" d=\"0.0\"/>\n"
                                "        </lane></right>\n"
                                "      </laneSection>\n"
                                "    </lanes>\n"
                                "  </road>\n"
                                "</OpenDRIVE>\n";
    const std::string path_wp = WriteTemp("border_width_prevails.xodr", xodr_wp);
    ASSERT_TRUE(LoadXodr(path_wp)) << "load failed: " << path_wp;
    road = roadmanager::Position::GetOpenDrive()->GetRoadByIdx(0);
    ASSERT_NE(road, nullptr);
    EXPECT_NEAR(road->GetLaneWidthByS(10.0, -1), 4.0, 1e-9) << "authored <width> must prevail over <border> (spec)";

    roadmanager::Position::GetOpenDrive()->Clear();
}

// 3. Lane <speed> L2 lookup: section + sOffset resolution and unit conversion.
TEST(OdrLaneExtras, LaneSpeedLookup)
{
    // Two lane sections. Section 0 (s=0): lane -1 speeds @0 -> 30 km/h, @60 -> 50 km/h;
    // lane 1 speed 50 km/h. Section 1 (s=100): lane -1 speed 20 mph; lane 1 speed 13.9
    // WITHOUT a unit attribute (ODR default m/s passthrough).
    const std::string xodr = Header18() +
                             "  <road name=\"speed\" length=\"150.0\" id=\"1\" junction=\"-1\">\n"
                             "    <link/>\n"
                             "    <planView>\n"
                             "      <geometry s=\"0.0\" x=\"0.0\" y=\"0.0\" hdg=\"0.0\" length=\"150.0\"><line/></geometry>\n"
                             "    </planView>\n"
                             "    <lanes>\n"
                             "      <laneSection s=\"0.0\">\n"
                             "        <left><lane id=\"1\" type=\"driving\" level=\"false\"><link/>\n"
                             "          <width sOffset=\"0.0\" a=\"3.5\" b=\"0.0\" c=\"0.0\" d=\"0.0\"/>\n"
                             "          <speed sOffset=\"0\" max=\"50\" unit=\"km/h\"/>\n"
                             "        </lane></left>\n"
                             "        <center><lane id=\"0\" type=\"none\" level=\"false\"><link/></lane></center>\n"
                             "        <right><lane id=\"-1\" type=\"driving\" level=\"false\"><link/>\n"
                             "          <width sOffset=\"0.0\" a=\"3.5\" b=\"0.0\" c=\"0.0\" d=\"0.0\"/>\n"
                             "          <speed sOffset=\"0\" max=\"30\" unit=\"km/h\"/>\n"
                             "          <speed sOffset=\"60\" max=\"50\" unit=\"km/h\"/>\n"
                             "        </lane></right>\n"
                             "      </laneSection>\n"
                             "      <laneSection s=\"100.0\">\n"
                             "        <left><lane id=\"1\" type=\"driving\" level=\"false\"><link/>\n"
                             "          <width sOffset=\"0.0\" a=\"3.5\" b=\"0.0\" c=\"0.0\" d=\"0.0\"/>\n"
                             "          <speed sOffset=\"0\" max=\"13.9\"/>\n"
                             "        </lane></left>\n"
                             "        <center><lane id=\"0\" type=\"none\" level=\"false\"><link/></lane></center>\n"
                             "        <right><lane id=\"-1\" type=\"driving\" level=\"false\"><link/>\n"
                             "          <width sOffset=\"0.0\" a=\"3.5\" b=\"0.0\" c=\"0.0\" d=\"0.0\"/>\n"
                             "          <speed sOffset=\"0\" max=\"20\" unit=\"mph\"/>\n"
                             "        </lane></right>\n"
                             "      </laneSection>\n"
                             "    </lanes>\n"
                             "  </road>\n"
                             "</OpenDRIVE>\n";
    const std::string path = WriteTemp("lane_speed.xodr", xodr);
    ASSERT_TRUE(LoadXodr(path)) << "load failed: " << path;

    const void* key = roadmanager::Position::GetOpenDrive();

    // Section 0, first record (sOffset 0): 30 km/h.
    EXPECT_NEAR(gt_esmini::odr::GetLaneSpeedLimit(key, "1", -1, 10.0), 30.0 / 3.6, 1e-9);
    // Section 0, second record takes over at sOffset 60: 50 km/h.
    EXPECT_NEAR(gt_esmini::odr::GetLaneSpeedLimit(key, "1", -1, 70.0), 50.0 / 3.6, 1e-9);
    // Section 1 (s=100) overrides: 20 mph.
    EXPECT_NEAR(gt_esmini::odr::GetLaneSpeedLimit(key, "1", -1, 110.0), 20.0 * 0.44704, 1e-9);
    // Left lane, section 0: 50 km/h.
    EXPECT_NEAR(gt_esmini::odr::GetLaneSpeedLimit(key, "1", 1, 10.0), 50.0 / 3.6, 1e-9);
    // No unit attribute -> ODR default m/s, value passed through untouched.
    EXPECT_NEAR(gt_esmini::odr::GetLaneSpeedLimit(key, "1", 1, 110.0), 13.9, 1e-9);
    // Absent lane and absent road: no record applies -> 0.0 ("no lane limit" contract).
    EXPECT_DOUBLE_EQ(gt_esmini::odr::GetLaneSpeedLimit(key, "1", -2, 10.0), 0.0);
    EXPECT_DOUBLE_EQ(gt_esmini::odr::GetLaneSpeedLimit(key, "999", -1, 10.0), 0.0);

    roadmanager::Position::GetOpenDrive()->Clear();
}

// 4. Legacy default path: a 1.4 asset with no P2 features stays untouched (empty + 0.0).
TEST(OdrLaneExtras, DefaultPathNoExtras)
{
    const std::string xodr = std::string("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n") +
                             "<OpenDRIVE>\n"
                             "  <header revMajor=\"1\" revMinor=\"4\" name=\"legacy\" version=\"1.00\"/>\n"
                             "  <road name=\"r\" length=\"100.0\" id=\"1\" junction=\"-1\">\n"
                             "    <link/>\n"
                             "    <planView>\n"
                             "      <geometry s=\"0.0\" x=\"0.0\" y=\"0.0\" hdg=\"0.0\" length=\"100.0\"><line/></geometry>\n"
                             "    </planView>\n"
                             "    <lanes>\n"
                             "      <laneSection s=\"0.0\">\n"
                             "        <center><lane id=\"0\" type=\"none\" level=\"false\"><link/></lane></center>\n"
                             "        <right><lane id=\"-1\" type=\"driving\" level=\"false\"><link/>"
                             "<width sOffset=\"0.0\" a=\"3.5\" b=\"0.0\" c=\"0.0\" d=\"0.0\"/></lane></right>\n"
                             "      </laneSection>\n"
                             "    </lanes>\n"
                             "  </road>\n"
                             "</OpenDRIVE>\n";
    const std::string path = WriteTemp("legacy_no_extras.xodr", xodr);
    ASSERT_TRUE(LoadXodr(path)) << "load failed: " << path;

    roadmanager::OpenDrive*             odr = roadmanager::Position::GetOpenDrive();
    const gt_esmini::odr::OdrSideModel* sm  = gt_esmini::odr::GetSideModel(odr);
    ASSERT_NE(sm, nullptr) << "side model missing after load (fork hook did not run?)";
    EXPECT_TRUE(sm->lane_extras.empty()) << "legacy assets without P2 data must keep the side model sparse";
    EXPECT_DOUBLE_EQ(gt_esmini::odr::GetLaneSpeedLimit(odr, "1", -1, 10.0), 0.0);

    roadmanager::Position::GetOpenDrive()->Clear();
}
