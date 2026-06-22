/*
 * RouteDriveController: lane-aware route following (LaneIndependentRouter) with
 * automatic lane changes and turn-signal (winker) pre-arming N seconds before
 * each lane change. A "strong default controller" — falls back to plain default
 * behaviour when no route is assigned.
 *
 * Ported/extended from EnvironmentSimulator ControllerFollowRoute (core is
 * read-only per repo policy R1). Position is moved by the lane-change action;
 * longitudinal motion is left to the default controller. Steering wheel angle is
 * rendered by a stacked ControllerKinematic (it reads GetActiveLaneChangeAction).
 */

#include <algorithm>
#include <cmath>
#include <fstream>

#include "gt_esmini/control/ControllerRouteDrive.hpp"
#include "gt_esmini/scenario/ExtraEntities.hpp"
#include "CommonMini.hpp"
#include "Entities.hpp"
#include "Storyboard.hpp"
#include "ScenarioEngine.hpp"
#include "LaneIndependentRouter.hpp"
#include "logger.hpp"

using namespace scenarioengine;

scenarioengine::Controller* gt_esmini::InstantiateControllerRouteDrive(void* args)
{
    Controller::InitArgs* initArgs = static_cast<Controller::InitArgs*>(args);
    return new gt_esmini::ControllerRouteDrive(initArgs);
}

gt_esmini::ControllerRouteDrive::ControllerRouteDrive(InitArgs* args) : Controller(args)
{
    if (args && args->properties)
    {
        if (args->properties->ValueExists("configFile"))
        {
            LoadConfig(args->properties->GetValueStr("configFile"));
        }
        if (args->properties->ValueExists("winkerLeadTime"))
        {
            config_.winker_lead_time = strtod(args->properties->GetValueStr("winkerLeadTime"));
        }
        if (args->properties->ValueExists("laneChangeTime"))
        {
            config_.lane_change_time = strtod(args->properties->GetValueStr("laneChangeTime"));
        }
        if (args->properties->ValueExists("minDistForCollision"))
        {
            config_.min_dist_for_collision = strtod(args->properties->GetValueStr("minDistForCollision"));
        }
        if (args->properties->ValueExists("debugLog"))
        {
            std::string v     = args->properties->GetValueStr("debugLog");
            config_.debug_log = (v == "true" || v == "1");
        }
    }
}

gt_esmini::ControllerRouteDrive::~ControllerRouteDrive()
{
    if (laneChangeAction_ != nullptr)
    {
        delete laneChangeAction_;
        laneChangeAction_ = nullptr;
    }
}

void gt_esmini::ControllerRouteDrive::LoadConfig(const std::string& configPath)
{
    std::ifstream file(configPath);
    if (!file.is_open())
    {
        LOG_WARN("RouteDriveController: Failed to open config file: {}", configPath);
        return;
    }
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    auto parseDouble = [&content](const std::string& key, double fallback) -> double {
        size_t pos = content.find("\"" + key + "\"");
        if (pos == std::string::npos)
            return fallback;
        size_t colon = content.find(':', pos);
        if (colon == std::string::npos)
            return fallback;
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
            return fallback;
        size_t colon = content.find(':', pos);
        if (colon == std::string::npos)
            return fallback;
        return content.substr(colon + 1, 20).find("true") != std::string::npos;
    };

    // String enum value of a key (between the colon and the next comma/brace), lowercased-ish.
    auto parseStr = [&content](const std::string& key) -> std::string {
        size_t pos = content.find("\"" + key + "\"");
        if (pos == std::string::npos)
            return "";
        size_t q1 = content.find('"', content.find(':', pos) + 1);
        if (q1 == std::string::npos)
            return "";
        size_t q2 = content.find('"', q1 + 1);
        if (q2 == std::string::npos)
            return "";
        return content.substr(q1 + 1, q2 - q1 - 1);
    };

    config_.winker_lead_time       = parseDouble("winker_lead_time", config_.winker_lead_time);
    config_.lane_change_time       = parseDouble("lane_change_time", config_.lane_change_time);
    config_.min_dist_for_collision = parseDouble("min_dist_for_collision", config_.min_dist_for_collision);
    config_.look_ahead_dist        = parseDouble("look_ahead_dist", config_.look_ahead_dist);
    config_.gap_comfort_distance   = parseDouble("gap_comfort_distance", config_.gap_comfort_distance);
    config_.debug_log              = parseBool("debug_log", config_.debug_log);

    std::string timing = parseStr("timing");
    std::string gap    = parseStr("gap");
    if (timing == "late")        timing_alpha_ = 0.0;
    else if (timing == "early")  timing_alpha_ = 1.0;
    else if (timing == "normal") timing_alpha_ = 0.5;
    if (gap == "wide")        gap_beta_ = 0.0;
    else if (gap == "tight")  gap_beta_ = 1.0;
    else if (gap == "normal") gap_beta_ = 0.5;

    LOG_INFO("RouteDriveController: Config loaded from {} (timing_alpha={}, gap_beta={})", configPath, timing_alpha_, gap_beta_);
}

