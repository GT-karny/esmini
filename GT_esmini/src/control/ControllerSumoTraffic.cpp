/*
 * GT_esmini - Extended esmini
 *
 * feature:F9 -- SUMO background traffic (experimental, default OFF).
 *
 * GT-side counterpart of upstream scenarioengine::ControllerSumo, which stays
 * untouched (R1 clean core). What differs, and why, is in the header and in
 * GT_esmini/docs/features/sumo_background_traffic.md.
 */

#include "gt_esmini/control/ControllerSumoTraffic.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

// PositionVector first: libsumo/Vehicle.h declares storeShape(..., PositionVector&)
// without forward-declaring it. Upstream's ControllerSumo.cpp has the same line
// for the same reason.
#include <utils/geom/PositionVector.h>

#include <libsumo/Simulation.h>
#include <libsumo/TraCIDefs.h>
#include <libsumo/Vehicle.h>

#include "CommonMini.hpp"
#include "ControllerSumo.hpp"  // SUMOVClass2OSCVehicleCategory (read-only reuse)
#include "Entities.hpp"
#include "ScenarioEngine.hpp"
#include "gt_esmini/common/SimpleJson.hpp"
#include "gt_esmini/core/ConfigLoader.hpp"
#include "gt_esmini/control/common/ModuleDirectory.hpp"
#include "logger.hpp"

using namespace scenarioengine;

namespace
{
// libsumo is a process-global singleton: Simulation::load() replaces whatever
// was loaded before. Two SUMO controllers in one scenario (two GT ones, or a GT
// one next to upstream's) would silently fight over it, so the second instance
// stays inert and says so instead.
bool g_sumo_simulation_owned = false;

// Below this per-step displacement the direction of travel is noise, so the
// heading override keeps the last good value instead. 0.05 m is one step at
// 1 m/s with the default 0.05 s step length.
constexpr double kMinHeadingDisplacement = 0.05;
}  // namespace

scenarioengine::Controller* gt_esmini::InstantiateControllerSumoTraffic(void* args)
{
    Controller::InitArgs* initArgs = static_cast<Controller::InitArgs*>(args);
    return new gt_esmini::ControllerSumoTraffic(initArgs);
}

gt_esmini::ControllerSumoTraffic::ControllerSumoTraffic(InitArgs* args) : Controller(args)
{
    // Like upstream's SUMO controller this one only ever overrides: it writes
    // the pose of vehicles that SUMO owns.
    if (mode_ != ControlOperationMode::MODE_OVERRIDE)
    {
        LOG_WARN("GTSumoTrafficController: mode \"{}\" not applicable, using override", Mode2Str(mode_));
        mode_ = ControlOperationMode::MODE_OVERRIDE;
    }

    ConfigLoader loader;
    std::string  config_filename = "sumo_traffic.json";
    if (args != nullptr && args->properties != nullptr && args->properties->ValueExists("ConfigFile"))
    {
        config_filename = args->properties->GetValueStr("ConfigFile");
    }
    LoadConfig(loader.ResolveConfigPathOrPassthrough(GetCurrentModuleDirectory(), config_filename));

    if (args != nullptr && args->properties != nullptr)
    {
        ApplyProperties(args->properties);

        // The scenario's own <File filepath> wins over the config default: the
        // .sumocfg belongs to the scenario, the knobs belong to the install.
        if (!args->properties->file_.filepath_.empty())
        {
            config_.sumocfg = args->properties->file_.filepath_;
        }
    }

    if (!config_.enabled)
    {
        LOG_INFO("GTSumoTrafficController: disabled (feature:F9 is experimental, enabled defaults to false). No SUMO traffic.");
        return;
    }

    if (config_.sumocfg.empty())
    {
        LOG_ERROR("GTSumoTrafficController: no .sumocfg given (neither <File filepath> nor config \"sumocfg\"). No SUMO traffic.");
        return;
    }

    if (g_sumo_simulation_owned)
    {
        LOG_ERROR("GTSumoTrafficController: a SUMO simulation is already loaded in this process (libsumo is a singleton). Staying inert.");
        return;
    }

    if (!ReadNetOffset(config_.sumocfg))
    {
        return;
    }

    // Same category weights upstream uses when picking a 3D model per vClass.
    std::vector<std::pair<int, double>> categories = {{Vehicle::Category::CAR, 5.0},
                                                      {Vehicle::Category::VAN, 2.0},
                                                      {Vehicle::Category::BUS, 1.0},
                                                      {Vehicle::Category::TRUCK, 2.0},
                                                      {Vehicle::Category::TRAILER, 0.0},  // allow trailers, but no single trailers
                                                      {Vehicle::Category::MOTORBIKE, 1.0}};
    vehicle_pool_.Initialize(scenario_engine_->GetScenarioReader(), &categories, false);

    loaded_ = LoadSimulation(config_.sumocfg);
}

