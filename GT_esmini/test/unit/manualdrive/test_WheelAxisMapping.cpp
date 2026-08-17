// feature:F8 -- wheel axis assignment + raw-range calibration.
//
// These tests exist because the pre-F8 code hardcoded BOTH the axis order
// (0=steer, 1=throttle, 2=brake, 3=clutch) and the G29 raw pedal convention
// (+32767 released, -32768 pressed), and a Logitech G923 reports a different
// axis order -- i.e. the failure mode is "the brake pedal arrives as clutch",
// which no amount of scenario-level testing can catch.
//
// The mapping is deliberately SDL-free and compiled unconditionally (NOT under
// GT_ENABLE_SDL2, which defaults OFF and is OFF in CI), so this file runs on
// machines with no wheel attached. That is the point: the arithmetic every
// device difference funnels through must be verifiable without the device.

#include <gtest/gtest.h>

#include "gt_esmini/control/manualdrive/ManualDriveConfig.hpp"
#include "gt_esmini/control/manualdrive/WheelAxisMapping.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace gt_esmini
{
namespace
{

std::filesystem::path WriteTempConfig(const std::string& name, const std::string& content)
{
    const auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream file(path);
    file << content;
    file.close();
    return path;
}

}  // namespace

// --- Regression anchor: the shipped defaults must reproduce the pre-F8 math --
//
// The old NormalizePedal was (32767 - raw) / 65535 and the old NormalizeAxis was
// raw / 32767. If these three expectations ever change, every recorded G29 log
// and every FFB calibration constant measured against them is invalidated --
// which is why they are asserted as exact values, not ranges.

TEST(WheelAxisMappingTest, G29DefaultPedalReproducesPreF8Normalization)
{
    const WheelAxisMapping map;

    EXPECT_DOUBLE_EQ(map.throttle.Normalize(32767), 0.0);   // released
    EXPECT_DOUBLE_EQ(map.throttle.Normalize(-32768), 1.0);  // fully pressed
    // The "phantom half" raw. NOT exactly 0.5, and that is the point of writing
    // the old formula out: (32767 - 0) / 65535 = 0.49999237. An expectation of
    // 0.5 here would be asserting an assumption about the old code rather than
    // its actual output -- which is how a "behaviour unchanged" claim quietly
    // becomes false.
    EXPECT_DOUBLE_EQ(map.throttle.Normalize(0), 32767.0 / 65535.0);
    // Brake and clutch share the convention.
    EXPECT_DOUBLE_EQ(map.brake.Normalize(32767), 0.0);
    EXPECT_DOUBLE_EQ(map.clutch.Normalize(-32768), 1.0);
    // Default indices are the pre-F8 hardcoded layout.
    EXPECT_EQ(map.steer.index, 0);
    EXPECT_EQ(map.throttle.index, 1);
    EXPECT_EQ(map.brake.index, 2);
    EXPECT_EQ(map.clutch.index, 3);
}

TEST(WheelAxisMappingTest, G29DefaultSteerReproducesPreF8Normalization)
{
    const WheelAxisMapping map;

    EXPECT_DOUBLE_EQ(map.steer.Normalize(0), 0.0);
    EXPECT_DOUBLE_EQ(map.steer.Normalize(32767), 1.0);
    EXPECT_DOUBLE_EQ(map.steer.Normalize(16383), 16383.0 / 32767.0);
    // Deliberate 3e-5 difference from the pre-F8 code, documented in the
    // header: the old division returned -1.00003 here, this clamps.
    EXPECT_DOUBLE_EQ(map.steer.Normalize(-32768), -1.0);
}

TEST(WheelAxisMappingTest, ZeroResultsAreNeverNegativeZero)
{
    // Observed on a real G29 (2026-08-06): a released pedal produced -0, which
    // is numerically equal to 0 but serializes as "-0" and formats as "-0.00",
    // so the live readout showed negative values for pedals at rest.
    // std::signbit is the only way to assert this -- EXPECT_DOUBLE_EQ(v, 0.0)
    // passes for -0.0 and would not catch a regression here.
    const WheelAxisMapping map;
    EXPECT_FALSE(std::signbit(map.throttle.Normalize(32767)));  // released
    EXPECT_FALSE(std::signbit(map.steer.Normalize(0)));         // centred

    // A mirrored calibration divides by a negative span, the other way to reach -0.
    SteerAxisSpec mirrored;
    mirrored.raw_full = -32767;
    EXPECT_FALSE(std::signbit(mirrored.Normalize(0)));
}

// --- The device differences this feature exists for ------------------------

TEST(WheelAxisMappingTest, NonInvertedPedalConventionNormalizesCorrectly)
{
    // A pedal whose released reading is 0 and rises to +32767 -- the opposite
    // polarity from a G29, expressed purely by the calibration pair.
    PedalAxisSpec pedal{2, /*raw_released=*/0, /*raw_full=*/32767};

    EXPECT_DOUBLE_EQ(pedal.Normalize(0), 0.0);
    EXPECT_DOUBLE_EQ(pedal.Normalize(32767), 1.0);
    EXPECT_DOUBLE_EQ(pedal.Normalize(16383), 16383.0 / 32767.0);
}

TEST(WheelAxisMappingTest, PartialRangePedalScalesToFullTravel)
{
    // A pedal that only ever reports 1000..20000 (a real possibility once a
    // user calibrates by actually pressing, rather than assuming full scale).
    PedalAxisSpec pedal{1, /*raw_released=*/1000, /*raw_full=*/20000};

    EXPECT_DOUBLE_EQ(pedal.Normalize(1000), 0.0);
    EXPECT_DOUBLE_EQ(pedal.Normalize(20000), 1.0);
    EXPECT_DOUBLE_EQ(pedal.Normalize(10500), 0.5);
    // Beyond the calibrated ends: clamped, never extrapolated. An
    // over-travelling pedal must not command >100% throttle.
    EXPECT_DOUBLE_EQ(pedal.Normalize(25000), 1.0);
    EXPECT_DOUBLE_EQ(pedal.Normalize(-5000), 0.0);
}

TEST(WheelAxisMappingTest, AsymmetricSteerCalibrationIsHonored)
{
    // Wheel whose electrical centre is not raw 0.
    SteerAxisSpec steer;
    steer.raw_center = -100;
    steer.raw_full   = 30000;

    EXPECT_DOUBLE_EQ(steer.Normalize(-100), 0.0);
    EXPECT_DOUBLE_EQ(steer.Normalize(30000), 1.0);
    // Left of centre comes out negative and clamps at -1.
    EXPECT_LT(steer.Normalize(-10000), 0.0);
    EXPECT_DOUBLE_EQ(steer.Normalize(-32768), -1.0);
}

TEST(WheelAxisMappingTest, SteerPolarityComesFromTheCalibrationOrder)
{
    // The inverted device is expressed by calibrating full-right at a raw value
    // BELOW centre -- there is no flag to set. Both polarities asserted on the
    // same raw input.
    SteerAxisSpec normal;  // centre 0, full right +32767
    SteerAxisSpec mirrored;
    mirrored.raw_center = 0;
    mirrored.raw_full   = -32767;  // this device counts up to the left

    EXPECT_DOUBLE_EQ(normal.Normalize(16383), -mirrored.Normalize(16383));
    EXPECT_DOUBLE_EQ(mirrored.Normalize(-32767), 1.0);  // full right, at a negative raw
    EXPECT_DOUBLE_EQ(mirrored.Normalize(32767), -1.0);  // full left
    EXPECT_DOUBLE_EQ(mirrored.Normalize(0), 0.0);
}

TEST(WheelAxisMappingTest, SignFactorFollowsTheCalibrationOrder)
{
    // SignFactor is the FFB's only source for the force direction, and getting it
    // out of step with Normalize would make the F7 servo push away from its
    // target (positive feedback on a powered actuator). Both are derived from the
    // same two numbers now, so the test pins the derivation for both polarities.
    SteerAxisSpec normal;
    SteerAxisSpec mirrored;
    mirrored.raw_full = -32767;

    EXPECT_DOUBLE_EQ(normal.SignFactor(), 1.0);
    EXPECT_DOUBLE_EQ(mirrored.SignFactor(), -1.0);
    // Sign of the normalized reading for a raw above centre must agree with it.
    EXPECT_GT(normal.Normalize(20000) * normal.SignFactor(), 0.0);
    EXPECT_GT(mirrored.Normalize(20000) * mirrored.SignFactor(), 0.0);
}

TEST(WheelAxisMappingTest, FlipInvertsAnAxisAndIsItsOwnInverse)
{
    // "Flip" is the whole user-facing inversion mechanism (GUI button), so its
    // two properties are asserted directly: it mirrors the reading, and applying
    // it twice restores the original calibration exactly.
    SteerAxisSpec steer;
    steer.raw_center = -100;
    steer.raw_full   = 30000;
    const double before = steer.Normalize(15000);

    steer.Flip();
    EXPECT_DOUBLE_EQ(steer.Normalize(15000), -before);
    EXPECT_DOUBLE_EQ(steer.SignFactor(), -1.0);
    EXPECT_DOUBLE_EQ(steer.Normalize(-100), 0.0);  // the centre is preserved
    steer.Flip();
    EXPECT_EQ(steer.raw_center, -100);
    EXPECT_EQ(steer.raw_full, 30000);
    EXPECT_DOUBLE_EQ(steer.Normalize(15000), before);

    PedalAxisSpec pedal{1, 32767, -32768};
    EXPECT_DOUBLE_EQ(pedal.Normalize(-32768), 1.0);
    pedal.Flip();
    EXPECT_DOUBLE_EQ(pedal.Normalize(-32768), 0.0);  // now the released end
    EXPECT_DOUBLE_EQ(pedal.Normalize(32767), 1.0);
    pedal.Flip();
    EXPECT_EQ(pedal.raw_released, 32767);
    EXPECT_EQ(pedal.raw_full, -32768);
}

// --- The "no HID report yet" sentinel: both polarities --------------------

TEST(WheelAxisMappingTest, ReleasedSentinelIsNeededOnlyWhenReleasedRawIsNonZero)
{
    // G29 convention: raw=0 means HALF PRESSED, so a driver that has not
    // reported yet (Windows/DirectInput returns 0) must be treated as released.
    PedalAxisSpec g29{1, 32767, -32768};
    EXPECT_TRUE(g29.NeedsReleasedSentinel());

    // Device whose pedal rests at raw 0: the guard MUST be off, otherwise the
    // pedal is pinned to "released" for the whole session (raw=0 never latches
    // the axis live), turning a startup transient into a dead pedal.
    PedalAxisSpec rest_at_zero{1, 0, 32767};
    EXPECT_FALSE(rest_at_zero.NeedsReleasedSentinel());
    EXPECT_DOUBLE_EQ(rest_at_zero.Normalize(0), 0.0);  // 0 already reads as released
}

// --- Degenerate / unassigned configurations -------------------------------

TEST(WheelAxisMappingTest, DegenerateSpanReadsAsNeutralInsteadOfDividingByZero)
{
    PedalAxisSpec broken{1, 5000, 5000};
    EXPECT_DOUBLE_EQ(broken.Normalize(5000), 0.0);
    EXPECT_DOUBLE_EQ(broken.Normalize(30000), 0.0);  // released is the safe fabrication

    SteerAxisSpec broken_steer;
    broken_steer.raw_center = 500;
    broken_steer.raw_full   = 500;
    EXPECT_DOUBLE_EQ(broken_steer.Normalize(30000), 0.0);  // centred
}

TEST(WheelAxisMappingTest, UnassignedAxisIsNotAProblem)
{
    WheelAxisMapping map;
    map.clutch.index = -1;  // wheel with no clutch pedal

    EXPECT_FALSE(map.clutch.IsAssigned());
    std::vector<std::string> problems;
    map.CollectProblems(/*num_axes=*/4, problems);
    ASSERT_TRUE(problems.empty()) << "unexpected problem: " << problems.front();
}

TEST(WheelAxisMappingTest, IndexBeyondDeviceAxisCountIsReported)
{
    // Exactly the G923-config-copied-from-a-G29 failure: a 3-axis device with a
    // config that names axis 3.
    WheelAxisMapping map;
    std::vector<std::string> problems;
    map.CollectProblems(/*num_axes=*/3, problems);

    ASSERT_EQ(problems.size(), 1u);
    EXPECT_NE(problems.front().find("clutch"), std::string::npos);
    EXPECT_NE(problems.front().find("does not exist"), std::string::npos);
}

TEST(WheelAxisMappingTest, DegenerateCalibrationIsReported)
{
    WheelAxisMapping map;
    map.brake.raw_full = map.brake.raw_released;

    std::vector<std::string> problems;
    map.CollectProblems(/*num_axes=*/4, problems);

    ASSERT_EQ(problems.size(), 1u);
    EXPECT_NE(problems.front().find("brake"), std::string::npos);
    EXPECT_NE(problems.front().find("degenerate"), std::string::npos);
}

TEST(WheelAxisMappingTest, CleanMappingReportsNoProblems)
{
    // Negative control for the two reporting tests above: without it, a
    // CollectProblems that always appended something would still pass them.
    const WheelAxisMapping map;
    std::vector<std::string> problems;
    map.CollectProblems(/*num_axes=*/4, problems);
    EXPECT_TRUE(problems.empty());
}

// --- Config plumbing ------------------------------------------------------

TEST(WheelAxisMappingConfigTest, DefaultsSurviveAConfigThatNeverMentionsAxes)
{
    // A config file predating F8: every axis key absent.
    const auto path = WriteTempConfig("gt_f8_axes_absent.json", R"({
        "input_type": "sdl2_wheel",
        "input": { "device_index": 0, "deadzone": 0.0 }
    })");

    ManualDriveConfig cfg;
    ASSERT_TRUE(cfg.LoadFromFile(path.string()));

    EXPECT_EQ(cfg.sdl2.axes.steer.index, 0);
    EXPECT_DOUBLE_EQ(cfg.sdl2.axes.steer.SignFactor(), 1.0);
    EXPECT_EQ(cfg.sdl2.axes.throttle.index, 1);
    EXPECT_EQ(cfg.sdl2.axes.brake.index, 2);
    EXPECT_EQ(cfg.sdl2.axes.clutch.index, 3);
    EXPECT_EQ(cfg.sdl2.axes.throttle.raw_released, 32767);
    EXPECT_EQ(cfg.sdl2.axes.throttle.raw_full, -32768);
}

