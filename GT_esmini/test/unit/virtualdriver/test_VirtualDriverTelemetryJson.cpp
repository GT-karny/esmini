#include <gtest/gtest.h>

#include "gt_esmini/control/virtualdriver/VirtualDriverTelemetryJson.hpp"

#include <string>

using namespace gt_esmini;

// ============================================================================
// The VirtualDriver telemetry JSON contract (GT_GetVirtualDriverTelemetry).
//
// W2 (capability_model §2.2a): PolicyConstraint carries an arbitration `tier`
// (COMFORT/COURTESY/COMPLIANCE/SAFETY) that the serializer used to drop, so the
// outcome of tier arbitration — "did AEB win as the SAFETY layer?" — was not
// observable from outside the process. These tests pin the tier field into the
// contract.
// ============================================================================

namespace
{
bool Contains(const std::string& hay, const std::string& needle)
{
    return hay.find(needle) != std::string::npos;
}

PolicyConstraint MakeConstraint(PolicyConstraint::Kind kind, const char* source, PolicyConstraint::Tier tier)
{
    PolicyConstraint c;
    c.kind   = kind;
    c.s      = 12.5;
    c.value  = 0.0;
    c.source = source;
    c.tier   = tier;
    return c;
}
}  // namespace

TEST(VirtualDriverTelemetryJson, PolicyConstraintCarriesArbitrationTier)
{
    VirtualDriverTelemetry t;
    t.policy.valid = true;
    t.policy.constraints.push_back(MakeConstraint(PolicyConstraint::Kind::STOP_AT_S, "aeb", PolicyConstraint::Tier::SAFETY));

    const std::string json = ToJson(t);

    EXPECT_TRUE(Contains(json, "\"tier\":\"safety\"")) << json;
    // The pre-existing fields must survive unchanged (frontend/matcher contract).
    EXPECT_TRUE(Contains(json, "\"kind\":\"stop_at_s\"")) << json;
    EXPECT_TRUE(Contains(json, "\"source\":\"aeb\"")) << json;
}

TEST(VirtualDriverTelemetryJson, DefaultTierSerializesAsComfort)
{
    VirtualDriverTelemetry t;
    t.policy.valid = true;
    // Every non-AEB policy leaves tier at its default.
    t.policy.constraints.push_back(MakeConstraint(PolicyConstraint::Kind::MAX_SPEED, "lead_vehicle", PolicyConstraint::Tier::COMFORT));

    EXPECT_TRUE(Contains(ToJson(t), "\"tier\":\"comfort\"")) << ToJson(t);
}

// W3: the policy-diagnostics KV channel must reach the JSON contract, otherwise
// "why did AEB fire / why didn't it" stays invisible outside the process.
TEST(VirtualDriverTelemetryJson, PolicyDiagnosticsDetailIsSerializedAsAnObject)
{
    VirtualDriverTelemetry t;
    t.policy.valid = true;
    t.policy.detail.emplace_back("gt.aeb.ttc_s", "1.842");
    t.policy.detail.emplace_back("gt.aeb.a_req_mps2", "4.310");

    const std::string json = ToJson(t);

    EXPECT_TRUE(Contains(json, "\"detail\":{")) << json;
    EXPECT_TRUE(Contains(json, "\"gt.aeb.ttc_s\":\"1.842\"")) << json;
    EXPECT_TRUE(Contains(json, "\"gt.aeb.a_req_mps2\":\"4.310\"")) << json;
}

TEST(VirtualDriverTelemetryJson, EmptyPolicyDiagnosticsSerializeAsAnEmptyObject)
{
    VirtualDriverTelemetry t;
    EXPECT_TRUE(Contains(ToJson(t), "\"detail\":{}")) << ToJson(t);
}

TEST(VirtualDriverTelemetryJson, EveryTierHasADistinctName)
{
    const struct
    {
        PolicyConstraint::Tier tier;
        const char*            name;
    } cases[] = {
        {PolicyConstraint::Tier::COMFORT, "comfort"},
        {PolicyConstraint::Tier::COURTESY, "courtesy"},
        {PolicyConstraint::Tier::COMPLIANCE, "compliance"},
        {PolicyConstraint::Tier::SAFETY, "safety"},
    };

    for (const auto& c : cases)
    {
        VirtualDriverTelemetry t;
        t.policy.valid = true;
        t.policy.constraints.push_back(MakeConstraint(PolicyConstraint::Kind::YIELD, "test", c.tier));

        EXPECT_TRUE(Contains(ToJson(t), std::string("\"tier\":\"") + c.name + "\"")) << c.name;
    }
}
