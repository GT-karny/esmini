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

    config_.look_ahead_time       = parseDouble("look_ahead_time", config_.look_ahead_time);
    config_.min_look_ahead_dist   = parseDouble("min_look_ahead_dist", config_.min_look_ahead_dist);
    config_.max_look_ahead_dist   = parseDouble("max_look_ahead_dist", config_.max_look_ahead_dist);
    config_.max_lateral_error     = parseDouble("max_lateral_error", config_.max_lateral_error);
    config_.pd_kp                 = parseDouble("pd_kp", config_.pd_kp);
    config_.pd_kd                 = parseDouble("pd_kd", config_.pd_kd);
    config_.steering_speed_inertia = parseDouble("steering_speed_inertia", config_.steering_speed_inertia);
    config_.max_acc                  = parseDouble("max_acc", config_.max_acc);
    config_.max_dec                  = parseDouble("max_dec", config_.max_dec);
    config_.max_speed                = parseDouble("max_speed", config_.max_speed);
    config_.curve_speed_reduction_k  = parseDouble("curve_speed_reduction_k", config_.curve_speed_reduction_k);
    config_.curve_speed_min_factor   = parseDouble("curve_speed_min_factor", config_.curve_speed_min_factor);
    config_.debug_log                = parseBool("debug_log", config_.debug_log);

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

void gt_esmini::ControllerKinematic::ResetToObject()
{
    if (!object_)
    {
        return;
    }

    vehicle_.Reset();
    vehicle_.SetPos(object_->pos_.GetX(), object_->pos_.GetY(), object_->pos_.GetZ(), object_->pos_.GetH());
    vehicle_.SetLength(object_->boundingbox_.dimensions_.length_);
    vehicle_.speed_ = object_->GetSpeed();

    prev_heading_error_ = 0.0;
    initialized_ = true;
}

int gt_esmini::ControllerKinematic::Activate(const ControlActivationMode (&mode)[static_cast<unsigned int>(ControlDomains::COUNT)])
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

        initialized_ = true;
        prev_heading_error_ = 0.0;

        object_->sensor_pos_[0] = object_->pos_.GetX();
        object_->sensor_pos_[1] = object_->pos_.GetY();
        object_->sensor_pos_[2] = object_->pos_.GetZ();
    }

    return Controller::Activate(mode);
}

void gt_esmini::ControllerKinematic::ComputeLookAheadTarget(double look_ahead_dist, double& target_x, double& target_y)
{
    look_ahead_dist = CLAMP(look_ahead_dist, config_.min_look_ahead_dist, config_.max_look_ahead_dist);

    // Create a temporary position from object's current road position for look-ahead.
    // object_->pos_ is kept up-to-date by scenario actions + defaultController (route, lane, s).
    // Share route pointer so MoveAlongS follows the correct path at junctions.
    roadmanager::Position lookAheadPos;
    lookAheadPos.Duplicate(object_->pos_);
    lookAheadPos.route_ = object_->pos_.route_;
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
        // Fallback: project ahead using object heading
        target_x = object_->pos_.GetX() + look_ahead_dist * cos(object_->pos_.GetH());
        target_y = object_->pos_.GetY() + look_ahead_dist * sin(object_->pos_.GetH());
    }

    // Detach shared pointer — lookAheadPos must not delete the route on destruction
    lookAheadPos.route_ = nullptr;
}

