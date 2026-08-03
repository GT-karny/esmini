#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace roadmanager
{
class OpenDrive;
class Signal;
}  // namespace roadmanager

namespace gt_esmini
{

// Which of the three resolution paths (see ResolveSignalJunction) produced a result.
// Exposed so callers/tests can pin not just THAT a junction was found but HOW.
enum class SignalJunctionSource
{
    CONTROLLER_CHAIN,  // (a) Signal -> Controller -> Junction, from authored <control>/<controller>
    ROAD_LINK,         // (c) the road ahead of the signal links directly to a junction
    CONNECTING_ROAD    // (b) the signal's own road IS a junction connecting road
};

// Resolve the OpenDRIVE junction that `signal` governs traffic for. Tries, in order:
// (a) the authored controller chain, (c) the road-link topology ahead of the signal,
// (b) the signal's own road's junction membership -- returns nullopt if none apply.
//
// (a) goes first because it alone does not depend on where the signal HEAD is
// physically mounted: it is the author's explicit signal->junction assignment, so a
// far-side/mast-arm head (mounted across the intersection it controls) still resolves
// to the junction it actually governs, where a geometry-only path would not.
//
// `travel_ds_dir` only matters for path (c), and only for a signal with no direction
// of its own (roadmanager Signal::Orientation::NONE): its sign follows
// RouteSignalScan.cpp's SignalFacesTravel/ds_dir convention (>0 governs +s travel ->
// look at the successor end, <0 governs -s travel -> predecessor end). A directional
// signal (POSITIVE/NEGATIVE) ignores it entirely -- its own orientation already picks
// the one road end that can be its junction.
//
// `source`, when non-null and resolution succeeds, is set to which path resolved it;
// left untouched when the result is nullopt.
//
// Builds and reuses a process-wide cache keyed on `odr` (see .cpp for why raw pointer
// identity alone is not a safe cache key). Not thread-safe, matching every other
// policies/ cache in this codebase (e.g. StopLineSignalCatalog).
std::optional<std::uint32_t> ResolveSignalJunction(roadmanager::OpenDrive*    odr,
                                                    const roadmanager::Signal* signal,
                                                    double                     travel_ds_dir,
                                                    SignalJunctionSource*      source = nullptr);

// ─────────────────────── pure decision logic (unit-testable) ───────────────────────

// One junction's controller references -- mirrors Junction::GetJunctionControllerByIdx()
// walked over THAT JUNCTION's own controller list. NOT OpenDrive::GetControllerByIdx():
// Junction::GetNumberOfControllers() (controllers THIS junction references) and
// OpenDrive::GetNumberOfControllers() (all top-level controllers in the file) share a
// name but count different things -- see the .cpp collector functions for how each is
// used.
struct JunctionControllerRefs
{
    std::uint32_t              junction_id = 0;
    std::vector<std::uint32_t> controller_ids;
};

// One controller's controlled-signal ids -- mirrors Controller::GetControl()'s
// Control::signalId_ list.
struct ControllerSignals
{
    std::uint32_t    controller_id = 0;
    std::vector<int> signal_ids;
};

// Pure: path (a)'s ambiguity-resolution core, decoupled from roadmanager types so it
// is directly unit-testable with hand-rolled data. A controller contributes
// signalId -> junction_id entries only when EXACTLY one junction in
// `junction_controllers` references it:
//   * a controller referenced by NO junction contributes nothing (unreferenced)
//   * a controller referenced by TWO OR MORE distinct junctions contributes nothing
//     for ANY of its signals (ambiguous -- which junction "owns" it cannot be
//     determined from the authoring alone)
// Callers must fall through to the next resolution path for signal ids missing from
// the result.
std::unordered_map<int, std::uint32_t> ResolveControllerChainJunctions(
    const std::vector<JunctionControllerRefs>& junction_controllers,
    const std::vector<ControllerSignals>&      controller_signals);

// Mirrors roadmanager::RoadObject::Orientation without this header depending on
// RoadManager.hpp (this folder's headers stay decoupled from it when the roadmanager
// type would only be used by value -- compare TrafficLightAware.hpp taking LampIcon as
// a plain int for the same reason); the .cpp translates at the one call site that
// needs the real type.
enum class SignalOrientation
{
    POSITIVE,
    NEGATIVE,
    NONE
};

// Which road-link end is "ahead" of the traffic a signal governs.
enum class SignalAheadEnd
{
    SUCCESSOR,
    PREDECESSOR
};

// Pure: path (c)'s direction handling, matching RouteSignalScan.cpp's SignalFacesTravel
// convention (POSITIVE <-> +s travel, NEGATIVE <-> -s travel). POSITIVE/NEGATIVE answer
// from the signal's own orientation alone and ignore travel_ds_dir; NONE (governs both
// directions) has no signal-intrinsic answer and falls back to travel_ds_dir's sign
// (>=0 -> SUCCESSOR, the same tie-break ScanSignalsAhead itself uses to seed direction
// from a stationary heading: `(cos(hRelative) >= 0.0) ? 1.0 : -1.0`).
SignalAheadEnd ResolveAheadLinkEnd(SignalOrientation orientation, double travel_ds_dir);

}  // namespace gt_esmini