void gt_esmini::ControllerRouteDrive::Init()
{
    // Lateral domain only; longitudinal stays with the default controller.
    operating_domains_ = static_cast<unsigned int>(ControlDomainMasks::DOMAIN_MASK_LAT);
    if (mode_ != ControlOperationMode::MODE_ADDITIVE)
    {
        LOG_INFO("RouteDriveController mode \"{}\" not applicable. Using additive mode (override only during lane changes).", Mode2Str(mode_));
        mode_ = ControlOperationMode::MODE_ADDITIVE;
    }
    Controller::Init();
}

int gt_esmini::ControllerRouteDrive::Activate(const ControlActivationMode (&mode)[static_cast<unsigned int>(ControlDomains::COUNT)])
{
    if (object_ != nullptr)
    {
        odr_ = object_->pos_.GetOpenDrive();
    }
    currentWaypointIndex_  = 0;
    scenarioWaypointIndex_ = 0;
    pathCalculated_        = false;
    changingLane_          = false;
    laneChangeDir_         = 0;
    junctionTurnDir_       = 0;
    junctionArmed_         = false;
    lcDirThisRoad_         = 0;
    prevTrackId_           = ID_UNDEFINED;
    indicatorLeftOn_       = false;
    indicatorRightOn_      = false;
    waypoints_.clear();
    laneChangeAction_ = nullptr;

    return Controller::Activate(mode);
}

void gt_esmini::ControllerRouteDrive::ReportKeyEvent(int key, bool down)
{
    (void)key;
    (void)down;
}

