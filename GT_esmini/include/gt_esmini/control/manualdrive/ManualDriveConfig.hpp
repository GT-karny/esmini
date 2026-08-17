#pragma once

#include "gt_esmini/control/manualdrive/WheelAxisMapping.hpp"

#include <string>

namespace gt_esmini
{

struct ManualDriveConfig
{
    // Top-level type selection
    std::string input_type   = "sdl2_wheel";     // "sdl2_wheel", "network", "stub", "headless_ffb", "scripted"
    std::string physics_type = "real_vehicle";    // "real_vehicle", "network"
    bool        ffb_enabled  = true;

    // Populated by LoadFromFile() itself: the directory containing the file
    // that was loaded (not a JSON key). Lets an input source resolve a
    // config-relative path (e.g. input_scripted.profile_file below) without
    // ControllerManualDrive having to thread the config path through a second
    // channel -- LoadFromFile already receives the full path. Empty if
    // LoadFromFile was never called (or was called with a bare filename with
    // no directory component).
    std::string config_dir;

    // Input: SDL2 wheel
    struct
    {
        int    device_index          = 0;
        // 0 = no deadzone. VirtualDriver has no deadzone key of its own, so for
        // the VD-holds-the-wheel configuration this default IS the effective
        // value. ManualDrive's config/manual_drive*.json set it explicitly and
        // therefore override this.
        double deadzone              = 0.0;
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

        // feature:F8 — per-device axis assignment + raw-range calibration.
        // Defaults reproduce the pre-F8 hardcoded G29 layout (0=steer,
        // 1=throttle, 2=brake, 3=clutch, pedals inverted with released=+32767),
        // so every existing config file behaves exactly as before.
        //
        // On-disk keys are FLAT and live in the same "input" block as the
        // button mapping (steer_axis / throttle_axis / throttle_raw_released
        // / ...). See the PARSER NOTE on the `adas` member below for why they
        // must be flat and globally unique regardless of JSON nesting; the
        // keyboard block's own "throttle"/"brake"/"clutch" keys do NOT alias
        // with these because the scanner matches the quoted key including its
        // closing quote.
        WheelAxisMapping axes;
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

    // Input: scripted profile playback (req-vd-ad:REQ-AD-025..031, vd-func:
    // FUNC-075; manualdrive_adas_verification_plan.md §7-4). Deterministic,
    // piecewise-linear replay of a recorded input profile against SIMULATION
    // time -- see ScriptedInputSource.hpp for the profile file schema and
    // interpolation rules. No socket: self-determinism (identical replay
    // every run) is the property the whole ManualDrive-ADAS batch judgment
    // rests on (verification plan §7-5), and a socket cannot give that
    // guarantee the way a file replayed against sim-time dt can.
    struct
    {
        // Path to the profile JSON. A relative path resolves against
        // config_dir above (the directory of THIS config file); an absolute
        // path (gt_esmini::ConfigLoader::IsAbsolutePath) passes through
        // unchanged. Empty (the shipped default) means "no profile" --
        // ScriptedInputSource::Init() fails loudly if input_type=="scripted"
        // and this is empty, rather than silently falling back to all-zero
        // input (a silently-zeroed run is a fabricated measurement, not a
        // real one -- project convention).
        std::string profile_file;
    } input_scripted;

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
            //
            // ---- SCOPE OF THE NON-FIRING GUARANTEE (stated, not implied) --
            //
            // The guarantee "a stuck wheel does not latch" holds ONLY while
            // the effective force stays inside the measured breakaway band
            // (<= 0.210). Above the band, a wheel that does not move is
            // indistinguishable from a wheel someone is holding — every
            // observable is identical — and the behaviour change above chose
            // detection. Both cannot hold at once.
            //
            // This IS a narrowing of what commit b6dc58f0 guaranteed, and the
            // record says its stuck condition was NOT confined to the band:
            // CHARACTERIZATION.md §6 measured, hands-off on the b6dc58f0-era
            // shipped configuration, a combined force above 0.20 for 4.5 % of
            // a lane change, 28.6 % of a curve and 18.8 % of a right turn,
            // while the wheel followed only 28.8 / 45.4 / 34.2 % of the
            // command. So force above the band with the wheel barely moving
            // did occur, and today's detector would fire on it.
            //
            // What removes it in practice is that the b6dc58f0-era
            // configuration no longer exists: commit cb9e1c1c added the
            // friction feed-forward, suppressed the feel terms while the servo
            // is active, and stopped creating SPRING/DAMPER alongside CONSTANT.
            // Hands-off follow rose to 92-109 % (CHARACTERIZATION.md §8e), i.e.
            // the wheel is no longer stuck, and the 2026-07-26 hands-off G29
            // run reproduces no stuck state at all. The remaining real failure
            // mode — force commanded but not reaching the wheel — is NOT
            // detectable by this design, and that is accepted knowingly.

