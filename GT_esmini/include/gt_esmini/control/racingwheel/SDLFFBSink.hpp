#pragma once

#ifdef GT_ENABLE_SDL2

#include "gt_esmini/control/racingwheel/IFFBSink.hpp"

#include <SDL.h>

namespace gt_esmini
{

struct RacingWheelConfig;

class SDLFFBSink : public IFFBSink
{
public:
    SDLFFBSink();
    ~SDLFFBSink();

    bool Init(SDL_Joystick* joystick, const RacingWheelConfig& config);
    void Update(const osi3::HostVehicleData& state, double dt) override;
    void SetEnabled(bool enabled) override;
    void Close();

private:
    void UpdateConstantEffect(double force);
    void UpdateSpringEffect(double coefficient);
    void UpdateDamperEffect(double coefficient);

    SDL_Haptic* haptic_ = nullptr;
    bool        enabled_ = true;

    // Effect IDs (-1 = not created)
    int constant_effect_id_ = -1;
    int spring_effect_id_   = -1;
    int damper_effect_id_   = -1;

    // Capability flags
    bool has_constant_ = false;
    bool has_spring_   = false;
    bool has_damper_   = false;

    // Config
    double spring_coefficient_ = 0.5;
    double damper_coefficient_ = 0.3;
    double constant_gain_      = 1.0;
    double max_force_          = 1.0;
};

} // namespace gt_esmini

#endif // GT_ENABLE_SDL2
