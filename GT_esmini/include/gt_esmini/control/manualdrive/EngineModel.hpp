/*
 * GT_esmini - Extended esmini with HostVehicleData Functionality
 * https://github.com/esmini/esmini
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#pragma once

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
    };

    struct State
    {
        double rpm           = 0.0;
        double torque_nm     = 0.0;
        bool   rev_limited   = false;
        bool   initialized   = false;
    };

    EngineModel() = default;

    void SetParams(const Params& p) { params_ = p; }
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
    void Step(double throttle, double target_rpm, bool clutch_locked, double dt);

    double GetRPM() const     { return state_.rpm; }
    double GetTorqueNm() const { return state_.torque_nm; }
    bool   IsRevLimited() const { return state_.rev_limited; }

    void Reset();

private:
    Params params_;
    State  state_;
};

} // namespace gt_esmini