TEST(WheelAxisMappingConfigTest, AxisKeysAreParsed)
{
    // A plausible non-G29 layout: pedals in a different order, brake reporting
    // 0..32767, steering asymmetric.
    const auto path = WriteTempConfig("gt_f8_axes_set.json", R"({
        "input_type": "sdl2_wheel",
        "input": {
            "steer_axis": 0,
            "steer_raw_center": -100,
            "steer_raw_full": 30000,
            "throttle_axis": 2,
            "throttle_raw_released": 32767,
            "throttle_raw_full": -32768,
            "brake_axis": 1,
            "brake_raw_released": 0,
            "brake_raw_full": 32767,
            "clutch_axis": -1
        }
    })");

    ManualDriveConfig cfg;
    ASSERT_TRUE(cfg.LoadFromFile(path.string()));

    EXPECT_EQ(cfg.sdl2.axes.steer.index, 0);
    EXPECT_EQ(cfg.sdl2.axes.steer.raw_center, -100);
    EXPECT_EQ(cfg.sdl2.axes.steer.raw_full, 30000);
    EXPECT_EQ(cfg.sdl2.axes.throttle.index, 2);
    EXPECT_EQ(cfg.sdl2.axes.brake.index, 1);
    EXPECT_EQ(cfg.sdl2.axes.brake.raw_released, 0);
    EXPECT_EQ(cfg.sdl2.axes.brake.raw_full, 32767);
    EXPECT_EQ(cfg.sdl2.axes.clutch.index, -1);
}

