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
// REPRESENTATION: POLARITY LIVES IN THE CALIBRATION, ON EVERY AXIS.
//
// No axis carries an invert flag. The calibrated pair already states the
// direction:
//   pedals   (raw_released, raw_full) -- released > full is the G29 convention
//   steering (raw_center, raw_full)   -- raw_full is FULL RIGHT, so raw_full <
//                                        raw_center means the axis counts up to
//                                        the left
// Inverting an axis therefore means exchanging its two calibrated ends, which is
// what the GUI's per-axis "Flip" button does. One fact, one representation, on
// all four axes.
//
// EARLIER DESIGN, AND WHY IT WAS WRONG (kept so it is not re-invented): steering
// had an extra `invert` bool "because which way the wheel turns is a preference,
// unlike a pedal". That was backwards. Nobody prefers mirrored steering -- you
// set it to match how the DEVICE counts, exactly like a pedal. And with
// raw_full defined as full-right, the flag duplicated information the
// calibration already carried, i.e. it was the redundancy the pedals were
// deliberately spared. It is gone; `steer_invert` in an old config file is
// ignored with a warning (ManualDriveConfig.cpp).
//
// The FFB consequence has not gone away, only its source: see SignFactor().

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

    /** Inverting a pedal IS exchanging its calibrated ends (GUI "Flip"). */
    void Flip()
    {
        const int released = raw_released;
        raw_released       = raw_full;
        raw_full           = released;
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
    int index = 0;
    // Raw value at wheel centre, and at FULL RIGHT lock. Defaults are the G29
    // convention (centre 0, full right +32767). raw_full < raw_center is a
    // device whose axis counts up toward the left -- that IS the inverted case,
    // and it needs no separate flag.
    int raw_center = 0;
    int raw_full   = 32767;

    bool IsAssigned() const
    {
        return index >= 0;
    }

    /**
     * Which way the device's raw value runs relative to "right is positive":
     * +1 when raw increases to the right, -1 when it increases to the left.
     *
     * Normalize() does NOT need this (its denominator already carries the sign).
     * It exists for the FFB, and that is a SAFETY interface, not a convenience:
     *
     * SDLFFBSink's force sign convention is tied to the axis direction. If the
     * axis runs the other way, the F7 target-tracking servo must have its OUTPUT
     * sign flipped too, otherwise it pushes AWAY from its target and the loop
     * becomes positive feedback on a powered actuator. Readback and commanded
     * force therefore both derive from this one function, so they cannot
     * disagree -- verified on a real G29 for both polarities (see
     * docs/features/wheel_axis_mapping.md §4-2).
     */
    double SignFactor() const
    {
        return (raw_full >= raw_center) ? 1.0 : -1.0;
    }

    /**
     * Inverting the steering IS mirroring its calibrated span about the centre,
     * which keeps the centre where the user put it (unlike a pedal, whose two
     * ends are simply exchanged). GUI "Flip".
     */
    void Flip()
    {
        raw_full = 2 * raw_center - raw_full;
    }

    // raw -> [-1,+1], clamped. Sign comes from the calibration itself: with
    // raw_full < raw_center the denominator is negative, so a raw above centre
    // normalizes negative (= left), which is exactly the inverted device.
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
