#ifdef GT_ENABLE_SDL2

#include "gt_esmini/control/manualdrive/SDL2KeyboardInput.hpp"
#include "gt_esmini/control/manualdrive/ManualDriveConfig.hpp"
#include "logger.hpp"

#include <algorithm>
#include <cmath>

#ifdef _WIN32
// Windows fallback so keyboard input works regardless of which window has focus.
// SDL_GetKeyboardState only delivers events SDL itself received — when the OSG
// viewer owns the focus, SDL gets nothing.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace gt_esmini
{

SDL2KeyboardInput::SDL2KeyboardInput() = default;

SDL2KeyboardInput::~SDL2KeyboardInput()
{
    Shutdown();
}

int SDL2KeyboardInput::GearTracker::Update(bool upshift_pressed, bool downshift_pressed)
{
    if (upshift_pressed && !prev_upshift)
    {
        if (current_gear < MAX_GEAR) current_gear++;
    }
    if (downshift_pressed && !prev_downshift)
    {
        if (current_gear > MIN_GEAR) current_gear--;
    }
    prev_upshift   = upshift_pressed;
    prev_downshift = downshift_pressed;
    return current_gear;
}

int SDL2KeyboardInput::ResolveKey(const std::string& name)
{
    if (name.empty()) return -1;

    // Common shorthand → SDL canonical name. The naive JSON parser strips
    // spaces so values like "Left Shift" come through as "LeftShift" — provide
    // single-word aliases to avoid that ambiguity.
    struct Alias { const char* in; const char* out; };
    static const Alias kAliases[] = {
        {"LShift",    "Left Shift"},
        {"RShift",    "Right Shift"},
        {"LCtrl",     "Left Ctrl"},
        {"RCtrl",     "Right Ctrl"},
        {"LAlt",      "Left Alt"},
        {"RAlt",      "Right Alt"},
        {"LeftShift", "Left Shift"},
        {"RightShift","Right Shift"},
        {"LeftCtrl",  "Left Ctrl"},
        {"RightCtrl", "Right Ctrl"},
        {"LeftAlt",   "Left Alt"},
        {"RightAlt",  "Right Alt"},
    };
    const char* canonical = name.c_str();
    for (const auto& a : kAliases)
    {
        if (name == a.in) { canonical = a.out; break; }
    }

    SDL_Scancode sc = SDL_GetScancodeFromName(canonical);
    if (sc == SDL_SCANCODE_UNKNOWN) return -1;
    return static_cast<int>(sc);
}

#ifdef _WIN32
// Map SDL_Scancode → Win32 VK code for the keys we care about.
// Returns 0 for unknown / unmapped (no fallback).
static int ScancodeToVK(int sc)
{
    switch (sc)
    {
        case SDL_SCANCODE_A: return 'A';
        case SDL_SCANCODE_B: return 'B';
        case SDL_SCANCODE_C: return 'C';
        case SDL_SCANCODE_D: return 'D';
        case SDL_SCANCODE_E: return 'E';
        case SDL_SCANCODE_F: return 'F';
        case SDL_SCANCODE_G: return 'G';
        case SDL_SCANCODE_H: return 'H';
        case SDL_SCANCODE_I: return 'I';
        case SDL_SCANCODE_J: return 'J';
        case SDL_SCANCODE_K: return 'K';
        case SDL_SCANCODE_L: return 'L';
        case SDL_SCANCODE_M: return 'M';
        case SDL_SCANCODE_N: return 'N';
        case SDL_SCANCODE_O: return 'O';
        case SDL_SCANCODE_P: return 'P';
        case SDL_SCANCODE_Q: return 'Q';
        case SDL_SCANCODE_R: return 'R';
        case SDL_SCANCODE_S: return 'S';
        case SDL_SCANCODE_T: return 'T';
        case SDL_SCANCODE_U: return 'U';
        case SDL_SCANCODE_V: return 'V';
        case SDL_SCANCODE_W: return 'W';
        case SDL_SCANCODE_X: return 'X';
        case SDL_SCANCODE_Y: return 'Y';
        case SDL_SCANCODE_Z: return 'Z';
        case SDL_SCANCODE_0: return '0';
        case SDL_SCANCODE_1: return '1';
        case SDL_SCANCODE_2: return '2';
        case SDL_SCANCODE_3: return '3';
        case SDL_SCANCODE_4: return '4';
        case SDL_SCANCODE_5: return '5';
        case SDL_SCANCODE_6: return '6';
        case SDL_SCANCODE_7: return '7';
        case SDL_SCANCODE_8: return '8';
        case SDL_SCANCODE_9: return '9';
        case SDL_SCANCODE_SPACE:    return VK_SPACE;
        case SDL_SCANCODE_RETURN:   return VK_RETURN;
        case SDL_SCANCODE_ESCAPE:   return VK_ESCAPE;
        case SDL_SCANCODE_TAB:      return VK_TAB;
        case SDL_SCANCODE_LSHIFT:   return VK_LSHIFT;
        case SDL_SCANCODE_RSHIFT:   return VK_RSHIFT;
        case SDL_SCANCODE_LCTRL:    return VK_LCONTROL;
        case SDL_SCANCODE_RCTRL:    return VK_RCONTROL;
        case SDL_SCANCODE_LALT:     return VK_LMENU;
        case SDL_SCANCODE_RALT:     return VK_RMENU;
        case SDL_SCANCODE_LEFT:     return VK_LEFT;
        case SDL_SCANCODE_RIGHT:    return VK_RIGHT;
        case SDL_SCANCODE_UP:       return VK_UP;
        case SDL_SCANCODE_DOWN:     return VK_DOWN;
        case SDL_SCANCODE_COMMA:    return VK_OEM_COMMA;
        case SDL_SCANCODE_PERIOD:   return VK_OEM_PERIOD;
        case SDL_SCANCODE_SLASH:    return VK_OEM_2;
        case SDL_SCANCODE_SEMICOLON: return VK_OEM_1;
        default: return 0;
    }
}
#endif

bool SDL2KeyboardInput::IsKeyDown(int scancode) const
{
    if (scancode < 0) return false;

#ifdef _WIN32
    // Prefer the Win32 path on Windows so focus loss doesn't drop input.
    int vk = ScancodeToVK(scancode);
    if (vk != 0)
    {
        // High bit of return value = currently pressed.
        return (GetAsyncKeyState(vk) & 0x8000) != 0;
    }
#endif

    int          numkeys = 0;
    const Uint8* state   = SDL_GetKeyboardState(&numkeys);
    if (!state || scancode >= numkeys) return false;
    return state[scancode] != 0;
}

bool SDL2KeyboardInput::Init(const ManualDriveConfig& config)
{
    const auto& kb = config.keyboard;
    sc_steer_left_      = ResolveKey(kb.steer_left);
    sc_steer_right_     = ResolveKey(kb.steer_right);
    sc_throttle_        = ResolveKey(kb.throttle);
    sc_brake_           = ResolveKey(kb.brake);
    sc_clutch_          = ResolveKey(kb.clutch);
    sc_upshift_         = ResolveKey(kb.upshift);
    sc_downshift_       = ResolveKey(kb.downshift);
    sc_override_        = ResolveKey(kb.override_key);
    sc_indicator_left_  = ResolveKey(kb.indicator_left);
    sc_indicator_right_ = ResolveKey(kb.indicator_right);
    sc_headlight_       = ResolveKey(kb.headlight);
    sc_high_beam_       = ResolveKey(kb.high_beam);
    sc_fog_light_       = ResolveKey(kb.fog_light);
    sc_hazard_          = ResolveKey(kb.hazard);

    steer_rate_         = kb.steer_rate;
    centering_rate_     = kb.centering_rate;
    pedal_press_rate_   = kb.pedal_press_rate;
    pedal_release_rate_ = kb.pedal_release_rate;

    // SDL_VIDEO + SDL_EVENTS are needed for SDL_GetKeyboardState. SDL_Init is
    // idempotent — fine to coexist with SDL2WheelInput's joystick init.
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0)
    {
        // Non-fatal on Windows since we have GetAsyncKeyState; warn only.
        LOG_WARN("SDL2KeyboardInput: SDL_Init(VIDEO|EVENTS) failed: {} (continuing)", SDL_GetError());
    }
    else
    {
        sdl_initialized_ = true;
    }

    LOG_INFO("SDL2KeyboardInput: ready (steer=A/D, pedals=W/S/LShift, shift=E/Q — see config to remap)");
    return true;
}

