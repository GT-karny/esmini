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
#include <iostream>
#include <set>
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
    roadmanager::Signal* rht_plus  = SignalById(rht, 200);  // signal ids are unique document-wide
    roadmanager::Signal* rht_minus = SignalById(rht, 201);
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

// ─────────────────────────────────────────────────────────────────────────────
// Issue #31 -- VirtualDriver junction "Traj" prediction alternates rapidly
// between the straight and left connecting roads even on a straight route.
//
// The VD policy/planner path walkers (TrajectoryShortPlanner preview,
// ConflictPointResolver corridor, ManeuverAwareSpeedPlanner scan,
// RouteSignal/CrosswalkScan) all predict the ego's forward path with the SAME
// idiom: an isolated Position copy (Duplicate + CopyRoute) walked with the
// CONVENIENCE MoveAlongS(ds) overload. That overload passes junctionSelectorAngle
// = -1.0, which RANDOMIZES the connecting road in Position::MoveToConnectingRoad's
// `else // randomize` branch whenever the route does not steer the choice.
//
// These tests reproduce the behaviour on esmini's canonical fabriksgatan junction
// (junction 4; incoming road 0, lane 1 -> connecting roads 8 / 9 / 10):
//   * Routeless: the walk MUST randomize (baseline that the -1.0 branch is live).
//   * Routed (straight route 0->8->1): DIAGNOSTIC for hypothesis D2 -- does the
//     assigned route actually steer the isolated prediction, or does the walk
//     still randomize (8/9/10) despite the straight route?
// ─────────────────────────────────────────────────────────────────────────────
namespace
{
std::string FabriksgatanXodr()
{
    return RepoRoot() + "/resources/xodr/fabriksgatan.xodr";
}

// Mirror the VD policy walkers EXACTLY: isolated copy + CopyRoute + MoveAlongS.
// `sel < 0` uses the CONVENIENCE overload (junctionSelectorAngle = -1.0, the buggy
// path). `sel >= 0` uses the explicit overload with that selector angle (the fix:
// 0.0 = straight-most, deterministic). Return the id of the first junction-4
// connecting road the walk enters, or -1 if it never reaches the junction.
int FirstConnectingRoadReached(const roadmanager::Position& ego, double lookahead, double step, double sel)
{
    roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();
    roadmanager::Position   pos;
    pos.Duplicate(ego);
    pos.CopyRoute(ego);

    double traveled = 0.0;
    while (traveled < lookahead)
    {
        const int ret = (sel < 0.0)
                            ? static_cast<int>(pos.MoveAlongS(step))
                            : static_cast<int>(pos.MoveAlongS(step, 0.0, sel, true,
                                                              roadmanager::Position::MoveDirectionMode::HEADING_DIRECTION, true));
        if (ret < 0) break;
        traveled += step;
        roadmanager::Road* r = odr->GetRoadById(pos.GetTrackId());
        if (r != nullptr && r->GetJunction() != ID_UNDEFINED)
        {
            return static_cast<int>(pos.GetTrackId());
        }
    }
    return -1;
}

// Count distinct first-connecting-roads over `trials` walks from the same ego.
std::set<int> DistinctConnectingRoads(const roadmanager::Position& ego, int trials, double sel)
{
    std::set<int> chosen;
    for (int t = 0; t < trials; ++t)
    {
        chosen.insert(FirstConnectingRoadReached(ego, 30.0, 1.0, sel));
    }
    return chosen;
}

// Straight route road 1 -> road 3 through the 4-way junction, entering from road 1
// (the N-S "main" axis). `lane` is the driving lane (RHT: -1, LHT: +1). Returns the
// distinct first-connecting-roads for the routed prediction walk with the buggy
// convenience overload (sel=-1.0), plus SetRoute rc and the ego OnRoute flag.
struct Routed4wayResult
{
    int           set_rc   = -99;
    bool          on_route = false;
    std::set<int> chosen;
};
Routed4wayResult Routed4wayWalk(int lane)
{
    roadmanager::Route* route = new roadmanager::Route;
    {
        roadmanager::Position w;
        w.SetLanePos(1, lane, 50.0, 0.0);
        route->AddWaypoint(w);
    }
    {
        roadmanager::Position w;
        w.SetLanePos(3, lane, 50.0, 0.0);
        route->AddWaypoint(w);
    }

    roadmanager::Position ego;
    ego.SetLanePos(1, lane, 95.0, 0.0);  // near road 1's junction end

    Routed4wayResult r;
    r.set_rc   = ego.SetRoute(route);
    r.on_route = ego.GetRoute() != nullptr && ego.GetRoute()->OnRoute();
    r.chosen   = DistinctConnectingRoads(ego, 40, -1.0);
    return r;
}

// Build the straight-through route road 0 -> connecting road 8 -> road 1 with the
// given number of waypoints (2 = endpoints only, 3 = incl. the connecting road).
roadmanager::Route* MakeStraightRoute(bool include_connecting_waypoint)
{
    roadmanager::Route* route = new roadmanager::Route;
    {
        roadmanager::Position w;
        w.SetLanePos(0, 1, 10.0, 0.0);
        w.SetHeadingRelative(M_PI);
        route->AddWaypoint(w);
    }
    if (include_connecting_waypoint)
    {
        roadmanager::Position w;
        w.SetLanePos(8, -1, 4.0, 0.0);
        route->AddWaypoint(w);
    }
    {
        roadmanager::Position w;
        w.SetLanePos(1, -1, 1.0, 0.0);
        route->AddWaypoint(w);
    }
    return route;
}
}  // namespace

