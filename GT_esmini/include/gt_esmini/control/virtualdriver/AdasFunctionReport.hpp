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

// Mirrors osi3::HostVehicleData_VehicleAutomatedDrivingFunction_DriverOverride_
// Reason (req-vd-ad:REQ-AD-028 段b, design §8-3). The enum has EXACTLY TWO
// values in OSI 3.7.0 -- brake pedal and steering input. There is NO
// accelerator value; that is a standard-level constraint, not an omission
// here, and it is why accelerator-origin overrides (AEB kickdown suppression,
// MSL cap release, ACC temporary override) are reported through `custom_state`
// (kDriverOverrideAccel below) instead of a third Reason. Pinned against the
// real proto by static_assert in GT_esminiLib.cpp, same as Name/State above.
enum OverrideReason
{
    REASON_BRAKE_PEDAL    = 0,
    REASON_STEERING_INPUT = 1,
};
}  // namespace osi_adas

// custom_state token for the accelerator-origin driver override (design §8-3's
// third bullet). A STRING token rather than an enum value precisely because
// OSI has no enum slot for it -- see osi_adas::OverrideReason's comment. The
// exact spelling is part of the observation contract: verification matchers
// (driver_override_reported) compare against this literal, so changing it is a
// breaking change to face-3 assets, not an internal rename.
inline constexpr const char* kDriverOverrideAccel = "DRIVER_OVERRIDE_ACCEL";

// One row's DriverOverride submessage (req-vd-ad:REQ-AD-028 段b).
//
// `reported` is NOT redundant with `active`. It distinguishes "this row's
// override channel was evaluated this frame and found nothing" (reported=true,
// active=false) from "nothing ever populated this channel" (reported=false) --
// the same absent-key-is-not-zero discipline AdasCoexistenceStack.hpp applies
// to `detail` on a bypassed frame, and the same STANDBY-vs-UNAVAILABLE
// distinction REQ-AD-028 段a makes for State. A consumer cannot evidence "the
// driver did not override" from a channel that was never written, so
// reported=false must reach OSI as an ABSENT submessage (GT_HostVehicleReporter
// only emits driver_override when reported is true), never as an explicit
// active=false that would look like a measurement.
//
// This is also what keeps every non-ManualDrive caller byte-identical: the
// RealDriver 24-slot rows and the VirtualDriver rows leave this default-
// constructed, so their serialized HVD is unchanged by phase B.
struct AdasDriverOverride
{
    bool             reported = false;  // false -> do not emit the submessage at all
    bool             active   = false;
    std::vector<int> reasons;           // osi_adas::OverrideReason values
};

// One row of vehicle_automated_driving_function[].
struct AdasFunctionState
{
    int          name  = osi_adas::NAME_OTHER;
    std::string  custom_name;  // always set — the only label for NAME_OTHER rows
    int          state = osi_adas::STATE_UNAVAILABLE;
    PolicyDetail detail;       // -> custom_detail (KeyValuePair), see PolicyDetail.hpp

    // req-vd-ad:REQ-AD-028 段b (phase B). Empty custom_state = field not set
    // on the wire (OSI `optional string custom_state`); only ManualDrive's
    // accelerator-origin override writes it today (kDriverOverrideAccel).
    AdasDriverOverride driver_override;
    std::string        custom_state;
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

    // req-vd-ad:REQ-AD-028 段b (phase B) -- the accelerator-origin driver
    // override, i.e. "the human's floored accelerator is currently holding
    // AEB off" (design §3-2/§8-3). True while the shared KickdownDetector is
    // latched AND kickdown suppression is configured on, NOT merely while AEB
    // happened to be suppressed on this particular frame.
    //
    // WHY THE WIDER CONDITION: OSI's DriverOverride asks "has the driver
    // overridden this FUNCTION", which is a property of the function's
    // availability, not of one frame's arbitration outcome. AEB is genuinely
    // overridden -- it cannot intervene -- for as long as the kickdown holds,
    // whether or not a target happens to be in front right now. Reporting
    // only the frames where a request was actually vetoed
    // (PedalArbitrationSnapshot::aeb_suppressed) would make the override
    // channel blink on and off with the traffic situation rather than track
    // the driver's input, and the narrower fact remains separately observable
    // anyway as the gt.aeb.suppressed custom_detail key. Neither fact is lost;
    // they are reported in the two places whose semantics each one fits.
    bool driver_override_accel = false;
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
// DRIVER OVERRIDE (req-vd-ad:REQ-AD-028 段b, phase B). Every row whose gate is
// OPEN (config-enabled AND owning the domain, i.e. the row is not UNAVAILABLE)
// gets driver_override.reported = true, because on such a frame the manual
// stack really did evaluate the override question -- an answer of "no
// override" is then a measurement, not silence. Rows behind a closed gate
// (config off / domain not owned) leave it default (reported=false), matching
// the empty-not-zeroed `detail` rule AdasCoexistenceStack applies to the same
// bypass: a function that was never running cannot have been overridden.
//
// Only the AEB row carries the accelerator override (decision.
// driver_override_accel -> active=true + custom_state=kDriverOverrideAccel).
// FCW does not: kickdown suppresses INTERVENTION, and suppressing the warning
// as well would remove the driver's last cue precisely when they are
// accelerating toward a hazard. The FCW row therefore reports an evaluated-
// but-inactive override, which doubles as the in-run negative control for the
// driver_override_reported matcher (same frame, same run, one row active and
// one not).
//
// `reasons` stays EMPTY for the accelerator override -- see
// osi_adas::OverrideReason: OSI has only brake/steering values, and picking
// the "closest" one would misreport which pedal the human used. Brake-origin
// (ACC cancel, phase C) and steering-origin (LKA interrupt, phase D) overrides
// are the producers that will populate it; phase B builds the mechanism and
// exercises the accelerator path only.
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
