// vd_intent_layer.md section 7 -- the OBSERVATION-only junction lookahead.
//
// The claim this file has to prove is a DIFFERENCE, not a capability: that
// RouteLookaheadNextJunctionTurn sees a junction turn which RouteLookaheadJunctionTurn cannot see
// from the same position with the same lookahead. A test that only asserted "the new function
// finds the turn" would pass just as well if the two functions were identical, and the whole
// justification for adding a second function (design section 7-1: "a longer lookahead does not
// help, the contract is the limit") would go unverified.
//
// So every positive case here is run through BOTH functions and the assertions are on the pair.
//
// Needs a real road network, unlike the rest of this layer -- both functions walk the network
// with MoveAlongS. Fixture pattern follows test_OdrJunctionGeom.cpp.

#include <gtest/gtest.h>

#include <iostream>
#include <string>

#include "RoadManager.hpp"
#include "gt_esmini/control/common/JunctionTurn.hpp"

using namespace gt_esmini;

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

// multi_intersections.xodr: several junctions strung along ordinary roads, so a position exists
// from which the first road boundary crossed is NOT a connector but a junction follows later --
// exactly the geometry section 7-1 is about.
bool LoadMultiIntersections()
{
    const std::string path = RepoRoot() + "/resources/xodr/multi_intersections.xodr";
    return roadmanager::Position::GetOpenDrive()->LoadOpenDriveFile(path.c_str(), true);
}

// Does this road carry `lane_id` as a driving lane at s? Asked BEFORE SetLanePos, because
// SetLanePos logs an error for a missing lane -- and a sweep that walks every road on a
// one-way network would fill the gate log with dozens of them. A green run that prints errors
// teaches people to ignore printed errors.
bool HasDrivingLane(roadmanager::Road* road, int lane_id, double s)
{
    if (road == nullptr) return false;
    roadmanager::LaneSection* section = road->GetLaneSectionByS(s);
    if (section == nullptr) return false;
    roadmanager::Lane* lane = section->GetLaneById(lane_id);
    return lane != nullptr && lane->IsDriving();
}

}  // namespace

// Walks every driving lane of every non-junction road, at a few positions each, and collects the
// cases where the two functions disagree. The disagreement must be one-directional: wherever the
// signal-side scan finds a turn, the observation scan must find the SAME turn (it is a strict
// superset), and there must exist positions where only the observation scan finds one.
//
// Discovering the positions rather than hard-coding one is deliberate: a hard-coded road id would
// silently stop testing anything the day the fixture is regenerated, and the property being
// asserted (superset, with a non-empty difference) is exactly what a hard-coded case cannot say.
TEST(JunctionTurnObservationScan, SeesTurnsTheSignalScanStructurallyCannot)
{
    ASSERT_FALSE(RepoRoot().empty());
    ASSERT_TRUE(LoadMultiIntersections());
    roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();
    ASSERT_NE(odr, nullptr);

    constexpr double kLookahead = 400.0;

    int observed_only   = 0;  // observation scan finds a turn, signal scan does not
    int both            = 0;
    int signal_only     = 0;  // must stay 0 -- the observation scan is a superset
    int direction_clash = 0;  // must stay 0 -- when both fire they must agree on the direction

    for (unsigned int ri = 0; ri < odr->GetNumOfRoads(); ++ri)
    {
        roadmanager::Road* road = odr->GetRoadByIdx(static_cast<int>(ri));
        if (road == nullptr || road->GetJunction() != ID_UNDEFINED) continue;  // start off-junction
        if (road->GetLength() < 10.0) continue;

        for (double frac : {0.1, 0.5, 0.9})
        {
            for (int lane_id : {-1, 1})
            {
                const double s_pos = road->GetLength() * frac;
                if (!HasDrivingLane(road, lane_id, s_pos)) continue;

                roadmanager::Position pos;
                if (pos.SetLanePos(road->GetId(), lane_id, s_pos, 0.0) ==
                    roadmanager::Position::ReturnCode::ERROR_GENERIC)
                {
                    continue;
                }

                const JunctionTurnLookahead sig = RouteLookaheadJunctionTurn(pos, odr, kLookahead);
                const JunctionTurnLookahead obs = RouteLookaheadNextJunctionTurn(pos, odr, kLookahead);

                const bool sig_hit = sig.dir != 0;
                const bool obs_hit = obs.dir != 0;

                if (sig_hit && obs_hit)
                {
                    ++both;
                    if (sig.dir != obs.dir) ++direction_clash;
                }
                else if (obs_hit)
                {
                    ++observed_only;
                }
                else if (sig_hit)
                {
                    ++signal_only;
                }
            }
        }
    }

    // The point of the whole section: positions exist where only the observation scan finds the
    // turn. If this is 0, the two functions are interchangeable and section 7 is not implemented.
    std::cout << "[ scan    ] observation-only=" << observed_only << " both=" << both
              << " signal-only=" << signal_only << std::endl;

    EXPECT_GT(observed_only, 0) << "no position found where the observation scan sees more than "
                                   "the signal scan -- the two functions are equivalent";
    // The observation scan must never LOSE a turn the signal scan found.
    EXPECT_EQ(signal_only, 0) << "the observation scan missed a turn the signal scan found";
    // Where both fire they are looking at the same connector, so the direction must match.
    EXPECT_EQ(direction_clash, 0) << "the two scans disagreed on the turn direction";

    odr->Clear();
}

