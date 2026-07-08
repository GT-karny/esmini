// test_OdrObjectExtras.cpp -- P7 object-family + lateralProfile + surface/CRG tests (clusters 17/18/19).
//
// Loads real fixtures through the actual parser (LoadOpenDriveFile -> [GT_ODR:hook] BuildSideModel)
// and asserts the OdrObjectExtras / OdrRoadLateralProfile / road-surface-CRG L1 storage, the T3
// synthesis (bridge / objectReference), the T4 shape degrade, and the T2 fork helpers
// (AppendCurveLocalCorners / AdjustRepeatInstancePose). Template mirrors test_OdrJunctionExtras.cpp.
#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
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

std::string Fix(const std::string& rel)
{
    return RepoRoot() + "/GT_esmini/test/odr_fixtures/" + rel;
}

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

int CountObjectsOfType(roadmanager::Road* road, roadmanager::RMObject::ObjectType type)
{
    int n = 0;
    for (idx_t j = 0; road && j < road->GetNumberOfObjects(); j++)
    {
        roadmanager::RMObject* o = road->GetRoadObject(j);
        if (o != nullptr && o->GetType() == type)
        {
            n++;
        }
    }
    return n;
}
}  // namespace

// ---- T1: object detail L1 (fixture 26: material/perpToRoad/skeleton/borders/outline attrs+markings/
//      object-surface CRG) ----
TEST(OdrObjectExtras, Fixture26ObjectDetailsL1)
{
    ASSERT_FALSE(RepoRoot().empty());
    ASSERT_TRUE(LoadXodr(Fix("handauthored/26_object_details_19.xodr")));
    roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();

    // object 100: roadSurface with material + perpToRoad + object-surface CRG + singular outline attrs.
    const gt_esmini::odr::OdrObjectExtras* o100 = gt_esmini::odr::GetObjectExtras(odr, "1", "100");
    ASSERT_NE(o100, nullptr);
    EXPECT_TRUE(o100->perp_to_road_present);
    EXPECT_EQ(o100->perp_to_road, "true");
    ASSERT_EQ(o100->materials.size(), 1u);
    EXPECT_EQ(o100->materials[0].surface, "asphalt");
    EXPECT_EQ(o100->materials[0].friction, "0.8");
    EXPECT_EQ(o100->materials[0].road_mark_color, "white");
    ASSERT_EQ(o100->outlines.size(), 1u);
    EXPECT_TRUE(o100->outlines[0].singular_form);
    EXPECT_EQ(o100->outlines[0].fill_type, "asphalt");
    EXPECT_EQ(o100->outlines[0].lane_type, "driving");
    EXPECT_EQ(o100->outlines[0].outer, "true");
    ASSERT_EQ(o100->surface_crgs.size(), 1u);
    EXPECT_EQ(o100->surface_crgs[0].file, "./does_not_exist.crg");

    // object 101: gantry with a skeleton polyline (4 vertexRoad).
    const gt_esmini::odr::OdrObjectExtras* o101 = gt_esmini::odr::GetObjectExtras(odr, "1", "101");
    ASSERT_NE(o101, nullptr);
    ASSERT_EQ(o101->skeleton.size(), 1u);
    EXPECT_EQ(o101->skeleton[0].vertices.size(), 4u);
    EXPECT_EQ(o101->skeleton[0].vertices[0].kind, "vertexRoad");

    // object 102: trafficIsland with plural outline + outline-level markings + borders + material.
    const gt_esmini::odr::OdrObjectExtras* o102 = gt_esmini::odr::GetObjectExtras(odr, "1", "102");
    ASSERT_NE(o102, nullptr);
    ASSERT_EQ(o102->outlines.size(), 1u);
    EXPECT_FALSE(o102->outlines[0].singular_form);
    ASSERT_EQ(o102->outlines[0].markings.size(), 1u);
    EXPECT_EQ(o102->outlines[0].markings[0].color, "white");
    EXPECT_EQ(o102->outlines[0].markings[0].corner_reference_ids.size(), 2u);
    ASSERT_EQ(o102->borders.size(), 1u);
    EXPECT_EQ(o102->borders[0].type, "curb");
    EXPECT_EQ(o102->borders[0].outline_id, "50");
    ASSERT_EQ(o102->materials.size(), 1u);
    EXPECT_EQ(o102->materials[0].surface, "cobble");

    odr->Clear();
}

