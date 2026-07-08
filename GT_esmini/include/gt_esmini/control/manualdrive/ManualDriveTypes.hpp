#pragma once

#include <cstdint>
#include <optional>

// PedalSteerCommand + ButtonBits moved to control/common/ so they can be
// shared across controllers (ManualDrive, VirtualDriver). Re-exported here
// for backward compatibility of existing manualdrive/* includes.
#include "gt_esmini/control/common/VehicleCommand.hpp"

#ifdef GT_ENABLE_OSI_MOTION_REQUEST
#include "osi_motionrequest.pb.h"
#endif

namespace gt_esmini
{

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
