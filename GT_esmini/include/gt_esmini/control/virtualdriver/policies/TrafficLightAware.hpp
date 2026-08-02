#pragma once

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "gt_esmini/control/virtualdriver/ITrafficPolicy.hpp"
#include "gt_esmini/control/virtualdriver/policies/JunctionStopGuard.hpp"

namespace gt_esmini
{

// Phase of the nearest relevant traffic light. UNKNOWN = no on-lamp readable.
enum class TrafficLightPhase
{
    UNKNOWN,
    RED,
    YELLOW,
    GREEN
};

struct TrafficLightParams
{
    // Max deceleration the driver will accept to stop on yellow. Stop on yellow if
    // the light can still be reached-and-stopped within this decel
    // (dist >= v^2 / (2*yellow_decel)); otherwise proceed (too late to stop safely).
    double yellow_decel = 4.0;   // [m/s^2]
};

// Pure decision: should the ego stop for this phase at `dist` ahead while doing
// `v_ego`? RED -> stop. GREEN/UNKNOWN -> go. YELLOW -> stop only if it can still
// be done within yellow_decel (else proceed through). Factored out for unit
// testing; the policy adds a commitment latch on top (see Evaluate).
bool TrafficLightShouldStop(TrafficLightPhase phase, double dist, double v_ego, const TrafficLightParams& p);

// True when a lamp icon addresses VEHICLE traffic: a blank head or a directional
// arrow. Pedestrian / bicycle / tram / bus icons address other road users and
// must never govern the ego. Anything else (unknown, countdown) counts as
// vehicle-facing — the fallback keeps the pre-filter behaviour.
//
// The argument is a roadmanager::LampIcon code. It is taken as int so this header
// stays free of the RoadManager include; the enum is explicitly numbered in
// RoadManager.hpp (ICON_PEDESTRIAN = 15 .. ICON_BUS_AND_TRAM = 24) and those
// values are what the traffic_light_type_map bakes into every lamp.
bool IsVehicleLampIcon(int lamp_icon);

// True when a traffic-light head governs vehicle traffic: it has at least one lamp
// AND at least one lamp icon addresses vehicles. An EMPTY lamp list means the head
// carries no readable state (an unknown type/subtype combo leaves nr_lamps_ == 0),
// so it does not govern either — otherwise it would mask a real vehicle head
// standing behind it.
//
// Why icons and not the OpenDRIVE type string: the icons come from RoadManager's
// traffic_light_type_map, so every pedestrian / bicycle / tram subtype it knows is
// covered without a second table here that could drift out of sync.
bool IsVehicleTrafficLightHead(const std::vector<int>& lamp_icons);

struct TrafficLightAwareConfig
{
    TrafficLightParams params;
    double             lookahead   = 80.0;  // [m] route look-ahead for signals
    // Stop this far BEFORE the signal s (so the front halts at the line while the
    // ego origin/rear stays short of it). Critically this keeps the signal within
    // the forward scan while stopped, so the RED constraint persists instead of
    // vanishing the instant the origin crosses the signal (which read as
    // red-light-running). ~ vehicle front overhang.
    double             stop_margin = 3.0;   // [m]
    // "Don't block the box". OFF restores the pre-guard behaviour exactly: the
    // nearest vehicle head governs and its stop target is emitted wherever it
    // lands, including in the middle of an intersection. See JunctionStopGuard.hpp.
    bool                    junction_guard_enabled = true;
    JunctionStopGuardParams junction;
    // Stop-line pairing (docs/virtualdriver/design/stop_line_stop_target.md): for
    // the governing head only, swap its distance for a paired stop-line signal's
    // when one is found within stop_line_window before the anchor. The anchor is
    // min(the entry of the junction the head governs, the head's own distance)
    // (RouteSignalScan::ResolveStopLineAnchor) when that junction is resolved
    // and reached by this route (SignalJunctionResolver) -- pairing then runs
    // through RouteSignalScan::FindPairedStopLineByDistance against that anchor
    // (the min is what keeps the paired stop-line at or before the head too,
    // design/stop_line_stop_target.md §5); otherwise the anchor is the head
    // itself and pairing runs through RouteSignalScan::FindPairedStopLine (the
    // head's own index), unchanged from before. OFF is a kill switch: neither
    // is ever called, so the stop target stays head_s - stop_margin exactly.
    bool   stop_line_aware_enabled = true;
    double stop_line_window       = 10.0;  // [m] pairing search window before the anchor (junction entry, or the head as fallback)
};

// Phase 3b: scan the route ahead for the nearest traffic light that faces the
// ego and applies to its lane, read its current lamp phase, and emit a
// STOP_AT_S constraint at the signal when the ego should stop.
//
// On top of that, two junction rules (junction_guard_enabled):
//
//   * the governing head's stop target is run through ResolveJunctionSafeStop,
//     so a red light sitting just beyond an intersection can no longer park the
//     ego inside it — with the ego already in the box the constraint is dropped
//     and the ego drives out;
//   * heads BEYOND the governing one are additionally consulted for the entry
//     side of the same rule: a red light the ego could not clear the
//     intersection before makes the ego hold BEFORE the intersection. This is
//     the only thing a non-governing head can do — it never contributes an
//     ordinary stop of its own, so "the nearest head decides where you stop"
//     still holds. (A green head at the junction entry masks the red one beyond
//     it until the ego is already committed, which is precisely why the entry
//     rule cannot be driven off the governing head alone.)
class TrafficLightAware : public ITrafficPolicy
{
public:
    explicit TrafficLightAware(const TrafficLightAwareConfig& cfg = {}) : cfg_(cfg) {}
    TrafficPolicySnapshot Evaluate(const TrafficPolicyContext& ctx) override;

private:
    TrafficLightAwareConfig cfg_;
    // Commitment latch keyed by signal id: once we decide to stop for a light
    // (RED, or a feasible YELLOW), keep stopping until it turns GREEN — so the
    // yellow decision does not flip-flop to "go" as the gap shrinks, and a brief
    // scan loss does not release the stop.
    std::unordered_map<int, bool> committed_;
    // Companion latch for the junction entry rule, keyed by junction id: once we
    // are holding before an intersection we cannot clear, the target must not be
    // released as the remaining distance drops under the braking distance. Rebuilt
    // every frame from the junctions actually held for, so it clears itself the
    // moment the light releases or the junction leaves the scan.
    std::unordered_set<std::uint32_t> junction_committed_;
};

}  // namespace gt_esmini
