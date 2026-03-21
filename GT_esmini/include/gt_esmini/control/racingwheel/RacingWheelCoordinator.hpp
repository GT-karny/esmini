#pragma once

namespace gt_esmini
{

class ControllerRacingWheel;

class RacingWheelCoordinator
{
public:
    void RunFrame(ControllerRacingWheel& controller, double time_step) const;
};

} // namespace gt_esmini
