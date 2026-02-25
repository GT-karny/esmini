// Level 4: Data Flow Verification Tests
// Purpose: Verify correct data marshalling between C++ and Python

#ifdef GT_ENABLE_EMBEDDED_PYTHON

#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>
#include <vector>
#include "gt_esmini/control/pythondriver/PythonDriverBridge.hpp"
#include "gt_esmini/control/ControllerRealDriver.hpp"      // WaypointData
#include "gt_esmini/control/realdriver/LonProfilePlanner.hpp"  // LonProfilePoint

namespace fs = std::filesystem;

namespace gt_esmini
{
namespace test
{

// Helper to get Python home path
// GT_EMBEDDED_PYTHON_HOME is defined as a quoted string literal by CMake
inline std::string GetPythonHome()
{
#ifdef GT_EMBEDDED_PYTHON_HOME
    return GT_EMBEDDED_PYTHON_HOME;
#else
    return "";
#endif
}

class DataFlowTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        test_dir_ = fs::temp_directory_path() / "dataflow_test";
        fs::create_directories(test_dir_);
    }

    void TearDown() override
    {
        bridge_.Shutdown();
        std::error_code ec;
        fs::remove_all(test_dir_, ec);
    }

    void WriteScript(const std::string& filename, const std::string& content)
    {
        std::ofstream f(test_dir_ / filename);
        f << content;
        f.close();
    }

    bool InitBridge(const std::string& script_name, const std::string& class_name)
    {
        return bridge_.Initialize(
            (test_dir_ / script_name).string(),
            class_name,
            GetPythonHome(), // python_home from embedded Python
            false, "", "", 0.01, 0
        );
    }

    fs::path test_dir_;
    PythonDriverBridge bridge_;
};

// L4-001: Waypoints array is correctly passed to Python
TEST_F(DataFlowTest, WaypointsPassedCorrectly)
{
    WriteScript("waypoint_echo.py", R"(
class WaypointEcho:
    def init(self, config):
        pass
    def step(self, frame_data):
        wps = frame_data.get("waypoints", [])
        # Return count as steering for verification
        count = len(wps)
        # Sum of x values as throttle
        x_sum = sum(wp.get("x", 0) for wp in wps) if wps else 0
        return {
            "throttle": x_sum / 100.0,  # Normalize
            "brake": 0.0,
            "steering": float(count),
            "gear": 1,
            "lights": {},
            "engine_brake": 0.49,
            "adas_states": []
        }
)");

    ASSERT_TRUE(InitBridge("waypoint_echo.py", "WaypointEcho"))
        << "Initialize failed: " << bridge_.GetLastError();

    std::vector<WaypointData> waypoints;
    waypoints.push_back({10.0, 2.0, 0.5, 1, 10.0, -1, 0.0});
    waypoints.push_back({20.0, 4.0, 0.6, 1, 15.0, -1, 0.0});
    waypoints.push_back({30.0, 6.0, 0.7, 1, 20.0, -1, 0.0});

    PythonFrameData frame_data;
    frame_data.waypoints = &waypoints;
    frame_data.waypoint_index = 0;

    PythonDriverInput result = bridge_.CallStep(frame_data);

    EXPECT_TRUE(result.valid) << "CallStep returned invalid result";
    EXPECT_NEAR(result.steering, 3.0, 1e-6) << "Expected 3 waypoints";
    EXPECT_NEAR(result.throttle, 0.6, 1e-6) << "Expected x_sum=60, throttle=0.6";
}

