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
    auto_resume_button_     = config.sdl2.auto_resume_button;

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

    // Prime axis state after open. On Windows/DirectInput a fresh JoystickOpen
    // returns raw=0 for every axis until the device sends its first HID report;
    // that means an untouched G29 pedal (whose released convention is raw=+32767)
    // reads back as NormalizePedal(0) = 0.5 = "half-throttle phantom" for the
    // first N frames → OverrideManager's throttle_threshold (0.1) trips →
    // longitudinal locks to MANUAL immediately → AD-driven SpeedActions ramping
    // ego from rest (e.g. virtual_driver_basic AccelAction 0→15 m/s) are frozen
    // at 0. Scenarios that InitAction-set a nonzero starting speed hide this
    // (e.g. anticipation batch's EgoSpeed=13.889) — the intermittency is what
    // makes the class of bug easy to miss in unit tests.
    //
    // Retry loop: Update + short delay, checking whether ANY axis has reported
    // a non-zero value. Bail as soon as we see one, otherwise keep pumping up
    // to a hard timeout. G29 typically reports within ~50 ms on cold open but
    // successive rapid re-opens can take 300-500 ms (observed variance in
    // real-machine testing this session). Timeout without a report → per-axis
    // "seen a non-zero" latch below treats still-zero axes as "released"
    // (raw = 32767 equivalent for pedals) instead of phantom half-pressed.
    const int n_axes = SDL_JoystickNumAxes(joystick_);
    const int max_settle_ms = 500;
    const int step_ms = 25;
    int total_ms = 0;
    bool any_axis_reported = false;
    while (total_ms < max_settle_ms && !any_axis_reported)
    {
        SDL_JoystickUpdate();
        for (int i = 0; i < n_axes; ++i)
        {
            if (SDL_JoystickGetAxis(joystick_, i) != 0)
            {
                any_axis_reported = true;
                break;
            }
        }
        if (any_axis_reported) break;
        SDL_Delay(step_ms);
        total_ms += step_ms;
    }
    SDL_JoystickUpdate();
    std::string axis_state;
    for (int i = 0; i < n_axes; ++i)
    {
        if (i > 0) axis_state += " ";
        axis_state += "a" + std::to_string(i) + "=" +
                      std::to_string(SDL_JoystickGetAxis(joystick_, i));
    }
    LOG_INFO("SDL2WheelInput: primed axis state (settled after {} ms, any_reported={}): {}",
             total_ms, any_axis_reported, axis_state);
    // Latch which axes have been "seen live" (non-zero). Pedals that never
    // reported are treated as "released" (raw override to 32767) in Poll —
    // this is the anti-phantom-half-throttle guard.
    axis_seen_live_.assign(n_axes, false);
    for (int i = 0; i < n_axes; ++i)
        if (SDL_JoystickGetAxis(joystick_, i) != 0)
            axis_seen_live_[i] = true;

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

    // Anti-phantom-half-throttle guard: if a pedal axis has NEVER reported a
    // non-zero value since open, the driver has not yet sent its initial HID
    // report and raw=0 does NOT mean "half-pressed" — it means "unknown". For
    // pedals whose released convention is raw=+32767, treat still-uninitialized
    // axes as released. Once ANY frame reports a real (non-zero) value, latch
    // that axis as live for the rest of the session. Steering axis (0) is not
    // pedal-inverted so this guard is confined to axes 1/2/3.
    auto raw_axis_pedal = [&](int idx) -> int {
        int raw = SDL_JoystickGetAxis(joystick_, idx);
        if (raw != 0 && idx < (int)axis_seen_live_.size()) axis_seen_live_[idx] = true;
        if (raw == 0 && idx < (int)axis_seen_live_.size() && !axis_seen_live_[idx])
            return 32767;   // "released" sentinel
        return raw;
    };

    PedalSteerCommand cmd;

    // Axis 0: Steering (-32768 ~ 32767). Guard-less: raw=0 is a legitimate
    // "wheel at center" reading.
    int raw_steer = SDL_JoystickGetAxis(joystick_, 0);
    cmd.steering = NormalizeAxis(raw_steer);

    // Axis 1: Throttle (G29: 32767=released, -32768=fully pressed — inverted)
    int raw_throttle = raw_axis_pedal(1);
    cmd.throttle = NormalizePedal(raw_throttle);

    // Axis 2: Brake (same inversion as throttle)
    int raw_brake = raw_axis_pedal(2);
    cmd.brake = NormalizePedal(raw_brake);

    // Axis 3: Clutch (same inversion)
    if (SDL_JoystickNumAxes(joystick_) > 3)
    {
        int raw_clutch = raw_axis_pedal(3);
        cmd.clutch = NormalizePedal(raw_clutch);
    }

    // Apply deadzone to steering with rescaling
    // Without rescaling, output jumps from 0 to deadzone_ at the threshold boundary,
    // creating a perceptible notch. Rescale so output is continuous: 0 at threshold, ±1 at full lock.
    if (std::abs(cmd.steering) < deadzone_)
    {
        cmd.steering = 0.0;
    }
    else
    {
        double sign = (cmd.steering > 0.0) ? 1.0 : -1.0;
        cmd.steering = sign * (std::abs(cmd.steering) - deadzone_) / (1.0 - deadzone_);
    }

    // Paddle shifters: report raw button state for the forward-AT path,
    // and also keep the legacy GearTracker output in cmd.gear so callers
    // that still use the legacy single-gear path keep working.
    bool upshift   = SDL_JoystickGetButton(joystick_, upshift_button_) != 0;
    bool downshift = SDL_JoystickGetButton(joystick_, downshift_button_) != 0;
    cmd.paddle_up_pressed   = upshift;
    cmd.paddle_down_pressed = downshift;
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
    read_btn(auto_resume_button_,     ButtonBits::AUTO_RESUME);

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
