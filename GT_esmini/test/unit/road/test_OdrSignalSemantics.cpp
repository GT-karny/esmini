// test_OdrSignalSemantics.cpp -- unit tests for the P4 L1 signal semantics / VMS boards /
// header license & defaultRegulations storage (plan P4, clusters 10/13/14).
//
// Fast + file-free: tiny xodr documents are built from inline raw strings via pugixml and
// BuildSideModel(doc, void* key) is called directly (no RoadManager fork hook). Extras are read
// back through GetSideModel() / GetSignalExtras(). Mirrors test_OdrSideModel.cpp.
//
// Covered:
//   * all 15 <semantics> subtypes stored;
//   * 1.8/1.9 attribute-form speed AND a child-element-form fallback normalize to the identical
//     OdrSemanticSpeed (EXPECT_EQ between the two parses);
//   * participants (vehicle/person/animal with e_vehicleCategory / e_personCategory);
//   * boards in the (shared 1.8/1.9) placement -- staticBoard + <sign>, vmsBoard + <displayArea>;
//   * document-level <vmsGroup>;
//   * header/license + header/defaultRegulations (road + signal regulations reusing <semantics>);
//   * sparse-storage invariant (a signal with no P3/P4 child produces NO signal_extras entry);
//   * the GetSignalExtras(key, road, signal) accessor incl. duplicate-id (first-match) behavior.
#include <gtest/gtest.h>

#include <string>

#include "gt_esmini/road/OdrSideModel.hpp"
#include "pugixml.hpp"

using namespace gt_esmini::odr;

namespace
{

pugi::xml_document ParseDoc(const std::string& xml)
{
    pugi::xml_document          doc;
    pugi::xml_parse_result res = doc.load_string(xml.c_str());
    EXPECT_TRUE(res) << "XML parse error: " << res.description();
    return doc;
}

// Distinct dummy registry keys per test (stable, distinct addresses).
const void* Key(int tag)
{
    static char slots[128];
    return &slots[tag & 127];
}

// Wrap a <signals> body inside a minimal 1.9 road + header.
std::string RoadWithSignals(const std::string& signals_body, const char* rev = "9")
{
    return std::string(
               "<OpenDRIVE>\n"
               "  <header revMajor=\"1\" revMinor=\"") + rev + "\" name=\"t\"/>\n"
           "  <road name=\"r\" length=\"100.0\" id=\"1\" junction=\"-1\">\n"
           "    <planView><geometry s=\"0.0\" x=\"0.0\" y=\"0.0\" hdg=\"0.0\" length=\"100.0\"><line/></geometry></planView>\n"
           "    <lanes><laneSection s=\"0.0\">\n"
           "      <center><lane id=\"0\" type=\"none\" level=\"false\"/></center>\n"
           "      <right><lane id=\"-1\" type=\"driving\" level=\"false\"><width sOffset=\"0.0\" a=\"3.5\" b=\"0.0\" c=\"0.0\" d=\"0.0\"/></lane></right>\n"
           "    </laneSection></lanes>\n"
           "    <signals>\n" + signals_body +
           "    </signals>\n"
           "  </road>\n"
           "</OpenDRIVE>\n";
}

const char* kSignalHead =
    "<signal s=\"50.0\" t=\"-6.0\" id=\"1\" name=\"s\" dynamic=\"no\" orientation=\"+\" country=\"DE\" type=\"274\" subtype=\"-1\">";

}  // namespace

