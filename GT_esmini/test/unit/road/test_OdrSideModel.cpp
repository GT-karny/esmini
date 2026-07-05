// test_OdrSideModel.cpp -- unit tests for the GT OpenDRIVE side model (plan P1).
//
// Fast, mostly file-free: tiny xodr documents are built from inline raw strings via pugixml and
// BuildSideModel() is called directly on the parsed document (no RoadManager fork hook needed).
// One test parses a real repo control file (via GT_ODR_REPO_ROOT) as the P1 acceptance miniature.
#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "gt_esmini/road/OdrSideModel.hpp"
#include "pugixml.hpp"

using namespace gt_esmini::odr;

namespace
{

// Parse a raw xodr string into a pugi document (asserts well-formedness).
pugi::xml_document ParseDoc(const char* xml)
{
    pugi::xml_document doc;
    pugi::xml_parse_result res = doc.load_string(xml);
    EXPECT_TRUE(res) << "XML parse error: " << res.description();
    return doc;
}

bool HasEntry(const OdrSideModel& m, const std::string& stored)
{
    return std::find(m.audit.entries.begin(), m.audit.entries.end(), stored) != m.audit.entries.end();
}

// Distinct dummy keys per test so the registry never cross-contaminates.
const void* Key(int tag)
{
    // Stable, distinct addresses.
    static char slots[64];
    return &slots[tag & 63];
}

// A minimal, clean 1.4-style road body (header omitted so callers can set the version).
const char* kCleanRoadBody = R"(
  <road name="r" length="100.0" id="1" junction="-1">
    <link/>
    <planView>
      <geometry s="0.0" x="0.0" y="0.0" hdg="0.0" length="100.0">
        <line/>
      </geometry>
    </planView>
    <lanes>
      <laneSection s="0.0">
        <left>
          <lane id="1" type="driving" level="false">
            <width sOffset="0.0" a="3.5" b="0.0" c="0.0" d="0.0"/>
            <roadMark sOffset="0.0" type="solid" weight="standard" color="standard" width="0.15"/>
          </lane>
        </left>
        <center>
          <lane id="0" type="driving" level="false">
            <roadMark sOffset="0.0" type="broken" weight="standard" color="standard" width="0.15"/>
          </lane>
        </center>
        <right>
          <lane id="-1" type="driving" level="false">
            <width sOffset="0.0" a="3.5" b="0.0" c="0.0" d="0.0"/>
            <roadMark sOffset="0.0" type="solid" weight="standard" color="standard" width="0.15"/>
          </lane>
        </right>
      </laneSection>
    </lanes>
  </road>
)";

std::string CleanDoc14()
{
    return std::string("<OpenDRIVE>\n"
                       "  <header revMajor=\"1\" revMinor=\"4\" name=\"clean\" version=\"1.00\"/>\n") +
           kCleanRoadBody + "</OpenDRIVE>\n";
}

}  // namespace

// 1. Clean 1.4-style doc -> zero unsupported entries.
TEST(OdrSideModel, CleanDocHasNoUnsupportedEntries)
{
    pugi::xml_document doc = ParseDoc(CleanDoc14().c_str());
    EXPECT_TRUE(BuildSideModel(doc, Key(1)));

    const OdrSideModel* m = GetSideModel(Key(1));
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->audit.unsupported_elements, 0u);
    EXPECT_EQ(m->audit.unsupported_attributes, 0u);
    EXPECT_EQ(m->audit.removed16_hits, 0u);
    EXPECT_TRUE(m->audit.entries.empty()) << (m->audit.entries.empty() ? "" : m->audit.entries.front());
    EXPECT_EQ(m->rev_major, 1);
    EXPECT_EQ(m->rev_minor, 4);
}

