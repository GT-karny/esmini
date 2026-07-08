// test_OdrResyncGuards.cpp -- P9b permanent double-processing guards (plan §5 P9b, resync
// rehearsal deliverable).
//
// After an upstream resync, the highest-risk silent failure mode is DOUBLE PROCESSING: upstream
// gains native handling for an element the GT side also synthesizes/stores, and both paths run.
// The observable damage for the synthesis families is duplicated RMObjects / colliding ids.
//
// These tests run in the unit gate (test_ScenarioReaderParsing -> run_gt_tests.ps1 -> regression
// gate Step 1) on every build, so the guard is PERMANENT, not a one-off rehearsal artifact:
//
//   1. Synth-id family bases stay disjoint: crosswalk 9.0e8 / bridge 9.1e8 / objectReference
//      9.2e8 (each family gets a 10M range; the constants live in odr_side and are asserted
//      here against drift).
//   2. Per family fixture: at least one synthesized object exists, ALL object ids across the
//      whole network are unique (a synth id colliding with an authored id, or a hunk applied
//      twice after a resync, breaks this), and synth ids stay inside their family range.
//   3. Re-parse idempotence: loading the same file again (replace=true, the production re-parse
//      path) yields the SAME synth count -- BuildSideModel must replace, never accumulate.
//
// The text-side companions (whitelist regen drift, parser_coverage duplicate paths,
// handled-by-upstream contradiction) live in scripts/check_resync_guards.py, wired into
// run_odr_conformance (every profile).
#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

#include "RoadManager.hpp"

namespace
{

constexpr unsigned int kCrosswalkBase = 900000000u;  // odr_side/OdrJunctionExtras.cpp
constexpr unsigned int kBridgeBase    = 910000000u;  // odr_side/OdrObjectExtras.cpp
constexpr unsigned int kObjRefBase    = 920000000u;  // odr_side/OdrObjectExtras.cpp
constexpr unsigned int kFamilySpan    = 10000000u;   // 10M ids per family

std::string RepoRoot()
{
#ifdef GT_ODR_REPO_ROOT
    return std::string(GT_ODR_REPO_ROOT);
#else
    return std::string();
#endif
}

bool LoadXodr(const std::string& rel_path)
{
    const std::string abs_path = RepoRoot() + "/" + rel_path;
    return roadmanager::Position::GetOpenDrive()->LoadOpenDriveFile(abs_path.c_str(), true);
}

// Collect every RMObject id in the loaded network, and the subset within [base, base+span).
struct IdSweep
{
    std::vector<id_t> all;
    std::vector<id_t> in_family;
    int               duplicate_count = 0;
};

IdSweep SweepObjectIds(unsigned int family_base)
{
    IdSweep                 sweep;
    std::map<id_t, int>     seen;
    roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();
    for (unsigned int r = 0; r < odr->GetNumOfRoads(); r++)
    {
        roadmanager::Road* road = odr->GetRoadByIdx(r);
        for (unsigned int o = 0; o < road->GetNumberOfObjects(); o++)
        {
            roadmanager::RMObject* obj = road->GetRoadObject(o);
            if (obj == nullptr)
            {
                continue;
            }
            const id_t id = obj->GetId();
            sweep.all.push_back(id);
            if (++seen[id] > 1)
            {
                sweep.duplicate_count++;
            }
            if (id >= family_base && id < family_base + kFamilySpan)
            {
                sweep.in_family.push_back(id);
            }
        }
    }
    return sweep;
}

void ExpectFamilyGuards(const std::string& fixture, unsigned int family_base)
{
    ASSERT_TRUE(LoadXodr(fixture)) << fixture;
    IdSweep first = SweepObjectIds(family_base);
    EXPECT_GE(first.in_family.size(), 1u) << fixture << ": no synthesized object in family "
                                          << family_base;
    EXPECT_EQ(first.duplicate_count, 0) << fixture << ": duplicated RMObject ids (double processing)";

    // Re-parse idempotence: the production re-parse path (replace=true) must yield the same
    // synth population -- BuildSideModel clears + rebuilds, AddObject must not accumulate.
    ASSERT_TRUE(LoadXodr(fixture)) << fixture << " (re-parse)";
    IdSweep second = SweepObjectIds(family_base);
    EXPECT_EQ(first.in_family.size(), second.in_family.size())
        << fixture << ": synth count changed across re-parse (accumulation)";
    EXPECT_EQ(second.duplicate_count, 0) << fixture << ": duplicated ids after re-parse";
}

TEST(OdrResyncGuards, SynthIdFamilyBasesDisjoint)
{
    // The three synthesis families must keep disjoint 10M ranges. If a base constant drifts in
    // odr_side, update BOTH sides deliberately (this is the drift alarm).
    EXPECT_EQ(kCrosswalkBase + kFamilySpan, kBridgeBase);
    EXPECT_EQ(kBridgeBase + kFamilySpan, kObjRefBase);
}

TEST(OdrResyncGuards, CrosswalkSynthesisNoDoubleProcessing)
{
    ExpectFamilyGuards("GT_esmini/test/odr_fixtures/handauthored/01_crossing_junction_18.xodr",
                       kCrosswalkBase);
}

TEST(OdrResyncGuards, BridgeSynthesisNoDoubleProcessing)
{
    ExpectFamilyGuards("GT_esmini/test/odr_fixtures/handauthored/11_bridge_15.xodr", kBridgeBase);
}

TEST(OdrResyncGuards, ObjectReferenceSynthesisNoDoubleProcessing)
{
    ExpectFamilyGuards("GT_esmini/test/odr_fixtures/handauthored/10_object_reference_15.xodr",
                       kObjRefBase);
}

}  // namespace
