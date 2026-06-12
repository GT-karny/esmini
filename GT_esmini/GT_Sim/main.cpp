/*
 * GT_Sim - Real-time execution wrapper for GT_esmini
 */

#include <gt_esmini/core/GT_esminiLib.hpp>
#include "esminiLib.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <cstdio>
#include <thread>
#include <chrono>
#include <algorithm>
#include <filesystem>
#include <atomic>
#include <mutex>
#ifdef _WIN32
#include <windows.h>
#endif

// Helper to check for existence of command line option (used for pre-parse checks)
static bool HasOption(int argc, const char* argv[], const std::string& option)
{
    for (int i = 1; i < argc; i++)
    {
        if (argv[i] && option == argv[i]) return true;
    }
    return false;
}

static bool ContainsArg(const std::vector<std::string>& args, const std::string& option)
{
    for (const auto& arg : args)
    {
        if (arg == option) return true;
    }
    return false;
}

struct ParamOverride
{
    std::string name;
    std::string value;
};

struct GtSimOptions
{
    // AutoLight
    bool autolight = false;
    bool autolight_egoless = false;

    // Vehicle Physics (observed pitch/roll for non-GT-controller vehicles)
    bool vehicle_physics = false;

    // Heading Correction (nose-leading behavior for non-GT-controller vehicles)
    bool heading_correction = false;

    // OSI
    std::string osi_ip;  // empty = disabled

    // Timing
    double frequency = 100.0;
    bool no_realtime = false;

    // Video capture
    bool video_enabled = false;
    bool video_headless = true;
    int video_width = 1280;
    int video_height = 720;
    int video_frames = -1;
    std::string video_prefix = "screen_shot_";

    // Control pipe (Windows)
    std::string control_pipe_name;  // empty = disabled

    // HVDEstimator initial drive mode (empty = use config default)
    std::string drive_mode;

    // Parameter overrides
    std::vector<ParamOverride> param_overrides;

    // Scenario Variables reporter
    int sv_port = 0;  // 0 = use default (48200)
};

static void PrintUsage()
{
    printf("Usage: GT_Sim --osc <osc filename> [options]\n");
    printf("GT_Sim-specific options:\n");
    printf("  --autolight          Enable AutoLight functionality\n");
    printf("  --autolight-egoless  Enable AutoLight but exclude Ego vehicle (first object)\n");
    printf("  --vehicle-physics    Enable observed vehicle physics (pitch/roll) for traffic vehicles\n");
    printf("  --heading-correction Enable nose-leading heading correction for traffic vehicles\n");
    printf("  --osi <ip>           Enable OSI output to specified IP\n");
    printf("  --hz <freq>          Simulation frequency used for GT_Step dt (default: 100)\n");
    printf("  --no_realtime        Disable real-time pacing (run as fast as possible)\n");
    printf("  --video_capture      Enable direct frame capture in GT_Sim\n");
    printf("  --video_window <w h> Capture window size (default: 1280 720)\n");
    printf("  --video_headless     Run capture in headless mode (default: true)\n");
    printf("  --video_frames <n>   Number of frames to capture (-1 for continuous)\n");
    printf("  --video_prefix <p>   Capture file prefix (default: screen_shot_)\n");
    printf("  --control_pipe <n>   Named pipe for runtime control (SPEED:<f>, DRIVE_MODE:<m>, QUIT) [Windows]\n");
    printf("  --drive_mode <name>  HVDEstimator drive mode at startup (e.g. comfort, sport)\n");
    printf("  --param <name,val>   Override scenario parameter (repeatable)\n");
    printf("\nSee esmini --help for engine options.\n");
}

static int CountFramesWithPrefix(const std::string& prefix)
{
    int count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::current_path()))
    {
        if (!entry.is_regular_file()) continue;
        const auto fileName = entry.path().filename().string();
        if (fileName.rfind(prefix, 0) == 0 && entry.path().extension() == ".tga")
        {
            count++;
        }
    }
    return count;
}

