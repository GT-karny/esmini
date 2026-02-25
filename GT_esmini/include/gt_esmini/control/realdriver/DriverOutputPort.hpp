#pragma once

#include <vector>

namespace gt_esmini
{
struct LonProfilePoint;
class ControllerRealDriver;

class DriverOutputPort
{
public:
    void SendLonProfile(ControllerRealDriver& controller, const std::vector<LonProfilePoint>& profile) const;
};
} // namespace gt_esmini
