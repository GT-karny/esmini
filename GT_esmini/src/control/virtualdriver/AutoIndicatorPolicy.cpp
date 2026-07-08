#include "gt_esmini/control/virtualdriver/AutoIndicatorPolicy.hpp"

namespace gt_esmini
{

IndicatorSnapshot AutoIndicatorPolicy::Update(const IndicatorContext& ctx, double dt)
{
    IndicatorSnapshot out;

    // Manual override takes precedence (human input source drives indicators).
    if (ctx.manual_active)
    {
        out.left_on  = ctx.manual_left;
        out.right_on = ctx.manual_right;
        active_dir_  = 0;
        off_timer_   = 0.0;
        return out;
    }

    // Auto mode: latch the maneuver direction; hold briefly after it clears.
    if (ctx.maneuver_dir != 0)
    {
        active_dir_ = ctx.maneuver_dir;
        off_timer_  = 0.0;
    }
    else if (active_dir_ != 0)
    {
        off_timer_ += dt;
        if (off_timer_ >= cfg_.min_on_time)
        {
            active_dir_ = 0;
            off_timer_  = 0.0;
        }
    }

    out.left_on  = (active_dir_ > 0);   // +1 = left
    out.right_on = (active_dir_ < 0);   // -1 = right
    return out;
}

}  // namespace gt_esmini
