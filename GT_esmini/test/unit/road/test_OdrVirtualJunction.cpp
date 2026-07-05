// test_OdrVirtualJunction.cpp -- P6 virtual junction tests.
//
// S1 [GT_ODR:vj-model]: pure data-model coverage of the additive RoadManager.hpp members
// (defaults + setter/getter roundtrips).
// S2 [GT_ODR:vj-parse-link]/[GT_ODR:vj-parse-junction] (T1): end-to-end parse through the REAL
// loader (roadmanager::Position::GetOpenDrive()->LoadOpenDriveFile, test_OdrForkPatches pattern)
// on fixtures 23/23b/23c plus synthetic temp xodr strings (connection-less virtual junction,
// denormal elementS, missing orientation). The S2 "no WARN" acceptances were asserted by STATE;
// S3 added a minimal RAII log-capture helper (VjLogCapture) for the zero-WARN load acceptance.
//
// S3 [GT_ODR:vj-synth]/[vj-membership]/[vj-osi-class]: counter-connection synthesis, the anchor
// registry (GetVirtualJunctionAtRoadS/GetVirtualJunctionAnchors), the T2 pass-through invariant,
// membership pinning (span reports false/ID_UNDEFINED) and IsOsiIntersection(VIRTUAL)==false.
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "RoadManager.hpp"
#include "logger.hpp"

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

// Minimal GT-owned RAII log capture: txtLogger fans every message out to registered callbacks
// (logger.cpp TxtLogger::Log) regardless of console/file sinks. The callback is a plain function
// pointer, so the accumulator is static. ~VjLogCapture uses ClearCallbacks(), which clears ALL
// callbacks -- fine in this test binary (nothing else registers one).
class VjLogCapture
{
public:
    VjLogCapture()
    {
        Lines().clear();
        txtLogger.RegisterCallback(&VjLogCapture::OnLog);
    }
    ~VjLogCapture()
    {
        txtLogger.ClearCallbacks();
    }
    static std::vector<std::string>& Lines()
    {
        static std::vector<std::string> lines;
        return lines;
    }
    static bool Contains(const std::string& needle)
    {
        for (const std::string& line : Lines())
        {
            if (line.find(needle) != std::string::npos)
            {
                return true;
            }
        }
        return false;
    }

private:
    static void OnLog(const std::string& msg)
    {
        Lines().push_back(msg);
    }
};

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
    // S2 parsed 2 connections; S3 EstablishVirtualJunctionConnections appends the branch->main
    // counter-connection (covered by Fixture23S3CounterConnectionSynthesized). Parsed ones keep idx 0/1.
    ASSERT_EQ(junction->GetNumberOfConnections(), 3u);

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
    EXPECT_EQ(junction->GetNumberOfConnections(), 3u) << "2 parsed + 1 S3-synthesized counter-connection";

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
    ASSERT_EQ(junction->GetNumberOfConnections(), 3u)
        << "kind-1 + kind-2 kept, dangling connection skipped (junc-abort still effective AFTER the vj branch); "
           "S3 appends the kind-1 counter-connection";

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

// ---------------------------------------------------------------------------------------------
// S3 [GT_ODR:vj-synth]: counter-connection synthesis + anchor registry
// ---------------------------------------------------------------------------------------------

