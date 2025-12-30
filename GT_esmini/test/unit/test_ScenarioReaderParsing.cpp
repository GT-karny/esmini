#include <gtest/gtest.h>
#include "GT_ScenarioReader.hpp"
#include "ExtraAction.hpp"
#include "pugixml.hpp"
#include <iostream>

namespace gt_esmini {

class ScenarioReaderTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialization if needed
    }

    void TearDown() override {
        // Cleanup if needed
    }
};

// Helper subclass to expose protected methods for testing
class TestableGT_ScenarioReader : public GT_ScenarioReader {
public:
    using GT_ScenarioReader::GT_ScenarioReader;
    // Expose protected methods
    using GT_ScenarioReader::ParseLightStateAction;
};

TEST_F(ScenarioReaderTest, ParseValidLightStateAction) {
    pugi::xml_document doc;
    pugi::xml_node node = doc.append_child("LightStateAction");
    node.append_attribute("transitionTime").set_value("0.5");
    
    pugi::xml_node lightType = node.append_child("LightType");
    pugi::xml_node vehicleLight = lightType.append_child("VehicleLight");
    vehicleLight.append_attribute("vehicleLightType").set_value("brakeLights");
    
    pugi::xml_node lightState = node.append_child("LightState");
    lightState.append_attribute("mode").set_value("on");
    lightState.append_attribute("luminousIntensity").set_value("1.0");
    
    TestableGT_ScenarioReader reader(nullptr, nullptr, nullptr);
    OSCLightStateAction* action = reader.ParseLightStateAction(node);
    
    ASSERT_NE(action, nullptr);
    EXPECT_EQ(action->lightType_, VehicleLightType::BRAKE_LIGHTS);
    EXPECT_EQ(action->lightState_.mode, LightState::Mode::ON);
    EXPECT_DOUBLE_EQ(action->lightState_.luminousIntensity, 1.0);
    EXPECT_DOUBLE_EQ(action->transitionTime_, 0.5);
    
    delete action;
}

TEST_F(ScenarioReaderTest, ParseMissingLightType) {
    pugi::xml_document doc;
    pugi::xml_node node = doc.append_child("LightStateAction");
    // Missing LightType
    
    TestableGT_ScenarioReader reader(nullptr, nullptr, nullptr);
    OSCLightStateAction* action = reader.ParseLightStateAction(node);
    
    EXPECT_EQ(action, nullptr);
}

TEST_F(ScenarioReaderTest, ParseInvalidMode) {
    pugi::xml_document doc;
    pugi::xml_node node = doc.append_child("LightStateAction");
    
    pugi::xml_node lightType = node.append_child("LightType");
    pugi::xml_node vehicleLight = lightType.append_child("VehicleLight");
    vehicleLight.append_attribute("vehicleLightType").set_value("lowBeam");
    
    pugi::xml_node lightState = node.append_child("LightState");
    lightState.append_attribute("mode").set_value("invalid_mode"); // Invalid mode
    
    TestableGT_ScenarioReader reader(nullptr, nullptr, nullptr);
    OSCLightStateAction* action = reader.ParseLightStateAction(node);
    
    ASSERT_NE(action, nullptr);
    EXPECT_EQ(action->lightType_, VehicleLightType::LOW_BEAM);
    EXPECT_EQ(action->lightState_.mode, LightState::Mode::OFF); // Default value
    
    delete action;
}

} // namespace gt_esmini

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
