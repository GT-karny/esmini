#pragma once

#ifdef GT_ENABLE_SDL2

#include "gt_esmini/control/manualdrive/IInputSource.hpp"
#include "gt_esmini/control/manualdrive/SDLFFBSink.hpp"
#include "gt_esmini/control/manualdrive/WheelAxisMapping.hpp"

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
    // feature:F8 -- reads one pedal axis through its spec, applying the
    // "no HID report yet" sentinel described on axis_seen_live_ below.
    // Unassigned (index < 0) reads as 0.0 = released.
    double ReadPedal(const PedalAxisSpec& spec);

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
    double        deadzone_               = 0.0;
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
    // req-vd-ad:REQ-AD-026 / REQ-AD-030 (phase C) -- ADAS stalk. -1 = unassigned.
    int           acc_toggle_button_     = -1;
    int           acc_set_resume_button_ = -1;
    int           acc_speed_up_button_   = -1;
    int           acc_speed_down_button_ = -1;
    int           acc_thw_cycle_button_  = -1;
    int           msl_toggle_button_     = -1;
    bool          sdl_initialized_        = false;
    // feature:F8 -- which SDL axis carries which function, and how each one's
    // raw range maps to [0,1] / [-1,+1]. Loaded from config; Init() disables
    // any spec naming an axis this device does not have (and says so in the
    // log) rather than silently falling back to axis 0.
    WheelAxisMapping axes_;
    // Per-axis "has reported a non-zero value since open" latch. Used by the
    // pedal read guard to treat raw=0 as "released" until we've seen a real
    // HID report — Windows/DirectInput can return raw=0 for pedals for
    // hundreds of ms after JoystickOpen, and on a G29 (released = +32767) a
    // phantom normalized 0.5 would spuriously trip OverrideManager's
    // throttle_threshold. The substituted value is the axis's OWN configured
    // raw_released, and the guard is skipped entirely for an axis whose
    // released reading IS 0 (see PedalAxisSpec::NeedsReleasedSentinel).
    std::vector<bool> axis_seen_live_;

    GearTracker gear_tracker_;
    SDLFFBSink  ffb_sink_;
};

} // namespace gt_esmini

#endif // GT_ENABLE_SDL2
