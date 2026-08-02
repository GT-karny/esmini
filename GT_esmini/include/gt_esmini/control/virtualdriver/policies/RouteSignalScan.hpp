#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "gt_esmini/control/virtualdriver/policies/JunctionStopGuard.hpp"

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
    bool                 is_stop_line   = false;  // StopLineSignalCatalog classification of this signal
};

// Walk the ego's route forward (Duplicate + CopyRoute + MoveAlongS, the same
// pattern as ManeuverAwareSpeedPlanner / DetectJunctionTurn) up to `lookahead`
// metres and collect every OpenDRIVE signal whose s-coordinate the walk crosses,
// keeping only those that FACE the ego's travel direction (Signal orientation)
// and APPLY to the ego's current lane (validity records). Results are returned
// in increasing distance order. Shared by TrafficLightAware (3b) and
// StopYieldSignAware (3c) so the route walk is written once.
//
// `junction_spans`, when non-null, is additionally filled with every junction
// connecting-road stretch the same walk crosses, in increasing entry order —
// the input JunctionStopGuard needs to keep a stop target out of an
// intersection. It rides along on this walk instead of a second one; passing
// nullptr (StopYieldSignAware) costs nothing. Boundaries are the exact road
// transition distances, not step-quantised. Two limits, both inherited from the
// signal walk: a connecting road shorter than `step` that MoveAlongS jumps over
// entirely is not seen, and a span still open at the horizon is closed with
// exit_ahead = +infinity (unknown exit reads as "cannot be cleared", the safe
// side for the guard).
std::vector<ScannedSignal> ScanSignalsAhead(scenarioengine::Object*         ego,
                                            double                          lookahead,
                                            double                          step           = 2.0,
                                            std::vector<RouteJunctionSpan>* junction_spans = nullptr);

// Pure: signals is distance-ascending (ScanSignalsAhead's output). Pairs
// signals[anchor_index] (a governing head or STOP sign) with the is_stop_line
// entry nearest to it that is at or before its distance_ahead and within
// `window` of it; nullopt if none qualifies (out-of-range anchor_index included).
std::optional<size_t> FindPairedStopLine(const std::vector<ScannedSignal>& signals,
                                         std::size_t                       anchor_index,
                                         double                            window);

}  // namespace gt_esmini
