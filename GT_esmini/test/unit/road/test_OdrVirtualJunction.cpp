// test_OdrVirtualJunction.cpp -- P6 virtual junction tests.
//
// S1 [GT_ODR:vj-model]: pure data-model coverage of the additive RoadManager.hpp members
// (defaults + setter/getter roundtrips).
// S2 [GT_ODR:vj-parse-link]/[GT_ODR:vj-parse-junction] (T1): end-to-end parse through the REAL
// loader (roadmanager::Position::GetOpenDrive()->LoadOpenDriveFile, test_OdrForkPatches pattern)
// on fixtures 23/23b/23c plus synthetic temp xodr strings (connection-less virtual junction,
// denormal elementS, missing orientation). There is no log-capture helper in this suite, so the
// "no 'Not supported yet' WARN" acceptance is asserted by STATE (GetType()==VIRTUAL proves the
// WARN+DEFAULT branch was replaced); WARN wording is not asserted.
//
// Deliberately NOT tested (S3 scope): OpenDrive::GetVirtualJunctionAtRoadS / anchor registry.
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "RoadManager.hpp"

using namespace roadmanager;

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

// Scratch dir + temp writer (mirrors test_OdrForkPatches: <repo>/build is gitignored).
std::filesystem::path VjScratchDir()
{
    std::error_code ec;
    const std::string root = RepoRoot();
    if (!root.empty())
    {
        std::filesystem::path cand = std::filesystem::path(root) / "build" / "odr_vj_tests";
        std::filesystem::create_directories(cand, ec);
        if (!ec && std::filesystem::is_directory(cand))
        {
            return cand;
        }
    }
    std::filesystem::path tmp = std::filesystem::temp_directory_path(ec) / "odr_vj_tests";
    std::filesystem::create_directories(tmp, ec);
    return tmp;
}

std::string WriteVjTemp(const std::string& name, const std::string& content)
{
    const std::filesystem::path p = VjScratchDir() / name;
    std::ofstream               out(p, std::ios::binary);
    out << content;
    out.close();
    return p.string();
}

bool LoadXodr(const std::string& abs_path)
{
    return Position::GetOpenDrive()->LoadOpenDriveFile(abs_path.c_str(), true);
}

std::string FixturePath(const std::string& name)
{
    return RepoRoot() + "/GT_esmini/test/odr_fixtures/handauthored/" + name;
}

// Minimal two-road network whose road 2 predecessor link carries the given raw attribute splice
// (e.g. ` elementS="2.12e-314" elementDir="+"`); `junction_xml` is appended before </OpenDRIVE>.
std::string TwoRoadNetwork(const std::string& link_attr_splice, const std::string& junction_xml)
{
    return std::string("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n") +
           "<OpenDRIVE>\n"
           "  <header revMajor=\"1\" revMinor=\"7\" name=\"vj_synthetic\" version=\"1.00\" date=\"2026-07-04T00:00:00\""
           " north=\"0.0\" south=\"0.0\" east=\"0.0\" west=\"0.0\"/>\n"
           "  <road name=\"main\" length=\"150.0\" id=\"1\" junction=\"-1\">\n"
           "    <link/>\n"
           "    <planView>\n"
           "      <geometry s=\"0.0\" x=\"0.0\" y=\"0.0\" hdg=\"0.0\" length=\"150.0\"><line/></geometry>\n"
           "    </planView>\n"
           "    <lanes>\n"
           "      <laneSection s=\"0.0\">\n"
           "        <center><lane id=\"0\" type=\"none\" level=\"false\"><link/></lane></center>\n"
           "        <right><lane id=\"-1\" type=\"driving\" level=\"false\"><link/>"
           "<width sOffset=\"0.0\" a=\"3.5\" b=\"0.0\" c=\"0.0\" d=\"0.0\"/></lane></right>\n"
           "      </laneSection>\n"
           "    </lanes>\n"
           "  </road>\n"
           "  <road name=\"branch\" length=\"30.0\" id=\"2\" junction=\"-1\">\n"
           "    <link><predecessor elementType=\"road\" elementId=\"1\"" +
           link_attr_splice +
           "/></link>\n"
           "    <planView>\n"
           "      <geometry s=\"0.0\" x=\"75.0\" y=\"0.0\" hdg=\"-0.78539816\" length=\"30.0\"><line/></geometry>\n"
           "    </planView>\n"
           "    <lanes>\n"
           "      <laneSection s=\"0.0\">\n"
           "        <center><lane id=\"0\" type=\"none\" level=\"false\"><link/></lane></center>\n"
           "        <right><lane id=\"-1\" type=\"driving\" level=\"false\"><link/>"
           "<width sOffset=\"0.0\" a=\"3.5\" b=\"0.0\" c=\"0.0\" d=\"0.0\"/></lane></right>\n"
           "      </laneSection>\n"
           "    </lanes>\n"
           "  </road>\n" +
           junction_xml +
           "</OpenDRIVE>\n";
}

}  // namespace

