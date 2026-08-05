/*
 * GT_esmini - Extended esmini with Light Functionality
 * https://github.com/esmini/esmini
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2024 GT_esmini contributors
 */

// Include esminiLib.cpp to access static 'player' and 'scenarioEngine'
// This effectively compiles esminiLib code as part of this module
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4244)  // SE_SOCKET → int narrowing in upstream code
#endif
#include "../../../EnvironmentSimulator/Libraries/esminiLib/esminiLib.cpp"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <gt_esmini/core/GT_esminiLib.hpp>
#include "gt_esmini/core/ConfigLoader.hpp"
#include "gt_esmini/scenario/GT_ScenarioReader.hpp"
#include "gt_esmini/control/AutoLightController.hpp"
#include "gt_esmini/control/HeadlightLogic.hpp" // F6 headlight config
#include "gt_esmini/common/SimpleJson.hpp"      // F6 auto_light.json parsing
#include "gt_esmini/scenario/ExtraEntities.hpp" // For VehicleExtensionManager

#include <vector>
#include <memory>
#include <functional>
#include <string>
#include <sstream>
#include <cstring>
#include <iostream>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <cstdint>
#include <cstdlib>
#include <chrono>
#include <mutex>
#include <osi_groundtruth.pb.h>

#include "gt_esmini/control/ControllerRealDriver.hpp"
#ifdef GT_ENABLE_EMBEDDED_PYTHON
#include "gt_esmini/control/ControllerPythonDriver.hpp"
#endif
#include "gt_esmini/control/ControllerManualDrive.hpp"
#include "gt_esmini/control/ControllerKinematic.hpp"
#include "gt_esmini/control/ControllerRouteDrive.hpp"
#include "gt_esmini/control/ControllerVirtualDriver.hpp"
#include "gt_esmini/control/virtualdriver/VirtualDriverTelemetryJson.hpp"
#include "gt_esmini/osi/GT_HostVehicleReporter.hpp"
#include "gt_esmini/osi/HVDEstimator.hpp"
#include "gt_esmini/io/GT_ScenarioVariablesReporter.hpp"
#include "gt_esmini/io/GT_VirtualDriverReporter.hpp"
#include "gt_esmini/scenario/TrafficSignalController.hpp"
#include "gt_esmini/control/VehiclePhysicsManager.hpp"
#include "gt_esmini/control/common/DomainOwnershipLedger.hpp"
#include "gt_esmini/control/HeadingCorrectionManager.hpp"

#include "gt_esmini/control/common/ModuleDirectory.hpp"
#include "gt_esmini/control/ControllerRealDriverUtils.hpp"

namespace
{
template <typename ControllerT>
ControllerT* GT_FindControllerOfType(scenarioengine::Object* object)
{
    if (!object) return nullptr;
    for (auto* controller : object->controllers_)
    {
        if (auto* typed = dynamic_cast<ControllerT*>(controller))
            return typed;
    }
    return nullptr;
}

scenarioengine::Controller* GT_FindControllerByIdentity(scenarioengine::Object* object, const void* identity)
{
    if (!object || !identity) return nullptr;
    for (auto* controller : object->controllers_)
    {
        if (controller == identity)
            return controller;
    }
    return nullptr;
}

scenarioengine::Controller* GT_FindHvdSource(scenarioengine::Object* object)
{
    if (!object) return nullptr;

    // The ledger identifies the sole controller permitted to integrate this
    // object. Prefer it over controller names or declaration order.
    if (auto* integrator = GT_FindControllerByIdentity(
            object, gt_esmini::DomainOwnershipLedger::Instance().IntegratorOf(object->GetId())))
        return integrator;

    // No GT ownership entry: retain the old feature coverage, but resolve by
    // concrete type so an XOSC Controller name cannot silence the telemetry.
    if (auto* ctrl = GT_FindControllerOfType<gt_esmini::ControllerRealDriver>(object)) return ctrl;
#ifdef GT_ENABLE_EMBEDDED_PYTHON
    if (auto* ctrl = GT_FindControllerOfType<gt_esmini::ControllerPythonDriver>(object)) return ctrl;
#endif
    if (auto* ctrl = GT_FindControllerOfType<gt_esmini::ControllerManualDrive>(object)) return ctrl;
    return GT_FindControllerOfType<gt_esmini::ControllerVirtualDriver>(object);
}
} // namespace

// ============ Pin the fixed 24-slot ADAS table to the real OSI enum ============
// `control` must not depend on `osi` (GT_esmini/CLAUDE.md §2), so
// ControllerRealDriverUtils.hpp mirrors the OSI Name values as plain ints. This
// translation unit is the one place that sees both, so the mirror is verified
// here: if OSI renumbers the enum, the build breaks instead of the stream being
// silently mislabeled. Same technique as the VD-side osi_adas pin below.
namespace
{
using GtOsiAdasName = osi3::HostVehicleData_VehicleAutomatedDrivingFunction_Name;

constexpr bool AdasSlotTableMatchesOsi()
{
    constexpr GtOsiAdasName kOsiOrder[] = {
        osi3::HostVehicleData_VehicleAutomatedDrivingFunction_Name_NAME_BLIND_SPOT_WARNING,
        osi3::HostVehicleData_VehicleAutomatedDrivingFunction_Name_NAME_FORWARD_COLLISION_WARNING,
        osi3::HostVehicleData_VehicleAutomatedDrivingFunction_Name_NAME_LANE_DEPARTURE_WARNING,
        osi3::HostVehicleData_VehicleAutomatedDrivingFunction_Name_NAME_PARKING_COLLISION_WARNING,
        osi3::HostVehicleData_VehicleAutomatedDrivingFunction_Name_NAME_REAR_CROSS_TRAFFIC_WARNING,
        osi3::HostVehicleData_VehicleAutomatedDrivingFunction_Name_NAME_AUTOMATIC_EMERGENCY_BRAKING,
        osi3::HostVehicleData_VehicleAutomatedDrivingFunction_Name_NAME_AUTOMATIC_EMERGENCY_STEERING,
        osi3::HostVehicleData_VehicleAutomatedDrivingFunction_Name_NAME_REVERSE_AUTOMATIC_EMERGENCY_BRAKING,
        osi3::HostVehicleData_VehicleAutomatedDrivingFunction_Name_NAME_ADAPTIVE_CRUISE_CONTROL,
        osi3::HostVehicleData_VehicleAutomatedDrivingFunction_Name_NAME_LANE_KEEPING_ASSIST,
        osi3::HostVehicleData_VehicleAutomatedDrivingFunction_Name_NAME_ACTIVE_DRIVING_ASSISTANCE,
        osi3::HostVehicleData_VehicleAutomatedDrivingFunction_Name_NAME_BACKUP_CAMERA,
        osi3::HostVehicleData_VehicleAutomatedDrivingFunction_Name_NAME_SURROUND_VIEW_CAMERA,
        osi3::HostVehicleData_VehicleAutomatedDrivingFunction_Name_NAME_NIGHT_VISION,
        osi3::HostVehicleData_VehicleAutomatedDrivingFunction_Name_NAME_HEAD_UP_DISPLAY,
        osi3::HostVehicleData_VehicleAutomatedDrivingFunction_Name_NAME_ACTIVE_PARKING_ASSISTANCE,
        osi3::HostVehicleData_VehicleAutomatedDrivingFunction_Name_NAME_REMOTE_PARKING_ASSISTANCE,
        osi3::HostVehicleData_VehicleAutomatedDrivingFunction_Name_NAME_TRAILER_ASSISTANCE,
        osi3::HostVehicleData_VehicleAutomatedDrivingFunction_Name_NAME_AUTOMATIC_HIGH_BEAMS,
        osi3::HostVehicleData_VehicleAutomatedDrivingFunction_Name_NAME_DRIVER_MONITORING,
        osi3::HostVehicleData_VehicleAutomatedDrivingFunction_Name_NAME_URBAN_DRIVING,
        osi3::HostVehicleData_VehicleAutomatedDrivingFunction_Name_NAME_HIGHWAY_AUTOPILOT,
        osi3::HostVehicleData_VehicleAutomatedDrivingFunction_Name_NAME_CRUISE_CONTROL,
        osi3::HostVehicleData_VehicleAutomatedDrivingFunction_Name_NAME_SPEED_LIMIT_CONTROL,
    };
    static_assert(sizeof(kOsiOrder) / sizeof(kOsiOrder[0]) == gt_esmini::realdetail::kAdasFunctionCount,
                  "ADAS slot table size drifted from the OSI name list");

    for (std::size_t i = 0; i < gt_esmini::realdetail::kAdasFunctionCount; ++i)
    {
        if (gt_esmini::realdetail::kAdasSlots[i].osi_name != static_cast<int>(kOsiOrder[i]))
        {
            return false;
        }
    }
    return true;
}
static_assert(AdasSlotTableMatchesOsi(),
              "OSI Name enum drift: kAdasSlots no longer matches osi_hostvehicledata.proto");
}  // namespace

// ============ Pin the ManualDrive-ADAS osi_adas::Name additions to the real OSI enum ====
// req-vd-ad:REQ-AD-025 REQ-AD-028, vd-func:FUNC-075 (design doc
// manualdrive_adas_design.md §8-2) added 4 mirrored `Name` values to
// gt_esmini::osi_adas (AdasFunctionReport.hpp) for the ManualDrive ADAS report
// path (FCW/LDW/LKA/MSL). That header stays OSI-free on purpose (`control`
// must not depend on `osi`, GT_esmini/CLAUDE.md §2), so, same as
// AdasSlotTableMatchesOsi() above, this is the one place that sees both and
// can verify the mirror: if OSI renumbers the enum, the build breaks instead
// of the stream being silently mislabeled.
//
// NOTE: the pre-existing pins for NAME_OTHER / NAME_AUTOMATIC_EMERGENCY_BRAKING
// (AEB) / NAME_ADAPTIVE_CRUISE_CONTROL (ACC) / NAME_URBAN_DRIVING and the 3
// State values live further down, INSIDE the VirtualDriver controller-dispatch
// branch — that region belongs to another agent and is intentionally left
// untouched here. These 4 are pinned at FILE SCOPE instead, specifically so
// they are checked regardless of which dispatch branch runs (they back the
// ManualDrive report path, §8-1/§8-2, not the VirtualDriver one that owns the
// branch further down).
static_assert(
    gt_esmini::osi_adas::NAME_FORWARD_COLLISION_WARNING ==
        static_cast<int>(osi3::HostVehicleData_VehicleAutomatedDrivingFunction_Name_NAME_FORWARD_COLLISION_WARNING),
    "OSI Name enum drift: NAME_FORWARD_COLLISION_WARNING");
static_assert(
    gt_esmini::osi_adas::NAME_LANE_DEPARTURE_WARNING ==
        static_cast<int>(osi3::HostVehicleData_VehicleAutomatedDrivingFunction_Name_NAME_LANE_DEPARTURE_WARNING),
    "OSI Name enum drift: NAME_LANE_DEPARTURE_WARNING");
static_assert(
    gt_esmini::osi_adas::NAME_LANE_KEEPING_ASSIST ==
        static_cast<int>(osi3::HostVehicleData_VehicleAutomatedDrivingFunction_Name_NAME_LANE_KEEPING_ASSIST),
    "OSI Name enum drift: NAME_LANE_KEEPING_ASSIST");
static_assert(
    gt_esmini::osi_adas::NAME_SPEED_LIMIT_CONTROL ==
        static_cast<int>(osi3::HostVehicleData_VehicleAutomatedDrivingFunction_Name_NAME_SPEED_LIMIT_CONTROL),
    "OSI Name enum drift: NAME_SPEED_LIMIT_CONTROL");

