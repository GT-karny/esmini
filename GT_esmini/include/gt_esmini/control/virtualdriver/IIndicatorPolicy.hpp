#pragma once

#include "gt_esmini/control/virtualdriver/VirtualDriverTypes.hpp"

namespace scenarioengine
{
class Object;
}

namespace gt_esmini
{

// Per-frame inputs for indicator (turn-signal) decisions.
struct IndicatorContext
{
    scenarioengine::Object* object = nullptr;  // ego
    // Upcoming lateral maneuver direction: +1 = left, -1 = right, 0 = none.
    // Phase 1 derives this from the active lane-change / preview lateral shift.
    int    maneuver_dir  = 0;
    double sim_time      = 0.0;
    // Manual override (when a human IInputSource drives the indicators).
    bool   manual_left   = false;
    bool   manual_right  = false;
    bool   manual_active = false;  // true → use manual_* and ignore auto logic
};

// Cross-cutting indicator policy: decides left/right turn-signal state in
// either Auto (route/maneuver-driven, with lead-time pre-arming) or Manual mode.
class IIndicatorPolicy
{
public:
    virtual ~IIndicatorPolicy() = default;
    virtual IndicatorSnapshot Update(const IndicatorContext& ctx, double dt) = 0;
};

}  // namespace gt_esmini