gt_esmini::ControllerSumoTraffic::~ControllerSumoTraffic()
{
    if (loaded_)
    {
        try
        {
            libsumo::Simulation::close();
        }
        catch (const libsumo::TraCIException& e)
        {
            LOG_WARN("GTSumoTrafficController: SUMO close failed: {}", e.what());
        }
        loaded_                 = false;
        g_sumo_simulation_owned = false;
    }

    // The template is NOT deleted here. Unlike upstream's -- which ScenarioReader
    // never hands to Entities, leaving the controller as its owner -- a
    // user-range controller's host object goes through addObject() and is owned
    // by Entities (its destructor deletes object_ and object_pool_ alike).
    // Deleting it here would be a double free.
    template_vehicle_ = nullptr;
}

void gt_esmini::ControllerSumoTraffic::LoadConfig(const std::string& config_path)
{
    simplejson::Value root;
    std::string       error;
    if (!simplejson::LoadFile(config_path, root, &error))
    {
        LOG_INFO("GTSumoTrafficController: config not read at {} ({}), using defaults", config_path, error);
        return;
    }

    root.GetBool("enabled", config_.enabled);
    root.GetString("sumocfg", config_.sumocfg);
    root.GetInt("seed", config_.seed);
    root.GetDouble("step_length", config_.step_length);
    root.GetBool("inject_ego", config_.inject_ego);
    root.GetBool("override_heading", config_.override_heading);
    root.GetInt("speed_mode", config_.speed_mode);
}

void gt_esmini::ControllerSumoTraffic::ApplyProperties(OSCProperties* properties)
{
    auto read_bool = [properties](const char* key, bool& out)
    {
        if (properties->ValueExists(key))
        {
            const std::string value = properties->GetValueStr(key);
            out                     = (value == "true" || value == "1");
        }
    };

    // A scenario that asks for background traffic says so itself; the JSON knob
    // is the install-wide default (and the kill switch).
    read_bool("enabled", config_.enabled);
    read_bool("injectEgo", config_.inject_ego);
    read_bool("overrideHeading", config_.override_heading);
    if (properties->ValueExists("seed"))
    {
        config_.seed = static_cast<int>(strtod(properties->GetValueStr("seed")));
    }
    if (properties->ValueExists("speedMode"))
    {
        config_.speed_mode = static_cast<int>(strtod(properties->GetValueStr("speedMode")));
    }
    if (properties->ValueExists("stepLength"))
    {
        config_.step_length = strtod(properties->GetValueStr("stepLength"));
    }
    if (properties->ValueExists("overrideVehicleScaleMode"))
    {
        const std::string scale_str = properties->GetValueStr("overrideVehicleScaleMode");
        if (scale_str == "None")
        {
            scale_mode_ = EntityScaleMode::NONE;
        }
        else if (scale_str == "BBToModel")
        {
            scale_mode_ = EntityScaleMode::BB_TO_MODEL;
        }
        else if (scale_str == "ModelToBB")
        {
            scale_mode_ = EntityScaleMode::MODEL_TO_BB;
        }
        else if (scale_str != "UseVehicle")
        {
            LOG_ERROR("GTSumoTrafficController: unrecognized scale mode {}, ignoring", scale_str);
        }
    }
}

