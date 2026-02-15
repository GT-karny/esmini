#pragma once

namespace scenarioengine
{
class ObjectState;
}

namespace gt_esmini
{
class IHostVehicleDataProvider
{
public:
    virtual ~IHostVehicleDataProvider() = default;
    virtual int UpdateFromObjectState(const scenarioengine::ObjectState* ego_state) = 0;
    virtual void Send() = 0;
};
} // namespace gt_esmini
