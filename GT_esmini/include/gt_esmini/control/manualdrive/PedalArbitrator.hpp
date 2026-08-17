#pragma once

// req-vd-ad:REQ-AD-025 / vd-func:FUNC-075
//
// PedalArbitrator -- the SAFETY stage of the 3-stage longitudinal pedal
// arbitration order fixed by design §3-1 (manualdrive_adas_design.md):
// ACC (generate) -> MSL (limit) -> AEB (safety). Safety runs LAST so that
// whatever the generate/limit stages produced, the safety claim survives
// into the final command.
//
// PHASE A IMPLEMENTS ONLY THE SAFETY (AEB) STAGE. ACC and MSL are phase C
// (design §10). This header deliberately has no interface/hook for them:
// their integration point is the INPUT to Arbitrate() -- a phase-C
// AccLonController and MSL clamp run BEFORE this stage and hand their
// throttle/brake result into driver_throttle/driver_brake below, exactly as
// the human pedals do today. Adding an empty "AccStage"/"MslStage" hook now
// would be a speculative dependency with no consumer; the real seam is
// already here, it just has one producer (the human) instead of three.
//
// Pure logic: no esmini, no OSI, no controller state -- same convention as
// AdSteeringEnvelope (control/virtualdriver/AdSteeringEnvelope.hpp) and
// FfbTargetServo (control/manualdrive/FfbTargetServo.hpp).
//
// ---------------------------------------------------------------------------
// SIGN CONVENTION -- read this before touching aeb_decel_mps2 or
// measured_decel_mps2. Both fields are DECELERATION MAGNITUDES: POSITIVE
// means "slowing down", regardless of the vehicle's direction of travel.
// This is the OPPOSITE convention from a signed longitudinal acceleration
// (where braking is negative accel and speeding up is positive accel) --
// a NEGATIVE value in either field means the vehicle is ACCELERATING, not
// braking gently. Concretely: aeb_decel_mps2 = -1.0 does NOT mean "brake
// lightly", it means "the vehicle is speeding up by 1 m/s^2 while AEB wants
// it to be slowing down" and must drive the brake command UP, not down.
// This project has already paid for exactly this class of unit/sign
// confusion once (see design §12's RealVehicleBackend HVD handle-angle
// note for a different concrete instance, and MEMORY's
// verification_semantics_lesson for the general pattern) -- getting it
// wrong here is silent (both fields are just `double`) and costly to
// diagnose after the fact, so it is pinned by a dedicated unit test
// (test_PedalArbitrator.cpp's sign-convention test) rather than left to
// review alone.
// ---------------------------------------------------------------------------

namespace gt_esmini
{

struct PedalArbitratorConfig
{
    // §3-4: requested deceleration -> brake fraction. The static open-loop
    // map is deliberately NOT used: the physics backend is swappable
    // (RealVehicleBackend / NetworkPhysicsBridge) and a static brake map is
    // wrong the moment the backend changes. Feedforward seeds the command,
    // a PI loop on (requested - measured) deceleration closes the rest.
    //
    // REQUIRES CALIBRATION (verification plan §5): 8.0 m/s^2 is a textbook
    // full-ABS dry-pavement figure for a passenger car, not a value measured
    // against RealVehicleBackend's actual brake model. It only sets the
    // feedforward SEED -- the PI loop closes whatever gap that seed leaves
    // against measured_decel_mps2, so a wrong value here degrades
    // convergence speed, not correctness of the eventual steady state.
    double full_brake_decel_mps2 = 8.0;   // [m/s^2], REQUIRES CALIBRATION

