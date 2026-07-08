// test_OdrJunctionExtras.cpp -- P5 junction side-model tests (plan P5, clusters 5/7/22).
//
// Loads real fixtures through the actual parser (LoadOpenDriveFile -> [GT_ODR:hook] BuildSideModel)
// and asserts the OdrJunctionExtras L1 storage + the F3 priority accessor. Template mirrors
// test_OdrForkPatches.cpp SignalExtrasDependencyReference. Source-of-truth repo root via the
// GT_ODR_REPO_ROOT compile def.
#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

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

bool LoadXodr(const std::string& abs_path)
{
    return roadmanager::Position::GetOpenDrive()->LoadOpenDriveFile(abs_path.c_str(), true);
}

// Fetch the junction_extras entry with the given id, or nullptr.
const gt_esmini::odr::OdrJunctionExtras* FindJunction(const gt_esmini::odr::OdrSideModel* sm, const std::string& id)
{
    if (sm == nullptr)
    {
        return nullptr;
    }
    for (const gt_esmini::odr::OdrJunctionExtras& ex : sm->junction_extras)
    {
        if (ex.junction_id == id)
        {
            return &ex;
        }
    }
    return nullptr;
}

constexpr unsigned int kSynthIdBase = 900000000u;  // must match SynthesizeCrosswalks

// Resolve a runtime road by authored string id.
roadmanager::Road* RoadByStr(roadmanager::OpenDrive* od, const std::string& id_str)
{
    for (unsigned int i = 0; i < od->GetNumOfRoads(); i++)
    {
        roadmanager::Road* r = od->GetRoadByIdx(i);
        if (r != nullptr && (r->GetIdStr() == id_str || std::to_string(r->GetId()) == id_str))
        {
            return r;
        }
    }
    return nullptr;
}

// Count CROSSWALK objects on a road (whole-road scan).
int CountCrosswalks(roadmanager::Road* road)
{
    int n = 0;
    for (idx_t j = 0; road && j < road->GetNumberOfObjects(); j++)
    {
        roadmanager::RMObject* o = road->GetRoadObject(j);
        if (o != nullptr && o->GetType() == roadmanager::RMObject::ObjectType::CROSSWALK)
        {
            n++;
        }
    }
    return n;
}

// First CROSSWALK object on a road, or nullptr.
roadmanager::RMObject* FirstCrosswalk(roadmanager::Road* road)
{
    for (idx_t j = 0; road && j < road->GetNumberOfObjects(); j++)
    {
        roadmanager::RMObject* o = road->GetRoadObject(j);
        if (o != nullptr && o->GetType() == roadmanager::RMObject::ObjectType::CROSSWALK)
        {
            return o;
        }
    }
    return nullptr;
}
}  // namespace

