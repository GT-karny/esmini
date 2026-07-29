#pragma once

#include <cstdint>

namespace gt_esmini
{

// Button bitmask definitions for PedalSteerCommand.buttons
namespace ButtonBits
{
    constexpr uint32_t OVERRIDE        = 1u << 0;
    constexpr uint32_t INDICATOR_LEFT  = 1u << 1;
    constexpr uint32_t INDICATOR_RIGHT = 1u << 2;
    constexpr uint32_t HEADLIGHT       = 1u << 3;  // LOW_BEAM toggle
    constexpr uint32_t HIGH_BEAM       = 1u << 4;
    constexpr uint32_t FOG_LIGHT       = 1u << 5;
    constexpr uint32_t HAZARD          = 1u << 6;  // WARNING_LIGHTS toggle
    constexpr uint32_t AUTO_RESUME     = 1u << 7;  // feature:F7 — manual->auto return (edge-triggered)
}

// Normalized pedal/steer command — the lingua franca between a driver
// (human IInputSource or an automatic IDriverModel) and an IPhysicsBackend.
// Lives in control/common/ so it can be shared across controllers
// (ManualDrive, VirtualDriver, ...) without coupling to any single config.
struct PedalSteerCommand
{
    double   steering = 0.0;  // -1.0 ~ 1.0 (normalized)
    double   throttle = 0.0;  // 0.0 ~ 1.0
    double   brake    = 0.0;  // 0.0 ~ 1.0
    double   clutch   = 0.0;  // 0.0 ~ 1.0
    int      gear     = 0;    // legacy: -1=R, 0=N, 1~6 (used by legacy physics path)
    uint32_t buttons  = 0;    // bitmask (see ButtonBits)

    // Raw paddle button states for the forward-AT physics path.
    // Not transmitted over the network wire format; defaults preserve
    // legacy behaviour for non-paddle input sources.
    bool     paddle_up_pressed   = false;
    bool     paddle_down_pressed = false;
};

} // namespace gt_esmini
