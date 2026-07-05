// test_OdrRailroad.cpp -- P9a railroad/station side-model tests (plan §5 P9, cluster 20).
//
// Two patterns, mirroring the other Odr* tests:
//   * Inline XML (test_OdrSideModel.cpp idiom): ParseDoc + BuildSideModel(doc, Key(N)) + GetSideModel,
//     for the round-trip / sparse / audit-zero assertions on tiny hand-built documents.
//   * File-based official fixtures (test_OdrJunctionExtras.cpp idiom): LoadOpenDriveFile via
//     Position::GetOpenDrive(), guarded with GTEST_SKIP when the gitignored ASAM fixture is absent.
//
// The railroad/station data is L1 storage only and INERT (no runtime consumer); these tests pin the
// storage + accessors + the whitelist (audit-zero).
#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

#include "RoadManager.hpp"
#include "gt_esmini/road/OdrSideModel.hpp"
#include "pugixml.hpp"

namespace fs = std::filesystem;
using namespace gt_esmini::odr;

namespace
{
// Parse a raw xodr string into a pugi document (asserts well-formedness).
pugi::xml_document ParseDoc(const char* xml)
{
    pugi::xml_document        doc;
    pugi::xml_parse_result res = doc.load_string(xml);
    EXPECT_TRUE(res) << "XML parse error: " << res.description();
    return doc;
}

// Distinct dummy keys per test so the registry never cross-contaminates.
const void* Key(int tag)
{
    static char slots[64];
    return &slots[tag & 63];
}

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
}  // namespace

// 1a. Inline XML: a full <switch> (all attrs + mainTrack/sideTrack + partner) round-trips through the
//     side model and the GetRailSwitch / GetRoadRailSwitches accessors.
TEST(OdrRailroad, InlineSwitchFullRoundTrip)
{
    const char* xml = R"(<OpenDRIVE>
  <header revMajor="1" revMinor="8"/>
  <road name="" length="54.5" id="1" junction="-1">
    <planView><geometry s="0.0" x="0.0" y="0.0" hdg="0.0" length="54.5"><line/></geometry></planView>
    <railroad>
      <switch name="Switch12" id="12" position="dynamic">
        <mainTrack id="1" s="10.0" dir="+"/>
        <sideTrack id="2" s="0.0" dir="+"/>
        <partner name="Switch32" id="32"/>
      </switch>
    </railroad>
  </road>
</OpenDRIVE>)";
    pugi::xml_document doc = ParseDoc(xml);
    EXPECT_TRUE(BuildSideModel(doc, Key(1)));

    const OdrSideModel* m = GetSideModel(Key(1));
    ASSERT_NE(m, nullptr);
    ASSERT_EQ(m->rail_switches.size(), 1u);

    const OdrRailSwitch* sw = GetRailSwitch(Key(1), "1", "12");
    ASSERT_NE(sw, nullptr);
    EXPECT_EQ(sw->road_id, "1");
    EXPECT_EQ(sw->name, "Switch12");
    EXPECT_EQ(sw->id, "12");
    EXPECT_EQ(sw->position, "dynamic");

    ASSERT_TRUE(sw->has_main_track);
    EXPECT_EQ(sw->main_track.id, "1");
    EXPECT_DOUBLE_EQ(sw->main_track.s, 10.0);
    EXPECT_EQ(sw->main_track.dir, "+");

    ASSERT_TRUE(sw->has_side_track);
    EXPECT_EQ(sw->side_track.id, "2");
    EXPECT_DOUBLE_EQ(sw->side_track.s, 0.0);
    EXPECT_EQ(sw->side_track.dir, "+");

    ASSERT_TRUE(sw->has_partner);
    EXPECT_EQ(sw->partner_name, "Switch32");
    EXPECT_EQ(sw->partner_id, "32");

    // GetRoadRailSwitches copies the road's switches; a real side model -> true.
    std::vector<OdrRailSwitch> out;
    ASSERT_TRUE(GetRoadRailSwitches(Key(1), "1", out));
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].id, "12");

    // Miss: unknown switch id -> nullptr; unknown road but real model -> true + empty out.
    EXPECT_EQ(GetRailSwitch(Key(1), "1", "999"), nullptr);
    std::vector<OdrRailSwitch> none;
    EXPECT_TRUE(GetRoadRailSwitches(Key(1), "does-not-exist", none));
    EXPECT_TRUE(none.empty());
}

