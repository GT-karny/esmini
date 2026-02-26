// Level 2: PythonDriverBridge Unit Tests
// Purpose: Test Python embedding, script loading, and basic step() cycle

#ifdef GT_ENABLE_EMBEDDED_PYTHON

#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>
#include "gt_esmini/control/pythondriver/PythonDriverBridge.hpp"

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

class PythonDriverBridgeTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Create temp directory for test scripts
        test_dir_ = fs::temp_directory_path() / "pythondriver_test";
        fs::create_directories(test_dir_);
    }

    void TearDown() override
    {
        // Clean up test directory
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
            false,           // trace_enabled
            "",              // trace_dir
            "",              // xodr_path
            0.01,            // dt
            0                // ego_id
        );
    }

    fs::path test_dir_;
    PythonDriverBridge bridge_;
};

// L2-001: Initialize with minimal valid script
TEST_F(PythonDriverBridgeTest, InitializeWithMinimalScript)
{
    WriteScript("minimal_controller.py", R"(
class MinimalController:
    def init(self, config):
        pass
    def step(self, frame_data):
        return {
            "throttle": 0.0,
            "brake": 0.0,
            "steering": 0.0,
            "gear": 1,
            "lights": {},
            "engine_brake": 0.49,
            "adas_states": []
        }
)");

    bool result = InitBridge("minimal_controller.py", "MinimalController");

    EXPECT_TRUE(result) << "Initialize failed: " << bridge_.GetLastError();
    EXPECT_TRUE(bridge_.IsInitialized());
    EXPECT_FALSE(bridge_.HasFatalError());

    bridge_.Shutdown();
}

// L2-002: Script not found handling
TEST_F(PythonDriverBridgeTest, InitializeWithMissingScript)
{
    bool result = bridge_.Initialize(
        "/nonexistent/path/to/controller.py",
        "Controller",
        "", false, "", "", 0.01, 0
    );

    EXPECT_FALSE(result);
    EXPECT_FALSE(bridge_.IsInitialized());
    EXPECT_TRUE(bridge_.HasFatalError());
    EXPECT_FALSE(bridge_.GetLastError().empty());
}

// L2-003: Class not found handling
TEST_F(PythonDriverBridgeTest, InitializeWithMissingClass)
{
    WriteScript("no_class.py", R"(
# Empty script with no controller class
def some_function():
    pass
)");

    bool result = InitBridge("no_class.py", "NonExistentClass");

    EXPECT_FALSE(result);
    EXPECT_TRUE(bridge_.HasFatalError());
    // Error message should mention the missing class
    EXPECT_NE(bridge_.GetLastError().find("NonExistentClass"), std::string::npos)
        << "Error: " << bridge_.GetLastError();
}

// L2-004: init() exception handling
TEST_F(PythonDriverBridgeTest, InitExceptionHandling)
{
    WriteScript("init_exception.py", R"(
class BrokenController:
    def init(self, config):
        raise RuntimeError("init failed intentionally")
    def step(self, frame_data):
        return {"throttle": 0, "brake": 0, "steering": 0, "gear": 1, "lights": {}}
)");

    bool result = InitBridge("init_exception.py", "BrokenController");

    EXPECT_FALSE(result);
    EXPECT_TRUE(bridge_.HasFatalError());
}

// L2-005: step() returns valid data
TEST_F(PythonDriverBridgeTest, CallStepReturnsValidData)
{
    WriteScript("valid_step.py", R"(
class ValidController:
    def init(self, config):
        pass
    def step(self, frame_data):
        return {
            "throttle": 0.5,
            "brake": 0.1,
            "steering": -0.3,
            "gear": 1,
            "lights": {"low_beam": "auto"},
            "engine_brake": 0.49,
            "adas_states": [1, 0, 1]
        }
)");

    ASSERT_TRUE(InitBridge("valid_step.py", "ValidController"))
        << "Initialize failed: " << bridge_.GetLastError();

    PythonFrameData frame_data;
    frame_data.frame_id = 1;
    frame_data.dt = 0.01;

    PythonDriverInput result = bridge_.CallStep(frame_data);

    EXPECT_TRUE(result.valid) << "CallStep returned invalid result";
    EXPECT_NEAR(result.throttle, 0.5, 1e-6);
    EXPECT_NEAR(result.brake, 0.1, 1e-6);
    EXPECT_NEAR(result.steering, -0.3, 1e-6);
    EXPECT_EQ(result.gear, 1);
    ASSERT_EQ(result.adasStates.size(), 3u);
    EXPECT_EQ(result.adasStates[0], 1);
    EXPECT_EQ(result.adasStates[1], 0);
    EXPECT_EQ(result.adasStates[2], 1);

    bridge_.Shutdown();
}

