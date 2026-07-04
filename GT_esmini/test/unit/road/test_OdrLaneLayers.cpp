// test_OdrLaneLayers.cpp -- P8 1.9 lane-layer selection + s-range merge (plan P8).
//
//   PURE-LOGIC (BuildMergedLanes on in-memory pugixml docs):
//     (i)   boundary-aligned merge (roadworks miniature: perm s=0/20/50, temp s=20..50)
//     (ii)  re-open path (t1 mid-permanent-section) -> s=t1 section inserted + width Taylor-shift
//     (iii) laneOffset re-anchor at t1
//     (iv)  permanent mode -> the ORIGINAL node is returned (pointer identity)
//     (v)   single-<lanes> file -> the ORIGINAL node is returned (pointer identity)
//     (vi)  temporary-only (no permanent) -> WARN + the temporary node returned as-is
//
//   INTEGRATION (real parser, env/override-driven):
//     - g2 loaded in temporary mode -> road 1 right side has ONE lane at s=250; permanent mode -> 3.
//     - fixture 06 flags: signal id=1 temporary=true/invalidated=false, id=2 invalidated=true;
//       object id=1 temporary=true.
//
// Mode is flipped via SetLaneLayerModeForTest (test-only override of the env latch, plan D1). Each
// test reverts to SetLaneLayerModeUseEnv() and Clears the OpenDrive singleton on the way out.
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "RoadManager.hpp"
#include "gt_esmini/road/OdrSideModel.hpp"
#include "pugixml.hpp"

namespace fs = std::filesystem;

// BuildMergedLanes has external linkage in gt_esmini::odr::detail; forward-declare it (the internal
// header is private to the odr_side TUs).
namespace gt_esmini
{
namespace odr
{
namespace detail
{
void BuildMergedLanes(const pugi::xml_node& permanent,
                      const pugi::xml_node& temporary,
                      double                road_length,
                      pugi::xml_node        out_lanes);
}
}  // namespace odr
}  // namespace gt_esmini

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

fs::path ScratchDir()
{
    const std::string root = RepoRoot();
    std::error_code   ec;
    if (!root.empty())
    {
        fs::path cand = fs::path(root) / "build" / "odr_lane_layers_tests";
        fs::create_directories(cand, ec);
        if (!ec && fs::is_directory(cand))
        {
            return cand;
        }
    }
    fs::path tmp = fs::temp_directory_path(ec) / "odr_lane_layers_tests";
    fs::create_directories(tmp, ec);
    return tmp;
}

std::string WriteTemp(const std::string& name, const std::string& content)
{
    const fs::path p = ScratchDir() / name;
    std::ofstream  out(p, std::ios::binary);
    out << content;
    out.close();
    return p.string();
}

bool LoadXodr(const std::string& abs_path)
{
    return roadmanager::Position::GetOpenDrive()->LoadOpenDriveFile(abs_path.c_str(), true);
}

// Parse an XML string into `doc`; return the road node (first <road>) for the pure-logic helpers.
pugi::xml_node ParseRoad(pugi::xml_document& doc, const std::string& xml)
{
    doc.load_string(xml.c_str());
    return doc.child("OpenDRIVE").child("road");
}

// Collect the child <laneSection> @s values of a <lanes> node in DOM order.
std::vector<double> SectionSValues(const pugi::xml_node& lanes)
{
    std::vector<double> out;
    for (pugi::xml_node s = lanes.child("laneSection"); s; s = s.next_sibling("laneSection"))
    {
        out.push_back(s.attribute("s").as_double());
    }
    return out;
}

// The width poly (a/b/c/d @ sOffset) of the given lane id in a section (first <width>).
struct WidthPoly
{
    double s_offset, a, b, c, d;
};
WidthPoly LaneFirstWidth(const pugi::xml_node& section, const char* side, int lane_id)
{
    for (pugi::xml_node lane = section.child(side).child("lane"); lane; lane = lane.next_sibling("lane"))
    {
        if (lane.attribute("id").as_int() == lane_id)
        {
            pugi::xml_node w = lane.child("width");
            return WidthPoly{w.attribute("sOffset").as_double(),
                             w.attribute("a").as_double(),
                             w.attribute("b").as_double(),
                             w.attribute("c").as_double(),
                             w.attribute("d").as_double()};
        }
    }
    return WidthPoly{-1, -1, -1, -1, -1};
}

