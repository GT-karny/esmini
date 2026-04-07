/*
 * esmini - Environment Simulator Minimalistic
 * https://github.com/esmini/esmini
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) partners of Simulation Scenarios
 * https://sites.google.com/view/simulationscenarios
 */

/*
 * KinematicController: Follows the scenario path using a kinematic bicycle model
 * instead of perfectly snapping to road geometry. This produces physically plausible
 * steering behavior (rate-limited, with inertia) while still tracking the scenario.
 */

#include "ControllerKinematic.hpp"
#include "CommonMini.hpp"
#include "Entities.hpp"
#include "ScenarioGateway.hpp"
#include "ScenarioEngine.hpp"
#include "logger.hpp"

#include <fstream>
#include <cmath>
#include <algorithm>

using namespace scenarioengine;

Controller* scenarioengine::InstantiateControllerKinematic(void* args)
{
    Controller::InitArgs* initArgs = static_cast<Controller::InitArgs*>(args);
    return new ControllerKinematic(initArgs);
}

ControllerKinematic::ControllerKinematic(InitArgs* args)
    : Controller(args),
      initialized_(false),
      ghost_valid_(false),
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
        if (args->properties->ValueExists("maxLateralError"))
        {
            config_.max_lateral_error = strtod(args->properties->GetValueStr("maxLateralError"));
        }
        if (args->properties->ValueExists("debugLog"))
        {
            std::string val = args->properties->GetValueStr("debugLog");
            config_.debug_log = (val == "true" || val == "1");
        }
    }
}

void ControllerKinematic::Init()
{
    // Force override mode — this controller replaces default scenario movement
    if (mode_ != ControlOperationMode::MODE_OVERRIDE)
    {
        LOG_INFO("KinematicController mode \"{}\" not applicable. Using override mode instead.", Mode2Str(mode_));
        mode_ = ControlOperationMode::MODE_OVERRIDE;
    }

    Controller::Init();
}

void ControllerKinematic::LoadConfig(const std::string& configPath)
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

    config_.look_ahead_time       = parseDouble("look_ahead_time", config_.look_ahead_time);
    config_.min_look_ahead_dist   = parseDouble("min_look_ahead_dist", config_.min_look_ahead_dist);
    config_.max_look_ahead_dist   = parseDouble("max_look_ahead_dist", config_.max_look_ahead_dist);
    config_.max_lateral_error     = parseDouble("max_lateral_error", config_.max_lateral_error);
    config_.pd_kp                 = parseDouble("pd_kp", config_.pd_kp);
    config_.pd_kd                 = parseDouble("pd_kd", config_.pd_kd);
    config_.steering_speed_inertia = parseDouble("steering_speed_inertia", config_.steering_speed_inertia);
    config_.max_acc               = parseDouble("max_acc", config_.max_acc);
    config_.max_dec               = parseDouble("max_dec", config_.max_dec);
    config_.max_speed             = parseDouble("max_speed", config_.max_speed);
    config_.debug_log             = parseBool("debug_log", config_.debug_log);

    double max_steer_deg = parseDouble("max_steering_angle_deg", config_.max_steering_angle * 180.0 / M_PI);
    config_.max_steering_angle = max_steer_deg * M_PI / 180.0;

    config_.max_steering_rate = parseDouble("max_steering_rate", config_.max_steering_rate);

    std::string road_end = parseString("road_end_behavior", "inertia");
    if (road_end == "stop")
    {
        config_.road_end_behavior = Config::RoadEndBehavior::STOP;
    }
    else if (road_end == "error")
    {
        config_.road_end_behavior = Config::RoadEndBehavior::HALT_ERROR;
    }
    else
    {
        config_.road_end_behavior = Config::RoadEndBehavior::INERTIA;
    }

    LOG_INFO("KinematicController: Config loaded from {}", configPath);
}

void ControllerKinematic::ResetToObject()
{
    if (!object_)
    {
        return;
    }

    vehicle_.Reset();
    vehicle_.SetPos(object_->pos_.GetX(), object_->pos_.GetY(), object_->pos_.GetZ(), object_->pos_.GetH());
    vehicle_.SetLength(object_->boundingbox_.dimensions_.length_);
    vehicle_.speed_ = object_->GetSpeed();

    // Copy ghost position from current object position
    ghostPos_ = object_->pos_;

    prev_heading_error_ = 0.0;
    ghost_valid_ = true;
    initialized_ = true;
}

int ControllerKinematic::Activate(const ControlActivationMode (&mode)[static_cast<unsigned int>(ControlDomains::COUNT)])
{
    if (object_)
    {
        vehicle_.Reset();
        vehicle_.SetPos(object_->pos_.GetX(), object_->pos_.GetY(), object_->pos_.GetZ(), object_->pos_.GetH());
        vehicle_.SetLength(object_->boundingbox_.dimensions_.length_);
        vehicle_.speed_ = object_->GetSpeed();
        vehicle_.SetMaxSpeed(config_.max_speed);
        vehicle_.SetMaxAcc(config_.max_acc);
        vehicle_.SetMaxDec(config_.max_dec);
        vehicle_.SetSteeringScale(config_.steering_speed_inertia);
        vehicle_.SetSteeringRate(config_.max_steering_rate);

        // Initialize ghost at current object position
        ghostPos_ = object_->pos_;
        ghost_valid_ = true;
        initialized_ = true;
        prev_heading_error_ = 0.0;

        object_->sensor_pos_[0] = object_->pos_.GetX();
        object_->sensor_pos_[1] = object_->pos_.GetY();
        object_->sensor_pos_[2] = object_->pos_.GetZ();
    }

    return Controller::Activate(mode);
}