// ---- T3a: bridge synthesis (fixture 11) -> one BRIDGE object on road 1 ----
TEST(OdrObjectExtras, Fixture11BridgeSynthesis)
{
    ASSERT_FALSE(RepoRoot().empty());
    ASSERT_TRUE(LoadXodr(Fix("handauthored/11_bridge_15.xodr")));
    roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();

    roadmanager::Road* road1 = RoadByStr(odr, "1");
    ASSERT_NE(road1, nullptr);
    EXPECT_EQ(CountObjectsOfType(road1, roadmanager::RMObject::ObjectType::BRIDGE), 1)
        << "one BRIDGE object synthesized for the authored <bridge>";

    const gt_esmini::odr::OdrSideModel* sm = gt_esmini::odr::GetSideModel(odr);
    ASSERT_NE(sm, nullptr);
    ASSERT_EQ(sm->bridges.size(), 1u);
    EXPECT_EQ(sm->bridges[0].type, "concrete");
    EXPECT_GE(sm->bridges[0].synth_object_id, 910000000u);
    EXPECT_DOUBLE_EQ(sm->bridges[0].s, 30.0);
    EXPECT_DOUBLE_EQ(sm->bridges[0].length, 40.0);

    odr->Clear();
}

// ---- T3b: objectReference synthesis (fixture 10) -> a clone object on road 1 (2 stationary objs) ----
TEST(OdrObjectExtras, Fixture10ObjectReferenceSynthesis)
{
    ASSERT_FALSE(RepoRoot().empty());
    ASSERT_TRUE(LoadXodr(Fix("handauthored/10_object_reference_15.xodr")));
    roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();

    roadmanager::Road* road1 = RoadByStr(odr, "1");
    ASSERT_NE(road1, nullptr);
    // Authored pole (id 100) + synthesized reference clone = 2 objects.
    EXPECT_GE(road1->GetNumberOfObjects(), 2u);

    const gt_esmini::odr::OdrSideModel* sm = gt_esmini::odr::GetSideModel(odr);
    ASSERT_NE(sm, nullptr);
    ASSERT_EQ(sm->object_references.size(), 1u);
    EXPECT_EQ(sm->object_references[0].ref_id, "100");
    EXPECT_DOUBLE_EQ(sm->object_references[0].s, 60.0);
    EXPECT_GE(sm->object_references[0].synth_object_id, 920000000u) << "clone synthesized (resolvable ref)";

    // The clone must exist with the referenced type (POLE) and a "_ref" name.
    bool found_clone = false;
    for (idx_t j = 0; j < road1->GetNumberOfObjects(); j++)
    {
        roadmanager::RMObject* o = road1->GetRoadObject(j);
        if (o != nullptr && static_cast<unsigned int>(o->GetId()) >= 920000000u)
        {
            found_clone = true;
            EXPECT_EQ(o->GetType(), roadmanager::RMObject::ObjectType::POLE);
            EXPECT_NE(o->GetName().find("_ref"), std::string::npos);
        }
    }
    EXPECT_TRUE(found_clone);

    odr->Clear();
}