// Count right-side (id<0) lanes of the section active at s on a road.
int RightLaneCountAtS(roadmanager::Road* road, double s)
{
    roadmanager::LaneSection* ls = road->GetLaneSectionByS(s);
    if (ls == nullptr)
    {
        return -1;
    }
    int n = 0;
    for (unsigned int i = 0; i < ls->GetNumberOfLanes(); ++i)
    {
        roadmanager::Lane* lane = ls->GetLaneByIdx(i);
        if (lane != nullptr && lane->GetId() < 0)
        {
            ++n;
        }
    }
    return n;
}

// A roadworks-miniature two-layer road. permanent laneSections at s=0/20/50 (one right driving lane
// with a constant width, so the s-values are the observable). temporary laneSection s=20 length=30
// (covers [20,50] -- boundary-aligned with the permanent s=50 section start).
std::string TwoLayerAligned()
{
    return std::string("<?xml version=\"1.0\"?>\n") +
           "<OpenDRIVE>\n"
           "  <road name=\"rw\" length=\"80.0\" id=\"1\" junction=\"-1\">\n"
           "    <lanes layer=\"permanent\">\n"
           "      <laneSection s=\"0.0\"><center><lane id=\"0\" type=\"none\"/></center>"
           "<right><lane id=\"-1\" type=\"driving\"><width sOffset=\"0.0\" a=\"3.5\" b=\"0\" c=\"0\" d=\"0\"/></lane></right></laneSection>\n"
           "      <laneSection s=\"20.0\"><center><lane id=\"0\" type=\"none\"/></center>"
           "<right><lane id=\"-1\" type=\"driving\"><width sOffset=\"0.0\" a=\"3.5\" b=\"0\" c=\"0\" d=\"0\"/></lane></right></laneSection>\n"
           "      <laneSection s=\"50.0\"><center><lane id=\"0\" type=\"none\"/></center>"
           "<right><lane id=\"-1\" type=\"driving\"><width sOffset=\"0.0\" a=\"3.5\" b=\"0\" c=\"0\" d=\"0\"/></lane></right></laneSection>\n"
           "    </lanes>\n"
           "    <lanes layer=\"temporary\">\n"
           "      <laneSection s=\"20.0\" length=\"30.0\"><center><lane id=\"0\" type=\"none\"/></center>"
           "<right><lane id=\"-1\" type=\"driving\"><width sOffset=\"0.0\" a=\"2.5\" b=\"0\" c=\"0\" d=\"0\"/></lane></right></laneSection>\n"
           "    </lanes>\n"
           "  </road>\n"
           "</OpenDRIVE>\n";
}

}  // namespace

// (i) Boundary-aligned merge: perm s=0/20/50, temp s=20..50 -> section列 s = [0, 20(temp), 50].
TEST(OdrLaneLayers, MergeBoundaryAligned)
{
    pugi::xml_document doc;
    pugi::xml_node     road = ParseRoad(doc, TwoLayerAligned());
    pugi::xml_node     permanent, temporary;
    for (pugi::xml_node l = road.child("lanes"); l; l = l.next_sibling("lanes"))
    {
        if (std::string(l.attribute("layer").value()) == "temporary")
            temporary = l;
        else
            permanent = l;
    }

    pugi::xml_document out_doc;
    pugi::xml_node     out = out_doc.append_child("lanes");
    gt_esmini::odr::detail::BuildMergedLanes(permanent, temporary, 80.0, out);

    // Sections: permanent s=0 (before t0=20), temporary s=20, permanent s=50 (>= t1=50). No re-open
    // (t1 aligns with a permanent section start).
    const std::vector<double> s = SectionSValues(out);
    ASSERT_EQ(s.size(), 3u);
    EXPECT_DOUBLE_EQ(s[0], 0.0);
    EXPECT_DOUBLE_EQ(s[1], 20.0);
    EXPECT_DOUBLE_EQ(s[2], 50.0);

    // The middle section is the temporary one (width a=2.5, not 3.5).
    pugi::xml_node mid;
    for (pugi::xml_node sec = out.child("laneSection"); sec; sec = sec.next_sibling("laneSection"))
    {
        if (sec.attribute("s").as_double() == 20.0)
            mid = sec;
    }
    ASSERT_TRUE(mid);
    EXPECT_DOUBLE_EQ(LaneFirstWidth(mid, "right", -1).a, 2.5);
    // The s=50 section is the permanent one restored (width a=3.5).
    pugi::xml_node last;
    for (pugi::xml_node sec = out.child("laneSection"); sec; sec = sec.next_sibling("laneSection"))
    {
        if (sec.attribute("s").as_double() == 50.0)
            last = sec;
    }
    ASSERT_TRUE(last);
    EXPECT_DOUBLE_EQ(LaneFirstWidth(last, "right", -1).a, 3.5);
}