// (a) Fixture 01: virtual junction 555 has 1 crossPath (crossingRoad=2), crossing junction 666 has
//     a roadSection. Both are captured L1 (no longer dropped / flagged as unsupported).
TEST(OdrJunctionExtras, Fixture01CrossPathAndRoadSection)
{
    const std::string root = RepoRoot();
    ASSERT_FALSE(root.empty());
    ASSERT_TRUE(LoadXodr(root + "/GT_esmini/test/odr_fixtures/handauthored/01_crossing_junction_18.xodr"));
    roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();

    const gt_esmini::odr::OdrSideModel* sm = gt_esmini::odr::GetSideModel(odr);
    ASSERT_NE(sm, nullptr);

    const gt_esmini::odr::OdrJunctionExtras* j555 = FindJunction(sm, "555");
    ASSERT_NE(j555, nullptr) << "virtual junction 555 must carry crossPath extras";
    EXPECT_EQ(j555->type_str, "virtual");
    ASSERT_EQ(j555->cross_paths.size(), 1u);
    const gt_esmini::odr::OdrCrossPath& cp = j555->cross_paths[0];
    EXPECT_EQ(cp.id, "0");
    EXPECT_EQ(cp.crossing_road, "2");
    EXPECT_EQ(cp.road_at_start, "1");
    EXPECT_EQ(cp.road_at_end, "1");
    EXPECT_DOUBLE_EQ(cp.start_lane_link.s, 59.0);
    EXPECT_EQ(cp.start_lane_link.from, 2);
    EXPECT_EQ(cp.start_lane_link.to, -1);
    EXPECT_EQ(cp.end_lane_link.from, -2);
    EXPECT_EQ(cp.end_lane_link.to, -1);
    EXPECT_TRUE(j555->road_sections.empty());

    const gt_esmini::odr::OdrJunctionExtras* j666 = FindJunction(sm, "666");
    ASSERT_NE(j666, nullptr) << "crossing junction 666 must carry roadSection extras";
    EXPECT_EQ(j666->type_str, "crossing");
    ASSERT_EQ(j666->road_sections.size(), 1u);
    const gt_esmini::odr::OdrJunctionRoadSection& rs = j666->road_sections[0];
    EXPECT_EQ(rs.id, "0");
    EXPECT_EQ(rs.road_id, "1");
    EXPECT_DOUBLE_EQ(rs.s_start, 57.5);
    EXPECT_DOUBLE_EQ(rs.s_end, 61.5);
    EXPECT_TRUE(j666->cross_paths.empty());

    odr->Clear();
}

// (b) Priority accessor on the new 1.9 common-junction fixture (junction 900 <priority high=1 low=2>).
//     (d) laneLink overlapZone/fromLayer/toLayer L1 fields from the same fixture.
TEST(OdrJunctionExtras, Fixture21PriorityAndLaneLinkLayers)
{
    const std::string root = RepoRoot();
    ASSERT_FALSE(root.empty());
    ASSERT_TRUE(LoadXodr(root + "/GT_esmini/test/odr_fixtures/handauthored/21_common_junction_crosspath_19.xodr"));
    roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();

    // Priority accessor (F3 canonical source).
    std::vector<gt_esmini::odr::OdrJunctionPriority> prios;
    ASSERT_TRUE(gt_esmini::odr::GetJunctionPriorities(odr, "900", prios));
    ASSERT_EQ(prios.size(), 1u);
    EXPECT_EQ(prios[0].high, "1");
    EXPECT_EQ(prios[0].low, "2");

    // A junction with no side entry -> false, out untouched.
    std::vector<gt_esmini::odr::OdrJunctionPriority> none;
    EXPECT_FALSE(gt_esmini::odr::GetJunctionPriorities(odr, "does-not-exist", none));
    EXPECT_TRUE(none.empty());

    // laneLink 1.8/1.9 layer L1 fields (cluster 22 slot reservation).
    const gt_esmini::odr::OdrJunctionExtras* j900 = gt_esmini::odr::GetJunctionExtras(odr, "900");
    ASSERT_NE(j900, nullptr);
    ASSERT_EQ(j900->lane_link_extras.size(), 1u);
    const gt_esmini::odr::OdrLaneLinkExtras& ll = j900->lane_link_extras[0];
    EXPECT_EQ(ll.connection_id, "0");
    EXPECT_EQ(ll.from, -1);
    EXPECT_EQ(ll.to, -1);
    EXPECT_EQ(ll.overlap_zone, "0.5");
    EXPECT_EQ(ll.from_layer, "permanent");
    EXPECT_EQ(ll.to_layer, "permanent");

    // Common junction also carries the crossPath (crossingRoad=2) at the default type.
    ASSERT_EQ(j900->cross_paths.size(), 1u);
    EXPECT_EQ(j900->cross_paths[0].crossing_road, "2");
    EXPECT_EQ(j900->type_str, "");  // common/default junction: no @type authored

    odr->Clear();
}