// 1b. Inline XML: a <switch> without a <partner> -> has_partner=false.
TEST(OdrRailroad, InlineSwitchWithoutPartner)
{
    const char* xml = R"(<OpenDRIVE>
  <header revMajor="1" revMinor="8"/>
  <road name="" length="30.0" id="2" junction="-1">
    <planView><geometry s="0.0" x="0.0" y="0.0" hdg="0.0" length="30.0"><line/></geometry></planView>
    <railroad>
      <switch name="S1" id="1" position="straight">
        <mainTrack id="10" s="5.0" dir="-"/>
        <sideTrack id="11" s="6.0" dir="+"/>
      </switch>
    </railroad>
  </road>
</OpenDRIVE>)";
    pugi::xml_document doc = ParseDoc(xml);
    EXPECT_TRUE(BuildSideModel(doc, Key(2)));

    const OdrRailSwitch* sw = GetRailSwitch(Key(2), "2", "1");
    ASSERT_NE(sw, nullptr);
    EXPECT_EQ(sw->position, "straight");
    EXPECT_TRUE(sw->has_main_track);
    EXPECT_TRUE(sw->has_side_track);
    EXPECT_FALSE(sw->has_partner);
    EXPECT_EQ(sw->partner_name, "");
    EXPECT_EQ(sw->partner_id, "");
}

// 1c. Inline XML: an empty <railroad/> stores NOTHING (rail_switches empty) and produces zero audit.
TEST(OdrRailroad, InlineEmptyRailroadStoresNothing)
{
    const char* xml = R"(<OpenDRIVE>
  <header revMajor="1" revMinor="8"/>
  <road name="" length="30.0" id="4" junction="-1">
    <planView><geometry s="0.0" x="0.0" y="0.0" hdg="0.0" length="30.0"><line/></geometry></planView>
    <railroad>
    </railroad>
  </road>
</OpenDRIVE>)";
    pugi::xml_document doc = ParseDoc(xml);
    EXPECT_TRUE(BuildSideModel(doc, Key(3)));

    const OdrSideModel* m = GetSideModel(Key(3));
    ASSERT_NE(m, nullptr);
    EXPECT_TRUE(m->rail_switches.empty()) << "empty <railroad/> must store no switch";

    // Whitelist proof: the empty container itself must not raise a coverage entry.
    EXPECT_EQ(m->audit.unsupported_elements, 0u);
    EXPECT_EQ(m->audit.unsupported_attributes, 0u);
    EXPECT_TRUE(m->audit.entries.empty()) << (m->audit.entries.empty() ? "" : m->audit.entries.front());
}

// 2. Inline XML: a <station> with 1 platform / 2 segments round-trips; GetStation miss -> nullptr.
TEST(OdrRailroad, InlineStationRoundTripAndMiss)
{
    const char* xml = R"(<OpenDRIVE>
  <header revMajor="1" revMinor="8"/>
  <road name="" length="60.0" id="2" junction="-1">
    <planView><geometry s="0.0" x="0.0" y="0.0" hdg="0.0" length="60.0"><line/></geometry></planView>
  </road>
  <station name="H12" id="12" type="small">
    <platform name="platform1" id="12-1">
      <segment roadId="2" sStart="16.5" sEnd="51.0" side="right"/>
      <segment roadId="3" sStart="0.0" sEnd="20.0" side="left"/>
    </platform>
  </station>
</OpenDRIVE>)";
    pugi::xml_document doc = ParseDoc(xml);
    EXPECT_TRUE(BuildSideModel(doc, Key(4)));

    const OdrSideModel* m = GetSideModel(Key(4));
    ASSERT_NE(m, nullptr);
    ASSERT_EQ(m->stations.size(), 1u);

    const OdrStation* st = GetStation(Key(4), "12");
    ASSERT_NE(st, nullptr);
    EXPECT_EQ(st->id, "12");
    EXPECT_EQ(st->name, "H12");
    EXPECT_EQ(st->type, "small");
    ASSERT_EQ(st->platforms.size(), 1u);

    const OdrStationPlatform& pf = st->platforms[0];
    EXPECT_EQ(pf.id, "12-1");
    EXPECT_EQ(pf.name, "platform1");
    ASSERT_EQ(pf.segments.size(), 2u);
    EXPECT_EQ(pf.segments[0].road_id, "2");
    EXPECT_DOUBLE_EQ(pf.segments[0].s_start, 16.5);
    EXPECT_DOUBLE_EQ(pf.segments[0].s_end, 51.0);
    EXPECT_EQ(pf.segments[0].side, "right");
    EXPECT_EQ(pf.segments[1].road_id, "3");
    EXPECT_DOUBLE_EQ(pf.segments[1].s_start, 0.0);
    EXPECT_DOUBLE_EQ(pf.segments[1].s_end, 20.0);
    EXPECT_EQ(pf.segments[1].side, "left");

    EXPECT_EQ(GetStation(Key(4), "999"), nullptr) << "unknown station id -> nullptr";
}