            // Axis-fraction the real wheel must diverge from the shadow
            // prediction before the sustain clock runs. 0.08 ≈ 36° of a G29's
            // 900° range.
            //
            // WHAT THE ARGUMENT BELOW ACTUALLY ESTABLISHES — stated honestly,
            // because an earlier version of this comment read as though it
            // derived the number:
            //
            //   LOWER BOUND (must be above the noise): SDL2 quantisation
            //   ~0.001 and mechanical jitter ~0.005, plus the residual drift
            //   the re-anchor leaves standing. That puts the floor around
            //   0.01-0.02.
            //   UPPER BOUND (must be crossed by a real hand inside
            //   override_sustain_time = 0.10 s): a held wheel under a
            //   saturated 0.6 servo force diverges at ~1.0 axis-frac/s, so
            //   anything up to ~0.10 is crossed in time.
            //
            // So the physics bounds the value to roughly 0.02..0.10. It does
            // NOT single out 0.08. Within that band 0.08 was chosen to sit
            // near the permissive end, trading detection latency for immunity
            // to plant-model error — the residual is |measurement - model|,
            // and the model is a G29 fit whose error on any other wheel is
            // unknown (see override_shadow_v_max's provenance note).
            //
            // The threshold sweep that once appeared to justify a specific
            // level is formally RETRACTED and must not be cited: its driver
            // script is not in the repository, so it cannot be reproduced, and
            // a third of its latches came from an unrelated path
            // (docs/virtualdriver/measurements/measurement_discipline.md, 2026-07-28 note).
            // If a point value ever needs defending, it needs a reproducible
            // sweep — not this comment.
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
            // PROVENANCE — SINGLE SESSION, NO INDEPENDENT VERIFICATION.
            //
            // This value and the breakaway/kinetic hysteresis band above all
            // come from ONE measurement session: CHARACTERIZATION.md, dated
            // 2026-07-25, one Logitech G29, one afternoon, produced by
            // 07_friction_map.py. Nothing in scripts/ffb_spike/ re-measures
            // them, and every downstream number consumes them without ever
            // re-deriving them. They are core constants of the intervention
            // detector -- the residual IS |measurement - this model| -- so a
            // systematic error here shows up as a false latch or a missed
            // driver, and there is currently no second measurement that would
            // reveal it.
            //
            // v_max is additionally an EXTRAPOLATION, not a reading: the
            // measured force/velocity table stops at 0.954 axis-frac/s at
            // f=0.58 (the servo's own force ceiling is 0.6), and 1.0 is the
            // saturation asymptote fitted to it. No sample in the session ever
            // reached 1.0.
            //
            // Re-measuring needs the physical wheel, so it is on
            // GT_esmini/docs/virtualdriver/field-test/realmachine_open_items.md
            // (R-1), not fixable
            // here. Until then, treat any conclusion that leans on these
            // constants as resting on one session's data.
            double override_shadow_v_max                = 1.0;   // axis-frac / s

