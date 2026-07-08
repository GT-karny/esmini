/*
 * GT_esmini - Extended esmini with HostVehicleData Functionality
 * https://github.com/esmini/esmini
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "gt_esmini/core/IdleJitter.hpp"

#include <cmath>

namespace gt_esmini
{

void IdleJitter::Configure(const Params& p)
{
    params_ = p;
    state_  = 0.0;
    if (p.seed == 0)
    {
        std::random_device rd;
        rng_.seed(rd());
    }
    else
    {
        rng_.seed(p.seed);
    }
}

double IdleJitter::Step(double dt)
{
    if (params_.sigma_rpm <= 0.0 || dt <= 0.0)
    {
        return 0.0;
    }

    double tau = (params_.tau_s > 1e-3) ? params_.tau_s : 1e-3;
    double decay = std::exp(-dt / tau);
    // Stationary increment std-dev: σ * sqrt(1 - exp(-2 dt/τ)).
    double inc_sd = params_.sigma_rpm * std::sqrt(1.0 - decay * decay);

    state_ = decay * state_ + inc_sd * normal_(rng_);
    return state_;
}

}  // namespace gt_esmini