// Fixture 23: EstablishVirtualJunctionConnections() synthesizes the branch->main counter-connection
// (CheckJunctionConnection auto-add template): connection count grows 2 -> 3, incoming=branch(2),
// connecting=main(1), contact START (elementDir '+' reverse-merge rule: land at anchor_s heading
// s-increasing), incoming_contact_s_ = 0 (the branch PREDECESSOR end anchors), outgoing_contact_s_ =
// 100 (the anchor s on the main road), lane links reversed from the original connection.
TEST(OdrVirtualJunction, Fixture23S3CounterConnectionSynthesized)
{
    ASSERT_FALSE(RepoRoot().empty());
    ASSERT_TRUE(LoadXodr(FixturePath("23_virtual_junction_17.xodr")));
    OpenDrive* odr = Position::GetOpenDrive();

    Junction* junction  = odr->GetJunctionByIdStr("888");
    Road*     main_road = odr->GetRoadByIdStr("1");
    Road*     branch    = odr->GetRoadByIdStr("2");
    ASSERT_NE(junction, nullptr);
    ASSERT_NE(main_road, nullptr);
    ASSERT_NE(branch, nullptr);
    ASSERT_EQ(junction->GetNumberOfConnections(), 3u) << "2 parsed + 1 synthesized counter-connection";

    Connection* counter = nullptr;
    for (unsigned int i = 0; i < junction->GetNumberOfConnections(); i++)
    {
        Connection* connection = junction->GetConnectionByIdx(i);
        if (connection->GetIncomingRoad() == branch && connection->GetConnectingRoad() == main_road)
        {
            counter = connection;
        }
    }
    ASSERT_NE(counter, nullptr) << "branch->main counter-connection must be visible via GetConnectionByIdx";
    EXPECT_EQ(counter->GetContactPoint(), CONTACT_POINT_START) << "elementDir '+' -> START at anchor_s (reverse-merge rule)";
    EXPECT_DOUBLE_EQ(counter->GetIncomingContactS(), 0.0) << "branch contact s (predecessor end of road 2)";
    EXPECT_DOUBLE_EQ(counter->GetOutgoingContactS(), 100.0) << "anchor s on the main road";
    ASSERT_EQ(counter->GetNumberOfLaneLinks(), 1u);
    EXPECT_EQ(counter->GetLaneLink(0)->from_, -1) << "lane link reversed from the original (from=-1 to=-1 is symmetric)";
    EXPECT_EQ(counter->GetLaneLink(0)->to_, -1);
    EXPECT_FALSE(counter->IsVirtual());

    Position::GetOpenDrive()->Clear();
}

// Fixture 23: the per-main-road anchor registry. GetVirtualJunctionAtRoadS does INCLUSIVE span
// containment on [sStart, sEnd] = [95, 105] (documented boundary behavior: 95 and 105 hit, values
// strictly outside miss); GetVirtualJunctionAnchors falls back to a static empty vector.
TEST(OdrVirtualJunction, Fixture23S3AnchorRegistry)
{
    ASSERT_FALSE(RepoRoot().empty());
    ASSERT_TRUE(LoadXodr(FixturePath("23_virtual_junction_17.xodr")));
    OpenDrive* odr = Position::GetOpenDrive();

    Junction*  junction  = odr->GetJunctionByIdStr("888");
    Road*      main_road = odr->GetRoadByIdStr("1");
    Road*      branch    = odr->GetRoadByIdStr("2");
    const id_t main_id   = main_road->GetId();

    EXPECT_EQ(odr->GetVirtualJunctionAtRoadS(main_id, 100.0), junction) << "anchor s inside the span";
    EXPECT_EQ(odr->GetVirtualJunctionAtRoadS(main_id, 50.0), nullptr) << "on the main road but outside the span";
    EXPECT_EQ(odr->GetVirtualJunctionAtRoadS(main_id, 95.0), junction) << "span start is INCLUSIVE";
    EXPECT_EQ(odr->GetVirtualJunctionAtRoadS(main_id, 105.0), junction) << "span end is INCLUSIVE";
    EXPECT_EQ(odr->GetVirtualJunctionAtRoadS(main_id, 94.999), nullptr) << "just below the span";
    EXPECT_EQ(odr->GetVirtualJunctionAtRoadS(main_id, 105.001), nullptr) << "just above the span";
    EXPECT_EQ(odr->GetVirtualJunctionAtRoadS(branch->GetId(), 10.0), nullptr) << "branch road has no spans";

    const std::vector<OpenDrive::VirtualJunctionAnchor>& anchors = odr->GetVirtualJunctionAnchors(main_id);
    ASSERT_GE(anchors.size(), 1u);
    ASSERT_EQ(anchors.size(), 1u) << "one kind-1 connection = one anchor (the kind-2 connection is store-only)";
    EXPECT_EQ(anchors[0].junction_, junction);
    EXPECT_EQ(anchors[0].connection_idx_, 0u) << "index of the ORIGINAL main->branch connection";
    EXPECT_DOUBLE_EQ(anchors[0].anchor_s_, 100.0);
    EXPECT_EQ(anchors[0].dir_, RoadLink::DIR_PLUS);
    ASSERT_NE(anchors[0].link_, nullptr) << "registry-owned synthesized RoadLink (stable per-anchor identity)";
    EXPECT_EQ(anchors[0].link_->GetElementId(), branch->GetId());
    EXPECT_EQ(anchors[0].link_->GetContactPointType(), CONTACT_POINT_START) << "branch entered at its predecessor end";
    EXPECT_TRUE(odr->GetVirtualJunctionAnchors(static_cast<id_t>(987654)).empty()) << "static empty fallback";

    Position::GetOpenDrive()->Clear();
}

