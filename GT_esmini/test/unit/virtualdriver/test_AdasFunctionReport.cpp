#include <gtest/gtest.h>

#include "gt_esmini/control/virtualdriver/AdasFunctionReport.hpp"

#include <string>

using namespace gt_esmini;

// ============================================================================
// W1 (capability_model §2.2a): VirtualDriver emitted NOTHING into OSI
// HostVehicleData.vehicle_automated_driving_function[], so AEB — implemented,
// tested, green — was not observable from outside. These pin the policy -> OSI
// mapping decided 2026-07-20:
//
//   aeb          -> NAME_AUTOMATIC_EMERGENCY_BRAKING (7)
//   lead_vehicle -> NAME_ADAPTIVE_CRUISE_CONTROL (10)
//   the other 4  -> NAME_OTHER (1) + custom_name (no standard name exists)
//   plus one aggregate NAME_URBAN_DRIVING (22) row = "the VD stack is driving"
//
//   State: ACTIVE   = emitted a constraint this frame
//          STANDBY  = enabled but quiet this frame
//          UNAVAILABLE = policy disabled in config
// ============================================================================

namespace
{
const AdasFunctionState* Find(const std::vector<AdasFunctionState>& r, const char* custom_name)
{
    for (const auto& f : r)
        if (f.custom_name == custom_name) return &f;
    return nullptr;
}

VdPolicyEnableFlags AllEnabled()
{
    VdPolicyEnableFlags f;
    f.lead = f.traffic_light = f.stop_yield = f.conflict = f.crosswalk = f.aeb = true;
    return f;
}

PolicyConstraint FromSource(const char* source)
{
    PolicyConstraint c;
    c.kind   = PolicyConstraint::Kind::STOP_AT_S;
    c.source = source;
    return c;
}
}  // namespace

TEST(AdasFunctionReport, DisabledPolicyIsReportedUnavailable)
{
    VdPolicyEnableFlags flags;  // everything false by default
    TrafficPolicySnapshot snap;

    const auto report = BuildAdasFunctionReport(flags, snap);

    const auto* aeb = Find(report, "gt.aeb");
    ASSERT_NE(aeb, nullptr);
    EXPECT_EQ(aeb->state, osi_adas::STATE_UNAVAILABLE);
}

TEST(AdasFunctionReport, EnabledButQuietPolicyIsReportedStandby)
{
    TrafficPolicySnapshot snap;  // no constraints this frame

    const auto report = BuildAdasFunctionReport(AllEnabled(), snap);

    const auto* aeb = Find(report, "gt.aeb");
    ASSERT_NE(aeb, nullptr);
    EXPECT_EQ(aeb->state, osi_adas::STATE_STANDBY);
}

TEST(AdasFunctionReport, PolicyEmittingAConstraintIsReportedActive)
{
    TrafficPolicySnapshot snap;
    snap.constraints.push_back(FromSource("aeb"));

    const auto report = BuildAdasFunctionReport(AllEnabled(), snap);

    const auto* aeb = Find(report, "gt.aeb");
    ASSERT_NE(aeb, nullptr);
    EXPECT_EQ(aeb->state, osi_adas::STATE_ACTIVE);
    EXPECT_EQ(aeb->name, osi_adas::NAME_AUTOMATIC_EMERGENCY_BRAKING);
}

TEST(AdasFunctionReport, LeadVehicleMapsToTheStandardAdaptiveCruiseControlName)
{
    TrafficPolicySnapshot snap;
    snap.constraints.push_back(FromSource("lead_vehicle"));

    // NB: bind the report to a named local first -- Find() returns a pointer INTO the
    // vector, so inlining BuildAdasFunctionReport() here would dangle once the temporary
    // is destroyed at the end of the full expression (0xDDDDDDDD under the MSVC Debug
    // heap; Release masked it by leaving the freed bytes intact).
    const auto  report = BuildAdasFunctionReport(AllEnabled(), snap);
    const auto* lead   = Find(report, "gt.lead_vehicle");
    ASSERT_NE(lead, nullptr);
    EXPECT_EQ(lead->name, osi_adas::NAME_ADAPTIVE_CRUISE_CONTROL);
    EXPECT_EQ(lead->state, osi_adas::STATE_ACTIVE);
}

TEST(AdasFunctionReport, PoliciesWithoutAStandardNameUseOtherPlusCustomName)
{
    const auto report = BuildAdasFunctionReport(AllEnabled(), TrafficPolicySnapshot{});

    for (const char* name : {"gt.traffic_light", "gt.stop_yield", "gt.conflict_point", "gt.crosswalk"})
    {
        const auto* f = Find(report, name);
        ASSERT_NE(f, nullptr) << name;
        EXPECT_EQ(f->name, osi_adas::NAME_OTHER) << name;
    }
}

TEST(AdasFunctionReport, StopYieldIsActiveForEitherStopOrYieldSource)
{
    for (const char* source : {"stop_sign", "yield_sign"})
    {
        TrafficPolicySnapshot snap;
        snap.constraints.push_back(FromSource(source));

        // named local: Find() points into the vector; a temporary would dangle (see
        // LeadVehicleMapsToTheStandardAdaptiveCruiseControlName).
        const auto  report = BuildAdasFunctionReport(AllEnabled(), snap);
        const auto* f      = Find(report, "gt.stop_yield");
        ASSERT_NE(f, nullptr) << source;
        EXPECT_EQ(f->state, osi_adas::STATE_ACTIVE) << source;
    }
}

TEST(AdasFunctionReport, CarriesAnAggregateUrbanDrivingRowForTheStackItself)
{
    const auto report = BuildAdasFunctionReport(VdPolicyEnableFlags{}, TrafficPolicySnapshot{});

    const auto* vd = Find(report, "gt.virtual_driver");
    ASSERT_NE(vd, nullptr);
    EXPECT_EQ(vd->name, osi_adas::NAME_URBAN_DRIVING);
    // The VD stack is driving whenever it is stepped, independently of whether
    // any single policy fired.
    EXPECT_EQ(vd->state, osi_adas::STATE_ACTIVE);
}

TEST(AdasFunctionReport, DiagnosticsAreRoutedToTheOwningFunctionByKeyPrefix)
{
    TrafficPolicySnapshot snap;
    snap.constraints.push_back(FromSource("aeb"));
    snap.detail.emplace_back("gt.aeb.ttc_s", "1.842");
    snap.detail.emplace_back("gt.crosswalk.ped_dist_m", "9.000");

    const auto report = BuildAdasFunctionReport(AllEnabled(), snap);

    const auto* aeb = Find(report, "gt.aeb");
    ASSERT_NE(aeb, nullptr);
    ASSERT_EQ(aeb->detail.size(), 1u);
    EXPECT_EQ(aeb->detail[0].first, "gt.aeb.ttc_s");
    EXPECT_EQ(aeb->detail[0].second, "1.842");

    const auto* cw = Find(report, "gt.crosswalk");
    ASSERT_NE(cw, nullptr);
    ASSERT_EQ(cw->detail.size(), 1u);
    EXPECT_EQ(cw->detail[0].first, "gt.crosswalk.ped_dist_m");
}
