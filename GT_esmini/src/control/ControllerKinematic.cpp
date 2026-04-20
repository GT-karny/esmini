/*
 * KinematicController: Computes physically plausible steering angle from a
 * unified future trajectory polyline.
 *
 * The polyline integrates all path sources in global (x,y) coordinates:
 *   - Road/route geometry via incremental MoveAlongS
 *   - LaneChange / LaneOffset displacements via TransitionDynamics f(progress)
 *   - FollowTrajectoryAction via Shape::Evaluate() sampling
 *
 * Curvature is computed with the Menger formula — naturally continuous across
 * road connections, junctions, and action boundaries.
 */

#include "gt_esmini/control/ControllerKinematic.hpp"
#include "CommonMini.hpp"
#include "Entities.hpp"
#include "ScenarioGateway.hpp"
#include "ScenarioEngine.hpp"
#include "OSCPrivateAction.hpp"
#include "logger.hpp"

#include <fstream>
#include <cmath>
#include <algorithm>

using namespace scenarioengine;

static constexpr double MAX_CURVATURE = 0.25;  // |κ| ≤ 0.25 → R ≥ 4 m

// Evaluate TransitionDynamics shape at arbitrary progress [0,1].
// Mirrors OSCPrivateAction::TransitionDynamics::Evaluate() but accepts
// an arbitrary progress value instead of using internal param_val_.
static double EvalTransition(OSCPrivateAction::DynamicsShape shape,
                             double startVal, double A, double progress)
{
    progress = CLAMP(progress, 0.0, 1.0);
    switch (shape)
    {
        case OSCPrivateAction::DynamicsShape::SINUSOIDAL:
            return startVal - A * (cos(M_PI * progress) - 1.0) / 2.0;
        case OSCPrivateAction::DynamicsShape::CUBIC:
            return startVal + A * progress * progress * (3.0 - 2.0 * progress);
        case OSCPrivateAction::DynamicsShape::LINEAR:
            return startVal + A * progress;
        case OSCPrivateAction::DynamicsShape::STEP:
            return startVal + A;
        default:
            return startVal;
    }
}

Controller* gt_esmini::InstantiateControllerKinematic(void* args)
{
    Controller::InitArgs* initArgs = static_cast<Controller::InitArgs*>(args);
    return new gt_esmini::ControllerKinematic(initArgs);
}

gt_esmini::ControllerKinematic::ControllerKinematic(InitArgs* args)
    : Controller(args),
      initialized_(false),
      prev_curvature_(0.0),
      prev_rate_(0.0),
      smoothed_output_(0.0)
{
    if (args && args->properties)
    {
        if (args->properties->ValueExists("configFile"))
            LoadConfig(args->properties->GetValueStr("configFile"));
        if (args->properties->ValueExists("debugLog"))
        {
            std::string val = args->properties->GetValueStr("debugLog");
            config_.debug_log = (val == "true" || val == "1");
        }
    }
}

void gt_esmini::ControllerKinematic::Init()
{
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

    auto parseDouble = [&content](const std::string& key, double fallback) -> double {
        size_t pos = content.find("\"" + key + "\"");
        if (pos == std::string::npos) return fallback;
        size_t colon = content.find(':', pos);
        if (colon == std::string::npos) return fallback;
        try { return std::stod(content.substr(colon + 1)); }
        catch (...) { return fallback; }
    };
    auto parseBool = [&content](const std::string& key, bool fallback) -> bool {
        size_t pos = content.find("\"" + key + "\"");
        if (pos == std::string::npos) return fallback;
        size_t colon = content.find(':', pos);
        if (colon == std::string::npos) return fallback;
        return content.substr(colon + 1, 20).find("true") != std::string::npos;
    };

    config_.trajectory_step        = parseDouble("trajectory_step", config_.trajectory_step);
    config_.curvature_preview_time = parseDouble("curvature_preview_time", config_.curvature_preview_time);
    config_.min_preview_dist       = parseDouble("min_preview_dist", config_.min_preview_dist);
    config_.max_preview_dist       = parseDouble("max_preview_dist", config_.max_preview_dist);
    config_.max_steering_rate      = parseDouble("max_steering_rate", config_.max_steering_rate);
    config_.max_steering_accel     = parseDouble("max_steering_accel", config_.max_steering_accel);
    config_.output_smoothing_tau   = parseDouble("output_smoothing_tau", config_.output_smoothing_tau);
    config_.steering_speed_inertia = parseDouble("steering_speed_inertia", config_.steering_speed_inertia);
    config_.debug_log              = parseBool("debug_log", config_.debug_log);

    double deg = parseDouble("max_steering_angle_deg", config_.max_steering_angle * 180.0 / M_PI);
    config_.max_steering_angle = deg * M_PI / 180.0;

    LOG_INFO("KinematicController: Config loaded from {}", configPath);
}