            // ---- Transient behaviour (the steady-state map is not enough) --
            //
            // Everything above describes where the wheel ENDS UP for a given
            // force: it was measured by applying a constant force and reading
            // the terminal speed. The shadow used that map as if the wheel
            // reached it instantly. It does not, and on the 2026-07-26
            // hands-off G29 runs that cost 0.070 s of the 0.100 s latch clock
            // on traffic_lights_junction — 30 ms short of a false MANUAL latch
            // with nobody touching the wheel.
            //
            // velocity_tau: first-order lag on the shadow's velocity, i.e.
            // mechanical inertia. Identified on ONE run and validated on two
            // held-out runs (see the completion report §17). Takes the latch
            // clock to 0.000 s on all three measured scenarios.
            //
            // HONEST LIMIT: this does NOT make the shadow correct. The peak
            // residual margin only reaches ~1.2x, so about three quarters of
            // the residual is still not explained by anything in this model
            // family. Do not read the presence of this constant as validation.
            //
            // SHIPPED DEFAULT IS 0 (disabled), NOT the fitted 0.10 — see the
            // completion report §18. Enabling it on the detector alone turns
            // the verification plant into a false-alarm generator: the
            // synthetic plant is memoryless, so a shadow with inertia lags it
            // systematically and the hands-off parity fixture LATCHES
            // (measured: worst residual 0.0139 -> 0.0864 at dt=0.05, and a
            // real latch at dt=0.01). Both sides have to gain the dynamics,
            // and both values have to come from a dedicated measurement
            // rather than a fit to driving data — otherwise they agree by
            // construction and prove nothing.
            double override_shadow_velocity_tau         = 0.018; // seconds (measured)

            // dead_time: transport delay between commanding a force and the
            // wheel feeling it (plus the delay in reading the axis back).
            // DEFAULT 0 = DISABLED, deliberately: the residual's correlation
            // with the force's rate of change (+0.51..+0.80) is the classic
            // signature of a dead time, but fitting it against AD-driven runs
            // did not generalise — the value identified on one run made a
            // held-out run slightly worse. A delay must be measured with
            // dedicated step inputs, not inferred from driving data. The
            // plumbing is here so that measurement lands as a config change.
            double override_shadow_dead_time            = 0.041; // seconds (measured)

            // ---- Onset grace: treat the physically undecidable as undecided --
            //
            // Breakaway is a MEASURED BAND (0.170-0.210), which means that for
            // a force inside the band it is not knowable whether this wheel is
            // about to move. Accumulating residual across that instant is not a
            // modelling error to be fixed with a better constant — the quantity
            // itself is indeterminate. Measured cost: the onset regime accounts
            // for 17-50% of residual growth on the real machine
            // (residual_decompose.py R1).
            //
            // So when the shadow's motion STATE disagrees with the measured
            // axis's, the shadow is re-synced to the measurement instead of
            // banking the difference — but only for this long. A driver does
            // not show up as a few tens of milliseconds of onset-timing skew;
            // they show up as a disagreement that PERSISTS. Past the grace the
            // residual accumulates exactly as before, so a held wheel is still
            // detected.
            //
            // This is NOT the old direction gate coming back: nothing here
            // looks at which way the wheel sits relative to the target. It only
            // declines to measure during an interval that is physically
            // indeterminate.
            //
            // Too generous a grace would swallow a slow push, the same hole the
            // re-anchor has. The detection floors are measured by
            // Acceptance34_MinimumDetectableDriverRampRate; keep them in view
            // when changing this.
            double override_shadow_onset_grace          = 0.05;  // seconds; 0 = off
            // Axis rate above which the measured wheel counts as moving, for
            // the state comparison above.
            double override_shadow_motion_rate_eps      = 0.02;  // axis-frac / s
        } target_track;