// ------------------------------------------------------------------------------------------------
// All 15 semantics subtypes are stored.
TEST(OdrSignalSemantics, AllFifteenSubtypesStored)
{
    const std::string body =
        std::string(kSignalHead) +
        "<semantics>"
        "  <speed type=\"maximum\" value=\"60\" unit=\"km/h\"/>"
        "  <lane type=\"noOvertakeTrucks\"/>"
        "  <priority type=\"yield\"/>"
        "  <prohibited><vehicle><type>heavyTruck</type></vehicle></prohibited>"
        "  <warning/>"
        "  <routing/>"
        "  <streetname/>"
        "  <parking/>"
        "  <tourist/>"
        "  <supplementaryTime type=\"day\" value=\"1\"/>"
        "  <supplementaryAllows><person><type>pedestrian</type></person></supplementaryAllows>"
        "  <supplementaryProhibits><animal/></supplementaryProhibits>"
        "  <supplementaryDistance type=\"for\" value=\"1000\" unit=\"m\"/>"
        "  <supplementaryEnvironment type=\"rain\"/>"
        "  <supplementaryExplanatory/>"
        "</semantics></signal>";
    pugi::xml_document doc = ParseDoc(RoadWithSignals(body));
    EXPECT_TRUE(BuildSideModel(doc, Key(1)));

    const OdrSignalExtras* e = GetSignalExtras(Key(1), "1", "1");
    ASSERT_NE(e, nullptr);
    ASSERT_TRUE(e->has_semantics);
    const OdrSemantics& s = e->semantics;
    ASSERT_EQ(s.speeds.size(), 1u);
    EXPECT_EQ(s.speeds[0].type, "maximum");
    EXPECT_DOUBLE_EQ(s.speeds[0].value, 60.0);
    EXPECT_EQ(s.speeds[0].unit, "km/h");
    ASSERT_EQ(s.lane_types.size(), 1u);
    EXPECT_EQ(s.lane_types[0], "noOvertakeTrucks");
    ASSERT_EQ(s.priority_types.size(), 1u);
    EXPECT_EQ(s.priority_types[0], "yield");
    ASSERT_EQ(s.prohibited.size(), 1u);
    EXPECT_EQ(s.prohibited[0].kind, "vehicle");
    EXPECT_EQ(s.prohibited[0].category, "heavyTruck");
    EXPECT_EQ(s.warning_count, 1);
    EXPECT_EQ(s.routing_count, 1);
    EXPECT_EQ(s.streetname_count, 1);
    EXPECT_EQ(s.parking_count, 1);
    EXPECT_EQ(s.tourist_count, 1);
    ASSERT_EQ(s.supplementary_time.size(), 1u);
    EXPECT_EQ(s.supplementary_time[0].type, "day");
    EXPECT_DOUBLE_EQ(s.supplementary_time[0].value, 1.0);
    ASSERT_EQ(s.supplementary_allows.size(), 1u);
    EXPECT_EQ(s.supplementary_allows[0].kind, "person");
    EXPECT_EQ(s.supplementary_allows[0].category, "pedestrian");
    ASSERT_EQ(s.supplementary_prohibits.size(), 1u);
    EXPECT_EQ(s.supplementary_prohibits[0].kind, "animal");
    EXPECT_EQ(s.supplementary_prohibits[0].category, "");  // animal has no <type>
    ASSERT_EQ(s.supplementary_distance.size(), 1u);
    EXPECT_EQ(s.supplementary_distance[0].type, "for");
    EXPECT_DOUBLE_EQ(s.supplementary_distance[0].value, 1000.0);
    EXPECT_EQ(s.supplementary_distance[0].unit, "m");
    ASSERT_EQ(s.supplementary_environment.size(), 1u);
    EXPECT_EQ(s.supplementary_environment[0], "rain");
    EXPECT_EQ(s.supplementary_explanatory_count, 1);
    EXPECT_FALSE(s.IsEmpty());
}

// ------------------------------------------------------------------------------------------------
// The 1.9/1.8 attribute-form speed and a child-element draft form normalize IDENTICALLY.
TEST(OdrSignalSemantics, SpeedFormsNormalizeEqual)
{
    // Attribute form (1.8.1 + 1.9 canonical).
    const std::string attr_body =
        std::string(kSignalHead) +
        "<semantics><speed type=\"maximum\" value=\"80\" unit=\"km/h\"/></semantics></signal>";
    pugi::xml_document adoc = ParseDoc(RoadWithSignals(attr_body));
    EXPECT_TRUE(BuildSideModel(adoc, Key(2)));
    const OdrSignalExtras* ae = GetSignalExtras(Key(2), "1", "1");
    ASSERT_NE(ae, nullptr);
    ASSERT_EQ(ae->semantics.speeds.size(), 1u);
    const OdrSemanticSpeed attr_speed = ae->semantics.speeds[0];

    // Child-element draft form: <speed><maximum value=".." unit=".."/></speed>.
    const std::string child_body =
        std::string(kSignalHead) +
        "<semantics><speed><maximum value=\"80\" unit=\"km/h\"/></speed></semantics></signal>";
    pugi::xml_document cdoc = ParseDoc(RoadWithSignals(child_body));
    EXPECT_TRUE(BuildSideModel(cdoc, Key(3)));
    const OdrSignalExtras* ce = GetSignalExtras(Key(3), "1", "1");
    ASSERT_NE(ce, nullptr);
    ASSERT_EQ(ce->semantics.speeds.size(), 1u);
    const OdrSemanticSpeed child_speed = ce->semantics.speeds[0];

    // Both normalize to the SAME OdrSemanticSpeed.
    EXPECT_EQ(attr_speed.type, child_speed.type);
    EXPECT_EQ(attr_speed.value_str, child_speed.value_str);
    EXPECT_DOUBLE_EQ(attr_speed.value, child_speed.value);
    EXPECT_EQ(attr_speed.unit, child_speed.unit);
    EXPECT_EQ(attr_speed.type, "maximum");
    EXPECT_DOUBLE_EQ(attr_speed.value, 80.0);
    EXPECT_EQ(attr_speed.unit, "km/h");
}