// L4-002: Longitudinal profile is correctly passed
TEST_F(DataFlowTest, LonProfilePassedCorrectly)
{
    WriteScript("lon_profile_echo.py", R"(
class LonProfileEcho:
    def init(self, config):
        pass
    def step(self, frame_data):
        lp = frame_data.get("lon_profile", [])
        count = len(lp)
        # Return last v_target as throttle
        v_target = lp[-1]["v_target"] if lp else 0.0
        return {
            "throttle": v_target / 100.0,  # Normalize to [0,1]
            "brake": 0.0,
            "steering": float(count),
            "gear": 1,
            "lights": {},
            "engine_brake": 0.49,
            "adas_states": []
        }
)");

    ASSERT_TRUE(InitBridge("lon_profile_echo.py", "LonProfileEcho"))
        << "Initialize failed: " << bridge_.GetLastError();

    std::vector<LonProfilePoint> lon_profile;
    lon_profile.push_back({0.0, 10.0, 2.0, 1.0});
    lon_profile.push_back({0.15, 15.0, 2.0, 1.0});
    lon_profile.push_back({0.30, 20.0, 2.0, 1.0});

    PythonFrameData frame_data;
    frame_data.lon_profile = &lon_profile;

    PythonDriverInput result = bridge_.CallStep(frame_data);

    EXPECT_TRUE(result.valid) << "CallStep returned invalid result";
    EXPECT_NEAR(result.steering, 3.0, 1e-6) << "Expected 3 profile points";
    EXPECT_NEAR(result.throttle, 0.2, 1e-6) << "Expected v_target=20, throttle=0.2";
}

// L4-003: Action context flags are correctly passed
TEST_F(DataFlowTest, ActionContextPassedCorrectly)
{
    WriteScript("action_echo.py", R"(
class ActionEcho:
    def init(self, config):
        pass
    def step(self, frame_data):
        actions = frame_data.get("actions", {})
        # Encode action flags as throttle/brake/steering
        throttle = 1.0 if actions.get("lane_change") else 0.0
        brake = 1.0 if actions.get("assign_route") else 0.0
        target_lane = actions.get("lane_change_target_lane")
        steering = float(target_lane) if target_lane is not None else 0.0
        return {
            "throttle": throttle,
            "brake": brake,
            "steering": steering,
            "gear": 1,
            "lights": {},
            "engine_brake": 0.49,
            "adas_states": []
        }
)");

    ASSERT_TRUE(InitBridge("action_echo.py", "ActionEcho"))
        << "Initialize failed: " << bridge_.GetLastError();

    PythonFrameData frame_data;
    frame_data.actions.lane_change = true;
    frame_data.actions.lane_change_target_lane = -2;
    frame_data.actions.has_lane_change_target_lane = true;
    frame_data.actions.assign_route = true;

    PythonDriverInput result = bridge_.CallStep(frame_data);

    EXPECT_TRUE(result.valid) << "CallStep returned invalid result";
    EXPECT_NEAR(result.throttle, 1.0, 1e-6) << "lane_change should be true";
    EXPECT_NEAR(result.brake, 1.0, 1e-6) << "assign_route should be true";
    EXPECT_NEAR(result.steering, -2.0, 1e-6) << "target lane should be -2";
}

// L4-004: Light patches are correctly parsed
TEST_F(DataFlowTest, LightPatchesParsedCorrectly)
{
    WriteScript("light_control.py", R"(
class LightControl:
    def init(self, config):
        pass
    def step(self, frame_data):
        return {
            "throttle": 0.0,
            "brake": 0.0,
            "steering": 0.0,
            "gear": 1,
            "lights": {
                "low_beam": "on",
                "brake": "auto",
                "left_indicator": "off",
            },
            "engine_brake": 0.49,
            "adas_states": []
        }
)");

    ASSERT_TRUE(InitBridge("light_control.py", "LightControl"))
        << "Initialize failed: " << bridge_.GetLastError();

    PythonFrameData frame_data;
    PythonDriverInput result = bridge_.CallStep(frame_data);

    EXPECT_TRUE(result.valid) << "CallStep returned invalid result";

    // Light states: -1=unspecified, 0=auto, 1=off, 2=on
    const auto& lights = result.lights.states;
    EXPECT_EQ(lights[static_cast<size_t>(PythonLightSlot::LOW_BEAM)], 2)
        << "low_beam should be 'on' (2)";
    EXPECT_EQ(lights[static_cast<size_t>(PythonLightSlot::BRAKE)], 0)
        << "brake should be 'auto' (0)";
    EXPECT_EQ(lights[static_cast<size_t>(PythonLightSlot::LEFT_INDICATOR)], 1)
        << "left_indicator should be 'off' (1)";
}

