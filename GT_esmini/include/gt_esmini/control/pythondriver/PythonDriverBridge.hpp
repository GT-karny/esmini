#pragma once

#ifdef GT_ENABLE_EMBEDDED_PYTHON

#include <string>
#include <fstream>
#include <cstddef>
#include <vector>
#include <array>

// Forward declare Python types to avoid including Python.h in headers
struct _object;
typedef _object PyObject;

namespace gt_esmini
{

struct WaypointData;
struct LonProfilePoint;

enum class PythonLightSlot : std::size_t
{
    LOW_BEAM = 0,
    HIGH_BEAM,
    LEFT_INDICATOR,
    RIGHT_INDICATOR,
    FOG,
    BRAKE,
    REVERSE,
    COUNT
};

struct PythonLightsPatch
{
    // -1: not specified, 0: auto, 1: off, 2: on
    std::array<int, static_cast<std::size_t>(PythonLightSlot::COUNT)> states{
        -1, -1, -1, -1, -1, -1, -1
    };
};

struct PythonDriverInput
{
    double throttle   = 0.0;
    double brake      = 0.0;
    double steering   = 0.0;
    int    gear       = 1;
    PythonLightsPatch lights;
    double engineBrake = 0.49;
    std::vector<int> adasStates;
    bool   valid      = false;
};

struct PythonFrameData
{
    struct ActionContext
    {
        bool assign_route = false;
        bool lane_change = false;
        int lane_change_target_lane = 0;
        bool has_lane_change_target_lane = false;
        bool lane_offset = false;
        double lane_offset_target_m = 0.0;
        bool has_lane_offset_target_m = false;
        bool follow_trajectory = false;
        bool longitudinal_distance = false;
        bool speed_profile = false;
        bool synchronize = false;
    };

    std::size_t frame_id = 0;
    const char* ground_truth_bytes = nullptr;
    int         ground_truth_size  = 0;
    const std::vector<WaypointData>*    waypoints     = nullptr;
    int                                 waypoint_index = 0;
    std::size_t                         waypoint_generation_version = 0;
    ActionContext                       actions;
    const std::vector<LonProfilePoint>* lon_profile    = nullptr;
    double set_speed     = 0.0;
    double current_speed = 0.0;
    double dt            = 0.0;
};

class PythonDriverBridge
{
public:
    PythonDriverBridge();
    ~PythonDriverBridge();

    /// Initialize the Python interpreter and load the user script.
    /// @param script_path   Absolute path to the .py file
    /// @param class_name    Name of the controller class inside the script
    /// @param python_home   PYTHONHOME override (empty = use default / embedded)
    /// @param xodr_path     Path to the OpenDRIVE file for the scenario
    /// @param dt            Nominal time step (seconds)
    /// @param ego_id        Object ID of the ego vehicle
    /// @return true on success
    bool Initialize(const std::string& script_path,
                    const std::string& class_name,
                    const std::string& python_home,
                    bool trace_enabled,
                    const std::string& trace_dir,
                    const std::string& xodr_path,
                    double dt,
                    int ego_id);

    /// Call the Python controller's step() method with the current frame data.
    PythonDriverInput CallStep(const PythonFrameData& frame_data);

    /// Shutdown the Python interpreter and release resources.
    void Shutdown();

    bool IsInitialized() const { return initialized_; }
    bool HasFatalError() const { return fatal_error_; }
    const std::string& GetLastError() const { return last_error_; }

private:
    PyObject* BuildFrameDict(const PythonFrameData& data);
    PythonDriverInput ParseResult(PyObject* result);
    void SetupSysPath(const std::string& script_path, const std::string& python_home);
    void WriteCppToPyTrace(const PythonFrameData& data);
    void WritePyToCppTrace(std::size_t frame_id, PyObject* result, const PythonDriverInput& input, const char* error = nullptr);

    PyObject* script_module_   = nullptr;
    PyObject* script_instance_ = nullptr;
    bool initialized_          = false;
    bool interpreter_owned_    = false;
    int  consecutive_errors_   = 0;
    bool fatal_error_          = false;
    std::string last_error_;
    bool trace_enabled_        = false;
    std::string trace_dir_;
    std::ofstream cpp_to_py_trace_;
    std::ofstream py_to_cpp_trace_;

    // Store wide string for Py_SetPythonHome lifetime
    std::wstring python_home_wide_;
};

} // namespace gt_esmini

#endif // GT_ENABLE_EMBEDDED_PYTHON