// ------------------------------------------------------------------------------------------------
// Participants: vehicle/person/animal across several e_vehicleCategory values.
TEST(OdrSignalSemantics, Participants)
{
    const std::string body =
        std::string(kSignalHead) +
        "<semantics><prohibited>"
        "  <animal/>"
        "  <person><type>pedestrian</type></person>"
        "  <vehicle><type>heavyTruck</type></vehicle>"
        "  <vehicle><type>bus</type></vehicle>"
        "  <vehicle><type>motorcycle</type></vehicle>"
        "</prohibited></semantics></signal>";
    pugi::xml_document doc = ParseDoc(RoadWithSignals(body));
    EXPECT_TRUE(BuildSideModel(doc, Key(4)));
    const OdrSignalExtras* e = GetSignalExtras(Key(4), "1", "1");
    ASSERT_NE(e, nullptr);
    const auto& p = e->semantics.prohibited;
    ASSERT_EQ(p.size(), 5u);
    EXPECT_EQ(p[0].kind, "animal");
    EXPECT_EQ(p[0].category, "");
    EXPECT_EQ(p[1].kind, "person");
    EXPECT_EQ(p[1].category, "pedestrian");
    EXPECT_EQ(p[2].kind, "vehicle");
    EXPECT_EQ(p[2].category, "heavyTruck");
    EXPECT_EQ(p[3].category, "bus");
    EXPECT_EQ(p[4].category, "motorcycle");
}

// ------------------------------------------------------------------------------------------------
// Boards: staticBoard (+sign) and vmsBoard (+displayArea).
TEST(OdrSignalSemantics, Boards)
{
    const std::string body =
        std::string(kSignalHead) +
        "<staticBoard><sign v=\"0.0\" z=\"6.0\"/><sign v=\"1.0\" z=\"6.0\"/></staticBoard>"
        "<vmsBoard displayType=\"LED\" displayWidth=\"4.0\" displayHeight=\"2.0\" v=\"2.0\" z=\"6.0\">"
        "  <displayArea index=\"0\" v=\"2.0\" z=\"6.0\" width=\"4.0\" height=\"2.0\"/>"
        "</vmsBoard></signal>";
    pugi::xml_document doc = ParseDoc(RoadWithSignals(body));
    EXPECT_TRUE(BuildSideModel(doc, Key(5)));
    const OdrSignalExtras* e = GetSignalExtras(Key(5), "1", "1");
    ASSERT_NE(e, nullptr);
    ASSERT_EQ(e->static_boards.size(), 1u);
    ASSERT_EQ(e->static_boards[0].signs.size(), 2u);
    EXPECT_EQ(e->static_boards[0].signs[0].v, "0.0");
    EXPECT_EQ(e->static_boards[0].signs[1].v, "1.0");
    ASSERT_EQ(e->vms_boards.size(), 1u);
    EXPECT_EQ(e->vms_boards[0].display_type, "LED");
    EXPECT_EQ(e->vms_boards[0].display_width, "4.0");
    ASSERT_EQ(e->vms_boards[0].display_areas.size(), 1u);
    EXPECT_EQ(e->vms_boards[0].display_areas[0].index, "0");
    EXPECT_EQ(e->vms_boards[0].display_areas[0].width, "4.0");
}