// [GT_ODR:vj-osi-class] fork-variant test (manifest overlap_residuals: Junction::IsOsiIntersection):
// the VIRTUAL short-circuit fires before everything else in the fork, where the trailing else also
// carries the P5 [GT_ODR:junc-crossing] empty-connection guard -- junction 888 has connections AND
// is virtual, so only the new branch can explain false here (a DEFAULT junction with town roads
// and connections returns true).
TEST(OdrVirtualJunction, Fixture23S3OsiClassification)
{
    ASSERT_FALSE(RepoRoot().empty());
    ASSERT_TRUE(LoadXodr(FixturePath("23_virtual_junction_17.xodr")));

    Junction* junction = Position::GetOpenDrive()->GetJunctionByIdStr("888");
    ASSERT_NE(junction, nullptr);
    EXPECT_EQ(junction->GetType(), Junction::VIRTUAL);
    EXPECT_FALSE(junction->IsOsiIntersection()) << "virtual junctions are never OSI intersections (S3 acceptance)";

    Position::GetOpenDrive()->Clear();
}

// S3 acceptance: the synthesis pass must NOT remap the branch road's lanes -- ids and count on
// road 2 stay exactly as authored (left 1, center 0, right -1 in one lane section).
TEST(OdrVirtualJunction, Fixture23S3BranchLanesNotRemapped)
{
    ASSERT_FALSE(RepoRoot().empty());
    ASSERT_TRUE(LoadXodr(FixturePath("23_virtual_junction_17.xodr")));

    Road* branch = Position::GetOpenDrive()->GetRoadByIdStr("2");
    ASSERT_NE(branch, nullptr);
    ASSERT_EQ(branch->GetNumberOfLaneSections(), 1u);
    LaneSection* lane_section = branch->GetLaneSectionByIdx(0);
    ASSERT_NE(lane_section, nullptr);
    ASSERT_EQ(lane_section->GetNumberOfLanes(), 3u);
    EXPECT_NE(lane_section->GetLaneById(1), nullptr);
    EXPECT_NE(lane_section->GetLaneById(0), nullptr);
    EXPECT_NE(lane_section->GetLaneById(-1), nullptr);

    Position::GetOpenDrive()->Clear();
}

// Kind-2 topological connection: still stored after the synthesis pass (skipped, not consumed --
// no counter-connection is synthesized from it), and nothing crashed on its null connecting road.
TEST(OdrVirtualJunction, Fixture23S3Kind2StoredNotSynthesized)
{
    ASSERT_FALSE(RepoRoot().empty());
    ASSERT_TRUE(LoadXodr(FixturePath("23_virtual_junction_17.xodr")));
    OpenDrive* odr = Position::GetOpenDrive();

    Junction*    junction    = odr->GetJunctionByIdStr("888");
    unsigned int kind2_count = 0;
    for (unsigned int i = 0; i < junction->GetNumberOfConnections(); i++)
    {
        if (junction->GetConnectionByIdx(i)->IsVirtual())
        {
            EXPECT_EQ(junction->GetConnectionByIdx(i)->GetConnectingRoad(), nullptr);
            kind2_count++;
        }
    }
    EXPECT_EQ(kind2_count, 1u) << "kind-2 connection survives S3 (store-only in v1)";
    EXPECT_EQ(junction->GetNumberOfConnections(), 3u) << "no counter synthesized from the kind-2 connection";

    Position::GetOpenDrive()->Clear();
}

