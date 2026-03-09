#include "esminijs.hpp"
#include "StoryboardElement.hpp"
#include "OSCCondition.hpp"
#include <iostream>

namespace esmini
{

    // Static member initialization
    std::vector<StoryBoardEvent> OpenScenario::sbEventBuffer_;
    std::vector<ConditionEvent>  OpenScenario::condEventBuffer_;

    void copyStateFromScenarioGateway(ScenarioObjectState *state, scenarioengine::ObjectStateStruct *gw_state)
    {
        state->id              = gw_state->info.id;
        state->model_id        = gw_state->info.model_id;
        state->ctrl_type       = gw_state->info.ctrl_type;
        state->name            = std::string(gw_state->info.name, NAME_LEN);
        state->timestamp       = gw_state->info.timeStamp;
        state->x               = (float)gw_state->pos.GetX();
        state->y               = (float)gw_state->pos.GetY();
        state->z               = (float)gw_state->pos.GetZ();
        state->h               = (float)gw_state->pos.GetH();
        state->p               = (float)gw_state->pos.GetP();
        state->r               = (float)gw_state->pos.GetR();
        state->speed           = (float)gw_state->info.speed;
        state->road_id         = (int)gw_state->pos.GetTrackId();
        state->junction_id     = (int)gw_state->pos.GetJunctionId();
        state->t               = (float)gw_state->pos.GetT();
        state->lane_id         = (int)gw_state->pos.GetLaneId();
        state->s               = (float)gw_state->pos.GetS();
        state->lane_offset     = (float)gw_state->pos.GetOffset();
        state->center_offset_x = gw_state->info.boundingbox.center_.x_;
        state->center_offset_y = gw_state->info.boundingbox.center_.y_;
        state->center_offset_z = gw_state->info.boundingbox.center_.z_;
        state->width           = gw_state->info.boundingbox.dimensions_.width_;
        state->length          = gw_state->info.boundingbox.dimensions_.length_;
        state->height          = gw_state->info.boundingbox.dimensions_.height_;
        state->object_type     = gw_state->info.obj_type;
        state->object_category = gw_state->info.obj_category;

        // Extract wheel information from wheel_data vector
        if (!gw_state->info.wheel_data.empty())
        {
            // Use the first wheel's data for wheel angle and rotation
            const auto &wheel  = gw_state->info.wheel_data[0];
            state->wheel_angle = (float)wheel.h;              // heading/yaw for wheel angle
            state->wheel_rot   = (float)wheel.rotation_rate;  // rotation rate for wheel rotation
        }
        else
        {
            // Default values when no wheel data available
            state->wheel_angle = 0.0f;
            state->wheel_rot   = 0.0f;
        }
    }

    // Static callback: captures storyboard element state changes into buffer
    void OpenScenario::onStoryBoardElementStateChange(const char* name, int type, int state, const char* full_path)
    {
        StoryBoardEvent event;
        event.name      = name ? name : "";
        event.type      = type;
        event.state     = state;
        event.fullPath  = full_path ? full_path : "";
        event.timestamp = 0.0;  // Will be set to simulation time by caller context
        sbEventBuffer_.push_back(event);
    }

    // Static callback: captures condition trigger events into buffer
    void OpenScenario::onConditionTriggered(const char* name, double timestamp)
    {
        ConditionEvent event;
        event.name      = name ? name : "";
        event.timestamp = timestamp;
        condEventBuffer_.push_back(event);
    }

    void OpenScenario::registerCallbacks()
    {
        // Clear any previous buffers
        sbEventBuffer_.clear();
        condEventBuffer_.clear();

        // Register static callbacks directly on the esmini engine classes
        scenarioengine::StoryBoardElement::stateChangeCallback = &OpenScenario::onStoryBoardElementStateChange;
        scenarioengine::OSCCondition::conditionCallback        = &OpenScenario::onConditionTriggered;
    }

    OpenScenario::OpenScenario(const std::string &xosc_file, const OpenScenarioConfig &config)
        : xosc_file(xosc_file), config(config), initialized_(false), complete_(false), lastStepResult_(0)
    {
        this->scenarioEngine  = new scenarioengine::ScenarioEngine(this->xosc_file, false);
        this->scenarioGateway = this->scenarioEngine->getScenarioGateway();
        registerCallbacks();
    }

    OpenScenario::~OpenScenario()
    {
        // Unregister callbacks to avoid dangling pointers
        scenarioengine::StoryBoardElement::stateChangeCallback = nullptr;
        scenarioengine::OSCCondition::conditionCallback        = nullptr;

        if (scenarioEngine)
        {
            delete scenarioEngine;
            scenarioEngine  = nullptr;
            scenarioGateway = nullptr;
        }
    }