// ------------------------------------------------------------------------------------------------
// Document-level <vmsGroup>.
TEST(OdrSignalSemantics, VmsGroup)
{
    std::string xml =
        "<OpenDRIVE>\n"
        "  <header revMajor=\"1\" revMinor=\"9\" name=\"t\"/>\n"
        "  <road name=\"r\" length=\"100.0\" id=\"1\" junction=\"-1\">\n"
        "    <planView><geometry s=\"0.0\" x=\"0.0\" y=\"0.0\" hdg=\"0.0\" length=\"100.0\"><line/></geometry></planView>\n"
        "    <lanes><laneSection s=\"0.0\"><center><lane id=\"0\" type=\"none\"/></center></laneSection></lanes>\n"
        "  </road>\n"
        "  <vmsGroup id=\"g1\">\n"
        "    <vmsBoardReference signalId=\"1\" vmsIndex=\"0\" groupIndex=\"0\"/>\n"
        "    <vmsBoardReference signalId=\"2\" vmsIndex=\"0\" groupIndex=\"1\"/>\n"
        "  </vmsGroup>\n"
        "</OpenDRIVE>\n";
    pugi::xml_document doc = ParseDoc(xml);
    EXPECT_TRUE(BuildSideModel(doc, Key(6)));
    const OdrSideModel* m = GetSideModel(Key(6));
    ASSERT_NE(m, nullptr);
    ASSERT_EQ(m->vms_groups.size(), 1u);
    EXPECT_EQ(m->vms_groups[0].id, "g1");
    ASSERT_EQ(m->vms_groups[0].board_references.size(), 2u);
    EXPECT_EQ(m->vms_groups[0].board_references[0].signal_id, "1");
    EXPECT_EQ(m->vms_groups[0].board_references[1].group_index, "1");
}

// ------------------------------------------------------------------------------------------------
// header/license + header/defaultRegulations (road + signal regs reuse <semantics>).
TEST(OdrSignalSemantics, LicenseAndDefaultRegulations)
{
    std::string xml =
        "<OpenDRIVE>\n"
        "  <header revMajor=\"1\" revMinor=\"9\" name=\"t\">\n"
        "    <license name=\"CC-BY-4.0\" spdxid=\"CC-BY-4.0\" resource=\"http://x\"/>\n"
        "    <defaultRegulations>\n"
        "      <roadRegulations type=\"motorway\">\n"
        "        <semantics><priority type=\"priorityRoad\"/></semantics>\n"
        "      </roadRegulations>\n"
        "      <signalRegulations type=\"1000001\" subtype=\"-1\">\n"
        "        <semantics><speed type=\"maximum\" value=\"120\" unit=\"km/h\"/></semantics>\n"
        "      </signalRegulations>\n"
        "    </defaultRegulations>\n"
        "  </header>\n"
        "  <road name=\"r\" length=\"100.0\" id=\"1\" junction=\"-1\">\n"
        "    <planView><geometry s=\"0.0\" x=\"0.0\" y=\"0.0\" hdg=\"0.0\" length=\"100.0\"><line/></geometry></planView>\n"
        "    <lanes><laneSection s=\"0.0\"><center><lane id=\"0\" type=\"none\"/></center></laneSection></lanes>\n"
        "  </road>\n"
        "</OpenDRIVE>\n";
    pugi::xml_document doc = ParseDoc(xml);
    EXPECT_TRUE(BuildSideModel(doc, Key(7)));
    const OdrSideModel* m = GetSideModel(Key(7));
    ASSERT_NE(m, nullptr);
    ASSERT_TRUE(m->has_license);
    EXPECT_EQ(m->license.name, "CC-BY-4.0");
    EXPECT_EQ(m->license.spdxid, "CC-BY-4.0");
    EXPECT_EQ(m->license.resource, "http://x");
    ASSERT_EQ(m->default_regulations.size(), 2u);
    // roadRegulations
    EXPECT_FALSE(m->default_regulations[0].is_signal);
    EXPECT_EQ(m->default_regulations[0].type, "motorway");
    ASSERT_TRUE(m->default_regulations[0].has_semantics);
    ASSERT_EQ(m->default_regulations[0].semantics.priority_types.size(), 1u);
    EXPECT_EQ(m->default_regulations[0].semantics.priority_types[0], "priorityRoad");
    // signalRegulations reuses the SAME semantics content model (speed)
    EXPECT_TRUE(m->default_regulations[1].is_signal);
    EXPECT_EQ(m->default_regulations[1].type, "1000001");
    EXPECT_EQ(m->default_regulations[1].subtype, "-1");
    ASSERT_EQ(m->default_regulations[1].semantics.speeds.size(), 1u);
    EXPECT_DOUBLE_EQ(m->default_regulations[1].semantics.speeds[0].value, 120.0);
}

