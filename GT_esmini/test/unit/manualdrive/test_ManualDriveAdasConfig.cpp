// req-vd-ad:REQ-AD-025..031, vd-func:FUNC-075
//
// Unit tests for ManualDriveConfig's phase-A `adas` block and the
// `input_scripted` block (manualdrive_adas_design.md §9 phase-A subset;
// design §10 phase table -- ACC/LKA/MSL config is out of scope until phases
// C/D). See ManualDriveConfig.hpp's PARSER NOTE: LoadFromFile() matches keys
// by flat substring search across the WHOLE FILE, not by JSON object scope,
// so every on-disk key here is deliberately prefixed
// ("adas_aeb_enabled", not "enabled") to stay unique against pre-existing
// keys such as override_cfg's own "enabled". The alias-regression test below
// is the one that actually proves that discipline holds.

#include <gtest/gtest.h>

#include "gt_esmini/control/manualdrive/ManualDriveConfig.hpp"

#include <filesystem>
#include <fstream>

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

// --- Shipped defaults: everything OFF / harmless ------------------------

TEST(ManualDriveAdasConfigTest, ShippedDefaultsAreAllOff)
{
    // No "adas" / "input_scripted" content at all -- simulates a config file
    // predating this feature (or one that simply never mentions these keys).
    const auto path = WriteTempConfig("gt_mdadas_defaults.json", R"({
        "input_type": "sdl2_wheel"
    })");

    ManualDriveConfig cfg;
    ASSERT_TRUE(cfg.LoadFromFile(path.string()));

    EXPECT_FALSE(cfg.adas.aeb.enabled);
    EXPECT_TRUE(cfg.adas.aeb.kickdown_suppress_enabled);
    EXPECT_DOUBLE_EQ(cfg.adas.aeb.warning_ttc_threshold_s, 3.5);
    EXPECT_DOUBLE_EQ(cfg.adas.aeb.warning_min_a_req_mps2, 2.0);
    EXPECT_DOUBLE_EQ(cfg.adas.brake_control.full_brake_decel_mps2, 8.0);
    EXPECT_DOUBLE_EQ(cfg.adas.brake_control.brake_kp, 0.05);
    EXPECT_DOUBLE_EQ(cfg.adas.brake_control.brake_ki, 0.6);
    EXPECT_DOUBLE_EQ(cfg.adas.kickdown_threshold, 0.95);
    EXPECT_DOUBLE_EQ(cfg.adas.kickdown_release_threshold, 0.80);
    EXPECT_EQ(cfg.input_scripted.profile_file, "");

    std::filesystem::remove(path);
}

// --- Every new key parses --------------------------------------------------

TEST(ManualDriveAdasConfigTest, EveryAdasKeyParsesToTheFileValue)
{
    const auto path = WriteTempConfig("gt_mdadas_full.json", R"({
        "input_type": "scripted",
        "adas": {
            "adas_aeb_enabled": true,
            "adas_aeb_kickdown_suppress_enabled": false,
            "adas_aeb_warning_ttc_threshold_s": 4.2,
            "adas_aeb_warning_min_a_req_mps2": 1.25,
            "adas_brake_full_decel_mps2": 7.5,
            "adas_brake_kp": 0.11,
            "adas_brake_ki": 0.9,
            "adas_kickdown_threshold": 0.9,
            "adas_kickdown_release_threshold": 0.7
        },
        "input_scripted": {
            "input_scripted_profile_file": "profiles/kickdown_at.json"
        }
    })");

    ManualDriveConfig cfg;
    ASSERT_TRUE(cfg.LoadFromFile(path.string()));

    EXPECT_EQ(cfg.input_type, "scripted");
    EXPECT_TRUE(cfg.adas.aeb.enabled);
    EXPECT_FALSE(cfg.adas.aeb.kickdown_suppress_enabled);
    EXPECT_DOUBLE_EQ(cfg.adas.aeb.warning_ttc_threshold_s, 4.2);
    EXPECT_DOUBLE_EQ(cfg.adas.aeb.warning_min_a_req_mps2, 1.25);
    EXPECT_DOUBLE_EQ(cfg.adas.brake_control.full_brake_decel_mps2, 7.5);
    EXPECT_DOUBLE_EQ(cfg.adas.brake_control.brake_kp, 0.11);
    EXPECT_DOUBLE_EQ(cfg.adas.brake_control.brake_ki, 0.9);
    EXPECT_DOUBLE_EQ(cfg.adas.kickdown_threshold, 0.9);
    EXPECT_DOUBLE_EQ(cfg.adas.kickdown_release_threshold, 0.7);
    EXPECT_EQ(cfg.input_scripted.profile_file, "profiles/kickdown_at.json");

    std::filesystem::remove(path);
}