        // feature:F7 — UNATTENDED-RUN SAFETY WATCHDOG (SDLFFBSink only).
        //
        // A haptic wheel is a powered actuator, and a supervised run has a
        // human as its last line of defence. An unattended run does not, so
        // the sink must be able to shut itself up. See SDLFFBSink.hpp for the
        // physical argument behind each trip.
        //
        // BOTH DEFAULT TO 0 = DISABLED. Supervised/interactive behaviour and
        // every existing gate are therefore bit-identical; only the unattended
        // runbook turns them on. When a trip fires it is LATCHED for the rest
        // of the process (a watchdog that re-arms oscillates) and the servo is
        // held at zero force.
        struct
        {
            // Seconds of CONTINUOUS saturation before shutting the force off.
            // "Saturated" = |applied force| >= saturation_ratio * max_force.
            // Sizing: the servo legitimately touches the cap during a step, but
            // hands-off tracking of the most aggressive measured profile peaks
            // near 0.29 (CHARACTERIZATION.md §6), so seconds at the cap means
            // the loop is straining against something it cannot move.
            double max_saturation_seconds = 0.0;   // 0 = disabled
            // Seconds of total force-commanding lifetime for this sink. Catches
            // the case saturation cannot see: a run that hangs with a modest
            // but non-zero force applied.
            double max_runtime_seconds    = 0.0;   // 0 = disabled
            // Fraction of max_force that counts as saturated.
            double saturation_ratio       = 0.95;
        } safety;
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
        bool   button_takeover     = false;  // physical toggle: AUTO -> MANUAL
    } override_cfg;

    // ManualDrive ADAS -- PHASE A ONLY (AEB + the shared kickdown detector +
    // the §3-4 decel->brake PI conversion). req-vd-ad:REQ-AD-025,
    // vd-func:FUNC-075. manualdrive_adas_design.md §9 sketches the FULL
    // config skeleton across every phase (A-D); this struct implements ONLY
    // the phase-A subset (design §10 phase table) -- do NOT add acc/lka/msl
    // blocks here until those phases actually land, per the phase-A task
    // scope ("do not add config keys for functions phase A does not
    // implement").
    //
    // Every key below ships at a default-OFF / harmless value, same
    // convention as F6 AutoLight and lane_change_initiation: adding this
    // struct must not change any existing config file's runtime behaviour
    // (design §9 "全機能とも既定 OFF").
    //
    // PARSER NOTE -- READ BEFORE ADDING A KEY HERE. LoadFromFile() below is
    // NOT a real JSON parser: it scans the file LINE BY LINE and matches a
    // key by flat substring search (`line.find("\"" + key + "\"")`),
    // ignoring which JSON object the key is textually nested inside. Two
    // fields using the same on-disk key name -- e.g. this struct's "aeb
    // enabled" and override_cfg's existing "enabled" -- would silently alias
    // (whichever line the scanner is on assigns to BOTH C++ fields). The
    // existing ffb block already hit this and solved it the same way this
    // struct does: flat, prefixed, globally-unique on-disk key names
    // (ffb_enabled / target_track_enabled, not bare "enabled") even though
    // the JSON nests them for human readability. See LoadFromFile's parse_*
    // calls for the exact on-disk key strings used for each field below.
    struct
    {
        struct
        {
            // On-disk key: adas_aeb_enabled (see PARSER NOTE above -- bare
            // "enabled" would alias with override_cfg.enabled's own
            // "enabled" key).
            bool enabled = false;

            // Driver-override suppression (design §3-2): while the shared
            // KickdownDetector (kickdown_threshold/kickdown_release_threshold
            // below) is active, AEB does not intervene -- a translation of
            // UN R152's driver-override provision (SECONDARY SOURCE, original
            // text unread, no conformance claimed; see KickdownDetector.hpp).
            bool kickdown_suppress_enabled = true;

            // REQUIRES CALIBRATION -- verification plan §5. Must stay
            // LOOSER (numerically larger) than AebSafetyConfig::ttc_threshold
            // (2.5s, AebSafety.hpp) so FCW warns strictly before AEB
            // intervenes (design §3-2, slug md-fcw-leads-intervention); the
            // >=0.8s lead time itself is also unanchored (secondary-source
            // UN R152 concept, same §5 entry). 3.5s is a placeholder chosen
            // only to satisfy that ordering constraint, not a measured value.
            double warning_ttc_threshold_s = 3.5;

