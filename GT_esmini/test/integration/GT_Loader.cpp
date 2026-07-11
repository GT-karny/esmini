/*
 * GT_esmini - Integration test loader (GT_esmini_Integration_* ctest entries)
 *
 * Loads a scenario through GT_InitWithArgs, steps it headless and verifies
 * explicit expectations passed on the command line (audit TST-3: no more
 * "pass if it does not crash").
 *
 * Exit code 0 = all expectations met, 1 = any failure.
 *
 * Usage:
 *   GT_Loader <xosc> [--path <dir>]... [options] [expectations]
 *
 * Options:
 *   --duration <sec>            Max simulated time (default 15.0). The loop also
 *                               stops early when the scenario raises its quit flag
 *                               (StopTrigger), which is the normal completion path.
 *   --no-autolight              Do not enable the GT AutoLight controller
 *                               (default: enabled, mirroring GT_Sim usage).
 *   --light-vehicle-id <id>     Object id whose lights/position are sampled (default 0).
 *
 * Expectations (each adds a hard assertion):
 *   --expect-object <name>      Entity with this name must exist after init.
 *   --expect-min-objects <n>    At least n entities after init (default 1).
 *   --expect-min-sim-time <t>   Final simulation time must be >= t seconds
 *                               (catches scenarios that die mid-run).
 *   --expect-light-on <type>    Light <type> (GT_GetLightState index) must be
 *                               observed ON or FLASHING at least once during the run.
 *   --expect-light-never-on <type>
 *                               Light <type> must stay OFF for the whole run
 *                               (graceful-degradation checks for invalid actions).
 *   --expect-move <meters>      Vehicle --light-vehicle-id must travel at least
 *                               this XY distance from its initial position.
 */

#include <gt_esmini/core/GT_esminiLib.hpp>
#include "esminiLib.hpp"  // SE_* state queries (exported by GT_esminiLib)

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace
{
    struct Options
    {
        std::string              xoscFile;
        std::vector<std::string> resourcePaths;
        std::vector<std::string> extraArgs;
        double                   duration        = 15.0;
        bool                     enableAutoLight = true;
        int                      lightVehicleId  = 0;

        // Expectations
        std::vector<std::string> expectObjects;
        int                      expectMinObjects = 1;
        double                   expectMinSimTime = 0.0;
        std::vector<int>         expectLightOn;
        std::vector<int>         expectLightNeverOn;
        double                   expectMove = -1.0;  // < 0 = not checked
    };

    int g_failures = 0;

    void Check(bool ok, const std::string& what)
    {
        if (ok)
        {
            std::cout << "[GT_Loader] PASS: " << what << std::endl;
        }
        else
        {
            std::cout << "[GT_Loader] FAIL: " << what << std::endl;
            ++g_failures;
        }
    }

    const char* LightName(int type)
    {
        static const std::map<int, const char*> names = {{0, "daytimeRunning"},
                                                         {1, "lowBeam"},
                                                         {2, "highBeam"},
                                                         {3, "fog"},
                                                         {4, "fogFront"},
                                                         {5, "fogRear"},
                                                         {6, "brake"},
                                                         {7, "warning"},
                                                         {8, "indicatorLeft"},
                                                         {9, "indicatorRight"},
                                                         {10, "reversing"},
                                                         {11, "licensePlate"},
                                                         {12, "specialPurpose"}};
        auto it = names.find(type);
        return it != names.end() ? it->second : "unknown";
    }
}  // namespace

