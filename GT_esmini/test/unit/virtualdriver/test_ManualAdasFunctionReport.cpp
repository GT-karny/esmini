#include <gtest/gtest.h>

#include "gt_esmini/control/virtualdriver/AdasFunctionReport.hpp"

#include <string>

using namespace gt_esmini;

// ============================================================================
// req-vd-ad:REQ-AD-025 / REQ-AD-028, vd-func:FUNC-075 — ManualDrive ADAS
// coexistence report, phase A (AEB + its FCW warning pre-stage only; ACC/LKA/
// MSL are phases C/D and must not appear here).
//
// Pins the design decided in manualdrive_adas_design.md §8-1/§8-2:
//
//   AEB -> NAME_AUTOMATIC_EMERGENCY_BRAKING (7), custom_name "gt.aeb"
//   FCW -> NAME_FORWARD_COLLISION_WARNING (3), custom_name "gt.fcw"
//   NO aggregate "gt.virtual_driver" / NAME_URBAN_DRIVING row (§8-1: that row
//   is semantically wrong when a human is the one driving).
//
//   State (REQ-AD-028 step a's 3-value discipline, slug
//   md-state-three-value-discipline):
//     UNAVAILABLE = config disabled OR ManualDrive does not own the
//                   longitudinal domain (slug md-split-no-double-equipment)
//     STANDBY     = enabled + owning, but not intervening/warning this frame
//     ACTIVE      = intervening (AEB) / warning (FCW) this frame
// ============================================================================

namespace
{
const AdasFunctionState* Find(const std::vector<AdasFunctionState>& r, const char* custom_name)
{
    for (const auto& f : r)
        if (f.custom_name == custom_name) return &f;
    return nullptr;
}

ManualAdasEnableFlags AllEnabled()
{
    ManualAdasEnableFlags f;
    f.aeb = true;
    f.fcw = true;
    return f;
}
}  // namespace

TEST(ManualAdasFunctionReport, BothRowsCarryTheirNativeOsiNameNeverOther)
{
    ManualAdasDecision decision;  // quiet frame -- state is not what this test checks
    PolicyDetail        detail;

    // named local: Find() returns a pointer INTO the vector; binding the call
    // result to a temporary would dangle once the full expression ends (see
    // test_AdasFunctionReport.cpp's same note re: MSVC Debug heap 0xDDDDDDDD).
    const auto  report = BuildManualAdasFunctionReport(AllEnabled(), /*owns_longitudinal_domain=*/true, /*owns_lateral_domain=*/false, decision, detail);

    const auto* aeb = Find(report, "gt.aeb");
    ASSERT_NE(aeb, nullptr);
    EXPECT_EQ(aeb->name, osi_adas::NAME_AUTOMATIC_EMERGENCY_BRAKING);
    EXPECT_NE(aeb->name, osi_adas::NAME_OTHER);

    const auto* fcw = Find(report, "gt.fcw");
    ASSERT_NE(fcw, nullptr);
    EXPECT_EQ(fcw->name, osi_adas::NAME_FORWARD_COLLISION_WARNING);
    EXPECT_NE(fcw->name, osi_adas::NAME_OTHER);
}

TEST(ManualAdasFunctionReport, AebDisabledInConfigIsUnavailable)
{
    ManualAdasEnableFlags flags;  // aeb = false (default)
    ManualAdasDecision    decision;
    PolicyDetail           detail;

    const auto  report = BuildManualAdasFunctionReport(flags, /*owns_longitudinal_domain=*/true, /*owns_lateral_domain=*/false, decision, detail);
    const auto* aeb     = Find(report, "gt.aeb");

    ASSERT_NE(aeb, nullptr);
    EXPECT_EQ(aeb->state, osi_adas::STATE_UNAVAILABLE);
}