TEST(OdrJunctionPredictionFlicker, RoutelessPredictionWalkRandomizesJunctionChoice)
{
    ASSERT_TRUE(LoadXodr(FabriksgatanXodr()));

    roadmanager::Position ego;
    ego.SetLanePos(0, 1, 5.0, 0.0);
    ego.SetHeadingRelative(M_PI);  // lane 1 travels against +s -> junction is at road 0's s=0 end

    const std::set<int> chosen = DistinctConnectingRoads(ego, 40, -1.0);

    std::cout << "[#31 routeless -1.0] distinct first-connecting-roads over 40 walks:";
    for (int c : chosen) std::cout << " " << c;
    std::cout << std::endl;

    // The -1.0 convenience overload randomizes among connecting roads 8/9/10.
    EXPECT_GT(chosen.size(), static_cast<size_t>(1))
        << "routeless prediction walk should randomize the junction connecting road (baseline)";

    roadmanager::Position::GetOpenDrive()->Clear();
}

TEST(OdrJunctionPredictionFlicker, RoutedPredictionWalkStability)
{
    ASSERT_TRUE(LoadXodr(FabriksgatanXodr()));

    roadmanager::Route* route = MakeStraightRoute(/*include_connecting_waypoint=*/true);

    roadmanager::Position ego;
    ego.SetLanePos(0, 1, 5.0, 0.0);
    ego.SetHeadingRelative(M_PI);
    ASSERT_EQ(ego.SetRoute(route), 0);
    const bool ego_on_route = ego.GetRoute() != nullptr && ego.GetRoute()->OnRoute();

    const std::set<int> chosen = DistinctConnectingRoads(ego, 40, -1.0);

    std::cout << "[#31 routed(3wp) -1.0] ego OnRoute=" << ego_on_route
              << "; distinct first-connecting-roads over 40 walks:";
    for (int c : chosen) std::cout << " " << c;
    std::cout << std::endl;

    // On the (roughly right-angle) fabriksgatan junction a valid straight route
    // DOES steer the isolated prediction: MoveToConnectingRoad's route branch wins
    // and the walk deterministically follows connecting road 8 every frame.
    EXPECT_EQ(chosen.size(), static_cast<size_t>(1))
        << "routed prediction walk unexpectedly randomized";

    roadmanager::Position::GetOpenDrive()->Clear();
}

