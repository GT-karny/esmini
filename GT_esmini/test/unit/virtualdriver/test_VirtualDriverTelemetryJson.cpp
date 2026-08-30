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

// ───────────── vd_intent_layer.md §4-1: the two intent arrays on the wire ─────────────
//
// The serializer is where the enum vocabulary becomes strings, so it is where a value could
// silently turn into a different-but-plausible word. test_VdIntentProjection.cpp pins the token
// SET; these pin that the wire format actually carries them, in two separate arrays.

TEST(VirtualDriverTelemetryJson, IntentsAndReasonsAreTwoSeparateArrays)
{
    VirtualDriverTelemetry t;

    VdIntent intent;
    intent.id             = 7;
    intent.kind           = IntentKind::STOP;
    intent.phase          = IntentPhase::EXECUTING;
    intent.distance_m     = 24.1;
    intent.eta_s          = 3.5;
    intent.subject_osi_id = 11;
    intent.has_position   = true;
    intent.x              = 112.4;
    intent.y              = -8.2;
    t.intents.push_back(intent);

    VdIntentReason reason;
    reason.id          = 7;
    reason.kind        = IntentKind::STOP;
    reason.phase       = IntentPhase::EXECUTING;
    reason.source      = "traffic_light";
    reason.tier        = "compliance";
    reason.binding_lon = true;
    reason.committed   = true;
    t.intent_reasons.push_back(reason);

    const std::string json = ToJson(t);

    EXPECT_TRUE(Contains(json, "\"intents\":[{")) << json;
    EXPECT_TRUE(Contains(json, "\"intent_reasons\":[{")) << json;
    EXPECT_TRUE(Contains(json, "\"kind\":\"stop\"")) << json;
    EXPECT_TRUE(Contains(json, "\"phase\":\"executing\"")) << json;
    EXPECT_TRUE(Contains(json, "\"eta_s\":3.5")) << json;
    EXPECT_TRUE(Contains(json, "\"has_position\":true")) << json;
    EXPECT_TRUE(Contains(json, "\"binding_lon\":true")) << json;
    EXPECT_TRUE(Contains(json, "\"source\":\"traffic_light\"")) << json;
    EXPECT_TRUE(Contains(json, "\"tier\":\"compliance\"")) << json;
}

// -1 is "asked, and there is no answer" (past a planned stop) -- never 0, and never absent. If
// this key ever went missing on an unanswerable eta, a consumer defaulting it to 0 would show
// "arriving now".
TEST(VirtualDriverTelemetryJson, AnUnanswerableEtaIsSerializedAsMinusOneNotOmitted)
{
    VirtualDriverTelemetry t;
    VdIntent               intent;
    intent.kind  = IntentKind::TURN;
    intent.eta_s = -1.0;
    intent.distance_m = -1.0;
    t.intents.push_back(intent);

    const std::string json = ToJson(t);
    EXPECT_TRUE(Contains(json, "\"eta_s\":-1")) << json;
    EXPECT_TRUE(Contains(json, "\"has_position\":false")) << json;
}

// §8-2: the blocker row, and specifically that a blocker with no position serializes `where` as
// "" rather than dropping the key -- so every element of the array has the same shape.
TEST(VirtualDriverTelemetryJson, BlockersCarryWhereCodeAndTheMeasuredRequiredPair)
{
    VirtualDriverTelemetry t;
    t.lane_change.blockers = {
        {IntentWhere::REAR, 12, kBlockerRearGap, kQuantityGapM, 6.2, 11.3},
        {IntentWhere::FRONT, 11, kBlockerLeadGap, kQuantityGapM, 9.8, 16.7},
    };
    t.overtake.blockers = {{IntentWhere::NONE, -1, kBlockerRouteBudget, kQuantityBudgetM, 41.2, 88.6}};

    const std::string json = ToJson(t);

    EXPECT_TRUE(Contains(json, "\"where\":\"rear\"")) << json;
    EXPECT_TRUE(Contains(json, "\"where\":\"front\"")) << json;
    EXPECT_TRUE(Contains(json, "\"code\":\"rear_gap\"")) << json;
    EXPECT_TRUE(Contains(json, "\"measured\":6.2")) << json;
    EXPECT_TRUE(Contains(json, "\"required\":11.3")) << json;
    // A blocker that is about the maneuver rather than another vehicle: the key stays, empty.
    EXPECT_TRUE(Contains(json, "\"where\":\"\"")) << json;
    EXPECT_TRUE(Contains(json, "\"code\":\"route_budget\"")) << json;
    EXPECT_TRUE(Contains(json, "\"quantity\":\"budget_m\"")) << json;
}

TEST(VirtualDriverTelemetryJson, EmptyBlockerListsSerializeAsEmptyArrays)
{
    VirtualDriverTelemetry t;  // all defaults
    const std::string      json = ToJson(t);
    EXPECT_TRUE(Contains(json, "\"blockers\":[]")) << json;
    EXPECT_TRUE(Contains(json, "\"intents\":[]")) << json;
    EXPECT_TRUE(Contains(json, "\"intent_reasons\":[]")) << json;
}

// §3-4 / §3-3's two new materials. Both are additive keys on existing blocks.
TEST(VirtualDriverTelemetryJson, AbortedReasonAndBrakeLightAreOnTheWire)
{
    VirtualDriverTelemetry t;
    EXPECT_TRUE(Contains(ToJson(t), "\"aborted_reason\":\"\""));
    EXPECT_TRUE(Contains(ToJson(t), "\"brake_light_on\":false"));

    t.lane_change.aborted_reason = "manual_lateral";
    t.brake_light_on             = true;
    const std::string json       = ToJson(t);
    EXPECT_TRUE(Contains(json, "\"aborted_reason\":\"manual_lateral\"")) << json;
    EXPECT_TRUE(Contains(json, "\"brake_light_on\":true")) << json;
}

// §7: junction_turn_observed is a SECOND block, not a widening of junction_turn -- REQ-AD-021
// verifies against the latter and its contract must not move.
TEST(VirtualDriverTelemetryJson, TheObservationScanIsItsOwnBlockAlongsideJunctionTurn)
{
    VirtualDriverTelemetry t;
    t.junction_turn.dir              = 1;
    t.junction_turn.dist_to_entry_m  = 28.0;
    t.junction_turn_observed.dir     = -1;
    t.junction_turn_observed.dist_to_entry_m = 310.0;

    const std::string json = ToJson(t);
    EXPECT_TRUE(Contains(json, "\"junction_turn\":{\"dir\":1")) << json;
    EXPECT_TRUE(Contains(json, "\"junction_turn_observed\":{\"dir\":-1")) << json;
    EXPECT_TRUE(Contains(json, "\"dist_to_entry_m\":310")) << json;
}

// §5: -1 means no policy constraint is governing, which is a different statement from "not
// computed" -- so the key is emitted unconditionally.
TEST(VirtualDriverTelemetryJson, TheBindingConstraintIndexIsAlwaysEmitted)
{
    VirtualDriverTelemetry t;
    EXPECT_TRUE(Contains(ToJson(t), "\"binding_constraint_index\":-1"));

    t.midlong.binding_constraint_index = 2;
    EXPECT_TRUE(Contains(ToJson(t), "\"binding_constraint_index\":2"));
}