// RoadLink: default-constructed and legacy 4-arg-constructed links are NOT virtual-junction
// links -- element_s_ < 0 (the mid-contact discriminator) and direction UNKNOWN.
TEST(OdrVirtualJunction, RoadLinkElementSDefaults)
{
    RoadLink default_link;
    EXPECT_DOUBLE_EQ(default_link.GetElementS(), -1.0);
    EXPECT_EQ(default_link.GetElementDir(), RoadLink::DIR_UNKNOWN);

    RoadLink legacy_link(SUCCESSOR, RoadLink::ELEMENT_TYPE_ROAD, 2, CONTACT_POINT_START);
    EXPECT_DOUBLE_EQ(legacy_link.GetElementS(), -1.0) << "legacy ctor must leave the mid-contact discriminator unset";
    EXPECT_EQ(legacy_link.GetElementDir(), RoadLink::DIR_UNKNOWN);
    EXPECT_EQ(legacy_link.GetElementId(), 2u);
    EXPECT_EQ(legacy_link.GetContactPointType(), CONTACT_POINT_START);
}

// Junction: VIRTUAL type exists and the S1 VirtualJunctionAttributes default to "absent"
// (ID_UNDEFINED / -1 / -1 / ORIENTATION_NONE -- Ex_Pedestrian_Crossing omits @orientation),
// and roundtrip through Set/GetVirtualAttributes.
TEST(OdrVirtualJunction, JunctionVirtualAttributesRoundtrip)
{
    Junction junction(888, "888", "vj", Junction::VIRTUAL);
    EXPECT_EQ(junction.GetType(), Junction::VIRTUAL);

    const Junction::VirtualJunctionAttributes& defaults = junction.GetVirtualAttributes();
    EXPECT_EQ(defaults.main_road_id_, ID_UNDEFINED);
    EXPECT_DOUBLE_EQ(defaults.s_start_, -1.0);
    EXPECT_DOUBLE_EQ(defaults.s_end_, -1.0);
    EXPECT_EQ(defaults.orientation_, Junction::ORIENTATION_NONE);

    Junction::VirtualJunctionAttributes attributes;
    attributes.main_road_id_ = 1;
    attributes.s_start_      = 95.0;
    attributes.s_end_        = 105.0;
    attributes.orientation_  = Junction::ORIENTATION_PLUS;
    junction.SetVirtualAttributes(attributes);

    const Junction::VirtualJunctionAttributes& stored = junction.GetVirtualAttributes();
    EXPECT_EQ(stored.main_road_id_, 1u);
    EXPECT_DOUBLE_EQ(stored.s_start_, 95.0);
    EXPECT_DOUBLE_EQ(stored.s_end_, 105.0);
    EXPECT_EQ(stored.orientation_, Junction::ORIENTATION_PLUS);
}

// Connection: contact-s / is_virtual_ defaults and the SetVirtual roundtrip. The legacy 3-arg
// ctor only assigns the two road pointers and the contact point (RoadManager.cpp), so nullptr
// roads are safe for a pure field-level test; the dtor only clears the (empty) lane links.
TEST(OdrVirtualJunction, ConnectionVirtualDefaults)
{
    Connection connection(nullptr, nullptr, CONTACT_POINT_START);
    EXPECT_DOUBLE_EQ(connection.GetIncomingContactS(), -1.0);
    EXPECT_DOUBLE_EQ(connection.GetOutgoingContactS(), -1.0);
    EXPECT_FALSE(connection.IsVirtual());

    connection.SetVirtual(true);
    EXPECT_TRUE(connection.IsVirtual()) << "kind-2 topological connections are flagged via SetVirtual (store-only in v1)";
}

// LaneRoadLaneConnection: public pass-through contact_s_ (mirrors the existing public
// contact_point_ member) defaults to "legacy placement".
TEST(OdrVirtualJunction, LaneRoadLaneConnectionContactS)
{
    LaneRoadLaneConnection lane_connection;
    EXPECT_DOUBLE_EQ(lane_connection.contact_s_, -1.0);

    lane_connection.contact_s_ = 100.0;
    EXPECT_DOUBLE_EQ(lane_connection.contact_s_, 100.0);
}