// ------------------------------------------------------------------------------------------------
// Sparse-storage invariant: a signal with NO P3/P4 child produces NO signal_extras entry.
TEST(OdrSignalSemantics, SparseNoEntryForPlainSignal)
{
    const std::string body =
        std::string(kSignalHead) + "</signal>";  // plain signal, no children
    pugi::xml_document doc = ParseDoc(RoadWithSignals(body));
    EXPECT_TRUE(BuildSideModel(doc, Key(8)));
    const OdrSideModel* m = GetSideModel(Key(8));
    ASSERT_NE(m, nullptr);
    EXPECT_TRUE(m->signal_extras.empty());
    EXPECT_EQ(GetSignalExtras(Key(8), "1", "1"), nullptr);
    // ...and the audit stays clean (no unsupported entries for a plain signal).
    EXPECT_EQ(m->audit.unsupported_elements, 0u);
    EXPECT_EQ(m->audit.unsupported_attributes, 0u);
}

// An authored EMPTY <semantics/> still yields an entry (lossless: has_semantics true, block empty).
TEST(OdrSignalSemantics, EmptySemanticsStillStored)
{
    const std::string body = std::string(kSignalHead) + "<semantics/></signal>";
    pugi::xml_document doc = ParseDoc(RoadWithSignals(body));
    EXPECT_TRUE(BuildSideModel(doc, Key(9)));
    const OdrSignalExtras* e = GetSignalExtras(Key(9), "1", "1");
    ASSERT_NE(e, nullptr);
    EXPECT_TRUE(e->has_semantics);
    EXPECT_TRUE(e->semantics.IsEmpty());
}

// ------------------------------------------------------------------------------------------------
// Lookup accessor: hit / miss, and duplicate-signal-id-across-roads first-match behavior.
TEST(OdrSignalSemantics, LookupAccessorAndDuplicateId)
{
    // Two roads each with a signal id="7" carrying distinct semantics.
    std::string xml =
        "<OpenDRIVE>\n"
        "  <header revMajor=\"1\" revMinor=\"9\" name=\"t\"/>\n"
        "  <road name=\"a\" length=\"100.0\" id=\"10\" junction=\"-1\">\n"
        "    <planView><geometry s=\"0.0\" x=\"0.0\" y=\"0.0\" hdg=\"0.0\" length=\"100.0\"><line/></geometry></planView>\n"
        "    <lanes><laneSection s=\"0.0\"><center><lane id=\"0\" type=\"none\"/></center></laneSection></lanes>\n"
        "    <signals><signal s=\"50.0\" t=\"-6.0\" id=\"7\" name=\"a7\" dynamic=\"no\" orientation=\"+\" country=\"DE\" type=\"274\" subtype=\"-1\">"
        "      <semantics><priority type=\"stop\"/></semantics></signal></signals>\n"
        "  </road>\n"
        "  <road name=\"b\" length=\"100.0\" id=\"20\" junction=\"-1\">\n"
        "    <planView><geometry s=\"0.0\" x=\"0.0\" y=\"0.0\" hdg=\"0.0\" length=\"100.0\"><line/></geometry></planView>\n"
        "    <lanes><laneSection s=\"0.0\"><center><lane id=\"0\" type=\"none\"/></center></laneSection></lanes>\n"
        "    <signals><signal s=\"50.0\" t=\"-6.0\" id=\"7\" name=\"b7\" dynamic=\"no\" orientation=\"+\" country=\"DE\" type=\"274\" subtype=\"-1\">"
        "      <semantics><priority type=\"yield\"/></semantics></signal></signals>\n"
        "  </road>\n"
        "</OpenDRIVE>\n";
    pugi::xml_document doc = ParseDoc(xml);
    EXPECT_TRUE(BuildSideModel(doc, Key(10)));

    // (road_id, signal_id) is unique -> each resolves to its OWN road's semantics.
    const OdrSignalExtras* a = GetSignalExtras(Key(10), "10", "7");
    ASSERT_NE(a, nullptr);
    ASSERT_EQ(a->semantics.priority_types.size(), 1u);
    EXPECT_EQ(a->semantics.priority_types[0], "stop");
    const OdrSignalExtras* b = GetSignalExtras(Key(10), "20", "7");
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->semantics.priority_types[0], "yield");

    // Miss: unknown road / signal / key.
    EXPECT_EQ(GetSignalExtras(Key(10), "10", "999"), nullptr);
    EXPECT_EQ(GetSignalExtras(Key(10), "99", "7"), nullptr);
    EXPECT_EQ(GetSignalExtras(Key(999), "10", "7"), nullptr);
}
