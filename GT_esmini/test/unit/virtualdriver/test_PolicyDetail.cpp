#include <gtest/gtest.h>

#include "gt_esmini/control/virtualdriver/PolicyDetail.hpp"
#include "gt_esmini/control/virtualdriver/policies/AebSafety.hpp"

#include <cmath>
#include <string>

using namespace gt_esmini;

// ============================================================================
// W3 (capability_model §2.2a): AEB's TTC / required-decel used to die as locals
// inside AebSafety::Evaluate(), so "why did it fire / why didn't it" was not
// answerable from outside. These pin
//   (a) the collision-course gate as a pure, testable function, and
//   (b) the policy-diagnostics KV channel plus its key-naming convention
//       (§2.2 (a') — the same string KVs are forwarded verbatim to OSI
//       HostVehicleData custom_detail by W1).
//
// Convention (user decision, 2026-07-20):
//   key   = gt.<function>.<quantity>_<SI unit suffix>   e.g. gt.aeb.ttc_s
//           gt.dbg.* is debug-only and excluded from verdict trust
//   value = fixed 3-decimal decimal string ("%.3f"), or "true"/"false"
// ============================================================================

// ───────────────────── the collision-course gate (pure) ─────────────────────

TEST(AebGate, ComputesTtcAndRequiredDecelFromGapAndClosingSpeed)
{
    // 20 m gap closing at 10 m/s -> TTC = 2 s, a_req = 100/(2*20) = 2.5 m/s^2.
    const aeb::GateResult r = aeb::EvaluateGate(AebSafetyConfig{}, 20.0, 10.0);

    EXPECT_NEAR(r.ttc, 2.0, 1e-9);
    EXPECT_NEAR(r.a_req, 2.5, 1e-9);
    EXPECT_TRUE(r.valid);
}

TEST(AebGate, FiresOnlyWhenBothTtcAndRequiredDecelThresholdsAreCrossed)
{
    AebSafetyConfig cfg;  // ttc_threshold = 2.5 s, min_a_req = 3.0 m/s^2

    // Urgent: 10 m gap closing at 10 m/s -> TTC 1.0 s, a_req 5.0 m/s^2.
    EXPECT_TRUE(aeb::EvaluateGate(cfg, 10.0, 10.0).triggered);

    // Soft following: TTC 2.0 s but a_req only 2.5 m/s^2 -> comfort tier's job.
    EXPECT_FALSE(aeb::EvaluateGate(cfg, 20.0, 10.0).triggered);

    // Far away: a_req high enough only because the gap is tiny is not the case
    // here — 100 m gap closing at 10 m/s is TTC 10 s, well outside the window.
    EXPECT_FALSE(aeb::EvaluateGate(cfg, 100.0, 10.0).triggered);
}

TEST(AebGate, NotClosingOrOverlappingYieldsNoGateMath)
{
    AebSafetyConfig cfg;

    EXPECT_FALSE(aeb::EvaluateGate(cfg, 10.0, 0.0).valid);   // not closing
    EXPECT_FALSE(aeb::EvaluateGate(cfg, 10.0, -5.0).valid);  // opening
    EXPECT_FALSE(aeb::EvaluateGate(cfg, 0.0, 10.0).valid);   // already overlapping
    EXPECT_FALSE(aeb::EvaluateGate(cfg, 10.0, 0.0).triggered);
}

// ───────────────────── the diagnostics KV channel ─────────────────────

TEST(PolicyDetail, FormatsNumbersAsFixedThreeDecimalStrings)
{
    PolicyDetail d;
    AddDetail(d, "gt.aeb.ttc_s", 1.8419);

    ASSERT_EQ(d.size(), 1u);
    EXPECT_EQ(d[0].first, "gt.aeb.ttc_s");
    EXPECT_EQ(d[0].second, "1.842");
}

TEST(PolicyDetail, FormatsBooleansAsTrueFalse)
{
    PolicyDetail d;
    AddDetail(d, "gt.aeb.triggered", true);
    AddDetail(d, "gt.aeb.in_path", false);

    ASSERT_EQ(d.size(), 2u);
    EXPECT_EQ(d[0].second, "true");
    EXPECT_EQ(d[1].second, "false");
}

TEST(PolicyDetail, GateDiagnosticsUseTheGtAebKeyNamespaceWithUnitSuffixes)
{
    PolicyDetail d;
    aeb::AppendGateDetail(d, aeb::EvaluateGate(AebSafetyConfig{}, 10.0, 10.0));

    auto find = [&d](const char* key) -> const std::string* {
        for (const auto& kv : d)
            if (kv.first == key) return &kv.second;
        return nullptr;
    };

    const std::string* ttc   = find("gt.aeb.ttc_s");
    const std::string* a_req = find("gt.aeb.a_req_mps2");
    ASSERT_NE(ttc, nullptr);
    ASSERT_NE(a_req, nullptr);
    EXPECT_EQ(*ttc, "1.000");
    EXPECT_EQ(*a_req, "5.000");
    ASSERT_NE(find("gt.aeb.triggered"), nullptr);
    EXPECT_EQ(*find("gt.aeb.triggered"), "true");
}