// Same pinning, for the DriverOverride Reason mirror phase B added
// (req-vd-ad:REQ-AD-028 段b, design §8-3). File scope for the same reason as
// the 4 Names above: they back the ManualDrive report path, not a single
// dispatch branch. The COUNT matters as much as the values here — the whole
// reason accelerator-origin overrides go through custom_state instead of a
// Reason value is that OSI offers exactly these two and no third, so a future
// OSI release adding one would be a design-relevant event, not a silent gain.
static_assert(
    gt_esmini::osi_adas::REASON_BRAKE_PEDAL ==
        static_cast<int>(
            osi3::HostVehicleData_VehicleAutomatedDrivingFunction_DriverOverride_Reason_REASON_BRAKE_PEDAL),
    "OSI DriverOverride Reason enum drift: REASON_BRAKE_PEDAL");
static_assert(
    gt_esmini::osi_adas::REASON_STEERING_INPUT ==
        static_cast<int>(
            osi3::HostVehicleData_VehicleAutomatedDrivingFunction_DriverOverride_Reason_REASON_STEERING_INPUT),
    "OSI DriverOverride Reason enum drift: REASON_STEERING_INPUT");

// File-scope HVD estimator for non-GT-controller vehicles
static gt_esmini::HVDEstimator s_hvdEstimator;

// ====================== Log relay (audit CORE-4 / GT-5) ======================
// Bridges the core txtLogger callback (level-less "[time] [level] text\n" strings)
// to the leveled GT_SetLogCallback C API and keeps the last error-level message
// for GT_GetLastError.
namespace
{
    std::mutex       s_logMutex;
    GT_LogCallbackFn s_userLogCallback = nullptr;
    void*            s_userLogUserData = nullptr;
    std::string      s_lastError;

    // Parse the leading "[time] [level] " tags. Returns 0=unknown, 1=debug,
    // 2=info, 3=warn, 4=error; text_pos is set to the start of the message body.
    int GT_ParseLogLevel(const std::string& msg, size_t& text_pos)
    {
        text_pos = 0;
        if (msg.empty() || msg[0] != '[')
        {
            return 0;
        }
        size_t p1 = msg.find(']');
        if (p1 == std::string::npos || p1 + 2 >= msg.size() || msg[p1 + 1] != ' ' || msg[p1 + 2] != '[')
        {
            return 0;
        }
        size_t p2 = msg.find(']', p1 + 3);
        if (p2 == std::string::npos)
        {
            return 0;
        }
        text_pos = (p2 + 2 <= msg.size()) ? p2 + 2 : msg.size();
        const std::string lvl = msg.substr(p1 + 3, p2 - (p1 + 3));
        if (lvl == "debug") return 1;
        if (lvl == "info")  return 2;
        if (lvl == "warn")  return 3;
        if (lvl == "error") return 4;
        return 0;  // "[]" (pre-init line) or unrecognized tag
    }

    // txtLogger callback. Runs synchronously inside the core logger on the logging
    // thread — MUST NOT call LOG_* from here (would re-enter the logger).
    void GT_CoreLogRelay(const std::string& msg)
    {
        std::string line = msg;
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
        {
            line.pop_back();
        }

        size_t text_pos = 0;
        int    level    = GT_ParseLogLevel(line, text_pos);
        // pugixml XML syntax errors surface as info/warn (audit CORE-2/12) — promote.
        if (level != 4 && line.find("Error parsing") != std::string::npos)
        {
            level = 4;
        }

        GT_LogCallbackFn cb;
        void*            ud;
        {
            std::lock_guard<std::mutex> lock(s_logMutex);
            if (level == 4)
            {
                // Keep the root cause (audit CORE-3): the core re-logs the same
                // failure with an "Exception: " prefix and then a generic wrap-up
                // message — neither may overwrite an already recorded error.
                const std::string text = line.substr(text_pos);
                const bool        dup  = !s_lastError.empty() &&
                                 (text == s_lastError || text == "Exception: " + s_lastError);
                const bool generic = !s_lastError.empty() &&
                                     text == "Failed to initialize scenario player";
                if (!dup && !generic)
                {
                    s_lastError = text;
                }
            }
            cb = s_userLogCallback;
            ud = s_userLogUserData;
        }
        // Invoke outside the lock so a callback calling GT_GetLastError can't deadlock.
        if (cb != nullptr)
        {
            cb(level, line.c_str(), ud);
        }
    }

    // Core RegisterCallback has no single-unregister API (only ClearCallbacks) and
    // caps at 100 entries, so register exactly once for the process lifetime.
    void GT_RegisterCoreLogRelayOnce()
    {
        static bool registered = false;
        if (!registered)
        {
            txtLogger.RegisterCallback(&GT_CoreLogRelay);
            registered = true;
        }
    }
}  // namespace

// AutoLightManager Implementation
class AutoLightManager
{
public:
    static AutoLightManager& Instance()
    {
        static AutoLightManager instance;
        return instance;
    }

    void Init(scenarioengine::Entities* entities, scenarioengine::OSCEnvironment* environment = nullptr)
    {
        controllers_.clear();
        if (!entities) return;

        // Load the F6 headlight config (config/auto_light.json) once per Init.
        const gt_esmini::headlight::HeadlightConfig cfg = LoadHeadlightConfig();

        // Auto-detect Ego vehicle (Host Vehicle) as the first object in the list
        // This corresponds to OSI Host Vehicle ID logic
        if (!entities->object_.empty())
        {
            egoId_ = entities->object_.front()->GetId();
        }

        for (auto* obj : entities->object_)
        {
            if (obj && obj->type_ == scenarioengine::Object::Type::VEHICLE)
            {
                // Skip AutoLight for Ego vehicle if egoless mode is enabled
                if (egoless_ && obj->GetId() == egoId_)
                {
                    LOG_INFO("AutoLight: Skipping Ego vehicle (ID: {})", egoId_);
                    continue;
                }

                scenarioengine::Vehicle* vehicle = static_cast<scenarioengine::Vehicle*>(obj);

                // Ensure VehicleLightExtension exists
                auto* ext = gt_esmini::VehicleExtensionManager::Instance().GetExtension(vehicle);
                if (!ext)
                {
                    ext = new gt_esmini::VehicleLightExtension(vehicle);
                    gt_esmini::VehicleExtensionManager::Instance().RegisterExtension(vehicle, ext);
                }

                // Create AutoLightController with both arguments
                auto ctrl = std::make_unique<gt_esmini::AutoLightController>(vehicle, ext);
                ctrl->ConfigureHeadlights(cfg, environment, entities);
                // Inherit the current master state: Enable() only propagates to controllers
                // that already exist, so an Enable(true) issued before Init (e.g. the
                // --autolight-headlights argument filter) would otherwise be lost here.
                ctrl->Enable(enabled_);
                controllers_.push_back(std::move(ctrl));
            }
        }

        if (cfg.enabled)
        {
            LOG_INFO("AutoLight: environment-driven headlights ENABLED (F6)");
        }
    }

    void SetEgoless(bool egoless)
    {
        egoless_ = egoless;
    }

    // Force-enable the F6 headlight rule regardless of config (CLI --autolight-headlights).
    void SetHeadlightForceEnabled(bool enabled)
    {
        headlightForceEnabled_ = enabled;
    }

    void Enable(bool enable)
    {
        enabled_ = enable;
        for (auto& ctrl : controllers_)
        {
            ctrl->Enable(enable);
        }
    }

    void Update(double dt)
    {
        if (!enabled_) return;
        for (auto& ctrl : controllers_)
        {
            ctrl->Update(dt);
        }
    }

    void Close()
    {
        controllers_.clear();
        // Also clear extensions? They are owned by VehicleExtensionManager
        gt_esmini::VehicleExtensionManager::Instance().Clear();
        // R5-U3: scenario light ownership registry shares this lifecycle.
        gt_esmini::ScenarioLightRegistry::Instance().Clear();
    }

private:
    AutoLightManager() : enabled_(false), egoless_(false), egoId_(-1), headlightForceEnabled_(false) {}

    // Load config/auto_light.json (F6). Missing file / keys keep the safe defaults
    // (headlight feature OFF). --autolight-headlights force-enables regardless.
    gt_esmini::headlight::HeadlightConfig LoadHeadlightConfig() const
    {
        gt_esmini::headlight::HeadlightConfig cfg;  // defaults: enabled=false

        std::string       exeDir = gt_esmini::GetCurrentModuleDirectory();
        gt_esmini::ConfigLoader loader;
        std::string       path = loader.ResolveConfigPath(exeDir, "auto_light.json");

        gt_esmini::simplejson::Value root;
        std::string                  err;
        if (gt_esmini::simplejson::LoadFile(path, root, &err) && root.IsObject())
        {
            bool   b = false;
            double d = 0.0;
            if (root.GetBool("headlight_enabled", b)) cfg.enabled = b;
            if (root.GetDouble("headlight_illuminance_lux_threshold", d)) cfg.illuminance_lux_threshold = d;
            if (root.GetDouble("headlight_sun_elevation_deg", d)) cfg.sun_elevation_threshold_rad = d * 0.017453292519943295;  // deg->rad
            if (root.GetBool("headlight_use_time_of_day", b)) cfg.use_time_of_day = b;
            if (root.GetDouble("headlight_dusk_hour", d)) cfg.dusk_hour = d;
            if (root.GetDouble("headlight_dawn_hour", d)) cfg.dawn_hour = d;
            if (root.GetBool("headlight_tunnel_enabled", b)) cfg.tunnel_enabled = b;
            if (root.GetBool("highbeam_enabled", b)) cfg.highbeam_enabled = b;
            if (root.GetDouble("highbeam_range_m", d)) cfg.highbeam_range_m = d;
            if (root.GetDouble("highbeam_range_hysteresis_m", d)) cfg.highbeam_range_hysteresis_m = d;
            if (root.GetDouble("highbeam_corridor_half_width_m", d)) cfg.highbeam_corridor_half_m = d;
            if (root.GetDouble("highbeam_on_delay_s", d)) cfg.highbeam_on_delay_s = d;
            if (root.GetDouble("highbeam_off_delay_s", d)) cfg.highbeam_off_delay_s = d;
        }
        else
        {
            LOG_INFO("AutoLight: no config/auto_light.json ({}), headlights default OFF", err);
        }

        if (headlightForceEnabled_)
        {
            cfg.enabled = true;  // CLI override
        }
        return cfg;
    }

    std::vector<std::unique_ptr<gt_esmini::AutoLightController>> controllers_;
    bool enabled_;
    bool egoless_;
    int egoId_;
    bool headlightForceEnabled_;
};

// --- GT_esminiLib C-API Implementation ---

// ============================ GT road-model cache ============================
// Pre-generate the road 3D mesh with the parallel GT_RoadGen tool (cache-keyed by xodr
// content hash) and inject it as <SceneGraphFile> so the esmini viewer SKIPS its slow,
// single-threaded runtime road tessellation. Falls back silently to runtime generation.
namespace
{
    std::filesystem::path GT_SanitizedScenarioTempDir()
    {
        return std::filesystem::temp_directory_path() / "GT_esmini" / "sanitized_scenarios";
    }

    void GT_CleanupSanitizedScenarioTempDirOnce()
    {
        static bool cleaned = false;
        if (cleaned)
        {
            return;
        }
        cleaned = true;

        std::error_code ec;
        const auto dir = GT_SanitizedScenarioTempDir();
        std::filesystem::remove_all(dir, ec);
        std::filesystem::create_directories(dir, ec);
    }

    std::filesystem::path GT_MakeSanitizedScenarioPath(const char* inFile)
    {
        GT_CleanupSanitizedScenarioTempDirOnce();

        std::error_code ec;
        const auto dir = GT_SanitizedScenarioTempDir();
        std::filesystem::create_directories(dir, ec);

        const auto stem = std::filesystem::path(inFile ? inFile : "scenario").stem().string();
        const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        return dir / (stem + ".sanitized." + std::to_string(tick) + ".xosc");
    }