TEST(ManualAdasFunctionReport, AebEnabledButQuietIsStandby)
{
    ManualAdasDecision decision;  // aeb_intervening = false (default)
    PolicyDetail        detail;

    const auto  report = BuildManualAdasFunctionReport(AllEnabled(), /*owns_longitudinal_domain=*/true, /*owns_lateral_domain=*/false, decision, detail);
    const auto* aeb     = Find(report, "gt.aeb");

    ASSERT_NE(aeb, nullptr);
    EXPECT_EQ(aeb->state, osi_adas::STATE_STANDBY);
}

// REQ-AD-028 step a's whole point: "switched off" and "watching and chose not
// to fire" must be two DIFFERENT, both-reachable verdicts about the same
// silence -- not one collapsing into the other under some code path.
TEST(ManualAdasFunctionReport, StandbyAndUnavailableAreDistinguishableAndBothReachable)
{
    ManualAdasDecision decision;  // quiet in both cases
    PolicyDetail        detail;

    ManualAdasEnableFlags disabled;  // aeb = false
    ManualAdasEnableFlags enabled = AllEnabled();

    const auto  report_off = BuildManualAdasFunctionReport(disabled, /*owns_longitudinal_domain=*/true, /*owns_lateral_domain=*/false, decision, detail);
    const auto* aeb_off    = Find(report_off, "gt.aeb");
    ASSERT_NE(aeb_off, nullptr);

    const auto  report_on = BuildManualAdasFunctionReport(enabled, /*owns_longitudinal_domain=*/true, /*owns_lateral_domain=*/false, decision, detail);
    const auto* aeb_on    = Find(report_on, "gt.aeb");
    ASSERT_NE(aeb_on, nullptr);

    EXPECT_EQ(aeb_off->state, osi_adas::STATE_UNAVAILABLE);
    EXPECT_EQ(aeb_on->state, osi_adas::STATE_STANDBY);
    EXPECT_NE(aeb_off->state, aeb_on->state);
}

TEST(ManualAdasFunctionReport, AebInterveningIsActive)
{
    ManualAdasDecision decision;
    decision.aeb_intervening = true;
    PolicyDetail detail;

    const auto  report = BuildManualAdasFunctionReport(AllEnabled(), /*owns_longitudinal_domain=*/true, /*owns_lateral_domain=*/false, decision, detail);
    const auto* aeb     = Find(report, "gt.aeb");

    ASSERT_NE(aeb, nullptr);
    EXPECT_EQ(aeb->state, osi_adas::STATE_ACTIVE);
    EXPECT_EQ(aeb->name, osi_adas::NAME_AUTOMATIC_EMERGENCY_BRAKING);
}

// design §2-3 (slug md-split-no-double-equipment): in a split configuration
// where VirtualDriver owns LONGITUDINAL, ManualDrive must not double-equip AEB
// even though its own config says enabled -- both must report UNAVAILABLE, not
// STANDBY (STANDBY would falsely claim ManualDrive is armed and watching).
TEST(ManualAdasFunctionReport, NotOwningLongitudinalDomainIsUnavailableEvenIfConfigEnabled)
{
    ManualAdasDecision decision;
    PolicyDetail        detail;

    const auto  report = BuildManualAdasFunctionReport(AllEnabled(), /*owns_longitudinal_domain=*/false, /*owns_lateral_domain=*/false, decision, detail);
    const auto* aeb     = Find(report, "gt.aeb");

    ASSERT_NE(aeb, nullptr);
    EXPECT_EQ(aeb->state, osi_adas::STATE_UNAVAILABLE);
}

// REQ-AD-025 step e: the warning precedes intervention. A frame where TTC has
// crossed the (looser) warning threshold but not yet the intervention
// threshold must show FCW=ACTIVE alongside AEB=STANDBY, not ACTIVE on both or
// neither -- that is the observable evidence that the two thresholds are
// actually distinct in the report, not just in the design prose.
TEST(ManualAdasFunctionReport, FcwWarningActivatesFcwRowWhileAebRowStaysStandby)
{
    ManualAdasDecision decision;
    decision.fcw_warning = true;  // aeb_intervening stays false
    PolicyDetail detail;

    const auto  report = BuildManualAdasFunctionReport(AllEnabled(), /*owns_longitudinal_domain=*/true, /*owns_lateral_domain=*/false, decision, detail);

    const auto* fcw = Find(report, "gt.fcw");
    ASSERT_NE(fcw, nullptr);
    EXPECT_EQ(fcw->state, osi_adas::STATE_ACTIVE);

    const auto* aeb = Find(report, "gt.aeb");
    ASSERT_NE(aeb, nullptr);
    EXPECT_EQ(aeb->state, osi_adas::STATE_STANDBY);
}

