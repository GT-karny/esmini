/*
 * KinematicController: Follows the scenario path using a kinematic bicycle model
 * instead of perfectly snapping to road geometry. This produces physically plausible
 * steering behavior (rate-limited, with inertia) while still tracking the scenario.
 */

#include "gt_esmini/control/ControllerKinematic.hpp"
#include "CommonMini.hpp"
#include "Entities.hpp"
#include "ScenarioGateway.hpp"
#include "ScenarioEngine.hpp"
#include "logger.hpp"

#include <fstream>
#include <cmath>
#include <algorithm>

using namespace scenarioengine;

Controller* gt_esmini::InstantiateControllerKinematic(void* args)
{
    Controller::InitArgs* initArgs = static_cast<Controller::InitArgs*>(args);
    return new gt_esmini::ControllerKinematic(initArgs);
}

gt_esmini::ControllerKinematic::ControllerKinematic(InitArgs* args)
    : Controller(args),
      initialized_(false),
      prev_heading_error_(0.0)
{
    // Parse optional properties from XOSC <Controller><Properties>
    if (args && args->properties)
    {
        if (args->properties->ValueExists("configFile"))
        {
            LoadConfig(args->properties->GetValueStr("configFile"));
        }
        if (args->properties->ValueExists("lookAheadTime"))
        {
            config_.look_ahead_time = strtod(args->properties->GetValueStr("lookAheadTime"));
        }
        if (args->properties->ValueExists("debugLog"))
        {
            std::string val = args->properties->GetValueStr("debugLog");
            config_.debug_log = (val == "true" || val == "1");
        }
    }
}

void gt_esmini::ControllerKinematic::Init()
{
    // MODE_ADDITIVE: do NOT override any domain.
    // All scenario actions (LaneChange, SpeedAction, Route, etc.) and defaultController
    // run normally, updating object_->pos_ as the ideal path target.
    // This controller reads object_->pos_ and produces physically plausible XY/heading.
    operating_domains_ = static_cast<unsigned int>(ControlDomainMasks::DOMAIN_MASK_LAT);

    if (mode_ != ControlOperationMode::MODE_ADDITIVE)
    {
        LOG_INFO("KinematicController mode \"{}\" not applicable. Using additive mode instead.", Mode2Str(mode_));
        mode_ = ControlOperationMode::MODE_ADDITIVE;
    }

    Controller::Init();
}

