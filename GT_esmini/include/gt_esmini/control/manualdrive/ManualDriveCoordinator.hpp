#pragma once

namespace gt_esmini
{

class ControllerManualDrive;

class ManualDriveCoordinator
{
public:
    void RunFrame(ControllerManualDrive& controller, double time_step) const;
};

} // namespace gt_esmini