// --- Partial block: unset keys keep their own defaults --------------------

TEST(ManualDriveAdasConfigTest, PartialAdasBlockLeavesUnspecifiedKeysAtDefault)
{
    const auto path = WriteTempConfig("gt_mdadas_partial.json", R"({
        "adas": {
            "adas_aeb_enabled": true
        }
    })");

    ManualDriveConfig cfg;
    ASSERT_TRUE(cfg.LoadFromFile(path.string()));

    EXPECT_TRUE(cfg.adas.aeb.enabled);  // the one key the file set

    // Everything else must remain at its compiled-in default -- a partial
    // override must not zero out the rest of the block.
    EXPECT_TRUE(cfg.adas.aeb.kickdown_suppress_enabled);
    EXPECT_DOUBLE_EQ(cfg.adas.aeb.warning_ttc_threshold_s, 3.5);
    EXPECT_DOUBLE_EQ(cfg.adas.aeb.warning_min_a_req_mps2, 2.0);
    EXPECT_DOUBLE_EQ(cfg.adas.brake_control.full_brake_decel_mps2, 8.0);
    EXPECT_DOUBLE_EQ(cfg.adas.brake_control.brake_kp, 0.05);
    EXPECT_DOUBLE_EQ(cfg.adas.brake_control.brake_ki, 0.6);
    EXPECT_DOUBLE_EQ(cfg.adas.kickdown_threshold, 0.95);
    EXPECT_DOUBLE_EQ(cfg.adas.kickdown_release_threshold, 0.80);

    std::filesystem::remove(path);
}

// --- The FCW gate is a PAIR of keys, and both halves must be settable ------
//
// req-vd-ad:REQ-AD-028 (phase B), closing the gap design §9/§12 recorded in
// phase A. DeriveFcwGateConfig (AdasCoexistenceStack.cpp) builds the warning
// gate by clamping BOTH warning_ttc_threshold_s AND warning_min_a_req_mps2,
// so the warning fires only where both admit it. Phase A shipped a config key
// for the first alone, which meant that on any encounter where required
// deceleration was the binding side, moving the one exposed key could not move
// the warning point at all -- a calibration dead end rather than a wrong
// number.
//
// The two keys are also given DIFFERENT non-default values here on purpose:
// they share the "adas_aeb_warning_" prefix, and ManualDriveConfig's loader
// matches by flat substring, so a truncated/duplicated key token would alias
// them. Distinct values make that aliasing a failure rather than a silent
// coincidence (same discipline as the override/adas alias guard at the bottom
// of this file).
TEST(ManualDriveAdasConfigTest, BothFcwGateThresholdsAreIndependentlySettable)
{
    const auto path = WriteTempConfig("gt_mdadas_fcw_pair.json", R"({
        "adas": {
            "adas_aeb_warning_ttc_threshold_s": 4.75,
            "adas_aeb_warning_min_a_req_mps2": 0.5
        }
    })");

    ManualDriveConfig cfg;
    ASSERT_TRUE(cfg.LoadFromFile(path.string()));

    EXPECT_DOUBLE_EQ(cfg.adas.aeb.warning_ttc_threshold_s, 4.75);
    EXPECT_DOUBLE_EQ(cfg.adas.aeb.warning_min_a_req_mps2, 0.5);
    EXPECT_NE(cfg.adas.aeb.warning_ttc_threshold_s, cfg.adas.aeb.warning_min_a_req_mps2);

    std::filesystem::remove(path);
}