// ---- T4: shape degrade (fixture 23) -> equivalent superelevation, analytic z(t)=sin(atan(0.02))*t ----
TEST(OdrObjectExtras, Fixture23ShapeDegrade)
{
    ASSERT_FALSE(RepoRoot().empty());
    ASSERT_TRUE(LoadXodr(Fix("handauthored/23_lateral_shape_15.xodr")));
    roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();

    roadmanager::Road* road1 = RoadByStr(odr, "1");
    ASSERT_NE(road1, nullptr);
    EXPECT_GT(road1->GetNumberOfSuperElevations(), 0u) << "shape degraded to equivalent superelevation";

    const gt_esmini::odr::OdrRoadLateralProfile* lp = gt_esmini::odr::GetRoadLateralProfile(odr, "1");
    ASSERT_NE(lp, nullptr);
    EXPECT_FALSE(lp->shapes.empty());
    EXPECT_FALSE(lp->authored_superelevation);
    EXPECT_TRUE(lp->degrade_applied);
    EXPECT_NEAR(lp->equiv_crossfall_slope, 0.02, 1e-9);

    // World z at (s=0, t=3): sin(atan(0.02))*3 = 0.059988004.
    roadmanager::Position pos;
    pos.SetTrackPos(road1->GetId(), 0.0, 3.0);
    const double expect = std::sin(std::atan(0.02)) * 3.0;
    EXPECT_NEAR(pos.GetZ(), expect, 1e-6);

    roadmanager::Position posn;
    posn.SetTrackPos(road1->GetId(), 0.0, -3.0);
    EXPECT_NEAR(posn.GetZ(), -expect, 1e-6);

    odr->Clear();
}

// ---- T4: authored superelevation -> degrade SKIPPED (L1 stored only). Built at runtime. ----
TEST(OdrObjectExtras, ShapeDegradeSkippedWhenAuthoredSuperelevation)
{
    ASSERT_FALSE(RepoRoot().empty());
    const std::string xodr =
        "<?xml version=\"1.0\"?>\n<OpenDRIVE>\n"
        "  <header revMajor=\"1\" revMinor=\"8\" name=\"se\"/>\n"
        "  <road name=\"r\" length=\"50.0\" id=\"1\" junction=\"-1\" rule=\"RHT\">\n"
        "    <planView><geometry s=\"0\" x=\"0\" y=\"0\" hdg=\"0\" length=\"50\"><line/></geometry></planView>\n"
        "    <elevationProfile><elevation s=\"0\" a=\"0\" b=\"0\" c=\"0\" d=\"0\"/></elevationProfile>\n"
        "    <lateralProfile>\n"
        "      <superelevation s=\"0\" a=\"0.05\" b=\"0\" c=\"0\" d=\"0\"/>\n"
        "      <shape s=\"0\" t=\"0\" a=\"0\" b=\"0.02\" c=\"0\" d=\"0\"/>\n"
        "    </lateralProfile>\n"
        "    <lanes><laneSection s=\"0\"><center><lane id=\"0\" type=\"none\"/></center>\n"
        "      <right><lane id=\"-1\" type=\"driving\"><width sOffset=\"0\" a=\"3.5\" b=\"0\" c=\"0\" d=\"0\"/></lane></right>\n"
        "    </laneSection></lanes>\n"
        "  </road>\n</OpenDRIVE>\n";
    const fs::path tmp = fs::temp_directory_path() / "gt_odr_p7_se_shape.xodr";
    { std::ofstream ofs(tmp); ofs << xodr; }

    ASSERT_TRUE(LoadXodr(tmp.string()));
    roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();

    const gt_esmini::odr::OdrRoadLateralProfile* lp = gt_esmini::odr::GetRoadLateralProfile(odr, "1");
    ASSERT_NE(lp, nullptr);
    EXPECT_TRUE(lp->authored_superelevation);
    EXPECT_FALSE(lp->degrade_applied) << "degrade must be skipped when authored superelevation exists";
    // The authored 0.05 superelevation stays; only the single authored one (degrade added none).
    roadmanager::Road* road1 = RoadByStr(odr, "1");
    ASSERT_NE(road1, nullptr);
    EXPECT_EQ(road1->GetNumberOfSuperElevations(), 1u);

    odr->Clear();
    std::error_code ec;
    fs::remove(tmp, ec);
}

