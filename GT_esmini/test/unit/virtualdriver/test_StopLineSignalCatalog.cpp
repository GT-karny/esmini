#include <gtest/gtest.h>

#include "gt_esmini/control/virtualdriver/policies/StopLineSignalCatalog.hpp"

#include "CommonMini.hpp"

#include <string>
#include <unordered_map>

using namespace gt_esmini;

// ────────────── StopLineSignalCatalog: ClassifyStopLineType (pure) ──────────────
// Key convention matches LoadSignalsByCountry; catalog is caller-supplied so
// these tests hand-roll one instead of touching resources/.

namespace
{
std::unordered_map<std::string, StopLineKind> SampleCatalog()
{
    return {
        {"opendrive294",   StopLineKind::STOP_LINE},
        {"de294",          StopLineKind::STOP_LINE},
        {"opendrive294.1", StopLineKind::STOP_LINE},
    };
}
}  // namespace

TEST(StopLineClassification, KnownTypeClassifiesAsStopLine)
{
    const auto catalog = SampleCatalog();
    EXPECT_EQ(ClassifyStopLineType(catalog, "opendrive", "294", ""), StopLineKind::STOP_LINE);
}

TEST(StopLineClassification, UnknownTypeIsNone)
{
    const auto catalog = SampleCatalog();
    EXPECT_EQ(ClassifyStopLineType(catalog, "opendrive", "999", ""), StopLineKind::NONE);
}

TEST(StopLineClassification, DifferentCountrySameTypeIsNone)
{
    const auto catalog = SampleCatalog();
    EXPECT_EQ(ClassifyStopLineType(catalog, "jp", "294", ""), StopLineKind::NONE);
}

TEST(StopLineClassification, SubtypeAppendsToKeyWhenPresent)
{
    const auto catalog = SampleCatalog();
    EXPECT_EQ(ClassifyStopLineType(catalog, "opendrive", "294", "1"), StopLineKind::STOP_LINE);
    EXPECT_EQ(ClassifyStopLineType(catalog, "opendrive", "294", "2"), StopLineKind::NONE);
}

TEST(StopLineClassification, SubtypeNoneAndMinusOneAreTreatedAsAbsent)
{
    // "none"/"-1" subtype = no subtype, matching upstream's key rule
    const auto catalog = SampleCatalog();
    EXPECT_EQ(ClassifyStopLineType(catalog, "opendrive", "294", "none"), StopLineKind::STOP_LINE);
    EXPECT_EQ(ClassifyStopLineType(catalog, "opendrive", "294", "-1"), StopLineKind::STOP_LINE);
}

TEST(StopLineClassification, EmptyCatalogIsNone)
{
    const std::unordered_map<std::string, StopLineKind> empty_catalog;
    EXPECT_EQ(ClassifyStopLineType(empty_catalog, "opendrive", "294", ""), StopLineKind::NONE);
}

TEST(StopLineClassification, EmptyOrPlaceholderTypeIsSafelyNone)
{
    // "-1"/"none"/"" all mean "no type", mirroring upstream's guard
    const auto catalog = SampleCatalog();
    EXPECT_EQ(ClassifyStopLineType(catalog, "opendrive", "", ""), StopLineKind::NONE);
    EXPECT_EQ(ClassifyStopLineType(catalog, "opendrive", "-1", ""), StopLineKind::NONE);
    EXPECT_EQ(ClassifyStopLineType(catalog, "opendrive", "none", ""), StopLineKind::NONE);
}

TEST(StopLineClassification, EmptyCountryDoesNotMatchPrefixedEntries)
{
    const auto catalog = SampleCatalog();
    EXPECT_EQ(ClassifyStopLineType(catalog, "", "294", ""), StopLineKind::NONE);
}

// ────────────── StopLineSignalCatalog: LoadStopLineCatalog (file IO) ──────────────
// SE_Env path registration avoids depending on the ctest working directory.
TEST(StopLineCatalogLoad, ReadsRealOpendriveStopLineFile)
{
#ifdef GT_ODR_REPO_ROOT
    SE_Env::Inst().AddPath(std::string(GT_ODR_REPO_ROOT) + "/resources");

    ASSERT_TRUE(LoadStopLineCatalog("opendrive"));
    EXPECT_EQ(ClassifyStopLineType(GetStopLineCatalog(), "opendrive", "294", ""), StopLineKind::STOP_LINE);
#else
    GTEST_SKIP() << "GT_ODR_REPO_ROOT not defined";
#endif
}

