#pragma once

// feature:F8 -- per-device wheel axis assignment + raw-range calibration.
//
// WHY THIS EXISTS. Until now SDL2WheelInput hardcoded axis 0=steer, 1=throttle,
// 2=brake, 3=clutch AND hardcoded the G29 pedal convention (raw +32767 =
// released, -32768 = fully pressed). Both are DEVICE FACTS, not properties of
// the simulator: a Logitech G923 was observed reporting its pedals in a
// different axis order, and nothing in the config file could express that, so
// the wheel was simply unusable (brake input arriving as clutch, etc.).
//
// This header is deliberately SDL-FREE and lives in the unconditional source
// list -- NOT inside CMake's `if(GT_ENABLE_SDL2)` block. GT_ENABLE_SDL2
// defaults OFF (and CI builds with it OFF), so a mapping compiled only when
// SDL2 is enabled would take its unit tests out of CI silently. The normalize
// arithmetic is where every device difference is resolved, so it is exactly
// the part that must stay testable without a wheel attached.
//
// REPRESENTATION CHOICES (both are load-bearing):
//
//  - Pedals carry NO invert flag. The (raw_released, raw_full) pair already
//    encodes polarity: released > full is an inverted pedal (the G29 case),
//    released < full is the other convention. One representation per fact
//    means there is no way to state polarity twice and disagree with yourself.
//  - Steering DOES carry an invert flag, applied AFTER normalization. Range
//    calibration (raw_center / raw_full) and "the wheel turns the wrong way"
//    are different questions to a human, and the second one is a switch.
//    See the FFB note on SteerAxisSpec::invert -- the sign has a safety
//    consequence, so it must be read from ONE place.

#include <string>
#include <vector>

namespace gt_esmini
{

// A pedal axis (throttle / brake / clutch): normalizes to [0,1] where 0 is
// released and 1 is fully pressed, whatever raw convention the device uses.
struct PedalAxisSpec
{
    // SDL joystick axis index. -1 = unassigned (a wheel with no clutch pedal,
    // or a function the user does not want bound). An unassigned axis reads as
    // a constant 0.0 (= released) and is never polled.
    int index = -1;
    // Raw value the device reports with the pedal RELEASED, and with it fully
    // PRESSED. Defaults are the G29 convention, which is what every shipped
    // config file has been implicitly assuming.
    int raw_released = 32767;
    int raw_full     = -32768;

    bool IsAssigned() const
    {
        return index >= 0;
    }

    // Whether the "no HID report yet" guard in SDL2WheelInput is meaningful for
    // this axis. That guard substitutes raw_released when the driver has not yet
    // reported anything (Windows/DirectInput returns raw=0 for hundreds of ms
    // after JoystickOpen, and on a G29 raw=0 means HALF PRESSED -- a phantom
    // half-throttle that trips OverrideManager's threshold and locks the
    // longitudinal domain to MANUAL before the scenario even starts).
    //
    // When raw_released == 0 the guard MUST NOT run: raw=0 is then the correct
    // released reading, and substituting a sentinel would pin the pedal to
    // "released" forever on a device whose axis genuinely rests at 0 -- turning
    // a startup transient into a permanently dead pedal.
    bool NeedsReleasedSentinel() const
    {
        return raw_released != 0;
    }

    // raw -> [0,1]. Degenerate span (released == full, i.e. an uncalibrated or
    // mis-entered pair) yields 0.0 = released rather than a division by zero:
    // for a pedal, "released" is the safe reading to fabricate.
    double Normalize(int raw) const;
};

// The steering axis: normalizes to [-1,+1]. Sign convention is unchanged from
// the pre-F8 code -- positive = right (raw increasing toward raw_full).
struct SteerAxisSpec
{
    int  index = 0;
    // Applied AFTER normalization: n = -n. Use this for "the wheel turns the
    // wrong way", not for calibration.
    //
    // SAFETY -- READ BEFORE USING THIS ELSEWHERE. SDLFFBSink's force sign
    // convention is tied to the axis polarity (positive force pushes the wheel
    // LEFT, i.e. toward negative raw). If the axis is inverted relative to
    // that convention, the F7 target-tracking servo must have its OUTPUT sign
    // flipped by the SAME factor, otherwise the servo pushes away from its
    // target and the loop becomes positive feedback (a powered actuator
    // running away). Hence: this flag is read in exactly one place
    // (SteerSignFactor() below), and both the axis readback and the commanded
    // force multiply by it.
    bool invert = false;
    // Raw value at wheel centre, and at FULL RIGHT lock. Defaults are the G29
    // convention (centre 0, full right +32767).
    int  raw_center = 0;
    int  raw_full   = 32767;

    bool IsAssigned() const
    {
        return index >= 0;
    }

    double SignFactor() const
    {
        return invert ? -1.0 : 1.0;
    }

    // raw -> [-1,+1], clamped.
    //
    // NOTE, deliberate 3e-5 behaviour change vs the pre-F8 code: the old
    // NormalizeAxis divided by 32767 without clamping, so a raw of -32768
    // returned -1.00003. This clamps to exactly -1. The clamp is correct
    // (downstream code, including the FFB hard-stop zone, treats 1.0 as full
    // lock) and the magnitude is far below the wheel's own jitter (~0.005),
    // but it is stated here so nobody has to rediscover why a full-lock trace
    // differs in the fifth decimal from an old log.
    double Normalize(int raw) const;
};

struct WheelAxisMapping
{
    SteerAxisSpec steer;
    PedalAxisSpec throttle{1, 32767, -32768};
    PedalAxisSpec brake{2, 32767, -32768};
    PedalAxisSpec clutch{3, 32767, -32768};

    // Appends one human-readable line per problem found. An EMPTY result means
    // the mapping is usable as-is; it is not a claim that the mapping matches
    // the user's pedals (only the device can tell you that -- see
    // GT_WheelProbe).
    //
    // Checked: an assigned index beyond the device's axis count (the exact
    // failure a G923's different axis order produces when copied from a G29
    // config), and a degenerate calibration span. Both are reported rather
    // than silently corrected, because the correction that "looks safe"
    // (falling back to axis 0) is how a brake pedal ends up steering.
    void CollectProblems(int num_axes, std::vector<std::string>& problems) const;
};

}  // namespace gt_esmini