// 3. Audit-zero: a doc carrying railroad + switch (all children) + a station (platform + segment)
//    produces ZERO [ODR-UNSUPPORTED] entries (pins the whitelist rows).
TEST(OdrRailroad, RailroadAndStationAuditZero)
{
    const char* xml = R"(<OpenDRIVE>
  <header revMajor="1" revMinor="8"/>
  <road name="" length="54.5" id="1" junction="-1">
    <planView><geometry s="0.0" x="0.0" y="0.0" hdg="0.0" length="54.5"><line/></geometry></planView>
    <railroad>
      <switch name="Switch12" id="12" position="dynamic">
        <mainTrack id="1" s="10.0" dir="+"/>
        <sideTrack id="2" s="0.0" dir="+"/>
        <partner name="Switch32" id="32"/>
      </switch>
    </railroad>
  </road>
  <station name="H12" id="12" type="small">
    <platform name="platform1" id="12-1">
      <segment roadId="1" sStart="16.5" sEnd="51.0" side="right"/>
    </platform>
  </station>
</OpenDRIVE>)";
    pugi::xml_document doc = ParseDoc(xml);
    EXPECT_TRUE(BuildSideModel(doc, Key(5)));

    const OdrSideModel* m = GetSideModel(Key(5));
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->audit.unsupported_elements, 0u);
    EXPECT_EQ(m->audit.unsupported_attributes, 0u);
    EXPECT_EQ(m->audit.removed16_hits, 0u);
    EXPECT_TRUE(m->audit.entries.empty()) << (m->audit.entries.empty() ? "" : m->audit.entries.front());

    // And the data landed.
    EXPECT_EQ(m->rail_switches.size(), 1u);
    EXPECT_EQ(m->stations.size(), 1u);
}

// ===========================================================================
// Official fixtures (gitignored / CI-absent -> guard + GTEST_SKIP).
// ===========================================================================

// 4a. Ex_Railway-Switch: 2 switches (ids 12, 32) cross-referencing each other as partners, with the
//     exact mainTrack/sideTrack values. Empty <railroad/> on other roads stores nothing. Audit zero.
TEST(OdrRailroad, ExRailwaySwitchOfficial)
{
    const std::string root = RepoRoot();
    ASSERT_FALSE(root.empty());
    const std::string path = root + "/GT_esmini/test/odr_fixtures/official/examples/Ex_Railway-Switch/Ex_Railway-Switch.xodr";
    if (!fs::exists(path))
    {
        GTEST_SKIP() << "official Ex_Railway-Switch.xodr absent (gitignored; run odr_fixture_setup.py)";
    }
    ASSERT_TRUE(LoadXodr(path));
    roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();

    const OdrSideModel* m = GetSideModel(odr);
    ASSERT_NE(m, nullptr);
    ASSERT_EQ(m->rail_switches.size(), 2u) << "exactly 2 switches (ids 12, 32); empty railroads store nothing";

    // Switch 12 on road 1: mainTrack id=1 s=10 dir=+, sideTrack id=2 s=0 dir=+, partner Switch32/32.
    const OdrRailSwitch* s12 = GetRailSwitch(odr, "1", "12");
    ASSERT_NE(s12, nullptr);
    EXPECT_EQ(s12->name, "Switch12");
    EXPECT_EQ(s12->position, "dynamic");
    ASSERT_TRUE(s12->has_main_track);
    EXPECT_EQ(s12->main_track.id, "1");
    EXPECT_NEAR(s12->main_track.s, 10.0, 1e-6);
    EXPECT_EQ(s12->main_track.dir, "+");
    ASSERT_TRUE(s12->has_side_track);
    EXPECT_EQ(s12->side_track.id, "2");
    EXPECT_NEAR(s12->side_track.s, 0.0, 1e-6);
    EXPECT_EQ(s12->side_track.dir, "+");
    ASSERT_TRUE(s12->has_partner);
    EXPECT_EQ(s12->partner_name, "Switch32");
    EXPECT_EQ(s12->partner_id, "32");

    // Switch 32 on road 3: mainTrack id=3 s=10 dir=+, sideTrack id=2 s=34.898... dir=-, partner Switch12/12.
    const OdrRailSwitch* s32 = GetRailSwitch(odr, "3", "32");
    ASSERT_NE(s32, nullptr);
    EXPECT_EQ(s32->name, "Switch32");
    ASSERT_TRUE(s32->has_main_track);
    EXPECT_EQ(s32->main_track.id, "3");
    EXPECT_NEAR(s32->main_track.s, 10.0, 1e-6);
    EXPECT_EQ(s32->main_track.dir, "+");
    ASSERT_TRUE(s32->has_side_track);
    EXPECT_EQ(s32->side_track.id, "2");
    EXPECT_NEAR(s32->side_track.s, 34.898261533109149, 1e-6);
    EXPECT_EQ(s32->side_track.dir, "-");
    ASSERT_TRUE(s32->has_partner);
    EXPECT_EQ(s32->partner_name, "Switch12");
    EXPECT_EQ(s32->partner_id, "12");

    // Strict audit-zero (manifest expected_unsupported_entries: []).
    EXPECT_EQ(m->audit.unsupported_elements, 0u) << (m->audit.entries.empty() ? "" : m->audit.entries.front());
    EXPECT_EQ(m->audit.unsupported_attributes, 0u);

    odr->Clear();
}

