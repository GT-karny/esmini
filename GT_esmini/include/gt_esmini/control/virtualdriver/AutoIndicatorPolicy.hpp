#pragma once

#include "gt_esmini/control/virtualdriver/IIndicatorPolicy.hpp"

namespace gt_esmini
{

struct AutoIndicatorConfig
{
    double lead_time    = 2.0;   // [s] reserved — controller sets maneuver_dir this far ahead
    double min_on_time  = 0.3;   // [s] minimum on-time after a maneuver ends (anti-flicker)
};

// Phase 1 indicator policy. A simplified version of ControllerRouteDrive's
// turn-signal logic: maps an upcoming maneuver direction to the left/right
// indicator, with a minimum on-time to avoid flicker. Honors a manual override
// when a human input source is driving the indicators.
class AutoIndicatorPolicy : public IIndicatorPolicy
{
public:
    explicit AutoIndicatorPolicy(const AutoIndicatorConfig& cfg = {}) : cfg_(cfg) {}
    void Configure(const AutoIndicatorConfig& cfg) { cfg_ = cfg; }

    IndicatorSnapshot Update(const IndicatorContext& ctx, double dt) override;

private:
    AutoIndicatorConfig cfg_;
    int                 active_dir_ = 0;    // currently latched auto direction (+1 left, -1 right)
    double              off_timer_  = 0.0;  // time since maneuver_dir returned to 0
};

}  // namespace gt_esmini
