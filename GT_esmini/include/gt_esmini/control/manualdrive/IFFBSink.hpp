#pragma once

#include "osi_hostvehicledata.pb.h"

namespace gt_esmini
{

class IFFBSink
{
public:
    virtual ~IFFBSink() = default;
    virtual void Update(const osi3::HostVehicleData& state, double dt) = 0;
    virtual void SetEnabled(bool enabled) = 0;
};

} // namespace gt_esmini
