// test_OdrJunctionGeom.cpp -- P7 junction geometry + junctionGroup tests (clusters 8/9).
//
// Loads real fixtures through the actual parser and asserts the OdrJunctionGeomExtras /
// OdrJunctionGroup L1 storage + the roundabout policy hint. Template mirrors test_OdrJunctionExtras.cpp.
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "RoadManager.hpp"
#include "gt_esmini/road/OdrSideModel.hpp"

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

std::string Fix(const std::string& rel)
{
    return RepoRoot() + "/GT_esmini/test/odr_fixtures/" + rel;
}
}  // namespace

// ---- cluster 9: junctionGroup roundabout hint (fixture 25) ----
TEST(OdrJunctionGeom, Fixture25JunctionGroupRoundabout)
{
    ASSERT_FALSE(RepoRoot().empty());
    ASSERT_TRUE(LoadXodr(Fix("handauthored/25_junction_group_15.xodr")));
    roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();

    std::vector<gt_esmini::odr::OdrJunctionGroup> groups;
    ASSERT_TRUE(gt_esmini::odr::GetJunctionGroups(odr, groups));
    ASSERT_EQ(groups.size(), 1u);
    EXPECT_EQ(groups[0].id, "900");
    EXPECT_EQ(groups[0].name, "mini_roundabout");
    EXPECT_EQ(groups[0].type, "roundabout");
    ASSERT_EQ(groups[0].members.size(), 1u);
    EXPECT_EQ(groups[0].members[0], "100");

    // Policy hint: junction 100 is in a roundabout group; junction 999 is not.
    EXPECT_TRUE(gt_esmini::odr::IsJunctionInRoundabout(odr, "100"));
    EXPECT_FALSE(gt_esmini::odr::IsJunctionInRoundabout(odr, "999"));

    odr->Clear();
}

// ---- cluster 8: junction boundary storage (fixture g6) ----
TEST(OdrJunctionGeom, Fixtureg6JunctionBoundary)
{
    ASSERT_FALSE(RepoRoot().empty());
    ASSERT_TRUE(LoadXodr(Fix("generated/g6_junction_boundary_18.xodr")));
    roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();

    const gt_esmini::odr::OdrJunctionGeomExtras* geom = gt_esmini::odr::GetJunctionGeom(odr, "1");
    ASSERT_NE(geom, nullptr);
    ASSERT_EQ(geom->boundary.size(), 4u) << "four boundary segments";
    EXPECT_EQ(geom->boundary[0].type, "lane");
    EXPECT_EQ(geom->boundary[0].road_id, "0");
    EXPECT_EQ(geom->boundary[0].boundary_lane, "-1");
    EXPECT_EQ(geom->boundary[0].s_start, "end");
    EXPECT_FALSE(geom->has_grid) << "g6 has no elevationGrid";
    EXPECT_TRUE(geom->surface_crgs.empty());
    EXPECT_EQ(geom->object_count, 0);

    odr->Clear();
}

// ---- cluster 8: junction boundary + elevationGrid + surface CRG + objects (fixture g8) ----
TEST(OdrJunctionGeom, Fixtureg8JunctionGrid)
{
    ASSERT_FALSE(RepoRoot().empty());
    ASSERT_TRUE(LoadXodr(Fix("generated/g8_junction_grid_18.xodr")));
    roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();

    const gt_esmini::odr::OdrJunctionGeomExtras* geom = gt_esmini::odr::GetJunctionGeom(odr, "1");
    ASSERT_NE(geom, nullptr);
    EXPECT_EQ(geom->boundary.size(), 4u);
    EXPECT_TRUE(geom->has_grid);
    EXPECT_EQ(geom->grid_spacing, "1.0");
    ASSERT_EQ(geom->grid_elevations.size(), 1u);
    EXPECT_EQ(geom->grid_elevations[0].center, "0.0");
    EXPECT_EQ(geom->grid_elevations[0].left, "0.0 0.0");
    ASSERT_EQ(geom->surface_crgs.size(), 1u);
    EXPECT_EQ(geom->surface_crgs[0].file, "./does_not_exist.crg");
    EXPECT_FALSE(geom->surface_crgs[0].file_exists);
    EXPECT_EQ(geom->object_count, 1) << "one junction-level object counted";

    odr->Clear();
}

// ---- sparse: a legacy junction with no boundary/grid/objects/surface yields no geom entry ----
TEST(OdrJunctionGeom, SparseLegacyNoJunctionGeom)
{
    ASSERT_FALSE(RepoRoot().empty());
    ASSERT_TRUE(LoadXodr(Fix("handauthored/02_invalid_junction_connection_14.xodr")));
    roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();

    const gt_esmini::odr::OdrSideModel* sm = gt_esmini::odr::GetSideModel(odr);
    ASSERT_NE(sm, nullptr);
    EXPECT_TRUE(sm->junction_geom.empty());
    EXPECT_TRUE(sm->junction_groups.empty());
    std::vector<gt_esmini::odr::OdrJunctionGroup> empty_groups;
    EXPECT_FALSE(gt_esmini::odr::GetJunctionGroups(odr, empty_groups));

    odr->Clear();
}