    // REQUIRES CALIBRATION: placeholders picked only to give the PI loop a
    // visibly-converging shape (proportional response + integral trim), not
    // tuned against any physics backend. Units: brake-fraction per (m/s^2
    // error) for kp, per (m/s^2 * s error) for ki.
    double brake_kp = 0.05;   // REQUIRES CALIBRATION
    double brake_ki = 0.6;    // REQUIRES CALIBRATION, [1/s]
};

struct PedalArbitrationInput
{
    double driver_throttle     = 0.0;   // [0,1]
    double driver_brake        = 0.0;   // [0,1]
    bool   aeb_requested       = false;
    // REQUIRED deceleration, POSITIVE when decelerating. See the sign
    // convention block above.
    double aeb_decel_mps2      = 0.0;
    // ACHIEVED deceleration, POSITIVE when decelerating. See the sign
    // convention block above.
    double measured_decel_mps2 = 0.0;
    bool   kickdown_active     = false;
};

struct PedalArbitrationSnapshot
{
    double throttle_out = 0.0;
    double brake_out    = 0.0;

    bool   aeb_engaged    = false;  // safety stage actually commanded braking this frame
    bool   aeb_suppressed = false;  // safety stage was skipped because of kickdown (§3-2)

    // Brake fraction the safety stage computed BEFORE composing with the
    // driver's own brake (0 when quiet or suppressed) -- exposed so a
    // caller/telemetry can see the raw safety demand independent of what
    // the human happened to be doing with the pedal.
    double aeb_brake_request = 0.0;

    // Human brake >= aeb_brake_request, i.e. the max() composition kept the
    // human value. See §3-1: "the human's strong brake is never weakened".
    bool   driver_brake_dominant = false;
};

class PedalArbitrator
{
public:
    explicit PedalArbitrator(const PedalArbitratorConfig& cfg = {});

    // Run one frame of the safety stage.
    //
    //   in.aeb_requested == false:
    //     pure pass-through (throttle_out=driver_throttle,
    //     brake_out=driver_brake); the PI integrator is reset (§3-4's
    //     closed loop must not carry a stale integral into the next
    //     engagement -- see ResetOnRelease in the test file).
    //
    //   in.aeb_requested && in.kickdown_active:
    //     pass-through, aeb_suppressed=true, aeb_engaged=false, integrator
    //     reset -- the real-car-style driver override (design §3-2).
    //
    //   otherwise (firing, not suppressed):
    //     aeb_brake_request = clamp(ff + PI, 0, 1), where
    //       ff = clamp(aeb_decel_mps2 / cfg.full_brake_decel_mps2, 0, 1)
    //     brake_out    = max(driver_brake, aeb_brake_request)  -- §3-1: the
    //                    human's brake is never weakened, only topped up.
    //     throttle_out = 0.0
    //     aeb_engaged  = true
    //     driver_brake_dominant = (driver_brake >= aeb_brake_request)
    //
    //     The PI term integrates (aeb_decel_mps2 - measured_decel_mps2) --
    //     mind the sign convention above -- ONLY while engaged (not
    //     suppressed/quiet), WITH ANTI-WINDUP: the integral must not grow
    //     further while the combined command is already saturated at 0 or 1,
    //     otherwise releasing/lowering the request would leave the command
    //     pinned high for many frames while the excess integral bleeds off.
    //
    //   dt <= 0 is a guard: the integrator must not accumulate on a
    //   non-positive time step (paused sim, first frame, caller edge case --
    //   same discipline as AdSteeringEnvelope's dt handling).
    PedalArbitrationSnapshot Arbitrate(const PedalArbitrationInput& in, double dt);

    // Drops the PI integrator to zero. Called internally whenever the safety
    // stage is not actively braking (quiet or suppressed) -- see Arbitrate's
    // doc above for why "reset on release" is part of the contract, not
    // just a convenience: without it, a stale integral from a PRIOR
    // engagement would corrupt the very first frame of the NEXT one.
    void Reset();

    const PedalArbitratorConfig& Config() const
    {
        return cfg_;
    }

private:
    PedalArbitratorConfig cfg_;
    double                integral_ = 0.0;  // PI integrator state, §3-4
};

}  // namespace gt_esmini