int gt_esmini::ControllerKinematic::Activate(
    const ControlActivationMode (&mode)[static_cast<unsigned int>(ControlDomains::COUNT)])
{
    if (object_)
    {
        vehicle_.wheelAngle_ = 0.0;
        vehicle_.wheelRotation_ = 0.0;
        prev_curvature_ = 0.0;
        prev_rate_ = 0.0;
        smoothed_output_ = 0.0;
        initialized_ = true;
    }
    return Controller::Activate(mode);
}

// ===========================================================================
// BuildPathFromTrajectory: FollowTrajectoryAction → sample Shape directly
// ===========================================================================
bool gt_esmini::ControllerKinematic::BuildPathFromTrajectory(double total_dist)
{
    auto actions = object_->getPrivateActions();
    for (auto* action : actions)
    {
        if (action->action_type_ != OSCAction::ActionType::FOLLOW_TRAJECTORY) continue;
        if (action->GetCurrentState() != StoryBoardElement::State::RUNNING) continue;

        auto* ftAction = static_cast<FollowTrajectoryAction*>(action);
        auto* traj = ftAction->traj_;
        if (!traj || !traj->shape_) return false;

        double current_s = object_->pos_.GetTrajectoryS();
        double traj_length = traj->GetLength();
        double step = config_.trajectory_step;
        if (step < 0.1) step = 0.1;

        for (double s = current_s; s <= current_s + total_dist && s < traj_length; s += step)
        {
            roadmanager::TrajVertex v;
            traj->shape_->Evaluate(s, roadmanager::Shape::TRAJ_PARAM_TYPE_S, v);
            future_path_.push_back({v.x, v.y});
        }
        return true;
    }
    return false;
}