// RoadPath::PathNode: plain-struct contact_s defaults to "legacy end contact".
TEST(OdrVirtualJunction, PathNodeContactS)
{
    RoadPath::PathNode node;
    EXPECT_DOUBLE_EQ(node.contact_s, -1.0);
    EXPECT_EQ(node.link, nullptr);
}

// ---------------------------------------------------------------------------------------------
// S2 (T1): parse-time population -- fixture 23 end-to-end
// ---------------------------------------------------------------------------------------------

// Fixture 23: junction 888 parses as VIRTUAL (state-level proof that the upstream
// "Not supported yet" WARN+DEFAULT branch is gone) with retrievable span attributes.
TEST(OdrVirtualJunction, Fixture23JunctionVirtualWithAttributes)
{
    ASSERT_FALSE(RepoRoot().empty()) << "GT_ODR_REPO_ROOT not defined";
    ASSERT_TRUE(LoadXodr(FixturePath("23_virtual_junction_17.xodr")));
    OpenDrive* odr = Position::GetOpenDrive();

    Junction* junction = odr->GetJunctionByIdStr("888");
    ASSERT_NE(junction, nullptr);
    EXPECT_EQ(junction->GetType(), Junction::VIRTUAL) << "type=\"virtual\" must map to JunctionType::VIRTUAL (not WARN+DEFAULT)";

    Road* main_road = odr->GetRoadByIdStr("1");
    ASSERT_NE(main_road, nullptr);
    const Junction::VirtualJunctionAttributes& attributes = junction->GetVirtualAttributes();
    EXPECT_EQ(attributes.main_road_id_, main_road->GetId());
    EXPECT_DOUBLE_EQ(attributes.s_start_, 95.0);
    EXPECT_DOUBLE_EQ(attributes.s_end_, 105.0);
    EXPECT_EQ(attributes.orientation_, Junction::ORIENTATION_PLUS);

    Position::GetOpenDrive()->Clear();
}

// Fixture 23: road 2's predecessor is a mid-road contact on road 1 (@elementS/@elementDir, NO
// contactPoint) -- [GT_ODR:vj-parse-link] population + the demoted "Missing contact point" path.
TEST(OdrVirtualJunction, Fixture23BranchRoadElementSLink)
{
    ASSERT_FALSE(RepoRoot().empty());
    ASSERT_TRUE(LoadXodr(FixturePath("23_virtual_junction_17.xodr")));
    OpenDrive* odr = Position::GetOpenDrive();

    Road* branch = odr->GetRoadByIdStr("2");
    ASSERT_NE(branch, nullptr);
    RoadLink* pred = branch->GetLink(LinkType::PREDECESSOR);
    ASSERT_NE(pred, nullptr);
    EXPECT_EQ(pred->GetElementType(), RoadLink::ELEMENT_TYPE_ROAD);
    EXPECT_DOUBLE_EQ(pred->GetElementS(), 100.0);
    EXPECT_EQ(pred->GetElementDir(), RoadLink::DIR_PLUS);
    EXPECT_EQ(pred->GetContactPointType(), CONTACT_POINT_UNDEFINED) << "no contactPoint authored; elementS is the discriminator";

    Position::GetOpenDrive()->Clear();
}

