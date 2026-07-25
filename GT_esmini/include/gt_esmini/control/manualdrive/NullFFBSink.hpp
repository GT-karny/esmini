#pragma once

#include "gt_esmini/control/manualdrive/IFFBSink.hpp"

namespace gt_esmini
{

class NullFFBSink : public IFFBSink
{
public:
    void Update(const osi3::HostVehicleData& /*state*/, double /*dt*/) override {}
    void SetEnabled(bool /*enabled*/) override {}
    // SetSteerTarget / GetInterventionSample inherit the base-class no-ops.
};

} // namespace gt_esmini
