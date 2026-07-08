#pragma once

#include "gt_esmini/control/virtualdriver/VirtualDriverTypes.hpp"
#include "gt_esmini/control/common/VehicleCommand.hpp"

namespace gt_esmini
{

// Ego dynamic state the inverse-controller needs.
struct DriverState
{
    double x          = 0.0;
    double y          = 0.0;
    double h          = 0.0;  // heading [rad]
    double speed      = 0.0;  // [m/s]
    double wheel_base = 2.7;  // [m]
};

// Inverse controller (driver model): converts a trajectory preview + current
// ego state into a normalized pedal/steer command. Pluggable — PID+PurePursuit
// is the Phase 1 default; Stanley/MPC can be dropped in later.
class IDriverModel
{
public:
    virtual ~IDriverModel() = default;
    // out_snapshot may be null. Returns the command to feed the physics backend.
    virtual PedalSteerCommand Compute(const ShortPlannerSnapshot& plan,
                                      const DriverState&          state,
                                      double                      dt,
                                      DriverModelSnapshot*        out_snapshot) = 0;
};

}  // namespace gt_esmini