    // --- Batch execution (existing API, unchanged) ---

    std::vector<ScenarioObjectState> OpenScenario::get_object_state(const OpenScenarioConfig *config)
    {
        OpenScenarioConfig _config = this->config;
        if (config != nullptr)
        {
            _config = *config;
        }

        std::vector<ScenarioObjectState> objects_sts;
        int                              retval = 0;
        double                           dt;
        int64_t                          time_stamp = 0;
        while (retval == 0 && _config.max_loop > 0)
        {
            if (_config.dt == 0)
            {
                dt = SE_getSimTimeStep(time_stamp, _config.min_time_step, _config.max_time_step);
            }
            else
            {
                dt = _config.dt;
            }

            retval = this->scenarioEngine->step(dt);
            this->scenarioEngine->prepareGroundTruth(dt);
            this->scenarioGateway->clearDirtyBits();

            int numberofObjects = this->scenarioGateway->getNumberOfObjects();
            for (int i = 0; i < numberofObjects; i++)
            {
                scenarioengine::ObjectState* obj_state_ptr = this->scenarioGateway->getObjectStatePtrByIdx(i);
                if (obj_state_ptr != nullptr)
                {
                    ScenarioObjectState state;
                    copyStateFromScenarioGateway(&state, &obj_state_ptr->state_);
                    objects_sts.push_back(state);
                }
            }
            --_config.max_loop;
        }

        return objects_sts;
    }

    std::vector<ScenarioObjectState> OpenScenario::get_object_state_by_second(const int second, const int fps)
    {
        OpenScenarioConfig _config;
        _config.max_loop = second * fps;
        _config.dt       = 1.0 / fps;

        return this->get_object_state(&_config);
    }

    // --- Step execution (new API for editor playback) ---

    int OpenScenario::step(double dt)
    {
        if (complete_)
        {
            return -1;
        }

        lastStepResult_ = this->scenarioEngine->step(dt);
        initialized_    = true;

        // Finalize ground truth: computes velocities, accelerations, and
        // updates the ScenarioGateway object states so that getCurrentState()
        // returns the correct positions for this time step.
        // This mirrors the ScenarioFrame() flow in playerbase.cpp:
        //   1. scenarioEngine->step(dt)
        //   2. scenarioEngine->prepareGroundTruth(dt)
        //   3. scenarioGateway->clearDirtyBits()
        this->scenarioEngine->prepareGroundTruth(dt);
        this->scenarioGateway->clearDirtyBits();

        if (lastStepResult_ != 0)
        {
            complete_ = true;
        }

        // Update timestamps on storyboard events that were captured during this step
        double simTime = getSimulationTime();
        for (auto& event : sbEventBuffer_)
        {
            if (event.timestamp == 0.0)
            {
                event.timestamp = simTime;
            }
        }

        return lastStepResult_;
    }

    double OpenScenario::getSimulationTime() const
    {
        return this->scenarioEngine->getSimulationTime();
    }

    int OpenScenario::getNumberOfObjects() const
    {
        return this->scenarioGateway->getNumberOfObjects();
    }

    bool OpenScenario::isComplete() const
    {
        return complete_;
    }

    void OpenScenario::collectCurrentState(std::vector<ScenarioObjectState>& out)
    {
        int numberofObjects = this->scenarioGateway->getNumberOfObjects();
        out.reserve(static_cast<size_t>(numberofObjects));

        for (int i = 0; i < numberofObjects; i++)
        {
            // Use index-based access (not ID-based) to iterate all objects
            scenarioengine::ObjectState* obj_state_ptr = this->scenarioGateway->getObjectStatePtrByIdx(i);
            if (obj_state_ptr != nullptr)
            {
                ScenarioObjectState state;
                copyStateFromScenarioGateway(&state, &obj_state_ptr->state_);
                out.push_back(state);
            }
        }
    }

    std::vector<ScenarioObjectState> OpenScenario::getCurrentState()
    {
        std::vector<ScenarioObjectState> states;
        collectCurrentState(states);
        return states;
    }

    std::vector<StoryBoardEvent> OpenScenario::popStoryBoardEvents()
    {
        std::vector<StoryBoardEvent> events;
        events.swap(sbEventBuffer_);
        return events;
    }

    std::vector<ConditionEvent> OpenScenario::popConditionEvents()
    {
        std::vector<ConditionEvent> events;
        events.swap(condEventBuffer_);
        return events;
    }

}  // namespace esmini