TEST(WheelAxisMappingConfigTest, KeyboardBindingsDoNotAliasWithAxisKeys)
{
    // ALIAS REGRESSION. The loader matches keys by flat substring across the
    // whole file, ignoring JSON scope, so the keyboard block's "throttle" /
    // "brake" / "clutch" string keys sit one quote away from "throttle_axis" /
    // "brake_axis" / "clutch_axis". This is the test that proves the closing
    // quote in key_matches actually separates them -- without it the discipline
    // is only asserted in a comment.
    //
    // The keyboard keys are placed AFTER the axis keys on purpose: the scanner
    // is last-writer-wins per line, so an aliasing bug would show up as the
    // axis indices being clobbered by string values (stoi failure -> unchanged,
    // or worse, a partial parse).
    const auto path = WriteTempConfig("gt_f8_axes_alias.json", R"({
        "input": {
            "throttle_axis": 5,
            "brake_axis": 6,
            "clutch_axis": 7,
            "steer_axis": 4
        },
        "keyboard": {
            "steer_left": "A",
            "steer_right": "D",
            "throttle": "W",
            "brake": "S",
            "clutch": "LShift"
        }
    })");

    ManualDriveConfig cfg;
    ASSERT_TRUE(cfg.LoadFromFile(path.string()));

    EXPECT_EQ(cfg.sdl2.axes.steer.index, 4);
    EXPECT_EQ(cfg.sdl2.axes.throttle.index, 5);
    EXPECT_EQ(cfg.sdl2.axes.brake.index, 6);
    EXPECT_EQ(cfg.sdl2.axes.clutch.index, 7);
    // And the keyboard bindings themselves are unharmed in the other direction.
    EXPECT_EQ(cfg.keyboard.throttle, "W");
    EXPECT_EQ(cfg.keyboard.brake, "S");
    EXPECT_EQ(cfg.keyboard.clutch, "LShift");
}

