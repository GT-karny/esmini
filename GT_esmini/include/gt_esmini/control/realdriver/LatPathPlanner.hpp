#pragma once

namespace gt_esmini
{
class ControllerRealDriver;

class LatPathPlanner
{
public:
    bool HandleActions(ControllerRealDriver& controller, const char* phase_label) const;
};
} // namespace gt_esmini
