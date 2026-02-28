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

// Helper to check for existence of command line option
bool HasOption(int argc, const char* argv[], const std::string& option)
{
    for(int i=1; i<argc; i++)
    {
        if(argv[i] && option == argv[i]) return true;
    }
    return false;
}

// Helper to get value of command line option
const char* GetOptionValue(int argc, const char* argv[], const std::string& option)
{
    for(int i=1; i<argc; i++)
    {
        if(argv[i] && option == argv[i] && i+1 < argc)
        {
            return argv[i+1];
        }
    }
    return nullptr;
}

static bool ContainsArg(const std::vector<std::string>& args, const std::string& option)
{
    for (const auto& arg : args)
    {
        if (arg == option) return true;
    }
    return false;
}

struct VideoOptions
{
    bool enabled = false;
    bool headless = true;
    int width = 1280;
    int height = 720;
    int frames = -1;
    std::string prefix = "screen_shot_";
};

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

            if (line.rfind("SPEED:", 0) == 0)
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
        printf("Usage: GT_Sim --osc <osc filename> [options]\n");
        printf("GT_Sim-specific options:\n");
        printf("  --autolight          Enable AutoLight functionality\n");
        printf("  --autolight-egoless  Enable AutoLight but exclude Ego vehicle (first object)\n");
        printf("  --osi <ip>           Enable OSI output to specified IP\n");
        printf("  --hz <freq>          Simulation frequency used for GT_Step dt (default: 100)\n");
        printf("  --no_realtime        Disable real-time pacing (run as fast as possible)\n");
        printf("  --video_capture      Enable direct frame capture in GT_Sim\n");
        printf("  --video_window <w h> Capture window size (default: 1280 720)\n");
        printf("  --video_headless     Run capture in headless mode (default: true)\n");
        printf("  --video_frames <n>   Number of frames to capture (-1 for continuous)\n");
        printf("  --video_prefix <p>   Capture file prefix (default: screen_shot_)\n");
        printf("  --control_pipe <n>   Named pipe for runtime speed control (Windows)\n");
        printf("\nSee esmini --help for engine options.\n");
        return 0;
    }

    if (argc < 2)
    {
        printf("Usage: GT_Sim --osc <osc filename> [options]\n");
        printf("Options:\n");
        printf("  --autolight          Enable AutoLight functionality\n");
        printf("  --autolight-egoless  Enable AutoLight but exclude Ego vehicle (first object)\n");
        printf("  --osi <ip>           Enable OSI output to specified IP\n");
        printf("  --no_realtime        Disable real-time pacing (run as fast as possible)\n");
        printf("  --video_capture      Enable direct frame capture in GT_Sim\n");
        printf("  --video_window <w h> Capture window size (default: 1280 720)\n");
        printf("  --video_headless     Run capture in headless mode (default: true)\n");
        printf("  --video_frames <n>   Number of frames to capture (-1 for continuous)\n");
        printf("  --video_prefix <p>   Capture file prefix (default: screen_shot_)\n");
        printf("  --control_pipe <n>   Named pipe for runtime speed control (Windows)\n");
        printf("  ... [See esmini documentation for other arguments]\n");
        return -1;
    }

    // Parse GT_Sim-only options and build args forwarded to GT_InitWithArgs.
    VideoOptions video;
    std::vector<std::string> forwardArgs;
    forwardArgs.emplace_back(argv[0] ? argv[0] : "GT_Sim");

    for (int i = 1; i < argc; i++)
    {
        if (argv[i] == nullptr)
        {
            continue;
        }

        const std::string arg = argv[i];
        if (arg == "--video_capture")
        {
            video.enabled = true;
        }
        else if (arg == "--video_headless")
        {
            video.headless = true;
        }
        else if (arg == "--video_window" && i + 2 < argc)
        {
            video.enabled = true;
            video.width = std::max(1, std::atoi(argv[++i]));
            video.height = std::max(1, std::atoi(argv[++i]));
        }
        else if (arg == "--video_frames" && i + 1 < argc)
        {
            video.enabled = true;
            video.frames = std::atoi(argv[++i]);
            if (video.frames == 0)
            {
                video.frames = -1;
            }
        }
        else if (arg == "--video_prefix" && i + 1 < argc)
        {
            video.enabled = true;
            video.prefix = argv[++i];
        }
        else if (arg == "--control_pipe" && i + 1 < argc)
        {
            // Consumed by GT_Sim, not forwarded to esmini
            i++; // skip value, handled below
        }
        else
        {
            forwardArgs.emplace_back(arg);
        }
    }

    if (video.enabled)
    {
        const bool hasHeadless = ContainsArg(forwardArgs, "--headless");
        const bool hasWindow = ContainsArg(forwardArgs, "--window");

        // Keep --headless before --window: Config::PostProcessArgs removes window
        // options that appear before the last --headless argument.
        if (video.headless && !hasHeadless)
        {
            forwardArgs.emplace_back("--headless");
        }
        if (!hasWindow)
        {
            forwardArgs.emplace_back("--window");
            forwardArgs.emplace_back("0");
            forwardArgs.emplace_back("0");
            forwardArgs.emplace_back(std::to_string(video.width));
            forwardArgs.emplace_back(std::to_string(video.height));
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

    // 1.5 Start control pipe if requested
#ifdef _WIN32
    ControlPipe controlPipe;
    const char* pipeName = GetOptionValue(argc, argv, "--control_pipe");
    if (pipeName)
    {
        controlPipe.Start(pipeName);
    }
#endif

    // 2. Enable AutoLight if requested
    if (HasOption(argc, argv, "--autolight"))
    {
        printf("GT_Sim: Enabling AutoLight\n");
        GT_EnableAutoLight();
    }

    // 3. Open OSI Socket if requested
    const char* osiIp = GetOptionValue(argc, argv, "--osi");
    if (osiIp)
    {
        printf("GT_Sim: Enabling OSI output to %s\n", osiIp);
        SE_OpenOSISocket(osiIp);
    }

    // 4. Frequency Control (default 100Hz)
    double frequency = 100.0;
    const char* hzStr = GetOptionValue(argc, argv, "--hz");
    const bool noRealtime = HasOption(argc, argv, "--no_realtime");
    if (hzStr)
    {
        frequency = std::stod(hzStr);
        if (frequency <= 0.0) frequency = 100.0;
    }
    printf("GT_Sim: Running at %.1f Hz (realtime pacing: %s)\n", frequency, noRealtime ? "OFF" : "ON");

    bool captureRequested = video.enabled;
    bool captureStarted = false;
    if (video.enabled)
    {
        std::cout << "GT_Sim: Video capture requested (" << video.width << "x" << video.height << ", frames=" << video.frames << ")" << std::endl;
    }

    double dt = 1.0 / frequency;
    using Clock = std::chrono::steady_clock;
    auto next_target_time = Clock::now();

    // Stats
    long long delayed_frames = 0;

    // Set a flag to signal simulation loop to quit
    bool quit = false;

    // 5. Main Loop
    while (!quit)
    {
        // Check standard quit flag (e.g. from window close or end of scenario)
        if (SE_GetQuitFlag() == 1)
        {
            quit = true;
            break;
        }

        // Stepping
        GT_Step(dt); 

        if (captureRequested && !captureStarted)
        {
            const int captureRet = SE_SaveImagesToFile(video.frames);
            if (captureRet != 0)
            {
                std::cerr << "GT_Sim Warning: SE_SaveImagesToFile(" << video.frames << ") returned " << captureRet << std::endl;
            }
            else
            {
                captureStarted = true;
                std::cout << "GT_Sim: Video capture started." << std::endl;
            }
            captureRequested = false;
        }

        if (!noRealtime)
        {
            // Real-time pacing with speed factor
#ifdef _WIN32
            double current_speed = pipeName ? controlPipe.speed_factor.load() : 1.0;
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
                    // Only log critical slips
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

    if (video.enabled)
    {
        SE_SaveImagesToFile(0);
        RenameCapturedFramesIfNeeded(video.prefix);
        std::cout << "GT_Sim: Captured frames = " << CountFramesWithPrefix(video.prefix) << std::endl;
    }

    printf("Total delayed frames: %lld\n", delayed_frames);

#ifdef _WIN32
    if (pipeName)
    {
        controlPipe.Stop();
    }
#endif

    GT_Close();
    return 0;
}