            // On-disk key: adas_aeb_warning_min_a_req_mps2.
            //
            // The OTHER half of the FCW gate. DeriveFcwGateConfig
            // (AdasCoexistenceStack.cpp) clamps BOTH warning_ttc_threshold_s
            // and this value to build the warning-path AebSafetyConfig, so the
            // warning fires only where both thresholds admit it. Phase A
            // exposed only the first of the pair, which made the calibration
            // the verification plan §5 asks for structurally impossible: on
            // any encounter where required deceleration is the binding side,
            // moving warning_ttc_threshold_s alone cannot move the warning
            // point at all (design §9/§12's recorded gap, closed here in
            // phase B).
            //
            // REQUIRES CALIBRATION -- verification plan §5. Must stay LOOSER
            // (numerically SMALLER) than AebSafetyConfig::min_a_req (3.0
            // m/s^2); DeriveFcwGateConfig clamps rather than rejects a value
            // that is not, so a mis-set key degrades the warning lead instead
            // of breaking the run. 2.0 mirrors ManualAdasStackConfig::
            // warning_min_a_req_mps2's own compiled-in default, the same
            // "config file overrides the C++-side default" relationship
            // brake_control below has with PedalArbitratorConfig.
            double warning_min_a_req_mps2 = 2.0;   // [m/s^2]
        } aeb;

        // §3-4: required-deceleration -> brake-pedal PI conversion.
        // PedalArbitrator (control/manualdrive/PedalArbitrator.hpp, owned by
        // another agent and deliberately NOT included from this header) is
        // the C++-side single source of truth for these three constants --
        // the same relationship VirtualDriverConfig has with
        // AdSteeringEnvelopeConfig (see AdSteeringEnvelope.hpp's "Single
        // source of truth ON THE C++ SIDE" paragraph). These fields exist
        // only so the config *file* can override PedalArbitrator's
        // compiled-in defaults; the numeric values below are copied from
        // PedalArbitratorConfig's own default member initializers on
        // 2026-08-04 and a recalibration must update both places by hand.
        struct
        {
            // On-disk key: adas_brake_full_decel_mps2.
            // REQUIRES CALIBRATION (verification plan §5) -- mirrors
            // PedalArbitratorConfig::full_brake_decel_mps2: a textbook
            // full-ABS dry-pavement figure, not measured against
            // RealVehicleBackend's actual brake model.
            double full_brake_decel_mps2 = 8.0;   // [m/s^2]
            // REQUIRES CALIBRATION -- mirrors PedalArbitratorConfig::brake_kp:
            // a placeholder picked only to give the PI loop a visibly-
            // converging shape.
            double brake_kp = 0.05;
            // REQUIRES CALIBRATION -- mirrors PedalArbitratorConfig::brake_ki.
            double brake_ki = 0.6;   // [1/s]
        } brake_control;

        // Shared by AEB suppression (above) and, in phase C, MSL's temporary
        // cap release (design §3-3 -- ONE detector so the two edges can never
        // disagree). Mirrors KickdownDetectorConfig's own default member
        // initializers (KickdownDetector.hpp) for the same "config overrides
        // the C++-side default" relationship as brake_control above.
        // REQUIRES CALIBRATION (verification plan §5): placeholders picked to
        // be obviously "floored" vs "not floored", not measured values.
        // kickdown_threshold is compared directly against the normalized
        // throttle axis ([0,1]). A real G29 pedal may never reach 0.95 after
        // deadzone/axis calibration, even though synthetic input
        // (ScriptedInputSource) can always emit exactly 1.0 -- see
        // GT_esmini/docs/virtualdriver/field-test/realmachine_open_items.md
        // R-4 for the real-pedal measurement that resolves this (2026-08-05).
        double kickdown_threshold         = 0.95;  // engage, accelerator fraction [0,1]
        double kickdown_release_threshold = 0.80;  // release (hysteresis band), [0,1]

