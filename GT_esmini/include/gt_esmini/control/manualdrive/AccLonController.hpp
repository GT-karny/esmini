#pragma once

// req-vd-ad:REQ-AD-026 / req-vd-ad:REQ-AD-031 / vd-func:FUNC-079
//
// AccLonController -- the GENERATE stage of design §3-1's three-stage
// longitudinal arbitration (ACC generate -> MSL limit -> AEB safety), plus the
// ACC state machine (§4-1), the speed control (§4-2) and Stop&Go (§4-3).
//
// Pure logic: no esmini, no OSI, no controller state -- same convention as
// PedalArbitrator / KickdownDetector / AdSteeringEnvelope. Everything that
// carries a decision lives here so it is unit-testable without the engine; the
// engine-facing wiring (policy evaluation, ownership, reporting) stays in
// AdasCoexistenceStack.
//
// ============================================================================
// WHAT THIS DOES **NOT** REUSE FROM VirtualDriver, AND WHY
// ============================================================================
// VD's longitudinal stack is three layers (ManeuverAwareSpeedPlanner ->
// TrajectoryShortPlanner -> PIDPurePursuitDriver) built around FOLLOWING A
// ROUTE: the mid/long planner produces a v_target(s) PROFILE over route
// coordinates. ManualDrive has no route -- the human decides where to go --
// so a profile over route-s has no consumer here. §4-2 therefore evaluates the
// policy ceiling AT THE CURRENT POSITION ONLY (one point, EvaluateAccCeiling
// below) and closes the loop with a speed PID.
//
// `comfort_decel` is NOT read from anywhere in the VD config (design §4-2,
// §12): that number means "how smoothly the VD chooses to slow itself down",
// and there is no argument that carries it over to a car a human is driving.
// decel_max_mps2 below is ACC's own, and is what the kinematic ceiling
// (sqrt(2*a*d)) is computed against, so the two numbers stay traceable to one
// decision each.
//
// ============================================================================
// STATE MACHINE (design §4-1) -- three states, mapped onto the OSI 3-value
// discipline by BuildManualAdasFunctionReport: OFF -> UNAVAILABLE,
// STANDBY -> STANDBY, ACTIVE -> ACTIVE.
// ============================================================================
//
//   OFF     --toggle-->           STANDBY
//   STANDBY --set-->              ACTIVE      (set speed := current speed, real-car standard)
//   ACTIVE  --brake-->            STANDBY     (cancel; the set speed is REMEMBERED)
//   ACTIVE  --speed out of band-> STANDBY     (REQ-AD-026 step f)
//   STANDBY --resume-->           ACTIVE      (restores the remembered set speed)
//   ANY     --toggle-->           OFF
//
// SET and RESUME are the SAME BUTTON (ACC_SET_RESUME), exactly as on a real
// stalk: from STANDBY-with-no-remembered-setting it SETs to the current speed,
// from STANDBY-with-a-remembered-setting it RESUMEs that setting. `set_speed`
// surviving a cancel is what makes REQ-AD-026 step b's "resume まで再介入しない
// / resume で直前の設定速度へ復帰" observable as two different facts (the
// function stopped intervening; the setting was not forgotten).
//
// A TEMPORARY ACCELERATOR OVERRIDE IS NOT A STATE TRANSITION (§3-1, §4-1). The
// human pressing the accelerator past accel_override_threshold while ACTIVE
// keeps their own throttle and suppresses ACC's brake generation, but the
// state stays ACTIVE and releasing the pedal returns to following with no
// resume needed. Modelling it as a transition would make the HVD state column
// flicker with the driver's foot and would require a resume the real function
// does not ask for.
//
// ============================================================================
// STOP&GO (REQ-AD-031 段a/b, design §4-3)
// ============================================================================
// Stopping is not a separate controller: the ceiling from a STOP_AT_S
// constraint decays to 0 as the stop point approaches (EvaluateAccCeiling),
// so the ordinary speed loop drives the car to a halt. What IS separate is
// the HOLD: once stopped with a stop request still standing, the loop is
// replaced by a fixed hold brake, because RealVehicle models an automatic
// transmission and CREEPS -- releasing the brake at v=0 moves the car
// (design §12). The hold releases ONLY on the human's accelerator
// (restart_accel_threshold):段a defines restart as a human trigger, and this
// controller never decides to restart by itself (段c/d are explicitly future
// work).
//
// hold_brake REQUIRES CALIBRATION against the creep torque -- see the field.
//
// ============================================================================
// WHAT IS OBSERVABLE, AND THE "SET vs EFFECTIVE" SPLIT (REQ-AD-026 e/g/h)
// ============================================================================
// AccFrameOutput carries set_speed_mps and effective_cap_mps as two separate
// numbers, and thw_setting_s / thw_actual_s likewise. That split is the whole
// judgment basis for steps e/g/h: "the driver changed the setting" and "the
// change reached the vehicle" are different claims, and a matcher that could
// only see one of them would pass on a controller that stored the setting and
// ignored it. AdasCoexistenceStack emits all four as gt.acc.* custom_detail.

