#pragma once

namespace gt_esmini
{
class ControllerRealDriver;

class EsminiStateApplier
{
public:
    void Apply(ControllerRealDriver& controller, double combined_pitch, double combined_roll, bool block_speed_update) const;
};
} // namespace gt_esmini