void gt_esmini::ControllerRouteDrive::Step(double timeStep)
{
    // R5-U3: advance the GT blink ticker so RouteDrive FLASHING indicators animate.
    if (object_ && object_->type_ == scenarioengine::Object::Type::VEHICLE)
    {
        auto* veh = static_cast<scenarioengine::Vehicle*>(object_);
        if (auto* ext = VehicleExtensionManager::Instance().GetExtension(veh))
        {
            lightSimClock_ += timeStep;
            ext->Tick(lightSimClock_, timeStep);
        }
    }

    if (object_->pos_.GetRoute() == nullptr)
    {
        // No route assigned: behave as the plain default controller.
        ApplyIndicator(0);
        Controller::Step(timeStep);
        return;
    }

    if (!pathCalculated_)
    {
        CalculateWaypoints();
    }

    if (static_cast<unsigned int>(currentWaypointIndex_) >= waypoints_.size())
    {
        Deactivate();
        return;
    }

    roadmanager::Position vehiclePos   = object_->pos_;
    roadmanager::Position nextWaypoint = waypoints_[static_cast<unsigned int>(currentWaypointIndex_)];

    bool sameRoad = nextWaypoint.GetTrackId() == vehiclePos.GetTrackId();

    // Lanes with different ids may still be connected between lane sections.
    int connectedLaneID =
        odr_->GetRoadById(vehiclePos.GetTrackId())->GetConnectedLaneIdAtS(vehiclePos.GetLaneId(), vehiclePos.GetS(), nextWaypoint.GetS());
    bool lsecConnectedLane = connectedLaneID == nextWaypoint.GetLaneId();
    bool sameLane          = (nextWaypoint.GetLaneId() == vehiclePos.GetLaneId()) || lsecConnectedLane;

    // --- Winker pre-arm + lane-change trigger (Timing x Gap unified policy) ---
    int wantDir = 0;
    if (changingLane_)
    {
        wantDir = laneChangeDir_;  // hold latched direction through the maneuver
    }
    else if (sameRoad && !sameLane)
    {
        const int    target = nextWaypoint.GetLaneId();
        const int    dir    = LaneChangeDirection(vehiclePos, target);
        const double speed  = MAX(fabs(object_->GetSpeed()), 0.1);
        // Deadline is the end of the current road in the travel direction (the connection
        // point to the next road), where the target lane must already be reached. Measuring
        // to the road end — not to the waypoint s — gives a meaningful approach runway for
        // the Timing knob regardless of where the router placed the waypoint.
        roadmanager::Road* curRoad = odr_->GetRoadById(vehiclePos.GetTrackId());
        const double       roadLen = curRoad ? curRoad->GetLength() : 0.0;
        const bool         withS   = vehiclePos.GetDrivingDirectionRelativeRoad() >= 0;
        const double       dist    = withS ? MAX(roadLen - vehiclePos.GetS(), 0.0) : MAX(vehiclePos.GetS(), 0.0);

        // Latest feasible start so the transition still completes by the waypoint.
        const double dDeadline = MAX(config_.lane_change_time * speed, 25.0);
        // Timing knob: how far ahead we start seeking the change (alpha 0=Late .. 1=Early).
        const double dSeek = dDeadline + timing_alpha_ * MAX(config_.look_ahead_dist - dDeadline, 0.0);

        const bool laneAvail = TargetLaneAvailable(target);

        // Winker leads the (potential) start by winker_lead_time; preserved across all settings.
        if (laneAvail && dist <= dSeek + config_.winker_lead_time * speed)
        {
            wantDir = dir;
        }

        const bool seeking    = laneAvail && dist <= dSeek;
        const bool atDeadline = dist <= dDeadline;

        // Gap knob: required target-lane gap shrinks as beta->1 (Tight), floored at the
        // collision distance. At the deadline we go on any safe gap (forced).
        const double reqGap = MAX(config_.gap_comfort_distance * (1.0 - gap_beta_), config_.min_dist_for_collision);
        double gapAhead = LARGE_NUMBER, gapBehind = LARGE_NUMBER;
        ComputeTargetLaneGaps(target, gapAhead, gapBehind);
        const bool gapOk = (gapAhead >= reqGap && gapBehind >= reqGap);

        if (seeking && CanChangeLane(target) && (gapOk || atDeadline))
        {
            laneChangeDir_ = dir;
            wantDir        = dir;
            LOG_INFO("RouteDriveController: LC trigger dist={:.1f}m (dSeek={:.1f}, dDeadline={:.1f}, gapA={:.1f}, gapB={:.1f}, reqGap={:.1f}, forced={})",
                     dist, dSeek, dDeadline, gapAhead, gapBehind, reqGap, atDeadline && !gapOk);
            CreateLaneChange(target);
            changingLane_ = true;
        }
    }

    // --- Junction turn-signal (only when no lane-change signal is active) ---
    // Reset the per-road lane-change memory when we roll onto a new road.
    if (vehiclePos.GetTrackId() != prevTrackId_)
    {
        prevTrackId_   = vehiclePos.GetTrackId();
        lcDirThisRoad_ = 0;
    }

    if (wantDir == 0 && !changingLane_)
    {
        const bool inJunction = vehiclePos.GetJunctionId() != ID_UNDEFINED;

        bool approachingJunction = false;
        if (!inJunction)
        {
            roadmanager::Road* curRoad = odr_->GetRoadById(vehiclePos.GetTrackId());
            if (curRoad)
            {
                const bool             withS  = vehiclePos.GetDrivingDirectionRelativeRoad() >= 0;
                roadmanager::RoadLink* onward = curRoad->GetLink(withS ? roadmanager::LinkType::SUCCESSOR : roadmanager::LinkType::PREDECESSOR);
                if (onward && onward->GetElementType() == roadmanager::RoadLink::ElementType::ELEMENT_TYPE_JUNCTION)
                {
                    const double roadLen = curRoad->GetLength();
                    const double dist    = withS ? MAX(roadLen - vehiclePos.GetS(), 0.0) : MAX(vehiclePos.GetS(), 0.0);
                    const double speed   = MAX(fabs(object_->GetSpeed()), 0.1);
                    const double lead    = MAX(config_.winker_lead_time * speed, 30.0);
                    approachingJunction  = dist <= lead;
                }
            }
        }

        if (inJunction || approachingJunction)
        {
            // Decide once per junction maneuver, then hold the decision through transit
            // (the connecting road is a different track, so we must latch it here).
            if (!junctionArmed_)
            {
                const int jdir = JunctionTurnDirection();

                // Same-direction lane change preceded this junction. Two real-world cases
                // look identical at this point and we must keep them apart:
                //   (a) Highway exit: LC into a deceleration/exit lane, then follow a long
                //       gentle ramp. The lane-change blinker already conveyed intent — a
                //       second blink through the ramp is redundant. Suppress.
                //   (b) Turn lane at intersection: LC into a turn lane, then a sharp turn
                //       at the junction. Drivers (and the law) expect the blinker to stay
                //       on through the turn. Keep signalling.
                // Curvature distinguishes them cleanly: the ramp is a long, low-turn-rate
                // curve (rad/m); the intersection connector is a short, high-turn-rate
                // pivot. Probe the route's connecting road and decide on turn rate.
                bool exitContinuation = (lcDirThisRoad_ != 0 && jdir == lcDirThisRoad_);
                if (exitContinuation)
                {
                    roadmanager::Road* connRoad = nullptr;
                    for (unsigned int i = static_cast<unsigned int>(MAX(0, currentWaypointIndex_)); i < waypoints_.size(); i++)
                    {
                        roadmanager::Road* wpRoad = odr_->GetRoadById(waypoints_[i].GetTrackId());
                        if (wpRoad && wpRoad->GetJunction() != ID_UNDEFINED)
                        {
                            connRoad = wpRoad;
                            break;
                        }
                    }
                    if (connRoad && connRoad->GetLength() > 0.1)
                    {
                        roadmanager::Position p0;
                        roadmanager::Position pE;
                        p0.SetTrackPos(connRoad->GetId(), 0.0, 0.0);
                        pE.SetTrackPos(connRoad->GetId(), connRoad->GetLength(), 0.0);
                        const double dh       = fabs(GetAngleInIntervalMinusPIPlusPI(pE.GetH() - p0.GetH()));
                        const double turnRate = dh / connRoad->GetLength();
                        constexpr double SHARP_TURN_RATE = 0.04;  // rad/m (~2.3°/m)
                        if (turnRate >= SHARP_TURN_RATE)
                        {
                            exitContinuation = false;  // sharp connector ⇒ true turn-lane turn
                        }
                    }
                }

                junctionTurnDir_ = exitContinuation ? 0 : jdir;
                junctionArmed_   = true;
                if (junctionTurnDir_ != 0)
                {
                    LOG_INFO("RouteDriveController: junction turn signal dir {}", junctionTurnDir_);
                }
            }
            wantDir = junctionTurnDir_;
        }
        else
        {
            junctionArmed_   = false;
            junctionTurnDir_ = 0;
        }
    }

    ApplyIndicator(wantDir);

    ChangeLane(timeStep);
    UpdateWaypoints(vehiclePos, nextWaypoint);

    Controller::Step(timeStep);
}