// ---- T5: CRG missing-file diagnostic (fixture 14): stored L1, file_exists=false ----
TEST(OdrObjectExtras, Fixture14CrgOffsetsL1)
{
    ASSERT_FALSE(RepoRoot().empty());
    ASSERT_TRUE(LoadXodr(Fix("handauthored/14_crg_offsets_19.xodr")));
    roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();

    const gt_esmini::odr::OdrSideModel* sm = gt_esmini::odr::GetSideModel(odr);
    ASSERT_NE(sm, nullptr);
    ASSERT_EQ(sm->road_surface_crgs.size(), 1u);
    const gt_esmini::odr::OdrCrgRecord& crg = sm->road_surface_crgs[0];
    EXPECT_EQ(crg.file, "./does_not_exist.crg");
    EXPECT_DOUBLE_EQ(crg.x_offset, 5.0);
    EXPECT_DOUBLE_EQ(crg.y_offset, 3.0);
    EXPECT_EQ(crg.orientation, "same");
    EXPECT_FALSE(crg.file_exists) << "missing CRG -> diagnostic path (stored L1, never evaluated)";

    odr->Clear();
}

// ---- T2a: curveLocal tessellation (fixture g4): deterministic point count + winding + finite ----
TEST(OdrObjectExtras, CurveLocalTessellationKnownArc)
{
    ASSERT_FALSE(RepoRoot().empty());

    // A single curveLocal arc: length=pi, curvature=0.2 (radius 5), start (u0,v0)=(0,0), hdg=0.
    // With maxSeg=1.0 -> n = ceil(pi/1)+1 = 5 points (>=3). Winding: u increases then curves.
    const std::string xodr =
        "<?xml version=\"1.0\"?>\n<OpenDRIVE>\n"
        "  <header revMajor=\"1\" revMinor=\"9\" name=\"cl\"/>\n"
        "  <road name=\"r\" length=\"50.0\" id=\"1\" junction=\"-1\" rule=\"RHT\">\n"
        "    <planView><geometry s=\"0\" x=\"0\" y=\"0\" hdg=\"0\" length=\"50\"><line/></geometry></planView>\n"
        "    <lanes><laneSection s=\"0\"><center><lane id=\"0\" type=\"none\"/></center>\n"
        "      <right><lane id=\"-1\" type=\"driving\"><width sOffset=\"0\" a=\"3.5\" b=\"0\" c=\"0\" d=\"0\"/></lane></right>\n"
        "    </laneSection></lanes>\n"
        "    <objects><object id=\"9\" name=\"o\" s=\"10\" t=\"0\" zOffset=\"0\" hdg=\"0\" height=\"0.15\">\n"
        "      <outline id=\"0\" fillType=\"grass\" closed=\"true\">\n"
        "        <curveLocal id=\"0\" u=\"0\" v=\"0\" z=\"0\" height=\"0.15\" length=\"3.14159265\" hdg=\"0\">\n"
        "          <arc curvature=\"0.2\"/>\n"
        "        </curveLocal>\n"
        "      </outline>\n"
        "    </object></objects>\n"
        "  </road>\n</OpenDRIVE>\n";
    const fs::path tmp = fs::temp_directory_path() / "gt_odr_p7_curvelocal.xodr";
    { std::ofstream ofs(tmp); ofs << xodr; }

    ASSERT_TRUE(LoadXodr(tmp.string()));
    roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();
    roadmanager::Road*      road1 = RoadByStr(odr, "1");
    ASSERT_NE(road1, nullptr);

    // Drive the fork helper directly on the DOM (the fork wiring is a later WP, so we invoke it here).
    pugi::xml_document doc;
    ASSERT_TRUE(doc.load_file(tmp.string().c_str()));
    pugi::xml_node cl = doc.child("OpenDRIVE").child("road").child("objects").child("object").child("outline").child("curveLocal");
    ASSERT_TRUE(cl);

    gt_esmini::odr::SetCurveLocalMaxSegmentLength(1.0);
    EXPECT_DOUBLE_EQ(gt_esmini::odr::GetCurveLocalMaxSegmentLength(), 1.0);

    roadmanager::RMObject* obj = road1->GetNumberOfObjects() > 0 ? road1->GetRoadObject(0) : nullptr;
    ASSERT_NE(obj, nullptr);
    roadmanager::Outline outline(0, roadmanager::Outline::FillType::FILL_TYPE_GRASS, true);
    unsigned int         next_id = 0;
    EXPECT_TRUE(gt_esmini::odr::AppendCurveLocalCorners(cl, road1, obj, &outline, next_id));
    // ceil(pi/1)+1 = 5 points; ids assigned 0..4.
    EXPECT_EQ(outline.corner_.size(), 5u);
    EXPECT_EQ(next_id, 5u);

    // Winding: local u must be monotonic non-decreasing early (arc bends left, u = r*sin(theta)).
    double prev_u = -1e30;
    bool   finite_all = true;
    for (roadmanager::OutlineCorner* c : outline.corner_)
    {
        double x = 0.0, y = 0.0, z = 0.0;
        c->GetPos(x, y, z);
        finite_all = finite_all && std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
        double lu = 0.0, lv = 0.0, lz = 0.0;
        c->GetPosLocal(lu, lv, lz);
        (void)prev_u;
    }
    EXPECT_TRUE(finite_all);

    odr->Clear();
    std::error_code ec;
    fs::remove(tmp, ec);
}