void gt_esmini::ControllerKinematic::LoadConfig(const std::string& configPath)
{
    std::ifstream file(configPath);
    if (!file.is_open())
    {
        LOG_WARN("KinematicController: Failed to open config file: {}", configPath);
        return;
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    file.close();

    // Simple JSON value parser (no external JSON library dependency)
    auto parseDouble = [&content](const std::string& key, double fallback) -> double {
        size_t pos = content.find("\"" + key + "\"");
        if (pos == std::string::npos)
        {
            return fallback;
        }
        size_t colon = content.find(':', pos);
        if (colon == std::string::npos)
        {
            return fallback;
        }
        try
        {
            return std::stod(content.substr(colon + 1));
        }
        catch (...)
        {
            return fallback;
        }
    };

    auto parseBool = [&content](const std::string& key, bool fallback) -> bool {
        size_t pos = content.find("\"" + key + "\"");
        if (pos == std::string::npos)
        {
            return fallback;
        }
        size_t colon = content.find(':', pos);
        if (colon == std::string::npos)
        {
            return fallback;
        }
        std::string rest = content.substr(colon + 1, 20);
        return (rest.find("true") != std::string::npos);
    };

    auto parseString = [&content](const std::string& key, const std::string& fallback) -> std::string {
        size_t pos = content.find("\"" + key + "\"");
        if (pos == std::string::npos)
        {
            return fallback;
        }
        size_t colon = content.find(':', pos);
        if (colon == std::string::npos)
        {
            return fallback;
        }
        size_t quote1 = content.find('"', colon + 1);
        if (quote1 == std::string::npos)
        {
            return fallback;
        }
        size_t quote2 = content.find('"', quote1 + 1);
        if (quote2 == std::string::npos)
        {
            return fallback;
        }
        return content.substr(quote1 + 1, quote2 - quote1 - 1);
    };

    config_.look_ahead_time        = parseDouble("look_ahead_time", config_.look_ahead_time);
    config_.min_look_ahead_dist    = parseDouble("min_look_ahead_dist", config_.min_look_ahead_dist);
    config_.max_look_ahead_dist    = parseDouble("max_look_ahead_dist", config_.max_look_ahead_dist);
    config_.steering_speed_inertia = parseDouble("steering_speed_inertia", config_.steering_speed_inertia);
    config_.max_steering_rate      = parseDouble("max_steering_rate", config_.max_steering_rate);
    config_.debug_log              = parseBool("debug_log", config_.debug_log);

    double max_steer_deg = parseDouble("max_steering_angle_deg", config_.max_steering_angle * 180.0 / M_PI);
    config_.max_steering_angle = max_steer_deg * M_PI / 180.0;

    LOG_INFO("KinematicController: Config loaded from {}", configPath);
    LOG_INFO("  look_ahead={:.2f}s min_la={:.1f} max_la={:.1f} max_steer={:.1f}deg rate={:.2f}rad/s inertia={:.4f}",
             config_.look_ahead_time, config_.min_look_ahead_dist, config_.max_look_ahead_dist,
             max_steer_deg, config_.max_steering_rate, config_.steering_speed_inertia);
}

int gt_esmini::ControllerKinematic::Activate(const ControlActivationMode (&mode)[static_cast<unsigned int>(ControlDomains::COUNT)])
{
    if (object_)
    {
        vehicle_.wheelAngle_ = 0.0;
        vehicle_.wheelRotation_ = 0.0;
        initialized_ = true;
        prev_heading_error_ = 0.0;
    }

    return Controller::Activate(mode);
}

bool gt_esmini::ControllerKinematic::ComputeLookAheadTarget(double look_ahead_dist, double& target_x, double& target_y)
{
    look_ahead_dist = CLAMP(look_ahead_dist, config_.min_look_ahead_dist, config_.max_look_ahead_dist);

    // Create a temporary position from object's current road position for look-ahead.
    // object_->pos_ is kept up-to-date by scenario actions + defaultController (route, lane, s).
    // Share route pointer so MoveAlongS follows the correct path at junctions.
    roadmanager::Position lookAheadPos;
    lookAheadPos.Duplicate(object_->pos_);
    lookAheadPos.route_ = object_->pos_.route_;
    int ret = static_cast<int>(lookAheadPos.MoveAlongS(look_ahead_dist));

    // Detach shared pointer — lookAheadPos must not delete the route on destruction
    lookAheadPos.route_ = nullptr;

    if (ret == 0 ||
        ret == static_cast<int>(roadmanager::Position::ReturnCode::ERROR_END_OF_ROUTE) ||
        ret == static_cast<int>(roadmanager::Position::ReturnCode::ERROR_END_OF_ROAD))
    {
        target_x = lookAheadPos.GetX();
        target_y = lookAheadPos.GetY();
        return true;
    }

    if (config_.debug_log)
    {
        LOG_INFO("KinematicController [{}]: MoveAlongS failed (ret={}), holding previous angle",
                 object_->GetName(), ret);
    }
    return false;
}

void gt_esmini::ControllerKinematic::Step(double timeStep)
{
    if (!object_ || !initialized_)
    {
        return;
    }

    // --- Phase 0: Detect teleport → snap filter state ---
    if (object_->GetDirtyBitMask() & static_cast<int>(Object::DirtyBit::TELEPORT))
    {
        vehicle_.wheelAngle_ = 0.0;
        prev_heading_error_ = 0.0;
        if (config_.debug_log)
        {
            LOG_INFO("KinematicController [{}]: TELEPORT reset", object_->GetName());
        }
        return;
    }

    // --- Phase 1: Compute ideal wheel angle from road geometry ---
    // Use look-ahead on the ideal path to get the geometric steering angle.
    // No bicycle model, no PD — pure geometry.
    double speed = object_->GetSpeed();
    double look_dist = CLAMP(config_.look_ahead_time * fabs(speed),
                             config_.min_look_ahead_dist, config_.max_look_ahead_dist);

    double target_x, target_y;
    bool look_ahead_ok = ComputeLookAheadTarget(look_dist, target_x, target_y);

    // If MoveAlongS failed, hold previous angle — don't compute from fallback position
    double ideal_angle = prev_heading_error_;

    if (look_ahead_ok)
    {
        // Update sensor position for visualization (only when valid)
        object_->sensor_pos_[0] = target_x;
        object_->sensor_pos_[1] = target_y;
        object_->sensor_pos_[2] = object_->pos_.GetZ();

        // Pure pursuit geometry: angle from current heading to look-ahead point
        double dx = target_x - object_->pos_.GetX();
        double dy = target_y - object_->pos_.GetY();
        double dist = sqrt(dx * dx + dy * dy);

        if (dist > 1.0)  // need meaningful distance for stable direction
        {
            double target_heading = atan2(dy, dx);
            double heading_diff = target_heading - object_->pos_.GetH();
            // Normalize to [-PI, PI]
            while (heading_diff >  M_PI) heading_diff -= 2.0 * M_PI;
            while (heading_diff < -M_PI) heading_diff += 2.0 * M_PI;

            // Pure pursuit: wheel_angle = atan(2 * L * sin(alpha) / ld)
            double wheelbase = object_->boundingbox_.dimensions_.length_ * 0.6;
            ideal_angle = atan(2.0 * wheelbase * sin(heading_diff) / dist);
        }
    }
    prev_heading_error_ = ideal_angle;  // store for next frame fallback

    // Speed-dependent max angle
    double speed_scale = 1.0 / (1.0 + config_.steering_speed_inertia * speed * speed);
    double max_angle = speed_scale * config_.max_steering_angle;
    ideal_angle = CLAMP(ideal_angle, -max_angle, max_angle);

    // --- Phase 2: Fixed-speed interp (MoveTowards) ---
    // wheel_angle moves toward ideal_angle at most max_steering_rate rad/s.
    double diff = ideal_angle - vehicle_.wheelAngle_;
    double max_delta = config_.max_steering_rate * timeStep;
    double new_angle = vehicle_.wheelAngle_ + CLAMP(diff, -max_delta, max_delta);

    vehicle_.SetWheelAngle(new_angle);

    // Wheel rotation from speed
    double wheel_radius = 0.35;
    vehicle_.wheelRotation_ += (speed * timeStep) / wheel_radius;

    // --- Phase 3: Write to gateway ---
    gateway_->updateObjectWheelRotation(object_->id_, 0.0, vehicle_.wheelRotation_);
    gateway_->updateObjectWheelAngle(object_->id_, 0.0, vehicle_.wheelAngle_);
    object_->wheel_angle_ = vehicle_.wheelAngle_;
    object_->wheel_rot_ = vehicle_.wheelRotation_;
    // Protect from ScenarioEngine's default wheel_angle recalculation (heading_rate-based)
    object_->SetDirtyBits(Object::DirtyBit::WHEEL_ANGLE | Object::DirtyBit::WHEEL_ROTATION);

    if (config_.debug_log)
    {
        LOG_INFO("KinematicController [{}]: ideal={:.4f} actual={:.4f} speed={:.1f} look_dist={:.1f}",
                 object_->GetName(), ideal_angle, vehicle_.wheelAngle_, speed, look_dist);
    }
}

void gt_esmini::ControllerKinematic::ReportKeyEvent(int key, bool down)
{
    (void)key;
    (void)down;
}
