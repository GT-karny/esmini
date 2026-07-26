#pragma once

#include <string>

namespace gt_esmini
{

struct ManualDriveConfig
{
    // Top-level type selection
    std::string input_type   = "sdl2_wheel";     // "sdl2_wheel", "network", "stub"
    std::string physics_type = "real_vehicle";    // "real_vehicle", "network"
    bool        ffb_enabled  = true;

    // Input: SDL2 wheel
    struct
    {
        int    device_index          = 0;
        double deadzone              = 0.05;
        int    upshift_button        = 4;
        int    downshift_button      = 5;
        int    override_button       = 0;
        int    indicator_left_button = 7;
        int    indicator_right_button = 6;
        int    headlight_button     = -1;  // -1 = unassigned
        int    high_beam_button     = -1;
        int    fog_light_button     = -1;
        int    hazard_button        = -1;
        int    auto_resume_button   = -1;  // feature:F7 — resume AD after manual override
    } sdl2;

    // Input: SDL2 keyboard
    // Key names are SDL scancode names ("A", "Space", "Left", "LShift", ...).
    // Empty string disables the binding.
    struct
    {
        std::string steer_left      = "A";
        std::string steer_right     = "D";
        std::string throttle        = "W";
        std::string brake           = "S";
        std::string clutch          = "LShift";
        std::string upshift         = "E";
        std::string downshift       = "Q";
        std::string override_key    = "O";
        std::string indicator_left  = "Z";
        std::string indicator_right = "X";
        std::string headlight       = "L";
        std::string high_beam       = "K";
        std::string fog_light       = "F";
        std::string hazard          = "H";

        double steer_rate         = 2.0;  // /s, full lock in 0.5 s
        double centering_rate     = 3.0;  // /s, return to center
        double pedal_press_rate   = 4.0;  // /s, full press in 0.25 s
        double pedal_release_rate = 6.0;  // /s
    } keyboard;

    // Indicator auto-cancel
    double indicator_cancel_angle = 0.06;  // normalized (~20 deg)

    // Input: Network bridge
    struct
    {
        std::string transport_type = "udp";  // "udp", "tcp"
        int         port           = 9100;
        std::string level          = "pedal_steer";  // "pedal_steer", "motion_request"
    } input_network;

    // Physics: RealVehicle
    struct
    {
        std::string vehicle_params_file = "real_vehicle_params.json";
    } real_vehicle;

    // Physics: Network bridge (external simulator)
    struct
    {
        std::string transport_type = "udp";
        std::string host           = "127.0.0.1";
        int         cmd_port       = 9200;
        int         state_port     = 9201;
    } physics_network;

    // FFB parameters (v5: physics-inspired model)
    struct
    {
        double sat_gain            = 0.08;  // reactive SAT strength (lat_accel -> force)
        double sat_centering_gain  = 1.50;  // caster trail centering (steering_angle -> force)
        double friction_base       = 0.12;  // static friction magnitude
        double friction_speed_gain = 0.04;  // additional friction proportional to speed
        double damper_base         = 0.02;  // low-speed damping coefficient
        double damper_speed_gain   = 0.06;  // additional damping proportional to speed
        double soft_stop_gain      = 0.5;   // resistance near steering lock
        double lock_angle          = 0.7;   // steering lock angle [rad]
        double assist_low_speed    = 0.90;  // power assist ratio at 0 m/s
        double assist_high_speed   = 0.20;  // power assist ratio at 30 m/s
        double max_force           = 1.0;   // output clamp [-1, 1]
        bool   disable_non_realtime = true;

