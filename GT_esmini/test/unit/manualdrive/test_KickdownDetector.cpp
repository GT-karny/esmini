// req-vd-ad:REQ-AD-025 (step d) / req-vd-ad:REQ-AD-030 (step b) / vd-func:FUNC-075
//
// Unit tests for KickdownDetector. See the header for why AEB suppression
// and MSL release share this ONE instance rather than each testing the
// pedal against its own threshold: two thresholds would eventually
// disagree, producing an externally-unexplainable state.

#include <gtest/gtest.h>

#include "gt_esmini/control/manualdrive/KickdownDetector.hpp"

namespace gt_esmini
{
namespace
{

KickdownDetectorConfig MakeDefaultCfg()
{
    return KickdownDetectorConfig{};  // engage=0.95 / release=0.80
}

}  // namespace

TEST(KickdownDetectorTest, StaysInactiveBelowEngageThreshold)
{
    KickdownDetector d(MakeDefaultCfg());
    EXPECT_FALSE(d.Update(0.0));
    EXPECT_FALSE(d.Update(0.5));
    EXPECT_FALSE(d.Update(0.94));  // just under engage_threshold
    EXPECT_FALSE(d.IsActive());
}

TEST(KickdownDetectorTest, EngagesAtExactlyTheEngageThreshold)
{
    KickdownDetector d(MakeDefaultCfg());
    EXPECT_TRUE(d.Update(0.95));  // >= engage_threshold, boundary is inclusive
    EXPECT_TRUE(d.IsActive());
}

TEST(KickdownDetectorTest, HoldsInsideHysteresisBandOnceEngaged)
{
    KickdownDetector d(MakeDefaultCfg());
    ASSERT_TRUE(d.Update(1.0));  // engage at full pedal
    // 0.85 sits inside [release=0.80, engage=0.95): must HOLD, not release.
    // This is the entire point of the band -- without it, a pedal resting
    // near the threshold would toggle AEB suppression every frame.
    EXPECT_TRUE(d.Update(0.85));
    EXPECT_TRUE(d.IsActive());
}

TEST(KickdownDetectorTest, ReleasesBelowReleaseThreshold)
{
    KickdownDetector d(MakeDefaultCfg());
    ASSERT_TRUE(d.Update(1.0));
    EXPECT_FALSE(d.Update(0.79));  // strictly below release_threshold
    EXPECT_FALSE(d.IsActive());
}

TEST(KickdownDetectorTest, InvertedBandIsCoercedAndCannotChatter)
{
    KickdownDetectorConfig cfg;
    cfg.engage_threshold  = 0.5;
    cfg.release_threshold = 0.9;  // inverted: release >= engage
    KickdownDetector d(cfg);

    // The constructor must clamp release down to engage -- a band that
    // cannot chatter because both edges coincide.
    EXPECT_EQ(d.Config().release_threshold, d.Config().engage_threshold);

    // Feed the exact boundary value repeatedly. With a genuinely inverted
    // (uncoerced) band, a value sitting between the two raw thresholds is
    // undefined/oscillation-prone; with the coerced coincident band the
    // verdict must be identical on every call.
    const bool first = d.Update(0.5);
    for (int i = 0; i < 10; ++i)
    {
        EXPECT_EQ(d.Update(0.5), first) << "toggled on repeated call " << i;
    }
}

TEST(KickdownDetectorTest, ResetClearsTheLatch)
{
    KickdownDetector d(MakeDefaultCfg());
    ASSERT_TRUE(d.Update(1.0));
    ASSERT_TRUE(d.IsActive());
    d.Reset();
    EXPECT_FALSE(d.IsActive());
}

TEST(KickdownDetectorTest, ConfigReportsEffectiveCoercedThresholds)
{
    KickdownDetectorConfig cfg;
    cfg.engage_threshold  = 0.6;
    cfg.release_threshold = 0.8;  // inverted vs engage_threshold
    KickdownDetector d(cfg);

    // Config() must describe what the detector actually USES (post-coercion),
    // not the raw JSON/caller-supplied values -- callers reporting thresholds
    // into custom_detail need the effective numbers.
    EXPECT_EQ(d.Config().engage_threshold, 0.6);
    EXPECT_LE(d.Config().release_threshold, d.Config().engage_threshold);
}

}  // namespace gt_esmini
