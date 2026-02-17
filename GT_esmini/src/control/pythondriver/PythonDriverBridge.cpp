#ifdef GT_ENABLE_EMBEDDED_PYTHON

// Python.h must be included before any standard headers on Windows
#include <Python.h>

#include "gt_esmini/control/pythondriver/PythonDriverBridge.hpp"
#include "gt_esmini/control/ControllerRealDriver.hpp" // For WaypointData
#include "gt_esmini/control/realdriver/LonProfilePlanner.hpp" // For LonProfilePoint
#include "logger.hpp"

#include <filesystem>
#include <iostream>

namespace gt_esmini
{

PythonDriverBridge::PythonDriverBridge() = default;

PythonDriverBridge::~PythonDriverBridge()
{
    Shutdown();
}

bool PythonDriverBridge::Initialize(
    const std::string& script_path,
    const std::string& class_name,
    const std::string& python_home,
    const std::string& xodr_path,
    double dt,
    int ego_id)
{
    fatal_error_ = false;
    last_error_.clear();

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
        if (consecutive_errors_ == 1 || consecutive_errors_ % 100 == 0)
        {
            LOG_ERROR("PythonDriverBridge: step() exception (#{} consecutive)", consecutive_errors_);
        }
        return input;
    }

    consecutive_errors_ = 0;
    input = ParseResult(result);
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
        val = PyFloat_FromDouble(data.set_speed);
        PyDict_SetItemString(dict, "set_speed", val); Py_DECREF(val);
        val = PyFloat_FromDouble(data.current_speed);
        PyDict_SetItemString(dict, "current_speed", val); Py_DECREF(val);
        val = PyFloat_FromDouble(data.dt);
        PyDict_SetItemString(dict, "dt", val); Py_DECREF(val);
    }

    return dict;
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
    input.lightMask   = get_int("light_mask", 0);
    input.engineBrake = get_double("engine_brake", 0.49);
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

    // 4. If using embedded Python, add site-packages path
    if (!python_home.empty())
    {
        fs::path site_packages = fs::path(python_home) / "Lib" / "site-packages";
        if (fs::exists(site_packages))
        {
            paths_to_add.push_back(site_packages.string());
        }
    }

    // Build Python code to add paths to sys.path
    std::string code = "import sys\n";
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

    if (interpreter_owned_ && Py_IsInitialized())
    {
        Py_FinalizeEx();
        interpreter_owned_ = false;
        LOG_INFO("PythonDriverBridge: Python interpreter finalized");
    }

    initialized_ = false;
    fatal_error_ = false;
    last_error_.clear();
}

} // namespace gt_esmini

#endif // GT_ENABLE_EMBEDDED_PYTHON