// (c) Ex_Pedestrian_Crossing acceptance (iv). Official asset is gitignored / CI-absent -> guard +
//     GTEST_SKIP. Asserts priority {high="1", low="2"} + crossPath fields (crossingRoad=2,
//     startLaneLink s=59.0 from=4 to=-1) per the ASAM reference.
TEST(OdrJunctionExtras, ExPedestrianCrossingOfficial)
{
    const std::string root = RepoRoot();
    ASSERT_FALSE(root.empty());
    const std::string path = root + "/GT_esmini/test/odr_fixtures/official/examples/Ex_Pedestrian_Crossing/Ex_Pedestrian_Crossing.xodr";
    if (!fs::exists(path))
    {
        GTEST_SKIP() << "official Ex_Pedestrian_Crossing.xodr absent (gitignored; run odr_fixture_setup.py)";
    }
    ASSERT_TRUE(LoadXodr(path));
    roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();

    std::vector<gt_esmini::odr::OdrJunctionPriority> prios;
    ASSERT_TRUE(gt_esmini::odr::GetJunctionPriorities(odr, "555", prios));
    ASSERT_EQ(prios.size(), 1u);
    EXPECT_EQ(prios[0].high, "1");
    EXPECT_EQ(prios[0].low, "2");

    const gt_esmini::odr::OdrJunctionExtras* j = gt_esmini::odr::GetJunctionExtras(odr, "555");
    ASSERT_NE(j, nullptr);
    ASSERT_EQ(j->cross_paths.size(), 1u);
    const gt_esmini::odr::OdrCrossPath& cp = j->cross_paths[0];
    EXPECT_EQ(cp.crossing_road, "2");
    EXPECT_DOUBLE_EQ(cp.start_lane_link.s, 59.0);
    EXPECT_EQ(cp.start_lane_link.from, 4);
    EXPECT_EQ(cp.start_lane_link.to, -1);

    odr->Clear();
}

// (e) Sparse: a legacy 1.4 xodr whose junction carries no crossPath/roadSection/priority/controller/
//     laneLink-layer datum yields ZERO junction_extras entries.
TEST(OdrJunctionExtras, SparseLegacyNoJunctionExtras)
{
    const std::string root = RepoRoot();
    ASSERT_FALSE(root.empty());
    ASSERT_TRUE(LoadXodr(root + "/GT_esmini/test/odr_fixtures/handauthored/02_invalid_junction_connection_14.xodr"));
    roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();

    const gt_esmini::odr::OdrSideModel* sm = gt_esmini::odr::GetSideModel(odr);
    ASSERT_NE(sm, nullptr);
    EXPECT_TRUE(sm->junction_extras.empty()) << "a junction with no P5 extras must produce no side entry";

    odr->Clear();
}

// ===========================================================================
// P5 stage 2: crossPath -> synthesized CROSSWALK RMObject + PedPath polyline
// ===========================================================================

// Dump + sanity-check the synthesized footprint corner world coords (probe + assertion in one).
void DumpAndCheckFootprint(roadmanager::RMObject* cw)
{
    ASSERT_NE(cw, nullptr);
    EXPECT_GE(cw->GetId(), kSynthIdBase) << "synthetic id must be in the reserved range";
    ASSERT_GE(cw->GetNumberOfOutlines(), 1u);
    roadmanager::Outline* ol = cw->GetOutline(0);
    ASSERT_NE(ol, nullptr);
    EXPECT_TRUE(ol->closed_);
    ASSERT_EQ(ol->corner_.size(), 4u) << "closed 4-corner footprint";

    std::vector<std::array<double, 3>> pts;
    for (roadmanager::OutlineCorner* c : ol->corner_)
    {
        ASSERT_NE(c, nullptr);
        double x = 0.0, y = 0.0, z = 0.0;
        c->GetPos(x, y, z);
        EXPECT_TRUE(std::isfinite(x) && std::isfinite(y) && std::isfinite(z)) << "corner world coords finite";
        pts.push_back({x, y, z});
        std::cout << "[PROBE] corner world x=" << x << " y=" << y << " z=" << z << "\n";
    }
    // Corners mutually distinct (a real rectangle, not a collapsed point).
    for (size_t a = 0; a < pts.size(); a++)
    {
        for (size_t b = a + 1; b < pts.size(); b++)
        {
            const double dx = pts[a][0] - pts[b][0], dy = pts[a][1] - pts[b][1];
            EXPECT_GT(dx * dx + dy * dy, 1.0e-6) << "corners " << a << "," << b << " must be distinct";
        }
    }
}

