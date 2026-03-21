#include "gt_esmini/control/manualdrive/ManualDriveCoordinator.hpp"
#include "gt_esmini/control/ControllerManualDrive.hpp"
#include "gt_esmini/control/manualdrive/IInputSource.hpp"
#include "gt_esmini/control/manualdrive/IPhysicsBackend.hpp"
#include "gt_esmini/control/manualdrive/IFFBSink.hpp"
#include "gt_esmini/osi/GT_HostVehicleReporter.hpp"
#include "gt_esmini/scenario/ExtraEntities.hpp"
#include "Entities.hpp"

namespace gt_esmini
{

void ManualDriveCoordinator::RunFrame(ControllerManualDrive& c, double dt) const
{
    // 1. Poll input source
    InputFrame frame = c.input_source_->Poll(dt);

    // 2. Override judgment (domain-aware)
    c.override_mgr_.Update(frame, dt);

    // If neither domain is manual, delegate entirely to scenario
    if (!c.override_mgr_.IsAnyManual())
    {
        c.scenarioengine::Controller::Step(dt);
        return;
    }

    // 3. Build command — merge manual input with scenario for split-domain control
    PedalSteerCommand cmd = c.last_cmd_;
    if (frame.pedal_steer)
    {
        // Start from manual input
        cmd = *frame.pedal_steer;

        // If lateral is scenario-controlled, zero out steering (scenario will handle it)
        if (!c.override_mgr_.IsLateralManual())
        {
            cmd.steering = 0.0;
        }

        // If longitudinal is scenario-controlled, zero out throttle/brake
        if (!c.override_mgr_.IsLongitudinalManual())
        {
            cmd.throttle = 0.0;
            cmd.brake = 0.0;
        }
    }
    c.last_cmd_ = cmd;

    // 4. Physics step
    osi3::HostVehicleData hvd;
    hvd = c.physics_backend_->StepPedalSteer(cmd, dt);

    // 5. FFB update
    IFFBSink* ffb = c.input_source_->GetFFBSink();
    if (!ffb) ffb = c.ffb_sink_;
    if (ffb)
    {
        ffb->Update(hvd, dt);
    }

    // 6. Extract vehicle state from HVD
    double pos_x = 0.0, pos_y = 0.0, pos_z = 0.0;
    double heading = 0.0, speed = 0.0, wheel_angle = 0.0;

    if (hvd.has_location())
    {
        const auto& loc = hvd.location();
        if (loc.has_position())
        {
            pos_x = loc.position().x();
            pos_y = loc.position().y();
            pos_z = loc.position().z();
        }
        if (loc.has_orientation())
        {
            heading = loc.orientation().yaw();
        }
        if (loc.has_velocity())
        {
            double vx = loc.velocity().x();
            double vy = loc.velocity().y();
            speed = std::sqrt(vx * vx + vy * vy);
        }
    }
    if (hvd.has_vehicle_steering() && hvd.vehicle_steering().has_vehicle_steering_wheel())
    {
        wheel_angle = hvd.vehicle_steering().vehicle_steering_wheel().angle();
    }

    // 7. Body offset and attitude
    double body_dx = 0.0, body_dy = 0.0, body_dz = 0.0;
    c.physics_backend_->GetBodyPositionOffset(body_dx, body_dy, body_dz);

    double combined_pitch = 0.0, combined_roll = 0.0;
    c.physics_backend_->GetCombinedAttitude(combined_pitch, combined_roll);

    // 8. Sync to esmini gateway
    //    block_speed_update when longitudinal is scenario-controlled
    bool block_speed = !c.override_mgr_.IsLongitudinalManual();

    if (c.object_ && c.gateway_)
    {
        c.state_applier_.Apply(c.gateway_, c.object_,
                               pos_x, pos_y, pos_z,
                               heading, speed, wheel_angle,
                               body_dx, body_dy, body_dz,
                               combined_pitch, combined_roll,
                               block_speed);
    }

    // 9. Update OSI HostVehicleReporter
    c.current_hvd_ = hvd;
    if (c.object_)
    {
        GT_HostVehicleReporter::Instance().SetBaseHostVehicleData(c.object_->GetId(), hvd);
    }

    // 10. Update vehicle lights
    if (c.object_)
    {
        auto* vehicle = dynamic_cast<scenarioengine::Vehicle*>(c.object_);
        if (vehicle)
        {
            auto* ext = VehicleExtensionManager::Instance().GetExtension(vehicle);
            if (ext)
            {
                auto set_light = [&](VehicleLightType type, bool on) {
                    LightState ls;
                    ls.mode = on ? LightState::Mode::ON : LightState::Mode::OFF;
                    ext->SetLightState(type, ls);
                };

                set_light(VehicleLightType::BRAKE_LIGHTS,     c.last_cmd_.brake > 0.05);
                set_light(VehicleLightType::REVERSING_LIGHTS,  c.last_cmd_.gear == -1);
                set_light(VehicleLightType::INDICATOR_LEFT,    (c.last_cmd_.buttons & ButtonBits::INDICATOR_LEFT) != 0);
                set_light(VehicleLightType::INDICATOR_RIGHT,   (c.last_cmd_.buttons & ButtonBits::INDICATOR_RIGHT) != 0);
            }
        }
    }

    // 11. Base controller step
    c.scenarioengine::Controller::Step(dt);
}

} // namespace gt_esmini