        // ==================================================================
        // PHASE C -- ACC (req-vd-ad:REQ-AD-026 / REQ-AD-031, vd-func:FUNC-079)
        // ==================================================================
        // Every field mirrors AccLonControllerConfig / AccStopAndGoConfig,
        // which stay the C++-side single source of truth for the DEFAULTS
        // (same relationship brake_control has with PedalArbitratorConfig);
        // these exist only so the config FILE can override them. A
        // recalibration must update both places by hand.
        //
        // On-disk keys are flat and prefixed (adas_acc_*) for the PARSER NOTE's
        // reason. Note in particular `adas_acc_enabled` rather than a nested
        // "enabled": the scanner is scope-blind and a bare "enabled" would
        // alias with override_cfg's.
        struct
        {
            bool   enabled            = false;
            double set_speed_step_mps = 1.39;   // ~5 km/h

            // Following-distance stages, as THREE FLAT KEYS rather than the
            // design sketch's JSON array (design §9's "thw_stages_s": [...]).
            // The loader cannot read an array at all -- it is a line scanner --
            // so an array in the file would parse as nothing and leave every
            // stage at its compiled-in default, silently. Three keys make the
            // 3-stage count explicit, which is the count the design specifies.
            // REQUIRES CALIBRATION (verification plan §5).
            double thw_stage_short_s = 1.0;
            double thw_stage_mid_s   = 1.6;
            double thw_stage_long_s  = 2.2;
            int    thw_default_stage = 1;

            // REQ-AD-026 step f. max <= 0 means no upper bound; min 0 means
            // down to standstill (correct for an ACC with Stop&Go).
            double min_speed_mps = 0.0;
            double max_speed_mps = 0.0;

            // REQ-AD-026 step g. Same key vocabulary as the VD overtake
            // ceiling's respect_speed_limit (req-vd-ad:REQ-AD-023), as that
            // requirement's note asks.
            bool   respect_speed_limit = false;

            // ACC's OWN comfort envelope. Deliberately NOT the VD
            // comfort_decel (design §4-2/§12: that number means "how smoothly
            // the VD slows itself", which carries no meaning for a car a human
            // is driving). REQUIRES CALIBRATION.
            double accel_max_mps2 = 1.2;
            double decel_max_mps2 = 2.0;

            // Pedal references. These are what make the two limits above real:
            // the speed loop commands an ACCELERATION and divides by these to
            // get a pedal, so a large speed error can no longer saturate the
            // command past the envelope. See AccLonControllerConfig's own
            // "Pedal references" block for the measurement that motivated it.
            // REQUIRES CALIBRATION.
            double full_brake_decel_mps2    = 8.0;
            double full_throttle_accel_mps2 = 3.0;

            // Speed loop, in the ACCELERATION domain (kp: 1/s, ki: 1/s^2).
            // REQUIRES CALIBRATION.
            double speed_kp           = 0.45;
            double speed_ki           = 0.12;
            double speed_deadband_mps = 0.20;

            // Driver-input thresholds: accelerator = temporary override
            // (state retained), brake = cancel (ACTIVE -> STANDBY).
            double accel_override_threshold = 0.05;
            double brake_cancel_threshold   = 0.05;

            // Stop&Go (REQ-AD-031 段a/b).
            struct
            {
                bool   enabled = true;
                // 段b targets. Each one ADDS a policy to the manual stack;
                // leaving it false means the policy is never instantiated, so
                // the negative direction of md-sng-target-config-polarity is
                // structural rather than a filter (AdasCoexistenceStack's
                // constructor).
                bool   stop_at_traffic_light = false;
                bool   stop_at_stop_sign     = false;
                double restart_accel_threshold = 0.10;
                // MEASURED against RealVehicle's automatic-transmission creep
                // (design §12), 2026-08-05: hold_brake 0.30 keeps the vehicle
                // inside 0.032 m over an 11.6 s hold. stop_speed_eps_mps 0.5
                // must stay ABOVE the ~0.16 m/s creep floor or the hold can
                // never engage at all -- see AccLonController.hpp's field
                // comment and GT_esmini/docs/virtualdriver/measurements/
                // manualdrive_creep_stop_hold_2026-08-05.md.
                double hold_brake              = 0.30;
                double stop_speed_eps_mps      = 0.5;
            } stop_and_go;
        } acc;