// Fixture 01: virtual junction 555 crossPath -> exactly 1 CROSSWALK on the crossed road (1); crossing
// junction 666 (roadSection only) synthesizes nothing. Corners straddle road 1 (a straight along +x
// at y=0), so they must bracket y=0 and cluster near x=59 (crossPath @s=59 on road 1).
TEST(OdrJunctionExtras, Fixture01SynthesizeCrosswalk)
{
    const std::string root = RepoRoot();
    ASSERT_FALSE(root.empty());
    ASSERT_TRUE(LoadXodr(root + "/GT_esmini/test/odr_fixtures/handauthored/01_crossing_junction_18.xodr"));
    roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();

    roadmanager::Road* road1 = RoadByStr(odr, "1");
    roadmanager::Road* road2 = RoadByStr(odr, "2");
    ASSERT_NE(road1, nullptr);
    ASSERT_NE(road2, nullptr);

    EXPECT_EQ(CountCrosswalks(road1), 1) << "one CROSSWALK synthesized on the crossed road (1)";
    EXPECT_EQ(CountCrosswalks(road2), 0) << "no CROSSWALK on the crossing road (2)";

    roadmanager::RMObject* cw = FirstCrosswalk(road1);
    DumpAndCheckFootprint(cw);

    // Footprint must straddle road 1 (bracket y=0) and sit near x=59.
    double ymin = 1e30, ymax = -1e30, xmin = 1e30, xmax = -1e30;
    for (roadmanager::OutlineCorner* c : cw->GetOutline(0)->corner_)
    {
        double x = 0.0, y = 0.0, z = 0.0;
        c->GetPos(x, y, z);
        ymin = std::min(ymin, y);
        ymax = std::max(ymax, y);
        xmin = std::min(xmin, x);
        xmax = std::max(xmax, x);
    }
    EXPECT_LT(ymin, 0.0) << "footprint straddles road 1 centerline (y<0 side)";
    EXPECT_GT(ymax, 0.0) << "footprint straddles road 1 centerline (y>0 side)";
    EXPECT_NEAR(0.5 * (xmin + xmax), 59.0, 3.0) << "crossing centered near road-1 s=59 (x~59)";

    // The side model recorded the synthesized id and a monotonic PedPath polyline.
    const gt_esmini::odr::OdrJunctionExtras* j555 = FindJunction(gt_esmini::odr::GetSideModel(odr), "555");
    ASSERT_NE(j555, nullptr);
    ASSERT_EQ(j555->cross_paths.size(), 1u);
    EXPECT_GE(j555->cross_paths[0].synth_object_id, kSynthIdBase);
    const auto& pp = j555->cross_paths[0].ped_path;
    ASSERT_GE(pp.size(), 2u) << "PedPath polyline non-empty";
    for (size_t i = 1; i < pp.size(); i++)
    {
        EXPECT_GT(pp[i].s, pp[i - 1].s) << "PedPath s monotonic increasing";
        EXPECT_TRUE(std::isfinite(pp[i].x) && std::isfinite(pp[i].y));
    }
    std::cout << "[PROBE] fixture01 pedpath span s=[" << pp.front().s << "," << pp.back().s << "] world y=["
              << pp.front().y << "," << pp.back().y << "]\n";

    odr->Clear();
}

