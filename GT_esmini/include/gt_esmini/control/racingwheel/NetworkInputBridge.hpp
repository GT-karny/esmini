#pragma once

#include "gt_esmini/control/racingwheel/IInputSource.hpp"
#include "gt_esmini/control/racingwheel/ITransport.hpp"

#include <vector>

namespace gt_esmini
{

class NetworkInputBridge : public IInputSource
{
public:
    NetworkInputBridge();
    ~NetworkInputBridge();

    bool Init(const RacingWheelConfig& config) override;
    InputFrame Poll(double dt) override;
    void Shutdown() override;
    bool IsConnected() const override;

private:
    ITransport*  transport_ = nullptr;
    std::string  level_;  // "pedal_steer" or "motion_request"
    bool         has_data_ = false;

    PedalSteerCommand last_cmd_;  // hold-last-value
    std::vector<char> recv_buf_;

    static constexpr uint32_t MAGIC_PEDAL_STEER = 0x50535443;  // "PSTC"
};

} // namespace gt_esmini