// 2. Bogus element + bogus attr -> exactly 2 entries; topmost-only (bogus child NOT reported).
TEST(OdrSideModel, BogusElementAndAttrTopmostOnly)
{
    const char* xml = R"(<OpenDRIVE>
  <header revMajor="1" revMinor="4"/>
  <road name="r" length="100.0" id="7" junction="-1" bogusAttr="x">
    <planView>
      <geometry s="0.0" x="0.0" y="0.0" hdg="0.0" length="100.0"><line/></geometry>
    </planView>
    <bogusChild foo="1">
      <deeperBogus bar="2"/>
    </bogusChild>
  </road>
</OpenDRIVE>)";
    pugi::xml_document doc = ParseDoc(xml);
    EXPECT_TRUE(BuildSideModel(doc, Key(2)));

    const OdrSideModel* m = GetSideModel(Key(2));
    ASSERT_NE(m, nullptr);

    // Exactly two entries: the bogus attr on <road> and the topmost bogus element.
    EXPECT_EQ(m->audit.entries.size(), 2u);
    EXPECT_EQ(m->audit.unsupported_elements, 1u);
    EXPECT_EQ(m->audit.unsupported_attributes, 1u);
    EXPECT_TRUE(HasEntry(*m, "road@bogusAttr|ctx=7"));
    EXPECT_TRUE(HasEntry(*m, "road/bogusChild|ctx=7"));
    // Topmost-only: the deeper bogus element and its attr must NOT appear.
    EXPECT_FALSE(HasEntry(*m, "road/bogusChild/deeperBogus|ctx=7"));
    EXPECT_FALSE(HasEntry(*m, "road/bogusChild@foo|ctx=7"));
    EXPECT_FALSE(HasEntry(*m, "road/bogusChild/deeperBogus@bar|ctx=7"));
}

// 3. userData / dataQuality anywhere -> stored (count + owner_path), never in entries, not descended.
TEST(OdrSideModel, UserDataAndDataQualityStoredNotAudited)
{
    const char* xml = R"(<OpenDRIVE>
  <header revMajor="1" revMinor="5">
    <userData code="hdr" value="h"><customThing weird="1"/></userData>
  </header>
  <road name="r" length="100.0" id="3" junction="-1">
    <planView>
      <geometry s="0.0" x="0.0" y="0.0" hdg="0.0" length="100.0"><line/></geometry>
    </planView>
    <dataQuality><rawData postProcessing="raw"/></dataQuality>
    <userData code="road" value="r"/>
  </road>
</OpenDRIVE>)";
    pugi::xml_document doc = ParseDoc(xml);
    EXPECT_TRUE(BuildSideModel(doc, Key(3)));

    const OdrSideModel* m = GetSideModel(Key(3));
    ASSERT_NE(m, nullptr);

    // Two userData (header + road), one dataQuality (road).
    EXPECT_EQ(m->user_data.size(), 2u);
    EXPECT_EQ(m->data_quality.size(), 1u);

    // Owner paths + context recorded correctly.
    bool found_hdr = false, found_road = false;
    for (const auto& u : m->user_data)
    {
        if (u.owner_path == "header")
        {
            found_hdr = true;
            EXPECT_EQ(u.context_id, "");  // header is not under a road/junction
        }
        if (u.owner_path == "road")
        {
            found_road = true;
            EXPECT_EQ(u.context_id, "3");
        }
    }
    EXPECT_TRUE(found_hdr);
    EXPECT_TRUE(found_road);
    EXPECT_EQ(m->data_quality.front().owner_path, "road");
    EXPECT_EQ(m->data_quality.front().context_id, "3");

    // The custom children INSIDE userData/dataQuality must NOT have been audited.
    EXPECT_EQ(m->audit.unsupported_elements, 0u);
    EXPECT_EQ(m->audit.unsupported_attributes, 0u);
    EXPECT_TRUE(m->audit.entries.empty());
    // Stored XML captured the element.
    EXPECT_NE(m->user_data.front().xml.find("userData"), std::string::npos);
}

// 4. include -> BuildSideModel returns false.
// PERMANENT SPEC (P9a, plan sec 10-6): <include> is a hard parse error by permanent design decision,
// NOT a deferred TODO -- zero known real-world usage; resolution would need file IO/recursion/security
// design with no consumer. BuildSideModel returning false here IS the specified correct behavior. The
// exact [ODR-INCLUDE] diagnostic text is asserted at the conformance-harness level (fixture
// 16_include_error_15, expected_diagnostics), not in this gtest.
TEST(OdrSideModel, IncludeIsHardError)
{
    const char* xml = R"(<OpenDRIVE>
  <header revMajor="1" revMinor="5"/>
  <road name="r" length="100.0" id="1" junction="-1">
    <planView>
      <geometry s="0.0" x="0.0" y="0.0" hdg="0.0" length="100.0"><line/></geometry>
    </planView>
    <include file="does_not_exist.xml"/>
  </road>
</OpenDRIVE>)";
    pugi::xml_document doc = ParseDoc(xml);
    EXPECT_FALSE(BuildSideModel(doc, Key(4)));
    // Model is still registered so stats are queryable.
    EXPECT_NE(GetSideModel(Key(4)), nullptr);
}

