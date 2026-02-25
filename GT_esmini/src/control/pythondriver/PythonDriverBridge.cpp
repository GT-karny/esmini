#ifdef GT_ENABLE_EMBEDDED_PYTHON

// Python.h must be included before any standard headers on Windows
#include <Python.h>

#include "gt_esmini/control/pythondriver/PythonDriverBridge.hpp"
#include "gt_esmini/control/ControllerRealDriver.hpp" // For WaypointData
#include "gt_esmini/control/realdriver/LonProfilePlanner.hpp" // For LonProfilePoint
#include "logger.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace gt_esmini
{
namespace
{
constexpr int kLightUnset = -1;
constexpr int kLightAuto = 0;
constexpr int kLightOff = 1;
constexpr int kLightOn = 2;

std::string EscapeJson(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 16);
    for (char c : s)
    {
        switch (c)
        {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out += c; break;
        }
    }
    return out;
}

const char* LightSlotToKey(PythonLightSlot slot)
{
    switch (slot)
    {
    case PythonLightSlot::LOW_BEAM: return "low_beam";
    case PythonLightSlot::HIGH_BEAM: return "high_beam";
    case PythonLightSlot::LEFT_INDICATOR: return "left_indicator";
    case PythonLightSlot::RIGHT_INDICATOR: return "right_indicator";
    case PythonLightSlot::FOG: return "fog";
    case PythonLightSlot::BRAKE: return "brake";
    case PythonLightSlot::REVERSE: return "reverse";
    default: return "unknown";
    }
}

const char* LightStateToString(int state)
{
    switch (state)
    {
    case kLightAuto: return "auto";
    case kLightOff: return "off";
    case kLightOn: return "on";
    default: return "unset";
    }
}
} // namespace

PythonDriverBridge::PythonDriverBridge() = default;

PythonDriverBridge::~PythonDriverBridge()
{
    Shutdown();
}

