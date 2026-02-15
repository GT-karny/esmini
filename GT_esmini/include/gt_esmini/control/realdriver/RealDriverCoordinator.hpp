#pragma once

namespace gt_esmini
{
class ControllerRealDriver;

class RealDriverCoordinator
{
public:
    void RunFrame(ControllerRealDriver& controller, double time_step) const;
};
} // namespace gt_esmini