#include "gt_esmini/control/common/VehicleCommand.hpp"  // ButtonBits (DecodeAdasOperations)

#include <cstdint>
#include <limits>

namespace gt_esmini
{

// ---------------------------------------------------------------------------
// Operating controls
// ---------------------------------------------------------------------------

// One frame's decoded ADAS button EDGES (see ButtonBits' phase-C block for why
// every one of these is an edge and not a level).
struct AdasOperations
{
    bool acc_toggle     = false;
    bool acc_set_resume = false;
    bool acc_speed_up   = false;
    bool acc_speed_down = false;
    bool acc_thw_cycle  = false;
    bool msl_toggle     = false;
};

// Rising edges of the phase-C ButtonBits between `prev_buttons` and `buttons`.
AdasOperations DecodeAdasOperations(std::uint32_t buttons, std::uint32_t prev_buttons);

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------

// Following-distance stages (design §4-1 "段階式 THW、既定 3 段階"). Three FLAT
// fields rather than the design sketch's JSON array: ManualDriveConfig::
// LoadFromFile is a line-wise scanner, not a JSON parser (see that header's
// PARSER NOTE), so an array literal cannot be read from the config file at all.
// Three named stages keep the on-disk keys flat and unique
// (adas_acc_thw_stage_{short,mid,long}_s) at the cost of fixing the count at 3,
// which is the count the design specifies anyway.
//
// REQUIRES CALIBRATION (verification plan §5): 1.0 / 1.6 / 2.2 s are the
// customary three-stage figures quoted for production ACC, carried over from
// the requirement's own note -- not measured here.
struct AccThwStages
{
    double short_s = 1.0;
    double mid_s   = 1.6;
    double long_s  = 2.2;

    // Stage index 0/1/2 -> seconds. Any other index clamps into range rather
    // than reading out of bounds; the caller's cycle arithmetic is modulo 3.
    double AtStage(int stage) const;

    static constexpr int kStageCount = 3;
};

struct AccStopAndGoConfig
{
    bool enabled = true;

    // Which policies count as stop targets (REQ-AD-031 段b). The LEAD vehicle
    // is always a stop target when Stop&Go is on -- that is 段a and needs no
    // switch. These two add the 段b targets, and AdasCoexistenceStack honours
    // them by ADDING (or not adding) TrafficLightAware / StopYieldSignAware to
    // the manual policy stack: a policy that was never instantiated cannot
    // emit a constraint, so the negative direction of
    // md-sng-target-config-polarity is structural rather than a filter that
    // could be bypassed.
    bool stop_at_traffic_light = false;
    bool stop_at_stop_sign     = false;

    // Accelerator fraction that releases the hold and resumes following
    // (段a: the human is the ONLY restart trigger). REQUIRES CALIBRATION:
    // 0.10 is picked to sit above pedal noise and below any deliberate press,
    // not measured.
    double restart_accel_threshold = 0.10;

    // Brake fraction held while stopped, against RealVehicle's automatic-
    // transmission creep torque (design §12): too low and the car creeps out of
    // its own stop, too high and the release is a lurch.
    //
    // MEASURED, 2026-08-05 (manualdrive_creep_stop_hold_2026-08-05.md): at 0.30
    // the vehicle travelled 0.015-0.032 m over hold windows of 3.2 s / 11.6 s /
    // 18.7 s in three separate runs -- i.e. the creep is fully suppressed and
    // the residual is at the level of position noise, not a slow roll.
    double hold_brake = 0.30;

