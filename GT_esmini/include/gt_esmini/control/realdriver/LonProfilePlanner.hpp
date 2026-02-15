#pragma once

#include <vector>

namespace gt_esmini
{
struct LonProfilePoint
{
    double t_offset;
    double v_target;
    double a_max;
    double j_max;
};

class ControllerRealDriver;

class LonProfilePlanner
{
public:
    std::vector<LonProfilePoint> BuildProfile(double current_speed, double target_speed) const;
};
} // namespace gt_esmini