int gt_esmini::ControllerRouteDrive::LaneChangeDirection(const roadmanager::Position& pos, int targetLane) const
{
    // +t / higher lane id is road-left when driving with s; flip for against-s so
    // the result is always vehicle-frame (+1 = vehicle-left). Mirrors AutoLightController.
    double sign_drive = (pos.GetDrivingDirectionRelativeRoad() < 0) ? -1.0 : 1.0;
    int    laneDiff   = (targetLane - pos.GetLaneId()) * static_cast<int>(sign_drive);
    if (laneDiff > 0)
        return 1;  // vehicle-left
    if (laneDiff < 0)
        return -1;  // vehicle-right
    return 0;
}

int gt_esmini::ControllerRouteDrive::JunctionTurnDirection() const
{
    if (odr_ == nullptr || waypoints_.empty())
    {
        return 0;
    }

    const roadmanager::Position& cur      = object_->pos_;
    const id_t                   curTrack = cur.GetTrackId();

    // Find the waypoint on the road the vehicle will drive on AFTER the next junction:
    // the first waypoint on a different road that is itself a real road (not a junction
    // connecting road). Fall back to the first different-road waypoint (a connector).
    const roadmanager::Position* afterJunction = nullptr;
    const roadmanager::Position* firstOther    = nullptr;
    for (unsigned int i = static_cast<unsigned int>(MAX(0, currentWaypointIndex_)); i < waypoints_.size(); i++)
    {
        if (waypoints_[i].GetTrackId() == curTrack)
        {
            continue;
        }
        if (firstOther == nullptr)
        {
            firstOther = &waypoints_[i];
        }
        roadmanager::Road* wpRoad = odr_->GetRoadById(waypoints_[i].GetTrackId());
        if (wpRoad && wpRoad->GetJunction() == ID_UNDEFINED)
        {
            afterJunction = &waypoints_[i];
            break;
        }
    }

    const roadmanager::Position* target = afterJunction ? afterJunction : firstOther;
    if (target == nullptr)
    {
        return 0;
    }

    // GetDrivingDirection() is the actual heading the vehicle faces, so a world-frame
    // CCW (left) turn maps directly to vehicle-left regardless of road s-direction.
    const double targetH  = target->GetDrivingDirection();
    const double currentH = cur.GetDrivingDirection();
    const double diff     = GetAngleInIntervalMinusPIPlusPI(targetH - currentH);

    constexpr double threshold = 0.10;  // rad (matches AutoLightController)
    if (diff > threshold)
    {
        return 1;  // vehicle-left
    }
    if (diff < -threshold)
    {
        return -1;  // vehicle-right
    }
    return 0;
}

