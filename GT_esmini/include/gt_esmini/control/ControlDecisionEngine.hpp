#pragma once

namespace gt_esmini
{
class ControllerRealDriver;

class ControlDecisionEngine
{
public:
    void UpdateSetSpeed(ControllerRealDriver& controller) const;
};
} // namespace gt_esmini
