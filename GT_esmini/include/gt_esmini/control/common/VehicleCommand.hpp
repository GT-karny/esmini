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
    // Physical wheel toggle's AUTO->MANUAL direction. Web and network Resume
    // requests deliberately set AUTO_RESUME only, preserving their old meaning.
    constexpr uint32_t TAKE_MANUAL     = 1u << 8;

    // req-vd-ad:REQ-AD-026 / REQ-AD-030, vd-func:FUNC-079 / FUNC-081 (phase C).
    // ManualDrive ADAS operating controls -- the real-car ACC/limiter stalk,
    // mapped onto the SAME button bitmask every other ManualDrive control uses
    // (design §4-1: "manual_drive.json の既存ボタンマッピング流儀に乗せる").
    //
    // ALL SIX ARE EDGE-TRIGGERED, not level-held: AdasCoexistenceStack decodes
    // rising edges against the previous frame's mask (DecodeAdasOperations,
    // AccLonController.hpp). A driver holding the SET/RESUME button down must
    // not re-set the target speed every frame, and a held speed-up button must
    // not ramp the setting at frame rate -- the physical stalk they model is a
    // momentary switch. ScriptedInputSource's step-held `buttons` channel makes
    // the same guarantee testable: an ops profile expresses one press as a
    // keyframe pair (bit set, then cleared).
    constexpr uint32_t ACC_TOGGLE      = 1u << 9;   // OFF <-> STANDBY
    constexpr uint32_t ACC_SET_RESUME  = 1u << 10;  // set (from STANDBY) / resume (after cancel)
    constexpr uint32_t ACC_SPEED_UP    = 1u << 11;  // +1 step on the setting
    constexpr uint32_t ACC_SPEED_DOWN  = 1u << 12;  // -1 step on the setting
    constexpr uint32_t ACC_THW_CYCLE   = 1u << 13;  // cycle the following-distance stage
    constexpr uint32_t MSL_TOGGLE      = 1u << 14;  // speed limiter OFF <-> STANDBY
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
