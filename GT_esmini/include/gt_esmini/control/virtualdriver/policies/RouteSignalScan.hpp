#pragma once

#include <cstddef>
#include <cstdint>
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
    // The junction this signal governs (SignalJunctionResolver::ResolveSignalJunction),
    // resolved with the same travel direction used for the orientation filter below.
    // nullopt is the normal case for a signal that governs no OpenDRIVE junction
    // (unmodelled intersection, or none of the resolver's three paths apply) -- not
    // a sentinel, an absent value.
    std::optional<std::uint32_t> junction_id;
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
// Excludes signals[anchor_index] itself from the candidates (see
// FindPairedStopLineByDistance below for why that function cannot just delegate
// to, or be delegated to by, this one).
std::optional<size_t> FindPairedStopLine(const std::vector<ScannedSignal>& signals,
                                         std::size_t                       anchor_index,
                                         double                            window);

// Pure: same pairing rule as FindPairedStopLine above, but the anchor is a bare
// route distance rather than one of `signals`' own entries -- e.g. a junction
// entry (RouteJunctionSpan::entry_ahead), which is not itself a ScannedSignal.
// Pairs the is_stop_line entry nearest to `anchor_dist` that is at or before it
// and within `window`; nullopt if none qualifies. Every entry of `signals` is
// eligible: unlike FindPairedStopLine there is no "self" to exclude, because
// anchor_dist need not be any entry's own distance_ahead.
//
// A DIFFERENT NAME on purpose, not a std::size_t/double overload of
// FindPairedStopLine: every existing caller of the anchor_index form (including
// the 10 cases in test_RouteSignalScan.cpp) passes a bare int literal for
// anchor_index, and int->std::size_t and int->double are both user-invisible
// "Conversion rank" standard conversions with no tie-break between them --
// verified with a throwaway MSVC compile that such a call is rejected as
// ambiguous (C2668) once both signatures exist under one name.
std::optional<size_t> FindPairedStopLineByDistance(const std::vector<ScannedSignal>& signals,
                                                    double                             anchor_dist,
                                                    double                             window);

// Which distance anchors a paired-stop-line search for a governing head, and
// the diagnostic token that names it (gt.traffic_light.stop_line_anchor).
struct StopLineAnchor
{
    double      distance_ahead;
    const char* token;  // "junction_entry" or "head" -- which input below won
};

// Pure: min(junction_entry_ahead, head_dist_ahead) -- keeps the paired
// stop-line at or before the head too (design/stop_line_stop_target.md §5).
StopLineAnchor ResolveStopLineAnchor(double junction_entry_ahead, double head_dist_ahead);

}  // namespace gt_esmini
