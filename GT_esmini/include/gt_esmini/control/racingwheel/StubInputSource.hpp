#pragma once

#include "gt_esmini/control/racingwheel/IInputSource.hpp"

namespace gt_esmini
{

class StubInputSource : public IInputSource
{
public:
    bool Init(const RacingWheelConfig& /*config*/) override { return true; }

    InputFrame Poll(double /*dt*/) override
    {
        InputFrame frame;
        frame.connected = true;
        frame.pedal_steer = PedalSteerCommand{};  // all zeros
        return frame;
    }

    void Shutdown() override {}
    bool IsConnected() const override { return true; }
};

} // namespace gt_esmini
