#include "StubReporter.hpp"
#include <iostream>

void StubReporter::report(const std::vector<SE_ScenarioObjectState>& states) {
    if (states.empty()) return;

    std::cout << "[Reporter] Frame Status: " << states.size() << " objects detected." << std::endl;
    for (const auto& s : states) {
        std::cout << "  - ID: " << s.id 
                  << " Pos: (" << s.x << ", " << s.y << ", " << s.z << ")" 
                  << " Speed: " << s.speed << std::endl;
    }
}
