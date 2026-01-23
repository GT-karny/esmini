#include "TerrainTracker.hpp"

namespace gt_esmini
{

// Static member initialization
bool TerrainTracker::enabled_ = false;  // Default disabled (feature needs API updates)

void TerrainTracker::UpdateAllVehicleTerrain(scenarioengine::ScenarioEngine* se)
{
    // Feature temporarily disabled due to API incompatibility
    // The terrain tracking functionality needs to be updated to match
    // current esmini API for wheel_data and position setters
    (void)se;  // Suppress unused parameter warning
}

void TerrainTracker::UpdateVehicleTerrain(scenarioengine::Object* obj)
{
    // Feature temporarily disabled
    (void)obj;  // Suppress unused parameter warning
}

double TerrainTracker::GetWheelHeight(double global_x, double global_y)
{
    // Feature temporarily disabled
    (void)global_x;
    (void)global_y;
    return 0.0;
}

}  // namespace gt_esmini