    void GT_RemoveSanitizedScenario(const std::string& path)
    {
        if (path.empty()) return;
        std::error_code ec;
        const auto temp_dir = std::filesystem::weakly_canonical(GT_SanitizedScenarioTempDir(), ec);
        const auto candidate = std::filesystem::weakly_canonical(path, ec);
        if (!ec && candidate.string().find(temp_dir.string()) == 0)
        {
            std::filesystem::remove(candidate, ec);
        }
    }

    bool GT_IsRelativeScenarioPathValue(const std::string& value)
    {
        if (value.empty() || value[0] == '$' || value.find("://") != std::string::npos)
        {
            return false;
        }
        return std::filesystem::path(value).is_relative();
    }

    void GT_AbsolutizeScenarioPaths(pugi::xml_node node, const std::filesystem::path& base_dir)
    {
        for (pugi::xml_attribute attr : node.attributes())
        {
            const std::string name = attr.name();
            if (name != "filepath" && name != "path")
            {
                continue;
            }

            const std::string value = attr.value();
            if (!GT_IsRelativeScenarioPathValue(value))
            {
                continue;
            }

            std::error_code ec;
            const auto abs = std::filesystem::absolute(base_dir / value, ec);
            if (!ec)
            {
                attr.set_value(abs.string().c_str());
            }
        }

        for (pugi::xml_node child = node.first_child(); child; child = child.next_sibling())
        {
            GT_AbsolutizeScenarioPaths(child, base_dir);
        }
    }

    uint64_t GT_FnvHashFile(const std::filesystem::path& p)
    {
        std::ifstream f(p, std::ios::binary);
        if (!f)
        {
            return 0;
        }
        uint64_t h = 1469598103934665603ULL;  // FNV-1a 64-bit offset basis
        char     buf[1 << 16];
        while (f)
        {
            f.read(buf, sizeof(buf));
            std::streamsize n = f.gcount();
            for (std::streamsize i = 0; i < n; i++)
            {
                h ^= static_cast<uint8_t>(buf[i]);
                h *= 1099511628211ULL;
            }
        }
        return h;
    }

    int GT_RunProcess(const std::string& cmdline)
    {
#ifdef _WIN32
        // cmd /c needs the whole command wrapped in an extra pair of quotes when it
        // contains multiple quoted tokens (paths with spaces).
        std::string wrapped = "\"" + cmdline + "\"";
        return std::system(wrapped.c_str());
#else
        return std::system(cmdline.c_str());
#endif
    }

    // Returns absolute path to a cached .osgb road model for `xodrAbs`, generating it via
    // GT_RoadGen if not already cached. Returns "" on any failure (caller falls back).
    std::string GT_EnsureCachedRoadModel(const std::filesystem::path& xodrAbs)
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        if (!fs::exists(xodrAbs, ec))
        {
            return "";
        }

        uint64_t h = GT_FnvHashFile(xodrAbs);
        if (h == 0)
        {
            return "";
        }

        fs::path exeDir   = gt_esmini::GetCurrentModuleDirectory();
        fs::path cacheDir = exeDir / "model_cache";
        fs::create_directories(cacheDir, ec);

        char hbuf[20];
        snprintf(hbuf, sizeof(hbuf), "%016llx", static_cast<unsigned long long>(h));
        fs::path cachePath = cacheDir / (std::string(hbuf) + ".osgb");

        if (fs::exists(cachePath, ec))
        {
            return cachePath.string();  // cache hit
        }

        fs::path gen = exeDir / "GT_RoadGen.exe";
        if (!fs::exists(gen, ec))
        {
            gen = exeDir / "GT_RoadGen";  // non-Windows
            if (!fs::exists(gen, ec))
            {
                return "";
            }
        }

        std::string cmd = "\"" + gen.string() + "\" \"" + xodrAbs.string() + "\" \"" + cachePath.string() + "\" --threads 0";
        LOG_INFO("GT_esmini: generating road model: {}", cmd);
        int rc = GT_RunProcess(cmd);
        if (rc == 0 && fs::exists(cachePath, ec))
        {
            return cachePath.string();
        }

        LOG_WARN("GT_esmini: road model generation failed (rc={}); using runtime generation", rc);
        return "";
    }

    // Resolve the OpenDRIVE path referenced by the scenario, then inject a cached road
    // <SceneGraphFile> into RoadNetwork (no-op if one already exists or anything fails).
    void GT_InjectCachedRoadModel(pugi::xml_document& doc, const char* inFile)
    {
        namespace fs = std::filesystem;
        pugi::xml_node rn = doc.child("OpenSCENARIO").child("RoadNetwork");
        if (!rn || rn.child("SceneGraphFile"))
        {
            return;  // no road network, or an explicit model is already specified
        }
        pugi::xml_node lf = rn.child("LogicFile");
        std::string    xodrRel = lf ? lf.attribute("filepath").as_string() : "";
        if (xodrRel.empty())
        {
            return;
        }

        std::error_code ec;
        fs::path        xodr(xodrRel);
        if (xodr.is_relative())
        {
            xodr = fs::path(inFile).parent_path() / xodr;
        }
        if (!fs::exists(xodr, ec))
        {
            // Fallback to esmini's resources/xodr (bare-filename scenarios)
            fs::path alt = fs::path(gt_esmini::GetCurrentModuleDirectory()) / ".." / "resources" / "xodr" / fs::path(xodrRel).filename();
            if (fs::exists(alt, ec))
            {
                xodr = alt;
            }
        }
        xodr = fs::weakly_canonical(xodr, ec);

        std::string model = GT_EnsureCachedRoadModel(xodr);
        if (!model.empty())
        {
            pugi::xml_node sg = rn.append_child("SceneGraphFile");
            sg.append_attribute("filepath").set_value(model.c_str());
            LOG_INFO("GT_esmini: injected cached road model: {}", model);
        }
    }
}  // namespace

// Basic XOSC sanitizer. When inject_road_model is true, also injects a pre-generated
// <SceneGraphFile> (see above).
//
// R5-U3: AppearanceAction / LightStateAction are NO LONGER stripped. Upstream esmini
// v3.3.0 added a native LightStateAction parser+executor (writes Object::vehLghtStsList[]
// + DirtyBit::LIGHT_STATE), so the native ScenarioReader in SE_Init now handles those
// nodes directly with full fidelity (transitions / candela / flashing / conflict). Leaving
// them in is precisely the "delegate to the native parser" path.
//
// R5-U3 follow-ups:
//  - A bare <LightStateAction> placed directly under <PrivateAction> (without the
//    <AppearanceAction> wrapper) was accepted by the pre-U3 GT parser but trips the native
//    reader's "Action is not supported" throw. It is REWRAPPED in place (not dropped) so
//    such scenarios keep executing their light actions.
//  - If the scenario contains NO light action at all, a no-op native light action
//    (licensePlateIllumination off) is injected into Storyboard/Init/Actions so the native
//    reader sets has_lightstate_action_ -> playerbase.cpp gates viewer UpdateLight() on it
//    -> GT-writer-only lights (AutoLight / ManualDrive) become visible in the OSG viewer.
//    licensePlateIllumination is outside the OSI/HVD lightMask mappings; cost is one dat
//    LIGHT_STATE packet at t=0. The marker comment GT_VIEWER_LIGHT_GATE is written to the
//    temp file for diagnosability (pugixml default parse flags drop comments on re-load,
//    so the native parser never sees it).
static bool CreateSanitizedScenario(const char* inFile, const std::string& outFile, bool inject_road_model = false)
{
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_file(inFile);
    if (!result) return false;

    // Pass 1: rewrap bare-form light actions (PrivateAction > LightStateAction) into the
    // standard PrivateAction > AppearanceAction > LightStateAction shape, preserving all
    // attributes and child nodes.
    std::function<void(pugi::xml_node)> rewrap;
    rewrap = [&](pugi::xml_node node) {
        for (pugi::xml_node child = node.first_child(); child; )
        {
            pugi::xml_node next = child.next_sibling();
            std::string name = child.name();

            if (name == "LightStateAction" && std::string(node.name()) == "PrivateAction")
            {
                pugi::xml_node wrapper = node.insert_child_before("AppearanceAction", child);
                wrapper.append_copy(child);  // deep copy: attributes + children preserved
                node.remove_child(child);
                LOG_INFO("GT sanitizer: rewrapped bare LightStateAction under PrivateAction into AppearanceAction wrapper (native parser form)");
            }
            else
            {
                rewrap(child);
            }
            child = next;
        }
    };
    rewrap(doc);

    // Pass 2: viewer-gate injection. If the document contains no light action anywhere,
    // append a no-op one so the native reader flags HasLightStateAction().
    bool hasLightNode = false;
    std::function<void(pugi::xml_node)> findLight;
    findLight = [&](pugi::xml_node n) {
        if (hasLightNode) return;
        for (pugi::xml_node c = n.first_child(); c && !hasLightNode; c = c.next_sibling())
        {
            std::string nm = c.name();
            if (nm == "LightStateAction" || nm == "AppearanceAction")
            {
                hasLightNode = true;
                return;
            }
            findLight(c);
        }
    };
    findLight(doc);

    if (!hasLightNode)
    {
        pugi::xml_node osc      = doc.child("OpenSCENARIO");
        pugi::xml_node entities = osc.child("Entities");
        pugi::xml_node actions  = osc.child("Storyboard").child("Init").child("Actions");
        if (entities && actions)
        {
            auto is_vehicle = [&](const char* name) -> bool {
                for (pugi::xml_node so = entities.child("ScenarioObject"); so; so = so.next_sibling("ScenarioObject"))
                {
                    if (std::string(so.attribute("name").value()) == name)
                    {
                        return !so.child("Vehicle").empty();
                    }
                }
                return false;
            };

            // Reuse an EXISTING Init <Private> block. The native reader calls
            // activateObject() once per Private block, so creating a SECOND Private for an
            // already-activated entity aborts init ("Already active"). Prefer a Private whose
            // entity is a Vehicle; otherwise the first Private. Only when the scenario has no
            // Init Private at all do we create one (for the first Vehicle / first object) —
            // there is no prior activation to duplicate in that case.
            pugi::xml_node targetPriv;
            pugi::xml_node firstPriv;
            for (pugi::xml_node p = actions.child("Private"); p; p = p.next_sibling("Private"))
            {
                if (!firstPriv)
                {
                    firstPriv = p;
                }
                if (is_vehicle(p.attribute("entityRef").value()))
                {
                    targetPriv = p;
                    break;
                }
            }
            if (!targetPriv)
            {
                targetPriv = firstPriv;
            }

            std::string entityName;
            if (targetPriv)
            {
                entityName = targetPriv.attribute("entityRef").value();
            }
            else
            {
                // No Init Private blocks at all: create one for the first Vehicle (or first
                // object). The native action only touches Object::vehLghtStsList (a base
                // Object member), so it degrades gracefully on any entity type.
                pugi::xml_node chosen;
                for (pugi::xml_node so = entities.child("ScenarioObject"); so; so = so.next_sibling("ScenarioObject"))
                {
                    if (so.child("Vehicle"))
                    {
                        chosen = so;
                        break;
                    }
                }
                if (!chosen)
                {
                    chosen = entities.child("ScenarioObject");
                }
                const char* nm = chosen ? chosen.attribute("name").value() : "";
                if (chosen && nm[0] != '\0' && nm[0] != '$')
                {
                    targetPriv = actions.append_child("Private");
                    targetPriv.append_attribute("entityRef") = nm;
                    entityName = nm;
                }
            }

            if (targetPriv && !entityName.empty() && entityName[0] != '$')  // skip parameterized names
            {
                pugi::xml_node pa  = targetPriv.append_child("PrivateAction");
                pugi::xml_node app = pa.append_child("AppearanceAction");
                pugi::xml_node lsa = app.append_child("LightStateAction");
                lsa.append_attribute("transitionTime") = "0";
                pugi::xml_node lt = lsa.append_child("LightType");
                pugi::xml_node vl = lt.append_child("VehicleLight");
                vl.append_attribute("vehicleLightType") = "licensePlateIllumination";
                pugi::xml_node ls = lsa.append_child("LightState");
                ls.append_attribute("mode") = "off";
                LOG_INFO("GT sanitizer: injected no-op LightStateAction (licensePlateIllumination off) into Init Private for entity '{}' to enable the viewer light gate",
                         entityName);
            }
        }
    }

    if (inject_road_model)
    {
        GT_InjectCachedRoadModel(doc, inFile);
    }

    GT_AbsolutizeScenarioPaths(doc, std::filesystem::path(inFile).parent_path());

    return doc.save_file(outFile.c_str());
}