bool gt_esmini::ControllerSumoTraffic::ReadNetOffset(const std::string& sumocfg_path)
{
    if (doc_sumo_.load_file(sumocfg_path.c_str()).status != pugi::status_ok)
    {
        LOG_ERROR("GTSumoTrafficController: failed to load SUMO config file {}", sumocfg_path);
        return false;
    }

    // <net-file value="..."> is relative to the .sumocfg; try it as given first
    // (relative to CWD) and then relative to the config, exactly as upstream.
    std::vector<std::string> candidates;
    candidates.push_back(doc_sumo_.child("configuration").child("input").child("net-file").attribute("value").value());
    candidates.push_back(CombineDirectoryPathAndFilepath(DirNameOf(sumocfg_path), candidates[0]));

    pugi::xml_parse_result net_result;
    bool                   net_loaded = false;
    for (const std::string& candidate : candidates)
    {
        net_result = doc_sumo_.load_file(candidate.c_str());
        if (net_result.status == pugi::status_ok)
        {
            net_loaded = true;
            break;
        }
    }
    if (!net_loaded)
    {
        LOG_ERROR("GTSumoTrafficController: failed to load SUMO net file {}", candidates[0]);
        return false;
    }

    const std::string netoffset = doc_sumo_.child("net").child("location").attribute("netOffset").value();
    const std::size_t delim     = netoffset.find(',');
    if (delim == std::string::npos)
    {
        LOG_ERROR("GTSumoTrafficController: unreadable netOffset \"{}\" in {}", netoffset, candidates[0]);
        return false;
    }
    try
    {
        net_offset_.x = std::stod(netoffset.substr(0, delim));
        net_offset_.y = std::stod(netoffset.substr(delim + 1));
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("GTSumoTrafficController: unreadable netOffset \"{}\" ({})", netoffset, e.what());
        return false;
    }

    return true;
}

bool gt_esmini::ControllerSumoTraffic::LoadSimulation(const std::string& sumocfg_path)
{
    // Note the "-c <path>" single-token form: that is what upstream passes and
    // what this SUMO build is known to accept.
    std::vector<std::string> options;
    options.push_back("-c " + sumocfg_path);
    options.push_back("--xml-validation");
    options.push_back("never");

    // Determinism (design doc section 4). Use case (b) -- VD verification with
    // background traffic -- cannot have a frozen baseline without this.
    if (config_.seed > 0)
    {
        options.push_back("--seed");
        options.push_back(std::to_string(config_.seed));
    }
    if (config_.step_length > 0.0)
    {
        options.push_back("--step-length");
        options.push_back(std::to_string(config_.step_length));
    }

    try
    {
        libsumo::Simulation::load(options);
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("GTSumoTrafficController: SUMO load failed: {}", e.what());
        return false;
    }

    if (!libsumo::Simulation::isLoaded())
    {
        LOG_ERROR("GTSumoTrafficController: failed to load SUMO simulation from {}", sumocfg_path);
        return false;
    }

    g_sumo_simulation_owned = true;
    LOG_INFO("GTSumoTrafficController: SUMO loaded from {} (seed {}, speed_mode {})", sumocfg_path, config_.seed, config_.speed_mode);
    return true;
}

void gt_esmini::ControllerSumoTraffic::Init()
{
    // The host ScenarioObject is a template, not a participant. Upstream gets
    // this from ScenarioReader's CONTROLLER_TYPE_SUMO special case; a user-range
    // controller type does not, so take the object out of the scene here --
    // deactivate rather than remove, because removeObject() deletes it and this
    // object's 3D model is the fallback for spawned vehicles.
    //
    // Usually there is nothing to do: an entity is only activated by having Init
    // private actions, and a controller host has none. The call is here for the
    // scenario that does give it a TeleportAction, which would otherwise leave a
    // parked phantom car in the scene.
    if (object_ != nullptr && entities_ != nullptr)
    {
        template_vehicle_ = object_;
        if (object_->IsActive())
        {
            LOG_INFO("GTSumoTrafficController: host object {} is a template, taking it out of the scene", object_->GetName());
            entities_->deactivateObject(object_);
        }
    }

    if (!loaded_)
    {
        return;  // inert: never activate, so the engine never steps it
    }

    // Same reason the reader activates upstream's SUMO controller explicitly:
    // this controller drives SUMO-owned vehicles, so it is never the target of
    // an ActivateControllerAction and would otherwise never be stepped.
    Activate({ControlActivationMode::ON, ControlActivationMode::ON, ControlActivationMode::OFF, ControlActivationMode::OFF});
}