// ---------------------------------------------------------------------------------------------
// S3 T2 pass-through invariant + [GT_ODR:vj-membership] pinning
// ---------------------------------------------------------------------------------------------

// T2 (stage-critical, design test plan): with NO route set, MoveAlongS across the anchor at s=100
// must pass straight through -- the VJ anchor must neither divert nor stop the move. From s=50,
// MoveAlongS(150) lands exactly at s == road length (200): upstream's end-of-road branch triggers
// on STRICTLY greater (s_ + ds > length), so the exact-length move returns OK and stays on road 1;
// the next move returns the END_OF_ROAD family code with the position clamped at s = 200.
TEST(OdrVirtualJunction, Fixture23T2PassThroughInvariant)
{
    ASSERT_FALSE(RepoRoot().empty());
    ASSERT_TRUE(LoadXodr(FixturePath("23_virtual_junction_17.xodr")));

    Road*      main_road = Position::GetOpenDrive()->GetRoadByIdStr("1");
    const id_t main_id   = main_road->GetId();

    Position pos;
    ASSERT_GE(static_cast<int>(pos.SetLanePos(main_id, -1, 50.0, 0.0)), 0);
    Position::ReturnCode ret = pos.MoveAlongS(150.0);
    EXPECT_GE(static_cast<int>(ret), 0) << "crossing the anchor at s=100 must not fail or divert";
    EXPECT_EQ(pos.GetTrackId(), main_id) << "T2: still on the main road (no diversion at the anchor)";
    EXPECT_NEAR(pos.GetS(), main_road->GetLength(), 1e-9) << "T2: full 150 m travelled to the road end";

    Position::ReturnCode end_ret = pos.MoveAlongS(1.0);
    EXPECT_EQ(end_ret, Position::ReturnCode::ERROR_END_OF_ROAD) << "link-less main road end -> END_OF_ROAD family";
    EXPECT_EQ(pos.GetTrackId(), main_id);
    EXPECT_NEAR(pos.GetS(), main_road->GetLength(), 1e-9);

    Position::GetOpenDrive()->Clear();
}

// [GT_ODR:vj-membership] pinning (INTERPRETIVE, v1): a position ON the virtual junction span of the
// main road keeps reporting "not in a junction" -- the unsplit main road carries junction == -1
// (avoids ScenarioEngine junction-selector re-randomization; surfaced upstream in #592).
TEST(OdrVirtualJunction, Fixture23MembershipPinnedFalseOnSpan)
{
    ASSERT_FALSE(RepoRoot().empty());
    ASSERT_TRUE(LoadXodr(FixturePath("23_virtual_junction_17.xodr")));

    const id_t main_id = Position::GetOpenDrive()->GetRoadByIdStr("1")->GetId();
    Position   pos;
    ASSERT_GE(static_cast<int>(pos.SetLanePos(main_id, -1, 100.0, 0.0)), 0);  // exactly at the anchor, inside [95, 105]
    EXPECT_FALSE(pos.IsInJunction()) << "v1 membership: main-road span reports false";
    EXPECT_EQ(pos.GetJunctionId(), ID_UNDEFINED) << "v1 membership: main-road span reports ID_UNDEFINED";

    Position::GetOpenDrive()->Clear();
}