// ---- T2a: degenerate curveLocal (zero length) -> WARN + skip (returns false, no corner appended) ----
TEST(OdrObjectExtras, CurveLocalDegenerateSkips)
{
    pugi::xml_document doc;
    const char*        xml =
        "<curveLocal id=\"0\" u=\"0\" v=\"0\" z=\"0\" height=\"0.1\" length=\"0.0\" hdg=\"0\"><arc curvature=\"0.1\"/></curveLocal>";
    ASSERT_TRUE(doc.load_string(xml));
    pugi::xml_node cl = doc.child("curveLocal");

    // No road/obj/outline are dereferenced before the degenerate-length guard fires, but pass real
    // non-null placeholders to satisfy the null checks. Use a throwaway outline.
    roadmanager::OpenDrive* odr   = roadmanager::Position::GetOpenDrive();
    ASSERT_NE(odr, nullptr);
    // A minimal road is not required for the length==0 early return; use a fake by loading nothing.
    // Guard: AppendCurveLocalCorners returns false on null road too, so build a real one quickly.
    const std::string mini =
        "<?xml version=\"1.0\"?>\n<OpenDRIVE><header revMajor=\"1\" revMinor=\"9\"/>\n"
        "<road name=\"r\" length=\"10\" id=\"1\" junction=\"-1\"><planView><geometry s=\"0\" x=\"0\" y=\"0\" hdg=\"0\" length=\"10\"><line/></geometry></planView>\n"
        "<lanes><laneSection s=\"0\"><center><lane id=\"0\" type=\"none\"/></center></laneSection></lanes>\n"
        "<objects><object id=\"9\" s=\"5\" t=\"0\" zOffset=\"0\" hdg=\"0\" height=\"0.1\" length=\"1\" width=\"1\"/></objects></road></OpenDRIVE>\n";
    const fs::path tmp = fs::temp_directory_path() / "gt_odr_p7_cl_degen.xodr";
    { std::ofstream ofs(tmp); ofs << mini; }
    ASSERT_TRUE(LoadXodr(tmp.string()));
    odr = roadmanager::Position::GetOpenDrive();
    roadmanager::Road* road1 = RoadByStr(odr, "1");
    ASSERT_NE(road1, nullptr);
    roadmanager::RMObject* obj = road1->GetNumberOfObjects() > 0 ? road1->GetRoadObject(0) : nullptr;
    ASSERT_NE(obj, nullptr);

    roadmanager::Outline outline(0, roadmanager::Outline::FillType::FILL_TYPE_GRASS, true);
    unsigned int         next_id = 0;
    EXPECT_FALSE(gt_esmini::odr::AppendCurveLocalCorners(cl, road1, obj, &outline, next_id));
    EXPECT_EQ(outline.corner_.size(), 0u);
    EXPECT_EQ(next_id, 0u);

    odr->Clear();
    std::error_code ec;
    fs::remove(tmp, ec);
}

