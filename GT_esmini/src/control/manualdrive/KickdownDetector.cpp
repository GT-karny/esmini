// req-vd-ad:REQ-AD-025 (step d) / req-vd-ad:REQ-AD-030 (step b) / vd-func:FUNC-075
//
// See the header for the shared-instance rationale (AEB suppression and MSL
// release must agree on the SAME edge, design §3-3).

#include "gt_esmini/control/manualdrive/KickdownDetector.hpp"

namespace gt_esmini
{

KickdownDetector::KickdownDetector(const KickdownDetectorConfig& cfg)
    : cfg_(cfg)
{
    // Coerce an inverted/degenerate band (release_threshold >= engage_threshold)
    // down to a COINCIDENT band (release == engage) rather than, say, rejecting
    // the config or swapping the two values. Coincident is the only coercion
    // that is provably non-chattering: Update()'s engage check (>=) and release
    // check (<) partition the pedal axis at a single point with no gap either
    // side can straddle, so a value sitting exactly on that point still gets a
    // single, stable verdict every call (see
    // InvertedBandIsCoercedAndCannotChatter in the test file). Swapping the two
    // values instead would just produce a DIFFERENT, still-inverted band.
    if (cfg_.release_threshold >= cfg_.engage_threshold)
    {
        cfg_.release_threshold = cfg_.engage_threshold;
    }
}

bool KickdownDetector::Update(double accel_pedal)
{
    if (accel_pedal >= cfg_.engage_threshold)
    {
        active_ = true;
    }
    else if (accel_pedal < cfg_.release_threshold)
    {
        active_ = false;
    }
    // else: inside the hysteresis band (or, post-coercion, exactly on the
    // coincident boundary without meeting the >= engage check) -- hold
    // whatever the latch already was.

    return active_;
}

}  // namespace gt_esmini