    // Speed at/below which the vehicle counts as stopped for hold entry.
    //
    // 0.5 m/s (1.8 km/h) IS NOT A ROUNDING CHOICE, IT IS ABOVE THE CREEP FLOOR.
    // RealVehicle's automatic transmission creeps: with no brake applied the
    // vehicle does not converge to zero, it converges to ~0.16 m/s (measured,
    // same record). A threshold below that floor can NEVER be crossed, so the
    // hold would never engage -- and the failure is silent in the worst way,
    // because everything upstream looks right: the function is ACTIVE, the
    // ceiling has decayed to zero, the car has visibly stopped following, and
    // gt.acc.stop_hold simply stays false forever. The first phase-C probe run
    // did exactly this with the pre-measurement default of 0.10 (0 hold frames
    // in 640). Any recalibration of this number must re-measure the creep floor
    // first; a value under it turns Stop&Go into dead code.
    double stop_speed_eps_mps = 0.5;
};

struct AccLonControllerConfig
{
    bool enabled = false;  // design §9: every ADAS function ships default OFF

    // Setting adjustment step (REQ-AD-026 step e). 1.39 m/s ~= 5 km/h, the
    // customary stalk increment.
    double set_speed_step_mps = 1.39;

    AccThwStages thw_stages;
    int          thw_default_stage = 1;  // "mid"

    // Availability speed band (REQ-AD-026 step f). max_speed_mps <= 0 means
    // "no upper bound"; min_speed_mps 0 means "down to standstill", which is
    // the correct default for an ACC that includes Stop&Go.
    double min_speed_mps = 0.0;
    double max_speed_mps = 0.0;

    // REQ-AD-026 step g. When true the effective cap additionally honours the
    // road's own speed limit (same GetSpeedLimit() route the VD overtake path
    // uses, REQ-AD-023).
    bool respect_speed_limit = false;

    // ACC's OWN comfort envelope -- deliberately not VD's comfort_decel (see
    // this header's top block). decel_max_mps2 is also the deceleration the
    // kinematic stop ceiling is computed against, so one number governs both
    // "how hard will it slow" and "how early will it start".
    // REQUIRES CALIBRATION (verification plan §5).
    double accel_max_mps2 = 1.2;
    double decel_max_mps2 = 2.0;

    // ==================================================================
    // Pedal references -- what turns the envelope above into a REAL limit
    // ==================================================================
    // The speed loop works in the ACCELERATION domain and converts its
    // command to a pedal by dividing by these. That indirection is the whole
    // reason accel_max/decel_max mean anything: a loop that computed a pedal
    // DIRECTLY from the speed error saturates at full authority the moment
    // the error is large, and the "comfort envelope" above becomes a config
    // knob that changes nothing.
    //
    // This was measured, not reasoned: the first phase-C run of
    // md_acc_aeb_independence had the lead brake at 8 m/s^2 from 35 m -- far
    // outside a 2.0 m/s^2 budget -- and ACC absorbed the entire event with
    // gt.aeb never leaving STANDBY, because its brake command had saturated
    // at 1.0. The scenario claimed to demonstrate that safety wins and never
    // reached the safety stage at all.
    //
    // REQUIRES CALIBRATION (verification plan §5). full_brake_decel_mps2
    // mirrors PedalArbitratorConfig's own value (a textbook full-ABS
    // dry-pavement figure) and full_throttle_accel_mps2 is a rough
    // passenger-car figure; neither is measured against RealVehicleBackend.
    // A wrong value here scales the pedal, so it degrades how closely ACC
    // tracks its own envelope -- it does NOT reopen the saturation hole,
    // because the clamp is applied in the acceleration domain first.
    double full_brake_decel_mps2    = 8.0;
    double full_throttle_accel_mps2 = 3.0;

    // Speed loop (design §4-2: "PIDPurePursuitDriver の縦ブロックを参考に独立
    // 実装"), in the ACCELERATION domain. Units: kp is (m/s^2) per (m/s) of
    // speed error, i.e. 1/s; ki is (m/s^2) per (m/s * s), i.e. 1/s^2.
    // REQUIRES CALIBRATION -- placeholders that give the loop a visibly
    // converging shape against RealVehicleBackend, not values measured
    // against a reference vehicle.
    double speed_kp = 0.45;
    double speed_ki = 0.12;