// (ii) Re-open path: t1 falls in the MIDDLE of a permanent section -> a section at s=t1 is inserted,
// with the lane width Taylor-shifted so the value at s=t1 is continuous.
TEST(OdrLaneLayers, MergeReopenTaylorShift)
{
    // Permanent: single section s=0 with a NON-constant width w(ds) = 3.0 + 0.02*ds (b=0.02).
    // Temporary: s=10 length=20 -> covers [10,30). No permanent section starts at 30 -> re-open at 30.
    const std::string xml =
        std::string("<?xml version=\"1.0\"?>\n") +
        "<OpenDRIVE>\n"
        "  <road name=\"r\" length=\"100.0\" id=\"1\" junction=\"-1\">\n"
        "    <lanes layer=\"permanent\">\n"
        "      <laneSection s=\"0.0\"><center><lane id=\"0\" type=\"none\"/></center>"
        "<right><lane id=\"-1\" type=\"driving\"><width sOffset=\"0.0\" a=\"3.0\" b=\"0.02\" c=\"0\" d=\"0\"/></lane></right></laneSection>\n"
        "    </lanes>\n"
        "    <lanes layer=\"temporary\">\n"
        "      <laneSection s=\"10.0\" length=\"20.0\"><center><lane id=\"0\" type=\"none\"/></center>"
        "<right><lane id=\"-1\" type=\"driving\"><width sOffset=\"0.0\" a=\"2.0\" b=\"0\" c=\"0\" d=\"0\"/></lane></right></laneSection>\n"
        "    </lanes>\n"
        "  </road>\n"
        "</OpenDRIVE>\n";
    pugi::xml_document doc;
    pugi::xml_node     road = ParseRoad(doc, xml);
    pugi::xml_node     permanent, temporary;
    for (pugi::xml_node l = road.child("lanes"); l; l = l.next_sibling("lanes"))
    {
        if (std::string(l.attribute("layer").value()) == "temporary")
            temporary = l;
        else
            permanent = l;
    }

    pugi::xml_document out_doc;
    pugi::xml_node     out = out_doc.append_child("lanes");
    gt_esmini::odr::detail::BuildMergedLanes(permanent, temporary, 100.0, out);

    // Sections: permanent s=0 (covers [0,t0=10)), temporary s=10, re-opened permanent s=30.
    const std::vector<double> s = SectionSValues(out);
    ASSERT_EQ(s.size(), 3u);
    EXPECT_DOUBLE_EQ(s[0], 0.0);
    EXPECT_DOUBLE_EQ(s[1], 10.0);
    EXPECT_DOUBLE_EQ(s[2], 30.0);

    // Re-opened section width @sOffset=0 must equal the permanent width at ds=30: a' = 3.0+0.02*30 = 3.6,
    // b' = 0.02 (Taylor-shift of a linear poly).
    pugi::xml_node reopened;
    for (pugi::xml_node sec = out.child("laneSection"); sec; sec = sec.next_sibling("laneSection"))
    {
        if (sec.attribute("s").as_double() == 30.0)
            reopened = sec;
    }
    ASSERT_TRUE(reopened);
    const WidthPoly wp = LaneFirstWidth(reopened, "right", -1);
    EXPECT_DOUBLE_EQ(wp.s_offset, 0.0);
    EXPECT_NEAR(wp.a, 3.6, 1e-6);
    EXPECT_NEAR(wp.b, 0.02, 1e-6);
}