// design section 7-4: the loop notices a road change only through GetTrackId(), so a step longer
// than a connector steps clean over it and reports the ordinary road on the far side -- "no turn"
// at exactly the junctions that have one. This is why intent_turn_scan_step_m must not be
// coarsened as a cheap optimisation. Demonstrated rather than asserted-by-comment.
TEST(JunctionTurnObservationScan, ACoarseStepSkipsShortConnectors)
{
    ASSERT_FALSE(RepoRoot().empty());
    ASSERT_TRUE(LoadMultiIntersections());
    roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();

    constexpr double kLookahead = 400.0;

    int fine_hits   = 0;  // step = 2 m (the default)
    int coarse_hits = 0;  // step = 40 m

    for (unsigned int ri = 0; ri < odr->GetNumOfRoads(); ++ri)
    {
        roadmanager::Road* road = odr->GetRoadByIdx(static_cast<int>(ri));
        if (road == nullptr || road->GetJunction() != ID_UNDEFINED) continue;
        if (road->GetLength() < 10.0) continue;

        for (double frac : {0.1, 0.5, 0.9})
        {
            for (int lane_id : {-1, 1})
            {
                const double s_pos = road->GetLength() * frac;
                if (!HasDrivingLane(road, lane_id, s_pos)) continue;

                roadmanager::Position pos;
                if (pos.SetLanePos(road->GetId(), lane_id, s_pos, 0.0) ==
                    roadmanager::Position::ReturnCode::ERROR_GENERIC)
                {
                    continue;
                }
                if (RouteLookaheadNextJunctionTurn(pos, odr, kLookahead, 2.0).dir != 0) ++fine_hits;
                if (RouteLookaheadNextJunctionTurn(pos, odr, kLookahead, 40.0).dir != 0) ++coarse_hits;
            }
        }
    }

    std::cout << "[ scan    ] fine(2m)=" << fine_hits << " coarse(40m)=" << coarse_hits << std::endl;

    ASSERT_GT(fine_hits, 0) << "the fine scan found nothing -- the fixture cannot show the effect";
    EXPECT_LT(coarse_hits, fine_hits) << "a 40 m step lost no turns; if this ever holds, the "
                                         "step-size warning in the header is no longer true and "
                                         "should be re-derived, not deleted";

    odr->Clear();
}