int main(int argc, char** argv)
{
    Options opt;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];

        auto nextArg = [&](const char* flag) -> std::string
        {
            if (i + 1 >= argc)
            {
                std::cerr << "[GT_Loader] Missing value for " << flag << std::endl;
                exit(1);
            }
            return argv[++i];
        };

        if (arg == "--path")
        {
            opt.resourcePaths.push_back(nextArg("--path"));
        }
        else if (arg == "--duration")
        {
            opt.duration = std::stod(nextArg("--duration"));
        }
        else if (arg == "--no-autolight")
        {
            opt.enableAutoLight = false;
        }
        else if (arg == "--light-vehicle-id")
        {
            opt.lightVehicleId = std::stoi(nextArg("--light-vehicle-id"));
        }
        else if (arg == "--expect-object")
        {
            opt.expectObjects.push_back(nextArg("--expect-object"));
        }
        else if (arg == "--expect-min-objects")
        {
            opt.expectMinObjects = std::stoi(nextArg("--expect-min-objects"));
        }
        else if (arg == "--expect-min-sim-time")
        {
            opt.expectMinSimTime = std::stod(nextArg("--expect-min-sim-time"));
        }
        else if (arg == "--expect-light-on")
        {
            opt.expectLightOn.push_back(std::stoi(nextArg("--expect-light-on")));
        }
        else if (arg == "--expect-light-never-on")
        {
            opt.expectLightNeverOn.push_back(std::stoi(nextArg("--expect-light-never-on")));
        }
        else if (arg == "--expect-move")
        {
            opt.expectMove = std::stod(nextArg("--expect-move"));
        }
        else if (arg.rfind("--", 0) == 0)
        {
            opt.extraArgs.push_back(arg);  // forwarded verbatim to GT_InitWithArgs
        }
        else
        {
            opt.xoscFile = arg;
        }
    }

    if (opt.xoscFile.empty())
    {
        std::cerr << "Usage: GT_Loader <xosc_file> [--path <dir>]... [--duration <sec>] [expectations]" << std::endl;
        return 1;
    }

    std::cout << "[GT_Loader] Loading scenario: " << opt.xoscFile << std::endl;

    std::vector<std::string> initArgs;
    initArgs.emplace_back("GT_Loader");
    initArgs.emplace_back("--osc");
    initArgs.emplace_back(opt.xoscFile);
    initArgs.emplace_back("--headless");
    for (const auto& pathArg : opt.resourcePaths)
    {
        initArgs.emplace_back("--path");
        initArgs.emplace_back(pathArg);
    }
    for (const auto& extra : opt.extraArgs)
    {
        initArgs.push_back(extra);
    }

    std::vector<const char*> initArgv;
    initArgv.reserve(initArgs.size());
    for (const auto& a : initArgs)
    {
        initArgv.push_back(a.c_str());
    }

    // Assertion 1: initialization must succeed.
    const int initRc = GT_InitWithArgs(static_cast<int>(initArgv.size()), initArgv.data());
    Check(initRc == 0, "GT_InitWithArgs rc == 0 (got " + std::to_string(initRc) + ")");
    if (initRc != 0)
    {
        return 1;  // nothing else is meaningful
    }

    // Assertion 2: entity population.
    const int numObjects = SE_GetNumberOfObjects();
    Check(numObjects >= opt.expectMinObjects,
          "object count >= " + std::to_string(opt.expectMinObjects) + " (got " + std::to_string(numObjects) + ")");

    for (const auto& name : opt.expectObjects)
    {
        bool found = false;
        for (int idx = 0; idx < numObjects; ++idx)
        {
            const char* objName = SE_GetObjectName(SE_GetId(idx));
            if (objName != nullptr && name == objName)
            {
                found = true;
                break;
            }
        }
        Check(found, "entity '" + name + "' exists");
    }

    if (opt.enableAutoLight)
    {
        GT_EnableAutoLight();
    }

    // Record initial position of the observed vehicle for --expect-move.
    SE_ScenarioObjectState state;
    std::memset(&state, 0, sizeof(state));
    double x0        = 0.0;
    double y0        = 0.0;
    bool   haveState = (SE_GetObjectState(opt.lightVehicleId, &state) == 0);
    if (haveState)
    {
        x0 = state.x;
        y0 = state.y;
    }

    // Step loop: run until quit flag (StopTrigger) or duration cap.
    const double        stepTime = 0.05;
    const int           maxSteps = static_cast<int>(opt.duration / stepTime);
    std::map<int, bool> lightSeenOn;     // type -> observed ON/FLASHING at least once
    std::map<int, int>  lightViolation;  // type -> mode observed for never-on lights
    for (int t : opt.expectLightOn)
    {
        lightSeenOn[t] = false;
    }

    int steps = 0;
    for (; steps < maxSteps; ++steps)
    {
        GT_Step(stepTime);

        for (auto& kv : lightSeenOn)
        {
            const int mode = GT_GetLightState(opt.lightVehicleId, kv.first);
            if (mode == 1 || mode == 2)  // ON or FLASHING
            {
                kv.second = true;
            }
        }
        for (int t : opt.expectLightNeverOn)
        {
            const int mode = GT_GetLightState(opt.lightVehicleId, t);
            if (mode == 1 || mode == 2)
            {
                lightViolation[t] = mode;
            }
        }

        if (SE_GetQuitFlag() != 0)
        {
            std::cout << "[GT_Loader] Quit flag raised at t=" << SE_GetSimulationTime() << "s" << std::endl;
            break;
        }
    }

    const double simTime = SE_GetSimulationTime();
    std::cout << "[GT_Loader] Simulation finished: " << steps << " steps, simTime=" << simTime << "s" << std::endl;

    // Assertion 3: the scenario ran (did not die right after init).
    if (opt.expectMinSimTime > 0.0)
    {
        Check(simTime >= opt.expectMinSimTime,
              "sim time >= " + std::to_string(opt.expectMinSimTime) + "s (got " + std::to_string(simTime) + "s)");
    }

    // Assertion 4: light expectations.
    for (const auto& kv : lightSeenOn)
    {
        Check(kv.second, std::string("light '") + LightName(kv.first) + "' observed ON/FLASHING during run");
    }
    for (int t : opt.expectLightNeverOn)
    {
        auto it = lightViolation.find(t);
        Check(it == lightViolation.end(),
              std::string("light '") + LightName(t) + "' stayed OFF for the whole run" +
                  (it != lightViolation.end() ? " (observed mode " + std::to_string(it->second) + ")" : ""));
    }

    // Assertion 5: movement.
    if (opt.expectMove >= 0.0)
    {
        double dist = -1.0;
        if (haveState && SE_GetObjectState(opt.lightVehicleId, &state) == 0)
        {
            const double dx = state.x - x0;
            const double dy = state.y - y0;
            dist            = std::sqrt(dx * dx + dy * dy);
        }
        Check(dist >= opt.expectMove,
              "vehicle " + std::to_string(opt.lightVehicleId) + " moved >= " + std::to_string(opt.expectMove) + "m (got " +
                  std::to_string(dist) + "m)");
    }

    GT_Close();

    if (g_failures > 0)
    {
        std::cout << "[GT_Loader] RESULT: FAIL (" << g_failures << " failed expectation(s))" << std::endl;
        return 1;
    }
    std::cout << "[GT_Loader] RESULT: PASS" << std::endl;
    return 0;
}
