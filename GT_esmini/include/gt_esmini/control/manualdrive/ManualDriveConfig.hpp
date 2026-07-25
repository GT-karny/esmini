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
            double override_steer_force_threshold       = 0.20;
            double override_steer_dev_threshold         = 0.04;
            double override_sustain_time                = 0.10;  // seconds
            // Rate-gate for the torque-proxy detector. When |d(target)/dt|
            // exceeds this (axis-fraction per second), the servo is chasing a
            // moving target and its tracking lag creates non-zero
            // position_error that MUST NOT be misread as driver intervention.
            // Detection sustain is reset while above the gate; re-arms when
            // the target settles. Default 0.30 = ~10% axis-fraction per 333 ms
            // = comfortably above a slow lane change (spike §2b calibrated
            // torque proxy against STATIC target only — this gate closes that
            // real-driving gap; real-machine bug found after commit a43e4c67).
            double override_target_rate_gate            = 0.30;  // axis-frac / s
            // Second gate on |d(position_error)/dt|. Distinguishes the
            // "servo chasing" transient (dev changes as the wheel catches up
            // to target) from a real block (dev sits at a persistent value —
            // driver is holding). Also masks the startup transient when the
            // physical wheel is at rest and the servo hasn't accelerated it
            // yet — the first commit's rate-gate alone did NOT cover this
            // because target itself is nearly static at startup while dev
            // is large. Real-machine bug found after commit 549e5823.
            //
            // Default 0.10 axis-frac/s is calibrated for typical G29 first-
            // order response: a 0.10-axis-fraction step decays through the
            // dev threshold over ~300 ms → decay rate ~0.30 /s → firmly
            // above the gate for the whole transient. A truly held wheel
            // sits at exactly 0 rate. Any positive gate value between the
            // two catches the transient; 0.10 leaves headroom for physical
            // wheel jitter (~0.005 per 20 ms tick = 0.25 /s peak).
            double override_position_error_rate_gate    = 0.10;  // axis-frac / s
            // Third gate: the physical wheel must be OPPOSING the target
            // (either sign-opposed, or overshooting the target magnitude by
            // more than this epsilon) for the torque-proxy latch to fire.
            //
            // Rationale (real-machine bug found on virtual_driver_basic LC /
            // anticipation curves / right_turn after commit f723fa90): the
            // small AD servo command cannot overcome G29 breakaway friction,
            // so the wheel either stays at 0 (short scenarios) or slowly
            // creeps toward target under sustained pressure (long scenarios
            // like a curve — measured ~5%/s creep). In both cases the wheel
            // is following the servo's direction, |actual| ≤ |target|,
            // dev ≈ target and force ≈ Kp·target — signals identical to a
            // "driver holding wheel steady" case except for one physical
            // truth: the wheel is NOT past target. A driver actively taking
            // over either turns the wheel PAST where AD wants it (opposition
            // by magnitude), or in the OPPOSITE direction (sign opposition).
            // A wheel obediently between 0 and target is servo behavior,
            // never user behavior.
            //
            // Servo-dynamics-aware condition:
            //   wheel_engaged = (target*actual < 0 AND |actual| >= ε)   # sign opposition (deadzoned)
            //                 OR (|actual| > |target| + ε)              # magnitude opposition
            // Where actual = target - position_error (derived from the sample).
            //
            // Default ε = 0.05 axis-fraction = 5% of full lock ≈ 22° on G29
            // 900°. Absorbs natural PID overshoot (kd=0.35 with sinusoidal
            // targets never exceeds ~2%) plus physical jitter margin. Small
            // enough that even a modest deliberate over-push (60°) latches
            // immediately. The deadzone on the sign arm is required because
            // a stuck G29 wheel measured on 2026-07-25 (right_turn scenario,
            // target -0.83) sits at +0.011 axis due to column mechanical
            // offset / SDL2 noise floor (~0.001 = 1 raw count in ~32767) —
            // technically opposite sign of target but nowhere near a real
            // driver push. ε at 50× the noise floor is safely above hardware
            // jitter but well below any deliberate hand movement.
            //
            // Edge case: user holding wheel firmly at 0 while AD wants ±X.
            // wheel_engaged=false (same sign, |actual|=0 < |target|+ε).
            // The latch will not fire — the user must either move the wheel
            // to a non-trivial position OR use the RESUME button / config
            // button-override. This is a defensible trade: the alternative
            // (a min-deflection gate) confuses "user firmly at 0" with
            // "wheel stuck at rest" and cannot resolve the ambiguity.
            double override_wheel_over_target_epsilon = 0.05;

            // feature:F7 (F7b, follow-up post-93b2c6c4) — velocity-opposition
            // gate. Fourth (additive) detection signature, independent of the
            // target_rate/position_error_rate gates above.
            //
            // Bug this closes: the AD steering safety envelope (93b2c6c4)
            // ramps the wheel target at up to steer_rate_max (default 1.5
            // rad/s ≈ 2.46 axis-frac/s) during the post-RESUME recovery
            // transient — comfortably above override_target_rate_gate (0.30)
            // for the ENTIRE transient. moving_target therefore stays true and
            // zeroes ffb_sustain_accum_ every frame, so a driver grabbing the
            // wheel during exactly that window (the moment they'd want to
            // take over) could never latch MANUAL — a safety regression.
            //
            // Raising target_rate_gate is not an option (defeats its purpose);
            // ignoring moving_target while wheel_engaged (position test) is
            // also unsafe: during a FAST-REVERSING target, tracking lag alone
            // can make |actual| momentarily exceed |target| (the position
            // test's opposition condition) with zero driver input — the
            // envelope's ramp can plausibly reverse direction faster than the
            // physical wheel/servo settles.
            //
            // This gate instead characterises "driver opposing" via a
            // signature that is invariant to how the target is moving: a
            // servo alone can only push the wheel in the direction that
            // reduces tracking error, so sign(commanded_force_signed) tracks
            // sign(d(actual_norm)/dt) whenever the wheel is unheld — true
            // whether the target is static, ramping, or reversing. A driver
            // fighting the servo is the only thing that can invert that
            // relationship. Condition (see OverrideManager::Update):
            //
            //   wheel_engaged_velocity = |d(actual_norm)/dt| > this gate
            //                            AND commanded_force_signed * d(actual_norm)/dt < 0
            //
            // When true, sustain is allowed to accumulate EVEN WHILE
            // moving_target/tracking_transient are tripped — this is exactly
            // the case the two rate gates otherwise blackout. The existing
            // position-based wheel_engaged test (override_wheel_over_target_
            // epsilon) is UNCHANGED and still requires both rate gates to be
            // settled, so none of the four real-machine false-positive fixes
            // above (a43e4c67 / 549e5823 / f723fa90 follow-ups) are weakened.
            //
            // Default 0.30 axis-frac/s: same order of magnitude as
            // override_target_rate_gate — comfortably above G29 physical
            // jitter (~0.25/s peak, see override_position_error_rate_gate
            // comment) so brief inertial coasting right at a target reversal
            // doesn't false-trigger, while well below a deliberate driver
            // countersteer (a quarter-axis flick in 150 ms ≈ 1-2/s). Residual
            // risk: an unusually slow, sustained (>=sustain_time) inertial
            // mismatch right at a reversal could theoretically still cross
            // this gate; not observed in the 33-scenario/22,983-frame
            // characterization run behind 93b2c6c4 but not exhaustively
            // proven against this specific gate. Flagged for real-G29 replay.
            double override_opposition_velocity_gate = 0.30;  // axis-frac / s
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
