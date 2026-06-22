// R5-U3 light-storage unification tests.
//
// Scenario LightStateAction parsing is now done entirely by the NATIVE esmini parser
// (scenarioengine::LightStateAction), so the old GT ParseLightStateAction unit tests are
// gone. These tests instead cover the new VehicleLightBridge (the single source of truth
// is Object::vehLghtStsList[]) and the ScenarioLightRegistry arbitration latch.

#include <gtest/gtest.h>
#include "gt_esmini/scenario/ExtraAction.hpp"          // gt_esmini::LightState / VehicleLightType
#include "gt_esmini/scenario/VehicleLightBridge.hpp"   // ApplyLight / ReadLight / registry
#include "Entities.hpp"                                 // scenarioengine::Vehicle / Object
#include "OSCPrivateAction.hpp"                         // scenarioengine::LightStateAction
#include "Storyboard.hpp"                               // StoryBoard

namespace gt_esmini {

// ---------------------------------------------------------------------------
// Enum mapping completeness
// ---------------------------------------------------------------------------
TEST(VehicleLightBridgeMapping, AllGtTypesMapToDistinctUpstreamSlots) {
    // All 13 GT light types must map to a valid (non-UNDEFINED, non-sentinel) upstream slot.
    EXPECT_EQ(lightbridge::MappedGtTypeCount(), 13);

    for (int i = static_cast<int>(VehicleLightType::DAYTIME_RUNNING_LIGHTS);
         i <= static_cast<int>(VehicleLightType::SPECIAL_PURPOSE_LIGHTS); ++i) {
        auto up = lightbridge::ToUpstream(static_cast<VehicleLightType>(i));
        EXPECT_NE(up, scenarioengine::Object::VehicleLightType::UNDEFINED)
            << "GT light type index " << i << " is unmapped";
        EXPECT_NE(up, scenarioengine::Object::VehicleLightType::VEHICLE_LIGHT_SIZE);
    }
}

TEST(VehicleLightBridgeMapping, RoundTripPreservesType) {
    for (int i = static_cast<int>(VehicleLightType::DAYTIME_RUNNING_LIGHTS);
         i <= static_cast<int>(VehicleLightType::SPECIAL_PURPOSE_LIGHTS); ++i) {
        auto gt  = static_cast<VehicleLightType>(i);
        auto up  = lightbridge::ToUpstream(gt);
        VehicleLightType back;
        ASSERT_TRUE(lightbridge::ToGt(up, back));
        EXPECT_EQ(back, gt);
    }
}

TEST(VehicleLightBridgeMapping, UpstreamTailLightsHasNoGtCounterpart) {
    VehicleLightType dummy;
    EXPECT_FALSE(lightbridge::ToGt(scenarioengine::Object::VehicleLightType::TAIL_LIGHTS, dummy));
}

// ---------------------------------------------------------------------------
// ReadLight: UNKNOWN -> OFF
// ---------------------------------------------------------------------------
TEST(VehicleLightBridgeRead, FreshObjectReadsOff) {
    scenarioengine::Vehicle veh;  // vehLghtStsList[] default-initialized (all UNKNOWN)
    auto* obj = static_cast<scenarioengine::Object*>(&veh);

    // Native default mode is UNKNOWN -> bridge must report OFF.
    EXPECT_EQ(ReadLight(obj, VehicleLightType::BRAKE_LIGHTS).mode, LightState::Mode::OFF);
    EXPECT_EQ(ReadLight(obj, VehicleLightType::LOW_BEAM).mode, LightState::Mode::OFF);
}

TEST(VehicleLightBridgeRead, NullObjectReadsOff) {
    EXPECT_EQ(ReadLight(nullptr, VehicleLightType::BRAKE_LIGHTS).mode, LightState::Mode::OFF);
}

// ---------------------------------------------------------------------------
// ApplyLight: write/read round trip + dirty-bit-only-on-change
// ---------------------------------------------------------------------------
static bool DirtyLight(const scenarioengine::Object* obj) {
    return obj->dirty_.Check(static_cast<uint64_t>(scenarioengine::Object::DirtyBit::LIGHT_STATE));
}

TEST(VehicleLightBridgeApply, WriteThenReadRoundTrip) {
    scenarioengine::Vehicle veh;
    auto* obj = static_cast<scenarioengine::Object*>(&veh);

    LightState on;
    on.mode = LightState::Mode::ON;
    ApplyLight(obj, VehicleLightType::BRAKE_LIGHTS, on);
    EXPECT_EQ(ReadLight(obj, VehicleLightType::BRAKE_LIGHTS).mode, LightState::Mode::ON);

    LightState off;
    off.mode = LightState::Mode::OFF;
    ApplyLight(obj, VehicleLightType::BRAKE_LIGHTS, off);
    EXPECT_EQ(ReadLight(obj, VehicleLightType::BRAKE_LIGHTS).mode, LightState::Mode::OFF);
}

TEST(VehicleLightBridgeApply, DirtyBitSetOnlyOnChange) {
    scenarioengine::Vehicle veh;
    auto* obj = static_cast<scenarioengine::Object*>(&veh);

    LightState on;
    on.mode = LightState::Mode::ON;

    // First write changes state -> dirty + returns true.
    obj->dirty_.Clear();
    EXPECT_TRUE(ApplyLight(obj, VehicleLightType::BRAKE_LIGHTS, on));
    EXPECT_TRUE(DirtyLight(obj));

    // Re-writing the SAME state must not set the dirty bit (no dat spam).
    obj->dirty_.Clear();
    EXPECT_FALSE(ApplyLight(obj, VehicleLightType::BRAKE_LIGHTS, on));
    EXPECT_FALSE(DirtyLight(obj));

    // Changing state sets it again.
    LightState off;
    off.mode = LightState::Mode::OFF;
    obj->dirty_.Clear();
    EXPECT_TRUE(ApplyLight(obj, VehicleLightType::BRAKE_LIGHTS, off));
    EXPECT_TRUE(DirtyLight(obj));
}

TEST(VehicleLightBridgeApply, OffZeroesEmission) {
    scenarioengine::Vehicle veh;
    auto* obj = static_cast<scenarioengine::Object*>(&veh);

    LightState on;
    on.mode = LightState::Mode::ON;
    on.luminousIntensity = 6000.0;
    ApplyLight(obj, VehicleLightType::HIGH_BEAM, on);

    LightState off;
    off.mode = LightState::Mode::OFF;
    ApplyLight(obj, VehicleLightType::HIGH_BEAM, off);

    const auto& slot = obj->vehLghtStsList[static_cast<size_t>(
        scenarioengine::Object::VehicleLightType::HIGH_BEAM)];
    EXPECT_DOUBLE_EQ(slot.emission[0], 0.0);
    EXPECT_DOUBLE_EQ(slot.emission[1], 0.0);
    EXPECT_DOUBLE_EQ(slot.emission[2], 0.0);
}

// ---------------------------------------------------------------------------
// Aggregate expansion: FOG_LIGHTS -> front+rear, WARNING_LIGHTS -> left+right
// ---------------------------------------------------------------------------
TEST(VehicleLightBridgeApply, FogAggregateExpandsToFrontAndRear) {
    scenarioengine::Vehicle veh;
    auto* obj = static_cast<scenarioengine::Object*>(&veh);

    LightState on;
    on.mode = LightState::Mode::ON;
    ApplyLight(obj, VehicleLightType::FOG_LIGHTS, on);

    EXPECT_EQ(ReadLight(obj, VehicleLightType::FOG_LIGHTS).mode, LightState::Mode::ON);        // aggregate slot
    EXPECT_EQ(ReadLight(obj, VehicleLightType::FOG_LIGHTS_FRONT).mode, LightState::Mode::ON);
    EXPECT_EQ(ReadLight(obj, VehicleLightType::FOG_LIGHTS_REAR).mode, LightState::Mode::ON);
}

TEST(VehicleLightBridgeApply, WarningAggregateExpandsToBothIndicators) {
    scenarioengine::Vehicle veh;
    auto* obj = static_cast<scenarioengine::Object*>(&veh);

    LightState flashing;
    flashing.mode = LightState::Mode::FLASHING;
    ApplyLight(obj, VehicleLightType::WARNING_LIGHTS, flashing);

    EXPECT_EQ(ReadLight(obj, VehicleLightType::WARNING_LIGHTS).mode, LightState::Mode::FLASHING);
    EXPECT_EQ(ReadLight(obj, VehicleLightType::INDICATOR_LEFT).mode, LightState::Mode::FLASHING);
    EXPECT_EQ(ReadLight(obj, VehicleLightType::INDICATOR_RIGHT).mode, LightState::Mode::FLASHING);
}

// ---------------------------------------------------------------------------
// Arbitration latch: scenario ownership latches once the action starts
// ---------------------------------------------------------------------------
TEST(ScenarioLightRegistryLatch, LatchesOnceActionLeavesStandby) {
    ScenarioLightRegistry::Instance().Clear();

    scenarioengine::Vehicle veh;
    auto* obj = static_cast<scenarioengine::Object*>(&veh);

    // Build a native LightStateAction for brakeLights, attach to a hand-built storyboard.
    auto* lsa = new scenarioengine::LightStateAction(nullptr);
    lsa->SetVehicleLightType(scenarioengine::Object::VehicleLightType::BRAKE_LIGHTS);
    lsa->object_ = obj;
    lsa->ResetState(scenarioengine::StoryBoardElement::State::STANDBY);  // not yet started

    scenarioengine::StoryBoard sb;
    sb.init_.private_action_.push_back(lsa);

    ScenarioLightRegistry::Instance().RegisterFromStoryboard(sb);

    // Registered, but not started -> not controlled yet.
    EXPECT_TRUE(ScenarioLightRegistry::Instance().IsRegistered(obj, VehicleLightType::BRAKE_LIGHTS));
    EXPECT_FALSE(ScenarioLightRegistry::Instance().IsScenarioControlled(obj, VehicleLightType::BRAKE_LIGHTS));

    // Action starts (leaves STANDBY) -> now controlled.
    lsa->ResetState(scenarioengine::StoryBoardElement::State::RUNNING);
    EXPECT_TRUE(ScenarioLightRegistry::Instance().IsScenarioControlled(obj, VehicleLightType::BRAKE_LIGHTS));

    // Latched permanently: even after returning to STANDBY it stays controlled.
    lsa->ResetState(scenarioengine::StoryBoardElement::State::STANDBY);
    EXPECT_TRUE(ScenarioLightRegistry::Instance().IsScenarioControlled(obj, VehicleLightType::BRAKE_LIGHTS));

    // Unrelated light type is not scenario-controlled.
    EXPECT_FALSE(ScenarioLightRegistry::Instance().IsScenarioControlled(obj, VehicleLightType::LOW_BEAM));

    sb.init_.private_action_.clear();
    delete lsa;
    ScenarioLightRegistry::Instance().Clear();
}

TEST(ScenarioLightRegistryLatch, WarningActionLatchesBothIndicators) {
    ScenarioLightRegistry::Instance().Clear();

    scenarioengine::Vehicle veh;
    auto* obj = static_cast<scenarioengine::Object*>(&veh);

    auto* lsa = new scenarioengine::LightStateAction(nullptr);
    lsa->SetVehicleLightType(scenarioengine::Object::VehicleLightType::WARNING_LIGHTS);
    lsa->object_ = obj;
    lsa->ResetState(scenarioengine::StoryBoardElement::State::RUNNING);

    scenarioengine::StoryBoard sb;
    sb.init_.private_action_.push_back(lsa);
    ScenarioLightRegistry::Instance().RegisterFromStoryboard(sb);

    // Aggregate expansion: warningLights ownership covers both indicators.
    EXPECT_TRUE(ScenarioLightRegistry::Instance().IsScenarioControlled(obj, VehicleLightType::INDICATOR_LEFT));
    EXPECT_TRUE(ScenarioLightRegistry::Instance().IsScenarioControlled(obj, VehicleLightType::INDICATOR_RIGHT));

    sb.init_.private_action_.clear();
    delete lsa;
    ScenarioLightRegistry::Instance().Clear();
}

// ---------------------------------------------------------------------------
// Blink ticker: must not fight native scenario FLASHING (R5-U3 follow-up FIX 1)
// ---------------------------------------------------------------------------
TEST(LightBlinkTicker, SkipsScenarioControlledFlashingSlots) {
    ScenarioLightRegistry::Instance().Clear();

    scenarioengine::Vehicle veh;
    auto* obj = static_cast<scenarioengine::Object*>(&veh);

    // Latch INDICATOR_LEFT as scenario-controlled (native action RUNNING).
    auto* lsa = new scenarioengine::LightStateAction(nullptr);
    lsa->SetVehicleLightType(scenarioengine::Object::VehicleLightType::INDICATOR_LEFT);
    lsa->object_ = obj;
    lsa->ResetState(scenarioengine::StoryBoardElement::State::RUNNING);
    scenarioengine::StoryBoard sb;
    sb.init_.private_action_.push_back(lsa);
    ScenarioLightRegistry::Instance().RegisterFromStoryboard(sb);
    ASSERT_TRUE(ScenarioLightRegistry::Instance().IsScenarioControlled(obj, VehicleLightType::INDICATOR_LEFT));

    // GT writes INDICATOR_RIGHT FLASHING (emission becomes non-zero via the bridge).
    LightState fl;
    fl.mode = LightState::Mode::FLASHING;
    ApplyLight(obj, VehicleLightType::INDICATOR_RIGHT, fl);

    // Simulate the native action having animated INDICATOR_LEFT: FLASHING with a
    // distinctive emission the ticker must NOT touch.
    auto& left = obj->vehLghtStsList[static_cast<size_t>(scenarioengine::Object::VehicleLightType::INDICATOR_LEFT)];
    left.mode        = scenarioengine::Object::VehicleLightMode::FLASHING;
    left.emission[0] = 0.42;
    left.emission[1] = 0.21;
    left.emission[2] = 0.11;

    auto& right = obj->vehLghtStsList[static_cast<size_t>(scenarioengine::Object::VehicleLightType::INDICATOR_RIGHT)];
    ASSERT_GT(right.emission[0], 0.0);

    LightBlinkTicker t;
    t.Tick(obj, 0.6, 0.6, 0.5, 0.5);  // edge: off -> on phase
    t.Tick(obj, 1.2, 0.6, 0.5, 0.5);  // edge: on -> off phase (GT slot emission zeroed)

    // Scenario-controlled slot untouched by the GT ticker.
    EXPECT_DOUBLE_EQ(left.emission[0], 0.42);
    EXPECT_DOUBLE_EQ(left.emission[1], 0.21);
    EXPECT_DOUBLE_EQ(left.emission[2], 0.11);
    // GT-written slot blinked to the OFF phase.
    EXPECT_DOUBLE_EQ(right.emission[0], 0.0);
    EXPECT_DOUBLE_EQ(right.emission[1], 0.0);
    EXPECT_DOUBLE_EQ(right.emission[2], 0.0);
    // Mode stays FLASHING throughout (ticker toggles emission only).
    EXPECT_EQ(right.mode, scenarioengine::Object::VehicleLightMode::FLASHING);

    sb.init_.private_action_.clear();
    delete lsa;
    ScenarioLightRegistry::Instance().Clear();
}

} // namespace gt_esmini

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
