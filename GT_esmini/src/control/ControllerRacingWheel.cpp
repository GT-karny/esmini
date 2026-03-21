#include "gt_esmini/control/ControllerRacingWheel.hpp"
#include "gt_esmini/control/racingwheel/IInputSource.hpp"
#include "gt_esmini/control/racingwheel/IPhysicsBackend.hpp"
#include "gt_esmini/control/racingwheel/IFFBSink.hpp"
#include "gt_esmini/control/racingwheel/NullFFBSink.hpp"
#include "gt_esmini/control/racingwheel/StubInputSource.hpp"
#include "gt_esmini/control/racingwheel/RealVehicleBackend.hpp"
#ifdef GT_ENABLE_SDL2
#include "gt_esmini/control/racingwheel/SDL2WheelInput.hpp"
#endif
#include "gt_esmini/control/racingwheel/RacingWheelCoordinator.hpp"
#include "gt_esmini/core/ConfigLoader.hpp"
#include "gt_esmini/scenario/ExtraEntities.hpp"
#include "CommonMini.hpp"
#include "Entities.hpp"

namespace gt_esmini { extern std::string GetCurrentModuleDirectory(); }

namespace gt_esmini
{

ControllerRacingWheel::ControllerRacingWheel(InitArgs* args)
    : Controller(args)
    , input_source_(nullptr)
    , physics_backend_(nullptr)
    , ffb_sink_(nullptr)
    , coordinator_(nullptr)
{
    // Load config file
    std::string exe_dir = GetCurrentModuleDirectory();
    ConfigLoader loader;

    // Allow XOSC property to override config file name
    std::string config_filename = "racing_wheel.json";
    if (args && args->properties && args->properties->ValueExists("ConfigFile"))
    {
        config_filename = args->properties->GetValueStr("ConfigFile");
    }

    std::string config_path = loader.ResolveConfigPath(exe_dir, config_filename);
    if (!config_.LoadFromFile(config_path))
    {
        LOG_INFO("RacingWheelController: Config not found at {}, using defaults", config_path);
    }

    // Create input source
#ifdef GT_ENABLE_SDL2
    if (config_.input_type == "sdl2_wheel")
    {
        input_source_ = new SDL2WheelInput();
    }
    else
#endif
    if (config_.input_type == "network")
    {
        // NetworkInputBridge will be created in Phase 5
        LOG_INFO("RacingWheelController: Network input requested but not yet implemented, falling back to stub");
        input_source_ = new StubInputSource();
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
        // NetworkPhysicsBridge will be created in Phase 5
        LOG_INFO("RacingWheelController: Network physics requested but not yet implemented, using RealVehicle");
        physics_backend_ = new RealVehicleBackend();
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
    coordinator_ = new RacingWheelCoordinator();

    // Set default mode to ADDITIVE (same as RealDriverController)
    if (args && args->properties && !args->properties->ValueExists("mode"))
    {
        mode_ = ControlOperationMode::MODE_ADDITIVE;
    }

    LOG_INFO("RacingWheelController: Created (input={}, physics={})", config_.input_type, config_.physics_type);
}

ControllerRacingWheel::~ControllerRacingWheel()
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

void ControllerRacingWheel::Step(double timeStep)
{
    coordinator_->RunFrame(*this, timeStep);
}

int ControllerRacingWheel::Activate(const ControlActivationMode (&mode)[static_cast<unsigned int>(ControlDomains::COUNT)])
{
    LOG_INFO("RacingWheelController::Activate() called");

    if (object_)
    {
        // Initialize physics backend from scenario object state
        physics_backend_->Init(config_, object_);
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

        LOG_INFO("RacingWheelController: Activated for object {} at ({:.1f}, {:.1f})",
                 object_->GetId(), object_->pos_.GetX(), object_->pos_.GetY());
    }

    return Controller::Activate(mode);
}

void ControllerRacingWheel::GetInputsForOSI(double& throttle, double& brake, double& steering, int& gear) const
{
    throttle = last_cmd_.throttle;
    brake    = last_cmd_.brake;
    steering = last_cmd_.steering;
    gear     = last_cmd_.gear;
}

void ControllerRacingWheel::GetPowertrainForOSI(double& rpm, double& torque) const
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

scenarioengine::Controller* InstantiateControllerRacingWheel(void* args)
{
    auto* initArgs = static_cast<scenarioengine::Controller::InitArgs*>(args);
    return new ControllerRacingWheel(initArgs);
}

} // namespace gt_esmini