// (iii) laneOffset re-anchor: a permanent laneOffset active at t1 (but not starting there) is
// Taylor-shifted to s=t1.
TEST(OdrLaneLayers, MergeLaneOffsetReanchor)
{
    // Permanent laneOffset s=0 with a=1.0 b=0.1 (linear). Temporary covers [10,30), no permanent
    // laneOffset starts at 30 -> re-anchored copy at s=30: a' = 1.0 + 0.1*30 = 4.0, b'=0.1.
    const std::string xml =
        std::string("<?xml version=\"1.0\"?>\n") +
        "<OpenDRIVE>\n"
        "  <road name=\"r\" length=\"100.0\" id=\"1\" junction=\"-1\">\n"
        "    <lanes layer=\"permanent\">\n"
        "      <laneOffset s=\"0.0\" a=\"1.0\" b=\"0.1\" c=\"0.0\" d=\"0.0\"/>\n"
        "      <laneSection s=\"0.0\"><center><lane id=\"0\" type=\"none\"/></center>"
        "<right><lane id=\"-1\" type=\"driving\"><width sOffset=\"0.0\" a=\"3.0\" b=\"0\" c=\"0\" d=\"0\"/></lane></right></laneSection>\n"
        "    </lanes>\n"
        "    <lanes layer=\"temporary\">\n"
        "      <laneSection s=\"10.0\" length=\"20.0\"><center><lane id=\"0\" type=\"none\"/></center>"
        "<right><lane id=\"-1\" type=\"driving\"><width sOffset=\"0.0\" a=\"2.0\" b=\"0\" c=\"0\" d=\"0\"/></lane></right></laneSection>\n"
        "    </lanes>\n"
        "  </road>\n"
        "</OpenDRIVE>\n";
    pugi::xml_document doc;
    pugi::xml_node     road = ParseRoad(doc, xml);
    pugi::xml_node     permanent, temporary;
    for (pugi::xml_node l = road.child("lanes"); l; l = l.next_sibling("lanes"))
    {
        if (std::string(l.attribute("layer").value()) == "temporary")
            temporary = l;
        else
            permanent = l;
    }

    pugi::xml_document out_doc;
    pugi::xml_node     out = out_doc.append_child("lanes");
    gt_esmini::odr::detail::BuildMergedLanes(permanent, temporary, 100.0, out);

    // Find the re-anchored laneOffset at s=30.
    pugi::xml_node lo30;
    for (pugi::xml_node lo = out.child("laneOffset"); lo; lo = lo.next_sibling("laneOffset"))
    {
        if (lo.attribute("s").as_double() == 30.0)
            lo30 = lo;
    }
    ASSERT_TRUE(lo30) << "re-anchored laneOffset at s=t1=30 must exist";
    EXPECT_NEAR(lo30.attribute("a").as_double(), 4.0, 1e-6);
    EXPECT_NEAR(lo30.attribute("b").as_double(), 0.1, 1e-6);
}

// (iv) Permanent mode: SelectLanesLayer returns the ORIGINAL permanent node (no copy) -> pointer id.
TEST(OdrLaneLayers, PermanentModeReturnsOriginalNode)
{
    gt_esmini::odr::SetLaneLayerModeForTest(false);  // permanent

    pugi::xml_document doc;
    pugi::xml_node     road = ParseRoad(doc, TwoLayerAligned());
    pugi::xml_node     permanent = road.child("lanes");  // first <lanes> == permanent

    const void*    key      = static_cast<const void*>(&doc);
    pugi::xml_node selected = gt_esmini::odr::SelectLanesLayer(road, key);
    EXPECT_EQ(selected.internal_object(), permanent.internal_object()) << "permanent mode must return the original node";

    gt_esmini::odr::SetLaneLayerModeUseEnv();
    gt_esmini::odr::ClearSideModel(key);
}

