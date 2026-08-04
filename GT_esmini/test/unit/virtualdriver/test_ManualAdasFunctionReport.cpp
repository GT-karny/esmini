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
    const auto  report = BuildManualAdasFunctionReport(AllEnabled(), /*owns_longitudinal_domain=*/true, decision, detail);

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

    const auto  report = BuildManualAdasFunctionReport(flags, /*owns_longitudinal_domain=*/true, decision, detail);
    const auto* aeb     = Find(report, "gt.aeb");

    ASSERT_NE(aeb, nullptr);
    EXPECT_EQ(aeb->state, osi_adas::STATE_UNAVAILABLE);
}

TEST(ManualAdasFunctionReport, AebEnabledButQuietIsStandby)
{
    ManualAdasDecision decision;  // aeb_intervening = false (default)
    PolicyDetail        detail;

    const auto  report = BuildManualAdasFunctionReport(AllEnabled(), /*owns_longitudinal_domain=*/true, decision, detail);
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

    const auto  report_off = BuildManualAdasFunctionReport(disabled, /*owns_longitudinal_domain=*/true, decision, detail);
    const auto* aeb_off    = Find(report_off, "gt.aeb");
    ASSERT_NE(aeb_off, nullptr);

    const auto  report_on = BuildManualAdasFunctionReport(enabled, /*owns_longitudinal_domain=*/true, decision, detail);
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

    const auto  report = BuildManualAdasFunctionReport(AllEnabled(), /*owns_longitudinal_domain=*/true, decision, detail);
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

    const auto  report = BuildManualAdasFunctionReport(AllEnabled(), /*owns_longitudinal_domain=*/false, decision, detail);
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

    const auto  report = BuildManualAdasFunctionReport(AllEnabled(), /*owns_longitudinal_domain=*/true, decision, detail);

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

    const auto report = BuildManualAdasFunctionReport(AllEnabled(), /*owns_longitudinal_domain=*/true, decision, detail);

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

    const auto report = BuildManualAdasFunctionReport(AllEnabled(), /*owns_longitudinal_domain=*/true, decision, detail);

    for (const auto& f : report)
    {
        EXPECT_NE(f.name, osi_adas::NAME_URBAN_DRIVING) << f.custom_name;
        EXPECT_NE(f.custom_name, "gt.virtual_driver");
    }
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

    const auto report = BuildManualAdasFunctionReport(AllEnabled(), /*owns_longitudinal_domain=*/true, decision, detail);

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