// ===========================================================================
// BuildPathFromRoad: MoveAlongS polyline + LC/LaneOffset displacement overlay
// ===========================================================================
void gt_esmini::ControllerKinematic::BuildPathFromRoad(double total_dist, double speed)
{
    double step = config_.trajectory_step;
    if (step < 0.1) step = 0.1;
    double abs_speed = fabs(speed);
    if (abs_speed < 0.1) abs_speed = 0.1;  // avoid division by zero

    // --- Collect active lateral actions ---
    struct LatInfo
    {
        OSCPrivateAction::DynamicsShape shape;
        double startVal;     // td->GetStartVal()
        double A;            // targetVal - startVal
        double P;            // param target
        double current_p;    // current param
        double current_off;  // current offset = f(current_p/P)
        bool   time_based;   // TIME or RATE dimension
    };
    std::vector<LatInfo> lat_actions;

    auto actions = object_->getPrivateActions();
    for (auto* action : actions)
    {
        if (action->GetCurrentState() != StoryBoardElement::State::RUNNING) continue;

        const OSCPrivateAction::TransitionDynamics* td = nullptr;
        if (action->action_type_ == OSCAction::ActionType::LAT_LANE_CHANGE)
            td = &static_cast<LatLaneChangeAction*>(action)->transition_;
        else if (action->action_type_ == OSCAction::ActionType::LAT_LANE_OFFSET)
            td = &static_cast<LatLaneOffsetAction*>(action)->transition_;

        if (!td || td->GetParamTargetVal() < 1e-6) continue;

        double startVal = td->GetStartVal();
        double A = td->GetTargetVal() - startVal;
        double P = td->GetParamTargetVal();
        double cur_p = td->GetParamVal();
        double cur_off = EvalTransition(td->shape_, startVal, A, cur_p / P);
        bool tb = (td->dimension_ == OSCPrivateAction::DynamicsDimension::TIME ||
                   td->dimension_ == OSCPrivateAction::DynamicsDimension::RATE);

        lat_actions.push_back({td->shape_, startVal, A, P, cur_p, cur_off, tb});
    }

    // --- Build polyline ---
    future_path_.push_back({object_->pos_.GetX(), object_->pos_.GetY()});

    roadmanager::Position pos;
    pos.Duplicate(object_->pos_);
    pos.route_ = object_->pos_.route_;

    int n_steps = static_cast<int>(ceil(total_dist / step));
    double acc_dist = 0.0;

    for (int i = 0; i < n_steps; ++i)
    {
        double ds = std::min(step, total_dist - acc_dist);
        if (ds < 0.01) break;
        acc_dist += ds;

        int ret = static_cast<int>(pos.MoveAlongS(ds));
        if (ret == static_cast<int>(roadmanager::Position::ReturnCode::ERROR_GENERIC))
            break;

        double px = pos.GetX();
        double py = pos.GetY();

        // --- Overlay lateral action displacements ---
        if (!lat_actions.empty())
        {
            double road_h = pos.GetH();
            double nx = -sin(road_h);  // road-left normal
            double ny =  cos(road_h);

            for (auto& la : lat_actions)
            {
                // Estimate future param value
                double future_p;
                if (la.time_based)
                    future_p = la.current_p + acc_dist / abs_speed;
                else
                    future_p = la.current_p + acc_dist;

                future_p = std::min(future_p, la.P);  // clamp to action range

                double future_off = EvalTransition(la.shape, la.startVal, la.A, future_p / la.P);
                double delta = future_off - la.current_off;

                px += delta * nx;
                py += delta * ny;
            }
        }

        future_path_.push_back({px, py});
    }

    pos.route_ = nullptr;
}

// ===========================================================================
// RebuildFuturePath: dispatch to trajectory or road+lateral
// ===========================================================================
void gt_esmini::ControllerKinematic::RebuildFuturePath(double total_dist, double speed)
{
    future_path_.clear();

    double step = config_.trajectory_step;
    if (step < 0.1) step = 0.1;
    future_path_.reserve(static_cast<size_t>(ceil(total_dist / step)) + 2);

    if (BuildPathFromTrajectory(total_dist))
        return;

    BuildPathFromRoad(total_dist, speed);
}

// ===========================================================================
// CurvatureFromPath: Menger curvature from three polyline points
// ===========================================================================
double gt_esmini::ControllerKinematic::CurvatureFromPath(double preview_dist) const
{
    if (future_path_.size() < 3) return 0.0;

    double step = config_.trajectory_step;
    if (step < 0.1) step = 0.1;

    size_t idx = static_cast<size_t>(preview_dist / step);
    if (idx < 1) idx = 1;
    if (idx >= future_path_.size() - 1) idx = future_path_.size() - 2;

    const PathPoint& p0 = future_path_[idx - 1];
    const PathPoint& p1 = future_path_[idx];
    const PathPoint& p2 = future_path_[idx + 1];

    double ax = p1.x - p0.x, ay = p1.y - p0.y;
    double bx = p2.x - p1.x, by = p2.y - p1.y;
    double cross = ax * by - ay * bx;

    double la = sqrt(ax * ax + ay * ay);
    double lb = sqrt(bx * bx + by * by);
    double cx = p2.x - p0.x, cy = p2.y - p0.y;
    double lc = sqrt(cx * cx + cy * cy);

    double denom = la * lb * lc;
    if (denom < 1e-9) return 0.0;

    return CLAMP(2.0 * cross / denom, -MAX_CURVATURE, MAX_CURVATURE);
}

