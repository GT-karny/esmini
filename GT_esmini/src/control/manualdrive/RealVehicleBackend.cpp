#include "gt_esmini/control/manualdrive/RealVehicleBackend.hpp"
#include "gt_esmini/control/manualdrive/ManualDriveConfig.hpp"
#include "gt_esmini/core/ConfigLoader.hpp"
#include "Entities.hpp"

#include <cmath>

namespace gt_esmini { extern std::string GetCurrentModuleDirectory(); }

namespace gt_esmini
{

bool RealVehicleBackend::Init(const ManualDriveConfig& config, const scenarioengine::Object* obj)
{
    // Load vehicle parameters
    std::string exe_dir = GetCurrentModuleDirectory();
    ConfigLoader loader;
    std::string params_path = loader.ResolveConfigPath(exe_dir, config.real_vehicle.vehicle_params_file);
    real_vehicle_.LoadParameters(params_path);

    // Initialize vehicle dimensions from scenario object
    if (obj)
    {
        real_vehicle_.length_ = obj->boundingbox_.dimensions_.length_;
    }

    return true;
}

osi3::HostVehicleData RealVehicleBackend::StepPedalSteer(const PedalSteerCommand& cmd, double dt)
{
    last_cmd_ = cmd;
    real_vehicle_.UpdatePhysics(dt, cmd.throttle, cmd.brake, cmd.steering, cmd.gear);
    return BuildHVD(cmd);
}

#ifdef GT_ENABLE_OSI_MOTION_REQUEST
osi3::HostVehicleData RealVehicleBackend::StepMotionRequest(const osi3::MotionRequest& req, double dt)
{
    // TODO Phase 5+: PID control to convert MotionRequest → PedalSteer
    // For now, treat as zero input
    PedalSteerCommand cmd;
    last_cmd_ = cmd;
    real_vehicle_.UpdatePhysics(dt, 0.0, 0.0, 0.0, 1);
    return BuildHVD(cmd);
}
#endif

void RealVehicleBackend::SetInitialState(double x, double y, double z, double h, double speed)
{
    real_vehicle_.SetPos(x, y, z, h);
    real_vehicle_.SetSpeed(speed);
}

void RealVehicleBackend::SyncState(double x, double y, double z, double h, double speed)
{
    // Position-only resync: preserves gear, RPM, and other dynamic state
    real_vehicle_.SetPos(x, y, z, h);
    real_vehicle_.SetSpeed(speed);
}

void RealVehicleBackend::GetBodyPositionOffset(double& dx, double& dy, double& dz) const
{
    // const_cast is safe here: GetBodyPositionOffset doesn't modify state,
    // but the base class method isn't marked const
    const_cast<RealVehicle&>(real_vehicle_).GetBodyPositionOffset(dx, dy, dz);
}

void RealVehicleBackend::GetCombinedAttitude(double& pitch, double& roll) const
{
    const_cast<RealVehicle&>(real_vehicle_).GetCombinedAttitude(pitch, roll);
}

osi3::HostVehicleData RealVehicleBackend::BuildHVD(const PedalSteerCommand& cmd) const
{
    osi3::HostVehicleData hvd;

    // Location (BaseMoving) — position, velocity, orientation
    auto* location = hvd.mutable_location();
    location->mutable_position()->set_x(real_vehicle_.posX_);
    location->mutable_position()->set_y(real_vehicle_.posY_);
    location->mutable_position()->set_z(real_vehicle_.posZ_);

    location->mutable_orientation()->set_yaw(real_vehicle_.heading_);
    location->mutable_orientation()->set_pitch(real_vehicle_.pitch_);
    double roll_val = 0.0;
    double pitch_val = 0.0;
    const_cast<RealVehicle&>(real_vehicle_).GetCombinedAttitude(pitch_val, roll_val);
    location->mutable_orientation()->set_roll(roll_val);

    location->mutable_velocity()->set_x(real_vehicle_.speed_ * std::cos(real_vehicle_.heading_));
    location->mutable_velocity()->set_y(real_vehicle_.speed_ * std::sin(real_vehicle_.heading_));

    // Acceleration (vehicle frame: x=longitudinal, y=lateral)
    location->mutable_acceleration()->set_x(real_vehicle_.longAcc_);
    location->mutable_acceleration()->set_y(real_vehicle_.latAcc_);

    // Angular velocity (yaw rate)
    location->mutable_orientation_rate()->set_yaw(real_vehicle_.headingDot_);

    // Vehicle Steering
    auto* steering = hvd.mutable_vehicle_steering();
    auto* wheel = steering->mutable_vehicle_steering_wheel();
    wheel->set_angle(real_vehicle_.wheelAngle_);

    // Vehicle Powertrain
    auto* powertrain = hvd.mutable_vehicle_powertrain();
    powertrain->set_pedal_position_acceleration(cmd.throttle);
    powertrain->set_gear_transmission(cmd.gear);
    auto* motor = powertrain->add_motor();
    motor->set_rpm(real_vehicle_.GetRPM());
    motor->set_torque(real_vehicle_.GetTorqueOutput());

    // Vehicle Brake System
    auto* brake = hvd.mutable_vehicle_brake_system();
    brake->set_pedal_position_brake(cmd.brake);

    return hvd;
}

} // namespace gt_esmini
