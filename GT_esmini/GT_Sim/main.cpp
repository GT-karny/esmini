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
        printf("Usage: GT_Sim <osc filename> [options]\n");
        printf("Options:\n");
        printf("  --autolight          Enable AutoLight functionality\n");
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

    // Set a flag to signal simulation loop to quit
    bool quit = false;

    // 4. Main Loop
    while (!quit)
    {
        // Check standard quit flag (e.g. from window close or end of scenario)
        if (SE_GetQuitFlag() == 1)
        {
            quit = true;
            break;
        }

        // Stepping
        // GT_Step(dt) wraps SE_StepDT(dt) and AutoLightManager::Update(dt).
        // It returns void.
        GT_Step(0.01); 

        // Check if we need to sleep to maintain real-time roughly
        // (SE_StepDT doesn't sleep)
        std::this_thread::sleep_for(std::chrono::milliseconds(10)); 
    }

    GT_Close();
    return 0;
}