// 5. Version fields; revMinor missing -> -1.
TEST(OdrSideModel, VersionFields)
{
    {
        const char* xml = R"(<OpenDRIVE><header revMajor="1" revMinor="7" name="n" version="1.7"/></OpenDRIVE>)";
        pugi::xml_document doc = ParseDoc(xml);
        EXPECT_TRUE(BuildSideModel(doc, Key(50)));
        const OdrSideModel* m = GetSideModel(Key(50));
        ASSERT_NE(m, nullptr);
        EXPECT_EQ(m->rev_major, 1);
        EXPECT_EQ(m->rev_minor, 7);
        EXPECT_EQ(m->header_name, "n");
        EXPECT_EQ(m->header_version, "1.7");
    }
    {
        // revMinor absent -> -1.
        const char* xml = R"(<OpenDRIVE><header revMajor="1"/></OpenDRIVE>)";
        pugi::xml_document doc = ParseDoc(xml);
        EXPECT_TRUE(BuildSideModel(doc, Key(51)));
        const OdrSideModel* m = GetSideModel(Key(51));
        ASSERT_NE(m, nullptr);
        EXPECT_EQ(m->rev_major, 1);
        EXPECT_EQ(m->rev_minor, -1);
    }
}

// 6. Dedupe: same bogus element on two roads -> 2 entries (different ctx); twice on SAME road -> 1.
TEST(OdrSideModel, DedupePerContext)
{
    // Same bogus element on two different roads -> two entries (ctx differs).
    {
        const char* xml = R"(<OpenDRIVE>
  <header revMajor="1" revMinor="4"/>
  <road name="a" length="10.0" id="10" junction="-1"><bogusChild/></road>
  <road name="b" length="10.0" id="20" junction="-1"><bogusChild/></road>
</OpenDRIVE>)";
        pugi::xml_document doc = ParseDoc(xml);
        EXPECT_TRUE(BuildSideModel(doc, Key(60)));
        const OdrSideModel* m = GetSideModel(Key(60));
        ASSERT_NE(m, nullptr);
        EXPECT_EQ(m->audit.unsupported_elements, 2u);
        EXPECT_TRUE(HasEntry(*m, "road/bogusChild|ctx=10"));
        EXPECT_TRUE(HasEntry(*m, "road/bogusChild|ctx=20"));
    }
    // Same bogus element twice on the SAME road -> one entry.
    {
        const char* xml = R"(<OpenDRIVE>
  <header revMajor="1" revMinor="4"/>
  <road name="a" length="10.0" id="10" junction="-1"><bogusChild/><bogusChild/></road>
</OpenDRIVE>)";
        pugi::xml_document doc = ParseDoc(xml);
        EXPECT_TRUE(BuildSideModel(doc, Key(61)));
        const OdrSideModel* m = GetSideModel(Key(61));
        ASSERT_NE(m, nullptr);
        EXPECT_EQ(m->audit.unsupported_elements, 1u);
        EXPECT_EQ(m->audit.entries.size(), 1u);
        EXPECT_TRUE(HasEntry(*m, "road/bogusChild|ctx=10"));
    }
}

