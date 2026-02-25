#pragma once

#include "ScenarioEngine.hpp"
#include "RoadManager.hpp"
#include "Entities.hpp"

namespace gt_esmini
{
    class TerrainTracker
    {
    public:
        // Enable/disable terrain tracking globally
        static void SetEnabled(bool enabled) { enabled_ = enabled; }
        static bool IsEnabled() { return enabled_; }

        // Update terrain-induced pitch/roll for all vehicles
        static void UpdateAllVehicleTerrain(scenarioengine::ScenarioEngine* se);

    private:
        // Update single vehicle's terrain attitude
        static void UpdateVehicleTerrain(scenarioengine::Object* obj);

        // Get road height at wheel position
        static double GetWheelHeight(double global_x, double global_y);

        // Global enable flag
        static bool enabled_;
    };
}  // namespace gt_esmini
