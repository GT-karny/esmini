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
#include "gt_esmini/control/HeadingCorrectionManager.hpp"

#include "gt_esmini/control/common/ModuleDirectory.hpp"

// File-scope HVD estimator for non-GT-controller vehicles
static gt_esmini::HVDEstimator s_hvdEstimator;

// AutoLightManager Implementation
class AutoLightManager
{
public:
    static AutoLightManager& Instance()
    {
        static AutoLightManager instance;
        return instance;
    }

    void Init(scenarioengine::Entities* entities)
    {
        controllers_.clear();
        if (!entities) return;

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
                    std::cout << "AutoLight: Skipping Ego vehicle (ID: " << egoId_ << ")" << std::endl;
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
                controllers_.push_back(std::make_unique<gt_esmini::AutoLightController>(vehicle, ext));
            }
        }
    }

    void SetEgoless(bool egoless)
    {
        egoless_ = egoless;
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
    AutoLightManager() : enabled_(false), egoless_(false), egoId_(-1) {}
    
    std::vector<std::unique_ptr<gt_esmini::AutoLightController>> controllers_;
    bool enabled_;
    bool egoless_;
    int egoId_;
};

// Hook registration function (externally defined in GT_OSIReporter.cpp part of ScenarioEngine)
void GT_SetLightStateProvider(std::function<::gt_esmini::LightState(void*, int)> provider);

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
        std::cerr << "[GT_esmini] generating road model: " << cmd << std::endl;
        int rc = GT_RunProcess(cmd);
        if (rc == 0 && fs::exists(cachePath, ec))
        {
            return cachePath.string();
        }

        std::cerr << "[GT_esmini] road model generation failed (rc=" << rc << "); using runtime generation" << std::endl;
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
            std::cerr << "[GT_esmini] injected cached road model: " << model << std::endl;
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
    // 1. Create a sanitized version of the scenario
    // esmini throws error on AppearanceAction/LightStateAction.
    // We strip them for the main initialization.
    std::string sanitizedFile = GT_MakeSanitizedScenarioPath(oscFilename).string();
    if (!CreateSanitizedScenario(oscFilename, sanitizedFile))
    {
         std::cerr << "GT_Init: Failed to create sanitized scenario file." << std::endl;
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
            std::cerr << "GT_Init: Failed to reload XOSC for extensions: " << result.description() << std::endl;
        }

        // 3b. Initialize TrafficSignalControllers
        gt_esmini::TrafficSignalControllerManager::Instance().InitAll();

        // 4. Initialize AutoLightManager
        AutoLightManager::Instance().Init(&player->scenarioEngine->entities_);

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

        // 5. Register Hook for OSIReporter
        // Forward declaration of GT_SetLightStateProvider (defined in GT_OSIReporter.cpp)
        extern void GT_SetLightStateProvider(std::function<::gt_esmini::LightState(void*, int)> provider);

        GT_SetLightStateProvider([](void* v, int t) -> gt_esmini::LightState {
            // R5-U3: read straight from the native vehLghtStsList[] via the bridge. Works
            // for ANY vehicle, with or without a VehicleLightExtension.
            auto* obj = static_cast<scenarioengine::Object*>(static_cast<scenarioengine::Vehicle*>(v));
            return gt_esmini::ReadLight(obj, static_cast<gt_esmini::VehicleLightType>(t));
        });

        // 6. Register OSIReporter for global access (for Light state)
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
    std::cerr << "[GT_esmini] GT_InitWithArgs called with argc=" << argc << std::endl;
    if (argc > 0 && argv) {
        std::cerr << "[GT_esmini] argv[0]=" << (argv[0] ? argv[0] : "NULL") << std::endl;
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
        std::cerr << "[GT_esmini] Sanitizing filename: " << filename << std::endl;
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
             std::cerr << "GT_InitWithArgs: Failed to create sanitized scenario file." << std::endl;
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
    std::cerr << "[GT_esmini] Calling SE_InitWithArgs with " << newArgv.size() << " args." << std::endl;
    int ret = SE_InitWithArgs(static_cast<int>(newArgv.size()), newArgv.data());
    std::cerr << "[GT_esmini] SE_InitWithArgs returned: " << ret << std::endl;
    
    GT_RemoveSanitizedScenario(sanitizedFile);

    if (ret != 0)
    {
        return ret;
    }

    // [GT_MOD] DIAGNOSTIC & FIX: Check and Reset QuitFlag
    int postSeInitQuit = SE_GetQuitFlag();
    if (postSeInitQuit) {
        std::cout << "GT_InitWithArgs: WARNING: SE_InitWithArgs returned 0 but QuitFlag is " << postSeInitQuit << ". Forcing reset." << std::endl;
        if (player) {
            player->SetQuitRequest(false);
            std::cout << "GT_InitWithArgs: QuitFlag forced to 0." << std::endl;
        }
    } else {
        std::cout << "GT_InitWithArgs: SE_InitWithArgs OK. QuitFlag=0." << std::endl;
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
            std::cerr << "GT_InitWithArgs: Failed to reload XOSC for extensions: " << result.description() << std::endl;
        }

        // 3b. Initialize TrafficSignalControllers
        gt_esmini::TrafficSignalControllerManager::Instance().InitAll();

        // 4. Initialize AutoLightManager
        AutoLightManager::Instance().Init(&player->scenarioEngine->entities_);

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
             std::cout << "GT_Init: AutoLight enabled via argument." << std::endl;
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
                    std::cout << "GT_Init: VehiclePhysics enabled via argument." << std::endl;
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
                    std::cout << "GT_Init: HeadingCorrection enabled via argument." << std::endl;
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
                    std::cout << "GT_Init: RouteDrive timing=" << routeDriveTiming << " gap=" << routeDriveGap << std::endl;
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
                std::cout << "GT_Init: RouteDriveController assigned to ego '" << ego->GetName() << "'." << std::endl;
            }
            else if (ego != nullptr)
            {
                std::cout << "GT_Init: RouteDriveController skipped - ego '" << ego->GetName()
                          << "' already has an explicit controller." << std::endl;
            }
            else
            {
                std::cout << "GT_Init: RouteDriveController - no ego vehicle found." << std::endl;
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
            std::cout << "GT_Init: KinematicController assigned to " << assignCount << " vehicle(s)." << std::endl;
        }

        // 5. Register Hook for OSIReporter
        extern void GT_SetLightStateProvider(std::function<::gt_esmini::LightState(void*, int)> provider);

        GT_SetLightStateProvider([](void* v, int t) -> gt_esmini::LightState {
            // R5-U3: read straight from the native vehLghtStsList[] via the bridge. Works
            // for ANY vehicle, with or without a VehicleLightExtension.
            auto* obj = static_cast<scenarioengine::Object*>(static_cast<scenarioengine::Vehicle*>(v));
            return gt_esmini::ReadLight(obj, static_cast<gt_esmini::VehicleLightType>(t));
        });

        // 6. Register OSIReporter for global access (for Light state)
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
        std::cerr << "GT_InitWithArgs: CRITICAL! QuitFlag=" << finalQuit << " at end of GT_InitWithArgs." << std::endl;
    }
    // [GT_MOD] END

    return 0;
}

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
                Controller* ctrl = egoObject->GetController(CONTROLLER_REAL_DRIVER_TYPE_NAME);
