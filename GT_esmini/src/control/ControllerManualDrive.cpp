#include "gt_esmini/control/ControllerManualDrive.hpp"
#include "gt_esmini/control/common/ModuleDirectory.hpp"
#include "gt_esmini/control/manualdrive/IInputSource.hpp"
#include "gt_esmini/control/common/IPhysicsBackend.hpp"
#include "gt_esmini/control/manualdrive/IFFBSink.hpp"
#include "gt_esmini/control/manualdrive/NullFFBSink.hpp"
#include "gt_esmini/control/manualdrive/StubInputSource.hpp"
#include "gt_esmini/control/common/RealVehicleBackend.hpp"
#include "gt_esmini/control/manualdrive/NetworkInputBridge.hpp"
#include "gt_esmini/control/manualdrive/NetworkPhysicsBridge.hpp"
#ifdef GT_ENABLE_SDL2
#include "gt_esmini/control/manualdrive/SDL2WheelInput.hpp"
#include "gt_esmini/control/manualdrive/SDL2KeyboardInput.hpp"
#endif
#include "gt_esmini/control/manualdrive/ManualDriveCoordinator.hpp"
#include "gt_esmini/control/common/DomainOwnershipLedger.hpp"
#include "gt_esmini/core/ConfigLoader.hpp"
#include "gt_esmini/scenario/ExtraEntities.hpp"
#include "CommonMini.hpp"
#include "Entities.hpp"

