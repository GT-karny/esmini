#pragma once

// req-vd-ad:REQ-AD-025 (step d) / req-vd-ad:REQ-AD-030 (step b) / vd-func:FUNC-075
//
// Kickdown = "the driver has floored the accelerator", i.e. an unambiguous
// intent to accelerate. Two different ADAS functions consume that same verdict:
//
//   * AEB  suppresses its intervention while it holds (design §3-2, a
//          translation of UN R152's driver-override provision -- SECONDARY
//          SOURCE, original text unread, so no conformance is claimed).
//   * MSL  temporarily lifts its throttle cap while it holds (design §6).
//
// They share ONE detector instance rather than each testing the pedal against
// its own threshold. Two thresholds would eventually disagree, producing a
// state nobody can explain from the outside ("AEB was suppressed but the
// limiter never released"), and REQ-AD-030 step b / the
// md-kickdown-shared-consistency observation are written on the assumption
// that both edges are the SAME edge (design §3-3).
//
// Pure logic: no esmini, no OSI, no controller state -- same convention as
// AdSteeringEnvelope (control/virtualdriver/AdSteeringEnvelope.hpp) and
// FfbTargetServo, so the semantics that carry the safety argument are unit
// testable on their own.

namespace gt_esmini
{

// REQUIRES CALIBRATION (verification plan §5): both numbers are placeholders
// picked to be obviously "floored" vs "not floored", not measured values. The
// extended batch's boundary assets (verification plan §3-4) are what fixes
// them; until then they must not be cited as anything but defaults.
constexpr double kKickdownDefaultEngageThreshold  = 0.95;  // accelerator fraction [0,1]
constexpr double kKickdownDefaultReleaseThreshold = 0.80;  // accelerator fraction [0,1]

struct KickdownDetectorConfig
{
    // Engage at/above engage_threshold, release strictly below
    // release_threshold. The gap between them is the hysteresis band: without
    // it a pedal resting exactly on the threshold toggles AEB suppression every
    // frame, which is the one behaviour a safety-relevant gate must never have.
    double engage_threshold  = kKickdownDefaultEngageThreshold;
    double release_threshold = kKickdownDefaultReleaseThreshold;
};

class KickdownDetector
{
public:
    explicit KickdownDetector(const KickdownDetectorConfig& cfg = {});

    // Feed this frame's accelerator position [0,1]; returns the latched verdict.
    bool Update(double accel_pedal);

    bool IsActive() const
    {
        return active_;
    }

    void Reset()
    {
        active_ = false;
    }

    // The EFFECTIVE config, i.e. after the inverted-band coercion in the
    // constructor. Exposed so a caller reporting thresholds into custom_detail
    // reports what the detector actually used, not what the JSON asked for.
    const KickdownDetectorConfig& Config() const
    {
        return cfg_;
    }

private:
    KickdownDetectorConfig cfg_;
    bool                   active_ = false;
};

}  // namespace gt_esmini