        // feature:F7 (F7b) — FFB target-angle tracking (AD⇄手動 override).
        // When enabled, the FFB adds a PID servo term that drives the physical
        // wheel toward a target angle supplied by the AD stack (via
        // IFFBSink::SetSteerTarget). The commanded force + position error are
        // then read by OverrideManager as a torque-proxy intervention signal:
        // sustained push above the force/dev thresholds latches to MANUAL.
        // Default OFF so existing behavior (ManualDrive-only FFB) is unchanged.
        // Numbers from scripts/ffb_spike/README.md §1e/§2e (G29-calibrated,
        // NORMALIZED axis-fraction units — NOT radians).
        struct
        {
            bool   enabled                              = false;
            double kp                                   = 4.0;
            double kd                                   = 0.35;
            double max_force                            = 0.6;
            double hard_stop_zone                       = 0.85;
            // Coulomb friction feed-forward — removes the F_break/Kp static
            // friction deadband that otherwise swallows small AD steering
            // commands. MUST stay below the wheel's minimum breakaway force
            // (G29 measured 0.170) or the term could move the wheel by itself.
            // scripts/ffb_spike/CHARACTERIZATION.md §4/§7.
            double friction_ff                          = 0.15;
            double friction_ff_eps                      = 0.01;
            // Road-feel authority while the servo is active: SAT / friction /
            // damping are multiplied by this. 0 = the servo owns the wheel
            // (correct for a hands-off AD-driven wheel, and what the rig
            // measurements are calibrated against); 1 = legacy behaviour, where
            // the feel terms fight the servo and it reaches ~29% of a lane
            // change. Full feel always returns on override latch — that clears
            // target_active. CHARACTERIZATION.md §6/§8.
            double feel_ratio                           = 0.0;
            // OBSERVATIONAL ONLY since the residual rework (see
            // override_residual_threshold below). |u_feedback| and
            // |position_error| are still compared against these and reported
            // through FfbLatchDiagnostics / VirtualDriverTelemetry, because
            // they are the first two numbers a human wants when reading a
            // real-machine trace ("was the servo pushing at all? how far off
            // was the wheel?"). They no longer gate the latch: a threshold on
            // absolute force/deviation cannot separate "servo working hard
            // against a stiff plant" from "driver holding the wheel", which
            // is the whole reason the detector moved to a residual.
            double override_steer_force_threshold       = 0.20;
            double override_steer_dev_threshold         = 0.04;
            // Seconds the residual must stay above override_residual_threshold
            // before the lateral domain latches MANUAL.
            double override_sustain_time                = 0.10;  // seconds
            // OBSERVATIONAL ONLY since the residual rework. |d(target)/dt|
            // above this is reported as `moving_target` in the diagnostics
            // ("AD is actively steering right now"), which is the context a
            // human needs to read a trace — but it no longer suppresses
            // detection.
            //
            // History, kept because it is the reason the detector had to
            // change: this gate existed to stop the servo's own tracking lag
            // from being misread as a driver push while AD steers (real-
            // machine bug after a43e4c67). It worked, but it also blacked out
            // detection for the ENTIRE post-RESUME recovery ramp of the AD
            // steering safety envelope (93b2c6c4) — precisely the window in
            // which a driver is most likely to grab the wheel. The residual
            // detector is target-motion-invariant BY CONSTRUCTION (it
            // compares the wheel against a force-driven prediction of itself,
            // not against the target), so it needs no such gate and has no
            // such blackout.
            double override_target_rate_gate            = 0.30;  // axis-frac / s
            // OBSERVATIONAL ONLY since the residual rework. |d(position_error)
            // /dt| above this is reported as `tracking_transient` in the
            // diagnostics ("the servo is still catching up"). Formerly a
            // suppression gate for the startup transient (real-machine bug
            // after 549e5823); the residual detector handles that case
            // physically instead — at startup the wheel sits below breakaway,
            // so the shadow predicts no motion either and the residual stays
            // at zero.
            double override_position_error_rate_gate    = 0.10;  // axis-frac / s
            // ---- Residual-based intervention detection (feature:F7) -------
            //
            // WHAT REPLACED WHAT. The previous detector was DIRECTION-based:
            // it required the wheel to sit sign-opposed to the AD target, or
            // past it in magnitude ("wheel_engaged"). That is structurally
            // blind to the single most natural human reaction to an AD
            // steering command that is too aggressive — HOLDING THE WHEEL
            // SHORT OF IT. Same sign, |actual| < |target|: geometrically
            // indistinguishable from an obedient wheel, so it could never
            // fire, no matter how hard the driver pushed.
            //
            // The old comment here claimed the ambiguity was unresolvable
            // ("user firmly at 0" vs "wheel stuck at rest"). That claim is
            // WITHDRAWN — it was an artifact of only ever looking at
            // POSITION. Measurement (scripts/ffb_spike/CHARACTERIZATION.md,
            // real G29, 2026-07-25) shows the wheel's motion is a sharp,
            // deterministic function of the force applied to it:
            //   |f| <= 0.16          → displacement is EXACTLY zero
            //   |f| in 0.170..0.210  → breakaway (starts moving)
            //   moving               → v ≈ 3.35·(|f| − 0.16) axis-frac/s,
            //                          saturating at v_max ≈ 1.0 /s
            // So "where would this wheel be RIGHT NOW if nobody were
            // touching it?" is computable. Integrating that plant against
            // the effective force gives a shadow position; the difference
            // between the real axis and the shadow is physical evidence of
            // an external hand, and it does not care which DIRECTION that
            // hand pushes. "Stuck at rest" now predicts itself: below
            // breakaway the shadow does not move either, so the residual
            // stays zero and the detector stays quiet.
            //
            // Detection:  |actual − shadow| > residual_threshold, sustained
            //             for override_sustain_time  →  latch MANUAL.
            // Release is unchanged: AUTO_RESUME button only.
            //
            // Deliberate behaviour change: a driver gripping the wheel at 0
            // while AD steers away NOW LATCHES (it used to be documented as
            // undetectable). That is the desired safety behaviour.