// Endpoints-only route (no explicit connecting-road waypoint): does esmini's route
// path-finding still steer the prediction through the junction deterministically?
TEST(OdrJunctionPredictionFlicker, RoutedEndpointsOnlyPredictionWalk)
{
    ASSERT_TRUE(LoadXodr(FabriksgatanXodr()));

    roadmanager::Route* route = MakeStraightRoute(/*include_connecting_waypoint=*/false);

    roadmanager::Position ego;
    ego.SetLanePos(0, 1, 5.0, 0.0);
    ego.SetHeadingRelative(M_PI);
    const int set_rc      = ego.SetRoute(route);
    const bool ego_on_route = ego.GetRoute() != nullptr && ego.GetRoute()->OnRoute();

    const std::set<int> chosen = DistinctConnectingRoads(ego, 40, -1.0);

    std::cout << "[#31 routed(2wp) -1.0] SetRoute=" << set_rc << " OnRoute=" << ego_on_route
              << "; distinct first-connecting-roads over 40 walks:";
    for (int c : chosen) std::cout << " " << c;
    std::cout << std::endl;

    // Even a 2-waypoint (endpoints-only) route resolves the connecting road and steers
    // the prediction deterministically here -- esmini's RoadPath connects road 0 -> 8.
    EXPECT_EQ(set_rc, 0);
    EXPECT_EQ(chosen.size(), static_cast<size_t>(1)) << "endpoints-only route should still steer deterministically";

    roadmanager::Position::GetOpenDrive()->Clear();
}

// FIX validation: the explicit overload with junctionSelectorAngle = 0.0
// (straight-most) makes BOTH the routeless and routed prediction walks
// deterministic -> the "straight Traj / left Traj" alternation cannot occur.
TEST(OdrJunctionPredictionFlicker, StraightMostSelectorIsDeterministic)
{
    ASSERT_TRUE(LoadXodr(FabriksgatanXodr()));

    roadmanager::Position ego;
    ego.SetLanePos(0, 1, 5.0, 0.0);
    ego.SetHeadingRelative(M_PI);

    const std::set<int> routeless0 = DistinctConnectingRoads(ego, 40, 0.0);
    std::cout << "[#31 routeless 0.0] distinct first-connecting-roads over 40 walks:";
    for (int c : routeless0) std::cout << " " << c;
    std::cout << std::endl;
    EXPECT_EQ(routeless0.size(), static_cast<size_t>(1))
        << "junctionSelectorAngle=0.0 must pick a single deterministic connecting road";

    roadmanager::Route* route = MakeStraightRoute(/*include_connecting_waypoint=*/true);
    roadmanager::Position ego2;
    ego2.SetLanePos(0, 1, 5.0, 0.0);
    ego2.SetHeadingRelative(M_PI);
    ASSERT_EQ(ego2.SetRoute(route), 0);
    const std::set<int> routed0 = DistinctConnectingRoads(ego2, 40, 0.0);
    std::cout << "[#31 routed 0.0] distinct first-connecting-roads over 40 walks:";
    for (int c : routed0) std::cout << " " << c;
    std::cout << std::endl;
    EXPECT_EQ(routed0.size(), static_cast<size_t>(1))
        << "with a route + angle=0 the walk must be deterministic";

    roadmanager::Position::GetOpenDrive()->Clear();
}

// Diagnostic: does a routed prediction walk randomize on a 4-way junction, and does
// the traffic-hand rule (RHT vs LHT) change the answer? Issue #31 telemetry showed
// the alternation WITH an AssignRoute on an LHT junction (2-lane left lane, straight
// + left permitted). RHT = the stock 4way_priority__main_ns.xodr; LHT = the same
// topology with rule flipped.
TEST(OdrJunctionPredictionFlicker, Routed4wayRhtVsLht)
{
    // ---- RHT (stock 4way, driving lane -1) ----
    ASSERT_TRUE(LoadXodr(RepoRoot() + "/resources/scenario_authoring/road_catalog/generated/4way_priority__main_ns.xodr"));
    const Routed4wayResult rht = Routed4wayWalk(-1);
    std::cout << "[#31 4way RHT lane -1] SetRoute=" << rht.set_rc << " OnRoute=" << rht.on_route
              << "; distinct first-connecting-roads:";
    for (int c : rht.chosen) std::cout << " " << c;
    std::cout << std::endl;
    EXPECT_EQ(rht.chosen.size(), static_cast<size_t>(1)) << "RHT routed walk should be deterministic";
    roadmanager::Position::GetOpenDrive()->Clear();

    // ---- LHT (same topology, rule=LHT, driving lane +1) ----
    ASSERT_TRUE(LoadXodr(Fix("4way_priority_lht.xodr")));
    const Routed4wayResult lht = Routed4wayWalk(1);
    std::cout << "[#31 4way LHT lane +1] SetRoute=" << lht.set_rc << " OnRoute=" << lht.on_route
              << "; distinct first-connecting-roads:";
    for (int c : lht.chosen) std::cout << " " << c;
    std::cout << std::endl;
    EXPECT_EQ(lht.chosen.size(), static_cast<size_t>(1)) << "LHT routed walk should be deterministic (rule is not the trigger)";
    roadmanager::Position::GetOpenDrive()->Clear();
}

