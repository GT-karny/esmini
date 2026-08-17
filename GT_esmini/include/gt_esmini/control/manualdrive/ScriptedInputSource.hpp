#pragma once

#include "gt_esmini/control/manualdrive/IInputSource.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace gt_esmini
{

// req-vd-ad:REQ-AD-025..031, vd-func:FUNC-075 -- deterministic input replay
// for ManualDrive ADAS verification
// (manualdrive_adas_verification_plan.md §7-4/§3-3).
//
// Replays a piecewise-linear, time-indexed input profile against SIMULATION
// time (the `dt` handed to Poll() each frame), never wall-clock time, so a
// batch run reproduces bit-for-bit run to run -- the self-determinism
// control the whole ManualDrive-ADAS batch judgment rests on (design §7-5 /
// verification plan §7-5, same two-stage discipline vd_ffb_notouch_parity.py
// established). Deliberately NO SOCKET: a socket cannot give that guarantee,
// and a test process's own send loop is not needed for a purely time-driven
// replay (verification plan §7-4).
//
// ===========================================================================
// PROFILE FILE FORMAT
// ===========================================================================
// JSON, parsed with gt_esmini::simplejson
// (GT_esmini/include/gt_esmini/common/SimpleJson.hpp):
//
//   {
//     "name": "kickdown_at",
//     "description": "free text; provenance / which observation slug this serves",
//     "keyframes": [
//       { "t": 0.0, "throttle": 0.0, "brake": 0.0, "steering": 0.0, "gear": 1, "buttons": 0 },
//       { "t": 3.0, "throttle": 0.0 },
//       { "t": 3.1, "throttle": 1.0 }
//     ]
//   }
//
// "name" and "description" are free text for humans/asset review; this class
// does not interpret them.
//
// ===========================================================================
// INTERPOLATION RULES -- every rule below is pinned by a dedicated test in
// test_ScriptedInputSource.cpp; if you change the behaviour, update the test
// that proves it, not just this comment.
// ===========================================================================
//
//  * throttle / brake / steering are interpolated LINEARLY between the two
//    bracketing keyframes.
//
//  * gear / buttons are STEP-HELD: the value used at a given sample time is
//    exactly the value stored on the most recent keyframe AT OR BEFORE that
//    time. Interpolating a bitmask or a gear number is meaningless, so these
//    two channels never blend.
//
//  * *** AN OMITTED CHANNEL IN A KEYFRAME MEANS 0.0 -- NOT "HOLD THE
//    PREVIOUS KEYFRAME'S VALUE". *** This is a rule about how ONE keyframe
//    object is parsed, and it applies to every channel: throttle, brake,
//    steering, gear AND buttons alike. A keyframe that specifies only
//    "throttle" resets brake/steering/gear/buttons to 0 AT THAT KEYFRAME --
//    and because gear/buttons are step-held (above), that 0 then holds
//    forward until a later keyframe redefines them. If a profile author
//    wants a gear or a button held across a keyframe, it must be repeated
//    explicitly in every keyframe where it should still apply.
//
//    This is deliberately loud, not implicit: this project has a documented
//    history of a "silent hold" class of defect, where a value one part of
//    the system treats as persisted is actually momentary elsewhere, and the
//    mismatch is invisible until someone reads the source. A profile that
//    silently held omitted fields would read one way (author intent: "gear
//    stays 1") and behave another (whatever the parser actually does), which
//    is exactly that defect class transplanted into a verification asset --
//    worse here than most places, because a verification profile that lies
//    about its own semantics can validate a bug instead of catching one.
//
//  * *** PRESENT-BUT-UNPARSEABLE IS NOT THE SAME AS ABSENT, EVEN THOUGH BOTH
//    "FAIL" THE SAME UNDERLYING JSON LOOKUP. *** This looks like it
//    contradicts the rule immediately above unless the reason is written
//    down, so: a channel key that is ABSENT from a keyframe object reads as
//    0.0 (the rule above -- deliberate, documented, tested). A channel key
//    that IS PRESENT but cannot be read as a number -- wrong JSON type
//    (bool/array/object), or a string that does not parse as one, e.g.
//    `"throttle": "banana"` -- is a DIFFERENT situation and Init() FAILS
//    LOUDLY on it instead of defaulting it to 0.0 (see the keyframe index
//    and key name in the logged error).
//
//    The distinction matters because a general config file and an input
//    PROFILE do not carry the same risk from silent coercion. In a general
//    config, a bad key degrading to a documented default still leaves a run
//    that means something. In a profile, the profile itself IS the
//    experimental variable: a present-but-malformed "throttle" silently
//    becoming 0.0 would make the run measure an unresponsive driver while
//    the manifest, the scenario name, the expectations file and the report
//    all still say "steady throttle" -- every downstream artefact
//    internally consistent and wrong, with nothing that looks broken. That
//    is the fabricated-measurement failure mode this project has already
//    paid for once (see the FAILURE POLICY section below) and this rule
//    exists specifically to keep a typo in a profile file from reproducing
//    it.
//
//  * Before the first keyframe: the first keyframe's own values (no
//    backward extrapolation).
//
//  * After the last keyframe: the last keyframe's own values (hold; no
//    forward extrapolation).
//
// ===========================================================================
// FAILURE POLICY
// ===========================================================================
// Init() FAILS LOUDLY -- returns false and logs an error via LOG_ERROR,
// leaving IsConnected() false -- on ANY of:
//   * input_scripted.profile_file is empty
//   * the file cannot be opened
//   * the file does not parse as JSON
//   * "keyframes" is missing, not an array, or an empty array
//   * a keyframe entry is not a JSON object, or is missing a numeric "t"
//   * a keyframe entry has a throttle/brake/steering/gear/buttons key that
//     IS PRESENT but cannot be read as a number (see the "PRESENT-BUT-
//     UNPARSEABLE VS ABSENT" paragraph above -- this is deliberately NOT the
//     same outcome as the key being absent)
//   * keyframe "t" values are not STRICTLY increasing (ties or reordering
//     both fail; a zero-duration segment has no well-defined interpolation)
//
// It NEVER silently substitutes an all-zero profile for a failed load. A
// test that runs on silently-zeroed input because the profile failed to load
// is a fabricated measurement -- this project treats that as worse than a
// crash (a crash is at least visible). A genuinely all-zero profile (e.g. the
// verification plan's "unresponsive" driver observation) is a valid,
// intentional PROFILE and is distinguished from a failed Init purely by
// Init()'s return value / IsConnected() -- never by inspecting whether the
// output happens to be zero, because those two cases must be
// indistinguishable in their OUTPUT and distinguishable in their STATUS.
//
// ===========================================================================
// PATH RESOLUTION
// ===========================================================================
// A relative input_scripted.profile_file resolves against
// ManualDriveConfig::config_dir (the directory of the config file that was
// loaded, populated by ManualDriveConfig::LoadFromFile itself). An absolute
// path (gt_esmini::ConfigLoader::IsAbsolutePath) passes through unchanged --
// the same "config-relative unless absolute" convention ConfigLoader applies
// to other config-referenced files.
//
// Pure logic aside from the one file read in Init(): no esmini dependency in
// the sampling path, unit-testable like AdSteeringEnvelope / FfbTargetServo /
// KickdownDetector / PedalArbitrator.
class ScriptedInputSource : public IInputSource
{
public:
    bool Init(const ManualDriveConfig& config) override;
    InputFrame Poll(double dt) override;
    void Shutdown() override;
    bool IsConnected() const override
    {
        return connected_;
    }

private:
    struct Keyframe
    {
        double   t        = 0.0;
        double   throttle = 0.0;  // omitted in JSON => 0.0 (class comment above)
        double   brake    = 0.0;
        double   steering = 0.0;
        int      gear     = 0;
        uint32_t buttons  = 0;
    };

    // Sample the loaded profile at simulation time `t` per the interpolation
    // rules above. Safe to call with an empty keyframes_ (returns an
    // all-zero command) even though Init() never leaves keyframes_ empty on
    // success -- keeps this function total rather than asserting.
    PedalSteerCommand SampleAt(double t) const;

    std::vector<Keyframe> keyframes_;
    double                clock_s_   = 0.0;
    bool                  connected_ = false;
};

} // namespace gt_esmini
