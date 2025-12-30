#include "GT_esminiLib.hpp"
#include <iostream>
#include <thread>
#include <chrono>

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "Usage: GT_Loader <xosc_file>" << std::endl;
        return 1;
    }
    
    std::string xoscFile = argv[1];
    std::cout << "Loading scenario: " << xoscFile << std::endl;
    
    // Initialize GT_esmini
    if (GT_Init(xoscFile.c_str(), 0) != 0)
    {
        std::cerr << "Failed to initialize GT_esmini" << std::endl;
        return 1;
    }
    
    // Enable AutoLight (optional, but good to test)
    GT_EnableAutoLight();
    
    // Run simulation for a few seconds (e.g., 5 seconds) to cover the events in the test scenarios
    // Run simulation for a few seconds (e.g., 15 seconds) to cover the events in the test scenarios
    const double stepTime = 0.05;
    const double duration = 15.0; // seconds
    const int steps = static_cast<int>(duration / stepTime);
    
    for (int i = 0; i < steps; ++i)
    {
        GT_Step(stepTime);
        
        // Check light states for vehicle 0 (ObjectId 0 usually 'Ego' or first vehicle)
        // Note: SE_GetObjectId(0) usually returns ID of first object.
        // Assuming ID=0 for simple/single vehicle scenario or explicit ID 0.
        // Let's check ID 0 for now.
        int vehicleId = 0; 
        
        // Log states every 0.5s or on change (simple polling for now)
        if (i % 10 == 0) // Every 0.5s (10 * 0.05)
        {
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
