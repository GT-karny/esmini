#include <string>
#include <iostream>
#include <iomanip>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten/val.h>
#endif

#include "RoadManager.hpp"
#include "CommonMini.hpp"
#include "ScenarioEngine.hpp"

// make a class for  js interface
namespace esmini
{
    struct OpenScenarioConfig
    {
        int    max_loop      = 10e3;       // maximum loop count
        double min_time_step = 1.0 / 120;  // minimum time step
        double max_time_step = 1.0 / 25;   // maximum time step
        double dt            = 0;          // time step

        friend std::ostream& operator<<(std::ostream& output, const OpenScenarioConfig& config)
        {
            output << "max_loop:" << config.max_loop << " min_time_step:" << config.min_time_step << ",max_time_step:" << config.max_time_step
                   << ",dt:" << config.dt;
            return output;
        }
    };

    struct ScenarioObjectState
    {
        std::string name;             // name xosc define
        int         id;               // Automatically generated unique object id
        int         model_id;         // Id to control what 3D model to represent the vehicle - see carModelsFiles_[] in scenarioenginedll.cpp
        int         ctrl_type;        // 0: DefaultController 1: External. Further values see Controller::Type enum
        float       timestamp;        // Not used yet (idea is to use it to interpolate position for increased sync bewtween simulators)
        float       x;                // global x coordinate of position
        float       y;                // global y coordinate of position
        float       z;                // global z coordinate of position
        float       h;                // heading/yaw in global coordinate system
        float       p;                // pitch in global coordinate system
        float       r;                // roll in global coordinate system
        int         road_id;          // road ID
        int         junction_id;      // Junction ID (-1 if not in a junction)
        float       t;                // lateral position in road coordinate system
        int         lane_id;          // lane ID
        float       lane_offset;      // lateral offset from lane center
        float       s;                // longitudinal position in road coordinate system
        float       speed;            // speed
        float       center_offset_x;  // x coordinate of bounding box center relative object reference point (local coordinate system)
        float       center_offset_y;  // y coordinate of bounding box center relative object reference point (local coordinate system)
        float       center_offset_z;  // z coordinate of bounding box center relative object reference point (local coordinate system)
        float       width;            // width
        float       length;           // length
        float       height;           // height
        int         object_type;      // Main type according to entities.hpp / Object / Type
        int         object_category;  // Sub category within type, according to entities.hpp / Vehicle, Pedestrian, MiscObject / Category
        float       wheel_angle;      // Steering angle of the wheel
        float       wheel_rot;        // Rotation angle of the wheel

        friend std::ostream& operator<<(std::ostream& output, const ScenarioObjectState& state)
        {
            output << "name:" << state.name << "time:" << state.timestamp << ",id:" << state.id << ",model_id:" << state.model_id
                   << ",ctrl_type:" << state.ctrl_type << ",pos.x:" << std::setprecision(3) << state.x << ",pos.y:" << std::setprecision(3) << state.y
                   << ",pos.z:" << std::setprecision(3) << state.z << ",speed:" << state.speed << ",type:" << state.object_type
                   << ",category:" << state.object_category << ",width:" << state.width << ",height:" << state.height << ",length:" << state.length;
            return output;
        }
    };

    // StoryBoard element state change event (captured from scenario execution)
    struct StoryBoardEvent
    {
        std::string name;       // Element name
        int         type;       // ElementType: 1=STORY_BOARD, 2=STORY, 3=ACT, 4=MANEUVER_GROUP, 5=MANEUVER, 6=EVENT, 7=ACTION
        int         state;      // State: 0=INIT, 1=STANDBY, 2=RUNNING, 3=COMPLETE
        std::string fullPath;   // Hierarchical path e.g. "Story1::Act1::ManeuverGroup1::Event1"
        double      timestamp;  // Simulation time when the event occurred
    };

    // Condition trigger event
    struct ConditionEvent
    {
        std::string name;       // Condition name
        double      timestamp;  // Simulation time when the condition was triggered
    };

    class OpenScenario
    {
    public:
        OpenScenario(const std::string& xosc_file, const OpenScenarioConfig& config = OpenScenarioConfig{});
        ~OpenScenario();

        // Batch execution (existing API)
        std::vector<ScenarioObjectState> get_object_state(const OpenScenarioConfig* config = nullptr);
        std::vector<ScenarioObjectState> get_object_state_by_second(const int second, const int fps = 30);

        // Step execution (new API for editor playback)
        int    step(double dt);            // Advance one step. Returns 0=OK, -1=scenario ended or error
        double getSimulationTime() const;  // Current simulation time in seconds
        int    getNumberOfObjects() const; // Number of entities in the scenario
        bool   isComplete() const;         // Whether the scenario has finished

        // Get current frame state (call after step())
        std::vector<ScenarioObjectState> getCurrentState();

        // StoryBoard introspection (call after step() to get events since last call)
        std::vector<StoryBoardEvent> popStoryBoardEvents();
        std::vector<ConditionEvent>  popConditionEvents();

#ifdef __EMSCRIPTEN__
        // Traffic signal introspection
        emscripten::val getTrafficSignalStates();      // Full info (position + state), call once or on demand
        emscripten::val getTrafficLightStatesOnly();    // Lightweight: {id, state} only, call every frame

        // Vehicle light introspection (LightStateAction support)
        emscripten::val getVehicleLightStates();        // Per-vehicle light states from VehicleExtensionManager
#endif

        OpenScenario(const OpenScenario&)            = delete;
        OpenScenario& operator=(const OpenScenario&) = delete;

    private:
        std::string                      xosc_file;
        OpenScenarioConfig               config;
        scenarioengine::ScenarioEngine*  scenarioEngine;
        bool                             initialized_;  // Whether first step has been performed
        bool                             complete_;     // Whether scenario has ended
        int                              lastStepResult_;

        // Event buffers (populated by static callbacks, consumed by pop methods)
        static std::vector<StoryBoardEvent> sbEventBuffer_;
        static std::vector<ConditionEvent>  condEventBuffer_;

        // Static callback functions for ScenarioEngine registration
        static void onStoryBoardElementStateChange(const char* name, int type, int state, const char* full_path);
        static void onConditionTriggered(const char* name, double timestamp);

        void registerCallbacks();
        void collectCurrentState(std::vector<ScenarioObjectState>& out);
    };
}  // namespace esmini
