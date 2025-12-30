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
    const double stepTime = 0.05;
    const double duration = 5.0; // seconds
    const int steps = static_cast<int>(duration / stepTime);
    
    for (int i = 0; i < steps; ++i)
    {
        GT_Step(stepTime);
        // std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Optional: real-time pacing
    }
    
    std::cout << "Simulation completed." << std::endl;
    
    // Cleanup
    GT_Close();
    
    return 0;
}