int gt_esmini::ControllerSumoTraffic::Activate(const ControlActivationMode (&mode)[static_cast<unsigned int>(ControlDomains::COUNT)])
{
    time_ = 0.0;

    if (mode[static_cast<unsigned int>(ControlDomains::DOMAIN_LAT)] != ControlActivationMode::ON ||
        mode[static_cast<unsigned int>(ControlDomains::DOMAIN_LONG)] != ControlActivationMode::ON)
    {
        LOG_INFO("GTSumoTrafficController: forced into operation of both domains (lat/long)");
    }

    return Controller::Activate({ControlActivationMode::ON, ControlActivationMode::ON, ControlActivationMode::OFF, ControlActivationMode::OFF});
}

void gt_esmini::ControllerSumoTraffic::Step(double timeStep)
{
    if (!loaded_)
    {
        Controller::Step(timeStep);
        return;
    }

    time_ = scenario_engine_->getSimulationTime();

    try
    {
        libsumo::Simulation::step(time_);

        SpawnDepartedVehicles();
        RemoveArrivedVehicles();
        if (config_.inject_ego)
        {
            InjectScenarioVehicles();
        }
        UpdatePoses();
    }
    catch (const libsumo::TraCIException& e)
    {
        LOG_ERROR("GTSumoTrafficController: SUMO step failed at t={:.2f}: {}", time_, e.what());
    }

    Controller::Step(timeStep);
}

void gt_esmini::ControllerSumoTraffic::SpawnDepartedVehicles()
{
    if (libsumo::Simulation::getDepartedNumber() <= 0)
    {
        return;
    }

    const std::vector<std::string> departed = libsumo::Simulation::getDepartedIDList();
    for (const std::string& id : departed)
    {
        if (entities_->nameExists(id))
        {
            continue;  // one of ours pushed INTO SUMO, not a SUMO-born vehicle
        }

        const std::string vclass = libsumo::Vehicle::getVehicleClass(id);

        Vehicle* vehicle = nullptr;
        if (vehicle_pool_.GetVehicles(ControllerSumo::SUMOVClass2OSCVehicleCategory(vclass)).empty())
        {
            vehicle = new Vehicle();
            if (template_vehicle_ != nullptr)
            {
                vehicle->SetModel3DFullPath(template_vehicle_->GetModel3DFullPath());
            }
        }
        else
        {
            Vehicle* pooled = (vclass != "ignoring") ? vehicle_pool_.GetRandomVehicle(ControllerSumo::SUMOVClass2OSCVehicleCategory(vclass))
                                                     : vehicle_pool_.GetRandomVehicle();
            if (pooled != nullptr)
            {
                vehicle = new Vehicle(*pooled);
            }
        }

        if (vehicle == nullptr)
        {
            LOG_ERROR("GTSumoTrafficController: no 3D model available for SUMO vehicle {}", id);
            continue;
        }

        vehicle->name_ = id;
        vehicle->AssignController(this);
        if (scale_mode_ != EntityScaleMode::UNDEFINED)
        {
            vehicle->scaleMode_ = scale_mode_;
        }
        vehicle->role_ = Object::Role::CIVIL;
        // Upstream's known inconsistency, kept: the 3D model follows the vClass
        // but the OSC category stays CAR (design doc section 5).
        vehicle->category_ = Vehicle::Category::CAR;
        vehicle->odometer_ = 0.0;
        vehicle->dirty_.SetBits(Object::DirtyBit::TELEPORT);  // pops up: skip odometer update

        LOG_INFO("GTSumoTrafficController: add SUMO vehicle {} ({}) to scenario", id, vclass);
        entities_->addObject(vehicle, true);
        spawned_ids_.insert(id);
    }
}