// Fixture 23: the default-shaped connection is NOT skipped and carries the anchor s from its
// <predecessor>; the kind-2 <connection type="virtual"> (no connectingRoad) is stored with
// is_virtual_ + null connecting road (documented v1 representation).
TEST(OdrVirtualJunction, Fixture23Connections)
{
    ASSERT_FALSE(RepoRoot().empty());
    ASSERT_TRUE(LoadXodr(FixturePath("23_virtual_junction_17.xodr")));
    OpenDrive* odr = Position::GetOpenDrive();

    Junction* junction = odr->GetJunctionByIdStr("888");
    ASSERT_NE(junction, nullptr);
    ASSERT_EQ(junction->GetNumberOfConnections(), 2u);

    Road* main_road = odr->GetRoadByIdStr("1");
    Road* branch    = odr->GetRoadByIdStr("2");
    ASSERT_NE(main_road, nullptr);
    ASSERT_NE(branch, nullptr);

    Connection* default_connection = junction->GetConnectionByIdx(0);
    ASSERT_NE(default_connection, nullptr);
    EXPECT_FALSE(default_connection->IsVirtual());
    EXPECT_EQ(default_connection->GetIncomingRoad(), main_road);
    EXPECT_EQ(default_connection->GetConnectingRoad(), branch);
    EXPECT_EQ(default_connection->GetContactPoint(), CONTACT_POINT_START);
    EXPECT_DOUBLE_EQ(default_connection->GetIncomingContactS(), 100.0) << "anchor s from the connection <predecessor>";
    EXPECT_DOUBLE_EQ(default_connection->GetOutgoingContactS(), -1.0);
    EXPECT_EQ(default_connection->GetNumberOfLaneLinks(), 1u);

    Connection* kind2 = junction->GetConnectionByIdx(1);
    ASSERT_NE(kind2, nullptr);
    EXPECT_TRUE(kind2->IsVirtual()) << "kind-2 topological connection stored (is_virtual_, store-only in v1)";
    EXPECT_EQ(kind2->GetConnectingRoad(), nullptr);
    EXPECT_EQ(kind2->GetIncomingRoad(), main_road) << "incoming road derived from <predecessor elementId>";
    EXPECT_DOUBLE_EQ(kind2->GetIncomingContactS(), 105.0);
    EXPECT_DOUBLE_EQ(kind2->GetOutgoingContactS(), 0.0);

    Position::GetOpenDrive()->Clear();
}

// Fixture 23b (LHT variant, S5 [vj-lanes] data): parses identically at S2.
TEST(OdrVirtualJunction, Fixture23bLhtVariantParses)
{
    ASSERT_FALSE(RepoRoot().empty());
    ASSERT_TRUE(LoadXodr(FixturePath("23b_virtual_junction_lht_17.xodr")));
    OpenDrive* odr = Position::GetOpenDrive();

    Junction* junction = odr->GetJunctionByIdStr("888");
    ASSERT_NE(junction, nullptr);
    EXPECT_EQ(junction->GetType(), Junction::VIRTUAL);
    EXPECT_EQ(junction->GetNumberOfConnections(), 2u);

    Position::GetOpenDrive()->Clear();
}

// Fork-variant test for the declared parse-loop OVERLAP with [GT_ODR:junc-abort] (fixture 23c):
//   * connection 0: type="virtual" WITH connectingRoad (kind-1) -> normal Connection + anchor
//   * connection 1: type="virtual" WITHOUT connectingRoad (kind-2) -> stored; the fork's
//     junc-abort WARN+skip path must NOT catch it (the @type check runs BEFORE the abort guard)
//   * connection 2: dangling connectingRoad="99" -> junc-abort skips it (fork behavior; upstream
//     pristine would abort the parse -- the recorded overlap residual)
TEST(OdrVirtualJunction, Fixture23cParseVariantsThroughJuncAbort)
{
    ASSERT_FALSE(RepoRoot().empty());
    ASSERT_TRUE(LoadXodr(FixturePath("23c_virtual_junction_parse_variants_17.xodr")))
        << "the dangling default connection must be skipped, not abort the load";
    OpenDrive* odr = Position::GetOpenDrive();

    Junction* junction = odr->GetJunctionByIdStr("999");
    ASSERT_NE(junction, nullptr);
    EXPECT_EQ(junction->GetType(), Junction::VIRTUAL);
    ASSERT_EQ(junction->GetNumberOfConnections(), 2u)
        << "kind-1 + kind-2 kept, dangling connection skipped (junc-abort still effective AFTER the vj branch)";

    Connection* kind1 = junction->GetConnectionByIdx(0);
    ASSERT_NE(kind1, nullptr);
    EXPECT_FALSE(kind1->IsVirtual()) << "kind-1 (with connecting road) is a regular Connection carrying the anchor";
    ASSERT_NE(kind1->GetConnectingRoad(), nullptr);
    EXPECT_EQ(kind1->GetConnectingRoad(), odr->GetRoadByIdStr("2"));
    EXPECT_DOUBLE_EQ(kind1->GetIncomingContactS(), 120.0);

    Connection* kind2 = junction->GetConnectionByIdx(1);
    ASSERT_NE(kind2, nullptr);
    EXPECT_TRUE(kind2->IsVirtual());
    EXPECT_EQ(kind2->GetConnectingRoad(), nullptr);
    EXPECT_DOUBLE_EQ(kind2->GetIncomingContactS(), 118.0);
    EXPECT_DOUBLE_EQ(kind2->GetOutgoingContactS(), 0.0);

    Position::GetOpenDrive()->Clear();
}