GT_ESMINI_API int GT_Init(const char* oscFilename, int disable_ctrls)
{
    GT_RegisterCoreLogRelayOnce();
    {
        std::lock_guard<std::mutex> lock(s_logMutex);
        s_lastError.clear();
    }

    // 1. Create a sanitized version of the scenario
    // esmini throws error on AppearanceAction/LightStateAction.
    // We strip them for the main initialization.
    std::string sanitizedFile = GT_MakeSanitizedScenarioPath(oscFilename).string();
    if (!CreateSanitizedScenario(oscFilename, sanitizedFile))
    {
         LOG_ERROR("GT_Init: Failed to create sanitized scenario file.");
         return -1;
    }

    // 1.5 Register Custom Controllers
    scenarioengine::ScenarioReader::RegisterController(CONTROLLER_REAL_DRIVER_TYPE_NAME, gt_esmini::InstantiateControllerRealDriver);
#ifdef GT_ENABLE_EMBEDDED_PYTHON
    scenarioengine::ScenarioReader::RegisterController(CONTROLLER_PYTHON_DRIVER_TYPE_NAME, gt_esmini::InstantiateControllerPythonDriver);
#endif
    scenarioengine::ScenarioReader::RegisterController(CONTROLLER_MANUAL_DRIVE_TYPE_NAME, gt_esmini::InstantiateControllerManualDrive);
    scenarioengine::ScenarioReader::RegisterController(CONTROLLER_KINEMATIC_TYPE_NAME, gt_esmini::InstantiateControllerKinematic);
    scenarioengine::ScenarioReader::RegisterController(CONTROLLER_ROUTE_DRIVE_TYPE_NAME, gt_esmini::InstantiateControllerRouteDrive);
    scenarioengine::ScenarioReader::RegisterController(CONTROLLER_VIRTUAL_DRIVER_TYPE_NAME, gt_esmini::InstantiateControllerVirtualDriver);

    // 2. Initialize esmini using SE_Init with sanitized file
    int ret = SE_Init(sanitizedFile.c_str(), disable_ctrls, 0, 0, 0);

    // Clean up temp file
    GT_RemoveSanitizedScenario(sanitizedFile);

    if (ret != 0)
    {
        return ret;
    }

    // 3. Perform Delta Parsing for Extensions using ORIGINAL file
    if (player && player->scenarioEngine)
    {
        // Load ORIGINAL XML
        pugi::xml_document doc;
        pugi::xml_parse_result result = doc.load_file(oscFilename);

        if (result)
        {
            // Use GT_ScenarioReader to parse extensions
            // Access Catalogs via existing loader because it's private in Engine
            auto* scReader = player->scenarioEngine->GetScenarioReader();
            auto* catalogs = scReader ? scReader->GetCatalogs() : nullptr;

            // Save static parameters/variables (see GT_InitWithArgs for rationale)
            auto savedParams = scenarioengine::ScenarioReader::parameters;
            auto savedVars   = scenarioengine::ScenarioReader::variables;

            {
                gt_esmini::GT_ScenarioReader reader(
                    &player->scenarioEngine->entities_,
                    catalogs,
                    &player->scenarioEngine->environment
                );

                scenarioengine::ScenarioReader::parameters = savedParams;
                scenarioengine::ScenarioReader::variables   = savedVars;

                // Inject actions into Storyboard
                reader.ParseExtensionActions(doc, player->scenarioEngine->storyBoard);
            }
            scenarioengine::ScenarioReader::parameters = savedParams;
            scenarioengine::ScenarioReader::variables   = savedVars;
        }
        else
        {
            LOG_ERROR("GT_Init: Failed to reload XOSC for extensions: {}", result.description());
        }

        // 3b. Initialize TrafficSignalControllers
        gt_esmini::TrafficSignalControllerManager::Instance().InitAll();

        // 4. Initialize AutoLightManager
        AutoLightManager::Instance().Init(&player->scenarioEngine->entities_, &player->scenarioEngine->environment);

        // 4b. Initialize VehiclePhysicsManager
        {
            std::string exeDir = gt_esmini::GetCurrentModuleDirectory();
            gt_esmini::ConfigLoader config_loader;
            std::string paramsFile = config_loader.ResolveConfigPath(exeDir, "real_vehicle_params.json");

            auto& vpm = gt_esmini::VehiclePhysicsManager::Instance();
            vpm.LoadProfiles(paramsFile);
            vpm.Init(&player->scenarioEngine->entities_);

            s_hvdEstimator.LoadParams(paramsFile);
        }

        // 5. Register OSIReporter for global access (for Light state)
        // R5-U4: the OSI light state is now emitted by GT_OSIReporter reading the native
        // vehLghtStsList[] storage directly (the R5-U3 single source of truth); the former
        // GT_SetLightStateProvider hook indirection has been removed.
#ifdef _USE_OSI
        extern void GT_SetCurrentOSIReporter(OSIReporter* reporter);
        if (player->osiReporter)
        {
            GT_SetCurrentOSIReporter(player->osiReporter);
        }
#endif  // _USE_OSI

        // 7. Initialize GT_HostVehicleReporter (separated from OSIReporter)
        {
            std::string exeDir = gt_esmini::GetCurrentModuleDirectory();
            gt_esmini::ConfigLoader config_loader;
            std::string configFile = config_loader.ResolveConfigPath(exeDir, "host_vehicle_config.json");
            gt_esmini::GT_HostVehicleReporter::Instance().Init(48199, configFile);
        }
    }

    return 0;
}