void gt_esmini::ControllerSumoTraffic::RemoveArrivedVehicles()
{
    if (libsumo::Simulation::getArrivedNumber() <= 0)
    {
        return;
    }

    const std::vector<std::string> arrived = libsumo::Simulation::getArrivedIDList();
    for (const std::string& id : arrived)
    {
        tracked_.erase(id);
        pushed_ids_.erase(id);

        // Design doc section 3-7: only entities THIS controller spawned may be
        // removed on a SUMO arrival. Upstream matches on name alone, so a SUMO
        // arrival event deletes the scenario's own entity of that name -- the
        // Ego included, once it reaches the end of its SUMO route.
        if (spawned_ids_.erase(id) == 0)
        {
            LOG_INFO("GTSumoTrafficController: SUMO vehicle {} arrived but was not spawned here, keeping the scenario entity", id);
            continue;
        }

        Object* obj = entities_->GetObjectByName(id);
        if (obj == nullptr)
        {
            continue;
        }

        LOG_INFO("GTSumoTrafficController: remove SUMO vehicle {} from scenario", id);
        if (!obj->objectEvents_.empty() || !obj->initActions_.empty())
        {
            entities_->deactivateObject(obj);
        }
        else
        {
            entities_->removeObject(obj, false);
        }
    }
}

void gt_esmini::ControllerSumoTraffic::InjectScenarioVehicles()
{
    const std::vector<std::string> sumo_ids = libsumo::Vehicle::getIDList();

    for (size_t i = 0; i < entities_->object_.size(); i++)
    {
        Object* obj = entities_->object_[i];
        if (!obj->IsActive() || obj->IsGhost() || obj->TowVehicle() != nullptr)
        {
            continue;  // trailers and ghosts have no business in SUMO
        }
        if (obj->IsAnyActiveControllerOfType(static_cast<Controller::Type>(CONTROLLER_TYPE_SUMO_TRAFFIC)))
        {
            continue;  // SUMO already owns it
        }
        if (std::find(sumo_ids.begin(), sumo_ids.end(), obj->GetName()) != sumo_ids.end())
        {
            continue;  // already injected
        }

        const std::string id = obj->name_;
        try
        {
            // departSpeed = the entity's current speed. SUMO otherwise starts a
            // freshly inserted vehicle at 0 and ramps up over ~1.2 s, which is
            // the whole of the residual error measured in design doc 2-4.
            libsumo::Vehicle::add(id, "", "DEFAULT_VEHTYPE", "now", "first", "base", std::to_string(obj->GetSpeed()));

            // Take the speed limiter off before the first setSpeed, so SUMO sees
            // the scenario's speed instead of min(requested, lane limit *
            // speedFactor) -- design doc 2-4 / 3-4.
            if (config_.speed_mode >= 0)
            {
                libsumo::Vehicle::setSpeedMode(id, config_.speed_mode);
            }
            PushToSumo(obj);
            pushed_ids_.insert(id);
            LOG_INFO("GTSumoTrafficController: add scenario vehicle {} to SUMO at {:.2f} m/s", id, obj->GetSpeed());
        }
        catch (const libsumo::TraCIException& e)
        {
            LOG_ERROR("GTSumoTrafficController: failed to add {} to SUMO: {}", id, e.what());
        }
    }
}

gt_esmini::sumotraffic::SumoPose gt_esmini::ControllerSumoTraffic::SumoPoseOf(const Object* obj) const
{
    const sumotraffic::EsminiPose pose{obj->pos_.GetX(), obj->pos_.GetY(), obj->pos_.GetH()};
    return sumotraffic::ToSumoPose(pose, net_offset_, obj->boundingbox_.center_.x_, obj->boundingbox_.dimensions_.length_);
}