bool PythonDriverBridge::Initialize(
    const std::string& script_path,
    const std::string& class_name,
    const std::string& python_home,
    bool trace_enabled,
    const std::string& trace_dir,
    const std::string& xodr_path,
    double dt,
    int ego_id)
{
    fatal_error_ = false;
    last_error_.clear();
    trace_enabled_ = trace_enabled;
    trace_dir_ = trace_dir;

    if (trace_enabled_)
    {
        namespace fs = std::filesystem;
        fs::path out_dir = trace_dir_.empty() ? fs::current_path() : fs::path(trace_dir_);
        std::error_code ec;
        fs::create_directories(out_dir, ec);
        cpp_to_py_trace_.open((out_dir / "cpp_to_py_trace.jsonl").string(), std::ios::out | std::ios::trunc);
        py_to_cpp_trace_.open((out_dir / "py_to_cpp_trace.jsonl").string(), std::ios::out | std::ios::trunc);
        if (!cpp_to_py_trace_.is_open() || !py_to_cpp_trace_.is_open())
        {
            trace_enabled_ = false;
            LOG_WARN("PythonDriverBridge: trace requested but failed to open trace files in '{}'", out_dir.string());
        }
    }

    // 1. Set PYTHONHOME if specified (for embedded / venv support)
    if (!python_home.empty())
    {
        python_home_wide_.assign(python_home.begin(), python_home.end());
        Py_SetPythonHome(python_home_wide_.c_str());
    }

    // 2. Initialize Python interpreter if not already running
    if (!Py_IsInitialized())
    {
        Py_Initialize();
        interpreter_owned_ = true;
        LOG_INFO("PythonDriverBridge: Python interpreter initialized");
    }

    // 3. Setup sys.path to find user scripts and packages
    SetupSysPath(script_path, python_home);

    // 4. Import the user's script as a module
    std::filesystem::path p(script_path);
    std::string module_name = p.stem().string();

    PyObject* py_module_name = PyUnicode_FromString(module_name.c_str());
    script_module_ = PyImport_Import(py_module_name);
    Py_DECREF(py_module_name);

    if (!script_module_)
    {
        PyErr_Print();
        fatal_error_ = true;
        last_error_  = "Failed to import module '" + module_name + "'";
        LOG_ERROR("PythonDriverBridge: Failed to import module '{}'", module_name);
        return false;
    }

    // 5. Instantiate the controller class
    PyObject* py_class = PyObject_GetAttrString(script_module_, class_name.c_str());
    if (!py_class || !PyCallable_Check(py_class))
    {
        PyErr_Print();
        fatal_error_ = true;
        last_error_  = "Class '" + class_name + "' not found or not callable";
        LOG_ERROR("PythonDriverBridge: Class '{}' not found or not callable in module '{}'", class_name, module_name);
        Py_XDECREF(py_class);
        return false;
    }

    script_instance_ = PyObject_CallObject(py_class, nullptr);
    Py_DECREF(py_class);

    if (!script_instance_)
    {
        PyErr_Print();
        fatal_error_ = true;
        last_error_  = "Failed to instantiate class '" + class_name + "'";
        LOG_ERROR("PythonDriverBridge: Failed to instantiate class '{}'", class_name);
        return false;
    }

    // 6. Call init(config) with scenario configuration
    PyObject* config = PyDict_New();
    {
        PyObject* val;
        val = PyUnicode_FromString(xodr_path.c_str());
        PyDict_SetItemString(config, "xodr_path", val);
        Py_DECREF(val);

        val = PyFloat_FromDouble(dt);
        PyDict_SetItemString(config, "dt", val);
        Py_DECREF(val);

        val = PyUnicode_FromString(p.parent_path().string().c_str());
        PyDict_SetItemString(config, "script_dir", val);
        Py_DECREF(val);

        val = PyLong_FromLong(ego_id);
        PyDict_SetItemString(config, "ego_id", val);
        Py_DECREF(val);

        val = trace_enabled_ ? Py_True : Py_False;
        Py_INCREF(val);
        PyDict_SetItemString(config, "trace_enabled", val);
        Py_DECREF(val);

        val = PyUnicode_FromString(trace_dir_.c_str());
        PyDict_SetItemString(config, "trace_dir", val);
        Py_DECREF(val);
    }

    PyObject* init_result = PyObject_CallMethod(script_instance_, "init", "O", config);
    Py_DECREF(config);

    if (!init_result)
    {
        PyErr_Print();
        fatal_error_ = true;
        last_error_  = "init() raised exception";
        LOG_ERROR("PythonDriverBridge: init() raised an exception");
        return false;
    }
    Py_DECREF(init_result);

    initialized_ = true;
    LOG_INFO("PythonDriverBridge: Initialized successfully (script='{}', class='{}')", script_path, class_name);
    return true;
}

PythonDriverInput PythonDriverBridge::CallStep(const PythonFrameData& frame_data)
{
    PythonDriverInput input;
    if (!initialized_ || !script_instance_)
    {
        return input;
    }

    WriteCppToPyTrace(frame_data);

    PyObject* frame_dict = BuildFrameDict(frame_data);
    if (!frame_dict)
    {
        return input;
    }

    PyObject* result = PyObject_CallMethod(script_instance_, "step", "O", frame_dict);
    Py_DECREF(frame_dict);

    if (!result)
    {
        PyErr_Print();
        consecutive_errors_++;
        fatal_error_ = true;
        last_error_  = "step() raised exception";
        WritePyToCppTrace(frame_data.frame_id, nullptr, input, "step_exception");
        if (consecutive_errors_ == 1 || consecutive_errors_ % 100 == 0)
        {
            LOG_ERROR("PythonDriverBridge: step() exception (#{} consecutive)", consecutive_errors_);
        }
        return input;
    }

    consecutive_errors_ = 0;
    input = ParseResult(result);
    WritePyToCppTrace(frame_data.frame_id, result, input, input.valid ? nullptr : "invalid_result");
    Py_DECREF(result);
    return input;
}