GT_ESMINI_API int GT_InitWithArgs(int argc, const char* argv[])
{
    GT_RegisterCoreLogRelayOnce();
    {
        std::lock_guard<std::mutex> lock(s_logMutex);
        s_lastError.clear();
    }

    LOG_DEBUG("GT_InitWithArgs called with argc={}", argc);
    if (argc > 0 && argv) {
        LOG_DEBUG("argv[0]={}", argv[0] ? argv[0] : "NULL");
    }
    const char* filename = nullptr;
    
    // Simple argument parsing to find the filename.
    // Logic mostly copied from esmini-dyn/main.cpp to identify the filename arg
    if (argc >= 2)
    {
        if (strncmp(argv[1], "--", 2) != 0)
        {
             filename = argv[1];
        }
        else
        {
            // Look for --osc argument if needed, or iterate
            for(int i=1; i<argc; i++)
            {
                if (strcmp(argv[i], "--osc") == 0 && i+1 < argc)
                {
                    filename = argv[i+1];
                    break;
                }
            }
        }
    }

    // Capture OSI IP and SV port if provided
    std::string osiTargetIp = "";
    int svPort = 48200;  // default SV reporter port
    int vdPort = 48202;  // default VirtualDriver telemetry reporter port
    bool kinematicModeEnabled = false;
    bool routeDriveModeEnabled = false;
    std::string routeDriveTiming = "normal";  // late | normal | early (Timing knob)
    std::string routeDriveGap    = "normal";  // wide | normal | tight (Gap knob)

    // If filename found, sanitized it
    std::string sanitizedFile;
    std::vector<const char*> newArgv;
    std::vector<std::string> argStorage; // to keep strings alive

    if (filename)
    {
        LOG_DEBUG("Sanitizing filename: {}", filename);
        // A viewer runs unless --headless is requested; only then is a road 3D model needed.
        bool headless = false;
        for (int hi = 0; hi < argc; hi++)
        {
            if (argv[hi] && strcmp(argv[hi], "--headless") == 0)
            {
                headless = true;
                break;
            }
        }
        sanitizedFile = GT_MakeSanitizedScenarioPath(filename).string();
        if (!CreateSanitizedScenario(filename, sanitizedFile, !headless))
        {
             LOG_WARN("GT_InitWithArgs: Failed to create sanitized scenario file.");
             // Try proceeding with original filename (might crash if unsupported actions present)
             sanitizedFile = filename;
        }

        // Reconstruct argv with sanitized filename
        for(int i=0; i<argc; i++)
        {
            if (argv[i] && strcmp(argv[i], filename) == 0)
            {
                argStorage.push_back(sanitizedFile);
                newArgv.push_back(argStorage.back().c_str());
            }
            // Filter custom arguments that esmini doesn't recognize
            else if (argv[i] &&
                     (strcmp(argv[i], "--autolight") == 0 ||
                      strcmp(argv[i], "--autolight-egoless") == 0 ||
                      strcmp(argv[i], "--autolight-headlights") == 0 ||
                      strcmp(argv[i], "--vehicle-physics") == 0 ||
                      strcmp(argv[i], "--heading-correction") == 0 ||
                      strcmp(argv[i], "--osi") == 0 ||
                      strcmp(argv[i], "--hz") == 0 ||
                      strcmp(argv[i], "--no_realtime") == 0 ||
                      strcmp(argv[i], "--video_capture") == 0 ||
                      strcmp(argv[i], "--video_headless") == 0 ||
                      strcmp(argv[i], "--video_window") == 0 ||
                      strcmp(argv[i], "--video_frames") == 0 ||
                      strcmp(argv[i], "--video_prefix") == 0 ||
                      strcmp(argv[i], "--sv-port") == 0 ||
                      strcmp(argv[i], "--vd-port") == 0 ||
                      strcmp(argv[i], "--kinematic-mode") == 0 ||
                      strcmp(argv[i], "--route-drive-mode") == 0 ||
                      strcmp(argv[i], "--route-drive-timing") == 0 ||
                      strcmp(argv[i], "--route-drive-gap") == 0))
            {
                if (strcmp(argv[i], "--autolight-egoless") == 0)
                {
                    AutoLightManager::Instance().SetEgoless(true);
                }

                if (strcmp(argv[i], "--autolight-headlights") == 0)
                {
                    // F6: force-enable environment-driven headlights (overrides config).
                    // Asking for headlights implies the AutoLight master switch: without
                    // Enable(true), AutoLightManager::Update() early-returns and the rule
                    // never runs (GT_Loader masked this by enabling the master by default).
                    AutoLightManager::Instance().SetHeadlightForceEnabled(true);
                    AutoLightManager::Instance().Enable(true);
                }

                if (strcmp(argv[i], "--osi") == 0)
                {
                    if (i + 1 < argc)
                    {
                        osiTargetIp = argv[i+1];
                        i++; // Skip the IP
                    }
                }
                else if (strcmp(argv[i], "--hz") == 0)
                {
                    i++; // Skip the frequency
                }
                else if (strcmp(argv[i], "--video_window") == 0)
                {
                    i += 2; // Skip width and height
                }
                else if (strcmp(argv[i], "--video_frames") == 0 || strcmp(argv[i], "--video_prefix") == 0)
                {
                    i++; // Skip single value
                }
                else if (strcmp(argv[i], "--sv-port") == 0)
                {
                    if (i + 1 < argc)
                    {
                        try { svPort = std::stoi(argv[i+1]); } catch (...) {}
                        i++; // Skip the port value
                    }
                }
                else if (strcmp(argv[i], "--vd-port") == 0)
                {
                    if (i + 1 < argc)
                    {
                        try { vdPort = std::stoi(argv[i+1]); } catch (...) {}
                        i++; // Skip the port value
                    }
                }
                else if (strcmp(argv[i], "--kinematic-mode") == 0)
                {
                    kinematicModeEnabled = true;
                }
                else if (strcmp(argv[i], "--route-drive-mode") == 0)
                {
                    routeDriveModeEnabled = true;
                }
                else if (strcmp(argv[i], "--route-drive-timing") == 0)
                {
                    if (i + 1 < argc) { routeDriveTiming = argv[i + 1]; i++; }
                }
                else if (strcmp(argv[i], "--route-drive-gap") == 0)
                {
                    if (i + 1 < argc) { routeDriveGap = argv[i + 1]; i++; }
                }
            }
            else
            {
                newArgv.push_back(argv[i]);
            }
        }
    }
    else
    {
        // No filename found? Pass as is.
        for(int i=0; i<argc; i++) newArgv.push_back(argv[i]);
    }

    // 1.5 Register Custom Controllers
    scenarioengine::ScenarioReader::RegisterController(CONTROLLER_REAL_DRIVER_TYPE_NAME, gt_esmini::InstantiateControllerRealDriver);
#ifdef GT_ENABLE_EMBEDDED_PYTHON
    scenarioengine::ScenarioReader::RegisterController(CONTROLLER_PYTHON_DRIVER_TYPE_NAME, gt_esmini::InstantiateControllerPythonDriver);
#endif
    scenarioengine::ScenarioReader::RegisterController(CONTROLLER_MANUAL_DRIVE_TYPE_NAME, gt_esmini::InstantiateControllerManualDrive);
    scenarioengine::ScenarioReader::RegisterController(CONTROLLER_KINEMATIC_TYPE_NAME, gt_esmini::InstantiateControllerKinematic);
    scenarioengine::ScenarioReader::RegisterController(CONTROLLER_ROUTE_DRIVE_TYPE_NAME, gt_esmini::InstantiateControllerRouteDrive);
    scenarioengine::ScenarioReader::RegisterController(CONTROLLER_VIRTUAL_DRIVER_TYPE_NAME, gt_esmini::InstantiateControllerVirtualDriver);

    // 2. Initialize esmini using SE_Init with sanitized args
    LOG_DEBUG("Calling SE_InitWithArgs with {} args.", newArgv.size());
    int ret = SE_InitWithArgs(static_cast<int>(newArgv.size()), newArgv.data());
    LOG_DEBUG("SE_InitWithArgs returned: {}", ret);
    
    GT_RemoveSanitizedScenario(sanitizedFile);

    if (ret != 0)
    {
        return ret;
    }

    // [GT_MOD] DIAGNOSTIC & FIX: Check and Reset QuitFlag
    int postSeInitQuit = SE_GetQuitFlag();
    if (postSeInitQuit) {
        LOG_WARN("GT_InitWithArgs: SE_InitWithArgs returned 0 but QuitFlag is {}. Forcing reset.", postSeInitQuit);
        if (player) {
            player->SetQuitRequest(false);
            LOG_INFO("GT_InitWithArgs: QuitFlag forced to 0.");
        }
    } else {
        LOG_DEBUG("GT_InitWithArgs: SE_InitWithArgs OK. QuitFlag=0.");
    }
    // [GT_MOD] END

    // 3. Perform Delta Parsing for Extensions using ORIGINAL file
    if (filename && player && player->scenarioEngine)
    {
        // Load ORIGINAL XML
        pugi::xml_document doc;
        pugi::xml_parse_result result = doc.load_file(filename);

        if (result)
        {
            auto* scReader = player->scenarioEngine->GetScenarioReader();
            auto* catalogs = scReader ? scReader->GetCatalogs() : nullptr;

            // Save static parameters/variables: ScenarioReader's constructor and
            // destructor call Clear() on these statics, which would wipe out the
            // parameter declarations already loaded by SE_InitWithArgs.  This breaks
            // ParameterCondition and ParameterAction at runtime.
            auto savedParams = scenarioengine::ScenarioReader::parameters;
            auto savedVars   = scenarioengine::ScenarioReader::variables;

            {
                gt_esmini::GT_ScenarioReader reader(
                    &player->scenarioEngine->entities_,
                    catalogs,
                    &player->scenarioEngine->environment
                );

                // Restore immediately after construction (constructor cleared them)
                scenarioengine::ScenarioReader::parameters = savedParams;
                scenarioengine::ScenarioReader::variables   = savedVars;

                // Inject actions into Storyboard
                reader.ParseExtensionActions(doc, player->scenarioEngine->storyBoard);
            }
            // Destructor cleared them again — restore once more
            scenarioengine::ScenarioReader::parameters = savedParams;
            scenarioengine::ScenarioReader::variables   = savedVars;
        }
        else
        {
            LOG_ERROR("GT_InitWithArgs: Failed to reload XOSC for extensions: {}", result.description());
        }

        // 3b. Initialize TrafficSignalControllers
        gt_esmini::TrafficSignalControllerManager::Instance().InitAll();

        // 4. Initialize AutoLightManager
        AutoLightManager::Instance().Init(&player->scenarioEngine->entities_, &player->scenarioEngine->environment);

        // Check for --autolight argument in original argv
        bool autoLightEnabled = false;
        for(int i=0; i<argc; i++) {
            if(argv[i] && strcmp(argv[i], "--autolight") == 0) {
                autoLightEnabled = true;
                break;
            }
        }
        if (autoLightEnabled) {
             AutoLightManager::Instance().Enable(true);
             LOG_INFO("GT_Init: AutoLight enabled via argument.");
        }

        // 4b. Initialize VehiclePhysicsManager (observed pitch/roll for non-GT vehicles)
        {
            std::string exeDir = gt_esmini::GetCurrentModuleDirectory();
            gt_esmini::ConfigLoader config_loader;
            std::string paramsFile = config_loader.ResolveConfigPath(exeDir, "real_vehicle_params.json");

            auto& vpm = gt_esmini::VehiclePhysicsManager::Instance();
            vpm.LoadProfiles(paramsFile);
            vpm.Init(&player->scenarioEngine->entities_);

            // Share the same params file with HVDEstimator (pedal_estimation + shift_schedule)
            s_hvdEstimator.LoadParams(paramsFile);

            // Check for --vehicle-physics argument in original argv
            for (int i = 0; i < argc; i++)
            {
                if (argv[i] && strcmp(argv[i], "--vehicle-physics") == 0)
                {
                    vpm.Enable(true);
                    LOG_INFO("GT_Init: VehiclePhysics enabled via argument.");
                    break;
                }
            }
        }

        // 4c. Initialize HeadingCorrectionManager (nose-leading heading for non-GT vehicles)
        {
            std::string exeDir2 = gt_esmini::GetCurrentModuleDirectory();
            gt_esmini::ConfigLoader config_loader2;
            std::string paramsFile2 = config_loader2.ResolveConfigPath(exeDir2, "real_vehicle_params.json");

            auto& hcm = gt_esmini::HeadingCorrectionManager::Instance();
            hcm.LoadProfiles(paramsFile2);
            hcm.Init(&player->scenarioEngine->entities_);

            for (int i = 0; i < argc; i++)
            {
                if (argv[i] && strcmp(argv[i], "--heading-correction") == 0)
                {
                    hcm.Enable(true);
                    LOG_INFO("GT_Init: HeadingCorrection enabled via argument.");
                    break;
                }
            }
        }

        // 4c-2. RouteDriveController auto-assignment (Route Drive Mode) — ego only.
        // Runs BEFORE the Kinematic block so the Kinematic guard can detect and
        // stack onto the RouteDrive-controlled ego.
        if (routeDriveModeEnabled)
        {
            // Resolve ego: HVD target vehicle by name, else first VEHICLE.
            std::string egoName;
            if (gt_esmini::GT_HostVehicleReporter::Instance().IsInitialized())
            {
                egoName = gt_esmini::GT_HostVehicleReporter::Instance().GetTargetVehicle();
            }
            Object* ego          = nullptr;
            Object* firstVehicle = nullptr;
            for (auto* obj : player->scenarioEngine->entities_.object_)
            {
                if (!obj || obj->type_ != scenarioengine::Object::Type::VEHICLE)
                {
                    continue;
                }
                if (firstVehicle == nullptr)
                {
                    firstVehicle = obj;
                }
                if (!egoName.empty() && obj->GetName() == egoName)
                {
                    ego = obj;
                    break;
                }
            }
            if (ego == nullptr)
            {
                ego = firstVehicle;
            }

            if (ego != nullptr && ego->GetNrOfAssignedControllers() == 0)
            {
                std::string          exeDirRD     = gt_esmini::GetCurrentModuleDirectory();
                gt_esmini::ConfigLoader config_loaderRD;
                std::string          rdConfigPath = config_loaderRD.ResolveConfigPath(exeDirRD, "route_drive_controller.json");

                scenarioengine::OSCProperties        propsRD;
                scenarioengine::Controller::InitArgs initArgsRD;
                initArgsRD.name            = std::string("RouteDriveController_") + ego->GetName();
                initArgsRD.type            = CONTROLLER_ROUTE_DRIVE_TYPE_NAME;
                initArgsRD.properties      = &propsRD;
                initArgsRD.scenario_engine = player->scenarioEngine;
                initArgsRD.parameters      = nullptr;

                auto* rdCtrl = new gt_esmini::ControllerRouteDrive(&initArgsRD);
                rdCtrl->LoadConfig(rdConfigPath);
                // CLI Timing/Gap knobs override the JSON defaults.
                {
                    double alpha = (routeDriveTiming == "late") ? 0.0 : (routeDriveTiming == "early") ? 1.0 : 0.5;
                    double beta  = (routeDriveGap == "wide") ? 0.0 : (routeDriveGap == "tight") ? 1.0 : 0.5;
                    rdCtrl->SetTimingGap(alpha, beta);
                    LOG_INFO("GT_Init: RouteDrive timing={} gap={}", routeDriveTiming, routeDriveGap);
                }
                rdCtrl->LinkObject(ego);
                ego->AssignController(rdCtrl);
                rdCtrl->Init();

                ControlActivationMode rdModes[static_cast<unsigned int>(ControlDomains::COUNT)];
                rdModes[static_cast<unsigned int>(ControlDomains::DOMAIN_LONG)]  = ControlActivationMode::UNDEFINED;
                rdModes[static_cast<unsigned int>(ControlDomains::DOMAIN_LAT)]   = ControlActivationMode::ON;
                rdModes[static_cast<unsigned int>(ControlDomains::DOMAIN_LIGHT)] = ControlActivationMode::UNDEFINED;
                rdModes[static_cast<unsigned int>(ControlDomains::DOMAIN_ANIM)]  = ControlActivationMode::UNDEFINED;
                rdCtrl->Activate(rdModes);

                player->scenarioEngine->scenarioReader->AddController(rdCtrl);
                LOG_INFO("GT_Init: RouteDriveController assigned to ego '{}'.", ego->GetName());
            }
            else if (ego != nullptr)
            {
                LOG_INFO("GT_Init: RouteDriveController skipped - ego '{}' already has an explicit controller.", ego->GetName());
            }
            else
            {
                LOG_WARN("GT_Init: RouteDriveController - no ego vehicle found.");
            }
        }

        // 4d. KinematicController auto-assignment (Kinematic Mode)
        if (kinematicModeEnabled)
        {
            std::string exeDir3 = gt_esmini::GetCurrentModuleDirectory();
            gt_esmini::ConfigLoader config_loader3;
            std::string kinConfigPath = config_loader3.ResolveConfigPath(exeDir3, "kinematic_controller.json");

            int assignCount = 0;
            for (auto* obj : player->scenarioEngine->entities_.object_)
            {
                if (!obj || obj->type_ != scenarioengine::Object::Type::VEHICLE)
                {
                    continue;
                }
                // Assign to vehicles without an explicit controller. Also stack
                // onto a vehicle whose ONLY controller is the RouteDriveController
                // (so Route Drive + Kinematic compose on the ego). Vehicles with
                // RealDriver/ManualDrive/etc. keep being skipped.
                int  nCtrl         = obj->GetNrOfAssignedControllers();
                bool onlyRouteDrive =
                    (nCtrl == 1 && obj->GetAssignedControllerOftype(
                                       static_cast<scenarioengine::Controller::Type>(gt_esmini::CONTROLLER_TYPE_ROUTE_DRIVE)) != nullptr);
                if (nCtrl > 0 && !onlyRouteDrive)
                {
                    continue;
                }

                // Create InitArgs for the controller
                scenarioengine::OSCProperties props;
                scenarioengine::Controller::InitArgs initArgs;
                initArgs.name = std::string("KinematicController_") + obj->GetName();
                initArgs.type = CONTROLLER_KINEMATIC_TYPE_NAME;
                initArgs.properties = &props;
                initArgs.scenario_engine = player->scenarioEngine;
                initArgs.parameters = nullptr;

                auto* ctrl = new gt_esmini::ControllerKinematic(&initArgs);
                ctrl->LoadConfig(kinConfigPath);
                ctrl->LinkObject(obj);
                // LinkObject only sets controller->object_; it does NOT register
                // the controller in object->controllers_. Without this call,
                // Object::GetAssignedControllerOftype() can't find the KC, which
                // breaks the HVD override path and leaves HVDEstimator's
                // preview-attenuated steering as the reported value.
                obj->AssignController(ctrl);
                // ScenarioEngine's Init loop for scenario-declared controllers
                // already ran before GT_Init. Call Init() explicitly so KC sets
                // mode_ = MODE_ADDITIVE (base-class default is MODE_OVERRIDE).
                // Without this, private actions that short-circuit on
                // IsControllerModeOnDomains(MODE_OVERRIDE, LAT) — notably
                // LatLaneChangeAction and LatLaneOffsetAction — would silently
                // no-op because KC would appear to own the LAT domain.
                ctrl->Init();

                // Activate on LAT domain only (additive mode).
                // Actions + defaultController run normally; bicycle model follows object_->pos_.
                ControlActivationMode modes[static_cast<unsigned int>(ControlDomains::COUNT)];
                modes[static_cast<unsigned int>(ControlDomains::DOMAIN_LONG)]  = ControlActivationMode::UNDEFINED;
                modes[static_cast<unsigned int>(ControlDomains::DOMAIN_LAT)]   = ControlActivationMode::ON;
                modes[static_cast<unsigned int>(ControlDomains::DOMAIN_LIGHT)] = ControlActivationMode::UNDEFINED;
                modes[static_cast<unsigned int>(ControlDomains::DOMAIN_ANIM)]  = ControlActivationMode::UNDEFINED;
                ctrl->Activate(modes);

                // Register with scenario engine so Step() is called each frame
                player->scenarioEngine->scenarioReader->AddController(ctrl);

                assignCount++;
            }
            LOG_INFO("GT_Init: KinematicController assigned to {} vehicle(s).", assignCount);
        }

        // 5. Register OSIReporter for global access (for Light state)
        // R5-U4: OSI light state is emitted by GT_OSIReporter reading vehLghtStsList[]
        // directly (R5-U3 single source of truth); the GT_SetLightStateProvider hook is gone.
#ifdef _USE_OSI
        extern void GT_SetCurrentOSIReporter(OSIReporter* reporter);
        if (player->osiReporter)
        {
            GT_SetCurrentOSIReporter(player->osiReporter);
        }
#endif  // _USE_OSI

        // 7. Initialize GT_HostVehicleReporter (separated from OSIReporter)
        {
            std::string exeDir = gt_esmini::GetCurrentModuleDirectory();
            gt_esmini::ConfigLoader config_loader;
            std::string configFile = config_loader.ResolveConfigPath(exeDir, "host_vehicle_config.json");
            gt_esmini::GT_HostVehicleReporter::Instance().Init(48199, configFile, osiTargetIp);
        }

        // 8. Initialize Scenario Variables Reporter (JSON over UDP)
        gt_esmini::GT_ScenarioVariablesReporter::Instance().Init(svPort, osiTargetIp);

        // 9. Initialize VirtualDriver telemetry Reporter (JSON over UDP).
        //    Sends each step only when a VirtualDriver controller is present.
        gt_esmini::GT_VirtualDriverReporter::Instance().Init(vdPort, osiTargetIp);
    }

    // [GT_MOD] DIAGNOSTIC
    int finalQuit = SE_GetQuitFlag();
    if (finalQuit) {
        LOG_ERROR("GT_InitWithArgs: CRITICAL! QuitFlag={} at end of GT_InitWithArgs.", finalQuit);
    }
    // [GT_MOD] END

    return 0;
}