// ────────────── StopLineSignalCatalog: country-switch mechanism ──────────────
// "zz" is ISO 3166-1 alpha-2's user-assigned code element ZZ, never allocated to any real
// country (Wikipedia "ISO 3166-1 alpha-2" article, "User-assigned code elements" section:
// "The following alpha-2 codes can be user-assigned: AA, QM to QZ, XA to XZ, and ZZ."), so
// zz_stop_line.txt cannot be mistaken for a real country's catalog. Its type (999999) is
// shaped unlike any real entry in this repo's catalogs (bare 2-3 digit codes such as 294, or
// OpenDRIVE's dotted 1.000.0xx form), so it cannot collide with a future real assignment either.
// "qq" (also inside the reserved QM-QZ range) is used below purely as a country with no catalog
// file, and none is ever added for it.
//
// Every test below re-adds the resources path: SE_Env is a process-wide singleton and gtest may
// reorder tests (--gtest_shuffle), so no test may assume an earlier test already registered it.
// LoadStopLineCatalog memoizes per country, so calling it again after another test already
// loaded (or failed to load) the same country is a cache hit with the same result — safe under
// any ordering.
TEST(StopLineCatalogLoad, ReadsPlaceholderCountryStopLineFile)
{
#ifdef GT_ODR_REPO_ROOT
    SE_Env::Inst().AddPath(std::string(GT_ODR_REPO_ROOT) + "/resources");

    ASSERT_TRUE(LoadStopLineCatalog("zz"));
    EXPECT_EQ(ClassifyStopLineType(GetStopLineCatalog(), "zz", "999999", ""), StopLineKind::STOP_LINE);
#else
    GTEST_SKIP() << "GT_ODR_REPO_ROOT not defined";
#endif
}

TEST(StopLineCatalogLoad, PlaceholderCountryTypeDoesNotLeakToOtherCountries)
{
#ifdef GT_ODR_REPO_ROOT
    SE_Env::Inst().AddPath(std::string(GT_ODR_REPO_ROOT) + "/resources");

    ASSERT_TRUE(LoadStopLineCatalog("zz"));
    ASSERT_TRUE(LoadStopLineCatalog("opendrive"));

    // zz's placeholder type must not classify under opendrive's catalog...
    EXPECT_EQ(ClassifyStopLineType(GetStopLineCatalog(), "opendrive", "999999", ""), StopLineKind::NONE);
    // ...and opendrive's real type must not classify under zz's catalog.
    EXPECT_EQ(ClassifyStopLineType(GetStopLineCatalog(), "zz", "294", ""), StopLineKind::NONE);
#else
    GTEST_SKIP() << "GT_ODR_REPO_ROOT not defined";
#endif
}

TEST(StopLineCatalogLoad, UnprovisionedCountryFailsToLoadAndClassifiesNone)
{
#ifdef GT_ODR_REPO_ROOT
    SE_Env::Inst().AddPath(std::string(GT_ODR_REPO_ROOT) + "/resources");

    // "qq" has no stop_line catalog file anywhere under resources/.
    EXPECT_FALSE(LoadStopLineCatalog("qq"));
    EXPECT_EQ(ClassifyStopLineType(GetStopLineCatalog(), "qq", "999999", ""), StopLineKind::NONE);
    EXPECT_EQ(ClassifyStopLineType(GetStopLineCatalog(), "qq", "294", ""), StopLineKind::NONE);
#else
    GTEST_SKIP() << "GT_ODR_REPO_ROOT not defined";
#endif
}

TEST(StopLineCatalogLoad, MultipleLoadedCountriesStayIndependent)
{
#ifdef GT_ODR_REPO_ROOT
    SE_Env::Inst().AddPath(std::string(GT_ODR_REPO_ROOT) + "/resources");

    ASSERT_TRUE(LoadStopLineCatalog("opendrive"));
    ASSERT_TRUE(LoadStopLineCatalog("zz"));

    // Both countries' own entries still classify correctly once both share the process-wide
    // cache — loading a second country must not clobber the first's entries.
    EXPECT_EQ(ClassifyStopLineType(GetStopLineCatalog(), "opendrive", "294", ""), StopLineKind::STOP_LINE);
    EXPECT_EQ(ClassifyStopLineType(GetStopLineCatalog(), "zz", "999999", ""), StopLineKind::STOP_LINE);
#else
    GTEST_SKIP() << "GT_ODR_REPO_ROOT not defined";
#endif
}