PyObject* PythonDriverBridge::BuildFrameDict(const PythonFrameData& data)
{
    PyObject* dict = PyDict_New();
    if (!dict)
    {
        return nullptr;
    }

    // Ground truth as bytes
    if (data.ground_truth_bytes && data.ground_truth_size > 0)
    {
        PyObject* gt_bytes = PyBytes_FromStringAndSize(data.ground_truth_bytes, data.ground_truth_size);
        PyDict_SetItemString(dict, "ground_truth_bytes", gt_bytes);
        Py_DECREF(gt_bytes);
    }
    else
    {
        Py_INCREF(Py_None);
        PyDict_SetItemString(dict, "ground_truth_bytes", Py_None);
    }

    // Waypoints as list of dicts
    {
        PyObject* wp_list = PyList_New(0);
        if (data.waypoints)
        {
            for (const auto& wp : *data.waypoints)
            {
                PyObject* wp_dict = PyDict_New();

                PyObject* val;
                val = PyFloat_FromDouble(wp.x);
                PyDict_SetItemString(wp_dict, "x", val); Py_DECREF(val);
                val = PyFloat_FromDouble(wp.y);
                PyDict_SetItemString(wp_dict, "y", val); Py_DECREF(val);
                val = PyFloat_FromDouble(wp.h);
                PyDict_SetItemString(wp_dict, "h", val); Py_DECREF(val);
                val = PyLong_FromUnsignedLong(wp.roadId);
                PyDict_SetItemString(wp_dict, "road_id", val); Py_DECREF(val);
                val = PyFloat_FromDouble(wp.s);
                PyDict_SetItemString(wp_dict, "s", val); Py_DECREF(val);
                val = PyLong_FromLong(wp.laneId);
                PyDict_SetItemString(wp_dict, "lane_id", val); Py_DECREF(val);
                val = PyFloat_FromDouble(wp.laneOffset);
                PyDict_SetItemString(wp_dict, "lane_offset", val); Py_DECREF(val);

                PyList_Append(wp_list, wp_dict);
                Py_DECREF(wp_dict);
            }
        }
        PyDict_SetItemString(dict, "waypoints", wp_list);
        Py_DECREF(wp_list);
    }

    {
        PyObject* val = PyLong_FromLong(data.waypoint_index);
        PyDict_SetItemString(dict, "waypoint_index", val);
        Py_DECREF(val);
    }

    {
        PyObject* generation = PyDict_New();
        PyObject* val = PyLong_FromUnsignedLongLong(static_cast<unsigned long long>(data.waypoint_generation_version));
        PyDict_SetItemString(generation, "version", val);
        Py_DECREF(val);
        PyDict_SetItemString(dict, "waypoint_generation", generation);
        Py_DECREF(generation);
    }

    {
        PyObject* actions = PyDict_New();
        PyObject* val = nullptr;

        val = data.actions.assign_route ? Py_True : Py_False;
        Py_INCREF(val);
        PyDict_SetItemString(actions, "assign_route", val);
        Py_DECREF(val);

        val = data.actions.lane_change ? Py_True : Py_False;
        Py_INCREF(val);
        PyDict_SetItemString(actions, "lane_change", val);
        Py_DECREF(val);

        if (data.actions.has_lane_change_target_lane)
        {
            val = PyLong_FromLong(data.actions.lane_change_target_lane);
        }
        else
        {
            val = Py_None;
            Py_INCREF(val);
        }
        PyDict_SetItemString(actions, "lane_change_target_lane", val);
        Py_DECREF(val);

        val = data.actions.lane_offset ? Py_True : Py_False;
        Py_INCREF(val);
        PyDict_SetItemString(actions, "lane_offset", val);
        Py_DECREF(val);

        if (data.actions.has_lane_offset_target_m)
        {
            val = PyFloat_FromDouble(data.actions.lane_offset_target_m);
        }
        else
        {
            val = Py_None;
            Py_INCREF(val);
        }
        PyDict_SetItemString(actions, "lane_offset_target_m", val);
        Py_DECREF(val);

        val = data.actions.follow_trajectory ? Py_True : Py_False;
        Py_INCREF(val);
        PyDict_SetItemString(actions, "follow_trajectory", val);
        Py_DECREF(val);

        val = data.actions.longitudinal_distance ? Py_True : Py_False;
        Py_INCREF(val);
        PyDict_SetItemString(actions, "longitudinal_distance", val);
        Py_DECREF(val);

        val = data.actions.speed_profile ? Py_True : Py_False;
        Py_INCREF(val);
        PyDict_SetItemString(actions, "speed_profile", val);
        Py_DECREF(val);

        val = data.actions.synchronize ? Py_True : Py_False;
        Py_INCREF(val);
        PyDict_SetItemString(actions, "synchronize", val);
        Py_DECREF(val);

        PyDict_SetItemString(dict, "actions", actions);
        Py_DECREF(actions);
    }

    // Longitudinal profile as list of dicts
    {
        PyObject* lp_list = PyList_New(0);
        if (data.lon_profile)
        {
            for (const auto& pt : *data.lon_profile)
            {
                PyObject* pt_dict = PyDict_New();

                PyObject* val;
                val = PyFloat_FromDouble(pt.t_offset);
                PyDict_SetItemString(pt_dict, "t_offset", val); Py_DECREF(val);
                val = PyFloat_FromDouble(pt.v_target);
                PyDict_SetItemString(pt_dict, "v_target", val); Py_DECREF(val);
                val = PyFloat_FromDouble(pt.a_max);
                PyDict_SetItemString(pt_dict, "a_max", val); Py_DECREF(val);
                val = PyFloat_FromDouble(pt.j_max);
                PyDict_SetItemString(pt_dict, "j_max", val); Py_DECREF(val);

                PyList_Append(lp_list, pt_dict);
                Py_DECREF(pt_dict);
            }
        }
        PyDict_SetItemString(dict, "lon_profile", lp_list);
        Py_DECREF(lp_list);
    }

    // Scalar values
    {
        PyObject* val;
        val = PyLong_FromUnsignedLongLong(static_cast<unsigned long long>(data.frame_id));
        PyDict_SetItemString(dict, "frame_id", val); Py_DECREF(val);
        val = PyFloat_FromDouble(data.set_speed);
        PyDict_SetItemString(dict, "set_speed", val); Py_DECREF(val);
        val = PyFloat_FromDouble(data.current_speed);
        PyDict_SetItemString(dict, "current_speed", val); Py_DECREF(val);
        val = PyFloat_FromDouble(data.dt);
        PyDict_SetItemString(dict, "dt", val); Py_DECREF(val);
    }

    return dict;
}