#ifdef GT_ENABLE_EMBEDDED_PYTHON
                if (!ctrl)
                {
                    ctrl = egoObject->GetController(CONTROLLER_PYTHON_DRIVER_TYPE_NAME);
                }
#endif
                if (!ctrl)
                {
                    ctrl = egoObject->GetController(CONTROLLER_MANUAL_DRIVE_TYPE_NAME);
                }
                if (!ctrl)
                {
                    ctrl = egoObject->GetController(CONTROLLER_VIRTUAL_DRIVER_TYPE_NAME);
                }
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

                        static const char* adasNames[] = {
                            "BLIND_SPOT_WARNING",                  // 0
                            "FORWARD_COLLISION_WARNING",           // 1
                            "LANE_DEPARTURE_WARNING",              // 2
                            "PARKING_COLLISION_WARNING",           // 3
                            "REAR_CROSS_TRAFFIC_WARNING",          // 4
                            "AUTOMATIC_EMERGENCY_BRAKING",         // 5
                            "AUTOMATIC_EMERGENCY_STEERING",        // 6
                            "REVERSE_AUTOMATIC_EMERGENCY_BRAKING", // 7
                            "ADAPTIVE_CRUISE_CONTROL",             // 8
                            "LANE_KEEPING_ASSIST",                 // 9
                            "ACTIVE_DRIVING_ASSISTANCE",           // 10
                            "BACKUP_CAMERA",                       // 11
                            "SURROUND_VIEW_CAMERA",                // 12
                            "NIGHT_VISION",                        // 13
                            "HEAD_UP_DISPLAY",                     // 14
                            "ACTIVE_PARKING_ASSISTANCE",           // 15
                            "REMOTE_PARKING_ASSISTANCE",           // 16
                            "TRAILER_ASSISTANCE",                  // 17
                            "AUTOMATIC_HIGH_BEAMS",                // 18
                            "DRIVER_MONITORING",                   // 19
                            "URBAN_DRIVING",                       // 20
                            "HIGHWAY_AUTOPILOT",                   // 21
                            "CRUISE_CONTROL",                      // 22
                            "SPEED_LIMIT_CONTROL",                 // 23
                        };

                        if (adasStates.size() >= 24)
                        {
                            for (int i = 0; i < 24; i++)
                            {
                                hvReporter.AddADASFunction(vehicleId, adasNames[i], adasStates[i]);
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
                    }
                    else if (auto* virtualDriver = dynamic_cast<gt_esmini::ControllerVirtualDriver*>(ctrl))
                    {
                        pushControllerState(virtualDriver);
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
        scenarioengine::Controller* ctrl =
            egoObj ? egoObj->GetController(CONTROLLER_VIRTUAL_DRIVER_TYPE_NAME) : nullptr;
        if (auto* vd = dynamic_cast<gt_esmini::ControllerVirtualDriver*>(ctrl))
        {
            gt_esmini::GT_VirtualDriverReporter::Instance().Send(gt_esmini::ToJson(vd->GetTelemetry()));
        }
    }
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

    scenarioengine::Controller* ctrl = obj->GetController(CONTROLLER_VIRTUAL_DRIVER_TYPE_NAME);
    auto* vd = dynamic_cast<gt_esmini::ControllerVirtualDriver*>(ctrl);
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

