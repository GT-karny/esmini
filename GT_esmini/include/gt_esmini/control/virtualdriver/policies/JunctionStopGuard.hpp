#pragma once

#include <cstdint>
#include <vector>

namespace gt_esmini
{

// One junction connecting-road stretch found on the ego's route ahead, in route
// distance from the ego. Produced by ScanSignalsAhead (RouteSignalScan.hpp),
// which already walks the route, so no second walk is needed.
//
// NOTE: membership is the RAW test `road->GetJunction() != ID_UNDEFINED`. It is
// deliberately NOT ManeuverAwareSpeedPlanner's `kind == "junction"` sample tag,
// which comes from IsTurningConnector (junction road AND heading change >=
// SHARP_TURN_RATE) and is therefore FALSE on a straight-through connector — the
// exact case this guard has to catch.
struct RouteJunctionSpan
{
    double        entry_ahead = 0.0;    // [m] where the connecting road begins (0.0 when ego_inside)
    double        exit_ahead  = 0.0;    // [m] where it ends; +inf when it runs past the scan horizon
    std::uint32_t junction_id = 0;      // OpenDRIVE junction id (latch key / diagnostics)
    bool          ego_inside  = false;  // the scan STARTED on this connecting road
};

struct JunctionStopGuardParams
{
    // Stop this far short of a junction entry when a stop target is pulled back
    // to before the junction. Same meaning (and, from config, the same value) as
    // TrafficLightAwareConfig::stop_margin: the front halts at the line.
    double stop_margin = 3.0;   // [m]
    // How far PAST a junction exit the ego origin must be able to stand before it
    // counts as having cleared the junction. Roughly the distance from the origin
    // (rear axle in esmini) to the rear bumper plus a safety gap; a stop target
    // nearer than this to the exit would leave the tail inside the box.
    double exit_clearance = 5.0;  // [m]
    // Deceleration assumed when asking "can we still stop before the junction?".
    // Fed from the mid/long planner's comfort_decel so the feasibility question is
    // asked with the deceleration that will actually be used to shape the approach.
    double decel = 2.0;         // [m/s^2]
};

enum class JunctionStopAction
{
    HOLD,       // the wanted stop target strands nobody — emit it unchanged
    PULL_BACK,  // it would strand the ego in a junction, but we can still stop short of it
    SUPPRESS    // it would strand the ego and stopping short is no longer possible — clear the box
};

struct JunctionStopResolution
{
    JunctionStopAction action      = JunctionStopAction::HOLD;
    double             s_stop      = 0.0;  // HOLD / PULL_BACK target, route metres ahead
    std::uint32_t      junction_id = 0;    // the blocking junction (PULL_BACK / SUPPRESS)
    bool               blocked     = false;// a junction span would be occupied by the stopped ego
};

// "Don't block the box": decide where a wanted STOP_AT_S target may actually be
// placed given the junctions on the route ahead.
//
// A stop target BLOCKS a junction when it lies past that junction's entry and
// not far enough past its exit for the ego to stand clear:
//
//     span.entry < s_stop_wanted  &&  s_stop_wanted < span.exit + exit_clearance
//
// The entry side is a STRICT `<` on purpose. A stop line placed just before a
// junction (the normal case — TrafficLightAware already backs off by
// stop_margin, and the resulting target sits AT or BEFORE the entry) must keep
// resolving to HOLD, unchanged. Only a target the ego would have to enter the
// junction to reach is treated as blocking.
//
// When blocked:
//   * ego already inside that junction   -> SUPPRESS (there is no "short of it"
//     left; the legal move is to clear the intersection, not to park in it)
//   * stopping short is still reachable  -> PULL_BACK to `entry - stop_margin`
//     (clamped at 0), i.e. hold before entering a box we cannot clear
//   * otherwise                          -> SUPPRESS
//
// `stop_already_committed` skips the braking-feasibility test: once the caller
// has committed to holding before this junction, the target must not evaporate
// as the remaining distance shrinks below the (now tiny) braking distance —
// that release would be a lurch straight into the box the guard just prevented.
//
// Pure function: unit-tested in test_TrafficPolicies.cpp, same shape as
// TrafficLightShouldStop.
JunctionStopResolution ResolveJunctionSafeStop(double                               s_stop_wanted,
                                               const std::vector<RouteJunctionSpan>& spans,
                                               double                               v_ego,
                                               const JunctionStopGuardParams&       p,
                                               bool                                 stop_already_committed = false);

}  // namespace gt_esmini
