#pragma once

#ifdef GT_ENABLE_SDL2

#include "gt_esmini/control/manualdrive/IInputSource.hpp"

#include <SDL.h>
#include <string>

namespace gt_esmini
{

// Keyboard-driven input source for ManualDrive.
//
// Polls SDL_GetKeyboardState() each frame; on Windows we additionally fall back
// to GetAsyncKeyState so keyboard input keeps working when an OSG window
// (rather than an SDL window) owns the focus.
class SDL2KeyboardInput : public IInputSource
{
public:
    SDL2KeyboardInput();
    ~SDL2KeyboardInput();

    bool       Init(const ManualDriveConfig& config) override;
    InputFrame Poll(double dt) override;
    void       Shutdown() override;
    bool       IsConnected() const override;
    IFFBSink*  GetFFBSink() override { return nullptr; }

private:
    struct GearTracker
    {
        int  current_gear   = 1;
        bool prev_upshift   = false;
        bool prev_downshift = false;
        static constexpr int MIN_GEAR = -1;
        static constexpr int MAX_GEAR = 6;
        int Update(bool upshift_pressed, bool downshift_pressed);
    };

    bool        IsKeyDown(int scancode) const;
    static int  ResolveKey(const std::string& name);

    int sc_steer_left_      = -1;
    int sc_steer_right_     = -1;
    int sc_throttle_        = -1;
    int sc_brake_           = -1;
    int sc_clutch_          = -1;
    int sc_upshift_         = -1;
    int sc_downshift_       = -1;
    int sc_override_        = -1;
    int sc_indicator_left_  = -1;
    int sc_indicator_right_ = -1;
    int sc_headlight_       = -1;
    int sc_high_beam_       = -1;
    int sc_fog_light_       = -1;
    int sc_hazard_          = -1;

    double steer_rate_         = 2.0;
    double centering_rate_     = 3.0;
    double pedal_press_rate_   = 4.0;
    double pedal_release_rate_ = 6.0;

    double steering_ = 0.0;
    double throttle_ = 0.0;
    double brake_    = 0.0;
    double clutch_   = 0.0;

    bool        sdl_initialized_ = false;
    GearTracker gear_tracker_;
};

} // namespace gt_esmini

#endif // GT_ENABLE_SDL2