// 7. removed-in-1.6: <neighbor> in a revMinor=6 doc -> [ODR-REMOVED-1.6]-classified; same doc
//    revMinor=5 -> generic unsupported.
TEST(OdrSideModel, RemovedIn16Classification)
{
    // road/link/neighbor is the verified 1.6 removal.
    const char* tmpl = R"(<OpenDRIVE>
  <header revMajor="1" revMinor="%d"/>
  <road name="r" length="10.0" id="5" junction="-1">
    <link>
      <neighbor side="left" elementId="9" direction="same"/>
    </link>
  </road>
</OpenDRIVE>)";
    char buf[1024];

    // revMinor=6 -> removed classification.
    snprintf(buf, sizeof(buf), tmpl, 6);
    {
        pugi::xml_document doc = ParseDoc(buf);
        EXPECT_TRUE(BuildSideModel(doc, Key(70)));
        const OdrSideModel* m = GetSideModel(Key(70));
        ASSERT_NE(m, nullptr);
        EXPECT_EQ(m->audit.removed16_hits, 1u);
        EXPECT_EQ(m->audit.unsupported_elements, 0u);
        EXPECT_TRUE(HasEntry(*m, "road/link/neighbor|ctx=5|removed16"));
    }

    // revMinor=5 -> generic unsupported (removal table not applied below 1.6).
    snprintf(buf, sizeof(buf), tmpl, 5);
    {
        pugi::xml_document doc = ParseDoc(buf);
        EXPECT_TRUE(BuildSideModel(doc, Key(71)));
        const OdrSideModel* m = GetSideModel(Key(71));
        ASSERT_NE(m, nullptr);
        EXPECT_EQ(m->audit.removed16_hits, 0u);
        EXPECT_EQ(m->audit.unsupported_elements, 1u);
        EXPECT_TRUE(HasEntry(*m, "road/link/neighbor|ctx=5"));
    }
}

// 8. Re-Build same key -> model replaced (no accumulation).
TEST(OdrSideModel, ReBuildReplacesModel)
{
    const char* dirty = R"(<OpenDRIVE>
  <header revMajor="1" revMinor="4"/>
  <road name="a" length="10.0" id="1" junction="-1"><bogusChild/></road>
</OpenDRIVE>)";
    pugi::xml_document d1 = ParseDoc(dirty);
    EXPECT_TRUE(BuildSideModel(d1, Key(80)));
    EXPECT_EQ(GetSideModel(Key(80))->audit.unsupported_elements, 1u);

    // Re-parse the SAME key with a clean doc -> stats reset, no accumulation.
    pugi::xml_document d2 = ParseDoc(CleanDoc14().c_str());
    EXPECT_TRUE(BuildSideModel(d2, Key(80)));
    const OdrSideModel* m = GetSideModel(Key(80));
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->audit.unsupported_elements, 0u);
    EXPECT_TRUE(m->audit.entries.empty());
}

// ClearSideModel removes the entry.
TEST(OdrSideModel, ClearRemovesEntry)
{
    pugi::xml_document doc = ParseDoc(CleanDoc14().c_str());
    EXPECT_TRUE(BuildSideModel(doc, Key(90)));
    EXPECT_NE(GetSideModel(Key(90)), nullptr);
    ClearSideModel(Key(90));
    EXPECT_EQ(GetSideModel(Key(90)), nullptr);
}

// Real repo control file (P1 acceptance miniature): a 1.4/1.5 file must audit to ZERO unsupported.
// If this fails, the whitelist or walker has a bug (or parser_coverage.yaml is incomplete).
TEST(OdrSideModel, RealControlFileHasNoUnsupportedEntries)
{
#ifdef GT_ODR_REPO_ROOT
    const std::string path = std::string(GT_ODR_REPO_ROOT) + "/resources/xodr/straight_500m.xodr";
    pugi::xml_document doc;
    pugi::xml_parse_result res = doc.load_file(path.c_str());
    ASSERT_TRUE(res) << "could not load " << path << ": " << res.description();

    EXPECT_TRUE(BuildSideModel(doc, Key(100)));
    const OdrSideModel* m = GetSideModel(Key(100));
    ASSERT_NE(m, nullptr);

    // Dump any offenders to make failures actionable.
    std::string dump;
    for (const auto& e : m->audit.entries)
    {
        dump += "\n  " + e;
    }
    EXPECT_EQ(m->audit.unsupported_elements, 0u) << "unexpected unsupported elements:" << dump;
    EXPECT_EQ(m->audit.unsupported_attributes, 0u) << "unexpected unsupported attributes:" << dump;
    EXPECT_EQ(m->audit.removed16_hits, 0u) << "unexpected removed-1.6 hits:" << dump;
    EXPECT_TRUE(m->audit.entries.empty()) << "control file produced audit entries:" << dump;
#else
    GTEST_SKIP() << "GT_ODR_REPO_ROOT not defined";
#endif
}
