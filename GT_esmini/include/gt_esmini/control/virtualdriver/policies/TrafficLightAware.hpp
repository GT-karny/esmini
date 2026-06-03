#pragma once

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
    double comfort_decel = 2.0;   // [m/s^2] used to judge a yellow stop is comfortable
    double yellow_margin = 1.2;   // require dist > margin * braking_dist to commit to stopping
};

// Pure decision: should the ego stop for this phase at `dist` ahead while doing
// `v_ego`? RED -> stop. GREEN/UNKNOWN -> go. YELLOW -> stop only if it can be
// done comfortably (else proceed through). Factored out for unit testing.
bool TrafficLightShouldStop(TrafficLightPhase phase, double dist, double v_ego, const TrafficLightParams& p);

struct TrafficLightAwareConfig
{
    TrafficLightParams params;
    double             lookahead = 80.0;  // [m] route look-ahead for signals
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
};

}  // namespace gt_esmini
