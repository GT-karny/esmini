#pragma once

#include "gt_esmini/control/racingwheel/IFFBSink.hpp"

namespace gt_esmini
{

class NullFFBSink : public IFFBSink
{
public:
    void Update(const osi3::HostVehicleData& /*state*/, double /*dt*/) override {}
    void SetEnabled(bool /*enabled*/) override {}
};

} // namespace gt_esmini
