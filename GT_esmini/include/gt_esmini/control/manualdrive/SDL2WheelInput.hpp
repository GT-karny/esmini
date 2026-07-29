#pragma once

#ifdef GT_ENABLE_SDL2

#include "gt_esmini/control/manualdrive/IInputSource.hpp"
#include "gt_esmini/control/manualdrive/SDLFFBSink.hpp"

#include <SDL.h>
#include <vector>

namespace gt_esmini
{

class SDL2WheelInput : public IInputSource
{
public:
    SDL2WheelInput();
    ~SDL2WheelInput();

    bool Init(const ManualDriveConfig& config) override;
    InputFrame Poll(double dt) override;
    void Shutdown() override;
    bool IsConnected() const override;
    IFFBSink* GetFFBSink() override;

private:
    double NormalizeAxis(int raw) const;
    double NormalizePedal(int raw) const;

    struct GearTracker
    {
        int  current_gear    = 1;   // -1=R, 0=N, 1~6
        bool prev_upshift    = false;
        bool prev_downshift  = false;
        static constexpr int MIN_GEAR = -1;
        static constexpr int MAX_GEAR = 6;

        int Update(bool upshift_pressed, bool downshift_pressed);
    };

    SDL_Joystick* joystick_  = nullptr;
    int           device_idx_             = 0;
    double        deadzone_               = 0.05;
    int           upshift_button_         = 4;
    int           downshift_button_       = 5;
    int           override_button_        = 0;
    int           indicator_left_button_  = 7;
    int           indicator_right_button_ = 6;
    int           headlight_button_      = -1;
    int           high_beam_button_      = -1;
    int           fog_light_button_      = -1;
    int           hazard_button_         = -1;
    int           auto_resume_button_    = -1;  // feature:F7
    bool          sdl_initialized_        = false;
    // Per-axis "has reported a non-zero value since open" latch. Used by the
    // pedal read guard to treat raw=0 as "released" (32767) until we've seen
    // a real HID report — Windows/DirectInput can return raw=0 for pedals
    // for hundreds of ms after JoystickOpen, and phantom NormalizePedal(0)=0.5
    // would spuriously trip OverrideManager's throttle_threshold.
    std::vector<bool> axis_seen_live_;

    GearTracker gear_tracker_;
    SDLFFBSink  ffb_sink_;
};

} // namespace gt_esmini

#endif // GT_ENABLE_SDL2