// Reproduce the ROUTED case: sweep the ego's approach position s along the incoming
// road and, at each position, run the routed prediction walk. If the route steers at
// every position the walk always picks connecting road 8; any position that deviates
// (randomizes, or picks a different connector) reproduces the "straight/left Traj"
// flicker WITH a route -- i.e. on_route_ dropping mid-approach. Diagnostic only.
TEST(OdrJunctionPredictionFlicker, RoutedApproachSweep)
{
    ASSERT_TRUE(LoadXodr(FabriksgatanXodr()));

    int on_route_positions = 0, off_route_positions = 0;
    int on_route_deviating = 0, off_route_deviating = 0;
    std::cout << "[#31 sweep] deviating positions (walk left deterministic road 8):" << std::endl;
    for (double s = 2.0; s <= 88.0; s += 1.0)
    {
        // Fresh route each iteration -- Position owns and deletes route_, so a shared
        // route object would double-free. Straight route road 0 (from s=90, heading
        // against +s) -> connecting 8 -> road 1: the whole approach lies on the route.
        roadmanager::Route* route = new roadmanager::Route;
        {
            roadmanager::Position w;
            w.SetLanePos(0, 1, 90.0, 0.0);
            w.SetHeadingRelative(M_PI);
            route->AddWaypoint(w);
        }
        {
            roadmanager::Position w;
            w.SetLanePos(8, -1, 4.0, 0.0);
            route->AddWaypoint(w);
        }
        {
            roadmanager::Position w;
            w.SetLanePos(1, -1, 1.0, 0.0);
            route->AddWaypoint(w);
        }

        roadmanager::Position ego;
        ego.SetLanePos(0, 1, s, 0.0);
        ego.SetHeadingRelative(M_PI);
        const int  rc       = ego.SetRoute(route);
        const bool on_route = rc == 0 && ego.GetRoute() != nullptr && ego.GetRoute()->OnRoute();
        std::set<int> chosen = DistinctConnectingRoads(ego, 8, -1.0);
        chosen.erase(-1);  // -1 == junction beyond the 30 m lookahead (ego too far), not a deviation
        if (chosen.empty()) continue;  // junction never reached from this position
        const bool deviates = chosen.size() > 1 || *chosen.begin() != 8;

        if (on_route) ++on_route_positions; else ++off_route_positions;
        if (deviates)
        {
            if (on_route) ++on_route_deviating; else ++off_route_deviating;
            std::cout << "  s=" << s << " SetRoute=" << rc << " OnRoute=" << on_route << " ->";
            for (int c : chosen) std::cout << " " << c;
            std::cout << std::endl;
        }
    }
    std::cout << "[#31 sweep] on-route positions=" << on_route_positions
              << " (deviating " << on_route_deviating << "); off-route positions=" << off_route_positions
              << " (deviating " << off_route_deviating << ")" << std::endl;

    // The whole point: an ON-ROUTE ego must never randomize (route steers). Only
    // OFF-ROUTE positions may. If this fails, a routed on-route ego DOES randomize
    // -> the "with a route" alternation is reproduced without needing routelessness.
    EXPECT_EQ(on_route_deviating, 0) << "an on-route ego's prediction walk randomized";

    roadmanager::Position::GetOpenDrive()->Clear();
}