TEST(WheelAxisMappingConfigTest, MirroredSteeringIsExpressedByTheCalibrationAlone)
{
    // The inverted device, as a config file: full-right calibrated BELOW centre.
    // No flag involved -- this is the whole mechanism, so it is asserted through
    // the loader and not only on a hand-built struct.
    //
    // ONE KEY PER LINE IS MANDATORY in these fixtures. The loader is a line
    // scanner that takes the substring after the FIRST colon on the line, so a
    // single-line `{ "a": 1, "b": 2 }` object parses as nothing (stoi throws on
    // the rest of the object and the value silently keeps its default). Written
    // as a one-liner, this test first "passed" while asserting on default values.
    const auto path = WriteTempConfig("gt_f8_mirrored.json", R"({
        "input": {
            "steer_axis": 0,
            "steer_raw_center": 0,
            "steer_raw_full": -32767
        }
    })");

    ManualDriveConfig cfg;
    ASSERT_TRUE(cfg.LoadFromFile(path.string()));

    EXPECT_DOUBLE_EQ(cfg.sdl2.axes.steer.SignFactor(), -1.0);
    EXPECT_DOUBLE_EQ(cfg.sdl2.axes.steer.Normalize(-32767), 1.0);  // full right
    EXPECT_DOUBLE_EQ(cfg.sdl2.axes.steer.Normalize(32767), -1.0);  // full left
}

