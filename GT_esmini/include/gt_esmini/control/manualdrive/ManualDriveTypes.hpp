#pragma once

#include <cstdint>
#include <optional>

#ifdef GT_ENABLE_OSI_MOTION_REQUEST
#include "osi_motionrequest.pb.h"
#endif

namespace gt_esmini
{

// Button bitmask definitions for PedalSteerCommand.buttons
namespace ButtonBits
{
    constexpr uint32_t OVERRIDE        = 1 << 0;
    constexpr uint32_t INDICATOR_LEFT  = 1 << 1;
    constexpr uint32_t INDICATOR_RIGHT = 1 << 2;
    // Bits 3-31 reserved for future use
}

struct PedalSteerCommand
{
    double   steering = 0.0;  // -1.0 ~ 1.0 (normalized)
    double   throttle = 0.0;  // 0.0 ~ 1.0
    double   brake    = 0.0;  // 0.0 ~ 1.0
    double   clutch   = 0.0;  // 0.0 ~ 1.0
    int      gear     = 0;    // -1=R, 0=N, 1~6
    uint32_t buttons  = 0;    // bitmask (see ButtonBits)
};

struct InputFrame
{
    std::optional<PedalSteerCommand> pedal_steer;
#ifdef GT_ENABLE_OSI_MOTION_REQUEST
    std::optional<osi3::MotionRequest> motion_request;
#endif
    bool connected = false;
};

} // namespace gt_esmini