    // Deadband on the speed error, in m/s, inside which neither pedal is
    // commanded. Without it the loop chatters between a trickle of throttle
    // and a trickle of brake at the target speed, which is both unpleasant and
    // noisy in the telemetry the matchers read.
    double speed_deadband_mps = 0.20;

    // Driver-input thresholds (design §3-1/§4-1). accel: temporary override
    // (ACTIVE is retained). brake: cancel (ACTIVE -> STANDBY).
    double accel_override_threshold = 0.05;
    double brake_cancel_threshold   = 0.05;

    AccStopAndGoConfig stop_and_go;
};

// ---------------------------------------------------------------------------
// Policy ceiling (design §4-2)
// ---------------------------------------------------------------------------

// "No ceiling" sentinel. Infinity rather than a large finite number so a
// min() chain is exact and a caller can test for it unambiguously.
constexpr double kAccNoCeiling = std::numeric_limits<double>::infinity();

struct AccCeiling
{
    // Speed ceiling implied by the policy constraints at the CURRENT position
    // (kAccNoCeiling when nothing constrains).
    double ceiling_mps = kAccNoCeiling;
    // A stop target is standing (some STOP_AT_S constraint qualified). This is
    // what lets the hold engage once the vehicle has actually stopped: without
    // it, "stopped at 0 m/s" and "stopped because something is in the way"
    // would be the same observation and the hold would latch at every red
    // light the driver stopped at by themselves.
    bool   stop_requested = false;
    // Distance to the nearest qualifying stop point [m] (only meaningful when
    // stop_requested).
    double stop_distance_m = 0.0;
};

}  // namespace gt_esmini

// PolicyConstraint lives in the VirtualDriver types header; including it here
// (rather than forward-declaring) is required because EvaluateAccCeiling takes
// the constraint vector by reference and the enum values are used inline.
#include "gt_esmini/control/virtualdriver/VirtualDriverTypes.hpp"

#include <vector>

namespace gt_esmini
{

// Folds one frame's policy constraints into a single-point speed ceiling.
//
//   * MAX_SPEED and MAX_SPEED_TO_S contribute their `value` directly. A
//     MAX_SPEED_TO_S is honoured AS IF it applied here and now: this is a
//     one-point evaluation with no route profile to defer it along (see this
//     header's top block), and honouring it early is the conservative reading.
//   * STOP_AT_S contributes sqrt(2 * decel_max * max(0, s)) -- the fastest
//     speed from which `decel_max` still stops the car in the remaining
//     distance -- and raises stop_requested.
//   * SAFETY-tier constraints are SKIPPED ENTIRELY. AEB is the safety stage
//     and runs after this one (design §3-1); folding its stop request into
//     ACC's ceiling as well would mean two stages independently reacting to
//     the same demand, and would make REQ-AD-026 step d's claim ("AEB fires
//     INDEPENDENTLY while ACC is active, and safety wins") untestable -- the
//     two would no longer be separable in the output.
AccCeiling EvaluateAccCeiling(const std::vector<PolicyConstraint>& constraints, double decel_max_mps2);

// ---------------------------------------------------------------------------
// Per-frame I/O
// ---------------------------------------------------------------------------

enum class AccState
{
    OFF,
    STANDBY,
    ACTIVE
};

struct AccFrameInput
{
    AdasOperations ops;

    double driver_throttle = 0.0;  // [0,1]
    double driver_brake    = 0.0;  // [0,1]
    double ego_speed_mps   = 0.0;

    AccCeiling policy;

    // Road speed limit [m/s]; <= 0 means "unknown / not available", in which
    // case respect_speed_limit has nothing to apply and the cap falls back to
    // the set speed. A 0 limit is never treated as "stop": an unknown limit
    // must not silently become the strictest constraint in the min() chain.
    double speed_limit_mps = 0.0;

    // Measured time headway to the lead [s], or < 0 when there is no lead.
    // Reported, never used for control (the gap is maintained by
    // LeadVehicleAware's own constraint) -- this is the "effective" half of
    // REQ-AD-026 step h's setting-vs-effect split.
    double thw_actual_s = -1.0;