// Fixture 21: common junction 900 crossPath -> exactly 1 CROSSWALK on road 1.
TEST(OdrJunctionExtras, Fixture21SynthesizeCrosswalk)
{
    const std::string root = RepoRoot();
    ASSERT_FALSE(root.empty());
    ASSERT_TRUE(LoadXodr(root + "/GT_esmini/test/odr_fixtures/handauthored/21_common_junction_crosspath_19.xodr"));
    roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();

    roadmanager::Road* road1 = RoadByStr(odr, "1");
    ASSERT_NE(road1, nullptr);
    EXPECT_EQ(CountCrosswalks(road1), 1) << "one CROSSWALK on road 1 (crossed road)";
    DumpAndCheckFootprint(FirstCrosswalk(road1));

    odr->Clear();
}

// Ex_Pedestrian_Crossing (official; guard-skip if absent): 1 CROSSWALK on road 1; footprint within
// the junction span (mainRoad s in [57.5,61.5] -> world x in [127.5,131.5], y around 360).
TEST(OdrJunctionExtras, ExPedestrianCrossingSynthesizeCrosswalk)
{
    const std::string root = RepoRoot();
    ASSERT_FALSE(root.empty());
    const std::string path = root + "/GT_esmini/test/odr_fixtures/official/examples/Ex_Pedestrian_Crossing/Ex_Pedestrian_Crossing.xodr";
    if (!fs::exists(path))
    {
        GTEST_SKIP() << "official Ex_Pedestrian_Crossing.xodr absent (gitignored; run odr_fixture_setup.py)";
    }
    ASSERT_TRUE(LoadXodr(path));
    roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();

    roadmanager::Road* road1 = RoadByStr(odr, "1");
    ASSERT_NE(road1, nullptr);
    EXPECT_EQ(CountCrosswalks(road1), 1);
    roadmanager::RMObject* cw = FirstCrosswalk(road1);
    DumpAndCheckFootprint(cw);

    double xmin = 1e30, xmax = -1e30, ymin = 1e30, ymax = -1e30;
    for (roadmanager::OutlineCorner* c : cw->GetOutline(0)->corner_)
    {
        double x = 0.0, y = 0.0, z = 0.0;
        c->GetPos(x, y, z);
        xmin = std::min(xmin, x);
        xmax = std::max(xmax, x);
        ymin = std::min(ymin, y);
        ymax = std::max(ymax, y);
    }
    std::cout << "[PROBE] Ex_Pedestrian_Crossing footprint x=[" << xmin << "," << xmax << "] y=[" << ymin << "," << ymax << "]\n";
    // mainRoad 1: x = 70 + s. Crossing at s~59 -> x~129, well inside the junction s-span carriageway.
    EXPECT_GT(0.5 * (xmin + xmax), 125.0);
    EXPECT_LT(0.5 * (xmin + xmax), 134.0);
    // road 1 y=360; footprint must straddle it.
    EXPECT_LT(ymin, 360.0);
    EXPECT_GT(ymax, 360.0);

    odr->Clear();
}