// feature:F7 — forward decl; definition (and the rationale) is below.
static void GT_CaptureVirtualDriverTelemetryFrame(void* player_ptr);

GT_ESMINI_API void GT_Step(double dt)
{
    // Call standard step
    SE_StepDT(static_cast<float>(dt));

    // Update TrafficSignalControllers (auto-cycling)
    gt_esmini::TrafficSignalControllerManager::Instance().StepAll(dt);

    // Update AutoLight
    AutoLightManager::Instance().Update(dt);

    // Update observed vehicle physics (pitch/roll for non-GT-controller vehicles)
    gt_esmini::VehiclePhysicsManager::Instance().Update(dt);

    // Update heading correction (nose-leading behavior for non-GT-controller vehicles)
    gt_esmini::HeadingCorrectionManager::Instance().Update(dt);

    // v3.0.0: Gateway module removed — Object::pos_ is the authoritative state.
    // Post-processed positions (pitch/roll from VehiclePhysicsManager, heading from
    // HeadingCorrectionManager) are already written directly to obj->pos_ by the
    // post-processors, so no sync step is needed. Double-buffered DirtyBits are
    // managed by ScenarioEngine automatically.

    // Update HostVehicleData (using separated GT_HostVehicleReporter)
#ifdef _USE_OSI
    if (player && player->scenarioEngine &&
        gt_esmini::GT_HostVehicleReporter::Instance().IsInitialized())
    {
        auto& hvReporter = gt_esmini::GT_HostVehicleReporter::Instance();

        // v3.0.0: Gateway removed — resolve ego Object* directly from entities
        const auto& entities = player->scenarioEngine->entities_.object_;
        Object* egoObj = nullptr;
        const auto& targetName = hvReporter.GetTargetVehicle();
        if (!targetName.empty())
        {
            for (auto* obj : entities)
            {
                if (obj && obj->name_ == targetName)
                {
                    egoObj = obj;
                    break;
                }
            }
            if (!egoObj)
            {
                static bool warnedOnce = false;
                if (!warnedOnce)
                {
                    LOG_WARN("GT_Step: target_vehicle '{}' not found, falling back to index 0", targetName);
                    warnedOnce = true;
                }
                egoObj = entities.empty() ? nullptr : entities[0];
            }
        }
        else
        {
            egoObj = entities.empty() ? nullptr : entities[0];
        }

        if (egoObj)
        {
            int vehicleId = egoObj->id_;

            // Clear ADAS functions from previous frame
            hvReporter.ClearADASFunctions(vehicleId);

            // Try to get RealDriverController (or PythonDriverController) and pass input data to HostVehicleReporter
            Object* egoObject = player->scenarioEngine->entities_.GetObjectById(vehicleId);
            if (egoObject)
            {
                Controller* ctrl = GT_FindHvdSource(egoObject);
                if (ctrl)
                {
                    auto pushControllerState = [&](auto* concreteCtrl) {
                        if (!concreteCtrl)
                        {
                            return;
                        }

                        double throttle, brake, steering;
                        int gear, lightMask;
                        concreteCtrl->GetInputsForOSI(throttle, brake, steering, gear, lightMask);

                        double rpm, torque;
                        concreteCtrl->GetPowertrainForOSI(rpm, torque);

                        hvReporter.SetInputs(vehicleId, throttle, brake, steering, gear);
                        hvReporter.SetLights(vehicleId, lightMask);
                        hvReporter.SetPowertrain(vehicleId, rpm, torque);

                        std::vector<int> adasStates;
                        concreteCtrl->GetADASStates(adasStates);

                        // Labels and their OSI Name values come from the single slot
                        // table in ControllerRealDriverUtils.hpp, which is also what the
                        // inbound custom_name -> index mapping uses. Keeping one table
                        // is the point: the outbound copy that used to live here had
                        // drifted from the OSI enum around NIGHT_VISION / HEAD_UP_DISPLAY
                        // (capability_model §2.2a residual debt).
                        if (adasStates.size() >= gt_esmini::realdetail::kAdasFunctionCount)
                        {
                            for (std::size_t i = 0; i < gt_esmini::realdetail::kAdasFunctionCount; i++)
                            {
                                const auto& slot = gt_esmini::realdetail::kAdasSlots[i];
                                hvReporter.AddADASFunctionEx(vehicleId,
                                                             slot.osi_name,
                                                             std::string(slot.label),
                                                             adasStates[i],
                                                             {});
                            }
                        }
                    };

                    if (auto* realDriver = dynamic_cast<gt_esmini::ControllerRealDriver*>(ctrl))
                    {
                        pushControllerState(realDriver);
                    }
#ifdef GT_ENABLE_EMBEDDED_PYTHON
                    else if (auto* pythonDriver = dynamic_cast<gt_esmini::ControllerPythonDriver*>(ctrl))
                    {
                        pushControllerState(pythonDriver);
                    }
#endif
                    else if (auto* manualDrive = dynamic_cast<gt_esmini::ControllerManualDrive*>(ctrl))
                    {
                        pushControllerState(manualDrive);

                        // req-vd-ad:REQ-AD-025/028, vd-func:FUNC-075 (design
                        // manualdrive_adas_design.md §8-1/§12) -- same
                        // AddADASFunctionEx path the VirtualDriver branch
                        // below uses, NOT the fixed 24-slot GetADASStates()
                        // path pushControllerState() just used above:
                        // ControllerManualDrive::GetADASStates() deliberately
                        // always returns empty (see its own header comment),
                        // so the `adasStates.size() >= kAdasFunctionCount`
                        // guard inside pushControllerState() never fires for
                        // ManualDrive and the 24-row block above contributes
                        // nothing here -- this loop is ManualDrive's ONLY
                        // source of ADAS rows. The gt.aeb/gt.fcw NAME values
                        // this reads (osi_adas::NAME_AUTOMATIC_EMERGENCY_
                        // BRAKING / NAME_FORWARD_COLLISION_WARNING) are
                        // pinned against the real OSI enum by the file-scope
                        // static_asserts above (another agent's region, left
                        // untouched here); this branch does not need its own.
                        //
                        // req-vd-ad:REQ-AD-028 段b (phase B): the per-row
                        // DriverOverride/custom_state travels alongside the
                        // state/detail through AddADASFunctionEx's defaulted
                        // 6th argument. control's AdasDriverOverride and osi's
                        // AdasFunctionOverride are two mirrors of the same
                        // proto submessage kept in separate modules by
                        // GT_esmini/CLAUDE.md §2 (control must not depend on
                        // osi); this dispatch is the one place that sees both,
                        // so the translation lives here. Only ManualDrive
                        // passes a non-default value -- every other branch
                        // omits the argument and is therefore byte-identical
                        // on the wire (test_hvd_dispatch_invariance.py pins
                        // that). The Reason enum mirror itself is pinned at
                        // FILE SCOPE next to the 4 Name pins, for the same
                        // reason those are: it backs the ManualDrive report
                        // path rather than any one dispatch branch.
                        std::vector<gt_esmini::AdasFunctionState> manualAdasFunctions;
                        manualDrive->GetADASFunctions(manualAdasFunctions);
                        for (const auto& f : manualAdasFunctions)
                        {
                            gt_esmini::AdasFunctionOverride ovr;
                            ovr.reported     = f.driver_override.reported;
                            ovr.active       = f.driver_override.active;
                            ovr.reasons      = f.driver_override.reasons;
                            ovr.custom_state = f.custom_state;
                            hvReporter.AddADASFunctionEx(vehicleId, f.name, f.custom_name, f.state, f.detail, ovr);
                        }
                    }
                    else if (auto* virtualDriver = dynamic_cast<gt_esmini::ControllerVirtualDriver*>(ctrl))
                    {
                        pushControllerState(virtualDriver);

                        // W1: VirtualDriver reports its AD functions through the
                        // Name-enum path instead of the fixed 24-slot label array
                        // above (which its GetADASStates() intentionally leaves
                        // empty). Without this, AEB — implemented and green —
                        // produced no observable OSI evidence at all.
                        //
                        // The mirrored enum values in AdasFunctionReport.hpp
                        // (which must stay OSI-free: control must not depend on
                        // osi) are pinned to the real .proto here, at the one
                        // place that sees both.
                        using OsiName  = osi3::HostVehicleData_VehicleAutomatedDrivingFunction_Name;
                        using OsiState = osi3::HostVehicleData_VehicleAutomatedDrivingFunction_State;
                        static_assert(gt_esmini::osi_adas::NAME_OTHER ==
                                          static_cast<int>(OsiName::HostVehicleData_VehicleAutomatedDrivingFunction_Name_NAME_OTHER),
                                      "OSI Name enum drift: NAME_OTHER");
                        static_assert(
                            gt_esmini::osi_adas::NAME_AUTOMATIC_EMERGENCY_BRAKING ==
                                static_cast<int>(
                                    OsiName::HostVehicleData_VehicleAutomatedDrivingFunction_Name_NAME_AUTOMATIC_EMERGENCY_BRAKING),
                            "OSI Name enum drift: NAME_AUTOMATIC_EMERGENCY_BRAKING");
                        static_assert(
                            gt_esmini::osi_adas::NAME_ADAPTIVE_CRUISE_CONTROL ==
                                static_cast<int>(
                                    OsiName::HostVehicleData_VehicleAutomatedDrivingFunction_Name_NAME_ADAPTIVE_CRUISE_CONTROL),
                            "OSI Name enum drift: NAME_ADAPTIVE_CRUISE_CONTROL");
                        static_assert(gt_esmini::osi_adas::NAME_URBAN_DRIVING ==
                                          static_cast<int>(
                                              OsiName::HostVehicleData_VehicleAutomatedDrivingFunction_Name_NAME_URBAN_DRIVING),
                                      "OSI Name enum drift: NAME_URBAN_DRIVING");
                        static_assert(gt_esmini::osi_adas::STATE_ACTIVE ==
                                          static_cast<int>(
                                              OsiState::HostVehicleData_VehicleAutomatedDrivingFunction_State_STATE_ACTIVE),
                                      "OSI State enum drift: STATE_ACTIVE");
                        static_assert(gt_esmini::osi_adas::STATE_STANDBY ==
                                          static_cast<int>(
                                              OsiState::HostVehicleData_VehicleAutomatedDrivingFunction_State_STATE_STANDBY),
                                      "OSI State enum drift: STATE_STANDBY");
                        static_assert(gt_esmini::osi_adas::STATE_UNAVAILABLE ==
                                          static_cast<int>(
                                              OsiState::HostVehicleData_VehicleAutomatedDrivingFunction_State_STATE_UNAVAILABLE),
                                      "OSI State enum drift: STATE_UNAVAILABLE");

                        std::vector<gt_esmini::AdasFunctionState> adasFunctions;
                        virtualDriver->GetADASFunctions(adasFunctions);
                        for (const auto& f : adasFunctions)
                        {
                            hvReporter.AddADASFunctionEx(vehicleId, f.name, f.custom_name, f.state, f.detail);
                        }
                    }
                }
                else
                {
                    // No GT custom controller with full inputs: estimate HVD from observable vehicle state
                    auto estimated = s_hvdEstimator.Estimate(egoObject, dt);

                    // If KinematicController is active, use its wheel angle directly.
                    // KC already produces a rate-limited, curvature-based value —
                    // bypass HVDEstimator's preview-point attenuation + EMA double-processing.
                    Controller* kinCtrl = egoObject->GetAssignedControllerOftype(
                        Controller::Type::CONTROLLER_TYPE_KINEMATIC);
                    if (kinCtrl && kinCtrl->IsActive())
                    {
                        auto* kc = static_cast<gt_esmini::ControllerKinematic*>(kinCtrl);
                        estimated.steering = kc->GetWheelAngle();
                    }

                    hvReporter.SetInputs(vehicleId, estimated.throttle, estimated.brake,
                                         estimated.steering, estimated.gear);
                    hvReporter.SetLights(vehicleId, estimated.lightMask);
                    hvReporter.SetPowertrain(vehicleId, estimated.rpm, estimated.torque);
                }
            }

            // Update HostVehicleData for target vehicle and send
            hvReporter.UpdateFromObjectState(egoObj);
            hvReporter.Send();
        }
    }
#endif  // _USE_OSI

    // Broadcast scenario variables (JSON over UDP, independent of OSI)
    gt_esmini::GT_ScenarioVariablesReporter::Instance().Update();

    // Broadcast live VirtualDriver telemetry (JSON over UDP). Only the ego's
    // VirtualDriver controller (if any) is reported; no-op otherwise. Shares the
    // exact JSON shape with GT_GetVirtualDriverTelemetry() via ToJson().
    if (gt_esmini::GT_VirtualDriverReporter::Instance().IsInitialized() &&
        player && player->scenarioEngine && !player->scenarioEngine->entities_.object_.empty())
    {
        scenarioengine::Object* egoObj = player->scenarioEngine->entities_.object_[0];
        if (auto* vd = GT_FindControllerOfType<gt_esmini::ControllerVirtualDriver>(egoObj))
        {
            gt_esmini::GT_VirtualDriverReporter::Instance().Send(gt_esmini::ToJson(vd->GetTelemetry()));
        }
    }
    GT_CaptureVirtualDriverTelemetryFrame(player);
}