// design §8-4: gt.aeb.warning and the pre-existing AebSafety diagnostics
// (gt.aeb.ttc_s / gt.aeb.a_req_mps2 / gt.aeb.triggered, ...) all share the
// "gt.aeb." key prefix, so key-prefix routing puts them ALL on the AEB row --
// including gt.aeb.warning, even though it is semantically an FCW quantity.
// That is intentional (see AdasFunctionReport.cpp), not a bug: the design
// table lists it under the gt.aeb.* key, and consumers reading "why did AEB's
// row look the way it did" get the FCW warning flag alongside the rest of the
// AEB diagnostics for free.
TEST(ManualAdasFunctionReport, AebDiagnosticsAreRoutedOnlyToTheAebRow)
{
    ManualAdasDecision decision;
    PolicyDetail        detail;
    detail.emplace_back("gt.aeb.ttc_s", "1.842");
    detail.emplace_back("gt.aeb.a_req_mps2", "-6.500");
    detail.emplace_back("gt.aeb.triggered", "false");
    detail.emplace_back("gt.aeb.warning", "true");

    const auto report = BuildManualAdasFunctionReport(AllEnabled(), /*owns_longitudinal_domain=*/true, /*owns_lateral_domain=*/false, decision, detail);

    const auto* aeb = Find(report, "gt.aeb");
    ASSERT_NE(aeb, nullptr);
    ASSERT_EQ(aeb->detail.size(), 4u);
    EXPECT_EQ(aeb->detail[0].first, "gt.aeb.ttc_s");
    EXPECT_EQ(aeb->detail[1].first, "gt.aeb.a_req_mps2");
    EXPECT_EQ(aeb->detail[2].first, "gt.aeb.triggered");
    EXPECT_EQ(aeb->detail[3].first, "gt.aeb.warning");

    const auto* fcw = Find(report, "gt.fcw");
    ASSERT_NE(fcw, nullptr);
    EXPECT_TRUE(fcw->detail.empty());
}

// design §8-1: the VD's aggregate "gt.virtual_driver" / NAME_URBAN_DRIVING row
// asserts "an automated driving function has control", which is false in a
// manual-driving context (the human is driving). It must not appear here.
TEST(ManualAdasFunctionReport, ReportHasNoAggregateRow)
{
    ManualAdasDecision decision;
    PolicyDetail        detail;

    const auto report = BuildManualAdasFunctionReport(AllEnabled(), /*owns_longitudinal_domain=*/true, /*owns_lateral_domain=*/false, decision, detail);

    for (const auto& f : report)
    {
        EXPECT_NE(f.name, osi_adas::NAME_URBAN_DRIVING) << f.custom_name;
        EXPECT_NE(f.custom_name, "gt.virtual_driver");
    }
}

// ============================================================================
// req-vd-ad:REQ-AD-028 段b (phase B) -- DriverOverride / custom_state.
//
// design §8-3 maps override causes onto OSI: brake -> REASON_BRAKE_PEDAL,
// steering -> REASON_STEERING_INPUT, accelerator -> NO Reason value exists, so
// custom_state carries kDriverOverrideAccel instead. Phase B builds the
// mechanism and wires the ACCELERATOR producer only (kickdown); the brake and
// steering producers are ACC (phase C) and LKA (phase D) and cannot be
// exercised here because nothing yet generates them.
// ============================================================================