void PythonDriverBridge::WriteCppToPyTrace(const PythonFrameData& data)
{
    if (!trace_enabled_ || !cpp_to_py_trace_.is_open())
    {
        return;
    }

    const std::size_t waypoint_count = data.waypoints ? data.waypoints->size() : 0U;
    const std::size_t lon_profile_count = data.lon_profile ? data.lon_profile->size() : 0U;
    cpp_to_py_trace_
        << "{\"frame_id\":" << data.frame_id
        << ",\"gt_size\":" << data.ground_truth_size
        << ",\"waypoint_count\":" << waypoint_count
        << ",\"waypoint_index\":" << data.waypoint_index
        << ",\"waypoint_generation_version\":" << data.waypoint_generation_version
        << ",\"actions\":{"
        << "\"assign_route\":" << (data.actions.assign_route ? "true" : "false") << ","
        << "\"lane_change\":" << (data.actions.lane_change ? "true" : "false") << ","
        << "\"lane_offset\":" << (data.actions.lane_offset ? "true" : "false") << ","
        << "\"follow_trajectory\":" << (data.actions.follow_trajectory ? "true" : "false") << ","
        << "\"longitudinal_distance\":" << (data.actions.longitudinal_distance ? "true" : "false") << ","
        << "\"speed_profile\":" << (data.actions.speed_profile ? "true" : "false") << ","
        << "\"synchronize\":" << (data.actions.synchronize ? "true" : "false")
        << "}"
        << ",\"lon_profile_count\":" << lon_profile_count
        << ",\"set_speed\":" << data.set_speed
        << ",\"current_speed\":" << data.current_speed
        << ",\"dt\":" << data.dt
        << "}\n";
}