// ===========================================================================
// Step: main per-frame entry point
// ===========================================================================
void gt_esmini::ControllerKinematic::Step(double timeStep)
{
    if (!object_ || !initialized_) return;

    // === Phase 0: Teleport ===
    if (object_->GetDirtyBitMask() & static_cast<int>(Object::DirtyBit::TELEPORT))
    {
        vehicle_.wheelAngle_ = 0.0;
        prev_curvature_ = 0.0;
        prev_rate_ = 0.0;
        smoothed_output_ = 0.0;
        if (config_.debug_log)
            LOG_INFO("KC [{}]: TELEPORT reset", object_->GetName());
        return;
    }

    double speed = object_->GetSpeed();
    double wheelbase = object_->boundingbox_.dimensions_.length_ * 0.6;
    double preview_dist = CLAMP(fabs(speed) * config_.curvature_preview_time,
                                config_.min_preview_dist, config_.max_preview_dist);

    // === Phase 1: Build unified polyline & compute curvature ===
    RebuildFuturePath(preview_dist + config_.trajectory_step * 2.0, speed);

    double curvature = prev_curvature_;  // fallback
    if (future_path_.size() >= 3)
    {
        curvature = CurvatureFromPath(preview_dist);

        // sensor_pos for visualization
        double step = config_.trajectory_step;
        if (step < 0.1) step = 0.1;
        size_t vis_idx = static_cast<size_t>(preview_dist / step);
        if (vis_idx >= future_path_.size()) vis_idx = future_path_.size() - 1;
        object_->sensor_pos_[0] = future_path_[vis_idx].x;
        object_->sensor_pos_[1] = future_path_[vis_idx].y;
        object_->sensor_pos_[2] = object_->pos_.GetZ();
    }
    prev_curvature_ = curvature;

    double ideal_angle = atan(wheelbase * curvature);

    // === Phase 2: Constrain ===
    double speed_scale = 1.0 / (1.0 + config_.steering_speed_inertia * speed * speed);
    double max_angle = speed_scale * config_.max_steering_angle;
    ideal_angle = CLAMP(ideal_angle, -max_angle, max_angle);

    // === Phase 3: Rate limit with acceleration constraint ===
    // First, compute desired rate to reach ideal_angle
    double desired_rate = (timeStep > 1e-6) ? (ideal_angle - vehicle_.wheelAngle_) / timeStep : 0.0;
    desired_rate = CLAMP(desired_rate, -config_.max_steering_rate, config_.max_steering_rate);

    // Limit rate-of-change of steering rate (acceleration)
    double rate_diff = desired_rate - prev_rate_;
    double max_rate_delta = config_.max_steering_accel * timeStep;
    double new_rate = prev_rate_ + CLAMP(rate_diff, -max_rate_delta, max_rate_delta);
    new_rate = CLAMP(new_rate, -config_.max_steering_rate, config_.max_steering_rate);
    prev_rate_ = new_rate;

    double raw_actual = vehicle_.wheelAngle_ + new_rate * timeStep;

    // === Phase 4: Output LPF ===
    if (config_.output_smoothing_tau > 1e-6 && timeStep > 1e-6)
    {
        double alpha = 1.0 - exp(-timeStep / config_.output_smoothing_tau);
        smoothed_output_ = smoothed_output_ + alpha * (raw_actual - smoothed_output_);
    }
    else
    {
        smoothed_output_ = raw_actual;
    }
    vehicle_.SetWheelAngle(smoothed_output_);

    // === Phase 5: Gateway write ===
    double wheel_radius = 0.35;
    vehicle_.wheelRotation_ += (speed * timeStep) / wheel_radius;

    gateway_->updateObjectWheelRotation(object_->id_, 0.0, vehicle_.wheelRotation_);
    gateway_->updateObjectWheelAngle(object_->id_, 0.0, vehicle_.wheelAngle_);
    object_->wheel_angle_ = vehicle_.wheelAngle_;
    object_->wheel_rot_ = vehicle_.wheelRotation_;
    object_->SetDirtyBits(Object::DirtyBit::WHEEL_ANGLE | Object::DirtyBit::WHEEL_ROTATION);

    if (config_.debug_log)
    {
        LOG_INFO("KC [{}]: κ={:.5f} ideal={:.4f} actual={:.4f} spd={:.1f} pts={}",
                 object_->GetName(), curvature, ideal_angle, vehicle_.wheelAngle_,
                 speed, future_path_.size());
    }
}

void gt_esmini::ControllerKinematic::ReportKeyEvent(int key, bool down)
{
    (void)key;
    (void)down;
}