// Setting ONLY the min_a_req half must move that half and leave the TTC half
// at its default -- the mirror of the phase-A situation, proving the new key
// is not merely present but independently effective.
TEST(ManualDriveAdasConfigTest, WarningMinAReqAloneDoesNotDisturbWarningTtc)
{
    const auto path = WriteTempConfig("gt_mdadas_fcw_min_only.json", R"({
        "adas": {
            "adas_aeb_warning_min_a_req_mps2": 1.75
        }
    })");

    ManualDriveConfig cfg;
    ASSERT_TRUE(cfg.LoadFromFile(path.string()));

    EXPECT_DOUBLE_EQ(cfg.adas.aeb.warning_min_a_req_mps2, 1.75);
    EXPECT_DOUBLE_EQ(cfg.adas.aeb.warning_ttc_threshold_s, 3.5);  // untouched default

    std::filesystem::remove(path);
}

// --- input_type "scripted" + profile_file round-trip -----------------------

TEST(ManualDriveAdasConfigTest, ScriptedInputTypeAndProfileFileRoundTrip)
{
    const auto path = WriteTempConfig("gt_mdadas_scripted_input.json", R"({
        "input_type": "scripted",
        "input_scripted": {
            "input_scripted_profile_file": "C:/runs/profiles/unresponsive.json"
        }
    })");

    ManualDriveConfig cfg;
    ASSERT_TRUE(cfg.LoadFromFile(path.string()));

    EXPECT_EQ(cfg.input_type, "scripted");
    EXPECT_EQ(cfg.input_scripted.profile_file, "C:/runs/profiles/unresponsive.json");

    // Unrelated ADAS defaults must be untouched by setting the input block.
    EXPECT_FALSE(cfg.adas.aeb.enabled);

    std::filesystem::remove(path);
}

// --- config_dir bookkeeping for relative profile_file resolution ----------

TEST(ManualDriveAdasConfigTest, LoadFromFilePopulatesConfigDirFromTheGivenPath)
{
    const auto path = WriteTempConfig("gt_mdadas_configdir.json", R"({ "input_type": "stub" })");

    ManualDriveConfig cfg;
    ASSERT_TRUE(cfg.LoadFromFile(path.string()));

    EXPECT_EQ(cfg.config_dir, path.parent_path().string());

    std::filesystem::remove(path);
}

// --- Alias-regression guard -------------------------------------------
//
// This is the test the parser's flat-substring-matching quirk exists to
// defend against. override_cfg.enabled and adas.aeb.enabled are DIFFERENT
// C++ fields that must be driven by DIFFERENT on-disk JSON keys
// ("enabled" under "override" vs "adas_aeb_enabled" under "adas"). Both
// keys are given values that are the OPPOSITE of each field's own compiled
// -in default (override_cfg.enabled defaults true; adas.aeb.enabled
// defaults false), so if the two keys ever aliased -- e.g. a future edit
// accidentally reused the bare "enabled" token for the adas field -- the
// LAST line containing "enabled" would stamp its value onto BOTH fields and
// this test would fail (both ending up equal, rather than each holding its
// own file-specified value).
TEST(ManualDriveAdasConfigTest, OverrideEnabledAndAdasAebEnabledDoNotAlias)
{
    const auto path = WriteTempConfig("gt_mdadas_alias_guard.json", R"({
        "override": {
            "enabled": false
        },
        "adas": {
            "adas_aeb_enabled": true
        }
    })");

    ManualDriveConfig cfg;
    ASSERT_TRUE(cfg.LoadFromFile(path.string()));

    EXPECT_FALSE(cfg.override_cfg.enabled);  // explicit file value, opposite of its own default (true)
    EXPECT_TRUE(cfg.adas.aeb.enabled);       // explicit file value, opposite of its own default (false)

    std::filesystem::remove(path);
}

}  // namespace gt_esmini
