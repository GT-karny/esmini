// req-vd-ad:REQ-AD-025..031, vd-func:FUNC-075
//
// Unit tests for ScriptedInputSource -- the deterministic input-profile
// replay ManualDrive-ADAS verification uses instead of a socket
// (manualdrive_adas_verification_plan.md §7-4/§7-5). Each TEST pins exactly
// one rule from the header's INTERPOLATION RULES / FAILURE POLICY comment;
// if you change ScriptedInputSource.cpp's behaviour, the corresponding test
// here must change with it, not just the comment.

#include <gtest/gtest.h>

#include "gt_esmini/control/manualdrive/ManualDriveConfig.hpp"
#include "gt_esmini/control/manualdrive/ScriptedInputSource.hpp"

#include <filesystem>
#include <fstream>

namespace gt_esmini
{
namespace
{

// Writes `content` to a fresh temp file and returns its ABSOLUTE path (same
// helper shape as test_SimpleJson.cpp's WriteTempJson). Absolute so
// ManualDriveConfig::config_dir (empty in these tests) never enters path
// resolution -- these tests exercise ScriptedInputSource's own parsing/
// sampling, not ConfigLoader's relative-path plumbing.
std::filesystem::path WriteTempProfile(const std::string& name, const std::string& content)
{
    const auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream file(path);
    file << content;
    file.close();
    return path;
}

ManualDriveConfig MakeConfig(const std::filesystem::path& profile_path)
{
    ManualDriveConfig cfg;
    cfg.input_type                     = "scripted";
    cfg.input_scripted.profile_file    = profile_path.string();
    return cfg;
}

}  // namespace

// --- Interpolation rules -----------------------------------------------

TEST(ScriptedInputSourceTest, LinearlyInterpolatesThrottleBrakeSteeringAtMidpoint)
{
    const auto path = WriteTempProfile("gt_scripted_lerp.json", R"({
        "name": "lerp",
        "keyframes": [
            { "t": 0.0, "throttle": 0.0, "brake": 0.2, "steering": -0.5 },
            { "t": 2.0, "throttle": 1.0, "brake": 0.8, "steering":  0.5 }
        ]
    })");

    ScriptedInputSource src;
    ASSERT_TRUE(src.Init(MakeConfig(path)));

    // t = 0 + dt(1.0) = 1.0 -> exact midpoint of [0, 2].
    const InputFrame frame = src.Poll(1.0);
    ASSERT_TRUE(frame.pedal_steer.has_value());
    EXPECT_DOUBLE_EQ(frame.pedal_steer->throttle, 0.5);
    EXPECT_DOUBLE_EQ(frame.pedal_steer->brake, 0.5);
    EXPECT_DOUBLE_EQ(frame.pedal_steer->steering, 0.0);

    std::filesystem::remove(path);
}

TEST(ScriptedInputSourceTest, ButtonsAndGearAreStepHeldNeverInterpolated)
{
    const auto path = WriteTempProfile("gt_scripted_stepheld.json", R"({
        "name": "step_held",
        "keyframes": [
            { "t": 0.0, "gear": 1, "buttons": 1 },
            { "t": 2.0, "gear": 2, "buttons": 2 }
        ]
    })");

    ScriptedInputSource src;
    ASSERT_TRUE(src.Init(MakeConfig(path)));

    // t = 1.0: exact midpoint. A naive interpolation would produce gear=1.5
    // (nonsensical) or a blended buttons mask (1|2=3, also nonsensical).
    // Step-held must return the EARLIER bracket keyframe's own values.
    const InputFrame frame = src.Poll(1.0);
    ASSERT_TRUE(frame.pedal_steer.has_value());
    EXPECT_EQ(frame.pedal_steer->gear, 1);
    EXPECT_EQ(frame.pedal_steer->buttons, 1u);

    std::filesystem::remove(path);
}

