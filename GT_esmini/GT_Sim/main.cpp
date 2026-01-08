/*
 * GT_Sim - Real-time execution wrapper for GT_esmini
 */

#include "GT_esminiLib.hpp"
#include "esminiLib.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <cstdio>
#include <thread>
#include <chrono>

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

int main(int argc, const char* argv[])
{
    if (argc < 2)
    {
        printf("Usage: GT_Sim --osc <osc filename> [options]\n");
        printf("Options:\n");
        printf("  --autolight          Enable AutoLight functionality\n");
        printf("  --autolight-egoless  Enable AutoLight but exclude Ego vehicle (first object)\n");
        printf("  --osi <ip>           Enable OSI output to specified IP\n");
        printf("  ... [See esmini documentation for other arguments]\n");
        return -1;
    }

    // 1. Initialize GT_esmini (Pass all args, sanitation happens inside)
    if (GT_InitWithArgs(argc, argv) != 0)
    {
        printf("Failed to initialize GT_esmini\n");
        return -1;
    }

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
    if (hzStr)
    {
        frequency = std::stod(hzStr);
        if (frequency <= 0.0) frequency = 100.0;
    }
    printf("GT_Sim: Running at %.1f Hz\n", frequency);

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

        // Real-time pacing
        next_target_time += std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(dt));
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

    printf("Total delayed frames: %lld\n", delayed_frames);
    GT_Close();
    return 0;
}
