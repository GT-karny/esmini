#include "gt_esmini/control/manualdrive/ManualDriveCoordinator.hpp"
#include "gt_esmini/control/ControllerManualDrive.hpp"
#include "gt_esmini/control/manualdrive/IInputSource.hpp"
#include "gt_esmini/control/common/IPhysicsBackend.hpp"
#include "gt_esmini/control/manualdrive/IFFBSink.hpp"
#include "gt_esmini/control/common/DomainOwnershipLedger.hpp"
#include "gt_esmini/control/virtualdriver/ITrafficPolicy.hpp"
#include "gt_esmini/osi/GT_HostVehicleReporter.hpp"
#include "gt_esmini/scenario/ExtraEntities.hpp"
#include "Entities.hpp"
#include "logger.hpp"

#include <cmath>

namespace gt_esmini
{

void ManualDriveCoordinator::RunFrame(ControllerManualDrive& c, double dt) const
{
    // req-vd-ad:REQ-AD-025/028, vd-func:FUNC-075 -- local simulation clock for
    // TrafficPolicyContext::sim_time (step 3a-adas below). Mirrors
    // ControllerVirtualDriver::Step's own `sim_time_ += timeStep;` (its FIRST
    // statement): sim_time is NOT actually an upstream scenarioengine::
    // Controller base-class member -- only object_/entities_/scenario_engine_
    // are (Controller.hpp) -- so ManualDrive keeps its own accumulator the
    // same way VD does, incremented unconditionally on every RunFrame() call
    // regardless of which branch below returns early.
    c.sim_time_ += dt;

    // 1. Poll input source
    InputFrame frame = c.input_source_->Poll(dt);

    // 2. Override judgment (domain-aware)
    c.override_mgr_.Update(frame, dt);

    // Publish raw controls before any AUTO delegation. A handover begins in
    // AUTO until ControllerManualDrive::Activate promotes it to MANUAL, and a
    // RESUME press must still be observable in either state.
    if (c.object_ && frame.pedal_steer)
    {
        auto& ledger = DomainOwnershipLedger::Instance();
        const int obj_id = c.object_->GetId();
        ledger.PublishDeviceAxis(obj_id, frame.pedal_steer->steering);
        ledger.PublishDeviceButtons(obj_id, frame.pedal_steer->buttons);
    }

    if (c.override_mgr_.JustPressedResume() && c.ResumeVirtualDriverControl())
    {
        // VD now owns the ledger. Returning here is essential: otherwise MD
        // can still integrate once after the ownership transfer.
        c.scenarioengine::Controller::Step(dt);
        return;
    }

    // feature:F7 — is a per-domain SPLIT in effect, i.e. do the two domains of
    // this object belong to two different controllers? Under a split this
    // controller must keep running even while fully AUTO, because the domain it
    // owns has no other source: the early return below skips the command build
    // AND the publish, so the peer integrator would find nothing on the bus.
    // With override.enabled=true (required for takeover to exist at all) both
    // domains start AUTO, so without this the reverse split loses its pedals on
    // frame 1.
    //
    // Deliberately narrow: a ManualDrive-only scenario has both domains on ONE
    // controller, so split_active is false and the AUTO-delegates-to-scenario
    // behaviour below is untouched.
    bool split_active = false;
    if (c.object_)
    {
        const auto& ledger = DomainOwnershipLedger::Instance();
        const int   id     = c.object_->GetId();
        const void* lat    = ledger.OwnerOf(id, OwnedDomain::LATERAL);
        const void* lon    = ledger.OwnerOf(id, OwnedDomain::LONGITUDINAL);
        split_active       = lat && lon && lat != lon;
    }

    // If neither domain is manual, delegate entirely to scenario
    if (!c.override_mgr_.IsAnyManual() && !split_active)
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

    // 3a-adas. req-vd-ad:REQ-AD-025/028, vd-func:FUNC-075 (design
    // manualdrive_adas_design.md §2-2) -- AdasCoexistenceStack arbitration.
    // MUST run HERE: after cmd is assembled, BEFORE the 3-bus publish block
    // immediately below. Do not move this.
    //
    // WHY HERE AND NOWHERE ELSE: the bus contract (3-bus block's own comment,
    // and DomainOwnershipLedger.hpp) is "publish what you own, consume what
    // you don't". If arbitration ran AFTER publish, a split configuration's
    // peer (the other domain's owner/integrator) would already have consumed
    // the PRE-arbitration value off the bus for this frame -- so under a
    // lat=manual/lon=VD-style split, an AEB intervention decided here would
    // never reach the channel that actually drives the vehicle; it would be
    // silently dropped. Arbitrating BEFORE publish means the owned
    // (longitudinal) channel this controller is about to publish always
    // carries the ADAS-arbitrated value, never the raw driver value.
    //
    // ctx.{ego,entities} come from the upstream scenarioengine::Controller
    // base class's own object_/entities_ members (Controller.hpp) -- the same
    // ones ControllerVirtualDriver reads for its own TrafficPolicyContext.
    // ctx.sim_time does NOT: it is ManualDrive's own accumulator (see this
    // function's top), because sim_time is NOT actually a Controller
    // base-class member despite manualdrive_adas_design.md §2-1 describing
    // all three as "upstream Controller基底クラスのメンバ" -- confirmed by
    // reading Controller.hpp; VirtualDriver's own sim_time_ is a
    // ControllerVirtualDriver member (ControllerVirtualDriver.hpp), not
    // inherited either. This did not change the hook's shape, only where
    // sim_time comes from.
    //
    // owns_longitudinal is read from the ledger HERE, once, and both used for
    // arbitration and CACHED (adas_last_owns_longitudinal_) for
    // GetADASFunctions() below -- see that method's own comment for why the
    // cached value (not a fresh re-read) is correct there.
    //
    // Runs unconditionally whenever c.object_ exists, i.e. also when this
    // controller turns out NOT to be the integrator (the `is_integrator`
    // check happens further down, AFTER this block and after 3-bus publish).
    // That is intentional, not an oversight: DomainOwnershipLedger::
    // IntegratorOf() prefers the LONGITUDINAL owner, falling back to the
    // LATERAL owner only when nobody owns LONGITUDINAL -- so the only way
    // this controller can be a NON-integrator is if it does NOT own
    // LONGITUDINAL (e.g. the reverse split, lat=manual/lon=VD). In that case
    // owns_longitudinal below reads false, and AdasCoexistenceStack::Step()'s
    // own domain-ownership bypass (AdasCoexistenceStack.hpp) makes this a
    // correct no-op for ARBITRATION while still evaluating the AEB/FCW
    // policies every frame -- which is exactly what that class's own header
    // asks for, to keep AebSafety's cross-frame encroachment debounce warm
    // across an ownership hand-off (design §12's dynamic-ownership risk item).
    //
    // Deliberately NOT run on the two early-return paths ABOVE this point in
    // the function (AUTO_RESUME hand-back to VirtualDriverControl, and the
    // fully-AUTO/no-split scenario-delegation return): both returns happen
    // before cmd is even assembled, so there is nothing yet to arbitrate, and
    // both represent this controller NOT being the human driver this frame
    // (design §1's scope is explicitly "human stays the primary driver" --
    // full delegation to the scenario/story is out of scope for ADAS
    // coexistence, and AUTO_RESUME's whole point is that longitudinal
    // ownership has already been handed back to VirtualDriverController by
    // the time that return executes). A consequence worth stating plainly:
    // AebSafety's cross-frame debounce state goes cold while this controller
    // is not driving at all (full AUTO) and re-warms over the following
    // few frames once manual driving resumes -- accepted for phase A, not a
    // defect, since that window is not a "manually driving but ADAS silently
    // skipped" case.
    if (c.object_)
    {
        auto&      ledger           = DomainOwnershipLedger::Instance();
        const int  obj_id           = c.object_->GetId();
        const bool owns_longitudinal = ledger.IsOwner(obj_id, &c, OwnedDomain::LONGITUDINAL);
        // req-vd-ad:REQ-AD-027 (phase D). Read here, once, from the SAME ledger
        // call shape as the longitudinal flag, and cached below for
        // GetADASFunctions() -- see AdasCoexistenceStack.hpp's DOMAIN OWNERSHIP
        // block for why the two domains need separate flags rather than one.
        const bool owns_lateral      = ledger.IsOwner(obj_id, &c, OwnedDomain::LATERAL);

        TrafficPolicyContext pctx;
        pctx.ego      = c.object_;
        pctx.entities = c.entities_;
        pctx.sim_time = c.sim_time_;

        // req-vd-ad:REQ-AD-026 段e/g/h + REQ-AD-030 (phase C) -- the two
        // per-frame inputs the stack cannot derive from the policy snapshots.
        //
        // GetSpeedLimit() is the SAME route the VD overtake ceiling uses
        // (ControllerVirtualDriver.cpp's respect_speed_limit branch,
        // req-vd-ad:REQ-AD-023), which is what REQ-AD-026 段g's note asks for.
        // It is read here, not inside the stack, because the stack takes only
        // a TrafficPolicyContext and must not start reaching into Position.
        //
        // `cmd.buttons` (not frame.pedal_steer->buttons) so the mask the ADAS
        // stalk sees is the same one every other consumer this frame sees --
        // cmd is what the domain-zeroing above produced and what the bus is
        // about to carry.
        ManualAdasEnvironment env;
        env.speed_limit_mps = c.object_->pos_.GetSpeedLimit();
        env.buttons         = cmd.buttons;

        // req-vd-ad:REQ-AD-027 (phase D) -- the LATERAL environment. THIS IS
        // THE ONE PLACE IN THE CODEBASE WHERE roadmanager's road-t sign
        // convention is converted to LaneKeepAssist's vehicle-left-positive
        // one; see LaneKeepAssist.hpp's SIGN CHAIN block, and do not repeat the
        // conversion anywhere downstream.
        //
        //   * GetRoadLaneInfo() supplies laneOffset (lane-relative, and the
        //     ONLY lane-relative offset esmini exposes) plus the current lane's
        //     WIDTH in one call, so the half-width and the offset can never
        //     come from two different s-values.
        //   * The road-t frame is +t = road-left along increasing s, which is
        //     vehicle-RIGHT for a vehicle driving against s. Multiplying by
        //     GetDrivingDirectionRelativeRoad()'s sign collapses all four cases
        //     (RHT/LHT x with/against s) onto "+ = vehicle-left" -- the same
        //     conversion, for the same reason, that AutoLightController.cpp
        //     already performs on the same quantity.
        //   * The lateral speed is taken from the HEADING, not by
        //     differentiating the offset. Differentiating would produce a huge
        //     spurious spike on the frame a lane boundary is crossed, because
        //     GetOffset() RE-REFERENCES to the new lane there -- and a lane
        //     boundary crossing is exactly the event this assist exists to
        //     prevent, i.e. the instrument would blow up precisely where it
        //     matters most. GetHRelativeDrivingDirection() already accounts for
        //     the lane sign, so speed * sin(h_rel) is vehicle-left-positive
        //     with no further conversion (the same v*sin(dh) form
        //     ControllerVirtualDriver uses to arm its resume-merge profile).
        //   * lane_valid is FALSE, and every geometric field left at zero, when
        //     the road/lane/width cannot be resolved. A fabricated 0.0 offset
        //     would read as a perfectly centred vehicle.
        {
            roadmanager::RoadLaneInfo lane_info;
            const bool                lane_ok =
                c.object_->pos_.GetRoadLaneInfo(&lane_info) == roadmanager::Position::ReturnCode::OK &&
                lane_info.width > 1e-6;
            if (lane_ok)
            {
                const double sign_drive =
                    (c.object_->pos_.GetDrivingDirectionRelativeRoad() < 0) ? -1.0 : 1.0;
                env.lane_valid           = true;
                env.lane_offset_m        = lane_info.laneOffset * sign_drive;
                env.lane_half_width_m    = lane_info.width * 0.5;
                env.vehicle_half_width_m = c.object_->boundingbox_.dimensions_.width_ * 0.5;
                env.lateral_speed_mps =
                    c.object_->GetSpeed() * std::sin(c.object_->pos_.GetHRelativeDrivingDirection());
                env.lane_id = lane_info.laneId;
            }
            // The indicator LAMP, from the FSM that owns it. Read here means it
            // is LAST frame's state: the FSM is advanced in step 11 below,
            // after this hook. That one-frame lag is accepted rather than
            // engineered away -- at dt=0.05 an indicator that has just come on
            // suppresses the assist 50 ms later, which is two orders of
            // magnitude shorter than any lane departure, and moving the FSM
            // update earlier would reorder a block that also owns
            // prev_buttons_/prev_steering_ for the light toggles.
            env.indicator_active = c.indicator_fsm_.left != IndicatorFSM::State::OFF ||
                                   c.indicator_fsm_.right != IndicatorFSM::State::OFF;
        }

        ManualAdasFrameResult adas_result =
            c.adas_stack_->Step(pctx, owns_longitudinal, owns_lateral, cmd, env, dt);

        // Apply the arbitrated commands back into cmd BEFORE the publish block
        // below. Phase A-C were longitudinal-only; phase D adds the steering,
        // and it is assigned UNCONDITIONALLY: on any frame the assist did not
        // run, lka.steer_out is the driver's own steering passed through
        // unchanged, so a branch here would only add a way for the two paths to
        // disagree.
        cmd.throttle = adas_result.pedals.throttle_out;
        cmd.brake    = adas_result.pedals.brake_out;
        cmd.steering = adas_result.lka.steer_out;

        // Cache for GetADASFunctions(), called by GT_Step AFTER this Step()
        // returns -- see that method's header comment for why "this frame".
        c.adas_last_result_            = adas_result;
        c.adas_last_owns_longitudinal_ = owns_longitudinal;
        c.adas_last_owns_lateral_      = owns_lateral;
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
            ledger.PublishLateral(obj_id, &c, cmd.steering, c.override_mgr_.IsLateralManual());
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
                bool   owner_manual   = false;
                if (ledger.ConsumeLateral(obj_id, owner_steering, owner_manual))
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

    // 6a. feature:F7 — route the AD's steering to the servo when SOMEONE ELSE
    // owns the lateral domain.
    //
    // The servo must track the LATERAL OWNER's command. Who physically holds the
    // device is a separate question, and in the split configurations the two are
    // different: VirtualDriver owns lateral but runs input_type=stub, whose
    // GetFFBSink() is nullptr, so VD has no sink to drive. ManualDrive is the one
    // holding the wheel. Without this hop the target is simply never set —
    // measured on the real G29: target_track enabled=true in the log, and the
    // target-track force component |tt| = 0.0000 for the entire run.
    //
    // Two independent reasons it was dead, both of which this fixes by routing
    // through the DEVICE HOLDER instead of the lateral owner:
    //   1. VD is the non-integrator in that configuration, so its Step returns
    //      before it would set the target at all (S2 output gate); worse, the
    //      falling edge actively sets SetSteerTarget(0.0, false) and nothing
    //      ever sets it again.
    //   2. Even reaching that line, VD's ffb is nullptr (stub input).
    // Fixing only the gate would therefore NOT have fixed this.
    //
    // NOTE this is why the forward split never showed the problem: not because
    // the lateral owner and the device holder coincided there (they did not —
    // VD was still sinkless), but because that configuration ships the servo
    // DISABLED, so nobody was looking at it.
    //
    // Deliberately additive: the branch where ManualDrive DOES own lateral is
    // left untouched, so single-controller ManualDrive scenarios (where the
    // human steers and target-track drives the takeover detector) keep their
    // existing behaviour bit for bit.
    //
    // req-vd-ad:REQ-AD-027 (phase D) -- WHY THE LKA CORRECTION CANNOT LAND ON
    // THE WHEEL TWICE. This block sets the servo target from the LATERAL OWNER
    // and runs only under `!IsOwner(..., LATERAL)`. The LKA correction is
    // applied to cmd.steering in the 3a-adas hook above and runs only under
    // owns_lateral. The two conditions are exact complements, so no frame can
    // both add a correction to the command AND push the physical wheel toward
    // it as a servo target -- which would be the same intervention applied
    // through two paths at once, felt as double authority on a real G29.
    // ManualLkaArbitrates() (LaneKeepAssist.hpp) is the shared predicate, and
    // LkaCorrectionAndFfbPeerRoutingAreMutuallyExclusive pins the complement
    // against this branch's own condition over every combination. Making this
    // hop unconditional would break that invariant.
    if (ffb && c.object_)
    {
        auto&     ledger = DomainOwnershipLedger::Instance();
        const int obj_id = c.object_->GetId();
        if (!ledger.IsOwner(obj_id, &c, OwnedDomain::LATERAL))
        {
            double owner_steering = 0.0;
            bool   owner_manual   = false;
            if (ledger.ConsumeLateral(obj_id, owner_steering, owner_manual))
            {
                // feature:F7 RETURN PATH — the servo must release the wheel
                // once the lateral owner has latched MANUAL, exactly like the
                // single-controller case gates on !lat_manual (see
                // ControllerVirtualDriver.cpp 5a). Before this, active was
                // hardcoded true regardless of owner_manual: the servo never
                // went inert in the split, so it kept commanding force toward
                // whatever the owner published (0 while its own frame was
                // stub) instead of releasing — fighting the driver at the
                // exact moment they took over, and never letting the latch
                // self-perpetuate the way OverrideManager's design assumes
                // (GetInterventionSample().active must go false to stop
                // feeding the detector).
                ffb->SetSteerTarget(owner_steering, /*active=*/!owner_manual);
                LOG_DEBUG("ManualDriveController[{}]: servo target <- lateral owner {} = {:.5f} (manual={})",
                          c.GetName(),
                          ledger.OwnerName(obj_id, OwnedDomain::LATERAL),
                          owner_steering,
                          owner_manual);
            }
        }
    }

    if (ffb)
    {
        ffb->Update(hvd, dt);

        // 6b. feature:F7 RETURN PATH — publish the servo's intervention sample
        // for the lateral owner's detector. Published AFTER Update() so it is
        // this frame's force, not last frame's. The lateral owner cannot read it
        // itself: it has no sink (see DomainOwnershipLedger's return-path note).
        if (c.object_)
        {
            DomainOwnershipLedger::Instance().PublishInterventionSample(
                c.object_->GetId(), ffb->GetInterventionSample());
        }
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
