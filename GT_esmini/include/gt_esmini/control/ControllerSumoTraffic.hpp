/*
 * GT_esmini - Extended esmini
 * https://github.com/esmini/esmini
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2026 GT_esmini contributors
 */

#pragma once

#include <set>
#include <string>
#include <unordered_map>

#include "Controller.hpp"
#include "VehiclePool.hpp"
#include "pugixml.hpp"

#include "gt_esmini/control/sumotraffic/SumoTransform.hpp"

// feature:F9 -- GT-side SUMO background traffic (experimental).
//
// The xosc-facing value carries a GT prefix on purpose. Controller selection
// goes through upstream's `esminiController` property key, and upstream already
// owns the value "SumoController" for a controller in the same domain with a
// different implementation. Picking the wrong one of the two produces no error
// at all -- the scenario runs, with the heading/reference-point defects of
// section 2 of the design doc silently back. C++ namespaces separate
// gt_esmini::ControllerSumoTraffic from scenarioengine::ControllerSumo; the xosc
// string has no namespace, so the distinction has to live in the value.
#define CONTROLLER_SUMO_TRAFFIC_TYPE_NAME "GTSumoTrafficController"

namespace gt_esmini
{
// User-range controller type id (USER_CONTROLLER_TYPE_BASE = 1000).
// RouteDrive = 1001, VirtualDriver = 1002, SumoTraffic = 1003.
constexpr int CONTROLLER_TYPE_SUMO_TRAFFIC = 1003;

/**
 * ControllerSumoTraffic: SUMO microscopic traffic as background traffic.
 *
 * Owns one in-process libsumo simulation, spawns an esmini entity per SUMO
 * vehicle, and (optionally) pushes the scenario's own vehicles into SUMO so
 * that SUMO's car-following reacts to them.
 *
 * Relative to upstream scenarioengine::ControllerSumo (which stays untouched,
 * R1) this fixes the four conversion defects measured in
 * GT_esmini/docs/features/sumo_background_traffic.md section 2 -- heading unit,
 * reference point, pitch sign, speed clipping -- plus two robustness items:
 * yaw is derived from the position history rather than from SUMO's angle, and
 * an "arrived" id only removes an entity when this controller is the one that
 * injected it (upstream lets SUMO's arrival logic delete scenario-defined
 * entities, including the Ego).
 *
 * Hosting: like upstream, the ScenarioObject carrying this controller is a
 * template, not a participant -- its 3D model is the fallback for spawned
 * vehicles. Upstream gets that for free because ScenarioReader special-cases
 * CONTROLLER_TYPE_SUMO and never adds the object to the entity list; a
 * user-range controller type gets no such treatment, so Init() deactivates the
 * host object itself (it stays alive as the template, but leaves the scene).
 * The same Init() self-activates the controller, for the same reason: the
 * reader's SUMO special case is what activates upstream's.
 */
class ControllerSumoTraffic : public scenarioengine::Controller
{
public:
    struct Config
    {
        // Naming the controller in a scenario IS the opt-in; this flag is the
        // kill switch, not a second opt-in. It defaults to the same value the
        // shipped sumo_traffic.json carries, so a missing config file behaves
        // like the shipped one instead of silently doing the opposite.
        bool        enabled          = true;
        std::string sumocfg;                  // fallback; the xosc <File filepath> wins
        int         seed            = 42;     // <= 0 : leave SUMO's own randomness alone
        double      step_length     = 0.05;   // <= 0 : leave the .sumocfg value alone
        bool        inject_ego      = true;   // push esmini entities into SUMO
        bool        override_heading = true;  // derive yaw from position history
        int         speed_mode      = 0;      // < 0 : do not call setSpeedMode at all
    };

    explicit ControllerSumoTraffic(InitArgs* args);
    ~ControllerSumoTraffic() override;

    const char* GetTypeName() const override
    {
        return CONTROLLER_SUMO_TRAFFIC_TYPE_NAME;
    }
    scenarioengine::Controller::Type GetType() const override
    {
        return static_cast<scenarioengine::Controller::Type>(CONTROLLER_TYPE_SUMO_TRAFFIC);
    }

    void Init() override;
    void Step(double timeStep) override;
    int  Activate(const ControlActivationMode (&mode)[static_cast<unsigned int>(ControlDomains::COUNT)]) override;

    const Config& GetConfig() const
    {
        return config_;
    }

private:
    void LoadConfig(const std::string& config_path);
    void ApplyProperties(scenarioengine::OSCProperties* properties);
    // Reads <net-file> out of the .sumocfg and its <location netOffset> out of
    // the net. Returns false when either file cannot be read.
    bool ReadNetOffset(const std::string& sumocfg_path);
    bool LoadSimulation(const std::string& sumocfg_path);

    void SpawnDepartedVehicles();
    void RemoveArrivedVehicles();
    void InjectScenarioVehicles();
    void UpdatePoses();

    // Pose of an esmini object expressed for SUMO (reference point -> front
    // bumper centre, heading -> navigational degrees, + netOffset).
    sumotraffic::SumoPose SumoPoseOf(const scenarioengine::Object* obj) const;

    // Push one esmini object's pose+speed into SUMO. Used both on injection and
    // on every subsequent step.
    void PushToSumo(scenarioengine::Object* obj);

    Config                            config_;
    bool                              loaded_      = false;
    double                            time_        = 0.0;
    sumotraffic::Vec2                 net_offset_;
    pugi::xml_document                doc_sumo_;
    scenarioengine::Object*           template_vehicle_ = nullptr;
    scenarioengine::VehiclePool       vehicle_pool_;
    EntityScaleMode                   scale_mode_ = EntityScaleMode::UNDEFINED;

    // Ids this controller injected INTO the scenario (i.e. SUMO-spawned). Only
    // these may be removed on a SUMO "arrived" event -- design doc section 3-7.
    std::set<std::string>             spawned_ids_;
    // Ids this controller injected INTO SUMO (i.e. scenario-defined entities).
    std::set<std::string>             pushed_ids_;

    // Per-vehicle history for the heading override (3-6). has_heading stays
    // false until the vehicle has moved far enough for a direction to mean
    // anything; until then the SUMO angle is used.
    struct TrackedPose
    {
        sumotraffic::Vec2 pos;
        double            heading     = 0.0;
        bool              has_pos     = false;
        bool              has_heading = false;
    };
    std::unordered_map<std::string, TrackedPose> tracked_;
};

scenarioengine::Controller* InstantiateControllerSumoTraffic(void* args);

}  // namespace gt_esmini
