#include "GT_esminiLib.hpp"
#include "esminiLib.hpp"  // For OSI functions: SE_OpenOSISocket, SE_UpdateOSI, SE_CloseOSISocket
#include <iostream>
#include <thread>
#include <chrono>
#include <string>

int main(int argc, char** argv)
{
    std::string xoscFile;
    std::string osiUdp;
    
    // Parse arguments
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        
        if (arg == "--osi_udp" && i + 1 < argc)
        {
            osiUdp = argv[++i];
        }
        else if (arg == "--window" && i + 4 < argc)
        {
            // Skip window arguments (not used in headless mode)
            i += 4;
        }
        else if (arg.find("--") != 0)
        {
            xoscFile = arg;
        }
    }
    
    if (xoscFile.empty())
    {
        std::cerr << "Usage: GT_Loader <xosc_file> [--osi_udp <ip:port>] [--window <x> <y> <w> <h>]" << std::endl;
        return 1;
    }
    
    std::cout << "Loading scenario: " << xoscFile << std::endl;
    
    // Initialize GT_esmini
    if (GT_Init(xoscFile.c_str(), 0) != 0)
    {
        std::cerr << "Failed to initialize GT_esmini" << std::endl;
        return 1;
    }
    
    // Open OSI socket if requested
    bool osiEnabled = false;
    if (!osiUdp.empty())
    {
        if (SE_OpenOSISocket(osiUdp.c_str()) == 0)
        {
            osiEnabled = true;
            std::cout << "OSI UDP enabled: " << osiUdp << std::endl;
        }
        else
        {
            std::cerr << "Warning: Failed to open OSI socket: " << osiUdp << std::endl;
        }
    }
    
    // Enable AutoLight (optional, but good to test)
    GT_EnableAutoLight();
    
    // Run simulation for a few seconds (e.g., 15 seconds) to cover the events in the test scenarios
    const double stepTime = 0.05;
    const double duration = 15.0; // seconds
    const int steps = static_cast<int>(duration / stepTime);
    
    for (int i = 0; i < steps; ++i)
    {
        GT_Step(stepTime);
        
        // OSI is updated automatically within GT_Step when socket is open
        
        // Check light states for vehicle 0 (ObjectId 0 usually 'Ego' or first vehicle)
        // Log states every 0.5s or on change (simple polling for now)
        if (i % 10 == 0) // Every 0.5s (10 * 0.05)
        {
            int vehicleId = 0; 
            int fog     = GT_GetLightState(vehicleId, 3); // FOG_LIGHTS
            int brake   = GT_GetLightState(vehicleId, 6); // BRAKE_LIGHTS
            int reverse = GT_GetLightState(vehicleId, 10); // REVERSING_LIGHTS
            int leftInd = GT_GetLightState(vehicleId, 8); // INDICATOR_LEFT
            int rightInd = GT_GetLightState(vehicleId, 9); // INDICATOR_RIGHT
            
            std::cout << "Time " << (i * stepTime) << "s | ID " << vehicleId
                      << " | Fog: " << fog
                      << " | Brake: " << brake 
                      << " | Rev: " << reverse 
                      << " | L-Ind: " << leftInd 
                      << " | R-Ind: " << rightInd << std::endl;
        }

        // std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Optional: real-time pacing
    }
    
    std::cout << "Simulation completed." << std::endl;
    
    // Cleanup
    GT_Close();
    
    return 0;
}