// The positive: the accelerator override reaches BOTH observable fields at
// once. Splitting them would let a half-populated row (active but no
// custom_state, or vice versa) pass -- and a consumer reading only one of the
// two would then draw a different conclusion than one reading the other.
TEST(ManualAdasFunctionReport, AccelOverrideSetsActiveAndCustomStateOnTheAebRow)
{
    ManualAdasDecision decision;
    decision.driver_override_accel = true;
    PolicyDetail detail;

    const auto  report = BuildManualAdasFunctionReport(AllEnabled(), /*owns_longitudinal_domain=*/true, /*owns_lateral_domain=*/false, decision, detail);
    const auto* aeb     = Find(report, "gt.aeb");

    ASSERT_NE(aeb, nullptr);
    EXPECT_TRUE(aeb->driver_override.reported);
    EXPECT_TRUE(aeb->driver_override.active);
    EXPECT_EQ(aeb->custom_state, std::string(kDriverOverrideAccel));

    // OSI's Reason enum has no accelerator value (osi_adas::OverrideReason),
    // and picking the "nearest" one would misreport which control the human
    // actually used. Empty is the honest encoding, and custom_state above is
    // what carries the information.
    EXPECT_TRUE(aeb->driver_override.reasons.empty());
}

// The negative that makes the positive mean something: same row, same enabled
// config, no override -- the channel is still REPORTED (the stack looked), but
// inactive and with no custom_state. Without this, "override observed" could
// not be distinguished from "this row always says active".
TEST(ManualAdasFunctionReport, NoAccelOverrideStillReportsTheChannelAsInactive)
{
    ManualAdasDecision decision;  // driver_override_accel = false
    PolicyDetail        detail;

    const auto  report = BuildManualAdasFunctionReport(AllEnabled(), /*owns_longitudinal_domain=*/true, /*owns_lateral_domain=*/false, decision, detail);
    const auto* aeb     = Find(report, "gt.aeb");

    ASSERT_NE(aeb, nullptr);
    EXPECT_TRUE(aeb->driver_override.reported);  // evaluated ...
    EXPECT_FALSE(aeb->driver_override.active);   // ... and found nothing
    EXPECT_TRUE(aeb->custom_state.empty());
}

// "Evaluated and found no override" vs "nobody ever looked" is the same
// distinction REQ-AD-028 段a draws between STANDBY and UNAVAILABLE, applied to
// the override channel. A closed gate (config off, or the longitudinal domain
// owned by someone else) must leave the channel UNREPORTED so it reaches OSI
// as an absent submessage -- a face-3 consumer cannot evidence "the driver did
// not override" from a function that was not running.
TEST(ManualAdasFunctionReport, ClosedGateLeavesTheOverrideChannelUnreported)
{
    ManualAdasDecision decision;
    decision.driver_override_accel = true;  // even with an override asserted
    PolicyDetail detail;

    ManualAdasEnableFlags disabled;  // aeb = fcw = false

    const auto  report_off = BuildManualAdasFunctionReport(disabled, /*owns_longitudinal_domain=*/true, /*owns_lateral_domain=*/false, decision, detail);
    const auto* aeb_off    = Find(report_off, "gt.aeb");
    ASSERT_NE(aeb_off, nullptr);
    ASSERT_EQ(aeb_off->state, osi_adas::STATE_UNAVAILABLE);
    EXPECT_FALSE(aeb_off->driver_override.reported);
    EXPECT_FALSE(aeb_off->driver_override.active);
    EXPECT_TRUE(aeb_off->custom_state.empty());

    const auto  report_split = BuildManualAdasFunctionReport(AllEnabled(), /*owns_longitudinal_domain=*/false, /*owns_lateral_domain=*/false, decision, detail);
    const auto* aeb_split    = Find(report_split, "gt.aeb");
    ASSERT_NE(aeb_split, nullptr);
    ASSERT_EQ(aeb_split->state, osi_adas::STATE_UNAVAILABLE);
    EXPECT_FALSE(aeb_split->driver_override.reported);
    EXPECT_FALSE(aeb_split->driver_override.active);
    EXPECT_TRUE(aeb_split->custom_state.empty());
}