            // Axis-fraction the real wheel must diverge from the shadow
            // prediction before the sustain clock runs. 0.08 ≈ 36° of a G29's
            // 900° range. Sized well above the plant model's own error floor
            // (SDL2 quantisation ~0.001, mechanical jitter ~0.005) and above
            // the residual drift the re-anchor below leaves standing, yet
            // small enough that a deliberate hand crosses it in well under
            // sustain time (a held wheel under a saturated 0.6 servo force
            // diverges at ~1.0 axis-frac/s → 0.08 in 80 ms).
            double override_residual_threshold          = 0.08;  // axis-fraction

            // Shadow drift control. The shadow is an INTEGRATOR, so any
            // mismatch between the model and the real plant accumulates
            // without bound and would eventually false-latch on a long drive.
            // While the residual is below threshold (i.e. no evidence of a
            // driver) and the domain is not latched, the shadow is pulled
            // back toward the measured axis with this first-order time
            // constant. Set 0 to re-anchor instantly.
            //
            // SIZING (this constant fights the detector — get it wrong and
            // real interventions stop firing). The leak rate at residual r is
            // r/tau, so the residual settles at r* = v_div · tau for a
            // divergence rate v_div. Detection needs r* > threshold, i.e.
            //     tau > residual_threshold / v_div,min
            // The SLOWEST genuine intervention is the weakest one: a small AD
            // command (LC-scale, |target| ≈ 0.05) with the driver gently
            // holding the wheel short of it. There the servo settles at
            // |f| ≈ 0.25, so the shadow runs away at 3.35·(0.25−0.16) ≈ 0.29
            // axis-frac/s → tau must exceed 0.08/0.29 ≈ 0.28 s. 1.5 s leaves
            // a 5× margin on that floor (r* ≈ 0.44, threshold crossed in
            // ~0.30 s) while still bleeding sustained model error at
            // 0.08/1.5 ≈ 0.053 axis-frac/s — an order of magnitude above the
            // drift a hands-off run actually produces, which is transient
            // (the servo converges and the force falls back under kinetic)
            // rather than standing.
            double override_residual_reanchor_tau       = 1.5;   // seconds