InputFrame SDL2KeyboardInput::Poll(double dt)
{
    InputFrame frame;
    frame.connected = true;

    if (sdl_initialized_)
    {
        SDL_PumpEvents();
    }

    bool kl = IsKeyDown(sc_steer_left_);
    bool kr = IsKeyDown(sc_steer_right_);
    bool kt = IsKeyDown(sc_throttle_);
    bool kb = IsKeyDown(sc_brake_);
    bool kc = IsKeyDown(sc_clutch_);

    // Steering: rate-limited toward target ±1, centering when neither pressed.
    if (kl && !kr)
    {
        steering_ -= steer_rate_ * dt;
    }
    else if (kr && !kl)
    {
        steering_ += steer_rate_ * dt;
    }
    else
    {
        // Centering toward 0
        double step = centering_rate_ * dt;
        if (steering_ > step)       steering_ -= step;
        else if (steering_ < -step) steering_ += step;
        else                        steering_  = 0.0;
    }
    steering_ = std::clamp(steering_, -1.0, 1.0);

    // Pedal helper: press toward 1, release toward 0.
    auto step_pedal = [&](double& v, bool pressed) {
        if (pressed) v += pedal_press_rate_   * dt;
        else         v -= pedal_release_rate_ * dt;
        v = std::clamp(v, 0.0, 1.0);
    };
    step_pedal(throttle_, kt);
    step_pedal(brake_,    kb);
    step_pedal(clutch_,   kc);

    PedalSteerCommand cmd;
    cmd.steering = steering_;
    cmd.throttle = throttle_;
    cmd.brake    = brake_;
    cmd.clutch   = clutch_;

    bool up_pressed = IsKeyDown(sc_upshift_);
    bool dn_pressed = IsKeyDown(sc_downshift_);
    cmd.paddle_up_pressed   = up_pressed;
    cmd.paddle_down_pressed = dn_pressed;
    cmd.gear = gear_tracker_.Update(up_pressed, dn_pressed);

    cmd.buttons = 0;
    auto set_bit = [&](int sc, uint32_t bit) {
        if (IsKeyDown(sc)) cmd.buttons |= bit;
    };
    set_bit(sc_override_,        ButtonBits::OVERRIDE);
    set_bit(sc_indicator_left_,  ButtonBits::INDICATOR_LEFT);
    set_bit(sc_indicator_right_, ButtonBits::INDICATOR_RIGHT);
    set_bit(sc_headlight_,       ButtonBits::HEADLIGHT);
    set_bit(sc_high_beam_,       ButtonBits::HIGH_BEAM);
    set_bit(sc_fog_light_,       ButtonBits::FOG_LIGHT);
    set_bit(sc_hazard_,          ButtonBits::HAZARD);

    frame.pedal_steer = cmd;
    return frame;
}

void SDL2KeyboardInput::Shutdown()
{
    if (sdl_initialized_)
    {
        SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
        sdl_initialized_ = false;
    }
}

bool SDL2KeyboardInput::IsConnected() const
{
    // Keyboard is always "connected".
    return true;
}

} // namespace gt_esmini

#endif // GT_ENABLE_SDL2