void ControllerKinematic::ComputeLookAheadTarget(double speed, double& target_x, double& target_y)
{
    double abs_speed = fabs(speed);
    double look_ahead_dist = abs_speed * config_.look_ahead_time;
    look_ahead_dist = CLAMP(look_ahead_dist, config_.min_look_ahead_dist, config_.max_look_ahead_dist);

    // Compute a look-ahead position along the ghost's road path
    roadmanager::Position lookAheadPos = ghostPos_;
    int ret = static_cast<int>(lookAheadPos.MoveAlongS(look_ahead_dist));

    if (ret == 0 ||
        ret == static_cast<int>(roadmanager::Position::ReturnCode::ERROR_END_OF_ROUTE) ||
        ret == static_cast<int>(roadmanager::Position::ReturnCode::ERROR_END_OF_ROAD))
    {
        target_x = lookAheadPos.GetX();
        target_y = lookAheadPos.GetY();
    }
    else
    {
        // Fallback: project ahead using ghost heading
        target_x = ghostPos_.GetX() + look_ahead_dist * cos(ghostPos_.GetH());
        target_y = ghostPos_.GetY() + look_ahead_dist * sin(ghostPos_.GetH());
    }
}

void ControllerKinematic::Step(double timeStep)
{
    if (!object_ || !initialized_)
    {
        return;
    }

    // --- Phase 0: Detect teleport ---
    if (object_->GetDirtyBitMask() & static_cast<int>(Object::DirtyBit::TELEPORT))
    {
        ResetToObject();
        if (config_.debug_log)
        {
            LOG_INFO("KinematicController [{}]: TELEPORT detected, reset to ({:.2f}, {:.2f}, h={:.3f})",
                     object_->GetName(), object_->pos_.GetX(), object_->pos_.GetY(), object_->pos_.GetH());
        }
        Controller::Step(timeStep);
        return;
    }

    // --- Phase 1: Get target speed from scenario ---
    double target_speed = object_->GetSpeed();

    // --- Phase 2: Compute steering target via look-ahead on ghost road path ---
    double target_x, target_y;
    if (ghost_valid_)
    {
        ComputeLookAheadTarget(target_speed, target_x, target_y);
    }
    else
    {
        // Inertia mode: project ahead using current vehicle heading
        double look_ahead_dist = CLAMP(fabs(target_speed) * config_.look_ahead_time,
                                       config_.min_look_ahead_dist, config_.max_look_ahead_dist);
        target_x = vehicle_.posX_ + look_ahead_dist * cos(vehicle_.heading_);
        target_y = vehicle_.posY_ + look_ahead_dist * sin(vehicle_.heading_);
    }

    // Update sensor position for visualization
    object_->sensor_pos_[0] = target_x;
    object_->sensor_pos_[1] = target_y;
    object_->sensor_pos_[2] = object_->pos_.GetZ();

    // --- Phase 3: Compute heading error (PD control) ---
    double diff_x = target_x - vehicle_.posX_;
    double diff_y = target_y - vehicle_.posY_;
    double len = sqrt(diff_x * diff_x + diff_y * diff_y);

    double heading_error = 0.0;
    if (len > SMALL_NUMBER)
    {
        double dir_x = diff_x / len;
        double dir_y = diff_y / len;

        double ego_dir_x, ego_dir_y;
        RotateVec2D(1.0, 0.0, vehicle_.heading_, ego_dir_x, ego_dir_y);

        heading_error = asin(CLAMP(GetCrossProduct2D(ego_dir_x, ego_dir_y, dir_x, dir_y), -1.0, 1.0));
    }

    double heading_error_rate = (heading_error - prev_heading_error_) / MAX(timeStep, SMALL_NUMBER);
    double desired_wheel_angle = config_.pd_kp * heading_error + config_.pd_kd * heading_error_rate;
    prev_heading_error_ = heading_error;

    // --- Phase 4: Update bicycle model (rate-limited steering) ---

    // 4a. Speed: converge toward target speed
    double acceleration = CLAMP(vehicle_.GetMaxAcc() * (target_speed - vehicle_.speed_),
                                -vehicle_.GetMaxDec(), vehicle_.GetMaxAcc());
    vehicle_.speed_ += acceleration * timeStep;
    vehicle_.speed_ = CLAMP(vehicle_.speed_, -config_.max_speed, config_.max_speed);

    // 4b. Steering: rate-limit then clamp
    double speed_dependent_scale = 1.0 / (1.0 + config_.steering_speed_inertia * vehicle_.speed_ * vehicle_.speed_);
    double max_angle = speed_dependent_scale * config_.max_steering_angle;
    desired_wheel_angle = CLAMP(desired_wheel_angle, -max_angle, max_angle);

    double max_delta = config_.max_steering_rate * timeStep;
    double steer_delta = CLAMP(desired_wheel_angle - vehicle_.wheelAngle_, -max_delta, max_delta);
    vehicle_.SetWheelAngle(vehicle_.wheelAngle_ + steer_delta);

    // 4c. Save pre-update position to compute actual displacement
    double prev_x = vehicle_.posX_;
    double prev_y = vehicle_.posY_;

    // 4d. Run bicycle model kinematics
    vehicle_.Update(timeStep);

    // --- Phase 5: Advance ghost by vehicle's actual displacement ---
    // This prevents longitudinal error accumulation: ghost and vehicle stay in sync along s.
    double dx_move = vehicle_.posX_ - prev_x;
    double dy_move = vehicle_.posY_ - prev_y;
    double ds_actual = sqrt(dx_move * dx_move + dy_move * dy_move);

    // Preserve sign: if going in reverse, ds should be negative
    if (vehicle_.speed_ < 0.0)
    {
        ds_actual = -ds_actual;
    }

    if (ghost_valid_ && fabs(ds_actual) > SMALL_NUMBER)
    {
        roadmanager::Position::ReturnCode ret = ghostPos_.MoveAlongS(ds_actual);

        if (ret == roadmanager::Position::ReturnCode::ERROR_END_OF_ROAD ||
            ret == roadmanager::Position::ReturnCode::ERROR_END_OF_ROUTE)
        {
            switch (config_.road_end_behavior)
            {
                case Config::RoadEndBehavior::INERTIA:
                    ghost_valid_ = false;
                    if (config_.debug_log)
                    {
                        LOG_INFO("KinematicController [{}]: Road end reached, switching to inertia mode",
                                 object_->GetName());
                    }
                    break;

                case Config::RoadEndBehavior::STOP:
                    vehicle_.speed_ = 0.0;
                    ghost_valid_ = false;
                    break;

                case Config::RoadEndBehavior::HALT_ERROR:
                    LOG_ERROR("KinematicController [{}]: Road end reached, stopping simulation", object_->GetName());
                    gateway_->updateObjectSpeed(object_->id_, 0.0, 0.0);
                    Controller::Step(timeStep);
                    return;
            }
        }
    }

    // Fetch Z and Pitch from ghost road position (keep vehicle grounded on road surface)
    vehicle_.posZ_  = ghostPos_.GetZ();
    vehicle_.pitch_ = ghostPos_.GetP();

    // --- Phase 6: Write to gateway ---
    // XY and heading come from bicycle model (physically plausible steering)
    gateway_->updateObjectWorldPosXYH(object_->id_, 0.0, vehicle_.posX_, vehicle_.posY_, vehicle_.heading_);
    gateway_->updateObjectSpeed(object_->id_, 0.0, vehicle_.speed_);

    if (IsActiveOnDomains(static_cast<unsigned int>(ControlDomainMasks::DOMAIN_MASK_LONG)))
    {
        gateway_->updateObjectWheelRotation(object_->id_, 0.0, vehicle_.wheelRotation_);
    }
    if (IsActiveOnDomains(static_cast<unsigned int>(ControlDomainMasks::DOMAIN_MASK_LAT)))
    {
        gateway_->updateObjectWheelAngle(object_->id_, 0.0, vehicle_.wheelAngle_);
    }

    // Sync wheel_angle to object for HVD estimator
    object_->wheel_angle_ = vehicle_.wheelAngle_;

    // --- Phase 7: Lateral divergence check ---
    // Compute lateral-only error (perpendicular to ghost heading) to ignore longitudinal component
    if (ghost_valid_)
    {
        double err_x = vehicle_.posX_ - ghostPos_.GetX();
        double err_y = vehicle_.posY_ - ghostPos_.GetY();

        // Project error onto ghost's lateral axis (perpendicular to ghost heading)
        double ghost_h = ghostPos_.GetH();
        double lateral_error = fabs(-sin(ghost_h) * err_x + cos(ghost_h) * err_y);

        if (config_.debug_log)
        {
            LOG_INFO("KinematicController [{}]: lat_err={:.3f}m steer={:.3f}rad speed={:.1f}m/s ghost=({:.1f},{:.1f}) veh=({:.1f},{:.1f})",
                     object_->GetName(), lateral_error, vehicle_.wheelAngle_, vehicle_.speed_,
                     ghostPos_.GetX(), ghostPos_.GetY(), vehicle_.posX_, vehicle_.posY_);
        }

        if (lateral_error > config_.max_lateral_error)
        {
            LOG_ERROR("KinematicController [{}]: Lateral error {:.2f}m exceeds threshold {:.2f}m — stopping vehicle",
                      object_->GetName(), lateral_error, config_.max_lateral_error);
            gateway_->updateObjectSpeed(object_->id_, 0.0, 0.0);
            vehicle_.speed_ = 0.0;
        }
    }

    Controller::Step(timeStep);
}

void ControllerKinematic::ReportKeyEvent(int key, bool down)
{
    (void)key;
    (void)down;
}