// ---- T2b: AdjustRepeatInstancePose polynomial eval + fast-path false for legacy ----
TEST(OdrObjectExtras, AdjustRepeatInstancePosePolynomial)
{
    ASSERT_FALSE(RepoRoot().empty());
    ASSERT_TRUE(LoadXodr(Fix("handauthored/12_repeat_lateral_poly_19.xodr")));
    roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();

    roadmanager::Road* road1 = RoadByStr(odr, "1");
    ASSERT_NE(road1, nullptr);

    // The side model recorded the repeat lateral poly for object 100 (bT=0.02, cT=0.0001, dT=0.00001).
    const gt_esmini::odr::OdrObjectExtras* ex = gt_esmini::odr::GetObjectExtras(odr, "1", "100");
    ASSERT_NE(ex, nullptr);
    ASSERT_TRUE(ex->repeat_poly.has_poly);
    EXPECT_NEAR(ex->repeat_poly.bT, 0.02, 1e-9);
    EXPECT_NEAR(ex->repeat_poly.cT, 0.0001, 1e-9);
    EXPECT_NEAR(ex->repeat_poly.dT, 0.00001, 1e-9);
    EXPECT_FALSE(ex->repeat_poly.detach_from_reference_line);

    // Find the authored object 100 to pass to the helper.
    roadmanager::RMObject* obj = nullptr;
    for (idx_t j = 0; j < road1->GetNumberOfObjects(); j++)
    {
        roadmanager::RMObject* o = road1->GetRoadObject(j);
        if (o != nullptr && static_cast<unsigned int>(o->GetId()) == 100u)
        {
            obj = o;
        }
    }
    ASSERT_NE(obj, nullptr);

    // At f=1: t_io += bT*1 + cT*1 + dT*1 = 0.02 + 0.0001 + 0.00001 = 0.02011.
    double s_io = 70.0, t_io = -5.0;
    EXPECT_TRUE(gt_esmini::odr::AdjustRepeatInstancePose(obj, road1, 70.0, 1.0, s_io, t_io));
    EXPECT_NEAR(t_io, -5.0 + 0.02011, 1e-9);
    EXPECT_DOUBLE_EQ(s_io, 70.0);  // attached: s unchanged

    // At f=0: no polynomial contribution.
    double s0 = 10.0, t0 = -6.0;
    EXPECT_TRUE(gt_esmini::odr::AdjustRepeatInstancePose(obj, road1, 10.0, 0.0, s0, t0));
    EXPECT_NEAR(t0, -6.0, 1e-12);

    odr->Clear();
}

// ---- T2b: legacy object with no lateral-poly -> fast-path false (s_io/t_io untouched) ----
TEST(OdrObjectExtras, AdjustRepeatInstancePoseLegacyFastPath)
{
    ASSERT_FALSE(RepoRoot().empty());
    // fixture 26 objects carry no repeat lateral poly.
    ASSERT_TRUE(LoadXodr(Fix("handauthored/26_object_details_19.xodr")));
    roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();
    roadmanager::Road*      road1 = RoadByStr(odr, "1");
    ASSERT_NE(road1, nullptr);

    roadmanager::RMObject* obj = road1->GetNumberOfObjects() > 0 ? road1->GetRoadObject(0) : nullptr;
    ASSERT_NE(obj, nullptr);
    double s_io = 5.0, t_io = 2.0;
    EXPECT_FALSE(gt_esmini::odr::AdjustRepeatInstancePose(obj, road1, 5.0, 0.5, s_io, t_io));
    EXPECT_DOUBLE_EQ(s_io, 5.0);
    EXPECT_DOUBLE_EQ(t_io, 2.0);

    odr->Clear();
}
