#include "esminijs.hpp"
#include "StoryboardElement.hpp"
#include "OSCCondition.hpp"
#include "gt_esmini/scenario/TrafficSignalController.hpp"
#include "gt_esmini/scenario/GT_ScenarioReader.hpp"
#include "gt_esmini/scenario/ExtraEntities.hpp"
#include "pugixml.hpp"
#include <iostream>
#include <functional>
#include <cstdio>

#ifdef __EMSCRIPTEN__
#include <emscripten/val.h>
#endif

namespace esmini
{

    // Static member initialization
    std::vector<StoryBoardEvent> OpenScenario::sbEventBuffer_;
    std::vector<ConditionEvent>  OpenScenario::condEventBuffer_;

    void copyStateFromScenarioObject(ScenarioObjectState *state, const scenarioengine::Object &obj, double sim_time)
    {
        state->id              = obj.id_;
        state->model_id        = obj.model_id_;
        state->ctrl_type       = static_cast<int>(obj.GetControllerTypeActiveOnDomain(ControlDomains::DOMAIN_LONG));
        state->name            = obj.GetName();
        state->timestamp       = (float)sim_time;
        state->x               = (float)obj.pos_.GetX();
        state->y               = (float)obj.pos_.GetY();
        state->z               = (float)obj.pos_.GetZ();
        state->h               = (float)obj.pos_.GetH();
        state->p               = (float)obj.pos_.GetP();
        state->r               = (float)obj.pos_.GetR();
        state->speed           = (float)obj.GetSpeed();
        state->road_id         = (int)obj.pos_.GetTrackId();
        state->junction_id     = (int)obj.pos_.GetJunctionId();
        state->t               = (float)obj.pos_.GetT();
        state->lane_id         = (int)obj.pos_.GetLaneId();
        state->s               = (float)obj.pos_.GetS();
        state->lane_offset     = (float)obj.pos_.GetOffset();
        state->center_offset_x = obj.boundingbox_.center_.x_;
        state->center_offset_y = obj.boundingbox_.center_.y_;
        state->center_offset_z = obj.boundingbox_.center_.z_;
        state->width           = obj.boundingbox_.dimensions_.width_;
        state->length          = obj.boundingbox_.dimensions_.length_;
        state->height          = obj.boundingbox_.dimensions_.height_;
        state->object_type     = static_cast<int>(obj.GetType());
        state->object_category = static_cast<int>(obj.category_);

        // Steering angle and rotation of the (assumed front, steering) wheel
        state->wheel_angle = (float)obj.GetWheelAngle();
        state->wheel_rot   = (float)obj.GetWheelRotation();
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
        // Load the original XOSC for extension parsing later
        pugi::xml_document originalDoc;
        pugi::xml_parse_result parseResult = originalDoc.load_file(this->xosc_file.c_str());

        // Create a sanitized copy that strips GT extension actions
        // (vanilla ScenarioReader throws on AppearanceAction/LightStateAction)
        std::string sanitizedPath = this->xosc_file + ".sanitized.tmp";
        bool useSanitized = false;

        if (parseResult)
        {
            pugi::xml_document sanitizedDoc;
            sanitizedDoc.reset(originalDoc);

            std::function<void(pugi::xml_node)> strip;
            strip = [&](pugi::xml_node node) {
                for (pugi::xml_node child = node.first_child(); child; )
                {
                    pugi::xml_node next = child.next_sibling();
                    std::string name = child.name();
                    if (name == "AppearanceAction" || name == "LightStateAction")
                    {
                        node.remove_child(child);
                    }
                    else
                    {
                        strip(child);
                    }
                    child = next;
                }
            };
            strip(sanitizedDoc);

            useSanitized = sanitizedDoc.save_file(sanitizedPath.c_str());
        }

        // Initialize ScenarioEngine with sanitized XOSC (no extension actions)
        this->scenarioEngine  = new scenarioengine::ScenarioEngine(
            useSanitized ? sanitizedPath : this->xosc_file, false);
        registerCallbacks();

        // Clean up temp file
        if (useSanitized)
        {
            std::remove(sanitizedPath.c_str());
        }

        // --- GT_esmini extensions: parse extension actions from original XOSC ---
        if (parseResult)
        {
            auto* scReader = this->scenarioEngine->GetScenarioReader();
            auto* catalogs = scReader ? scReader->GetCatalogs() : nullptr;

            gt_esmini::GT_ScenarioReader reader(
                &this->scenarioEngine->entities_,
                catalogs,
                &this->scenarioEngine->environment
            );

            // ParseExtensionActions internally calls ParseTrafficSignalControllers
            reader.ParseExtensionActions(originalDoc, this->scenarioEngine->storyBoard);
        }

        // Resolve OpenDRIVE signal pointers and apply initial phase states
        gt_esmini::TrafficSignalControllerManager::Instance().InitAll();
    }