void gt_esmini::ControllerKinematic::Step(double timeStep)
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
        return;
    }

    // --- Phase 1: Read scenario target ---
    double ref_x = object_->pos_.GetX();
    double ref_y = object_->pos_.GetY();
    double ref_h = object_->pos_.GetH();
    double target_speed = object_->GetSpeed();

    // --- Phase 1b: Gap-based reset ---
    // If the bicycle model has diverged too far (e.g. after sharp junction turns),
    // reset it to the reference position to prevent runaway divergence.
    double gap_x = ref_x - vehicle_.posX_;
    double gap_y = ref_y - vehicle_.posY_;
    double gap = sqrt(gap_x * gap_x + gap_y * gap_y);

    if (gap > config_.max_lateral_error)
    {
        vehicle_.SetPos(ref_x, ref_y, object_->pos_.GetZ(), ref_h);
        vehicle_.speed_ = target_speed;
        prev_heading_error_ = 0.0;
        gap = 0.0;

        if (config_.debug_log)
        {
            LOG_INFO("KinematicController [{}]: GAP RESET (gap was {:.1f}m > {:.1f}m limit)",
                     object_->GetName(), sqrt(gap_x * gap_x + gap_y * gap_y), config_.max_lateral_error);
        }
    }

    // --- Phase 2: Compute steering target ---
    // When bicycle is far from ideal path (e.g. during lane change), steer directly toward it.
    // When close, use look-ahead for curve anticipation.
    double target_x, target_y;

    bool trajectory_active = (object_->pos_.GetTrajectory() != nullptr);

    if (gap >= config_.min_look_ahead_dist)
    {
        // Bicycle is far from ideal path (lane change, etc.) — steer directly toward it
        target_x = ref_x;
        target_y = ref_y;
    }
    else
    {
        // Bicycle is close to ideal path — use look-ahead for curve anticipation
        double extra = config_.min_look_ahead_dist - gap;
        ComputeLookAheadTarget(extra, target_x, target_y);
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

    // 4a. Speed: use scenario speed for bicycle model kinematics.
    vehicle_.speed_ = CLAMP(target_speed, -config_.max_speed, config_.max_speed);

    // 4b. Steering: speed-dependent max angle, then rate-limit
    double speed_dependent_scale = 1.0 / (1.0 + config_.steering_speed_inertia * vehicle_.speed_ * vehicle_.speed_);
    double max_angle = speed_dependent_scale * config_.max_steering_angle;
    desired_wheel_angle = CLAMP(desired_wheel_angle, -max_angle, max_angle);

    double max_delta = config_.max_steering_rate * timeStep;
    double steer_delta = CLAMP(desired_wheel_angle - vehicle_.wheelAngle_, -max_delta, max_delta);
    vehicle_.SetWheelAngle(vehicle_.wheelAngle_ + steer_delta);

    // 4c. Run bicycle model kinematics (updates internal position, heading, wheel rotation)
    vehicle_.Update(timeStep);

    // --- Phase 5: Write steering output to gateway ---
    // Only wheel angle and rotation are written — NOT position or speed.
    // The viewer shows the ideal path position (from the normal report-to-gateway flow).
    // The bicycle model runs internally to produce realistic steering dynamics.
    gateway_->updateObjectWheelRotation(object_->id_, 0.0, vehicle_.wheelRotation_);
    gateway_->updateObjectWheelAngle(object_->id_, 0.0, vehicle_.wheelAngle_);

    // Sync wheel_angle to object for HVD estimator
    object_->wheel_angle_ = vehicle_.wheelAngle_;

    // --- Phase 6: Diagnostics ---
    if (config_.debug_log)
    {
        double err_x = vehicle_.posX_ - ref_x;
        double err_y = vehicle_.posY_ - ref_y;
        double lateral_error = fabs(-sin(ref_h) * err_x + cos(ref_h) * err_y);

        LOG_INFO("KinematicController [{}]: lat_err={:.3f}m h_err={:.4f} steer={:.3f}rad speed={:.1f}m/s gap={:.2f}m",
                 object_->GetName(), lateral_error, heading_error, vehicle_.wheelAngle_, vehicle_.speed_, gap);
    }

    // NOTE: We intentionally do NOT call Controller::Step(timeStep) here.
    // The base class clears object LAT|LONG dirty bits in ADDITIVE mode,
    // which would erase bits set by scenario actions and defaultController.
}

void gt_esmini::ControllerKinematic::ReportKeyEvent(int key, bool down)
{
    (void)key;
    (void)down;
}