// S3 acceptance: fixture 23 loads with ZERO link WARNs -- the [vj-synth] CheckLink short-circuit
// kills "Reversed road link" for the elementS link and the S2 [vj-parse-link] demotion killed the
// parse-time "Missing contact point type" ERROR.
TEST(OdrVirtualJunction, Fixture23ZeroWarnLoad)
{
    ASSERT_FALSE(RepoRoot().empty());

    VjLogCapture capture;
    ASSERT_TRUE(LoadXodr(FixturePath("23_virtual_junction_17.xodr")));
    EXPECT_FALSE(VjLogCapture::Contains("Reversed road link")) << "CheckLink short-circuit must silence the reverse-link WARN";
    EXPECT_FALSE(VjLogCapture::Contains("Missing contact point type")) << "S2 parse demotion regression";
    EXPECT_FALSE(VjLogCapture::Contains("unusable span")) << "the span [95, 105] on road 1 (length 200) is valid";
    EXPECT_FALSE(VjLogCapture::Contains("no elementS link")) << "connection 0's branch road anchors via elementS";

    Position::GetOpenDrive()->Clear();
}

// ---------------------------------------------------------------------------------------------
// S4 [GT_ODR:vj-path] RoadPath (T3): the link-less main road reaches the branch continuation.
// ---------------------------------------------------------------------------------------------

// T3 forward: RoadPath (road1 lane -1 s=10) -> (road3 lane -1 s=20) resolves via road 2. The
// distance is hand-computed on the fixture geometry:
//   * road1 leg: the anchor sits at s=100, start at s=10 -> 100 - 10 = 90 m to the branch-off
//   * road2 leg: the branch (length 30) is entered at its predecessor end (s=0) and fully
//                traversed to its successor (road3) -> 30 m
//   * road3 leg: entered at s=0 (road3.predecessor contactPoint=start seen from road2) -> 20 m to s=20
//   TOTAL = 90 + 30 + 20 = 140 m.  The main road carries no end link, so this ONLY resolves via
//   the [vj-path] registry seeding + mid-road anchor expansion.
TEST(OdrVirtualJunction, Fixture23S4RoadPathForwardThroughBranch)
{
    ASSERT_FALSE(RepoRoot().empty());
    ASSERT_TRUE(LoadXodr(FixturePath("23_virtual_junction_17.xodr")));

    Position start;
    ASSERT_GE(static_cast<int>(start.SetLanePos(1, -1, 10.0, 0.0)), 0);
    start.SetHeadingRelative(0.0);  // aligned with +s driving direction
    Position target;
    ASSERT_GE(static_cast<int>(target.SetLanePos(3, -1, 20.0, 0.0)), 0);

    RoadPath path(&start, &target);
    double   dist = 0.0;
    ASSERT_EQ(path.Calculate(dist, true), 0) << "the link-less main road must reach road 3 through the VJ anchor";
    EXPECT_NEAR(fabs(dist), 140.0, 1e-6) << "90 (road1 10->100) + 30 (road2) + 20 (road3 0->20)";

    Position::GetOpenDrive()->Clear();
}

// T3 reverse: RoadPath (road3 lane -1 s=20) -> (road1 lane -1 s=10). The path leaves road 3 to
// road 2, then road 2's mid-road predecessor (elementS=100) lands on road 1 at the anchor; the
// [vj-path] target-hit measures the remaining |100 - 10| = 90 m on the main road:
//   road3 leg 20 + road2 leg 30 + road1 leg 90 = 140 m (symmetric with the forward direction).
TEST(OdrVirtualJunction, Fixture23S4RoadPathReverseMergeBack)
{
    ASSERT_FALSE(RepoRoot().empty());
    ASSERT_TRUE(LoadXodr(FixturePath("23_virtual_junction_17.xodr")));

    Position start;
    ASSERT_GE(static_cast<int>(start.SetLanePos(3, -1, 20.0, 0.0)), 0);
    start.SetHeadingRelative(0.0);
    Position target;
    ASSERT_GE(static_cast<int>(target.SetLanePos(1, -1, 10.0, 0.0)), 0);

    RoadPath path(&start, &target);
    double   dist = 0.0;
    ASSERT_EQ(path.Calculate(dist, true), 0) << "reverse must merge back onto the main road at the anchor";
    EXPECT_NEAR(fabs(dist), 140.0, 1e-6) << "20 (road3) + 30 (road2) + 90 (road1 100->10 via anchor target-hit)";

    Position::GetOpenDrive()->Clear();
}