void PythonDriverBridge::WritePyToCppTrace(std::size_t frame_id, PyObject* result, const PythonDriverInput& input, const char* error)
{
    if (!trace_enabled_ || !py_to_cpp_trace_.is_open())
    {
        return;
    }

    std::vector<std::string> keys;
    auto has_key = [&](const char* key) -> bool {
        if (!result || !PyDict_Check(result))
        {
            return false;
        }
        PyObject* v = PyDict_GetItemString(result, key);
        return v != nullptr;
    };

    if (result && PyDict_Check(result))
    {
        PyObject *k = nullptr, *v = nullptr;
        Py_ssize_t pos = 0;
        while (PyDict_Next(result, &pos, &k, &v))
        {
            if (PyUnicode_Check(k))
            {
                keys.emplace_back(PyUnicode_AsUTF8(k));
            }
        }
    }

    py_to_cpp_trace_ << "{\"frame_id\":" << frame_id;
    if (error)
    {
        py_to_cpp_trace_ << ",\"error\":\"" << EscapeJson(error) << "\"";
    }
    py_to_cpp_trace_ << ",\"result_keys\":[";
    for (std::size_t i = 0; i < keys.size(); ++i)
    {
        if (i > 0)
        {
            py_to_cpp_trace_ << ",";
        }
        py_to_cpp_trace_ << "\"" << EscapeJson(keys[i]) << "\"";
    }
    py_to_cpp_trace_ << "]"
        << ",\"required_keys\":{"
        << "\"throttle\":" << (has_key("throttle") ? "true" : "false") << ","
        << "\"brake\":" << (has_key("brake") ? "true" : "false") << ","
        << "\"steering\":" << (has_key("steering") ? "true" : "false") << ","
        << "\"gear\":" << (has_key("gear") ? "true" : "false") << ","
        << "\"lights\":" << (has_key("lights") ? "true" : "false")
        << "}"
        << ",\"parsed\":{"
        << "\"throttle\":" << input.throttle << ","
        << "\"brake\":" << input.brake << ","
        << "\"steering\":" << input.steering << ","
        << "\"gear\":" << input.gear << ","
        << "\"lights\":{";
    for (std::size_t i = 0; i < static_cast<std::size_t>(PythonLightSlot::COUNT); ++i)
    {
        if (i > 0)
        {
            py_to_cpp_trace_ << ",";
        }
        const auto slot = static_cast<PythonLightSlot>(i);
        py_to_cpp_trace_ << "\"" << LightSlotToKey(slot) << "\":\"" << LightStateToString(input.lights.states[i]) << "\"";
    }
    py_to_cpp_trace_
        << "},"
        << "\"engine_brake\":" << input.engineBrake << ","
        << "\"adas_count\":" << input.adasStates.size()
        << "}"
        << ",\"valid\":" << (input.valid ? "true" : "false")
        << "}\n";
}

