#pragma once

#include "gt_esmini/control/racingwheel/RacingWheelTypes.hpp"

namespace gt_esmini
{

class IFFBSink;
struct RacingWheelConfig;

class IInputSource
{
public:
    virtual ~IInputSource() = default;
    virtual bool Init(const RacingWheelConfig& config) = 0;
    virtual InputFrame Poll(double dt) = 0;
    virtual void Shutdown() = 0;
    virtual bool IsConnected() const = 0;
    virtual IFFBSink* GetFFBSink() { return nullptr; }
};

} // namespace gt_esmini