// Issue #31 exact-geometry reproduction: a SKEWED (non-right-angle) 4-way with a
// 2-lane main road (left lane permits straight + turns), a 1-lane crossing, and a
// CURVED straight-through connector (100). The ego is ROUTED straight (road 1 ->
// road 3 through connector 100) and driven in the LEFT lane. Sweep the approach and
// count positions whose routed prediction walk leaves the straight connector 100
// (i.e. randomizes into a turn connector) -> reproduces "straight/left Traj flicker
// WITH a route". `lane` = the left driving lane (RHT: -1, LHT: +1).
int SkewLeftLaneRoutedDeviations(int lane, bool verbose)
{
    int deviating = 0, on_route_reached = 0;
    // A real VirtualDriver ego is NOT glued to the route centreline: its lateral
    // tracking carries a small offset/heading error. Sweep both the approach s AND a
    // lane offset + heading error, because a routed ego only randomizes once its
    // position fails to localise back onto the route (on_route_ = false) -- which the
    // offset can trigger in the skewed/curved junction throat.
    for (double off = -1.6; off <= 1.6001; off += 0.4)
    {
        for (double dh = -0.15; dh <= 0.15001; dh += 0.15)
        {
            for (double s = 60.0; s <= 98.0; s += 1.0)
            {
                roadmanager::Route* route = new roadmanager::Route;
                {
                    roadmanager::Position w;
                    w.SetLanePos(1, lane, 50.0, 0.0);
                    route->AddWaypoint(w);
                }
                {
                    roadmanager::Position w;
                    w.SetLanePos(3, lane, 50.0, 0.0);
                    route->AddWaypoint(w);
                }

                roadmanager::Position ego;
                ego.SetLanePos(1, lane, s, off);
                if (dh != 0.0) ego.SetHeadingRelative(dh < 0 ? (2 * M_PI + dh) : dh);
                const int  rc       = ego.SetRoute(route);
                const bool on_route = rc == 0 && ego.GetRoute() != nullptr && ego.GetRoute()->OnRoute();

                std::set<int> chosen = DistinctConnectingRoads(ego, 8, -1.0);
                chosen.erase(-1);
                if (chosen.empty()) continue;
                ++on_route_reached;
                const bool deviates = chosen.size() > 1 || *chosen.begin() != 100;  // 100 = straight-through
                if (deviates)
                {
                    ++deviating;
                    if (verbose && deviating <= 12)
                    {
                        std::cout << "  s=" << s << " off=" << off << " dh=" << dh
                                  << " SetRoute=" << rc << " OnRoute=" << on_route << " ->";
                        for (int c : chosen) std::cout << " " << c;
                        std::cout << std::endl;
                    }
                }
            }
        }
    }
    std::cout << "  reached-junction samples=" << on_route_reached << " deviating=" << deviating << std::endl;
    return deviating;
}

TEST(OdrJunctionPredictionFlicker, SkewedTwoLaneLeftLaneRoutedRht)
{
    ASSERT_TRUE(LoadXodr(Fix("4way_skew_2lane_rht.xodr")));
    std::cout << "[#31 skew RHT left lane -1] routed straight-through, approach sweep:" << std::endl;
    const int dev = SkewLeftLaneRoutedDeviations(-1, /*verbose=*/true);
    std::cout << "[#31 skew RHT] total deviating: " << dev << std::endl;
    // Skew + curved connector + 2-lane + steering error does NOT reproduce the flicker
    // as long as the route resolves (on-route ego). The trigger is an unresolved route,
    // not the geometry -- documented here as a negative-result regression guard.
    EXPECT_EQ(dev, 0) << "on-route ego on a skewed 2-lane junction must not randomize";
    roadmanager::Position::GetOpenDrive()->Clear();
}

TEST(OdrJunctionPredictionFlicker, SkewedTwoLaneLeftLaneRoutedLht)
{
    ASSERT_TRUE(LoadXodr(Fix("4way_skew_2lane_lht.xodr")));
    std::cout << "[#31 skew LHT left lane +1] routed straight-through, approach sweep:" << std::endl;
    const int dev = SkewLeftLaneRoutedDeviations(1, /*verbose=*/true);
    std::cout << "[#31 skew LHT] total deviating: " << dev << std::endl;
    EXPECT_EQ(dev, 0) << "on-route ego on a skewed 2-lane LHT junction must not randomize";
    roadmanager::Position::GetOpenDrive()->Clear();
}