static void RenameCapturedFramesIfNeeded(const std::string& prefix)
{
    if (prefix.empty() || prefix == "screen_shot_")
    {
        return;
    }

    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> renames;
    for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::current_path()))
    {
        if (!entry.is_regular_file()) continue;

        const auto fileName = entry.path().filename().string();
        const std::string defaultPrefix = "screen_shot_";
        if (fileName.rfind(defaultPrefix, 0) == 0 && entry.path().extension() == ".tga")
        {
            const auto suffix = fileName.substr(defaultPrefix.size());
            renames.emplace_back(entry.path(), entry.path().parent_path() / (prefix + suffix));
        }
    }

    for (const auto& op : renames)
    {
        std::error_code ec;
        std::filesystem::rename(op.first, op.second, ec);
        if (ec)
        {
            std::cerr << "GT_Sim Warning: Failed to rename frame " << op.first << " -> " << op.second
                      << " (" << ec.message() << ")" << std::endl;
        }
    }
}

#ifdef _WIN32
struct ControlPipe
{
    std::string pipe_name;
    HANDLE pipe_handle = INVALID_HANDLE_VALUE;
    std::thread reader_thread;
    std::atomic<bool> running{false};
    std::atomic<bool> quit_requested{false};
    std::atomic<double> speed_factor{1.0};

    bool Start(const std::string& name)
    {
        pipe_name = std::string("\\\\.\\pipe\\") + name;
        pipe_handle = CreateNamedPipeA(
            pipe_name.c_str(),
            PIPE_ACCESS_INBOUND,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1,    // max instances
            256,  // out buffer
            256,  // in buffer
            100,  // timeout ms
            NULL);

        if (pipe_handle == INVALID_HANDLE_VALUE)
        {
            std::cerr << "GT_Sim: Failed to create control pipe: " << pipe_name << std::endl;
            return false;
        }

        running.store(true);
        reader_thread = std::thread([this]() { ReaderLoop(); });
        printf("GT_Sim: Control pipe created: %s\n", pipe_name.c_str());
        return true;
    }

    void ReaderLoop()
    {
        while (running.load())
        {
            // Wait for client connection (blocking, but pipe will be closed on Stop)
            BOOL connected = ConnectNamedPipe(pipe_handle, NULL) ?
                TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);

            if (!connected || !running.load()) break;

            // Read commands from connected client
            char buffer[256];
            DWORD bytesRead = 0;
            while (running.load())
            {
                BOOL success = ReadFile(pipe_handle, buffer, sizeof(buffer) - 1, &bytesRead, NULL);
                if (!success || bytesRead == 0) break;

                buffer[bytesRead] = '\0';
                ProcessCommand(std::string(buffer, bytesRead));
            }

            DisconnectNamedPipe(pipe_handle);
        }
    }

    void ProcessCommand(const std::string& cmd)
    {
        // Simple format: "SPEED:2.0\n" or "SPEED:0.5\n"
        // Parse line by line
        size_t pos = 0;
        while (pos < cmd.size())
        {
            size_t end = cmd.find('\n', pos);
            if (end == std::string::npos) end = cmd.size();
            std::string line = cmd.substr(pos, end - pos);
            pos = end + 1;

            // Trim carriage return
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;

            if (line == "QUIT")
            {
                printf("GT_Sim: QUIT received via control pipe\n");
                quit_requested.store(true);
            }
            else if (line.rfind("SPEED:", 0) == 0)
            {
                try
                {
                    double factor = std::stod(line.substr(6));
                    if (factor > 0.0 && factor <= 100.0)
                    {
                        speed_factor.store(factor);
                        printf("GT_Sim: Speed factor set to %.2f\n", factor);
                    }
                }
                catch (...) {}
            }
            else if (line.rfind("DRIVE_MODE:", 0) == 0)
            {
                std::string mode = line.substr(11);
                int rc = GT_SetDriveMode(mode.c_str());
                printf("GT_Sim: DRIVE_MODE='%s' rc=%d\n", mode.c_str(), rc);
            }
        }
    }

    void Stop()
    {
        running.store(false);
        if (pipe_handle != INVALID_HANDLE_VALUE)
        {
            // This will unblock ConnectNamedPipe/ReadFile
            CancelIoEx(pipe_handle, NULL);
            CloseHandle(pipe_handle);
            pipe_handle = INVALID_HANDLE_VALUE;
        }
        if (reader_thread.joinable())
        {
            reader_thread.join();
        }
    }
};
#endif