void gt_esmini::ControllerSumoTraffic::PushToSumo(Object* obj)
{
    const sumotraffic::SumoPose pose = SumoPoseOf(obj);
    libsumo::Vehicle::moveToXY(obj->name_, "random", 0, pose.x, pose.y, pose.angle_deg, 0);
    libsumo::Vehicle::setSpeed(obj->name_, obj->GetSpeed());
}

void gt_esmini::ControllerSumoTraffic::UpdatePoses()
{
    for (size_t i = 0; i < entities_->object_.size(); i++)
    {
        Object* obj = entities_->object_[i];
        if (!obj->IsActive())
        {
            continue;
        }

        if (obj->IsAnyActiveControllerOfType(static_cast<Controller::Type>(CONTROLLER_TYPE_SUMO_TRAFFIC)))
        {
            const std::string&           id    = obj->name_;
            const libsumo::TraCIPosition pos   = libsumo::Vehicle::getPosition3D(id);
            const sumotraffic::Vec2      front{pos.x, pos.y};
            const double                 bb_cx = obj->boundingbox_.center_.x_;
            const double                 bb_l  = obj->boundingbox_.dimensions_.length_;

            // Default: SUMO's own angle, converted (and with the front-bumper
            // -> reference-point shift SUMO's position needs).
            sumotraffic::EsminiPose target =
                sumotraffic::ToEsminiPose(sumotraffic::SumoPose{pos.x, pos.y, libsumo::Vehicle::getAngle(id)}, net_offset_, bb_cx, bb_l);

            // Override: SUMO 1.6.0's angle carries no lane-change yaw offset
            // under the sublane model, so take the direction the vehicle
            // actually moved in (design doc 3-6). Below the displacement floor
            // the last good heading is kept; before there is one at all, SUMO's
            // angle stands.
            TrackedPose& tracked = tracked_[id];
            if (config_.override_heading)
            {
                double derived = 0.0;
                if (tracked.has_pos && sumotraffic::HeadingFromDisplacement(tracked.pos, front, kMinHeadingDisplacement, derived))
                {
                    tracked.heading     = derived;
                    tracked.has_heading = true;
                }
                if (tracked.has_heading)
                {
                    const sumotraffic::Vec2 ref =
                        sumotraffic::FrontToRefPoint(sumotraffic::Vec2{front.x - net_offset_.x, front.y - net_offset_.y},
                                                     tracked.heading,
                                                     bb_cx,
                                                     bb_l);
                    target = sumotraffic::EsminiPose{ref.x, ref.y, tracked.heading};
                }
            }
            tracked.pos     = front;
            tracked.has_pos = true;

            obj->SetSpeed(libsumo::Vehicle::getSpeed(id));
            obj->pos_.SetInertiaPosMode(target.x,
                                        target.y,
                                        pos.z,
                                        target.heading_rad,
                                        // getSlope() is positive uphill, esmini pitch is negative uphill
                                        sumotraffic::SumoSlopeToPitch(libsumo::Vehicle::getSlope(id)),
                                        0,
                                        roadmanager::Position::PosMode::Z_ABS | roadmanager::Position::PosMode::H_ABS |
                                            roadmanager::Position::PosMode::P_ABS | roadmanager::Position::PosMode::R_REL);

            if (obj->dirty_.Check(Object::DirtyBit::TELEPORT) && !obj->TowVehicle() && obj->TrailerVehicle())
            {
                static_cast<Vehicle*>(obj)->AlignTrailers();
            }
            if (obj->GetType() == Object::Type::VEHICLE)
            {
                static_cast<Vehicle*>(obj)->AlignRearAxlePosition();
            }

            obj->dirty_.SetBits(Object::DirtyBit::LATERAL | Object::DirtyBit::LONGITUDINAL);
        }
        else if (config_.inject_ego && !obj->IsGhost() && obj->TowVehicle() == nullptr &&
                 pushed_ids_.find(obj->name_) != pushed_ids_.end())
        {
            PushToSumo(obj);
        }
    }
}
