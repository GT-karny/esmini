/*
 * GT_esmini extension -- planned-path hand-off from an on-board planner to the
 * OSI reporter.
 *
 * WHY A REGISTRY AND NOT A DIRECT CALL
 * ------------------------------------
 * GT_OSIReporter_Moving.cpp is compiled INTO the upstream ScenarioEngine module
 * (EnvironmentSimulator/Modules/ScenarioEngine/CMakeLists.txt swaps it in for
 * upstream's OSIReporter.cpp), while ControllerVirtualDriver lives in
 * GT_esminiLib, which LINKS ScenarioEngine. The dependency therefore only runs
 * one way: the reporter cannot include or call anything from the controller.
 * This registry inverts it -- the controller (upper layer) PUBLISHES, the
 * reporter (lower layer) READS -- with the storage defined inside
 * GT_OSIReporter_Moving.cpp so no core build file has to change (R1).
 *
 * SCOPE -- EGO/HOST ONLY, DELIBERATELY
 * ------------------------------------
 * osi3 MovingObject.future_trajectory is a GroundTruth-side field; the standard
 * says it "should not be made available to the stack under test", and this
 * project's own knowledge graph (capability_model.md section on 他車の軌道予測,
 * function_catalog_vd_ad.yaml) forbids routing OTHER vehicles' predictions into
 * it -- that would record a driving AI's guesses as world truth. Publishing the
 * EGO's own planned path is a different thing: the planner is what actually
 * drives the ego, so its plan is the best available statement of where the ego
 * will be, and it is the host's own row. Do not generalise this to traffic.
 */
#pragma once

#include <vector>

namespace gt_esmini
{

// One sample of a planner's own path. Mirrors what osi3 StatePoint carries.
struct PlannedPathPoint
{
    double t = 0.0;  // seconds AHEAD of the publishing frame (0 = now)
    double x = 0.0;  // world position [m]
    double y = 0.0;
    double z = 0.0;
    double h = 0.0;  // yaw [rad]
    double p = 0.0;  // pitch [rad]
    double r = 0.0;  // roll [rad]
    double v = 0.0;  // planned speed at this point [m/s]
};

struct PlannedPath
{
    int                           object_id = -1;
    double                        stamp     = 0.0;  // sim time the path was published at [s]
    std::vector<PlannedPathPoint> points;
};

// Per-object slot for "the path this object's own planner is tracking".
//
// Not thread safe by design: publish and read both happen on the single
// simulation step thread (controller Step -> OSI reporter Update), same as the
// rest of the entity state they read.
class PlannedPathRegistry
{
public:
    static PlannedPathRegistry& Instance();

    // Called by the planner-owning controller once per frame. Replaces any
    // previous path for the same object id.
    void Publish(const PlannedPath& path);

    // Returns the published path for object_id, or nullptr when there is none,
    // when it is older than max_age_s, or when it is stamped in the FUTURE
    // relative to sim_time. The future check is the scenario-reload guard: a
    // re-load restarts sim time at ~0 while the registry still holds paths
    // stamped at the previous run's end, and a stale line frozen from a
    // previous run is exactly the kind of silent instrument this project has
    // been bitten by before.
    const PlannedPath* Get(int object_id, double sim_time, double max_age_s = 0.25) const;

    // A consumer flips this on when it is actually going to read the registry.
    // Publishers check it to skip the extra route walk that building a long
    // path costs when nothing will look at the result. Starts false, so the
    // very first frame after a consumer appears publishes nothing -- harmless,
    // the reporter falls back to its own projection for that one frame.
    void SetConsumerActive(bool active);
    bool IsConsumerActive() const;

    void Clear();

private:
    PlannedPathRegistry() = default;

    std::vector<PlannedPath> paths_;  // one entry per publishing object; N is tiny
    bool                     consumer_active_ = false;
};

}  // namespace gt_esmini