// Connection-less virtual junction (Ex_Pedestrian_Crossing junction 555 shape, synthetic twin:
// mainRoad/sStart/sEnd present, @orientation ABSENT, zero <connection>): tolerated, stays VIRTUAL,
// orientation falls back to ORIENTATION_NONE.
TEST(OdrVirtualJunction, ConnectionlessVirtualJunctionTolerated)
{
    const std::string junction_xml =
        "  <junction name=\"pedestrianCrossPath\" type=\"virtual\" id=\"555\" mainRoad=\"1\" sStart=\"57.5\" sEnd=\"61.5\"/>\n";
    const std::string path = WriteVjTemp("vj_connectionless.xodr", TwoRoadNetwork(" contactPoint=\"end\"", junction_xml));
    ASSERT_TRUE(LoadXodr(path));
    OpenDrive* odr = Position::GetOpenDrive();

    Junction* junction = odr->GetJunctionByIdStr("555");
    ASSERT_NE(junction, nullptr);
    EXPECT_EQ(junction->GetType(), Junction::VIRTUAL);
    EXPECT_EQ(junction->GetNumberOfConnections(), 0u);
    const Junction::VirtualJunctionAttributes& attributes = junction->GetVirtualAttributes();
    EXPECT_DOUBLE_EQ(attributes.s_start_, 57.5);
    EXPECT_DOUBLE_EQ(attributes.s_end_, 61.5);
    EXPECT_EQ(attributes.orientation_, Junction::ORIENTATION_NONE) << "missing @orientation -> NONE (+ WARN), not an error";

    Position::GetOpenDrive()->Clear();
}

// Defensive numeric validation: a subnormal elementS (official UC_ParamPoly3.xodr carries
// 2.12e-314 garbage) is rejected with a WARN and falls back to the legacy end-contact
// representation; the load succeeds.
TEST(OdrVirtualJunction, DenormalElementSFallsBackToLegacy)
{
    const std::string path =
        WriteVjTemp("vj_denormal.xodr", TwoRoadNetwork(" elementS=\"2.1219957904712067e-314\" elementDir=\"+\"", ""));
    ASSERT_TRUE(LoadXodr(path)) << "denormal elementS must not kill the load";
    OpenDrive* odr = Position::GetOpenDrive();

    Road* branch = odr->GetRoadByIdStr("2");
    ASSERT_NE(branch, nullptr);
    RoadLink* pred = branch->GetLink(LinkType::PREDECESSOR);
    ASSERT_NE(pred, nullptr);
    EXPECT_DOUBLE_EQ(pred->GetElementS(), -1.0) << "subnormal value rejected -> legacy fallback";
    EXPECT_EQ(pred->GetElementDir(), RoadLink::DIR_UNKNOWN) << "elementDir not stored when elementS is rejected";

    Position::GetOpenDrive()->Clear();
}

// RoadLink 6-arg ctor (defined at S2) + operator== now distinguishing element_s_/element_dir_.
TEST(OdrVirtualJunction, RoadLinkOperatorEqDistinguishesElementS)
{
    RoadLink mid_contact(SUCCESSOR, RoadLink::ELEMENT_TYPE_ROAD, 1, CONTACT_POINT_UNDEFINED, 100.0, RoadLink::DIR_PLUS);
    EXPECT_DOUBLE_EQ(mid_contact.GetElementS(), 100.0);
    EXPECT_EQ(mid_contact.GetElementDir(), RoadLink::DIR_PLUS);

    RoadLink same(SUCCESSOR, RoadLink::ELEMENT_TYPE_ROAD, 1, CONTACT_POINT_UNDEFINED, 100.0, RoadLink::DIR_PLUS);
    EXPECT_TRUE(mid_contact == same);

    RoadLink other_s(SUCCESSOR, RoadLink::ELEMENT_TYPE_ROAD, 1, CONTACT_POINT_UNDEFINED, 105.0, RoadLink::DIR_PLUS);
    EXPECT_FALSE(mid_contact == other_s) << "operator== must distinguish element_s_";

    RoadLink legacy(SUCCESSOR, RoadLink::ELEMENT_TYPE_ROAD, 1, CONTACT_POINT_UNDEFINED);
    EXPECT_FALSE(mid_contact == legacy) << "mid-road contact != legacy end contact";

    RoadLink other_dir(SUCCESSOR, RoadLink::ELEMENT_TYPE_ROAD, 1, CONTACT_POINT_UNDEFINED, 100.0, RoadLink::DIR_MINUS);
    EXPECT_FALSE(mid_contact == other_dir) << "operator== must distinguish element_dir_";
}