// Negative controls -- section 9-3. A scan that is off, or has nowhere to look, reports nothing.
TEST(JunctionTurnObservationScan, ZeroOrNegativeLookaheadFindsNothing)
{
    ASSERT_FALSE(RepoRoot().empty());
    ASSERT_TRUE(LoadMultiIntersections());
    roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();

    roadmanager::Road* road = nullptr;
    for (unsigned int ri = 0; ri < odr->GetNumOfRoads(); ++ri)
    {
        roadmanager::Road* candidate = odr->GetRoadByIdx(static_cast<int>(ri));
        if (candidate && candidate->GetJunction() == ID_UNDEFINED && candidate->GetLength() >= 10.0)
        {
            road = candidate;
            break;
        }
    }
    ASSERT_NE(road, nullptr);

    roadmanager::Position pos;
    ASSERT_NE(pos.SetLanePos(road->GetId(), -1, 1.0, 0.0), roadmanager::Position::ReturnCode::ERROR_GENERIC);

    // intent_turn_lookahead_m == 0.0 is the DEFAULT, so this is the state most runs are in.
    EXPECT_EQ(RouteLookaheadNextJunctionTurn(pos, odr, 0.0).dir, 0);
    EXPECT_FALSE(RouteLookaheadNextJunctionTurn(pos, odr, 0.0).on_connector);
    EXPECT_DOUBLE_EQ(RouteLookaheadNextJunctionTurn(pos, odr, 0.0).dist_to_entry, -1.0);

    EXPECT_EQ(RouteLookaheadNextJunctionTurn(pos, odr, -5.0).dir, 0);
    EXPECT_EQ(RouteLookaheadNextJunctionTurn(pos, odr, 400.0, 0.0).dir, 0);  // step must be > 0

    odr->Clear();
}

TEST(JunctionTurnObservationScan, ANullOpenDriveFindsNothing)
{
    roadmanager::Position pos;
    const JunctionTurnLookahead result = RouteLookaheadNextJunctionTurn(pos, nullptr, 400.0);
    EXPECT_EQ(result.dir, 0);
    EXPECT_FALSE(result.on_connector);
    EXPECT_DOUBLE_EQ(result.dist_to_entry, -1.0);
}

// Starting ON a connector is answered from the connector geometry itself, without scanning --
// identical to the signal-side function, so the two must agree exactly here.
TEST(JunctionTurnObservationScan, StartingOnAConnectorMatchesTheSignalScanExactly)
{
    ASSERT_FALSE(RepoRoot().empty());
    ASSERT_TRUE(LoadMultiIntersections());
    roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();

    int checked = 0;
    for (unsigned int ri = 0; ri < odr->GetNumOfRoads(); ++ri)
    {
        roadmanager::Road* road = odr->GetRoadByIdx(static_cast<int>(ri));
        if (road == nullptr || road->GetJunction() == ID_UNDEFINED) continue;  // connectors only
        if (road->GetLength() <= 0.1) continue;

        for (int lane_id : {-1, 1})
        {
            const double s_mid = road->GetLength() * 0.5;
            if (!HasDrivingLane(road, lane_id, s_mid)) continue;

            roadmanager::Position pos;
            if (pos.SetLanePos(road->GetId(), lane_id, s_mid, 0.0) ==
                roadmanager::Position::ReturnCode::ERROR_GENERIC)
            {
                continue;
            }

            const JunctionTurnLookahead sig = RouteLookaheadJunctionTurn(pos, odr, 400.0);
            const JunctionTurnLookahead obs = RouteLookaheadNextJunctionTurn(pos, odr, 400.0);

            EXPECT_TRUE(obs.on_connector);
            EXPECT_EQ(obs.on_connector, sig.on_connector);
            EXPECT_EQ(obs.dir, sig.dir);
            EXPECT_DOUBLE_EQ(obs.dist_to_entry, sig.dist_to_entry);
            ++checked;
        }
    }

    EXPECT_GT(checked, 0) << "no connector road was reachable -- the fixture proved nothing";
    odr->Clear();
}
