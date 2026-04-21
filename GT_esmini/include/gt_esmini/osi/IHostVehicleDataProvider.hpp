#pragma once

namespace scenarioengine
{
class Object;
}

namespace gt_esmini
{
class IHostVehicleDataProvider
{
public:
    virtual ~IHostVehicleDataProvider() = default;
    virtual int UpdateFromObjectState(const scenarioengine::Object* ego_object) = 0;
    virtual void Send() = 0;
};
} // namespace gt_esmini