            // ---- Shadow plant constants (per-device; G29 measured) --------
            // CHARACTERIZATION.md §2/§3. These describe the WHEEL, not the
            // controller, so they belong in config exactly like friction_ff:
            // another device has a different breakaway and must be recalibrated
            // with scripts/ffb_spike/07_friction_map.py.
            //
            // INDEPENDENCE REQUIREMENT (do not refactor this away). The
            // headless verification plant (HeadlessFfbInput "plant" mode) and
            // the unit-test WheelPlant model the same device, but must NOT
            // share code, constants, or a helper with this shadow. Sharing
            // would make "hands off produces zero residual" a tautology about
            // one shared function instead of evidence about the wheel, and
            // would render every headless non-firing result worthless. Each
            // side takes its numbers from CHARACTERIZATION.md separately, and
            // the verification sweeps vary the plant away from these values on
            // purpose.
            //
            // The bias to prefer, where a choice exists, is toward NOT firing:
            // a false negative is recoverable (the driver pushes harder, or
            // uses the RESUME/override button), while a false positive drops
            // AD out mid-manoeuvre.
            //
            // WHAT GATES THE ONSET OF MOTION IS BREAKAWAY — NOT 0.16.
            // This is the single easiest thing to get wrong here, so it is
            // stated explicitly: `kinetic` (0.16) below is the INTERCEPT of
            // the force→velocity line for a wheel that is ALREADY MOVING, and
            // the force at which a moving wheel stops. It is NOT the force at
            // which a wheel at rest starts. Using 0.16 as the deadzone
            // predicts motion the real wheel does not make: over the measured
            // hands-off stretch of the 2026-07-26 G29 log (t=17.49-24.99,
            // 7.5 s, |f| 0.166-0.180, measured displacement EXACTLY zero) a
            // 0.16 deadzone predicts 0.298 axis-frac of drift — 3-6x any
            // sensible threshold, i.e. a guaranteed false latch.
            //
            // BREAKAWAY IS A BAND, AND IT IS DIRECTION-ASYMMETRIC:
            //     pushing the wheel LEFT  (force > 0): 0.170 - 0.210, mean 0.178
            //     pushing the wheel RIGHT (force < 0): 0.190 - 0.210, mean 0.198
            // (CHARACTERIZATION.md §2. The asymmetry is ~0.02, comparable to
            // the repeat spread, which is why the servo does not compensate
            // for it — but a PREDICTOR must respect it, because the low end
            // of the left band and the low end of the right band are what
            // decide whether the shadow moves at all.)
            //
            // Picking a single number fails in both directions:
            //   too HIGH → the shadow starts LATER than the real wheel. A slow
            //     AD ramp is tracked in steady state at whatever force the
            //     wheel needs (|f| = 0.16 + rate/3.35). The real wheel creeps,
            //     the shadow sits frozen, and the residual grows for as long
            //     as the ramp lasts → false latch on gentle steering.
            //   too LOW  → the shadow starts EARLIER than a wheel sitting at
            //     the top of the band, so a genuinely stuck wheel accumulates
            //     residual → false latch.
            //
            // Resolved by using both ends and letting the MEASUREMENT break
            // the tie. The shadow starts unconditionally at `breakaway` (top
            // of both bands — any wheel is moving by then), and at the
            // per-direction band bottom only when the real axis is
            // demonstrably moving, which settles where in the band THIS
            // device actually sits without needing to know it a priori.
            // Once moving, shadow and wheel share the kinetic floor and the
            // force→velocity slope, so tracked ramps stay matched.
            //
            // Worked check against the real-machine hands-off stretch above:
            // the force there is NEGATIVE (pushing right) at |f| <= 0.180,
            // which is below the right-hand band bottom of 0.190 by >= 0.010
            // in every frame — so neither arm opens, the shadow stays parked
            // with the wheel, and the residual is identically zero.
            double override_shadow_breakaway            = 0.21;
            // Per-direction band bottoms, named by which way the force pushes
            // the wheel. Positive force pushes LEFT (axis negative); negative
            // force pushes RIGHT (axis positive) — FfbTargetServo.hpp.
            double override_shadow_breakaway_left       = 0.170;  // force > 0
            double override_shadow_breakaway_right      = 0.190;  // force < 0
            // Displacement (not rate) the measured axis must move from its
            // last at-rest anchor to count as "demonstrably moving". A
            // displacement test is used because it is immune to the wheel's
            // jitter: G29 column jitter is bounded at ~0.005 axis and cannot
            // accumulate, while real motion can. 0.01 is 10× the SDL2
            // quantisation floor (~0.001) and 2× the observed jitter.
            double override_shadow_motion_epsilon       = 0.01;  // axis-fraction
            // kinetic: force below which an ALREADY-MOVING wheel stops, and
            // the intercept of the force→velocity fit below. NOT an onset
            // threshold — see the breakaway note above; this is the number
            // that gets mistaken for a deadzone. The 0.02–0.03 gap up to
            // breakaway is the measured static/kinetic hysteresis.
            double override_shadow_kinetic              = 0.16;
            // Slope of the measured force→velocity line, axis-frac/s per unit
            // force above `kinetic` (CHARACTERIZATION.md §3b, linear to |f|≈0.40).
            double override_shadow_force_to_velocity    = 3.35;
            // Velocity saturation beyond the linear region (|f| >= 0.40).
            double override_shadow_v_max                = 1.0;   // axis-frac / s
        } target_track;
    } ffb;

    // Domain assignment (lateral / longitudinal)
    struct
    {
        std::string lateral      = "manual";  // "manual" or "scenario"
        std::string longitudinal = "manual";  // "manual" or "scenario"
    } domain;

    // Override (auto <-> manual)
    struct
    {
        bool   enabled             = true;
        double steering_threshold  = 0.05;
        double throttle_threshold  = 0.1;
        double brake_threshold     = 0.1;
        double auto_return_timeout = 0.0;  // 0 = no timeout
        bool   button_override     = true;
    } override_cfg;

    bool LoadFromFile(const std::string& filepath);
};

} // namespace gt_esmini