// 4b. Ex_Railway-Station: 2 root stations ("12"/"13"), each 1 platform / 1 segment (roadId 2 / 3,
//     sStart=16.5 sEnd=51.0 side=right). All roads carry empty <railroad/> (no switches). Audit zero
//     for the railroad/station NAMESPACE (whole-file zero is P9b: road/type@country leftover, below).
TEST(OdrRailroad, ExRailwayStationOfficial)
{
    const std::string root = RepoRoot();
    ASSERT_FALSE(root.empty());
    const std::string path = root + "/GT_esmini/test/odr_fixtures/official/examples/Ex_Railway-Station/Ex_Railway-Station.xodr";
    if (!fs::exists(path))
    {
        GTEST_SKIP() << "official Ex_Railway-Station.xodr absent (gitignored; run odr_fixture_setup.py)";
    }
    ASSERT_TRUE(LoadXodr(path));
    roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();

    const OdrSideModel* m = GetSideModel(odr);
    ASSERT_NE(m, nullptr);
    ASSERT_EQ(m->stations.size(), 2u);
    EXPECT_TRUE(m->rail_switches.empty()) << "all roads carry an empty <railroad/> -> no switches";

    const OdrStation* h12 = GetStation(odr, "12");
    ASSERT_NE(h12, nullptr);
    EXPECT_EQ(h12->name, "H12");
    EXPECT_EQ(h12->type, "small");
    ASSERT_EQ(h12->platforms.size(), 1u);
    ASSERT_EQ(h12->platforms[0].segments.size(), 1u);
    EXPECT_EQ(h12->platforms[0].segments[0].road_id, "2");
    EXPECT_NEAR(h12->platforms[0].segments[0].s_start, 16.5, 1e-6);
    EXPECT_NEAR(h12->platforms[0].segments[0].s_end, 51.0, 1e-6);
    EXPECT_EQ(h12->platforms[0].segments[0].side, "right");

    const OdrStation* h13 = GetStation(odr, "13");
    ASSERT_NE(h13, nullptr);
    EXPECT_EQ(h13->name, "H13");
    ASSERT_EQ(h13->platforms.size(), 1u);
    ASSERT_EQ(h13->platforms[0].segments.size(), 1u);
    EXPECT_EQ(h13->platforms[0].segments[0].road_id, "3");
    EXPECT_NEAR(h13->platforms[0].segments[0].s_start, 16.5, 1e-6);
    EXPECT_NEAR(h13->platforms[0].segments[0].s_end, 51.0, 1e-6);
    EXPECT_EQ(h13->platforms[0].segments[0].side, "right");

    // NAMESPACE-scoped audit-zero: P9a's acceptance is a clean railroad/station namespace ONLY;
    // whole-file zero-audit is the P9b final sweep. This fixture carries ONE known out-of-namespace
    // leftover -- road/type@country on road 4 (stored "road/type@country|ctx=4"; cluster ownership:
    // road/type, NOT cluster 20) -- deliberately NOT whitelisted here, deferred to P9b (the manifest
    // pins it exactly via expected_unsupported_entries). Assert only that no audit entry touches the
    // railroad/station namespace.
    for (const std::string& e : m->audit.entries)
    {
        EXPECT_EQ(e.find("railroad"), std::string::npos) << "railroad-namespace audit entry: " << e;
        EXPECT_NE(e.rfind("station", 0), 0u) << "station-namespace audit entry: " << e;
    }

    odr->Clear();
}