    OpenScenario::~OpenScenario()
    {
        // Unregister callbacks to avoid dangling pointers
        scenarioengine::StoryBoardElement::stateChangeCallback = nullptr;
        scenarioengine::OSCCondition::conditionCallback        = nullptr;

        // Clear TrafficSignalController state for clean re-initialization
        gt_esmini::TrafficSignalControllerManager::Instance().Clear();

        // Clear VehicleLightExtension state for clean re-initialization
        gt_esmini::VehicleExtensionManager::Instance().Clear();

        if (scenarioEngine)
        {
            delete scenarioEngine;
            scenarioEngine  = nullptr;
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

            double simTime = this->scenarioEngine->getSimulationTime();
            for (auto* obj : this->scenarioEngine->entities_.object_)
            {
                if (obj != nullptr)
                {
                    ScenarioObjectState state;
                    copyStateFromScenarioObject(&state, *obj, simTime);
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
        // updates the entity object states so that getCurrentState()
        // returns the correct positions for this time step.
        // This mirrors the ScenarioFrame() flow in playerbase.cpp:
        //   1. scenarioEngine->step(dt)
        //   2. scenarioEngine->prepareGroundTruth(dt)
        this->scenarioEngine->prepareGroundTruth(dt);

        // Advance GT_esmini TrafficSignalControllers (auto-cycling phases)
        gt_esmini::TrafficSignalControllerManager::Instance().StepAll(dt);

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
        return static_cast<int>(this->scenarioEngine->entities_.object_.size());
    }

    bool OpenScenario::isComplete() const
    {
        return complete_;
    }

    void OpenScenario::collectCurrentState(std::vector<ScenarioObjectState>& out)
    {
        auto& objects = this->scenarioEngine->entities_.object_;
        out.reserve(objects.size());

        double simTime = this->scenarioEngine->getSimulationTime();
        for (auto* obj : objects)
        {
            if (obj != nullptr)
            {
                ScenarioObjectState state;
                copyStateFromScenarioObject(&state, *obj, simTime);
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

#ifdef __EMSCRIPTEN__
    emscripten::val OpenScenario::getTrafficSignalStates()
    {
        emscripten::val arr = emscripten::val::array();

        roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();
        if (!odr) return arr;

        for (size_t ri = 0; ri < odr->GetNumOfRoads(); ri++)
        {
            roadmanager::Road* road = odr->GetRoadByIdx(static_cast<idx_t>(ri));
            if (!road) continue;

            int roadId = static_cast<int>(road->GetId());

            for (unsigned int si = 0; si < road->GetNumberOfSignals(); si++)
            {
                roadmanager::Signal* signal = road->GetSignal(static_cast<idx_t>(si));
                if (!signal) continue;

                // Calculate world position from road coordinates (same as SE_GetRoadSign)
                roadmanager::Position pos;
                pos.SetTrackPos(static_cast<id_t>(roadId), signal->GetS(), signal->GetT());

                emscripten::val obj = emscripten::val::object();
                obj.set("id", signal->GetId());
                obj.set("name", signal->GetName());
                obj.set("roadId", roadId);
                obj.set("s", signal->GetS());
                obj.set("t", signal->GetT());
                obj.set("x", pos.GetX());
                obj.set("y", pos.GetY());
                obj.set("z", pos.GetZ() + signal->GetZOffset());
                obj.set("h", pos.GetH() + signal->GetHOffset());
                obj.set("zOffset", signal->GetZOffset());
                obj.set("orientation", signal->GetOrientation() == roadmanager::Signal::Orientation::NEGATIVE ? -1 : 1);
                obj.set("type", signal->GetType());
                obj.set("subtype", signal->GetSubType());
                obj.set("dynamic", signal->IsDynamic());
                obj.set("height", signal->GetHeight());
                obj.set("width", signal->GetWidth());

                auto* tl = dynamic_cast<roadmanager::TrafficLight*>(signal);
                if (tl)
                {
                    obj.set("isTrafficLight", true);
                    obj.set("state", tl->GetStateString());
                }
                else
                {
                    obj.set("isTrafficLight", false);
                    obj.set("state", std::string(""));
                }

                arr.call<void>("push", obj);
            }
        }

        return arr;
    }

    emscripten::val OpenScenario::getTrafficLightStatesOnly()
    {
        emscripten::val arr = emscripten::val::array();

        roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();
        if (!odr) return arr;

        auto dynamicSignals = odr->GetDynamicSignals();
        for (auto* signal : dynamicSignals)
        {
            auto* tl = dynamic_cast<roadmanager::TrafficLight*>(signal);
            if (!tl) continue;

            emscripten::val obj = emscripten::val::object();
            obj.set("id", tl->GetId());
            obj.set("state", tl->GetStateString());
            arr.call<void>("push", obj);
        }

        return arr;
    }

    emscripten::val OpenScenario::getVehicleLightStates()
    {
        emscripten::val arr = emscripten::val::array();

        auto& extMgr = gt_esmini::VehicleExtensionManager::Instance();

        for (auto* obj : scenarioEngine->entities_.object_)
        {
            if (obj->type_ != scenarioengine::Object::Type::VEHICLE)
                continue;

            auto* vehicle = static_cast<scenarioengine::Vehicle*>(obj);
            auto* ext     = extMgr.GetExtension(vehicle);
            if (!ext)
                continue;

            emscripten::val vObj = emscripten::val::object();
            vObj.set("id", obj->GetId());
            vObj.set("name", std::string(obj->name_));

            // head_light: LOW_BEAM or HIGH_BEAM → "on"
            auto lowBeam  = ext->GetLightState(gt_esmini::VehicleLightType::LOW_BEAM);
            auto highBeam = ext->GetLightState(gt_esmini::VehicleLightType::HIGH_BEAM);
            bool headOn   = (lowBeam.mode != gt_esmini::LightState::Mode::OFF) ||
                            (highBeam.mode != gt_esmini::LightState::Mode::OFF);
            vObj.set("head_light", headOn ? std::string("on") : std::string("off"));

            // indicator: left / right / warning / off
            auto indL = ext->GetLightState(gt_esmini::VehicleLightType::INDICATOR_LEFT);
            auto indR = ext->GetLightState(gt_esmini::VehicleLightType::INDICATOR_RIGHT);
            bool leftOn  = (indL.mode != gt_esmini::LightState::Mode::OFF);
            bool rightOn = (indR.mode != gt_esmini::LightState::Mode::OFF);

            std::string indicator = "off";
            if (leftOn && rightOn)
                indicator = "warning";
            else if (leftOn)
                indicator = "left";
            else if (rightOn)
                indicator = "right";
            vObj.set("indicator", indicator);

            // brake_light: off / normal
            auto brake = ext->GetLightState(gt_esmini::VehicleLightType::BRAKE_LIGHTS);
            vObj.set("brake_light", (brake.mode != gt_esmini::LightState::Mode::OFF)
                                        ? std::string("normal") : std::string("off"));

            // fog_light
            auto fogF = ext->GetLightState(gt_esmini::VehicleLightType::FOG_LIGHTS_FRONT);
            auto fogR = ext->GetLightState(gt_esmini::VehicleLightType::FOG_LIGHTS_REAR);
            auto fog  = ext->GetLightState(gt_esmini::VehicleLightType::FOG_LIGHTS);
            bool fogOn = (fogF.mode != gt_esmini::LightState::Mode::OFF) ||
                         (fogR.mode != gt_esmini::LightState::Mode::OFF) ||
                         (fog.mode  != gt_esmini::LightState::Mode::OFF);
            vObj.set("fog_light", fogOn ? std::string("on") : std::string("off"));

            // reversing_light
            auto rev = ext->GetLightState(gt_esmini::VehicleLightType::REVERSING_LIGHTS);
            vObj.set("reversing_light", (rev.mode != gt_esmini::LightState::Mode::OFF)
                                            ? std::string("on") : std::string("off"));

            // warning_lights (hazard)
            auto warn = ext->GetLightState(gt_esmini::VehicleLightType::WARNING_LIGHTS);
            vObj.set("warning_light", (warn.mode != gt_esmini::LightState::Mode::OFF)
                                          ? std::string("on") : std::string("off"));

            // daytime_running_lights
            auto drl = ext->GetLightState(gt_esmini::VehicleLightType::DAYTIME_RUNNING_LIGHTS);
            vObj.set("daytime_running_light", (drl.mode != gt_esmini::LightState::Mode::OFF)
                                                  ? std::string("on") : std::string("off"));

            // high_beam (separate from head_light for detailed queries)
            vObj.set("high_beam", (highBeam.mode != gt_esmini::LightState::Mode::OFF)
                                      ? std::string("on") : std::string("off"));

            arr.call<void>("push", vObj);
        }

        return arr;
    }
#endif

}  // namespace esmini