// L2-006: step() exception handling
TEST_F(PythonDriverBridgeTest, StepExceptionHandling)
{
    WriteScript("step_exception.py", R"(
class ExceptionController:
    def init(self, config):
        pass
    def step(self, frame_data):
        raise ValueError("step failed intentionally")
)");

    ASSERT_TRUE(InitBridge("step_exception.py", "ExceptionController"))
        << "Initialize failed: " << bridge_.GetLastError();

    PythonFrameData frame_data;
    PythonDriverInput result = bridge_.CallStep(frame_data);

    EXPECT_FALSE(result.valid);
    EXPECT_TRUE(bridge_.HasFatalError());

    bridge_.Shutdown();
}

// L2-007: Missing required 'lights' key in result
TEST_F(PythonDriverBridgeTest, MissingLightsKeyInResult)
{
    WriteScript("no_lights.py", R"(
class NoLightsController:
    def init(self, config):
        pass
    def step(self, frame_data):
        # Missing 'lights' key - should fail validation
        return {"throttle": 0, "brake": 0, "steering": 0, "gear": 1}
)");

    ASSERT_TRUE(InitBridge("no_lights.py", "NoLightsController"))
        << "Initialize failed: " << bridge_.GetLastError();

    PythonFrameData frame_data;
    PythonDriverInput result = bridge_.CallStep(frame_data);

    EXPECT_FALSE(result.valid) << "Expected result.valid=false when 'lights' key is missing";

    bridge_.Shutdown();
}

// Additional: step() returns None
TEST_F(PythonDriverBridgeTest, StepReturnsNone)
{
    WriteScript("returns_none.py", R"(
class NoneController:
    def init(self, config):
        pass
    def step(self, frame_data):
        return None
)");

    ASSERT_TRUE(InitBridge("returns_none.py", "NoneController"))
        << "Initialize failed: " << bridge_.GetLastError();

    PythonFrameData frame_data;
    PythonDriverInput result = bridge_.CallStep(frame_data);

    EXPECT_FALSE(result.valid) << "Expected result.valid=false when step() returns None";

    bridge_.Shutdown();
}

// Additional: step() returns non-dict
TEST_F(PythonDriverBridgeTest, StepReturnsNonDict)
{
    WriteScript("returns_list.py", R"(
class ListController:
    def init(self, config):
        pass
    def step(self, frame_data):
        return [0.0, 0.0, 0.0, 1]
)");

    ASSERT_TRUE(InitBridge("returns_list.py", "ListController"))
        << "Initialize failed: " << bridge_.GetLastError();

    PythonFrameData frame_data;
    PythonDriverInput result = bridge_.CallStep(frame_data);

    EXPECT_FALSE(result.valid) << "Expected result.valid=false when step() returns non-dict";

    bridge_.Shutdown();
}

// Additional: Multiple step() calls
TEST_F(PythonDriverBridgeTest, MultipleStepCalls)
{
    WriteScript("counting_controller.py", R"(
class CountingController:
    def init(self, config):
        self.count = 0
    def step(self, frame_data):
        self.count += 1
        return {
            "throttle": self.count * 0.1,
            "brake": 0.0,
            "steering": 0.0,
            "gear": 1,
            "lights": {},
            "engine_brake": 0.49,
            "adas_states": []
        }
)");

    ASSERT_TRUE(InitBridge("counting_controller.py", "CountingController"))
        << "Initialize failed: " << bridge_.GetLastError();

    PythonFrameData frame_data;

    // First call
    PythonDriverInput result1 = bridge_.CallStep(frame_data);
    EXPECT_TRUE(result1.valid);
    EXPECT_NEAR(result1.throttle, 0.1, 1e-6);

    // Second call
    PythonDriverInput result2 = bridge_.CallStep(frame_data);
    EXPECT_TRUE(result2.valid);
    EXPECT_NEAR(result2.throttle, 0.2, 1e-6);

    // Third call
    PythonDriverInput result3 = bridge_.CallStep(frame_data);
    EXPECT_TRUE(result3.valid);
    EXPECT_NEAR(result3.throttle, 0.3, 1e-6);

    bridge_.Shutdown();
}

// Additional: Shutdown without Initialize
TEST_F(PythonDriverBridgeTest, ShutdownWithoutInit)
{
    // Should not crash
    bridge_.Shutdown();
    EXPECT_FALSE(bridge_.IsInitialized());
    SUCCEED();
}

// Additional: Shutdown twice
TEST_F(PythonDriverBridgeTest, ShutdownTwice)
{
    WriteScript("simple.py", R"(
class SimpleController:
    def init(self, config):
        pass
    def step(self, frame_data):
        return {"throttle": 0, "brake": 0, "steering": 0, "gear": 1, "lights": {}}
)");

    ASSERT_TRUE(InitBridge("simple.py", "SimpleController"));

    bridge_.Shutdown();
    EXPECT_FALSE(bridge_.IsInitialized());

    // Second shutdown should be safe
    bridge_.Shutdown();
    SUCCEED();
}

} // namespace test
} // namespace gt_esmini

#endif // GT_ENABLE_EMBEDDED_PYTHON