void gt_esmini::ControllerRouteDrive::ApplyIndicator(int dir)
{
    if (!object_ || object_->type_ != scenarioengine::Object::Type::VEHICLE)
    {
        return;
    }
    auto* vehicle = static_cast<scenarioengine::Vehicle*>(object_);
    auto* ext     = VehicleExtensionManager::Instance().GetExtension(vehicle);
    if (!ext)
    {
        return;
    }

    const bool wantLeft  = (dir > 0);
    const bool wantRight = (dir < 0);

    auto setOne = [&](VehicleLightType type, bool on, bool& cache) {
        if (ext->IsManualOverride(type))
        {
            return;  // respect manual/button control
        }
        if (on == cache)
        {
            return;  // no change
        }
        LightState st;
        st.mode = on ? LightState::Mode::FLASHING : LightState::Mode::OFF;
        ext->SetLightState(type, st);
        ext->SetLightSource(type, LightSource::AUTO);
        cache = on;
    };

    setOne(VehicleLightType::INDICATOR_LEFT, wantLeft, indicatorLeftOn_);
    setOne(VehicleLightType::INDICATOR_RIGHT, wantRight, indicatorRightOn_);
}

void gt_esmini::ControllerRouteDrive::CalculateWaypoints()
{
    roadmanager::LaneIndependentRouter router(odr_);

    roadmanager::Position startPos(object_->pos_);
    roadmanager::Position targetPos(object_->pos_.GetRoute()->scenario_waypoints_[static_cast<unsigned int>(scenarioWaypointIndex_)]);

    // If current road is found along route, set successor waypoint as target.
    unsigned int i = 0;
    unsigned int j = 0;
    for (auto& scwp : object_->pos_.GetRoute()->all_waypoints_)
    {
        if (scwp.GetTrackId() == object_->pos_.GetRoute()->scenario_waypoints_[j].GetTrackId())
        {
            j++;
        }

        if (scwp.GetTrackId() == startPos.GetTrackId())
        {
            bool drivingWithRoadDirection = startPos.GetDrivingDirectionRelativeRoad() == 1;
            if ((i < object_->pos_.GetRoute()->all_waypoints_.size() - 1) ||
                ((drivingWithRoadDirection && startPos.GetS() < scwp.GetS() - SMALL_NUMBER) ||
                 (!drivingWithRoadDirection && object_->pos_.GetS() > scwp.GetS() + SMALL_NUMBER)))
            {
                scenarioWaypointIndex_ = MIN(static_cast<int>(j), static_cast<int>(object_->pos_.GetRoute()->scenario_waypoints_.size() - 1));
                targetPos              = object_->pos_.GetRoute()->scenario_waypoints_[MAX(0, static_cast<unsigned int>(scenarioWaypointIndex_))];
                break;
            }
        }
        i++;
    }

    std::vector<roadmanager::Node> pathToGoal = router.CalculatePath(startPos, targetPos);
    if (pathToGoal.empty())
    {
        LOG_ERROR("RouteDriveController: Path not found, deactivating controller");
        Deactivate();
    }
    else
    {
        waypoints_ = router.GetWaypoints(pathToGoal, startPos, targetPos);

        object_->pos_.GetRoute()->ReplaceMinimalWaypoints({waypoints_[0], waypoints_[1]});
        object_->dirty_.SetBits(Object::DirtyBit::ROUTE);
        pathCalculated_ = true;
    }
}