        // ==================================================================
        // PHASE C -- MSL (req-vd-ad:REQ-AD-030, vd-func:FUNC-081)
        // ==================================================================
        // Mirrors SpeedLimiterConfig. There is deliberately no set-speed key:
        // the limiter's cap is SET FROM THE VEHICLE'S OWN SPEED when the
        // driver switches it on and adjusted with the same stalk buttons as
        // ACC's (design §9's shared-vocabulary note), exactly like a real
        // limiter -- a config-file cap would be a fourth way to set the same
        // number and none of the requirement's steps ask for one.
        struct
        {
            bool   enabled            = false;
            bool   speed_limit_linked = false;  // REQ-AD-030 step c
            double taper_band_mps     = 2.0;    // REQUIRES CALIBRATION
        } msl;

        // ==================================================================
        // PHASE D -- LKA / LDW (req-vd-ad:REQ-AD-027, vd-func:FUNC-080)
        // ==================================================================
        // Mirrors LaneKeepAssistConfig, which stays the C++-side single source
        // of truth for the DEFAULTS (same relationship acc/msl above have with
        // their own controller configs); these exist only so the config FILE
        // can override them. A recalibration must update both places by hand.
        //
        // On-disk keys are flat and prefixed (adas_lka_*) for the PARSER NOTE's
        // reason. `adas_lka_enabled` in particular, never a bare "enabled".
        //
        // NOTE ON warning_only: it is a MODE of this one block, not a second
        // function with its own enable. Setting warning_only=true keeps the
        // departure judgement (and therefore the gt.ldw row and every
        // gt.lka.* diagnostic) running and suppresses only the correction --
        // which is what makes REQ-AD-027 step f's two configurations differ in
        // exactly one observable (see LaneKeepAssist.hpp).
        struct
        {
            bool   enabled      = false;
            bool   warning_only = false;

            // REQ-AD-027 step e. Same key vocabulary as ACC (REQ-AD-026 step f)
            // by the shared decision on those two requirements. max <= 0 means
            // no upper bound. REQUIRES CALIBRATION -- the customary production
            // figure of ~60 km/h for the lower bound is second-hand, and the
            // shipped default is 0 so that enabling the function never silently
            // does nothing on a slower scenario.
            double min_speed_mps = 0.0;
            double max_speed_mps = 0.0;

            // Departure judgement (design §5-1). REQUIRES CALIBRATION.
            double tlc_threshold_s     = 1.5;
            double margin_threshold_m  = 0.15;
            double release_margin_m    = 0.30;

            // Correction law + the lateral envelope (design §5-2).
            // REQUIRES CALIBRATION. See LaneKeepAssistConfig for the arithmetic
            // these were sized from -- in particular why the gains are ~0.01
            // and not ~0.1 (a normalized correction of 0.0024 already nulls a
            // 0.3 m/s drift at 25 m/s).
            double kp_offset          = 0.012;
            double kd_lateral         = 0.020;
            double correction_max     = 0.03;
            double correction_rate_max = 0.10;

            // Human steering priority (design §5-3). REQUIRES CALIBRATION;
            // the real-wheel figure is a G29 item
            // (GT_esmini/docs/virtualdriver/field-test/realmachine_open_items.md).
            double steer_override_rate   = 0.03;
            double steer_override_hold_s = 2.0;
        } lka;
    } adas;

    // ADAS operating controls (req-vd-ad:REQ-AD-026 step e/h, REQ-AD-030).
    // Physical wheel button indices, -1 = unassigned -- the same convention
    // and the same place as the existing sdl2 button mapping, per design
    // §4-1 ("manual_drive.json の既存ボタンマッピング流儀に乗せる"). Kept in
    // their own struct rather than appended to `sdl2` so the on-disk keys stay
    // greppable as a group; the loader is scope-blind either way.
    struct
    {
        int acc_toggle_button     = -1;
        int acc_set_resume_button = -1;
        int acc_speed_up_button   = -1;
        int acc_speed_down_button = -1;
        int acc_thw_cycle_button  = -1;
        int msl_toggle_button     = -1;
    } adas_buttons;

    bool LoadFromFile(const std::string& filepath);
};

} // namespace gt_esmini
