#pragma once

#include <vector>

namespace scenarioengine
{
class Object;
}
namespace roadmanager
{
class Signal;
}

namespace gt_esmini
{

// One OpenDRIVE signal found ahead on the ego's route, with how far ahead it is.
struct ScannedSignal
{
    roadmanager::Signal* signal         = nullptr;
    double               distance_ahead = 0.0;  // [m] along the route from the ego
};

// Walk the ego's route forward (Duplicate + CopyRoute + MoveAlongS, the same
// pattern as ManeuverAwareSpeedPlanner / DetectJunctionTurn) up to `lookahead`
// metres and collect every OpenDRIVE signal whose s-coordinate the walk crosses,
// keeping only those that FACE the ego's travel direction (Signal orientation)
// and APPLY to the ego's current lane (validity records). Results are returned
// in increasing distance order. Shared by TrafficLightAware (3b) and
// StopYieldSignAware (3c) so the route walk is written once.
std::vector<ScannedSignal> ScanSignalsAhead(scenarioengine::Object* ego, double lookahead, double step = 2.0);

}  // namespace gt_esmini