TEST(ScriptedInputSourceTest, OmittedChannelReadsAsZeroNotHeldFromPreviousKeyframe)
{
    const auto path = WriteTempProfile("gt_scripted_omitted.json", R"({
        "name": "omitted_channel",
        "keyframes": [
            { "t": 0.0, "throttle": 0.5, "brake": 0.5, "steering": 0.5, "gear": 2, "buttons": 3 },
            { "t": 1.0, "throttle": 1.0 }
        ]
    })");

    ScriptedInputSource src;
    ASSERT_TRUE(src.Init(MakeConfig(path)));

    // Land exactly on the second keyframe (t=1.0). Everything it did not
    // spell out must read 0 -- NOT the first keyframe's 0.5/0.5/2/3. This is
    // the documented "no silent hold" rule (header comment), the whole
    // reason this test exists.
    const InputFrame frame = src.Poll(1.0);
    ASSERT_TRUE(frame.pedal_steer.has_value());
    EXPECT_DOUBLE_EQ(frame.pedal_steer->throttle, 1.0);  // explicitly set
    EXPECT_DOUBLE_EQ(frame.pedal_steer->brake, 0.0);     // omitted -> 0.0, not 0.5
    EXPECT_DOUBLE_EQ(frame.pedal_steer->steering, 0.0);  // omitted -> 0.0, not 0.5
    EXPECT_EQ(frame.pedal_steer->gear, 0);               // omitted -> 0, not 2
    EXPECT_EQ(frame.pedal_steer->buttons, 0u);           // omitted -> 0, not 3

    std::filesystem::remove(path);
}

// --- Present-but-unparseable vs absent (the two tests below are meant to be
// read TOGETHER -- one channel, one omitted / one malformed, opposite
// outcomes -- see the header's "PRESENT-BUT-UNPARSEABLE VS ABSENT"
// paragraph for why that is not a contradiction). ------------------------

TEST(ScriptedInputSourceTest, OmittedThrottleReadsAsZeroAndInitSucceeds)
{
    // "throttle" is absent from the (only) keyframe -- legitimate, reads 0.0,
    // Init() succeeds. Contrast with PresentButNonNumericThrottleFailsInit
    // immediately below: same channel, different reason it has no value.
    const auto path = WriteTempProfile("gt_scripted_throttle_omitted.json", R"({
        "name": "throttle_omitted",
        "keyframes": [
            { "t": 0.0, "brake": 0.5 }
        ]
    })");

    ScriptedInputSource src;
    ASSERT_TRUE(src.Init(MakeConfig(path)));
    ASSERT_TRUE(src.IsConnected());

    const InputFrame frame = src.Poll(1.0);
    ASSERT_TRUE(frame.pedal_steer.has_value());
    EXPECT_DOUBLE_EQ(frame.pedal_steer->throttle, 0.0);  // absent -> 0.0
    EXPECT_DOUBLE_EQ(frame.pedal_steer->brake, 0.5);     // explicitly set, unaffected

    std::filesystem::remove(path);
}

TEST(ScriptedInputSourceTest, PresentButNonNumericThrottleFailsInit)
{
    // "throttle" IS present, but its value ("banana") cannot be read as a
    // number. This must NOT silently become 0.0 like the omitted case above
    // -- a present-but-malformed channel is a malformed PROFILE (the
    // experimental variable itself), not a degraded run. Init() must fail
    // loudly instead.
    const auto path = WriteTempProfile("gt_scripted_throttle_malformed.json", R"({
        "name": "throttle_malformed",
        "keyframes": [
            { "t": 0.0, "throttle": "banana" }
        ]
    })");

    ScriptedInputSource src;
    EXPECT_FALSE(src.Init(MakeConfig(path)));
    EXPECT_FALSE(src.IsConnected());

    std::filesystem::remove(path);
}

TEST(ScriptedInputSourceTest, PresentButWrongJsonTypeChannelFailsInit)
{
    // Same rule, different malformed shape: a JSON array (not a number and
    // not a numeric-string) instead of a string that merely fails to parse.
    // Pins that the rejection is type-based, not just "the string parser
    // failed" -- ReadOptionalNumericChannel's non-Number/non-String branch.
    const auto path = WriteTempProfile("gt_scripted_brake_wrongtype.json", R"({
        "name": "brake_wrong_type",
        "keyframes": [
            { "t": 0.0, "brake": [1, 2, 3] }
        ]
    })");

    ScriptedInputSource src;
    EXPECT_FALSE(src.Init(MakeConfig(path)));
    EXPECT_FALSE(src.IsConnected());

    std::filesystem::remove(path);
}

