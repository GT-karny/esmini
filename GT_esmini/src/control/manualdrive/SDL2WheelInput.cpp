#ifdef GT_ENABLE_SDL2

#include "gt_esmini/control/manualdrive/SDL2WheelInput.hpp"
#include "gt_esmini/control/manualdrive/ManualDriveConfig.hpp"
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

int SDL2WheelInput::GearTracker::Update(bool upshift_pressed, bool downshift_pressed)
{
    // Edge detection: shift only on button press (not hold)
    if (upshift_pressed && !prev_upshift)
    {
        if (current_gear < MAX_GEAR)
        {
            current_gear++;
        }
    }
    if (downshift_pressed && !prev_downshift)
    {
        if (current_gear > MIN_GEAR)
        {
            current_gear--;
        }
    }

    prev_upshift = upshift_pressed;
    prev_downshift = downshift_pressed;
    return current_gear;
}

bool SDL2WheelInput::Init(const ManualDriveConfig& config)
{
    device_idx_             = config.sdl2.device_index;
    deadzone_               = config.sdl2.deadzone;
    upshift_button_         = config.sdl2.upshift_button;
    downshift_button_       = config.sdl2.downshift_button;
    override_button_        = config.sdl2.override_button;
    indicator_left_button_  = config.sdl2.indicator_left_button;
    indicator_right_button_ = config.sdl2.indicator_right_button;
    headlight_button_       = config.sdl2.headlight_button;
    high_beam_button_       = config.sdl2.high_beam_button;
    fog_light_button_       = config.sdl2.fog_light_button;
    hazard_button_          = config.sdl2.hazard_button;

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

    // Gear from paddle shifters (edge-detected)
    bool upshift   = SDL_JoystickGetButton(joystick_, upshift_button_) != 0;
    bool downshift = SDL_JoystickGetButton(joystick_, downshift_button_) != 0;
    cmd.gear = gear_tracker_.Update(upshift, downshift);

    // Map configured buttons to standardized ButtonBits
    cmd.buttons = 0;
    auto read_btn = [&](int btn_id, uint32_t bit) {
        if (btn_id >= 0 && SDL_JoystickGetButton(joystick_, btn_id))
            cmd.buttons |= bit;
    };
    read_btn(override_button_,        ButtonBits::OVERRIDE);
    read_btn(indicator_left_button_,  ButtonBits::INDICATOR_LEFT);
    read_btn(indicator_right_button_, ButtonBits::INDICATOR_RIGHT);
    read_btn(headlight_button_,       ButtonBits::HEADLIGHT);
    read_btn(high_beam_button_,       ButtonBits::HIGH_BEAM);
    read_btn(fog_light_button_,       ButtonBits::FOG_LIGHT);
    read_btn(hazard_button_,          ButtonBits::HAZARD);

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


} // namespace gt_esmini

#endif // GT_ENABLE_SDL2
