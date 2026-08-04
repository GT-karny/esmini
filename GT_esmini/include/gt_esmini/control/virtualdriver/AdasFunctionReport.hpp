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
//
// req-vd-ad:REQ-AD-025 / REQ-AD-028 (ManualDrive ADAS phase A/B) add the 4 FCW/
// LDW/LKA/MSL values below (design doc manualdrive_adas_design.md §8-2). Values
// cross-checked against test/unit/realdriver/test_AdasSlotTable.cpp, which pins
// the same OSI 3.7.0 enum independently for the RealDriver 24-slot path. As with
// the pre-existing 3 values, the coordinator pins these against the real .proto
// with static_assert in GT_esminiLib.cpp (control must not depend on osi).
namespace osi_adas
{
enum Name
{
    NAME_OTHER                       = 1,
    NAME_FORWARD_COLLISION_WARNING   = 3,   // vd-func:FUNC-075 FCW (phase A)
    NAME_LANE_DEPARTURE_WARNING      = 4,   // vd-func:FUNC-080 LDW (phase D)
    NAME_AUTOMATIC_EMERGENCY_BRAKING = 7,   // vd-func:FUNC-075 AEB (phase A)
    NAME_ADAPTIVE_CRUISE_CONTROL     = 10,
    NAME_LANE_KEEPING_ASSIST         = 11,  // vd-func:FUNC-080 LKA (phase D)
    NAME_URBAN_DRIVING               = 22,
    NAME_SPEED_LIMIT_CONTROL         = 25,  // vd-func:FUNC-081 MSL (phase C)
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

// ============================================================================
// ManualDrive ADAS coexistence stack (req-vd-ad:REQ-AD-025 REQ-AD-028,
// vd-func:FUNC-075). Phase A wires only AEB + its FCW warning pre-stage
// (design doc manualdrive_adas_design.md §10 phase table); ACC/LKA/MSL land
// in phases C/D and are deliberately NOT declared here yet.
// ============================================================================

// Which manual-stack functions are configured ON (config, not domain
// ownership -- see owns_longitudinal_domain below). Mirrors
// VdPolicyEnableFlags' UNAVAILABLE-vs-STANDBY discipline: a function that was
// never enabled can never fire, and "switched off" must stay distinguishable
// from "watching and chose not to fire" (REQ-AD-028 step a).
//
// `fcw` is declared separately from `aeb` even though phase A's config skeleton
// (design §9) only exposes a single `adas.aeb.enabled` key: FCW is the warning
// PRE-STAGE built from AebSafety's own output (design §3-2, same policy, looser
// threshold), not a separately-instantiated policy, but the report-building
// function should not assume the two are always co-enabled -- the caller
// (AdasCoexistenceStack) decides how config maps onto these two booleans, this
// function only consumes the result.
struct ManualAdasEnableFlags
{
    bool aeb = false;
    bool fcw = false;
};

// Per-frame decisions the manual stack (AdasCoexistenceStack / AebSafety) has
// already computed. Deliberately booleans, not a TrafficPolicySnapshot search
// by PolicyConstraint::source string like BuildAdasFunctionReport() above:
// ManualDrive calls AebSafety::Evaluate() directly (design §2-1 "policy bodies
// are reused with zero modification") and already knows whether this frame
// intervened / warned, so re-deriving that from a source-string search would
// just be a slower, more roundabout way to read the same fact the caller
// already holds. (A source-string mechanism would also be a worse fit for FCW,
// which is not itself a PolicyConstraint emitter -- see design §3-2.)
struct ManualAdasDecision
{
    bool aeb_intervening = false;  // AEB safety stage fired this frame (brake authority raised)
    bool fcw_warning     = false;  // FCW pre-stage: TTC below the (looser) warning threshold
};

// Projects one frame of the ManualDrive ADAS stack onto the OSI AD-function
// rows. Pure: no engine, no OSI, no controller state.
//
// `owns_longitudinal_domain`: design §2-3's split-configuration rule. AEB and
// FCW are both longitudinal-domain functions; in a split configuration where
// VirtualDriver owns LONGITUDINAL (DomainOwnershipLedger::OwnerOf), ManualDrive
// must not double-equip -- both rows report UNAVAILABLE regardless of the
// config flags above, exactly like a config-disabled function (slug
// md-split-no-double-equipment). This is intentionally a single flag for phase
// A: both functions this phase implements share one domain.
//
// NO aggregate row: unlike BuildAdasFunctionReport()'s "gt.virtual_driver" /
// NAME_URBAN_DRIVING row (§8-1), a manual-context report must NOT claim "an
// automated driving function has control" as a whole-stack summary -- the
// human is driving. Emitting that row here would misreport who is in control
// to any face-3 consumer that trusts it, so this function omits it entirely
// rather than trying to redefine its meaning.
std::vector<AdasFunctionState> BuildManualAdasFunctionReport(const ManualAdasEnableFlags& flags,
                                                              bool                         owns_longitudinal_domain,
                                                              const ManualAdasDecision&    decision,
                                                              const PolicyDetail&          detail);

}  // namespace gt_esmini