void gt_esmini::ControllerRouteDrive::CreateLaneChange(int lane)
{
    LatLaneChangeAction* action_lanechange    = new LatLaneChangeAction(nullptr);
    action_lanechange->SetName("RouteDriveLaneChange");
    action_lanechange->object_                = object_;
    action_lanechange->transition_.shape_     = OSCPrivateAction::DynamicsShape::SINUSOIDAL;
    action_lanechange->transition_.dimension_ = OSCPrivateAction::DynamicsDimension::TIME;
    action_lanechange->transition_.SetParamTargetVal(config_.lane_change_time);
    action_lanechange->max_num_executions_ = 10;

    LatLaneChangeAction::TargetAbsolute* target = new LatLaneChangeAction::TargetAbsolute;
    target->value_                              = lane;
    action_lanechange->target_                  = target;

    laneChangeAction_ = action_lanechange;

    LOG_INFO("RouteDriveController: lane change -> lane {} (dir {})", lane, laneChangeDir_);
}

void gt_esmini::ControllerRouteDrive::ChangeLane(double timeStep)
{
    if (laneChangeAction_ == nullptr || laneChangeAction_->GetCurrentState() == OSCAction::State::COMPLETE)
    {
        return;
    }

    if (!(laneChangeAction_->GetCurrentState() == StoryBoardElement::State::RUNNING))
    {
        laneChangeAction_->Start(scenario_engine_->getSimulationTime());
        mode_ = ControlOperationMode::MODE_OVERRIDE;  // prevent default controller from moving the entity laterally
        // skip step this frame; default controller already moved the entity
    }
    else
    {
        mode_ = ControlOperationMode::MODE_ADDITIVE;
        laneChangeAction_->Step(scenario_engine_->getSimulationTime(), timeStep);
        mode_ = ControlOperationMode::MODE_OVERRIDE;

        if (laneChangeAction_->GetCurrentState() == OSCAction::State::COMPLETE)
        {
            lcDirThisRoad_ = laneChangeDir_;  // remember this move for exit-vs-turn signal logic
            changingLane_  = false;
            laneChangeDir_ = 0;
            delete laneChangeAction_;
            laneChangeAction_ = nullptr;
            mode_             = ControlOperationMode::MODE_ADDITIVE;
        }
    }
}

bool gt_esmini::ControllerRouteDrive::TargetLaneAvailable(int lane)
{
    roadmanager::Position     vehiclePos = object_->pos_;
    roadmanager::Road*        road       = odr_->GetRoadById(vehiclePos.GetTrackId());
    roadmanager::LaneSection* ls         = road->GetLaneSectionByS(vehiclePos.GetS());
    if (ls->GetLaneById(lane) == 0)  // lane does not exist on road in current lanesection
    {
        return false;
    }
    if (road->GetLaneWidthByS(vehiclePos.GetS(), lane) < minLaneWidth_)
    {
        return false;
    }
    return true;
}