// Position::Delta ds between the two positions == the hand-computed anchor length (sign folds into
// the driving-direction convention; magnitude is the load-bearing assertion here).
TEST(OdrVirtualJunction, Fixture23S4DeltaAcrossAnchor)
{
    ASSERT_FALSE(RepoRoot().empty());
    ASSERT_TRUE(LoadXodr(FixturePath("23_virtual_junction_17.xodr")));

    Position a;
    ASSERT_GE(static_cast<int>(a.SetLanePos(1, -1, 10.0, 0.0)), 0);
    a.SetHeadingRelative(0.0);
    Position b;
    ASSERT_GE(static_cast<int>(b.SetLanePos(3, -1, 20.0, 0.0)), 0);

    PositionDiff diff;
    ASSERT_TRUE(a.Delta(&b, diff, true, LARGE_NUMBER)) << "Delta must find the VJ path";
    EXPECT_NEAR(fabs(diff.ds), 140.0, 1e-6) << "Delta ds threads the RoadPath anchor distance";

    Position::GetOpenDrive()->Clear();
}

// ---------------------------------------------------------------------------------------------
// S4 [GT_ODR:vj-route] Route + CalcRoutePosition BEFORE and AFTER the anchor.
// ---------------------------------------------------------------------------------------------

// A route (road1 lane -1 s=10) -> (road3 lane -1 s=20) auto-expands the intermediate branch road 2
// via RoadPath (AddWaypoint internal-waypoint machinery), then CalcRoutePosition (SetTrackS) maps a
// probe position to route-s. Assertions straddle the anchor at s=100 on the main road:
//   (road1 s=10)  -> route-s 0      (route origin)
//   (road1 s=99)  -> route-s 89     (BEFORE the anchor, still on the road1 leg [10,100])
//   (road2 s=1)   -> route-s 91     (90 to the branch-off + 1 on the branch)
//   (road3 s=5)   -> route-s 125    (90 + 30 + 5)
//   (road1 s=150) -> NOT on route   ([vj-route] anchor-span clamp: past s=100 you left the route
//                                    onto the unsplit main road; SetRoute must reject, not extrapolate)
TEST(OdrVirtualJunction, Fixture23S4RouteBeforeAndAfterAnchor)
{
    ASSERT_FALSE(RepoRoot().empty());
    ASSERT_TRUE(LoadXodr(FixturePath("23_virtual_junction_17.xodr")));

    Route*   route = new Route;
    Position wp0;
    wp0.SetLanePos(1, -1, 10.0, 0.0);
    wp0.SetHeadingRelative(0.0);
    Position wp1;
    wp1.SetLanePos(3, -1, 20.0, 0.0);
    route->AddWaypoint(wp0);
    route->AddWaypoint(wp1);

    EXPECT_NEAR(route->GetLength(), 140.0, 1e-6) << "route length = the VJ RoadPath distance";

    // Map track-s to route-s directly through Route::SetTrackS (the [vj-route] site under test); GetPathS()
    // reads back the accumulated route-s. update_state=false keeps the route reusable across probes without
    // CalcRoutePosition's XYZ2Route off-route fallback (which threads the S5-deferred along-route VJ path).
    ASSERT_GE(static_cast<int>(route->SetTrackS(1, 10.0, false)), 0);
    EXPECT_NEAR(route->GetPathS(), 0.0, 1e-3) << "route origin (road1 s=10)";

    ASSERT_GE(static_cast<int>(route->SetTrackS(1, 99.0, false)), 0);
    EXPECT_NEAR(route->GetPathS(), 89.0, 1e-3) << "before the anchor on the main-road leg";

    ASSERT_GE(static_cast<int>(route->SetTrackS(1, 100.0, false)), 0) << "exactly at the anchor is on-route (inclusive)";
    EXPECT_NEAR(route->GetPathS(), 90.0, 1e-3) << "route-s at the branch-off anchor";

    ASSERT_GE(static_cast<int>(route->SetTrackS(2, 1.0, false)), 0);
    EXPECT_NEAR(route->GetPathS(), 91.0, 1e-3) << "1 m onto the branch past the s=100 branch-off";

    ASSERT_GE(static_cast<int>(route->SetTrackS(3, 5.0, false)), 0);
    EXPECT_NEAR(route->GetPathS(), 125.0, 1e-3) << "90 + 30 + 5 on the continuation road";

    // Beyond the anchor on the main road: the [vj-route] clamp rejects (would else over-extrapolate toward 140).
    EXPECT_LT(static_cast<int>(route->SetTrackS(1, 150.0, false)), 0) << "[vj-route] clamp: past the anchor the main road is off the route";

    delete route;
    Position::GetOpenDrive()->Clear();
}

