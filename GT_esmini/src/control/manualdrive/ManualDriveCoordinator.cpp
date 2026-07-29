#include "gt_esmini/control/manualdrive/ManualDriveCoordinator.hpp"
#include "gt_esmini/control/ControllerManualDrive.hpp"
#include "gt_esmini/control/manualdrive/IInputSource.hpp"
#include "gt_esmini/control/common/IPhysicsBackend.hpp"
#include "gt_esmini/control/manualdrive/IFFBSink.hpp"
#include "gt_esmini/control/common/DomainOwnershipLedger.hpp"
#include "gt_esmini/osi/GT_HostVehicleReporter.hpp"
#include "gt_esmini/scenario/ExtraEntities.hpp"
#include "Entities.hpp"
#include "logger.hpp"

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
    // 3-bus. feature:F7 S3 — publish owned channels, then consume the unowned
    // ones if this controller is the integrator. See the bus contract in
    // DomainOwnershipLedger.hpp for why the merge is at the command stage.
    // Publishing precedes the output gate below so a ManualDrive that owns only
    // the lateral domain still supplies steering every frame despite not
    // integrating.
    if (c.object_)
    {
        auto&     ledger = DomainOwnershipLedger::Instance();
        const int obj_id = c.object_->GetId();

        if (ledger.IsOwner(obj_id, &c, OwnedDomain::LATERAL))
        {
            ledger.PublishLateral(obj_id, &c, cmd.steering);
        }
        if (ledger.IsOwner(obj_id, &c, OwnedDomain::LONGITUDINAL))
        {
            ledger.PublishLongitudinal(obj_id, &c, cmd.throttle, cmd.brake);
        }

        if (ledger.IsIntegrator(obj_id, &c))
        {
            if (!ledger.IsOwner(obj_id, &c, OwnedDomain::LATERAL))
            {
                double owner_steering = 0.0;
                if (ledger.ConsumeLateral(obj_id, owner_steering))
                {
                    cmd.steering = owner_steering;
                }
            }
            if (!ledger.IsOwner(obj_id, &c, OwnedDomain::LONGITUDINAL))
            {
                double owner_throttle = 0.0, owner_brake = 0.0;
                if (ledger.ConsumeLongitudinal(obj_id, owner_throttle, owner_brake))
                {
                    cmd.throttle = owner_throttle;
                    cmd.brake    = owner_brake;
                }
            }
        }
    }

    c.last_cmd_ = cmd;

    // 3a. feature:F7 S2 — output gate. Only the object's designated integrator
    // advances the body; see DomainOwnershipLedger::IntegratorOf for the rule.
    // The input poll, override judgment and command build above still run for a
    // non-integrator: those are the commands S3 will merge into the integrator.
    const bool is_integrator =
        c.object_ && DomainOwnershipLedger::Instance().IsIntegrator(c.object_->GetId(), &c);

    if (is_integrator && !c.was_domain_integrator_)
    {
        // Taking over integration from another controller mid-run, WITHOUT this
        // controller having just been activated (Activate() seeds the edge, so
        // that case never lands here). The backend has been frozen while the car
        // moved, so resume from the object's pose rather than teleporting it
        // back to where we last left off.
        //
        // KNOWN LIMITATION: object_->pos_ may already carry the scenario's own
        // advance for this frame, in which case that advance is absorbed here and
        // integrated a second time. Not reachable from the S2 scenarios — the
        // integrator is fixed for the whole run under a static split, and the
        // handover case is covered by the Activate() seeding — but it is the same
        // double-count that produced the measured 1.05 ratio before that seeding
        // existed. S3 removes the need for this path entirely.
        c.physics_backend_->SyncState(c.object_->pos_.GetX(),
                                      c.object_->pos_.GetY(),
                                      c.object_->pos_.GetZ(),
                                      c.object_->pos_.GetH(),
                                      c.object_->GetSpeed());
        LOG_INFO("ManualDriveController[{}]: took over integration ({})",
                 c.GetName(),
                 DomainOwnershipLedger::Instance().Describe(c.object_->GetId()));
    }
    else if (!is_integrator && c.was_domain_integrator_)
    {
        // Handing integration over. Release force feedback for the same reason
        // Deactivate() does: the device holds the last commanded force as an
        // infinite-duration effect and we are about to stop feeding it.
        IFFBSink* released = c.input_source_->GetFFBSink();
        if (!released) released = c.ffb_sink_;
        if (released)
        {
            released->SetSteerTarget(0.0, false);
            released->SetEnabled(false);
        }
        LOG_INFO("ManualDriveController[{}]: no longer integrating ({})",
                 c.GetName(),
                 DomainOwnershipLedger::Instance().Describe(c.object_->GetId()));
    }
    c.was_domain_integrator_ = is_integrator;

    if (!is_integrator)
    {
        c.scenarioengine::Controller::Step(dt);
        return;
    }

    // 4. Resync physics backend on AUTO→MANUAL transition to prevent coordinate jump
    if (c.override_mgr_.JustTransitionedToManual() && c.object_)
    {
        c.physics_backend_->SyncState(
            c.object_->pos_.GetX(),
            c.object_->pos_.GetY(),
            c.object_->pos_.GetZ(),
            c.object_->pos_.GetH(),
            c.object_->GetSpeed());
    }

    // 5. Physics step
    osi3::HostVehicleData hvd;
    hvd = c.physics_backend_->StepPedalSteer(cmd, dt);

    // 6. FFB update
    IFFBSink* ffb = c.input_source_->GetFFBSink();
    if (!ffb) ffb = c.ffb_sink_;
    if (ffb)
    {
        ffb->Update(hvd, dt);
    }

    // 7. Extract vehicle state from HVD
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

    // 8. Body offset and attitude
    double body_dx = 0.0, body_dy = 0.0, body_dz = 0.0;
    c.physics_backend_->GetBodyPositionOffset(body_dx, body_dy, body_dz);

    double combined_pitch = 0.0, combined_roll = 0.0;
    c.physics_backend_->GetDynamicAttitude(combined_pitch, combined_roll);

    // 9. Sync to esmini gateway
    //    block_speed_update when longitudinal is scenario-controlled
    bool block_speed = !c.override_mgr_.IsLongitudinalManual();

    if (c.object_)
    {
        c.state_applier_.Apply(c.object_,
                               pos_x, pos_y, pos_z,
                               heading, speed, wheel_angle,
                               body_dx, body_dy, body_dz,
                               combined_pitch, combined_roll,
                               block_speed);

        // Feed resolved road Z back to physics for correct HVD export
        c.physics_backend_->SyncRoadZ(c.object_->pos_.GetZ());
    }

    // 10. Update OSI HostVehicleReporter
    c.current_hvd_ = hvd;
    if (c.object_)
    {
        GT_HostVehicleReporter::Instance().SetBaseHostVehicleData(c.object_->GetId(), hvd);
    }

    // 11. Update vehicle lights
    if (c.object_)
    {
        auto* vehicle = dynamic_cast<scenarioengine::Vehicle*>(c.object_);
        if (vehicle)
        {
            auto* ext = VehicleExtensionManager::Instance().GetExtension(vehicle);
            if (ext)
            {
                auto set_light = [&](VehicleLightType type, bool on) {
                    if (ext->IsScenarioControlled(type))
                        return;  // scenario has priority
                    LightState ls;
                    ls.mode = on ? LightState::Mode::ON : LightState::Mode::OFF;
                    ext->SetLightState(type, ls);
                    ext->SetLightSource(type, LightSource::MANUAL_DRIVE);
                };

                // Edge detection helper for toggles
                auto rising = [&](uint32_t bit) {
                    return (c.last_cmd_.buttons & bit) && !(c.prev_buttons_ & bit);
                };

                // Toggle lights on rising edge
                if (rising(ButtonBits::HEADLIGHT))  c.headlight_on_ = !c.headlight_on_;
                if (rising(ButtonBits::HIGH_BEAM))   c.high_beam_on_ = !c.high_beam_on_;
                if (rising(ButtonBits::FOG_LIGHT))   c.fog_light_on_ = !c.fog_light_on_;
                if (rising(ButtonBits::HAZARD))      c.hazard_on_    = !c.hazard_on_;

                // Indicator auto-cancel FSM
                auto ind = c.indicator_fsm_.Update(
                    c.last_cmd_.buttons, c.prev_buttons_,
                    c.last_cmd_.steering, c.prev_steering_,
                    c.config_.indicator_cancel_angle, c.hazard_on_);

                c.prev_buttons_  = c.last_cmd_.buttons;
                c.prev_steering_ = c.last_cmd_.steering;

                // Auto-controlled lights
                set_light(VehicleLightType::BRAKE_LIGHTS,     c.last_cmd_.brake > 0.05);
                set_light(VehicleLightType::REVERSING_LIGHTS,  c.last_cmd_.gear == -1);

                // Indicator (FSM output)
                set_light(VehicleLightType::INDICATOR_LEFT,  ind.left_on);
                set_light(VehicleLightType::INDICATOR_RIGHT, ind.right_on);

                // Toggle-controlled lights
                set_light(VehicleLightType::LOW_BEAM,        c.headlight_on_);
                set_light(VehicleLightType::HIGH_BEAM,       c.high_beam_on_);
                set_light(VehicleLightType::FOG_LIGHTS,      c.fog_light_on_);
                set_light(VehicleLightType::WARNING_LIGHTS,  c.hazard_on_);

                // R5-U3: advance the GT blink ticker (no-op unless a GT writer set FLASHING).
                c.light_sim_clock_ += dt;
                ext->Tick(c.light_sim_clock_, dt);
            }
        }
    }

    // 12. Base controller step
    c.scenarioengine::Controller::Step(dt);
}

} // namespace gt_esmini
