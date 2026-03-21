#pragma once

#ifdef GT_ENABLE_SDL2

#include "gt_esmini/control/racingwheel/IInputSource.hpp"
#include "gt_esmini/control/racingwheel/SDLFFBSink.hpp"

#include <SDL.h>

namespace gt_esmini
{

class SDL2WheelInput : public IInputSource
{
public:
    SDL2WheelInput();
    ~SDL2WheelInput();

    bool Init(const RacingWheelConfig& config) override;
    InputFrame Poll(double dt) override;
    void Shutdown() override;
    bool IsConnected() const override;
    IFFBSink* GetFFBSink() override;

private:
    double NormalizeAxis(int raw) const;
    double NormalizePedal(int raw) const;
    int ReadGearFromButtons() const;

    SDL_Joystick* joystick_  = nullptr;
    int           device_idx_ = 0;
    double        deadzone_   = 0.05;
    bool          sdl_initialized_ = false;

    // Axis calibration (auto min/max)
    struct AxisCalibration
    {
        int min_seen = 0;
        int max_seen = 0;
        bool calibrated = false;
    };
    AxisCalibration steer_cal_;
    AxisCalibration throttle_cal_;
    AxisCalibration brake_cal_;
    AxisCalibration clutch_cal_;

    SDLFFBSink ffb_sink_;
};

} // namespace gt_esmini

#endif // GT_ENABLE_SDL2