// [GT_ODR:vj-connect] lockOnLane PINNING test (S4 DEFERRAL, manifest s4_deferrals): the lockOnLane
// XYZ2TrackPos direction-flip is NOT yet elementDir-aware (deferred to S5). This test PINS the
// current behavior so the deferral is explicit and a future S5 change is a visible diff: an XY probe
// near the branch-off on the main road locks onto a lane on the main road (road 1) without crashing
// and without a spurious cross onto the branch at the anchor.
TEST(OdrVirtualJunction, Fixture23T5LockOnLanePinnedAcrossAnchor)
{
    ASSERT_FALSE(RepoRoot().empty());
    ASSERT_TRUE(LoadXodr(FixturePath("23_virtual_junction_17.xodr")));

    // Reference point on road 1 lane -1 just BEFORE the branch-off (s=98, inside the span [95,105] but
    // clear of road 2's geometry origin at s=100), then read its world XY and re-probe with lockOnLane.
    Position ref;
    ASSERT_GE(static_cast<int>(ref.SetLanePos(1, -1, 98.0, 0.0)), 0);
    const double px = ref.GetX();
    const double py = ref.GetY();

    Position probe;
    probe.SetLockOnLane(true);
    probe.SetInertiaPos(px, py, 0.0, true);
    // PIN the current (S5-deferred) behavior: lockOnLane resolves to the MAIN road (road 1) inside the VJ
    // span -- it does NOT spuriously cross onto the branch (road 2). The elementDir-aware direction flip is
    // deferred (manifest s4_deferrals); when it lands in S5 this assertion is the visible regression sensor.
    EXPECT_EQ(probe.GetTrackId(), 1u) << "PINNED (S4): lockOnLane stays on the main road, no cross to the branch at the VJ anchor";

    Position::GetOpenDrive()->Clear();
}

// Sanity companion: without lockOnLane the same XY probe also resolves to the main road (road 1) -- the VJ
// anchor does not perturb plain XY localization on the unsplit main road (T2-adjacent invariant).
TEST(OdrVirtualJunction, Fixture23S4XyLocalizationStaysOnMainRoad)
{
    ASSERT_FALSE(RepoRoot().empty());
    ASSERT_TRUE(LoadXodr(FixturePath("23_virtual_junction_17.xodr")));

    Position ref;
    ASSERT_GE(static_cast<int>(ref.SetLanePos(1, -1, 98.0, 0.0)), 0);
    const double px = ref.GetX();
    const double py = ref.GetY();

    Position probe;
    probe.SetInertiaPos(px, py, 0.0, true);
    EXPECT_EQ(probe.GetTrackId(), 1u) << "plain XY localization stays on the unsplit main road inside the VJ span";
    EXPECT_NEAR(probe.GetS(), 98.0, 1e-3) << "and recovers the main-road s (no anchor diversion)";

    Position::GetOpenDrive()->Clear();
}