// (v) Single-<lanes> file: SelectLanesLayer returns the original node in either mode.
TEST(OdrLaneLayers, SingleLanesReturnsOriginalNode)
{
    const std::string xml =
        std::string("<?xml version=\"1.0\"?>\n") +
        "<OpenDRIVE>\n"
        "  <road name=\"r\" length=\"100.0\" id=\"1\" junction=\"-1\">\n"
        "    <lanes>\n"
        "      <laneSection s=\"0.0\"><center><lane id=\"0\" type=\"none\"/></center>"
        "<right><lane id=\"-1\" type=\"driving\"><width sOffset=\"0.0\" a=\"3.5\" b=\"0\" c=\"0\" d=\"0\"/></lane></right></laneSection>\n"
        "    </lanes>\n"
        "  </road>\n"
        "</OpenDRIVE>\n";
    gt_esmini::odr::SetLaneLayerModeForTest(true);  // temporary mode, but there is no temporary layer

    pugi::xml_document doc;
    pugi::xml_node     road      = ParseRoad(doc, xml);
    pugi::xml_node     the_lanes = road.child("lanes");

    const void*    key      = static_cast<const void*>(&doc);
    pugi::xml_node selected = gt_esmini::odr::SelectLanesLayer(road, key);
    EXPECT_EQ(selected.internal_object(), the_lanes.internal_object()) << "single-<lanes> road must return the original node";

    gt_esmini::odr::SetLaneLayerModeUseEnv();
    gt_esmini::odr::ClearSideModel(key);
}

// (vi) Temporary-only (no permanent): WARN + the temporary node returned as-is (both modes).
TEST(OdrLaneLayers, TemporaryOnlyReturnedAsIs)
{
    const std::string xml =
        std::string("<?xml version=\"1.0\"?>\n") +
        "<OpenDRIVE>\n"
        "  <road name=\"r\" length=\"100.0\" id=\"1\" junction=\"-1\">\n"
        "    <lanes layer=\"temporary\">\n"
        "      <laneSection s=\"0.0\" length=\"100.0\"><center><lane id=\"0\" type=\"none\"/></center>"
        "<right><lane id=\"-1\" type=\"driving\"><width sOffset=\"0.0\" a=\"2.5\" b=\"0\" c=\"0\" d=\"0\"/></lane></right></laneSection>\n"
        "    </lanes>\n"
        "  </road>\n"
        "</OpenDRIVE>\n";
    gt_esmini::odr::SetLaneLayerModeForTest(true);  // temporary

    pugi::xml_document doc;
    pugi::xml_node     road    = ParseRoad(doc, xml);
    pugi::xml_node     the_tmp = road.child("lanes");

    const void*    key      = static_cast<const void*>(&doc);
    pugi::xml_node selected = gt_esmini::odr::SelectLanesLayer(road, key);
    EXPECT_EQ(selected.internal_object(), the_tmp.internal_object()) << "temporary-only road must return the temporary node as-is";

    gt_esmini::odr::SetLaneLayerModeUseEnv();
    gt_esmini::odr::ClearSideModel(key);
}

// Integration: g2 loaded in temporary vs permanent mode -> different right-lane count at s=250.
TEST(OdrLaneLayers, G2TemporaryVsPermanentIntegration)
{
    const std::string g2 = RepoRoot() + "/GT_esmini/test/odr_fixtures/generated/g2_lanes_layer_19.xodr";

    // Permanent (default) mode: the permanent layer has 3 right lanes (-1 driving, -2 shoulder, -3 border).
    gt_esmini::odr::SetLaneLayerModeForTest(false);
    ASSERT_TRUE(LoadXodr(g2)) << "load failed (permanent): " << g2;
    roadmanager::Road* road_perm = roadmanager::Position::GetOpenDrive()->GetRoadById(1);
    ASSERT_NE(road_perm, nullptr);
    EXPECT_EQ(RightLaneCountAtS(road_perm, 250.0), 3) << "permanent layer has 3 right lanes";
    roadmanager::Position::GetOpenDrive()->Clear();

    // Temporary mode: the temporary layer replaces the whole road [0,500] with a single right driving lane.
    gt_esmini::odr::SetLaneLayerModeForTest(true);
    ASSERT_TRUE(LoadXodr(g2)) << "load failed (temporary): " << g2;
    roadmanager::Road* road_temp = roadmanager::Position::GetOpenDrive()->GetRoadById(1);
    ASSERT_NE(road_temp, nullptr);
    EXPECT_EQ(RightLaneCountAtS(road_temp, 250.0), 1) << "temporary layer has 1 right lane";
    roadmanager::Position::GetOpenDrive()->Clear();

    gt_esmini::odr::SetLaneLayerModeUseEnv();
}

