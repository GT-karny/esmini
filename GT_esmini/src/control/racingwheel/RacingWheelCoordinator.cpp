#include "gt_esmini/control/racingwheel/RacingWheelCoordinator.hpp"
#include "gt_esmini/control/ControllerRacingWheel.hpp"
#include "gt_esmini/control/racingwheel/IInputSource.hpp"
#include "gt_esmini/control/racingwheel/IPhysicsBackend.hpp"
#include "gt_esmini/control/racingwheel/IFFBSink.hpp"
#include "gt_esmini/osi/GT_HostVehicleReporter.hpp"
#include "gt_esmini/scenario/ExtraEntities.hpp"
#include "Entities.hpp"

namespace gt_esmini
{

void RacingWheelCoordinator::RunFrame(ControllerRacingWheel& c, double dt) const
{
    // 1. Poll input source
    InputFrame frame = c.input_source_->Poll(dt);

    // 2. Override judgment
    c.override_mgr_.Update(frame, dt);

    if (!c.override_mgr_.IsManualMode())
    {
        // AUTO mode: let scenario actions drive the vehicle
        c.scenarioengine::Controller::Step(dt);
        return;
    }

    // 3. MANUAL mode: process input through physics backend
    osi3::HostVehicleData hvd;

    if (frame.pedal_steer)
    {
        c.last_cmd_ = *frame.pedal_steer;
        hvd = c.physics_backend_->StepPedalSteer(*frame.pedal_steer, dt);
    }
#ifdef GT_ENABLE_OSI_MOTION_REQUEST
    else if (frame.motion_request)
    {
        hvd = c.physics_backend_->StepMotionRequest(*frame.motion_request, dt);
    }
#endif
    else
    {
        // No input: step with last known command
        hvd = c.physics_backend_->StepPedalSteer(c.last_cmd_, dt);
    }

    // 4. FFB update (prefer input source's FFB, fallback to controller's)
    IFFBSink* ffb = c.input_source_->GetFFBSink();
    if (!ffb) ffb = c.ffb_sink_;
    if (ffb)
    {
        ffb->Update(hvd, dt);
    }

    // 5. Extract vehicle state from HVD for gateway sync
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

    // 6. Get body offset and attitude from physics backend
    double body_dx = 0.0, body_dy = 0.0, body_dz = 0.0;
    c.physics_backend_->GetBodyPositionOffset(body_dx, body_dy, body_dz);

    double combined_pitch = 0.0, combined_roll = 0.0;
    c.physics_backend_->GetCombinedAttitude(combined_pitch, combined_roll);

    // 7. Sync to esmini gateway
    if (c.object_ && c.gateway_)
    {
        c.state_applier_.Apply(c.gateway_, c.object_,
                               pos_x, pos_y, pos_z,
                               heading, speed, wheel_angle,
                               body_dx, body_dy, body_dz,
                               combined_pitch, combined_roll,
                               false);  // block_speed_update: TODO check running actions
    }

    // 8. Update OSI HostVehicleReporter
    c.current_hvd_ = hvd;
    if (c.object_)
    {
        GT_HostVehicleReporter::Instance().SetBaseHostVehicleData(c.object_->GetId(), hvd);
    }

    // 9. Update vehicle lights
    if (c.object_)
    {
        auto* vehicle = dynamic_cast<scenarioengine::Vehicle*>(c.object_);
        if (vehicle)
        {
            auto* ext = VehicleExtensionManager::Instance().GetExtension(vehicle);
            if (ext)
            {
                LightState brake_ls;
                brake_ls.mode = (c.last_cmd_.brake > 0.05) ? LightState::Mode::ON : LightState::Mode::OFF;
                ext->SetLightState(VehicleLightType::BRAKE_LIGHTS, brake_ls);

                LightState reverse_ls;
                reverse_ls.mode = (c.last_cmd_.gear == -1) ? LightState::Mode::ON : LightState::Mode::OFF;
                ext->SetLightState(VehicleLightType::REVERSING_LIGHTS, reverse_ls);
            }
        }
    }

    // 10. Base controller step
    c.scenarioengine::Controller::Step(dt);
}

} // namespace gt_esmini
