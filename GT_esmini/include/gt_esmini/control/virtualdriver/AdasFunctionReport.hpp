#pragma once

#include "gt_esmini/control/virtualdriver/VirtualDriverTypes.hpp"

#include <string>
#include <vector>

// ============================================================================
// VirtualDriver -> OSI HostVehicleData.vehicle_automated_driving_function[]
// (capability_model §2.2 / W1).
//
// The VD stack had NO path into this field, so AEB (FUNC-001) — implemented and
// green — could not be observed from outside the process. OSI 3.7.0 has a
// first-class slot for exactly this; using it (rather than inventing a channel)
// is what keeps the observation interface comparable across AD stacks and
// preserves §0.2's contract that face3 observes face2 only through face1's OSI.
//
// This header stays OSI-free on purpose: it lives in `control`, which must not
// depend on `osi` (GT_esmini/CLAUDE.md §2). The enum values below MIRROR the
// .proto and are static_assert-ed against the real OSI enums where they are
// consumed (GT_HostVehicleReporter), so a drift in OSI breaks the build rather
// than silently mislabeling the stream.
// ============================================================================

namespace gt_esmini
{

// Mirrors osi3::HostVehicleData_VehicleAutomatedDrivingFunction_{Name,State}
// (osi_hostvehicledata.proto, OSI 3.7.0). Only the values GT_esmini emits.
namespace osi_adas
{
enum Name
{
    NAME_OTHER                       = 1,
    NAME_AUTOMATIC_EMERGENCY_BRAKING = 7,
    NAME_ADAPTIVE_CRUISE_CONTROL     = 10,
    NAME_URBAN_DRIVING               = 22,
};

enum State
{
    STATE_ERRORED     = 2,
    STATE_UNAVAILABLE = 3,
    STATE_AVAILABLE   = 4,
    STATE_STANDBY     = 5,
    STATE_ACTIVE      = 6,
};
}  // namespace osi_adas

// One row of vehicle_automated_driving_function[].
struct AdasFunctionState
{
    int          name  = osi_adas::NAME_OTHER;
    std::string  custom_name;  // always set — the only label for NAME_OTHER rows
    int          state = osi_adas::STATE_UNAVAILABLE;
    PolicyDetail detail;       // -> custom_detail (KeyValuePair), see PolicyDetail.hpp
};

// Which policies the controller actually instantiated (config flags). A policy
// that was never added can never fire, and "disabled" must be distinguishable
// from "enabled but quiet" — hence UNAVAILABLE vs STANDBY.
struct VdPolicyEnableFlags
{
    bool lead          = false;
    bool traffic_light = false;
    bool stop_yield    = false;
    bool conflict      = false;
    bool crosswalk     = false;
    bool aeb           = false;
};

// Projects one frame of VD policy output onto the OSI AD-function rows.
// Pure: no engine, no OSI, no controller state — so the mapping itself is unit
// testable, which is the part that carries the semantics.
std::vector<AdasFunctionState> BuildAdasFunctionReport(const VdPolicyEnableFlags&   flags,
                                                       const TrafficPolicySnapshot& snapshot);

}  // namespace gt_esmini