// Integration: fixture 06 temporary/invalidated flags land in the side model.
TEST(OdrLaneLayers, Fixture06Flags)
{
    const std::string f06 = RepoRoot() + "/GT_esmini/test/odr_fixtures/handauthored/06_temporary_invalidated_19.xodr";
    ASSERT_TRUE(LoadXodr(f06)) << "load failed: " << f06;

    roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();

    // signal id=1: temporary=true, invalidated=false.
    const gt_esmini::odr::OdrSignalExtras* s1 = gt_esmini::odr::GetSignalExtras(odr, std::string("1"), std::string("1"));
    ASSERT_NE(s1, nullptr) << "signal 1 extras missing (temporary flag should have created an entry)";
    EXPECT_TRUE(s1->temporary_present);
    EXPECT_TRUE(s1->temporary);
    EXPECT_TRUE(s1->invalidated_present);
    EXPECT_FALSE(s1->invalidated);

    // signal id=2: invalidated=true.
    const gt_esmini::odr::OdrSignalExtras* s2 = gt_esmini::odr::GetSignalExtras(odr, std::string("1"), std::string("2"));
    ASSERT_NE(s2, nullptr) << "signal 2 extras missing";
    EXPECT_TRUE(s2->invalidated_present);
    EXPECT_TRUE(s2->invalidated);
    EXPECT_FALSE(s2->temporary);

    // object id=1: temporary=true.
    const gt_esmini::odr::OdrObjectExtras* o1 = gt_esmini::odr::GetObjectExtras(odr, std::string("1"), std::string("1"));
    ASSERT_NE(o1, nullptr) << "object 1 extras missing (temporary flag should have created an entry)";
    EXPECT_TRUE(o1->temporary_present);
    EXPECT_TRUE(o1->temporary);
    EXPECT_TRUE(o1->invalidated_present);
    EXPECT_FALSE(o1->invalidated);

    roadmanager::Position::GetOpenDrive()->Clear();
}

// P8 stage 2 (D4): the invalidated flag must be queryable through the EXACT accessor the OSI reporter
// (UpdateStaticTrafficSignals) uses -- GetSignalExtras(od, Signal*) resolving from a live Signal
// pointer, not the (road_id, signal_id) string key. This guards the reporter's filter code path.
TEST(OdrLaneLayers, Fixture06SignalPointerPath)
{
    const std::string f06 = RepoRoot() + "/GT_esmini/test/odr_fixtures/handauthored/06_temporary_invalidated_19.xodr";
    ASSERT_TRUE(LoadXodr(f06)) << "load failed: " << f06;

    roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();
    roadmanager::Road*      road = odr->GetRoadById(1);
    ASSERT_NE(road, nullptr);

    // Resolve each Signal* exactly as the OSI reporter loop does (road->GetSignal(j)), then query via
    // the pointer overload -- the same call UpdateStaticTrafficSignals makes before its branch dispatch.
    int checked = 0;
    for (unsigned int j = 0; j < road->GetNumberOfSignals(); ++j)
    {
        roadmanager::Signal* sig = road->GetSignal(j);
        ASSERT_NE(sig, nullptr);
        const gt_esmini::odr::OdrSignalExtras* sx = gt_esmini::odr::GetSignalExtras(odr, sig);
        ASSERT_NE(sx, nullptr) << "signal " << sig->GetId() << " extras missing via Signal* path";
        if (sig->GetId() == 1)
        {
            EXPECT_FALSE(sx->invalidated) << "signal 1 is valid -> reporter must NOT skip it";
            ++checked;
        }
        else if (sig->GetId() == 2)
        {
            EXPECT_TRUE(sx->invalidated) << "signal 2 is invalidated -> reporter must skip it";
            ++checked;
        }
    }
    EXPECT_EQ(checked, 2) << "both fixture signals should have been resolved via the Signal* path";

    roadmanager::Position::GetOpenDrive()->Clear();
}

