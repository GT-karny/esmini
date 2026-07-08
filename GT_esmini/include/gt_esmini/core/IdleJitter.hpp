/*
 * GT_esmini - Extended esmini with HostVehicleData Functionality
 * https://github.com/esmini/esmini
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <cstdint>
#include <random>

namespace gt_esmini
{

/**
 * @brief Ornstein-Uhlenbeck idle-RPM jitter generator.
 *
 * Produces a mean-zero, Gauss-distributed, mean-reverting RPM offset that
 * models the small fluctuations a real engine exhibits at idle (cycle-to-cycle
 * combustion variation + ISC closed-loop residual). Output has standard
 * deviation `sigma_rpm` and characteristic time constant `tau_s`.
 *
 * Discrete update (exact for OU under constant params):
 *   n_{k+1} = n_k * exp(-dt/τ) + σ * sqrt(1 - exp(-2 dt/τ)) * N(0, 1)
 */
class IdleJitter
{
public:
    struct Params
    {
        double   sigma_rpm = 5.0;   // steady-state std-dev [RPM]; 0 disables jitter
        double   tau_s     = 1.2;   // mean-reversion time constant [s]
        uint32_t seed      = 0;     // 0 = nondeterministic (random_device); >0 = fixed
    };

    IdleJitter() { Configure(Params{}); }

    void Configure(const Params& p);

    /// Advance one timestep and return the current jitter offset [RPM].
    /// Returns 0 when sigma_rpm <= 0 (jitter disabled).
    double Step(double dt);

    /// Reset internal OU state to zero (e.g. on scenario restart).
    void Reset() { state_ = 0.0; }

private:
    Params       params_;
    double       state_ = 0.0;
    std::mt19937 rng_;
    std::normal_distribution<double> normal_{0.0, 1.0};
};

}  // namespace gt_esmini