int main(int argc, const char* argv[])
{
    std::cout << "GT_Sim build: PythonDriverController=ENABLED" << std::endl;

    const bool helpRequested = HasOption(argc, argv, "--help") || HasOption(argc, argv, "-h");
    const bool hasOsc = HasOption(argc, argv, "--osc");

    if (helpRequested && !hasOsc)
    {
        PrintUsage();
        return 0;
    }

    if (argc < 2)
    {
        PrintUsage();
        return -1;
    }

    // Parse GT_Sim-only options and build args forwarded to GT_InitWithArgs.
    GtSimOptions opts;
    std::vector<std::string> forwardArgs;
    forwardArgs.emplace_back(argv[0] ? argv[0] : "GT_Sim");

    for (int i = 1; i < argc; i++)
    {
        if (argv[i] == nullptr)
        {
            continue;
        }

        const std::string arg = argv[i];

        // --- GT_Sim-specific options (consumed, NOT forwarded to esmini) ---

        if (arg == "--autolight")
        {
            opts.autolight = true;
        }
        else if (arg == "--autolight-egoless")
        {
            opts.autolight = true;
            opts.autolight_egoless = true;
        }
        else if (arg == "--vehicle-physics")
        {
            opts.vehicle_physics = true;
        }
        else if (arg == "--heading-correction")
        {
            opts.heading_correction = true;
        }
        else if (arg == "--osi" && i + 1 < argc)
        {
            opts.osi_ip = argv[++i];
        }
        else if (arg == "--hz" && i + 1 < argc)
        {
            try
            {
                opts.frequency = std::stod(argv[++i]);
                if (opts.frequency <= 0.0) opts.frequency = 100.0;
            }
            catch (...)
            {
                std::cerr << "GT_Sim Warning: Invalid --hz value, using default 100 Hz" << std::endl;
                opts.frequency = 100.0;
            }
        }
        else if (arg == "--no_realtime")
        {
            opts.no_realtime = true;
        }
        else if (arg == "--video_capture")
        {
            opts.video_enabled = true;
        }
        else if (arg == "--video_headless")
        {
            opts.video_headless = true;
        }
        else if (arg == "--video_window" && i + 2 < argc)
        {
            opts.video_enabled = true;
            opts.video_width = std::max(1, std::atoi(argv[++i]));
            opts.video_height = std::max(1, std::atoi(argv[++i]));
        }
        else if (arg == "--video_frames" && i + 1 < argc)
        {
            opts.video_enabled = true;
            opts.video_frames = std::atoi(argv[++i]);
            if (opts.video_frames == 0)
            {
                opts.video_frames = -1;
            }
        }
        else if (arg == "--video_prefix" && i + 1 < argc)
        {
            opts.video_enabled = true;
            opts.video_prefix = argv[++i];
        }
        else if (arg == "--control_pipe" && i + 1 < argc)
        {
            opts.control_pipe_name = argv[++i];
        }
        else if (arg == "--drive_mode" && i + 1 < argc)
        {
            opts.drive_mode = argv[++i];
        }
        else if (arg == "--sv-port" && i + 1 < argc)
        {
            // Forward to GT_InitWithArgs (handled inside GT_esminiLib)
            forwardArgs.emplace_back(arg);
            forwardArgs.emplace_back(argv[++i]);
        }
        else if (arg == "--param" && i + 1 < argc)
        {
            std::string pv = argv[++i];
            auto commaPos = pv.find(',');
            if (commaPos != std::string::npos && commaPos > 0)
            {
                opts.param_overrides.push_back({pv.substr(0, commaPos), pv.substr(commaPos + 1)});
            }
            else
            {
                std::cerr << "GT_Sim Warning: Invalid --param format (expected name,value): " << pv << std::endl;
            }
        }
        // --- Everything else is forwarded to esmini ---
        else
        {
            forwardArgs.emplace_back(arg);
        }
    }

    if (opts.video_enabled)
    {
        const bool hasHeadless = ContainsArg(forwardArgs, "--headless");
        const bool hasWindow = ContainsArg(forwardArgs, "--window");

        // Keep --headless before --window: Config::PostProcessArgs removes window
        // options that appear before the last --headless argument.
        if (opts.video_headless && !hasHeadless)
        {
            forwardArgs.emplace_back("--headless");
        }
        if (!hasWindow)
        {
            forwardArgs.emplace_back("--window");
            forwardArgs.emplace_back("0");
            forwardArgs.emplace_back("0");
            forwardArgs.emplace_back(std::to_string(opts.video_width));
            forwardArgs.emplace_back(std::to_string(opts.video_height));
        }
    }

    std::vector<const char*> initArgv;
    initArgv.reserve(forwardArgs.size());
    for (const auto& arg : forwardArgs)
    {
        initArgv.push_back(arg.c_str());
    }

    // 1. Initialize GT_esmini (GT_Sim-only options are removed from forwarded args)
    if (GT_InitWithArgs(static_cast<int>(initArgv.size()), initArgv.data()) != 0)
    {
        printf("Failed to initialize GT_esmini\n");
        return -1;
    }

    // 1.0 Apply initial HVDEstimator drive mode if requested
    if (!opts.drive_mode.empty())
    {
        int rc = GT_SetDriveMode(opts.drive_mode.c_str());
        if (rc != 0)
        {
            printf("GT_Sim: warning - failed to set initial drive mode '%s' (rc=%d)\n",
                   opts.drive_mode.c_str(), rc);
        }
    }

    // 1.1 Apply parameter overrides
    if (!opts.param_overrides.empty())
    {
        // Build name->type map from scenario's ParameterDeclarations
        // Types: 1=int, 2=double, 3=string, 4=bool
        int numParams = SE_GetNumberOfParameters();
        std::vector<std::pair<std::string, int>> paramTypes;
        for (int pi = 0; pi < numParams; pi++)
        {
            int ptype = 0;
            const char* pname = SE_GetParameterName(pi, &ptype);
            if (pname) paramTypes.emplace_back(pname, ptype);
        }

        for (const auto& ov : opts.param_overrides)
        {
            // Find the parameter type
            int ptype = -1;
            for (const auto& pt : paramTypes)
            {
                if (pt.first == ov.name) { ptype = pt.second; break; }
            }

            if (ptype < 0)
            {
                std::cerr << "GT_Sim Warning: Unknown parameter '" << ov.name << "', skipping override" << std::endl;
                continue;
            }

            int ret = -1;
            try
            {
                if (ptype == 1) // int
                {
                    ret = SE_SetParameterInt(ov.name.c_str(), std::stoi(ov.value));
                }
                else if (ptype == 2) // double
                {
                    ret = SE_SetParameterDouble(ov.name.c_str(), std::stod(ov.value));
                }
                else if (ptype == 3) // string
                {
                    ret = SE_SetParameterString(ov.name.c_str(), ov.value.c_str());
                }
                else if (ptype == 4) // bool
                {
                    bool bval = (ov.value == "true" || ov.value == "1");
                    ret = SE_SetParameterBool(ov.name.c_str(), bval);
                }
            }
            catch (const std::exception& e)
            {
                std::cerr << "GT_Sim Warning: Failed to convert parameter " << ov.name
                          << " = '" << ov.value << "': " << e.what() << std::endl;
                continue;
            }

            if (ret == 0)
            {
                printf("GT_Sim: Parameter override: %s = %s\n", ov.name.c_str(), ov.value.c_str());
            }
            else
            {
                std::cerr << "GT_Sim Warning: Failed to set parameter " << ov.name << " = " << ov.value << std::endl;
            }
        }
    }

    // 1.5 Start control pipe if requested
#ifdef _WIN32
    ControlPipe controlPipe;
    if (!opts.control_pipe_name.empty())
    {
        controlPipe.Start(opts.control_pipe_name);
    }
#endif

    // 2. Enable AutoLight if requested
    if (opts.autolight)
    {
        printf("GT_Sim: Enabling AutoLight\n");
        GT_EnableAutoLight();
    }

    // 2b. Enable Vehicle Physics if requested
    if (opts.vehicle_physics)
    {
        printf("GT_Sim: Enabling Vehicle Physics\n");
        GT_EnableVehiclePhysics();
    }

    // 2c. Enable Heading Correction if requested
    if (opts.heading_correction)
    {
        printf("GT_Sim: Enabling Heading Correction\n");
        GT_EnableHeadingCorrection();
    }

    // 3. Open OSI Socket if requested
    if (!opts.osi_ip.empty())
    {
        printf("GT_Sim: Enabling OSI output to %s\n", opts.osi_ip.c_str());
        GT_OpenOSISocket(opts.osi_ip.c_str());
    }

    // 3b. Override SV reporter port if specified (handled inside GT_InitWithArgs via --sv-port)

    // 4. Frequency Control
    printf("GT_Sim: Running at %.1f Hz (realtime pacing: %s)\n", opts.frequency, opts.no_realtime ? "OFF" : "ON");

    bool captureRequested = opts.video_enabled;
    bool captureStarted = false;
    if (opts.video_enabled)
    {
        std::cout << "GT_Sim: Video capture requested (" << opts.video_width << "x" << opts.video_height
                  << ", frames=" << opts.video_frames << ")" << std::endl;
    }

    double dt = 1.0 / opts.frequency;
    using Clock = std::chrono::steady_clock;
    auto next_target_time = Clock::now();

    // Stats
    long long delayed_frames = 0;

    // 5. Main Loop
    while (SE_GetQuitFlag() != 1
#ifdef _WIN32
           && (!opts.control_pipe_name.empty() ? !controlPipe.quit_requested.load() : true)
#endif
    )
    {
        GT_Step(dt);

        if (captureRequested && !captureStarted)
        {
            const int captureRet = SE_SaveImagesToFile(opts.video_frames);
            if (captureRet != 0)
            {
                std::cerr << "GT_Sim Warning: SE_SaveImagesToFile(" << opts.video_frames << ") returned " << captureRet << std::endl;
            }
            else
            {
                captureStarted = true;
                std::cout << "GT_Sim: Video capture started." << std::endl;
            }
            captureRequested = false;
        }

        if (!opts.no_realtime)
        {
            // Real-time pacing with speed factor
#ifdef _WIN32
            double current_speed = !opts.control_pipe_name.empty() ? controlPipe.speed_factor.load() : 1.0;
#else
            double current_speed = 1.0;
#endif
            double wall_dt = dt / current_speed;
            next_target_time += std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(wall_dt));
            auto now = Clock::now();

            if (now > next_target_time)
            {
                // We are late
                auto delay = std::chrono::duration_cast<std::chrono::milliseconds>(now - next_target_time).count();

                // Count delayed frames if delay is significant (>2ms)
                if (delay > 2)
                {
                    delayed_frames++;
                }

                // If the delay is huge, we might want to reset.
                if (delay > 1000)
                {
                    printf("GT_Sim Warning: Huge delay (>1s), resyncing clock.\n");
                    next_target_time = now;
                }
            }
            else
            {
                // Sleep until next target
                std::this_thread::sleep_until(next_target_time);
            }
        }
    }

    if (opts.video_enabled)
    {
        SE_SaveImagesToFile(0);
        RenameCapturedFramesIfNeeded(opts.video_prefix);
        std::cout << "GT_Sim: Captured frames = " << CountFramesWithPrefix(opts.video_prefix) << std::endl;
    }

    printf("Total delayed frames: %lld\n", delayed_frames);

#ifdef _WIN32
    if (!opts.control_pipe_name.empty())
    {
        controlPipe.Stop();
    }
#endif

    GT_Close();
    return 0;
}
