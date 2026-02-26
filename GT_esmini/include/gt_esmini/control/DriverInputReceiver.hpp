#pragma once

namespace gt_esmini
{
class ControllerRealDriver;

class DriverInputReceiver
{
public:
    void Receive(ControllerRealDriver& controller) const;
};
} // namespace gt_esmini
