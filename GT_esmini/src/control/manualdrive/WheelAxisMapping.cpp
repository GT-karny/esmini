// feature:F8 -- see WheelAxisMapping.hpp for the design rationale.
//
// Deliberately SDL-free and compiled unconditionally (not under
// GT_ENABLE_SDL2), so the arithmetic every device difference funnels through
// stays under test on a machine with no wheel -- including CI, which builds
// with SDL2 OFF.

#include "gt_esmini/control/manualdrive/WheelAxisMapping.hpp"

#include <algorithm>

namespace gt_esmini
{

namespace
{

// Shared by both specs: an assigned index must exist on the device, and the
// calibration span must be non-degenerate.
void CheckAxis(const char*               label,
               int                       index,
               int                       raw_a,
               int                       raw_b,
               const char*               span_hint,
               int                       num_axes,
               std::vector<std::string>& problems)
{
    if (index < 0)
    {
        return;  // unassigned is a valid configuration, not a problem
    }
    if (index >= num_axes)
    {
        problems.emplace_back(std::string(label) + ": axis index " + std::to_string(index) +
                              " does not exist on this device (" + std::to_string(num_axes) +
                              " axes) -- function disabled");
    }
    if (raw_a == raw_b)
    {
        problems.emplace_back(std::string(label) + ": degenerate calibration (" + span_hint +
                              " are both " + std::to_string(raw_a) + ") -- reads as a constant");
    }
}

}  // namespace

double PedalAxisSpec::Normalize(int raw) const
{
    const double span = static_cast<double>(raw_full) - static_cast<double>(raw_released);
    if (span == 0.0)
    {
        return 0.0;  // released -- the safe reading to fabricate for a pedal
    }
    const double n = (static_cast<double>(raw) - static_cast<double>(raw_released)) / span;
    return std::clamp(n, 0.0, 1.0);
}

double SteerAxisSpec::Normalize(int raw) const
{
    const double span = static_cast<double>(raw_full) - static_cast<double>(raw_center);
    if (span == 0.0)
    {
        return 0.0;  // centred
    }
    const double n = (static_cast<double>(raw) - static_cast<double>(raw_center)) / span;
    return std::clamp(n, -1.0, 1.0) * SignFactor();
}

void WheelAxisMapping::CollectProblems(int num_axes, std::vector<std::string>& problems) const
{
    CheckAxis("steer", steer.index, steer.raw_center, steer.raw_full, "raw_center/raw_full", num_axes, problems);
    CheckAxis("throttle", throttle.index, throttle.raw_released, throttle.raw_full, "raw_released/raw_full", num_axes, problems);
    CheckAxis("brake", brake.index, brake.raw_released, brake.raw_full, "raw_released/raw_full", num_axes, problems);
    CheckAxis("clutch", clutch.index, clutch.raw_released, clutch.raw_full, "raw_released/raw_full", num_axes, problems);
}

}  // namespace gt_esmini
