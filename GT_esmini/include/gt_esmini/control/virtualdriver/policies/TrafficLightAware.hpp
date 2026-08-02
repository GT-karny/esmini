#pragma once

#include <unordered_map>
#include <vector>

#include "gt_esmini/control/virtualdriver/ITrafficPolicy.hpp"

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
};

// Phase 3b: scan the route ahead for the nearest traffic light that faces the
// ego and applies to its lane, read its current lamp phase, and emit a
// STOP_AT_S constraint at the signal when the ego should stop.
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
};

}  // namespace gt_esmini