PythonDriverInput PythonDriverBridge::ParseResult(PyObject* result)
{
    PythonDriverInput input;
    if (!PyDict_Check(result))
    {
        LOG_WARN("PythonDriverBridge: step() did not return a dict");
        return input;
    }

    auto get_double = [&](const char* key, double default_val) -> double {
        PyObject* val = PyDict_GetItemString(result, key); // borrowed ref
        if (val && PyFloat_Check(val)) return PyFloat_AsDouble(val);
        if (val && PyLong_Check(val))  return static_cast<double>(PyLong_AsLong(val));
        return default_val;
    };

    auto get_int = [&](const char* key, int default_val) -> int {
        PyObject* val = PyDict_GetItemString(result, key); // borrowed ref
        if (val && PyLong_Check(val))  return static_cast<int>(PyLong_AsLong(val));
        if (val && PyFloat_Check(val)) return static_cast<int>(PyFloat_AsDouble(val));
        return default_val;
    };

    input.throttle    = get_double("throttle", 0.0);
    input.brake       = get_double("brake", 0.0);
    input.steering    = get_double("steering", 0.0);
    input.gear        = get_int("gear", 1);
    input.engineBrake = get_double("engine_brake", 0.49);

    PyObject* lights = PyDict_GetItemString(result, "lights"); // borrowed ref
    if (!lights || !PyDict_Check(lights))
    {
        LOG_WARN("PythonDriverBridge: step() result is missing required dict key 'lights'");
        return input;
    }

    auto parse_light_state = [&](const char* key, PythonLightSlot slot) -> bool {
        PyObject* val = PyDict_GetItemString(lights, key); // borrowed ref
        if (!val)
        {
            return true; // not specified: keep unset
        }
        if (!PyUnicode_Check(val))
        {
            LOG_WARN("PythonDriverBridge: lights.{} must be a string", key);
            return false;
        }

        const char* raw = PyUnicode_AsUTF8(val);
        if (!raw)
        {
            return false;
        }

        std::string token(raw);
        std::transform(token.begin(), token.end(), token.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const std::size_t idx = static_cast<std::size_t>(slot);
        if (token == "auto")
        {
            input.lights.states[idx] = kLightAuto;
        }
        else if (token == "off")
        {
            input.lights.states[idx] = kLightOff;
        }
        else if (token == "on")
        {
            input.lights.states[idx] = kLightOn;
        }
        else
        {
            LOG_WARN("PythonDriverBridge: lights.{} has invalid value '{}'", key, token);
            return false;
        }
        return true;
    };

    if (!parse_light_state("low_beam", PythonLightSlot::LOW_BEAM) ||
        !parse_light_state("high_beam", PythonLightSlot::HIGH_BEAM) ||
        !parse_light_state("left_indicator", PythonLightSlot::LEFT_INDICATOR) ||
        !parse_light_state("right_indicator", PythonLightSlot::RIGHT_INDICATOR) ||
        !parse_light_state("fog", PythonLightSlot::FOG) ||
        !parse_light_state("brake", PythonLightSlot::BRAKE) ||
        !parse_light_state("reverse", PythonLightSlot::REVERSE))
    {
        return input;
    }

    input.valid       = true;

    // Optional: ADAS states
    PyObject* adas = PyDict_GetItemString(result, "adas_states"); // borrowed ref
    if (adas && PyList_Check(adas))
    {
        Py_ssize_t size = PyList_Size(adas);
        input.adasStates.resize(size);
        for (Py_ssize_t i = 0; i < size; i++)
        {
            PyObject* item = PyList_GetItem(adas, i); // borrowed ref
            input.adasStates[i] = PyLong_Check(item) ? static_cast<int>(PyLong_AsLong(item)) : 0;
        }
    }

    return input;
}

void PythonDriverBridge::SetupSysPath(const std::string& script_path, const std::string& python_home)
{
    namespace fs = std::filesystem;

    // Collect paths to add to sys.path
    std::vector<std::string> paths_to_add;

    // 1. The directory containing the user script
    fs::path script_dir = fs::path(script_path).parent_path();
    paths_to_add.push_back(script_dir.string());

    // 2. DriverScript/ root (parent of realdriver/) for 'realdriver' package imports
    //    Walk up from script_dir to find DriverScript/
    for (auto dir = script_dir; dir.has_parent_path() && dir != dir.root_path(); dir = dir.parent_path())
    {
        if (dir.filename().string() == "DriverScript")
        {
            paths_to_add.push_back(dir.string());
            break;
        }
    }

    // 3. osi3 package path (from DriverScript or scripts directory)
    //    Look for DriverScript/osi3/ relative to script_dir
    {
        fs::path osi_dir = script_dir / ".." / "osi3";
        if (fs::exists(osi_dir))
        {
            paths_to_add.push_back(osi_dir.parent_path().string());
        }
    }

    // 4. If using embedded Python, add python312.zip (critical for encodings module)
    //    Py_SetPythonHome() disables python312._pth processing, so we must manually add
    //    the standard library ZIP to sys.path
    if (!python_home.empty())
    {
        fs::path python_zip = fs::path(python_home) / "python312.zip";
        if (fs::exists(python_zip))
        {
            // Add at the beginning to match python312._pth behavior
            paths_to_add.insert(paths_to_add.begin(), python_zip.string());
        }
        else
        {
            LOG_WARN("PythonDriverBridge: python312.zip not found at '{}', encodings module may fail", python_zip.string());
        }

        // Also add site-packages path
        fs::path site_packages = fs::path(python_home) / "Lib" / "site-packages";
        if (fs::exists(site_packages))
        {
            paths_to_add.push_back(site_packages.string());
        }
    }

    // Build Python code to add paths to sys.path
    std::string code = "import sys\n";

    // Remove incorrect python312.zip path added by Py_Initialize()
    // (Py_Initialize adds <executable_dir>/python312.zip, but we need <python_home>/python312.zip)
    if (!python_home.empty())
    {
        code += "sys.path = [p for p in sys.path if not p.endswith('python312.zip')]\n";
    }

    for (const auto& p : paths_to_add)
    {
        // Normalize path separators for Python
        std::string normalized = p;
        for (auto& c : normalized)
        {
            if (c == '\\') c = '/';
        }
        code += "if '" + normalized + "' not in sys.path: sys.path.insert(0, '" + normalized + "')\n";
    }

    PyRun_SimpleString(code.c_str());
}

void PythonDriverBridge::Shutdown()
{
    // Only clean up Python objects if interpreter is still initialized
    if (Py_IsInitialized())
    {
        if (script_instance_)
        {
            // Call close() if the method exists
            if (PyObject_HasAttrString(script_instance_, "close"))
            {
                PyObject* r = PyObject_CallMethod(script_instance_, "close", nullptr);
                if (r)
                {
                    Py_DECREF(r);
                }
                else
                {
                    PyErr_Print();
                }
            }
            Py_DECREF(script_instance_);
            script_instance_ = nullptr;
        }

        if (script_module_)
        {
            Py_DECREF(script_module_);
            script_module_ = nullptr;
        }
    }
    else
    {
        // Python interpreter already finalized, just null out the pointers without calling Python API
        script_instance_ = nullptr;
        script_module_ = nullptr;
    }

    // Clear any pending Python errors before finalization
    if (Py_IsInitialized())
    {
        PyErr_Clear();
    }

    // Close trace files BEFORE Python finalization to avoid access violations during DLL unload
    if (cpp_to_py_trace_.is_open())
    {
        cpp_to_py_trace_.close();
    }
    if (py_to_cpp_trace_.is_open())
    {
        py_to_cpp_trace_.close();
    }

    // Finalize Python interpreter with error handling
    if (interpreter_owned_ && Py_IsInitialized())
    {
        int finalize_result = Py_FinalizeEx();
        if (finalize_result < 0)
        {
            LOG_ERROR("PythonDriverBridge: Python finalization encountered errors");
        }
        else
        {
            LOG_INFO("PythonDriverBridge: Python interpreter finalized successfully");
        }
        interpreter_owned_ = false;
    }

    initialized_ = false;
    fatal_error_ = false;
    last_error_.clear();
}

} // namespace gt_esmini

#endif // GT_ENABLE_EMBEDDED_PYTHON
