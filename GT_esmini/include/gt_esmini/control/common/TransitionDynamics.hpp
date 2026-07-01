#pragma once

#include "OSCPrivateAction.hpp"

#include <algorithm>
#include <cmath>

namespace gt_esmini
{

inline double EvaluateTransitionShape(scenarioengine::OSCPrivateAction::DynamicsShape shape,
                                      double start_value,
                                      double delta,
                                      double progress)
{
    constexpr double kPi = 3.14159265358979323846;
    progress = std::clamp(progress, 0.0, 1.0);
    switch (shape)
    {
    case scenarioengine::OSCPrivateAction::DynamicsShape::SINUSOIDAL:
        return start_value - delta * (std::cos(kPi * progress) - 1.0) / 2.0;
    case scenarioengine::OSCPrivateAction::DynamicsShape::CUBIC:
        return start_value + delta * progress * progress * (3.0 - 2.0 * progress);
    case scenarioengine::OSCPrivateAction::DynamicsShape::LINEAR:
        return start_value + delta * progress;
    case scenarioengine::OSCPrivateAction::DynamicsShape::STEP:
        return start_value + delta;
    default:
        return start_value;
    }
}

}  // namespace gt_esmini