// L4-005: Frame timing data is correctly passed
TEST_F(DataFlowTest, FrameTimingPassedCorrectly)
{
    WriteScript("timing_echo.py", R"(
class TimingEcho:
    def init(self, config):
        pass
    def step(self, frame_data):
        frame_id = frame_data.get("frame_id", -1)
        dt = frame_data.get("dt", 0.0)
        current_speed = frame_data.get("current_speed", 0.0)
        set_speed = frame_data.get("set_speed", 0.0)
        return {
            "throttle": float(frame_id) / 1000.0,
            "brake": dt,
            "steering": (set_speed - current_speed) / 10.0,
            "gear": 1,
            "lights": {},
            "engine_brake": 0.49,
            "adas_states": []
        }
)");

    ASSERT_TRUE(InitBridge("timing_echo.py", "TimingEcho"))
        << "Initialize failed: " << bridge_.GetLastError();

    PythonFrameData frame_data;
    frame_data.frame_id = 100;
    frame_data.dt = 0.01;
    frame_data.current_speed = 10.0;
    frame_data.set_speed = 15.0;

    PythonDriverInput result = bridge_.CallStep(frame_data);

    EXPECT_TRUE(result.valid) << "CallStep returned invalid result";
    EXPECT_NEAR(result.throttle, 0.1, 1e-6) << "frame_id=100, throttle=0.1";
    EXPECT_NEAR(result.brake, 0.01, 1e-6) << "dt=0.01";
    EXPECT_NEAR(result.steering, 0.5, 1e-6) << "(15-10)/10 = 0.5";
}

// Additional: Waypoint index is correctly passed
TEST_F(DataFlowTest, WaypointIndexPassedCorrectly)
{
    WriteScript("waypoint_index_echo.py", R"(
class WaypointIndexEcho:
    def init(self, config):
        pass
    def step(self, frame_data):
        idx = frame_data.get("waypoint_index", -1)
        return {
            "throttle": 0.0,
            "brake": 0.0,
            "steering": float(idx),
            "gear": 1,
            "lights": {},
            "engine_brake": 0.49,
            "adas_states": []
        }
)");

    ASSERT_TRUE(InitBridge("waypoint_index_echo.py", "WaypointIndexEcho"))
        << "Initialize failed: " << bridge_.GetLastError();

    std::vector<WaypointData> waypoints;
    waypoints.push_back({1.0, 2.0, 0.0, 1, 10.0, -1, 0.0});
    waypoints.push_back({2.0, 3.0, 0.0, 1, 15.0, -1, 0.0});
    waypoints.push_back({3.0, 4.0, 0.0, 1, 20.0, -1, 0.0});

    PythonFrameData frame_data;
    frame_data.waypoints = &waypoints;
    frame_data.waypoint_index = 2;

    PythonDriverInput result = bridge_.CallStep(frame_data);

    EXPECT_TRUE(result.valid) << "CallStep returned invalid result";
    EXPECT_NEAR(result.steering, 2.0, 1e-6) << "waypoint_index should be 2";
}

// Additional: Empty waypoints array
TEST_F(DataFlowTest, EmptyWaypointsArray)
{
    WriteScript("empty_waypoints.py", R"(
class EmptyWaypointsController:
    def init(self, config):
        pass
    def step(self, frame_data):
        wps = frame_data.get("waypoints", [])
        return {
            "throttle": 0.0,
            "brake": 0.0,
            "steering": float(len(wps)),
            "gear": 1,
            "lights": {},
            "engine_brake": 0.49,
            "adas_states": []
        }
)");

    ASSERT_TRUE(InitBridge("empty_waypoints.py", "EmptyWaypointsController"))
        << "Initialize failed: " << bridge_.GetLastError();

    std::vector<WaypointData> empty_waypoints;

    PythonFrameData frame_data;
    frame_data.waypoints = &empty_waypoints;

    PythonDriverInput result = bridge_.CallStep(frame_data);

    EXPECT_TRUE(result.valid) << "CallStep returned invalid result";
    EXPECT_NEAR(result.steering, 0.0, 1e-6) << "Empty waypoints should give 0";
}