void gt_esmini::ControllerRouteDrive::ComputeTargetLaneGaps(int lane, double& ahead, double& behind)
{
    ahead  = LARGE_NUMBER;
    behind = LARGE_NUMBER;

    roadmanager::Position vehiclePos = object_->pos_;
    // +s ahead when driving with road direction, otherwise -s is ahead.
    const double dirSign = (vehiclePos.GetDrivingDirectionRelativeRoad() < 0) ? -1.0 : 1.0;

    for (Object* other : scenario_engine_->entities_.object_)
    {
        if (other == object_)
        {
            continue;
        }
        if (other->pos_.GetTrackId() != vehiclePos.GetTrackId() || other->pos_.GetLaneId() != lane)
        {
            continue;  // only vehicles already in the target lane on this road
        }
        const double ds = (other->pos_.GetS() - vehiclePos.GetS()) * dirSign;  // >0 ahead, <0 behind
        if (ds >= 0.0)
        {
            ahead = MIN(ahead, ds);
        }
        else
        {
            behind = MIN(behind, -ds);
        }
    }
}

bool gt_esmini::ControllerRouteDrive::CanChangeLane(int lane)
{
    roadmanager::Position vehiclePos  = object_->pos_;
    std::vector<Object*>  allVehicles = scenario_engine_->entities_.object_;
    if (changingLane_)
    {
        return false;
    }
    if (!TargetLaneAvailable(lane))
    {
        return false;
    }

    if (config_.min_dist_for_collision <= 0)
    {
        return true;
    }

    for (Object* otherVehicle : allVehicles)
    {
        bool sameRoadOther = otherVehicle->pos_.GetTrackId() == vehiclePos.GetTrackId();
        bool sameLaneOther = otherVehicle->pos_.GetLaneId() == vehiclePos.GetLaneId();
        if (otherVehicle == object_ || (sameRoadOther && sameLaneOther))
        {
            continue;
        }
        bool collisionRisk  = DistanceBetween(vehiclePos, otherVehicle->pos_) < config_.min_dist_for_collision;
        bool sameSideOfRoad = SIGN(vehiclePos.GetLaneId()) == SIGN(otherVehicle->pos_.GetLaneId());

        if (!sameLaneOther && collisionRisk && sameSideOfRoad && sameRoadOther)
        {
            int  n   = vehiclePos.GetLaneId();
            auto inc = [&n] { return ++n; };
            auto dec = [&n] { return --n; };

            int  lanesBetween    = abs(vehiclePos.GetLaneId() - otherVehicle->pos_.GetLaneId());
            bool lanesIncreasing = otherVehicle->pos_.GetLaneId() > vehiclePos.GetLaneId();

            std::vector<int> laneIdsToCheck(static_cast<unsigned int>(lanesBetween));
            if (lanesIncreasing)
            {
                std::generate(laneIdsToCheck.begin(), laneIdsToCheck.end(), inc);
            }
            else
            {
                std::generate(laneIdsToCheck.begin(), laneIdsToCheck.end(), dec);
            }

            bool collides = std::find(laneIdsToCheck.begin(), laneIdsToCheck.end(), otherVehicle->pos_.GetLaneId()) != laneIdsToCheck.end();
            if (collides)
            {
                return false;
            }
        }
    }
    return true;
}

double gt_esmini::ControllerRouteDrive::DistanceBetween(roadmanager::Position p1, roadmanager::Position p2)
{
    double dx = p1.GetX() - p2.GetX();
    double dy = p1.GetY() - p2.GetY();
    return sqrt(dx * dx + dy * dy);
}