// feature:F7 — per-frame telemetry capture to a file (JSON Lines).
//
// WHY THIS EXISTS. Validating the override detector's shadow model needs the
// force actually applied and the physical wheel position at EVERY frame. The
// SDLFFBSink log line is emitted once per 50 frames and carries no physical
// axis at all, so it cannot describe a trajectory.
//
// The telemetry block already carries everything required —
// ffb.gates.{effective_force, actual_norm, shadow_norm, residual}, ffb.target_norm,
// sim_time — because those are the SHIPPED detector's own inputs and outputs.
// Capturing them is therefore not a second model: a replay against this file is
// a replay against the product. That is the whole point; anything that
// re-implemented the shadow offline would be validating a copy.
//
// Opt-in via GT_VD_TELEMETRY_JSONL=<path>; a no-op otherwise, so no gate,
// package, or interactive run changes behaviour. Append mode, flushed per
// frame, so an external supervisor can tail it as a live safety signal and a
// hard kill still leaves every frame written up to that point.
static void GT_CaptureVirtualDriverTelemetryFrame(void* player_ptr)
{
    static bool  resolved = false;
    static FILE* fp       = nullptr;
    if (!resolved)
    {
        resolved = true;
        if (const char* path = std::getenv("GT_VD_TELEMETRY_JSONL"))
        {
            if (path[0] != '\0')
            {
                fp = std::fopen(path, "w");
                if (!fp)
                    LOG_WARN("GT_VD_TELEMETRY_JSONL: cannot open '{}' for writing", path);
                else
                    LOG_INFO("GT_VD_TELEMETRY_JSONL: capturing per-frame telemetry to '{}'", path);
            }
        }
    }
    if (!fp) return;

    auto* player = static_cast<ScenarioPlayer*>(player_ptr);
    if (!player || !player->scenarioEngine || player->scenarioEngine->entities_.object_.empty()) return;
    scenarioengine::Object* egoObj = player->scenarioEngine->entities_.object_[0];
    auto* vd = GT_FindControllerOfType<gt_esmini::ControllerVirtualDriver>(egoObj);
    if (!vd) return;

    const std::string line = gt_esmini::ToJson(vd->GetTelemetry());
    std::fwrite(line.data(), 1, line.size(), fp);
    std::fputc('\n', fp);
    std::fflush(fp);
}

GT_ESMINI_API void GT_EnableVehiclePhysics()
{
    gt_esmini::VehiclePhysicsManager::Instance().Enable(true);
}

GT_ESMINI_API void GT_EnableHeadingCorrection()
{
    gt_esmini::HeadingCorrectionManager::Instance().Enable(true);
}

GT_ESMINI_API void GT_EnableAutoLight()
{
    AutoLightManager::Instance().Enable(true);
}

GT_ESMINI_API void GT_Close()
{
    // Release FFB before teardown so the wheel isn't left under torque
    if (player && player->scenarioEngine)
    {
        for (auto* obj : player->scenarioEngine->entities_.object_)
        {
            if (obj)
            {
                for (auto* ctrl : obj->controllers_)
                {
                    if (ctrl)
                    {
                        ctrl->Deactivate();
                    }
                }
            }
        }
    }

    s_hvdEstimator.Reset();
    gt_esmini::VehiclePhysicsManager::Instance().Close();
    gt_esmini::HeadingCorrectionManager::Instance().Close();
    gt_esmini::TrafficSignalControllerManager::Instance().Clear();
    // feature:F7 — the ledger is keyed by (object id, controller address), and a
    // host process runs many scenarios in a row (web backend, gt_sim_test batch).
    // Both keys get reused, so a surviving entry could make the next run's
    // controller look like it already owns — or has already lost — a domain.
    gt_esmini::DomainOwnershipLedger::Instance().Clear();
    gt_esmini::GT_HostVehicleReporter::Instance().Close();
    gt_esmini::GT_ScenarioVariablesReporter::Instance().Close();
    gt_esmini::GT_VirtualDriverReporter::Instance().Close();
    AutoLightManager::Instance().Close();
    SE_Close();
}

GT_ESMINI_API int GT_GetLightState(int vehicleId, int lightType)
{
    if (!player || !player->scenarioEngine) return -1;
    
    for (auto* obj : player->scenarioEngine->entities_.object_)
    {
        if (obj->id_ == vehicleId && obj->type_ == scenarioengine::Object::Type::VEHICLE)
        {
             // R5-U3: read straight from the native storage via the bridge (no extension needed).
             gt_esmini::LightState state = gt_esmini::ReadLight(obj, static_cast<gt_esmini::VehicleLightType>(lightType));
             return static_cast<int>(state.mode);
        }
    }
    return -1; // Vehicle not found
}

GT_ESMINI_API void GT_SetExternalLightState(int vehicleId, int lightType, int mode)
{
    if (!player || !player->scenarioEngine) return;

    for (auto* obj : player->scenarioEngine->entities_.object_)
    {
        if (obj->id_ == vehicleId && obj->type_ == scenarioengine::Object::Type::VEHICLE)
        {
            // R5-U3: write straight to native storage via the bridge. Preserves the prior
            // bypass semantics (no LightSource tracking, no extension required).
            gt_esmini::LightState state;
            state.mode = static_cast<gt_esmini::LightState::Mode>(mode);
            gt_esmini::ApplyLight(obj, static_cast<gt_esmini::VehicleLightType>(lightType), state);
            return;
        }
    }
}

GT_ESMINI_API int GT_GetLocalIdFromGlobalId(int global_id)
{
    // Access Raw OSI data via esmini API
    const char* rawPtr = SE_GetOSIGroundTruthRaw();
    if (!rawPtr) return -1;

    // Cast to osi3::GroundTruth*
    // Note: esminiLib returns the internal pointer which is osi3::GroundTruth*
    const osi3::GroundTruth* gt = reinterpret_cast<const osi3::GroundTruth*>(rawPtr);

    // Search Moving Objects
    // OSI IDs are generally uint64, esmini global_id is int (but stored as uint64 in OSI)
    for (int i = 0; i < gt->moving_object_size(); ++i) {
        const auto& obj = gt->moving_object(i);
        if (obj.id().value() == (uint64_t)global_id) {
             // Found object, parse source_reference for Local ID
             for (int j=0; j < obj.source_reference_size(); ++j) {
                 const auto& ref = obj.source_reference(j);
                 for (int k=0; k < ref.identifier_size(); ++k) {
                     const std::string& id_str = ref.identifier(k);
                     // Format created in OSIReporter.cpp: "entity_id:{id}"
                     if (id_str.find("entity_id:") == 0) {
                         try {
                             return std::stoi(id_str.substr(10));
                         } catch (...) {
                             return -1;
                         }
                     }
                 }
             }
        }
    }
    
    // Also check Stationary Objects if necessary, but TrafficUpdates typically target MovingObjects
    // (Vehicles, Pedestrians)
    
    return -1;
}