// ---------------------------------------------------------------------------
// GlobalIdStability (P8 stage 3, WP2): lane global-ID stability golden.
//
//   Loads a committed control set in DEFAULT (permanent) mode and dumps, for every lane,
//   {road_id, section_index, lane_id} -> GetGlobalId(), then compares against the committed
//   golden GT_esmini/test/odr_fixtures/golden/lane_global_ids.json.
//
//   The acceptance guard this pins: permanent mode returns the ORIGINAL first-lanes node with NO
//   copy (plan D2), so lane DOM-iteration order -- and therefore the monotonic global-id assignment
//   (GT_RoadManager.cpp LaneSection::AddLane -> Lane::SetGlobalId -> GetNewGlobalId) -- is UNCHANGED
//   by the P8 fork hook on control assets. This test machine-verifies that invariant.
//
//   NORMALIZATION (why the dump is offset-normalized, not raw g_id):
//   GetNewGlobalId() is a FILE-STATIC monotonic counter in CommonMini.cpp (`global_id`). Although
//   LoadOpenDriveFile(replace=true) -> Reset() -> Clear() -> ResetGlobalIdCounter() resets it to 0 at
//   the START of every load (so a single file's ids are deterministic), the RAW ids of a road would
//   still depend on how many lanes were assigned BEFORE that road within the same load. To make the
//   golden robust against ANY such drift (multi-road files, junction lanes, future counter changes,
//   or other tests sharing the gtest process), each lane's value is recorded RELATIVE to the FIRST
//   lane of ITS OWN road: value = GetGlobalId() - g_id_of_first_lane_of_that_road. This offset is
//   invariant under a uniform shift of the whole counter and pins exactly what P8 must not disturb:
//   the intra-road lane ORDERING/spacing that flows from DOM-iteration order.
//   Update mode mirrors test_OdrAssetProbe: env GT_ODR_PROBE_UPDATE=1 writes the golden and PASSes.