TEST(ScriptedInputSourceTest, PresentButNonNumericGearFailsInit)
{
    // gear/buttons go through a different code path (int/uint32_t cast on
    // top of ReadOptionalNumericChannel) than throttle/brake/steering --
    // pin that the same present-but-unparseable rejection applies there too.
    const auto path = WriteTempProfile("gt_scripted_gear_malformed.json", R"({
        "name": "gear_malformed",
        "keyframes": [
            { "t": 0.0, "gear": true }
        ]
    })");

    ScriptedInputSource src;
    EXPECT_FALSE(src.Init(MakeConfig(path)));
    EXPECT_FALSE(src.IsConnected());

    std::filesystem::remove(path);
}

TEST(ScriptedInputSourceTest, HoldsFirstKeyframeValuesBeforeItAndLastKeyframeValuesAfterIt)
{
    const auto path = WriteTempProfile("gt_scripted_hold.json", R"({
        "name": "hold",
        "keyframes": [
            { "t": 5.0, "throttle": 0.3, "gear": 1 },
            { "t": 6.0, "throttle": 0.9, "gear": 2 }
        ]
    })");

    // Before the first keyframe: dt=1.0 -> clock=1.0 < 5.0.
    {
        ScriptedInputSource src;
        ASSERT_TRUE(src.Init(MakeConfig(path)));
        const InputFrame frame = src.Poll(1.0);
        ASSERT_TRUE(frame.pedal_steer.has_value());
        EXPECT_DOUBLE_EQ(frame.pedal_steer->throttle, 0.3);  // first keyframe's own value
        EXPECT_EQ(frame.pedal_steer->gear, 1);
    }

    // After the last keyframe: dt=100.0 -> clock=100.0 > 6.0.
    {
        ScriptedInputSource src;
        ASSERT_TRUE(src.Init(MakeConfig(path)));
        const InputFrame frame = src.Poll(100.0);
        ASSERT_TRUE(frame.pedal_steer.has_value());
        EXPECT_DOUBLE_EQ(frame.pedal_steer->throttle, 0.9);  // last keyframe's own value, held
        EXPECT_EQ(frame.pedal_steer->gear, 2);
    }

    std::filesystem::remove(path);
}

TEST(ScriptedInputSourceTest, InternalClockAdvancesByPolledDtNotWallTime)
{
    const auto path = WriteTempProfile("gt_scripted_clock.json", R"({
        "name": "clock",
        "keyframes": [
            { "t": 0.0, "throttle": 0.0 },
            { "t": 1.0, "throttle": 1.0 }
        ]
    })");

    ScriptedInputSource src;
    ASSERT_TRUE(src.Init(MakeConfig(path)));

    // Two Poll(0.5) calls must land the sample at t=1.0 -- i.e. the source's
    // clock is driven purely by the dt ARGUMENT, never a real timer (no
    // sleep occurs between these two calls, and the assertion still holds).
    const InputFrame first = src.Poll(0.5);
    ASSERT_TRUE(first.pedal_steer.has_value());
    EXPECT_DOUBLE_EQ(first.pedal_steer->throttle, 0.5);  // t=0.5 -> midpoint

    const InputFrame second = src.Poll(0.5);
    ASSERT_TRUE(second.pedal_steer.has_value());
    EXPECT_DOUBLE_EQ(second.pedal_steer->throttle, 1.0);  // t=1.0 -> exactly the 2nd keyframe

    std::filesystem::remove(path);
}

// --- Failure policy: fail loudly, never silently degrade to zero -------

TEST(ScriptedInputSourceTest, MissingFileFailsInit)
{
    ManualDriveConfig cfg;
    cfg.input_type                  = "scripted";
    cfg.input_scripted.profile_file = (std::filesystem::temp_directory_path() /
                                        "gt_scripted_does_not_exist_12345.json")
                                           .string();

    ScriptedInputSource src;
    EXPECT_FALSE(src.Init(cfg));
    EXPECT_FALSE(src.IsConnected());
}