void gt_esmini::ControllerRouteDrive::UpdateWaypoints(roadmanager::Position vehiclePos, roadmanager::Position nextWaypoint)
{
    WaypointStatus waypointStatus = GetWaypointStatus(vehiclePos, nextWaypoint);
    switch (waypointStatus)
    {
        case WaypointStatus::PASSED:
        {
            currentWaypointIndex_++;
            if (nextWaypoint.GetTrackId() == waypoints_.back().GetTrackId())
            {
                bool scenarioWaypointsLeft =
                    static_cast<unsigned int>(scenarioWaypointIndex_) < object_->pos_.GetRoute()->scenario_waypoints_.size() - 1;
                if (scenarioWaypointsLeft)
                {
                    scenarioWaypointIndex_++;
                    pathCalculated_ = false;
                    CalculateWaypoints();
                    currentWaypointIndex_ = 0;
                }
                return;
            }

            object_->pos_.GetRoute()->ReplaceMinimalWaypoints({vehiclePos, waypoints_[static_cast<unsigned int>(currentWaypointIndex_)]});
            object_->dirty_.SetBits(Object::DirtyBit::ROUTE);
            return;
        }
        case WaypointStatus::MISSED:
            if (object_->pos_.GetRoute() != nullptr)
            {
                pathCalculated_ = false;
                CalculateWaypoints();
                currentWaypointIndex_ = 0;
            }
            return;
        case WaypointStatus::NOT_REACHED:
            return;
    }
}

gt_esmini::ControllerRouteDrive::WaypointStatus gt_esmini::ControllerRouteDrive::GetWaypointStatus(roadmanager::Position vehiclePos,
                                                                                                  roadmanager::Position waypoint)
{
    using namespace roadmanager;
    bool drivingWithRoadDirection = vehiclePos.GetDrivingDirectionRelativeRoad() == 1;
    bool sameRoad                 = waypoint.GetTrackId() == vehiclePos.GetTrackId();
    bool sameLane                 = waypoint.GetLaneId() == vehiclePos.GetLaneId();

    if (sameRoad && ((drivingWithRoadDirection && vehiclePos.GetS() > waypoint.GetS() - SMALL_NUMBER) ||
                     (!drivingWithRoadDirection && vehiclePos.GetS() < waypoint.GetS() + SMALL_NUMBER)))
    {
        return sameLane ? WaypointStatus::PASSED : WaypointStatus::MISSED;
    }
    if (sameRoad)
    {
        return WaypointStatus::NOT_REACHED;
    }
    Road*               currentRoad = odr_->GetRoadById(vehiclePos.GetTrackId());
    std::vector<Road*>  possiblePreviousRoads;
    RoadLink*           link;
    if (drivingWithRoadDirection)
    {
        link = currentRoad->GetLink(LinkType::PREDECESSOR);
    }
    else
    {
        link = currentRoad->GetLink(LinkType::SUCCESSOR);
    }

    if (link == nullptr)
    {
        return WaypointStatus::NOT_REACHED;
    }

    if (link->GetElementType() == RoadLink::ElementType::ELEMENT_TYPE_ROAD)
    {
        possiblePreviousRoads.push_back(odr_->GetRoadById(link->GetElementId()));
    }
    else if (link->GetElementType() == RoadLink::ElementType::ELEMENT_TYPE_JUNCTION)
    {
        Junction* junction = odr_->GetJunctionById(link->GetElementId());
        for (unsigned j = 0; j < junction->GetNoConnectionsFromRoadId(currentRoad->GetId()); j++)
        {
            id_t roadId = junction->GetConnectingRoadIdFromIncomingRoadId(currentRoad->GetId(), j);
            possiblePreviousRoads.push_back(odr_->GetRoadById(roadId));
        }
    }

    std::vector<Road*>::iterator itr = std::find_if(possiblePreviousRoads.begin(),
                                                    possiblePreviousRoads.end(),
                                                    [&waypoint](Road* road) { return road->GetId() == waypoint.GetTrackId(); });
    if (itr != possiblePreviousRoads.end())
    {
        int previousLaneId = currentRoad->GetConnectingLaneId(link, vehiclePos.GetLaneId(), waypoint.GetTrackId());
        return previousLaneId == waypoint.GetLaneId() ? WaypointStatus::PASSED : WaypointStatus::MISSED;
    }
    return WaypointStatus::NOT_REACHED;
}

void gt_esmini::ControllerRouteDrive::Deactivate()
{
    ApplyIndicator(0);
    LOG_INFO("RouteDriveController - Deactivated");
    if (object_ != nullptr)
    {
        object_->pos_.SetRoute(nullptr);
    }
    Controller::Deactivate();
}
