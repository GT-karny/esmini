#ifdef GT_ENABLE_SDL2

#include "gt_esmini/control/racingwheel/SDL2WheelInput.hpp"
#include "gt_esmini/control/racingwheel/RacingWheelConfig.hpp"
#include "CommonMini.hpp"
#include "logger.hpp"

#include <cmath>
#include <iostream>

namespace gt_esmini
{

SDL2WheelInput::SDL2WheelInput() = default;

SDL2WheelInput::~SDL2WheelInput()
{
    Shutdown();
}

bool SDL2WheelInput::Init(const RacingWheelConfig& config)
{
    device_idx_ = config.sdl2.device_index;
    deadzone_   = config.sdl2.deadzone;

    // Initialize SDL joystick + haptic subsystems (NOT video)
    if (SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC) < 0)
    {
        LOG_ERROR("SDL2WheelInput: SDL_Init failed: {}", SDL_GetError());
        return false;
    }
    sdl_initialized_ = true;

    int num_joysticks = SDL_NumJoysticks();
    LOG_INFO("SDL2WheelInput: {} joystick(s) detected", num_joysticks);

    if (device_idx_ >= num_joysticks)
    {
        LOG_WARN("SDL2WheelInput: Device index {} not available ({} joysticks). No wheel connected.",
                 device_idx_, num_joysticks);
        return true;  // Not an error — just no device
    }

    joystick_ = SDL_JoystickOpen(device_idx_);
    if (!joystick_)
    {
        LOG_ERROR("SDL2WheelInput: Failed to open joystick {}: {}", device_idx_, SDL_GetError());
        return false;
    }

    LOG_INFO("SDL2WheelInput: Opened '{}' — {} axes, {} buttons, {} hats",
             SDL_JoystickName(joystick_),
             SDL_JoystickNumAxes(joystick_),
             SDL_JoystickNumButtons(joystick_),
             SDL_JoystickNumHats(joystick_));

    // Initialize FFB
    if (!ffb_sink_.Init(joystick_, config))
    {
        LOG_WARN("SDL2WheelInput: FFB initialization failed (non-fatal)");
    }

    return true;
}

InputFrame SDL2WheelInput::Poll(double /*dt*/)
{
    InputFrame frame;
    frame.connected = (joystick_ != nullptr);

    if (!joystick_)
    {
        return frame;
    }

    SDL_JoystickUpdate();

    PedalSteerCommand cmd;

    // Axis 0: Steering (-32768 ~ 32767)
    int raw_steer = SDL_JoystickGetAxis(joystick_, 0);
    cmd.steering = NormalizeAxis(raw_steer);

    // Axis 1: Throttle (G29: 32767=released, -32768=fully pressed — inverted)
    int raw_throttle = SDL_JoystickGetAxis(joystick_, 1);
    cmd.throttle = NormalizePedal(raw_throttle);

    // Axis 2: Brake (same inversion as throttle)
    int raw_brake = SDL_JoystickGetAxis(joystick_, 2);
    cmd.brake = NormalizePedal(raw_brake);

    // Axis 3: Clutch (same inversion)
    if (SDL_JoystickNumAxes(joystick_) > 3)
    {
        int raw_clutch = SDL_JoystickGetAxis(joystick_, 3);
        cmd.clutch = NormalizePedal(raw_clutch);
    }

    // Apply deadzone to steering
    if (std::abs(cmd.steering) < deadzone_)
    {
        cmd.steering = 0.0;
    }

    // Gear from buttons (G29: paddle shifters or H-pattern)
    cmd.gear = ReadGearFromButtons();

    // All buttons as bitmask
    int num_buttons = SDL_JoystickNumButtons(joystick_);
    cmd.buttons = 0;
    for (int i = 0; i < num_buttons && i < 32; ++i)
    {
        if (SDL_JoystickGetButton(joystick_, i))
        {
            cmd.buttons |= (1u << i);
        }
    }

    frame.pedal_steer = cmd;
    return frame;
}

void SDL2WheelInput::Shutdown()
{
    ffb_sink_.Close();

    if (joystick_)
    {
        SDL_JoystickClose(joystick_);
        joystick_ = nullptr;
    }

    if (sdl_initialized_)
    {
        SDL_QuitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC);
        sdl_initialized_ = false;
    }
}

bool SDL2WheelInput::IsConnected() const
{
    return joystick_ != nullptr;
}

IFFBSink* SDL2WheelInput::GetFFBSink()
{
    return &ffb_sink_;
}

double SDL2WheelInput::NormalizeAxis(int raw) const
{
    // -32768 ~ 32767 → -1.0 ~ 1.0
    return static_cast<double>(raw) / 32767.0;
}

double SDL2WheelInput::NormalizePedal(int raw) const
{
    // G29 pedals: 32767=released, -32768=fully pressed
    // Normalize to 0.0 (released) ~ 1.0 (fully pressed)
    double normalized = (32767.0 - static_cast<double>(raw)) / 65535.0;
    return std::clamp(normalized, 0.0, 1.0);
}

int SDL2WheelInput::ReadGearFromButtons() const
{
    // G29 paddle shifters: typically button 4 = upshift, button 5 = downshift
    // For now, simple mapping:
    // - If upshift button is held: gear = 1 (forward)
    // - If downshift button is held: gear = -1 (reverse)
    // - Default: gear = 1

    // This is a simplified approach; a proper gear tracker would maintain state
    // across frames. For Phase 4, just report current button state.
    // TODO: Implement proper gear state machine with shift-up/shift-down tracking

    return 1;  // Default forward gear
}

} // namespace gt_esmini

#endif // GT_ENABLE_SDL2