// Additional: Null waypoints pointer
TEST_F(DataFlowTest, NullWaypointsPointer)
{
    WriteScript("null_waypoints.py", R"(
class NullWaypointsController:
    def init(self, config):
        pass
    def step(self, frame_data):
        wps = frame_data.get("waypoints", None)
        is_none = wps is None
        is_empty = len(wps) == 0 if wps is not None else True
        return {
            "throttle": 0.0,
            "brake": 0.0,
            "steering": 1.0 if is_empty else 0.0,
            "gear": 1,
            "lights": {},
            "engine_brake": 0.49,
            "adas_states": []
        }
)");

    ASSERT_TRUE(InitBridge("null_waypoints.py", "NullWaypointsController"))
        << "Initialize failed: " << bridge_.GetLastError();

    PythonFrameData frame_data;
    frame_data.waypoints = nullptr;  // No waypoints

    PythonDriverInput result = bridge_.CallStep(frame_data);

    EXPECT_TRUE(result.valid) << "CallStep returned invalid result";
    EXPECT_NEAR(result.steering, 1.0, 1e-6) << "Null waypoints should result in empty list";
}

// Additional: All action flags
TEST_F(DataFlowTest, AllActionFlagsPassedCorrectly)
{
    WriteScript("all_actions.py", R"(
class AllActionsController:
    def init(self, config):
        pass
    def step(self, frame_data):
        actions = frame_data.get("actions", {})
        # Count how many action flags are True
        flags = [
            actions.get("assign_route", False),
            actions.get("lane_change", False),
            actions.get("lane_offset", False),
            actions.get("follow_trajectory", False),
            actions.get("longitudinal_distance", False),
            actions.get("speed_profile", False),
            actions.get("synchronize", False),
        ]
        count = sum(1 for f in flags if f)
        return {
            "throttle": 0.0,
            "brake": 0.0,
            "steering": float(count),
            "gear": 1,
            "lights": {},
            "engine_brake": 0.49,
            "adas_states": []
        }
)");

    ASSERT_TRUE(InitBridge("all_actions.py", "AllActionsController"))
        << "Initialize failed: " << bridge_.GetLastError();

    PythonFrameData frame_data;
    frame_data.actions.assign_route = true;
    frame_data.actions.lane_change = true;
    frame_data.actions.lane_offset = true;
    frame_data.actions.follow_trajectory = false;
    frame_data.actions.longitudinal_distance = true;
    frame_data.actions.speed_profile = false;
    frame_data.actions.synchronize = true;

    PythonDriverInput result = bridge_.CallStep(frame_data);

    EXPECT_TRUE(result.valid) << "CallStep returned invalid result";
    EXPECT_NEAR(result.steering, 5.0, 1e-6) << "5 action flags should be True";
}

// Additional: Waypoint generation version
TEST_F(DataFlowTest, WaypointGenerationVersionPassed)
{
    WriteScript("generation_version.py", R"(
class GenerationVersionController:
    def init(self, config):
        pass
    def step(self, frame_data):
        gen = frame_data.get("waypoint_generation", {})
        version = gen.get("version", -1)
        return {
            "throttle": 0.0,
            "brake": 0.0,
            "steering": float(version),
            "gear": 1,
            "lights": {},
            "engine_brake": 0.49,
            "adas_states": []
        }
)");

    ASSERT_TRUE(InitBridge("generation_version.py", "GenerationVersionController"))
        << "Initialize failed: " << bridge_.GetLastError();

    PythonFrameData frame_data;
    frame_data.waypoint_generation_version = 42;

    PythonDriverInput result = bridge_.CallStep(frame_data);

    EXPECT_TRUE(result.valid) << "CallStep returned invalid result";
    EXPECT_NEAR(result.steering, 42.0, 1e-6) << "waypoint_generation.version should be 42";
}

} // namespace test
} // namespace gt_esmini

#endif // GT_ENABLE_EMBEDDED_PYTHON
