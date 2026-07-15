// test_OdrLhtRoadTransitions.cpp -- regression tests for road-to-road transitions and for the
// traffic-hand (LHT/RHT) rule in signal validity.
//
// Before the [GT_LHT] fixes in GT_RoadManager.cpp these were the two open defects:
//
//   1. END-to-END road connection with IMPLICIT (missing) lane links:
//      Position::MoveToConnectingRoad's "no lane link -> snap to closest lane" fallback returned
//      early, skipping the END-contact heading flip (h_relative += PI) and the t-axis inversion.
//      The entity warped to the oncoming side, its heading flipped 180 deg and it stalled at the
//      road boundary, spamming "No connection from rid ... - trying move to closest lane".
//      Reproduces under BOTH LHT and RHT.
//
//   2. Signal::SetAllValidLanes bound orientation="+" signals to lanes with id < 0 unconditionally.
//      Traffic travelling +s is on the negative lanes under RHT but on the POSITIVE lanes under LHT,
//      so on an LHT road every oriented sign/signal was bound to the oncoming lanes.
//
// Template mirrors the sibling test_OdrJunctionGeom.cpp.
#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <vector>

#include "RoadManager.hpp"

namespace
{
std::string RepoRoot()
{
#ifdef GT_ODR_REPO_ROOT
    return std::string(GT_ODR_REPO_ROOT);
#else
    return std::string();
#endif
}

bool LoadXodr(const std::string& abs_path)
{
    return roadmanager::Position::GetOpenDrive()->LoadOpenDriveFile(abs_path.c_str(), true);
}

std::string Fix(const std::string& rel)
{
    return RepoRoot() + "/GT_esmini/test/odr_fixtures/lht/" + rel;
}

// Drive an entity along the road network with the entity-heading strategy (the strategy the
// DefaultController / RouteDrive / Kinematic path uses) and report the trajectory extremes.
struct DriveResult
{
    double max_x            = -1e9;
    double final_x          = 0.0;
    double min_abs_y        = 1e9;   // smallest |y| seen -- a sign flip passes through ~0
    bool   y_sign_flipped   = false;
    bool   x_went_backwards = false;
};

DriveResult DriveAlongS(roadmanager::Position& pos, int steps, double ds)
{
    DriveResult r;
    const double y0     = pos.GetY();
    double       prev_x = pos.GetX();
    r.max_x             = prev_x;

    for (int i = 0; i < steps; i++)
    {
        pos.MoveAlongS(ds, true);

        const double x = pos.GetX();
        const double y = pos.GetY();

        if (x < prev_x - 1e-6)
        {
            r.x_went_backwards = true;
        }
        if (y * y0 < 0.0)
        {
            r.y_sign_flipped = true;
        }
        r.min_abs_y = std::min(r.min_abs_y, std::fabs(y));
        r.max_x     = std::max(r.max_x, x);
        prev_x      = x;
    }
    r.final_x = prev_x;
    return r;
}

// Map the signal's global valid-lane ids back to plain OpenDRIVE lane ids, for readable assertions.
std::vector<int> ValidLaneIds(roadmanager::Road* road, roadmanager::Signal* sig)
{
    std::vector<int>          out;
    roadmanager::LaneSection* ls = road->GetLaneSectionByS(sig->GetS());
    if (ls == nullptr)
    {
        return out;
    }
    for (const auto gid : sig->GetAllValidGlobalLanes())
    {
        for (unsigned int i = 0; i < ls->GetNumberOfLanes(); i++)
        {
            if (ls->GetLaneByIdx(i)->GetGlobalId() == gid)
            {
                out.push_back(ls->GetLaneIdByIdx(i));
            }
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

roadmanager::Signal* SignalById(roadmanager::Road* road, int id)
{
    for (unsigned int i = 0; i < road->GetNumberOfSignals(); i++)
    {
        if (road->GetSignal(i)->GetId() == id)
        {
            return road->GetSignal(i);
        }
    }
    return nullptr;
}
}  // namespace

// ---------------------------------------------------------------------------------------------
// Defect 1: END-to-END connection with implicit lane links.
//
// Geometry: road 1 runs +x from (0,0) to (100,0); road 2 runs -x from (200,0) to (100,0). They meet
// END-to-END at x=100 and each names the other as successor with contactPoint="end". No <lane><link>.
// An entity that starts on road 1 heading +x must cross x=100 and continue to x=200 without ever
// changing the sign of its y (i.e. without hopping to the oncoming side).
// ---------------------------------------------------------------------------------------------
TEST(OdrLhtRoadTransitions, EndToEndImplicitLaneLinks_Lht)
{
    ASSERT_FALSE(RepoRoot().empty());
    ASSERT_TRUE(LoadXodr(Fix("lht_end_to_end_implicit.xodr")));

    // LHT: traffic travelling +s drives on the positive (left) lanes.
    roadmanager::Position pos;
    ASSERT_GE(static_cast<int>(pos.SetLanePos(1, 1, 10.0, 0.0)), 0);
    pos.SetHeadingRelative(0.0);  // heading +x, i.e. along +s

    ASSERT_GT(pos.GetY(), 0.0) << "sanity: LHT +s traffic sits on the +y side of the centerline";

    // 200 steps x 1 m = 190 m of road ahead plus margin -> must reach the far end at x=200.
    const DriveResult r = DriveAlongS(pos, 200, 1.0);

    EXPECT_FALSE(r.y_sign_flipped) << "entity warped to the oncoming lane across the END-to-END joint";
    EXPECT_FALSE(r.x_went_backwards) << "entity reversed direction across the END-to-END joint";
    EXPECT_GT(r.final_x, 199.0) << "entity stalled at the road boundary instead of continuing (final_x=" << r.final_x << ")";
}

TEST(OdrLhtRoadTransitions, EndToEndImplicitLaneLinks_Rht)
{
    ASSERT_FALSE(RepoRoot().empty());
    ASSERT_TRUE(LoadXodr(Fix("rht_end_to_end_implicit.xodr")));

    // RHT control: traffic travelling +s drives on the negative (right) lanes. Same defect.
    roadmanager::Position pos;
    ASSERT_GE(static_cast<int>(pos.SetLanePos(1, -1, 10.0, 0.0)), 0);
    pos.SetHeadingRelative(0.0);  // heading +x, i.e. along +s

    ASSERT_LT(pos.GetY(), 0.0) << "sanity: RHT +s traffic sits on the -y side of the centerline";

    const DriveResult r = DriveAlongS(pos, 200, 1.0);

    EXPECT_FALSE(r.y_sign_flipped) << "entity warped to the oncoming lane across the END-to-END joint";
    EXPECT_FALSE(r.x_went_backwards) << "entity reversed direction across the END-to-END joint";
    EXPECT_GT(r.final_x, 199.0) << "entity stalled at the road boundary instead of continuing (final_x=" << r.final_x << ")";
}

// ---------------------------------------------------------------------------------------------
// Defect 2: signal orientation must be resolved against the road's traffic-hand rule.
// ---------------------------------------------------------------------------------------------
TEST(OdrLhtRoadTransitions, SignalOrientationFollowsRoadRule)
{
    ASSERT_FALSE(RepoRoot().empty());
    ASSERT_TRUE(LoadXodr(Fix("signals_orientation_lht_rht.xodr")));
    roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();

    roadmanager::Road* lht = odr->GetRoadById(1);
    roadmanager::Road* rht = odr->GetRoadById(2);
    ASSERT_NE(lht, nullptr);
    ASSERT_NE(rht, nullptr);
    ASSERT_EQ(lht->GetRule(), roadmanager::Road::RoadRule::LEFT_HAND_TRAFFIC);
    ASSERT_EQ(rht->GetRule(), roadmanager::Road::RoadRule::RIGHT_HAND_TRAFFIC);

    roadmanager::Signal* lht_plus  = SignalById(lht, 100);  // orientation="+"
    roadmanager::Signal* lht_minus = SignalById(lht, 101);  // orientation="-"
    roadmanager::Signal* rht_plus  = SignalById(rht, 100);
    roadmanager::Signal* rht_minus = SignalById(rht, 101);
    ASSERT_NE(lht_plus, nullptr);
    ASSERT_NE(lht_minus, nullptr);
    ASSERT_NE(rht_plus, nullptr);
    ASSERT_NE(rht_minus, nullptr);

    // RHT (unchanged upstream behaviour): +s traffic is on the negative lanes.
    EXPECT_EQ(ValidLaneIds(rht, rht_plus), (std::vector<int>{-1}));
    EXPECT_EQ(ValidLaneIds(rht, rht_minus), (std::vector<int>{1}));

    // LHT: +s traffic is on the POSITIVE lanes -- this is what was broken.
    EXPECT_EQ(ValidLaneIds(lht, lht_plus), (std::vector<int>{1}));
    EXPECT_EQ(ValidLaneIds(lht, lht_minus), (std::vector<int>{-1}));

    odr->Clear();
}