// Kickdown suppresses INTERVENTION, not the WARNING: suppressing FCW too would
// remove the driver's last cue exactly when they are accelerating toward a
// hazard. Consequence for verification: within a single frame of a single run,
// gt.aeb reports an active override while gt.fcw reports an inactive one --
// an in-run negative control for driver_override_reported that needs no second
// scenario.
TEST(ManualAdasFunctionReport, AccelOverrideMarksAebButNeverFcw)
{
    ManualAdasDecision decision;
    decision.driver_override_accel = true;
    PolicyDetail detail;

    const auto report = BuildManualAdasFunctionReport(AllEnabled(), /*owns_longitudinal_domain=*/true, /*owns_lateral_domain=*/false, decision, detail);

    const auto* aeb = Find(report, "gt.aeb");
    ASSERT_NE(aeb, nullptr);
    const auto* fcw = Find(report, "gt.fcw");
    ASSERT_NE(fcw, nullptr);

    EXPECT_TRUE(aeb->driver_override.active);
    EXPECT_TRUE(fcw->driver_override.reported);  // FCW's channel was evaluated ...
    EXPECT_FALSE(fcw->driver_override.active);   // ... and the warning is NOT overridden
    EXPECT_TRUE(fcw->custom_state.empty());
    EXPECT_NE(aeb->driver_override.active, fcw->driver_override.active);
}

// The override is orthogonal to State: a suppressed AEB is STANDBY (armed,
// watching, not intervening) WHILE reporting an active override. Collapsing
// the two -- e.g. reporting UNAVAILABLE during suppression -- would erase the
// REQ-AD-028 段a distinction the phase-A report was built to preserve, and
// would make a suppressed AEB indistinguishable from a switched-off one.
TEST(ManualAdasFunctionReport, AccelOverrideDoesNotChangeTheAebState)
{
    PolicyDetail detail;

    ManualAdasDecision quiet;  // no intervention, no override
    ManualAdasDecision suppressed;
    suppressed.driver_override_accel = true;  // override, still no intervention

    const auto  r_quiet = BuildManualAdasFunctionReport(AllEnabled(), /*owns_longitudinal_domain=*/true, /*owns_lateral_domain=*/false, quiet, detail);
    const auto* a_quiet = Find(r_quiet, "gt.aeb");
    ASSERT_NE(a_quiet, nullptr);

    const auto  r_supp = BuildManualAdasFunctionReport(AllEnabled(), /*owns_longitudinal_domain=*/true, /*owns_lateral_domain=*/false, suppressed, detail);
    const auto* a_supp = Find(r_supp, "gt.aeb");
    ASSERT_NE(a_supp, nullptr);

    EXPECT_EQ(a_quiet->state, osi_adas::STATE_STANDBY);
    EXPECT_EQ(a_supp->state, osi_adas::STATE_STANDBY);
    EXPECT_NE(a_quiet->driver_override.active, a_supp->driver_override.active);
}

// Phase A implements only AEB + FCW (design §10 phase table). The report must
// never emit rows for functions later phases add (ACC/LKA/MSL) -- there is no
// input to this function that could even ask for them yet, but this pins the
// row SET itself so a future phase's row addition is a deliberate, visible
// change here rather than an accidental widening.
TEST(ManualAdasFunctionReport, ReportContainsExactlyAebAndFcwRows)
{
    ManualAdasDecision decision;
    PolicyDetail        detail;

    const auto report = BuildManualAdasFunctionReport(AllEnabled(), /*owns_longitudinal_domain=*/true, /*owns_lateral_domain=*/false, decision, detail);

    ASSERT_EQ(report.size(), 2u);

    bool has_aeb = false;
    bool has_fcw = false;
    for (const auto& f : report)
    {
        if (f.custom_name == "gt.aeb") has_aeb = true;
        else if (f.custom_name == "gt.fcw") has_fcw = true;
        else ADD_FAILURE() << "unexpected row: " << f.custom_name;
    }
    EXPECT_TRUE(has_aeb);
    EXPECT_TRUE(has_fcw);
}
