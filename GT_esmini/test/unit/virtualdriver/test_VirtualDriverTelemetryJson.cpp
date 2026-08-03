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

// RouteLanePlan (control/virtualdriver/RouteLanePlan.hpp): the ego's per-frame
// road/lane match against the route's lane plan. Pins the full field set into
// the contract -- in particular that target_lanes serializes as a JSON array
// (a dropped loop there would silently ship an always-empty array) and that
// diagnostic/on_target_lane -- the two fields a consumer reads first -- survive.
TEST(VirtualDriverTelemetryJson, RouteLaneSnapshotIsSerialized)
{
    VirtualDriverTelemetry t;
    t.route_lane.valid                  = true;
    t.route_lane.road_id                = 42;
    t.route_lane.ego_lane               = -1;
    t.route_lane.ego_lane_raw           = -2;
    t.route_lane.target_lanes           = {-1, -2};
    t.route_lane.on_target_lane         = false;
    t.route_lane.dist_to_connection     = 12.5;
    t.route_lane.deviation_count        = 3;
    t.route_lane.last_deviation_road_id = 7;
    t.route_lane.rerouted               = true;
    t.route_lane.diagnostic             = "lane_discontinuity";
    t.route_lane.reason                 = "off_plan_road";

    const std::string json = ToJson(t);

    EXPECT_TRUE(Contains(json, "\"route_lane\":{\"valid\":true")) << json;
    EXPECT_TRUE(Contains(json, "\"road_id\":42")) << json;
    EXPECT_TRUE(Contains(json, "\"ego_lane\":-1")) << json;
    EXPECT_TRUE(Contains(json, "\"ego_lane_raw\":-2")) << json;
    EXPECT_TRUE(Contains(json, "\"target_lanes\":[-1,-2]")) << json;
    EXPECT_TRUE(Contains(json, "\"on_target_lane\":false")) << json;
    EXPECT_TRUE(Contains(json, "\"dist_to_connection\":12.5")) << json;
    EXPECT_TRUE(Contains(json, "\"deviation_count\":3")) << json;
    EXPECT_TRUE(Contains(json, "\"last_deviation_road_id\":7")) << json;
    EXPECT_TRUE(Contains(json, "\"rerouted\":true")) << json;
    EXPECT_TRUE(Contains(json, "\"diagnostic\":\"lane_discontinuity\"")) << json;
    EXPECT_TRUE(Contains(json, "\"reason\":\"off_plan_road\"")) << json;
}

TEST(VirtualDriverTelemetryJson, RouteLaneSnapshotDefaultsSerializeEmptyTargetLanesAndDiagnostic)
{
    VirtualDriverTelemetry t;  // route_lane left at its defaults (valid=false, target_lanes={}, diagnostic="")

    const std::string json = ToJson(t);

    EXPECT_TRUE(Contains(json, "\"target_lanes\":[]")) << json;
    EXPECT_TRUE(Contains(json, "\"diagnostic\":\"\"")) << json;
    EXPECT_TRUE(Contains(json, "\"reason\":\"\"")) << json;
}

// The overtake block carries the lead under BOTH ids on purpose: lead_id is the
// scenario entity index the maneuver uses internally, lead_osi_id is the one an
// OSI GroundTruth recording can be joined on (control/common/OsiIdentity.hpp).
// They are different numbers, so a serializer that shipped only one of them
// would leave one of the two consumers unable to name the vehicle at all.
TEST(VirtualDriverTelemetryJson, OvertakeCarriesBothEntityAndOsiLeadId)
{
    VirtualDriverTelemetry t;
    t.overtake.lead_id     = 3;
    t.overtake.lead_osi_id = 57;

    const std::string json = ToJson(t);

    EXPECT_TRUE(Contains(json, "\"lead_id\":3")) << json;
    EXPECT_TRUE(Contains(json, "\"lead_osi_id\":57")) << json;
}

TEST(VirtualDriverTelemetryJson, OvertakeLeadIdsDefaultToNoPartner)
{
    VirtualDriverTelemetry t;  // overtake left at its defaults

    const std::string json = ToJson(t);

    EXPECT_TRUE(Contains(json, "\"lead_id\":-1")) << json;
    EXPECT_TRUE(Contains(json, "\"lead_osi_id\":-1")) << json;
}