TEST(WheelAxisMappingConfigTest, RetiredSteerInvertKeyDoesNotChangeAnything)
{
    // A config left over from the flag era must not silently steer the wheel the
    // other way. The key is parsed only to WARN (ManualDriveConfig.cpp); the
    // mapping it produces has to be identical to the same file without it, which
    // is what this asserts (the warning itself is not observable here).
    // One key per line -- see the note in the test above; as a one-liner this
    // test compared two configs that had BOTH failed to parse, i.e. it passed
    // vacuously.
    const auto with_key = WriteTempConfig("gt_f8_legacy_invert.json", R"({
        "input": {
            "steer_axis": 0,
            "steer_invert": true,
            "steer_raw_center": -100,
            "steer_raw_full": 30000
        }
    })");
    const auto without = WriteTempConfig("gt_f8_legacy_invert_absent.json", R"({
        "input": {
            "steer_axis": 0,
            "steer_raw_center": -100,
            "steer_raw_full": 30000
        }
    })");

    ManualDriveConfig a;
    ManualDriveConfig b;
    ASSERT_TRUE(a.LoadFromFile(with_key.string()));
    ASSERT_TRUE(b.LoadFromFile(without.string()));

    // Non-default values, so "both fell back to defaults" cannot masquerade as
    // agreement.
    EXPECT_EQ(a.sdl2.axes.steer.raw_center, -100);
    EXPECT_EQ(a.sdl2.axes.steer.raw_full, 30000);
    EXPECT_EQ(a.sdl2.axes.steer.raw_center, b.sdl2.axes.steer.raw_center);
    EXPECT_EQ(a.sdl2.axes.steer.raw_full, b.sdl2.axes.steer.raw_full);
    EXPECT_DOUBLE_EQ(a.sdl2.axes.steer.SignFactor(), b.sdl2.axes.steer.SignFactor());
    EXPECT_DOUBLE_EQ(a.sdl2.axes.steer.Normalize(16383), b.sdl2.axes.steer.Normalize(16383));
}

}  // namespace gt_esmini