GT_ESMINI_API int GT_ReportObjectVel(int object_id, float timestamp, float x_vel, float y_vel, float z_vel)
{
    (void)timestamp;  // v3.0.0: timestamp removed from SE_ReportObjectVel
    // Call original esminiLib function to update velocity vector
    int ret = SE_ReportObjectVel(object_id, x_vel, y_vel, z_vel);
    if (ret != 0)
    {
        return ret;
    }

    // [GT_MOD] Sync scalar speed to match velocity vector magnitude
    double speed = std::sqrt(static_cast<double>(x_vel) * x_vel +
                              static_cast<double>(y_vel) * y_vel +
                              static_cast<double>(z_vel) * z_vel);

    // v3.0.0: Gateway removed — write directly to Object
    if (player && player->scenarioEngine)
    {
        Object* obj = player->scenarioEngine->entities_.GetObjectById(object_id);
        if (obj)
        {
            obj->SetSpeed(speed);
        }
    }

    return 0;
}

// =====================================
// HostVehicleData APIs
// =====================================

GT_ESMINI_API void GT_SetHostVehicleInputs(int vehicle_id, double throttle, double brake, double steering, int gear)
{
#ifdef _USE_OSI
    if (gt_esmini::GT_HostVehicleReporter::Instance().IsInitialized())
    {
        // If vehicle_id is -1, use the first vehicle (ego)
        int actual_id = vehicle_id;
        if (actual_id < 0 && player && player->scenarioEngine &&
            !player->scenarioEngine->entities_.object_.empty())
        {
            actual_id = player->scenarioEngine->entities_.object_[0]->id_;
        }

        if (actual_id >= 0)
        {
            gt_esmini::GT_HostVehicleReporter::Instance().SetInputs(actual_id, throttle, brake, steering, gear);
        }
    }
#endif
}

GT_ESMINI_API void GT_SetHostVehicleLights(int vehicle_id, int light_mask)
{
#ifdef _USE_OSI
    if (gt_esmini::GT_HostVehicleReporter::Instance().IsInitialized())
    {
        // If vehicle_id is -1, use the first vehicle (ego)
        int actual_id = vehicle_id;
        if (actual_id < 0 && player && player->scenarioEngine &&
            !player->scenarioEngine->entities_.object_.empty())
        {
            actual_id = player->scenarioEngine->entities_.object_[0]->id_;
        }

        if (actual_id >= 0)
        {
            gt_esmini::GT_HostVehicleReporter::Instance().SetLights(actual_id, light_mask);
        }
    }
#endif
}

GT_ESMINI_API int GT_GetTrafficSignalState(int road_id, int index, char* state, int bufferSize)
{
    if (!state || bufferSize <= 0) return -1;

    roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();
    if (!odr) return -1;

    roadmanager::Road* road = odr->GetRoadById(road_id);
    if (!road) return -1;

    if (index < 0 || index >= static_cast<int>(road->GetNumberOfSignals())) return -1;

    roadmanager::Signal* signal = road->GetSignal(static_cast<idx_t>(index));
    if (!signal) return -1;

    auto* tl = dynamic_cast<roadmanager::TrafficLight*>(signal);
    if (!tl) return -1;  // Not a traffic light

    std::string stateStr = tl->GetStateString();
    strncpy(state, stateStr.c_str(), bufferSize - 1);
    state[bufferSize - 1] = '\0';
    return 0;
}

GT_ESMINI_API int GT_GetVirtualDriverTelemetry(int vehicle_id, char* out_json, int buf_size)
{
    if (!out_json || buf_size <= 0) return -1;
    if (!player || !player->scenarioEngine) return -1;

    int actual_id = vehicle_id;
    if (actual_id < 0 && !player->scenarioEngine->entities_.object_.empty())
        actual_id = player->scenarioEngine->entities_.object_[0]->id_;
    if (actual_id < 0) return -1;

    scenarioengine::Object* obj = player->scenarioEngine->entities_.GetObjectById(actual_id);
    if (!obj) return -1;

    auto* vd = GT_FindControllerOfType<gt_esmini::ControllerVirtualDriver>(obj);
    if (!vd) return -1;

    const gt_esmini::VirtualDriverTelemetry& t = vd->GetTelemetry();

    std::string s = gt_esmini::ToJson(t);
    int n = static_cast<int>(s.size());
    if (n >= buf_size)
    {
        std::memcpy(out_json, s.c_str(), static_cast<size_t>(buf_size - 1));
        out_json[buf_size - 1] = '\0';
        return buf_size - 1;
    }
    std::memcpy(out_json, s.c_str(), static_cast<size_t>(n));
    out_json[n] = '\0';
    return n;
}

GT_ESMINI_API void GT_SetHostVehiclePowertrain(int vehicle_id, double rpm, double torque)
{
#ifdef _USE_OSI
    if (gt_esmini::GT_HostVehicleReporter::Instance().IsInitialized())
    {
        // If vehicle_id is -1, use the first vehicle (ego)
        int actual_id = vehicle_id;
        if (actual_id < 0 && player && player->scenarioEngine &&
            !player->scenarioEngine->entities_.object_.empty())
        {
            actual_id = player->scenarioEngine->entities_.object_[0]->id_;
        }

        if (actual_id >= 0)
        {
            gt_esmini::GT_HostVehicleReporter::Instance().SetPowertrain(actual_id, rpm, torque);
        }
    }
#endif
}

GT_ESMINI_API int GT_SetDriveMode(const char* mode)
{
    if (mode == nullptr) return -1;
    return s_hvdEstimator.SetActiveMode(std::string(mode)) ? 0 : -1;
}

// In-process OSI HostVehicleData access (req-vd-ad:REQ-AD-028 段c, vd-func:FUNC-075).
// See the doc-comment on the declaration (GT_esminiLib.hpp) for the full
// contract. Short version: GT_Step already keeps GT_HostVehicleReporter's
// buffer current every frame (HVD is not frequency-gated like GroundTruth),
// so this does not force a re-serialization -- it only decides whether the
// caller is allowed to see the buffer that is already there.
GT_ESMINI_API const void* GT_GetOSIHostVehicleData(int vehicle_id, int* size)
{
#ifdef _USE_OSI
    if (size)
    {
        *size = 0;
    }

    if (!player || !player->scenarioEngine)
    {
        return nullptr;
    }

    auto& hvReporter = gt_esmini::GT_HostVehicleReporter::Instance();
    if (!hvReporter.IsInitialized())
    {
        return nullptr;
    }

    const auto& entities = player->scenarioEngine->entities_.object_;

    // Resolve vehicle_id < 0 to the ego, same idiom as GT_SetHostVehicleInputs.
    int actual_id = vehicle_id;
    if (actual_id < 0 && !entities.empty())
    {
        actual_id = entities[0]->id_;
    }
    if (actual_id < 0)
    {
        return nullptr;
    }

    // GT_HostVehicleReporter holds exactly ONE serialization buffer: whichever
    // vehicle GT_Step most recently resolved as ego/target (GetTargetVehicle()
    // name match, else entities[0] -- the same resolution GT_Step performs
    // right before calling UpdateFromObjectState(), see the HVD block near the
    // top of GT_Step). Recompute that same resolution here and refuse the
    // request if it does not match actual_id: silently handing back a
    // DIFFERENT vehicle's bytes would let a caller believe it measured the
    // vehicle it asked for when it did not. This repo has a documented history
    // of fabricated-measurement / silent-instrument failures; getting nothing
    // back is a visible, honest failure, getting the wrong vehicle's data back
    // is not.
    scenarioengine::Object* egoObj    = nullptr;
    const auto&             targetName = hvReporter.GetTargetVehicle();
    if (!targetName.empty())
    {
        for (auto* obj : entities)
        {
            if (obj && obj->name_ == targetName)
            {
                egoObj = obj;
                break;
            }
        }
        if (!egoObj)
        {
            egoObj = entities.empty() ? nullptr : entities[0];
        }
    }
    else
    {
        egoObj = entities.empty() ? nullptr : entities[0];
    }

    if (!egoObj || egoObj->id_ != actual_id)
    {
        return nullptr;
    }

    return hvReporter.GetSerializedHostVehicleData(size);
#else
    if (size)
    {
        *size = 0;
    }
    (void)vehicle_id;
    return nullptr;
#endif  // _USE_OSI
}

// GT-flavored variant of SE_OpenOSISocket (auto-enables per-frame OSI frequency);
// core SE_OpenOSISocket is vanilla upstream (audit BND-2 / R5-U1).
//
// Opens the OSI groundtruth UDP socket and, unlike vanilla SE_OpenOSISocket,
// forces the OSI frequency to 1 (send every frame) when it was left at 0. The
// in-process verification harness (gt_lib.py / gt_sim_test) relies on this so
// that GT_Step emits OSI each frame even when --osi was not given a frequency.
// Returns the actual OpenSocket result (0 on success, -1 on failure); -1 if
// player/osiReporter are null or _USE_OSI is undefined.
GT_ESMINI_API int GT_OpenOSISocket(const char* ipaddr)
{
#ifdef _USE_OSI
    LOG_INFO("GT_OpenOSISocket: _USE_OSI is DEFINED");
    if (player == nullptr)
    {
        LOG_ERROR("GT_OpenOSISocket: player is nullptr!");
        return -1;
    }

    if (player->osiReporter == nullptr)
    {
        LOG_ERROR("GT_OpenOSISocket: osiReporter is nullptr!");
        return -1;
    }

    // Set OSI frequency to 1 (send every frame) if not already set
    if (player->osiReporter->GetOSIFrequency() == 0)
    {
        LOG_INFO("GT_OpenOSISocket: OSI frequency was 0, setting to 1 (send every frame)");
        player->osiReporter->SetOSIFrequency(1);
    }
    else
    {
        LOG_INFO("GT_OpenOSISocket: OSI frequency already set to {}", player->osiReporter->GetOSIFrequency());
    }

    LOG_INFO("GT_OpenOSISocket: Calling OpenSocket({})", ipaddr);
    int result = player->osiReporter->OpenSocket(ipaddr);
    LOG_INFO("GT_OpenOSISocket: OpenSocket returned {}", result);
    return result;
#else
    LOG_WARN("GT_OpenOSISocket: _USE_OSI is NOT DEFINED - OSI support is disabled!");
    (void)ipaddr;
    return -1;
#endif  // _USE_OSI
}

GT_ESMINI_API void GT_SetLogCallback(GT_LogCallbackFn callback, void* user_data)
{
    // Register the core relay here too, so a callback attached before
    // GT_Init/GT_InitWithArgs receives the earliest (pre-init) log lines.
    GT_RegisterCoreLogRelayOnce();
    std::lock_guard<std::mutex> lock(s_logMutex);
    s_userLogCallback = callback;
    s_userLogUserData = user_data;
}

GT_ESMINI_API int GT_GetLastError(char* buffer, int buffer_size)
{
    if (buffer == nullptr || buffer_size <= 0)
    {
        return -1;
    }
    std::lock_guard<std::mutex> lock(s_logMutex);
    if (s_lastError.empty())
    {
        buffer[0] = '\0';
        return 0;
    }
    int n = static_cast<int>(s_lastError.size());
    if (n >= buffer_size)
    {
        n = buffer_size - 1;
    }
    std::memcpy(buffer, s_lastError.c_str(), static_cast<size_t>(n));
    buffer[n] = '\0';
    return n;
}