namespace gt_esmini
{

ControllerManualDrive::ControllerManualDrive(InitArgs* args)
    : Controller(args)
    , input_source_(nullptr)
    , physics_backend_(nullptr)
    , ffb_sink_(nullptr)
    , coordinator_(nullptr)
{
    // Load config file
    std::string exe_dir = GetCurrentModuleDirectory();
    ConfigLoader loader;

    // Allow XOSC property to override config file name or provide absolute path
    std::string config_filename = "manual_drive.json";
    if (args && args->properties && args->properties->ValueExists("ConfigFile"))
    {
        config_filename = args->properties->GetValueStr("ConfigFile");
    }

    // If absolute path, use directly; otherwise resolve relative to exe_dir/config/
    std::string config_path;
    if (!config_filename.empty() && (config_filename[0] == '/' || (config_filename.size() > 1 && config_filename[1] == ':')))
    {
        config_path = config_filename;
    }
    else
    {
        config_path = loader.ResolveConfigPath(exe_dir, config_filename);
    }
    if (!config_.LoadFromFile(config_path))
    {
        LOG_INFO("ManualDriveController: Config not found at {}, using defaults", config_path);
    }

    // Create input source
#ifdef GT_ENABLE_SDL2
    if (config_.input_type == "sdl2_wheel")
    {
        input_source_ = new SDL2WheelInput();
    }
    else if (config_.input_type == "sdl2_keyboard")
    {
        input_source_ = new SDL2KeyboardInput();
    }
    else
#endif
    if (config_.input_type == "network")
    {
        input_source_ = new NetworkInputBridge();
    }
    else
    {
        input_source_ = new StubInputSource();
    }

    // Create physics backend
    if (config_.physics_type == "real_vehicle")
    {
        physics_backend_ = new RealVehicleBackend();
    }
    else if (config_.physics_type == "network")
    {
        physics_backend_ = new NetworkPhysicsBridge();
    }
    else
    {
        physics_backend_ = new RealVehicleBackend();
    }

    // Create FFB sink
    // For SDL2 wheel, FFB comes from the input source's GetFFBSink()
    // For all other cases, use NullFFBSink as the owned sink
    ffb_sink_ = new NullFFBSink();

    // Configure override manager
    override_mgr_.Configure(config_);

    // Create coordinator
    coordinator_ = new ManualDriveCoordinator();

    // Set default mode to ADDITIVE (same as RealDriverController)
    if (args && args->properties && !args->properties->ValueExists("mode"))
    {
        mode_ = ControlOperationMode::MODE_ADDITIVE;
    }

    LOG_INFO("ManualDriveController: Created (input={}, physics={})", config_.input_type, config_.physics_type);
}

ControllerManualDrive::~ControllerManualDrive()
{
    if (input_source_)
    {
        input_source_->Shutdown();
        delete input_source_;
    }
    delete physics_backend_;
    delete ffb_sink_;
    delete coordinator_;
}

void ControllerManualDrive::Step(double timeStep)
{
    // feature:F7 — per-frame ownership trace. active_mask is what this controller
    // believes about itself; the ledger is what GT has arbitrated. The two
    // disagreeing is the visible symptom of upstream's per-domain deactivation
    // defect, so both are printed side by side rather than just the verdict.
    if (object_)
    {
        LOG_DEBUG("ManualDriveController[{}]: ownership {} (self active_mask=0x{:x})",
                  GetName(),
                  DomainOwnershipLedger::Instance().Describe(object_->GetId()),
                  GetActiveDomains());
    }

    coordinator_->RunFrame(*this, timeStep);
}

void ControllerManualDrive::DeactivateDomains(unsigned int domains)
{
    Controller::DeactivateDomains(domains);
    if (object_)
    {
        // Re-assert against whatever the base left us holding. Claim() releases a
        // domain only when this controller is its recorded owner, so a domain that
        // upstream took from us and handed to a peer is not clawed back here.
        DomainOwnershipLedger::Instance().Claim(object_->GetId(), this, GetName(), GetActiveDomains());
    }
}

void ControllerManualDrive::Deactivate()
{
    // Release FFB before scenario teardown so the wheel isn't left under torque
    IFFBSink* ffb = input_source_ ? input_source_->GetFFBSink() : nullptr;
    if (!ffb) ffb = ffb_sink_;
    if (ffb)
    {
        ffb->SetEnabled(false);
    }

    if (object_)
    {
        DomainOwnershipLedger::Instance().ReleaseAll(object_->GetId(), this);
    }

    LOG_INFO("ManualDriveController: Deactivated — FFB released");
    Controller::Deactivate();
}

int ControllerManualDrive::Activate(const ControlActivationMode (&mode)[static_cast<unsigned int>(ControlDomains::COUNT)])
{
    LOG_INFO("ManualDriveController::Activate() called");

    if (object_)
    {
        // Initialize physics backend from scenario object state.
        // Translate ManualDriveConfig → backend-agnostic PhysicsInitParams so the
        // backend (shared with VirtualDriver) stays decoupled from this config schema.
        PhysicsInitParams phys_params;
        phys_params.vehicle_params_file   = config_.real_vehicle.vehicle_params_file;
        phys_params.network_transport_type = config_.physics_network.transport_type;
        phys_params.network_host           = config_.physics_network.host;
        phys_params.network_cmd_port       = config_.physics_network.cmd_port;
        phys_params.network_state_port     = config_.physics_network.state_port;
        physics_backend_->Init(phys_params, object_);
        physics_backend_->SetInitialState(
            object_->pos_.GetX(),
            object_->pos_.GetY(),
            object_->pos_.GetZ(),
            object_->pos_.GetH(),
            object_->GetSpeed());

        // Initialize input source
        input_source_->Init(config_);

        // Register VehicleLightExtension (same pattern as RealDriverController)
        auto* vehicle = dynamic_cast<scenarioengine::Vehicle*>(object_);
        if (vehicle)
        {
            auto* ext = VehicleExtensionManager::Instance().GetExtension(vehicle);
            if (!ext)
            {
                ext = new VehicleLightExtension(vehicle);
                VehicleExtensionManager::Instance().RegisterExtension(vehicle, ext);
            }
        }

        LOG_INFO("ManualDriveController: Activated for object {} at ({:.1f}, {:.1f})",
                 object_->GetId(), object_->pos_.GetX(), object_->pos_.GetY());
    }

    const int rc = Controller::Activate(mode);

    // feature:F7 — record the claim only after the base has resolved `mode` into
    // the actual bitmask. Reading the requested mode instead would mis-record any
    // domain the base refused (a domain outside operating_domains_ is silently
    // not granted), and the ledger must describe what was granted, not what was
    // asked for.
    if (object_)
    {
        DomainOwnershipLedger::Instance().Claim(object_->GetId(), this, GetName(), GetActiveDomains());
        LOG_INFO("ManualDriveController[{}]: ownership after activate — {}",
                 GetName(), DomainOwnershipLedger::Instance().Describe(object_->GetId()));

        // feature:F7 S2 — seed the integrator edge here so Step() does NOT treat
        // the first frame after activation as a take-over. SetInitialState above
        // has already seeded the backend from the object pose; letting Step()
        // resync as well would read object_->pos_ a second time, and by then the
        // scenario may have advanced it for this frame — which the backend would
        // then integrate on top of, moving the vehicle twice in one frame.
        // (Measured: a clean 2x position step at the handover instant, ratio 1.05.)
        was_domain_integrator_ = DomainOwnershipLedger::Instance().IsIntegrator(object_->GetId(), this);
    }

    return rc;
}

void ControllerManualDrive::GetInputsForOSI(double& throttle, double& brake, double& steering, int& gear, int& lightMask) const
{
    throttle  = last_cmd_.throttle;
    brake     = last_cmd_.brake;
    // Use the actual wheel angle from physics (stored in current_hvd_ by RealVehicleBackend)
    // rather than raw input, so the sign matches OSI convention (positive = left).
    if (current_hvd_.has_vehicle_steering() && current_hvd_.vehicle_steering().has_vehicle_steering_wheel())
    {
        steering = current_hvd_.vehicle_steering().vehicle_steering_wheel().angle();
    }
    else
    {
        steering = 0.0;
    }
    // Gear comes from the actual AT/drivetrain state, not the raw input.
    if (current_hvd_.has_vehicle_powertrain())
    {
        gear = current_hvd_.vehicle_powertrain().gear_transmission();
    }
    else
    {
        gear = last_cmd_.gear;
    }
    lightMask = BuildLightMaskFromExtension();
}

int ControllerManualDrive::BuildLightMaskFromExtension() const
{
    if (!object_ || object_->type_ != scenarioengine::Object::Type::VEHICLE)
        return 0;

    // R5-U3: read straight from native storage via the bridge (no extension needed).
    auto is_on = [&](VehicleLightType type) {
        return ReadLight(object_, type).mode == LightState::Mode::ON;
    };

    int mask = 0;
    if (is_on(VehicleLightType::LOW_BEAM))        mask |= 1;
    if (is_on(VehicleLightType::HIGH_BEAM))       mask |= 2;
    if (is_on(VehicleLightType::INDICATOR_LEFT))   mask |= 4;
    if (is_on(VehicleLightType::INDICATOR_RIGHT))  mask |= 8;
    if (is_on(VehicleLightType::FOG_LIGHTS) ||
        is_on(VehicleLightType::FOG_LIGHTS_FRONT) ||
        is_on(VehicleLightType::FOG_LIGHTS_REAR))  mask |= 16;
    return mask;
}

void ControllerManualDrive::GetPowertrainForOSI(double& rpm, double& torque) const
{
    if (current_hvd_.has_vehicle_powertrain() && current_hvd_.vehicle_powertrain().motor_size() > 0)
    {
        rpm    = current_hvd_.vehicle_powertrain().motor(0).rpm();
        torque = current_hvd_.vehicle_powertrain().motor(0).torque();
    }
    else
    {
        rpm    = 0.0;
        torque = 0.0;
    }
}

scenarioengine::Controller* InstantiateControllerManualDrive(void* args)
{
    auto* initArgs = static_cast<scenarioengine::Controller::InitArgs*>(args);
    return new ControllerManualDrive(initArgs);
}

} // namespace gt_esmini
