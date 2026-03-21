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
    constexpr uint32_t OVERRIDE        = 1u << 0;
    constexpr uint32_t INDICATOR_LEFT  = 1u << 1;
    constexpr uint32_t INDICATOR_RIGHT = 1u << 2;
    constexpr uint32_t HEADLIGHT       = 1u << 3;  // LOW_BEAM toggle
    constexpr uint32_t HIGH_BEAM       = 1u << 4;
    constexpr uint32_t FOG_LIGHT       = 1u << 5;
    constexpr uint32_t HAZARD          = 1u << 6;  // WARNING_LIGHTS toggle
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

// Indicator auto-cancel FSM (real-car steering column behavior)
struct IndicatorFSM
{
    enum class State { OFF, ARMED, ACTIVE };

    State  left  = State::OFF;
    State  right = State::OFF;

    struct Output { bool left_on; bool right_on; };

    // Call once per frame.
    //   buttons / prev_buttons : current / previous ButtonBits bitmask
    //   steering               : normalized -1.0(left) ~ +1.0(right)
    //   cancel_angle           : positive threshold (normalized) for cancel point
    //   prev_steering          : steering value from previous frame
    //   hazard_active          : if true, suppress steering-based cancel
    Output Update(uint32_t buttons, uint32_t prev_buttons,
                  double steering, double prev_steering,
                  double cancel_angle, bool hazard_active)
    {
        // Edge detection helpers
        auto rising = [&](uint32_t bit) { return (buttons & bit) && !(prev_buttons & bit); };

        // --- Left indicator ---
        if (rising(ButtonBits::INDICATOR_LEFT))
        {
            if (left == State::OFF)
            {
                left  = State::ARMED;
                right = State::OFF;  // cancel opposite
            }
            else
            {
                left = State::OFF;  // manual cancel
            }
        }

        // --- Right indicator ---
        if (rising(ButtonBits::INDICATOR_RIGHT))
        {
            if (right == State::OFF)
            {
                right = State::ARMED;
                left  = State::OFF;  // cancel opposite
            }
            else
            {
                right = State::OFF;  // manual cancel
            }
        }

        // --- Steering-based transitions (skip if hazard active) ---
        if (!hazard_active)
        {
            // Left: cancel_angle threshold is at -cancel_angle (left side)
            if (left == State::ARMED)
            {
                // Engage when steering goes deeper than cancel point to the left
                if (steering < -cancel_angle)
                    left = State::ACTIVE;
            }
            else if (left == State::ACTIVE)
            {
                // Cancel when steering crosses -cancel_angle from left to right
                if (prev_steering < -cancel_angle && steering >= -cancel_angle)
                    left = State::OFF;
            }

            // Right: cancel_angle threshold is at +cancel_angle (right side)
            if (right == State::ARMED)
            {
                if (steering > cancel_angle)
                    right = State::ACTIVE;
            }
            else if (right == State::ACTIVE)
            {
                if (prev_steering > cancel_angle && steering <= cancel_angle)
                    right = State::OFF;
            }
        }

        return { left != State::OFF, right != State::OFF };
    }
};

} // namespace gt_esmini
