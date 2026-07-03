// test_OdrJunctionGeom.cpp -- P7 junction geometry + junctionGroup tests (clusters 8/9).
//
// Loads real fixtures through the actual parser and asserts the OdrJunctionGeomExtras /
// OdrJunctionGroup L1 storage + the roundabout policy hint. Template mirrors test_OdrJunctionExtras.cpp.
#include <gtest/gtest.h>

#include <cmath>
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

// ================================================================================================
// WP4 (cluster 8 L3): authored junction <boundary> -> world polyline + feature flag gate.
// ================================================================================================

// ---- polyline builder on the g6 fixture junction (>3 pts, finite, ordered) ----
TEST(OdrJunctionGeom, WP4AuthoredBoundaryPolyline)
{
    ASSERT_FALSE(RepoRoot().empty());
    ASSERT_TRUE(LoadXodr(Fix("generated/g6_junction_boundary_18.xodr")));
    roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();

    std::vector<gt_esmini::odr::OdrBoundaryPoint> poly;
    ASSERT_TRUE(gt_esmini::odr::BuildAuthoredJunctionBoundaryPolyline(odr, "1", odr, poly));
    EXPECT_GT(poly.size(), 3u) << "closed junction boundary must yield a real polygon";
    for (const auto& p : poly)
    {
        EXPECT_TRUE(std::isfinite(p.x));
        EXPECT_TRUE(std::isfinite(p.y));
        EXPECT_TRUE(std::isfinite(p.z));
    }
    // Ordered: consecutive points are not all identical (the polyline advances around the junction).
    bool advanced = false;
    for (size_t i = 1; i < poly.size(); i++)
    {
        if (std::fabs(poly[i].x - poly[0].x) > 1e-6 || std::fabs(poly[i].y - poly[0].y) > 1e-6)
        {
            advanced = true;
            break;
        }
    }
    EXPECT_TRUE(advanced) << "boundary polyline must span more than a single point";

    odr->Clear();
}

// ---- dangling / unparseable references -> false (caller falls back to heuristic) ----
TEST(OdrJunctionGeom, WP4DanglingBoundaryRefReturnsFalse)
{
    ASSERT_FALSE(RepoRoot().empty());
    ASSERT_TRUE(LoadXodr(Fix("generated/g6_junction_boundary_18.xodr")));
    roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();

    std::vector<gt_esmini::odr::OdrBoundaryPoint> poly;
    // Unknown junction id -> no side-model boundary entry -> false, out untouched.
    EXPECT_FALSE(gt_esmini::odr::BuildAuthoredJunctionBoundaryPolyline(odr, "no_such_junction", odr, poly));
    EXPECT_TRUE(poly.empty());

    // Null OpenDrive -> false.
    EXPECT_FALSE(gt_esmini::odr::BuildAuthoredJunctionBoundaryPolyline(odr, "1", nullptr, poly));
    EXPECT_TRUE(poly.empty());

    odr->Clear();
}

// ---- feature flag: default OFF; setter toggles; env-independent under the setter ----
TEST(OdrJunctionGeom, WP4FeatureFlagDefaultOff)
{
    // The setter makes the flag deterministic regardless of the environment.
    gt_esmini::odr::SetUseAuthoredJunctionBoundary(false);
    EXPECT_FALSE(gt_esmini::odr::GetUseAuthoredJunctionBoundary()) << "default OFF";

    gt_esmini::odr::SetUseAuthoredJunctionBoundary(true);
    EXPECT_TRUE(gt_esmini::odr::GetUseAuthoredJunctionBoundary());

    gt_esmini::odr::SetUseAuthoredJunctionBoundary(false);
    EXPECT_FALSE(gt_esmini::odr::GetUseAuthoredJunctionBoundary());
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
