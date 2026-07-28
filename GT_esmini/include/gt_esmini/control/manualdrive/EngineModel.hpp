/*
 * GT_esmini - Extended esmini with HostVehicleData Functionality
 * https://github.com/esmini/esmini
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#pragma once

#include "gt_esmini/core/IdleJitter.hpp"

namespace gt_esmini
{

/**
 * @brief Forward engine model with a flat-top turbo torque curve.
 *
 * Tuned for 1.5L turbo (Honda Civic FL1 class):
 *   peak torque flat 1600..5000 RPM, falls off toward redline 6500 RPM.
 * Includes idle governor, rev limiter, and 1st-order RPM lag (engine inertia).
 */
class EngineModel
{
public:
    struct Params
    {
        double idle_rpm            = 700.0;
        double max_rpm             = 6500.0;
        double rev_limit_rpm       = 6500.0;  // fuel cut above this
        double torque_peak_nm      = 260.0;
        double torque_flat_low_rpm = 1600.0;
        double torque_flat_high_rpm = 5000.0;
        double torque_redline_factor = 0.65;  // falloff at redline (0..1 of peak)
        double engine_inertia_tau_s = 0.15;
        double idle_governor_gain   = 0.5;    // throttle floor while rpm < idle
        // Rev-match blip: synthetic throttle injection on downshift events.
        double blip_throttle_floor  = 0.6;    // minimum throttle while blip active
        double blip_inertia_tau_s   = 0.06;   // faster RPM rise during blip
        // Idle creep: residual converter-pumping torque at idle in gear,
        // which drives the car forward when brake is released at throttle=0.
        double idle_creep_torque_nm = 3.0;
        // Idle RPM jitter (OU process) — fades out as converter locks up.
        IdleJitter::Params idle_jitter;
    };

    /**
     * @brief Vehicle-state context provided by the caller (RealVehicle).
     *
     * EngineModel intentionally does not know about wheels or speed; the
     * coordinator passes pre-computed signals so the engine block can decide
     * when "idle" semantics apply (jitter, future load disturbances, etc.)
     * without re-deriving them.
     */
    struct VehicleContext
    {
        double abs_speed_mps = 0.0;
        double slip_factor   = 0.0;  // 0 = full converter slip (idle), 1 = locked
    };

    struct State
    {
        double rpm           = 0.0;  // displayed RPM = base + jitter
        double base_rpm      = 0.0;  // smooth lag-filtered RPM (no jitter)
        double torque_nm     = 0.0;
        bool   rev_limited   = false;
        bool   initialized   = false;
        // Rev-match blip transient
        double blip_timer_s  = 0.0;
        double blip_lift_rpm = 0.0;
    };

    EngineModel() = default;

    void SetParams(const Params& p)
    {
        params_ = p;
        jitter_.Configure(p.idle_jitter);
    }
    const Params& GetParams() const { return params_; }

    /// Compute the maximum (wide-open-throttle) torque at a given RPM.
    double MaxTorqueAt(double rpm) const;

    /**
     * @brief Advance the engine one step.
     *
     * @param throttle    [0..1] driver throttle
     * @param target_rpm  RPM the powertrain wants the engine at (geared from wheels
     *                    when the converter is locked, idle when fully unlocked)
     * @param clutch_locked  true = engine RPM is yanked toward target_rpm,
     *                    false = engine free-revs against load
     * @param dt          timestep [s]
     */
    void Step(double throttle, double target_rpm, bool clutch_locked,
              const VehicleContext& vctx, double dt);

    /// Trigger a transient rev-match blip (e.g. on AT downshift). For the next
    /// `duration_s` seconds, the engine target RPM is lifted by `lift_rpm` and
    /// a synthetic throttle floor is applied. Subsequent calls re-arm.
    void TriggerBlip(double duration_s, double lift_rpm);

    bool   IsBlipping() const { return state_.blip_timer_s > 0.0; }

    /// DISPLAY RPM: base + idle-jitter overlay. For gauges and OSI only.
    /// Never feed this into a force/torque calculation — the jitter is seeded
    /// from std::random_device unless idle_jitter_seed is set, so any physics
    /// term derived from it makes the whole simulation nondeterministic.
    double GetRPM() const     { return state_.rpm; }
    /// PHYSICS RPM: jitter-free. This is the one a force calculation may read.
    double GetBaseRPM() const { return state_.base_rpm; }
    double GetTorqueNm() const { return state_.torque_nm; }
    bool   IsRevLimited() const { return state_.rev_limited; }

    void Reset();

private:
    Params     params_;
    State      state_;
    IdleJitter jitter_;
};

} // namespace gt_esmini