    // True while another ADAS function has claimed the longitudinal domain and
    // demoted ACC (design §6's ACC/MSL exclusivity). A suspended ACC reports
    // STANDBY and produces no output, but keeps its set speed and stage --
    // "demoted", not "switched off".
    bool suspended = false;
};

struct AccFrameOutput
{
    AccState state = AccState::OFF;

    // Pedals this stage proposes. When !engaged these are the driver's own
    // values, passed through unchanged -- the generate stage is a no-op, not
    // a zeroing.
    double throttle = 0.0;
    double brake    = 0.0;

    // ACC actually generated this frame's pedals (state ACTIVE, not suspended,
    // not accelerator-overridden). Distinct from `state == ACTIVE` because an
    // accelerator override keeps the state ACTIVE while handing the pedals
    // back to the human -- see this header's state-machine block.
    bool engaged = false;

    // Stop&Go hold is applied (段a). Implies engaged.
    bool stop_hold = false;

    // req-vd-ad:REQ-AD-028 段b producers (design §8-3). Both track the DRIVER'S
    // INPUT, not the arbitration outcome of one frame -- the same choice phase
    // B made for the AEB/kickdown override and for the same reason (an
    // override channel that blinks with the traffic situation stops reporting
    // what the driver did).
    //   * driver_override_brake: the brake pedal is holding ACC off -- set on
    //     the brake cancel and held until the pedal comes back under the
    //     threshold. -> REASON_BRAKE_PEDAL.
    //   * driver_override_accel: the accelerator is overriding ACC's own
    //     generation while ACTIVE. -> custom_state DRIVER_OVERRIDE_ACCEL
    //     (OSI's Reason enum has no accelerator value).
    bool driver_override_brake = false;
    bool driver_override_accel = false;

    // The four observables REQ-AD-026 e/g/h judge on (see this header's last
    // block). effective_cap_mps is what the loop actually chased this frame:
    // min(set_speed, policy ceiling, speed limit when respected).
    double set_speed_mps      = 0.0;
    double effective_cap_mps  = 0.0;
    double thw_setting_s      = 0.0;
    double thw_actual_s       = -1.0;
    int    thw_stage          = 0;
};

// ---------------------------------------------------------------------------
// The controller
// ---------------------------------------------------------------------------

class AccLonController
{
public:
    explicit AccLonController(const AccLonControllerConfig& cfg = {});

    // One frame. Returns the generate stage's proposal; the caller feeds
    // output.throttle/brake into the MSL limit stage and then into
    // PedalArbitrator's safety stage (design §3-1's fixed order).
    //
    // dt <= 0 freezes the integrator (paused sim / first frame) exactly like
    // PedalArbitrator's own guard, but still evaluates the state machine: a
    // button press on a zero-length frame is still a button press.
    AccFrameOutput Step(const AccFrameInput& in, double dt);

    AccState State() const { return state_; }
    double   SetSpeed() const { return set_speed_mps_; }
    int      ThwStage() const { return thw_stage_; }

    // True while the THW stage or the set speed has EVER been changed by an
    // operation on this instance. Exposed for the setting_reflected matcher's
    // structural precondition (verification plan §4-2: "切替が1回も起きなければ
    // 構造的に赤") -- the run's own detail stream carries it so a matcher
    // cannot mistake a constant field for a satisfied claim.
    bool SettingEverChanged() const { return setting_ever_changed_; }

    const AccLonControllerConfig& Config() const { return cfg_; }

private:
    // Applies the operation edges to the state machine. Split out so the state
    // transitions can be read (and tested) without the pedal maths.
    void ApplyOperations(const AccFrameInput& in);

    AccLonControllerConfig cfg_;

    AccState state_          = AccState::OFF;
    double   set_speed_mps_  = 0.0;
    bool     has_set_speed_  = false;  // a setting exists to RESUME to
    int      thw_stage_      = 1;
    double   integral_       = 0.0;    // speed-loop integrator
    bool     stop_hold_      = false;
    bool     brake_override_latched_ = false;
    bool     setting_ever_changed_   = false;
};

}  // namespace gt_esmini