namespace
{

// Control set (all committed / always present): two multi-section repo roads + the two P8 lane
// fixtures. Relative to the repo root; forward slashes for stable golden keys.
const char* kGlobalIdControlSet[] = {
    "resources/xodr/fabriksgatan.xodr",
    "resources/xodr/multi_intersections.xodr",
    "GT_esmini/test/odr_fixtures/generated/g2_lanes_layer_19.xodr",
    "GT_esmini/test/odr_fixtures/handauthored/06_temporary_invalidated_19.xodr",
};

// Dump one loaded OpenDrive into ordered {"road:<id>|sec:<idx>|lane:<id>" -> normalized offset}.
std::map<std::string, long long> DumpNormalizedGlobalIds(roadmanager::OpenDrive* odr)
{
    std::map<std::string, long long> out;
    if (odr == nullptr)
    {
        return out;
    }
    for (unsigned int ri = 0; ri < odr->GetNumOfRoads(); ++ri)
    {
        roadmanager::Road* road = odr->GetRoadByIdx(ri);
        if (road == nullptr)
        {
            continue;
        }
        // First lane of THIS road (section 0, lane 0) anchors the offset normalization.
        long long anchor       = 0;
        bool      anchor_found = false;
        for (unsigned int si = 0; si < road->GetNumberOfLaneSections() && !anchor_found; ++si)
        {
            roadmanager::LaneSection* ls = road->GetLaneSectionByIdx(si);
            if (ls != nullptr && ls->GetNumberOfLanes() > 0)
            {
                roadmanager::Lane* l0 = ls->GetLaneByIdx(0);
                if (l0 != nullptr)
                {
                    anchor       = static_cast<long long>(l0->GetGlobalId());
                    anchor_found = true;
                }
            }
        }
        for (unsigned int si = 0; si < road->GetNumberOfLaneSections(); ++si)
        {
            roadmanager::LaneSection* ls = road->GetLaneSectionByIdx(si);
            if (ls == nullptr)
            {
                continue;
            }
            for (unsigned int li = 0; li < ls->GetNumberOfLanes(); ++li)
            {
                roadmanager::Lane* lane = ls->GetLaneByIdx(li);
                if (lane == nullptr)
                {
                    continue;
                }
                std::string key = "road:" + road->GetIdStr() + "|sec:" + std::to_string(si) + "|lane:" + std::to_string(lane->GetId());
                out[key]        = static_cast<long long>(lane->GetGlobalId()) - anchor;
            }
        }
    }
    return out;
}

// Deterministic JSON: top-level keyed by relpath, each value an ordered map key->int (sorted keys).
std::string SerializeGlobalIds(const std::map<std::string, std::map<std::string, long long>>& all)
{
    std::ostringstream os;
    os << "{\n";
    bool first_file = true;
    for (const auto& kv : all)
    {
        if (!first_file)
        {
            os << ",\n";
        }
        first_file = false;
        os << "  \"" << kv.first << "\": {";
        bool first = true;
        for (const auto& e : kv.second)
        {
            if (!first)
            {
                os << ",";
            }
            first = false;
            os << "\n    \"" << e.first << "\": " << e.second;
        }
        if (!kv.second.empty())
        {
            os << "\n  ";
        }
        os << "}";
    }
    os << "\n}\n";
    return os.str();
}

std::string GlobalIdGoldenPath()
{
    return RepoRoot() + "/GT_esmini/test/odr_fixtures/golden/lane_global_ids.json";
}

}  // namespace

TEST(OdrLaneLayers, GlobalIdStability)
{
    const std::string root = RepoRoot();
    ASSERT_FALSE(root.empty()) << "GT_ODR_REPO_ROOT not defined";

    // Pin permanent mode regardless of ambient GT_ODR_LANE_LAYERS so the control assets always
    // produce the ORIGINAL (un-merged) lane structure -- that is precisely what the golden freezes.
    gt_esmini::odr::SetLaneLayerModeForTest(false);

    std::map<std::string, std::map<std::string, long long>> all;
    for (const char* rel : kGlobalIdControlSet)
    {
        const std::string abs_path = root + "/" + rel;
        ASSERT_TRUE(LoadXodr(abs_path)) << "control-set load failed: " << abs_path;
        all[rel] = DumpNormalizedGlobalIds(roadmanager::Position::GetOpenDrive());
        roadmanager::Position::GetOpenDrive()->Clear();
    }

    gt_esmini::odr::SetLaneLayerModeUseEnv();

    const std::string serialized = SerializeGlobalIds(all);
    const std::string golden_path = GlobalIdGoldenPath();

    const char* update    = std::getenv("GT_ODR_PROBE_UPDATE");
    const bool  do_update = update != nullptr && std::string(update) == "1";

    if (do_update)
    {
        std::error_code ec;
        fs::create_directories(fs::path(golden_path).parent_path(), ec);
        std::ofstream out(golden_path, std::ios::binary);
        ASSERT_TRUE(out.good()) << "cannot write golden " << golden_path;
        out << serialized;
        out.close();
        std::cout << "[GT_ODR lane-global-id] UPDATE wrote golden: " << all.size() << " files\n";
        SUCCEED();
        return;
    }

    // Compare mode (EOL-insensitive: committed golden is LF, autocrlf may materialize CRLF).
    std::ifstream in(golden_path, std::ios::binary);
    ASSERT_TRUE(in.good()) << "golden missing: " << golden_path
                           << "\n  Run once with env GT_ODR_PROBE_UPDATE=1 to capture it.";
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string golden = ss.str();
    golden.erase(std::remove(golden.begin(), golden.end(), '\r'), golden.end());

    EXPECT_EQ(serialized, golden) << "lane global-id layout drifted from the golden (permanent-mode "
                                     "control assets must keep the ORIGINAL first-lanes ordering).";
}
