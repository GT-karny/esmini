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