TEST(ScriptedInputSourceTest, MalformedJsonFailsInit)
{
    const auto path = WriteTempProfile("gt_scripted_malformed.json", R"({ "keyframes": [ this is not json )");

    ScriptedInputSource src;
    EXPECT_FALSE(src.Init(MakeConfig(path)));
    EXPECT_FALSE(src.IsConnected());

    std::filesystem::remove(path);
}

TEST(ScriptedInputSourceTest, EmptyKeyframesArrayFailsInit)
{
    const auto path = WriteTempProfile("gt_scripted_empty.json", R"({ "name": "empty", "keyframes": [] })");

    ScriptedInputSource src;
    EXPECT_FALSE(src.Init(MakeConfig(path)));
    EXPECT_FALSE(src.IsConnected());

    std::filesystem::remove(path);
}

TEST(ScriptedInputSourceTest, MissingKeyframesKeyFailsInit)
{
    const auto path = WriteTempProfile("gt_scripted_nokeyframes.json", R"({ "name": "no_keyframes" })");

    ScriptedInputSource src;
    EXPECT_FALSE(src.Init(MakeConfig(path)));
    EXPECT_FALSE(src.IsConnected());

    std::filesystem::remove(path);
}

TEST(ScriptedInputSourceTest, OutOfOrderKeyframeTimesFailsInit)
{
    const auto path = WriteTempProfile("gt_scripted_outoforder.json", R"({
        "name": "out_of_order",
        "keyframes": [
            { "t": 2.0, "throttle": 1.0 },
            { "t": 1.0, "throttle": 0.0 }
        ]
    })");

    ScriptedInputSource src;
    EXPECT_FALSE(src.Init(MakeConfig(path)));
    EXPECT_FALSE(src.IsConnected());

    std::filesystem::remove(path);
}

TEST(ScriptedInputSourceTest, DuplicateKeyframeTimeFailsInit)
{
    // Strictly increasing is required -- a tie has no well-defined
    // interpolation fraction (span == 0).
    const auto path = WriteTempProfile("gt_scripted_duplicate.json", R"({
        "name": "duplicate_t",
        "keyframes": [
            { "t": 1.0, "throttle": 0.0 },
            { "t": 1.0, "throttle": 1.0 }
        ]
    })");

    ScriptedInputSource src;
    EXPECT_FALSE(src.Init(MakeConfig(path)));
    EXPECT_FALSE(src.IsConnected());

    std::filesystem::remove(path);
}

// --- The "fabricated measurement" guard ---------------------------------
//
// A genuinely all-zero profile (the verification plan's "unresponsive"
// driver observation) must be reachable and must be provably DIFFERENT, in
// STATUS, from a failed load that happens to also produce all-zero output.
// If Init() ever silently degraded to zero on failure, this test would still
// pass by accident while the failure-policy tests above catch the bug -- the
// point of this test is the other direction: prove the successful, genuinely
// -zero case is not itself mistaken for a failure.
TEST(ScriptedInputSourceTest, UnresponsiveAllZeroProfileIsDistinctFromFailedInit)
{
    const auto path = WriteTempProfile("gt_scripted_unresponsive.json", R"({
        "name": "unresponsive",
        "description": "verification plan sec-3-3: all inputs zero for the whole run",
        "keyframes": [
            { "t": 0.0, "throttle": 0.0, "brake": 0.0, "steering": 0.0, "gear": 0, "buttons": 0 }
        ]
    })");

    ScriptedInputSource src;
    ASSERT_TRUE(src.Init(MakeConfig(path)));   // succeeded ...
    ASSERT_TRUE(src.IsConnected());            // ... and is reported as connected ...

    const InputFrame frame = src.Poll(1.0);
    ASSERT_TRUE(frame.pedal_steer.has_value());
    EXPECT_DOUBLE_EQ(frame.pedal_steer->throttle, 0.0);  // ... while the OUTPUT is legitimately all-zero.
    EXPECT_DOUBLE_EQ(frame.pedal_steer->brake, 0.0);
    EXPECT_DOUBLE_EQ(frame.pedal_steer->steering, 0.0);
    EXPECT_EQ(frame.pedal_steer->gear, 0);
    EXPECT_EQ(frame.pedal_steer->buttons, 0u);

    std::filesystem::remove(path);
}

}  // namespace gt_esmini