// Collision: an authored object with an id INSIDE the reserved synthetic range must make synthesis
// SKIP (no crash, authored object intact, no CROSSWALK added). Written to a temp xodr at runtime.
TEST(OdrJunctionExtras, ReservedIdCollisionSkipsSynthesis)
{
    const std::string root = RepoRoot();
    ASSERT_FALSE(root.empty());

    // Minimal virtual-junction crossPath xodr whose crossed road (1) already carries an object with
    // an id in the reserved range (>= 900000000).
    const std::string xodr =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<OpenDRIVE>\n"
        "  <header revMajor=\"1\" revMinor=\"8\" name=\"collide\" version=\"1.00\" date=\"2026-07-03T00:00:00\"/>\n"
        "  <road name=\"main\" length=\"120.0\" id=\"1\" junction=\"-1\" rule=\"RHT\">\n"
        "    <planView><geometry s=\"0.0\" x=\"0.0\" y=\"0.0\" hdg=\"0.0\" length=\"120.0\"><line/></geometry></planView>\n"
        "    <lanes><laneSection s=\"0.0\">\n"
        "      <center><lane id=\"0\" type=\"none\"/></center>\n"
        "      <right><lane id=\"-1\" type=\"driving\"><width sOffset=\"0.0\" a=\"3.5\" b=\"0\" c=\"0\" d=\"0\"/></lane></right>\n"
        "    </laneSection></lanes>\n"
        "    <objects>\n"
        "      <object id=\"900000001\" s=\"10.0\" t=\"0.0\" type=\"barrier\" width=\"1\" length=\"1\" height=\"1\"/>\n"
        "    </objects>\n"
        "  </road>\n"
        "  <road name=\"ped\" length=\"14.0\" id=\"2\" junction=\"555\" rule=\"RHT\">\n"
        "    <planView><geometry s=\"0.0\" x=\"59.0\" y=\"-7.0\" hdg=\"1.5707963267948966\" length=\"14.0\"><line/></geometry></planView>\n"
        "    <lanes><laneSection s=\"0.0\">\n"
        "      <center><lane id=\"0\" type=\"none\"/></center>\n"
        "      <right><lane id=\"-1\" type=\"walking\"><width sOffset=\"0.0\" a=\"3.0\" b=\"0\" c=\"0\" d=\"0\"/></lane></right>\n"
        "    </laneSection></lanes>\n"
        "  </road>\n"
        "  <junction name=\"j\" type=\"virtual\" id=\"555\" mainRoad=\"1\" sStart=\"57.5\" sEnd=\"61.5\">\n"
        "    <crossPath id=\"0\" crossingRoad=\"2\" roadAtStart=\"1\" roadAtEnd=\"1\">\n"
        "      <startLaneLink s=\"59.0\" from=\"2\" to=\"-1\"/>\n"
        "      <endLaneLink s=\"59.0\" from=\"-2\" to=\"-1\"/>\n"
        "    </crossPath>\n"
        "  </junction>\n"
        "</OpenDRIVE>\n";

    const fs::path tmp = fs::temp_directory_path() / "gt_odr_p5_collision.xodr";
    {
        std::ofstream ofs(tmp);
        ASSERT_TRUE(ofs.good());
        ofs << xodr;
    }

    ASSERT_TRUE(LoadXodr(tmp.string()));
    roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();
    roadmanager::Road*      road1 = RoadByStr(odr, "1");
    ASSERT_NE(road1, nullptr);

    // Authored object intact, no CROSSWALK synthesized.
    EXPECT_EQ(CountCrosswalks(road1), 0) << "collision -> synthesis skipped";
    bool has_authored = false;
    for (idx_t j = 0; j < road1->GetNumberOfObjects(); j++)
    {
        roadmanager::RMObject* o = road1->GetRoadObject(j);
        if (o != nullptr && o->GetId() == 900000001u)
        {
            has_authored = true;
        }
    }
    EXPECT_TRUE(has_authored) << "authored object in reserved range must remain intact";

    // The crossPath was still parsed L1 but records no synthesized object.
    const gt_esmini::odr::OdrJunctionExtras* j = gt_esmini::odr::GetJunctionExtras(odr, "555");
    ASSERT_NE(j, nullptr);
    ASSERT_EQ(j->cross_paths.size(), 1u);
    EXPECT_EQ(j->cross_paths[0].synth_object_id, 0u);

    odr->Clear();
    std::error_code ec;
    fs::remove(tmp, ec);
}

// Sparse/no-op: a legacy (1.4) asset with no crossPath adds ZERO CROSSWALK objects to any road.
TEST(OdrJunctionExtras, LegacyNoCrosswalkSynthesis)
{
    const std::string root = RepoRoot();
    ASSERT_FALSE(root.empty());
    ASSERT_TRUE(LoadXodr(root + "/GT_esmini/test/odr_fixtures/handauthored/02_invalid_junction_connection_14.xodr"));
    roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();

    int total = 0;
    for (unsigned int i = 0; i < odr->GetNumOfRoads(); i++)
    {
        total += CountCrosswalks(odr->GetRoadByIdx(i));
    }
    EXPECT_EQ(total, 0) << "no crossPath -> zero synthesized crosswalks";

    odr->Clear();
}
