// test_OdrJunctionExtras.cpp -- P5 junction side-model tests (plan P5, clusters 5/7/22).
//
// Loads real fixtures through the actual parser (LoadOpenDriveFile -> [GT_ODR:hook] BuildSideModel)
// and asserts the OdrJunctionExtras L1 storage + the F3 priority accessor. Template mirrors
// test_OdrForkPatches.cpp SignalExtrasDependencyReference. Source-of-truth repo root via the
// GT_ODR_REPO_ROOT compile def.
#include <gtest/gtest.h>

#include <filesystem>
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
    EXPECT_EQ(ll.from_layer, "0");
    EXPECT_EQ(ll.to_layer, "0");

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
