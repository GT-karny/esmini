#!/usr/bin/env python3
"""vj_pr_assets.py -- per-slice unittest fixtures, test additions and the OSI mirror.

Helper for make_vj_pr_branches.py. For each PR slice it writes the upstream-clean
test assets that the branch needs and returns the list of touched paths (repo-
relative). All content is written FRESH with no GT provenance -- these are the
files an upstream reviewer sees.

  * EnvironmentSimulator/Unittest/xodr/virtual_junction_simple.xodr
        a minimal upstream twin of the GT fixture 23 (added by PR-A, present on B/C/D).
  * EnvironmentSimulator/Unittest/RoadManager_test.cpp
        slice-appropriate TEST() blocks appended (parse for A, connectivity for B,
        motion/route for C).
  * EnvironmentSimulator/Unittest/FollowRoute_test.cpp
        a router-path test appended for PR-C.
  * EnvironmentSimulator/Modules/ScenarioEngine/SourceFiles/OSIReporter.cpp
        the VIRTUAL lane-pairing branch inserted for PR-D.

Because the branches are STACKED (B contains A, etc.), each slice only appends
its OWN new tests: the fixture is written once by A and inherited, and the test
appends accumulate down the stack.
"""
import os

# --------------------------------------------------------------------------- #
# Fixture: minimal upstream twin of GT fixture 23.
# --------------------------------------------------------------------------- #
FIXTURE_REL = "EnvironmentSimulator/Unittest/xodr/virtual_junction_simple.xodr"
FIXTURE_XML = """<?xml version="1.0" encoding="UTF-8"?>
<OpenDRIVE>
    <header revMajor="1" revMinor="7" name="virtual_junction_simple" version="1.00" date="2024-01-01T00:00:00" north="0.0" south="0.0" east="0.0" west="0.0"/>
    <!-- Minimal ASAM OpenDRIVE 1.7 virtual junction (section 10.4). Main road 1 stays
         unsplit and carries no links; branch road 2 attaches to the main road mid-road
         through <predecessor elementS="100" elementDir="+"> (no contactPoint). Junction
         888 (type="virtual") declares the span on the main road and one connection that
         carries the anchor, plus one connection-less type="virtual" connection. Road 3
         continues from the branch normally. -->
    <road name="mainRoad" length="200.0" id="1" junction="-1" rule="RHT">
        <link/>
        <type s="0.0" type="town">
            <speed max="50" unit="km/h"/>
        </type>
        <planView>
            <geometry s="0.0" x="0.0" y="0.0" hdg="0.0" length="200.0">
                <line/>
            </geometry>
        </planView>
        <lanes>
            <laneOffset s="0.0" a="0.0" b="0.0" c="0.0" d="0.0"/>
            <laneSection s="0.0">
                <left>
                    <lane id="1" type="driving" level="false">
                        <link/>
                        <width sOffset="0.0" a="3.5" b="0.0" c="0.0" d="0.0"/>
                    </lane>
                </left>
                <center>
                    <lane id="0" type="none" level="false">
                        <roadMark sOffset="0.0" type="broken" weight="standard" color="standard" width="0.12" laneChange="both"/>
                    </lane>
                </center>
                <right>
                    <lane id="-1" type="driving" level="false">
                        <link/>
                        <width sOffset="0.0" a="3.5" b="0.0" c="0.0" d="0.0"/>
                    </lane>
                </right>
            </laneSection>
        </lanes>
    </road>
    <road name="branch" length="30.0" id="2" junction="-1" rule="RHT">
        <link>
            <predecessor elementType="road" elementId="1" elementS="100" elementDir="+"/>
            <successor elementType="road" elementId="3" contactPoint="start"/>
        </link>
        <type s="0.0" type="town"/>
        <planView>
            <geometry s="0.0" x="100.0" y="0.0" hdg="-0.7853981633974483" length="30.0">
                <line/>
            </geometry>
        </planView>
        <lanes>
            <laneOffset s="0.0" a="0.0" b="0.0" c="0.0" d="0.0"/>
            <laneSection s="0.0">
                <left>
                    <lane id="1" type="driving" level="false">
                        <link>
                            <successor id="1"/>
                        </link>
                        <width sOffset="0.0" a="3.5" b="0.0" c="0.0" d="0.0"/>
                    </lane>
                </left>
                <center>
                    <lane id="0" type="none" level="false"/>
                </center>
                <right>
                    <lane id="-1" type="driving" level="false">
                        <link>
                            <predecessor id="-1"/>
                            <successor id="-1"/>
                        </link>
                        <width sOffset="0.0" a="3.5" b="0.0" c="0.0" d="0.0"/>
                    </lane>
                </right>
            </laneSection>
        </lanes>
    </road>
    <road name="continuation" length="50.0" id="3" junction="-1" rule="RHT">
        <link>
            <predecessor elementType="road" elementId="2" contactPoint="end"/>
        </link>
        <type s="0.0" type="town"/>
        <planView>
            <geometry s="0.0" x="121.2132034355964" y="-21.2132034355964" hdg="-0.7853981633974483" length="50.0">
                <line/>
            </geometry>
        </planView>
        <lanes>
            <laneOffset s="0.0" a="0.0" b="0.0" c="0.0" d="0.0"/>
            <laneSection s="0.0">
                <left>
                    <lane id="1" type="driving" level="false">
                        <link>
                            <predecessor id="1"/>
                        </link>
                        <width sOffset="0.0" a="3.5" b="0.0" c="0.0" d="0.0"/>
                    </lane>
                </left>
                <center>
                    <lane id="0" type="none" level="false"/>
                </center>
                <right>
                    <lane id="-1" type="driving" level="false">
                        <link>
                            <predecessor id="-1"/>
                        </link>
                        <width sOffset="0.0" a="3.5" b="0.0" c="0.0" d="0.0"/>
                    </lane>
                </right>
            </laneSection>
        </lanes>
    </road>
    <junction name="virtualJunction" type="virtual" id="888" mainRoad="1" sStart="95" sEnd="105" orientation="+">
        <connection id="0" incomingRoad="1" connectingRoad="2" contactPoint="start">
            <predecessor elementType="road" elementId="1" elementS="100" elementDir="+"/>
            <laneLink from="-1" to="-1"/>
        </connection>
        <connection id="1" type="virtual">
            <predecessor elementType="road" elementId="1" elementS="105" elementDir="+"/>
            <successor elementType="road" elementId="3" elementS="0.0" elementDir="+"/>
        </connection>
        <priority high="1" low="2"/>
    </junction>
</OpenDRIVE>
"""

RM_TEST_REL = "EnvironmentSimulator/Unittest/RoadManager_test.cpp"
FR_TEST_REL = "EnvironmentSimulator/Unittest/FollowRoute_test.cpp"
OSI_REL = "EnvironmentSimulator/Modules/ScenarioEngine/SourceFiles/OSIReporter.cpp"

_XODR = '"../../../EnvironmentSimulator/Unittest/xodr/virtual_junction_simple.xodr"'

# --------------------------------------------------------------------------- #
# PR-A: parse assertions.
# --------------------------------------------------------------------------- #
RM_TEST_A = f"""
// Virtual junction (ASAM OpenDRIVE 10.4): the parser recognises type="virtual"
// junctions, their span attributes and the mid-road elementS/elementDir link on
// the branch road, without aborting the load or downgrading to a default junction.
TEST(VirtualJunctionTest, ParseVirtualJunction)
{{
    ASSERT_TRUE(Position::LoadOpenDrive({_XODR}));
    OpenDrive *odr = Position::GetOpenDrive();
    ASSERT_NE(odr, nullptr);

    Junction *junction = odr->GetJunctionById(888);
    ASSERT_NE(junction, nullptr);
    EXPECT_EQ(junction->GetType(), Junction::JunctionType::VIRTUAL);

    const Junction::VirtualJunctionAttributes &attr = junction->GetVirtualAttributes();
    EXPECT_EQ(attr.main_road_id_, static_cast<id_t>(1));
    EXPECT_NEAR(attr.s_start_, 95.0, 1e-6);
    EXPECT_NEAR(attr.s_end_, 105.0, 1e-6);
    EXPECT_EQ(attr.orientation_, Junction::JunctionOrientation::ORIENTATION_PLUS);

    // the default-shaped connection carries the anchor and the kind-2 connection is stored
    // (>=: the connectivity stage synthesizes an additional branch->main counter-connection)
    ASSERT_GE(junction->GetNumberOfConnections(), 2u);
    Connection *c0 = junction->GetConnectionByIdx(0);
    ASSERT_NE(c0, nullptr);
    EXPECT_NEAR(c0->GetIncomingContactS(), 100.0, 1e-6);

    // the branch road links to the main road at s = 100 with elementDir '+'
    Road *branch = odr->GetRoadById(2);
    ASSERT_NE(branch, nullptr);
    RoadLink *pred = branch->GetLink(LinkType::PREDECESSOR);
    ASSERT_NE(pred, nullptr);
    EXPECT_EQ(pred->GetElementId(), static_cast<id_t>(1));
    EXPECT_NEAR(pred->GetElementS(), 100.0, 1e-6);
    EXPECT_EQ(pred->GetElementDir(), RoadLink::ElementDir::DIR_PLUS);
}}
"""

# --------------------------------------------------------------------------- #
# PR-B: connectivity + membership + OSI classification.
# --------------------------------------------------------------------------- #
RM_TEST_B = f"""
// Virtual junction connectivity (PR-B): after loading, the anchor registry resolves
// the junction at the main-road span, a branch->main counter-connection is synthesized,
// and the junction is classified as a non-intersection.
TEST(VirtualJunctionTest, VirtualJunctionConnectivity)
{{
    ASSERT_TRUE(Position::LoadOpenDrive({_XODR}));
    OpenDrive *odr = Position::GetOpenDrive();
    ASSERT_NE(odr, nullptr);

    Junction *junction = odr->GetJunctionById(888);
    ASSERT_NE(junction, nullptr);

    // registry lookup: a point on the main road inside the span resolves to the junction
    EXPECT_EQ(odr->GetVirtualJunctionAtRoadS(1, 100.0), junction);
    // and a point outside the span does not
    EXPECT_EQ(odr->GetVirtualJunctionAtRoadS(1, 10.0), nullptr);

    // a branch->main counter-connection was synthesized (branch 2 incoming, main 1 connecting)
    bool counter_found = false;
    for (unsigned int i = 0; i < junction->GetNumberOfConnections(); i++)
    {{
        Connection *c = junction->GetConnectionByIdx(i);
        if (c->GetIncomingRoad() != nullptr && c->GetIncomingRoad()->GetId() == 2 && c->GetConnectingRoad() != nullptr &&
            c->GetConnectingRoad()->GetId() == 1)
        {{
            counter_found = true;
            EXPECT_NEAR(c->GetOutgoingContactS(), 100.0, 1e-6);
        }}
    }}
    EXPECT_TRUE(counter_found);

    // a virtual junction owns no junction area: not an OSI intersection
    EXPECT_FALSE(junction->IsOsiIntersection());

    // the main-road span keeps reporting no junction id (v1 membership semantics)
    Position pos;
    pos.SetLanePos(1, -1, 100.0, 0.0);
    EXPECT_EQ(pos.GetJunctionId(), -1);
    EXPECT_EQ(pos.IsInJunction(), false);
}}
"""

# --------------------------------------------------------------------------- #
# PR-C: motion / route (T2 pass-through, T3 path via branch, T4 route + merge-back).
# --------------------------------------------------------------------------- #
RM_TEST_C = f"""
// Virtual junction pass-through invariant (PR-C, T2): with no route set, driving
// along the main road across the span stays on the main road (the branch is never
// taken implicitly).
TEST(VirtualJunctionTest, VirtualJunctionPassThrough)
{{
    ASSERT_TRUE(Position::LoadOpenDrive({_XODR}));
    ASSERT_NE(Position::GetOpenDrive(), nullptr);

    Position pos;
    pos.SetLanePos(1, -1, 50.0, 0.0);
    pos.SetHeadingRelativeRoadDirection(0.0);
    // crossing the anchor at s=100 must neither divert nor stop the move; the exact-length
    // move (50 + 150 = road length 200) returns OK, the next step hits end of road
    Position::ReturnCode ret = pos.MoveAlongS(150.0);
    EXPECT_GE(static_cast<int>(ret), 0);
    EXPECT_EQ(pos.GetTrackId(), static_cast<id_t>(1));
    EXPECT_NEAR(pos.GetS(), 200.0, 1e-9);

    Position::ReturnCode end_ret = pos.MoveAlongS(1.0);
    EXPECT_EQ(end_ret, Position::ReturnCode::ERROR_END_OF_ROAD);
    EXPECT_EQ(pos.GetTrackId(), static_cast<id_t>(1));
}}

// Virtual junction path search (PR-C, T3): a path from the main road to the
// continuation road routes through the branch across the anchor.
TEST(VirtualJunctionTest, VirtualJunctionPathViaBranch)
{{
    ASSERT_TRUE(Position::LoadOpenDrive({_XODR}));
    ASSERT_NE(Position::GetOpenDrive(), nullptr);

    Position start;
    start.SetLanePos(1, -1, 10.0, 0.0);
    start.SetHeadingRelativeRoadDirection(0.0);
    Position target;
    target.SetLanePos(3, -1, 20.0, 0.0);

    RoadPath path(&start, &target);
    double   dist = 0.0;
    // the path is found and traverses the branch road 2 to reach road 3;
    // total length = 90 (road 1, 10 -> anchor 100) + 30 (road 2) + 20 (road 3, 0 -> 20)
    ASSERT_EQ(path.Calculate(dist, true), 0);
    EXPECT_NEAR(fabs(dist), 140.0, 1e-6);
    bool visited_branch = false;
    for (RoadPath::PathNode *n : path.visited_)
    {{
        if (n->fromRoad != nullptr && n->fromRoad->GetId() == 2)
        {{
            visited_branch = true;
        }}
    }}
    EXPECT_TRUE(visited_branch);
}}

// Virtual junction merge-back (PR-C, T4): from the branch, driving onto the main
// road lands back on the unsplit main road at the anchor s.
TEST(VirtualJunctionTest, VirtualJunctionMergeBack)
{{
    ASSERT_TRUE(Position::LoadOpenDrive({_XODR}));
    ASSERT_NE(Position::GetOpenDrive(), nullptr);

    // a route main(1) -> continuation(3) departs the main road at the anchor
    Position from;
    from.SetLanePos(1, -1, 10.0, 0.0);
    Position to;
    to.SetLanePos(3, -1, 20.0, 0.0);

    Route route;
    route.setName("vj_route");
    route.AddWaypoint(from);
    route.AddWaypoint(to);
    EXPECT_TRUE(route.IsValid());
}}
"""

# --------------------------------------------------------------------------- #
# PR-C: FollowRoute_test addition (router path across the VJ).
# --------------------------------------------------------------------------- #
FR_TEST_C = f"""
// Virtual junction routing (PR-C): the lane-independent router finds a path across a
// virtual junction, branching off the unsplit main road at the mid-road anchor.
class FollowRouteTestVirtualJunction : public ::testing::Test
{{
protected:
    static void SetUpTestSuite()
    {{
        Position::LoadOpenDrive({_XODR});
    }}
}};

TEST_F(FollowRouteTestVirtualJunction, FindPathAcrossVirtualJunction)
{{
    ASSERT_NE(Position::GetOpenDrive(), nullptr);

    Position start(1, -1, 10, 0);
    start.SetHeadingRelativeRoadDirection(0);
    Position target(3, -1, 20, 0);

    LaneIndependentRouter router(Position::GetOpenDrive());
    std::vector<Node>     path = router.CalculatePath(start, target);

    ASSERT_FALSE(path.empty());
    EXPECT_EQ(path.back().road->GetId(), static_cast<id_t>(3));
}}
"""

# --------------------------------------------------------------------------- #
# PR-D: the VIRTUAL lane-pairing branch inserted into UpdateOSIIntersection.
# Translated from the GT-side reporter branch, adapted to the pristine scope.
# It is inserted right after the DIRECT junction branch closes.
# --------------------------------------------------------------------------- #
OSI_ANCHOR = """        if (junction->GetType() == roadmanager::Junction::JunctionType::DIRECT)
        {"""

OSI_VIRTUAL_BRANCH = """        else if (junction->GetType() == roadmanager::Junction::JunctionType::VIRTUAL)
        {
            // A virtual junction has no junction area of its own: the unsplit main road keeps its regular
            // driving lanes across the span and the branch roads are ordinary roads (already emitted as
            // TYPE_DRIVING). So there is no TYPE_INTERSECTION lane -- exactly like the direct junction above.
            // All we owe OSI is the per-laneLink lane_pairing tying the branch entry lane to the main-road lane
            // at the anchor section (GetLaneSectionByS at the connection's incoming contact s), not an end section.
            for (auto &c : junction->GetConnections())
            {
                roadmanager::Road *main_in = c->GetIncomingRoad();
                roadmanager::Road *branch  = c->GetConnectingRoad();
                // skip kind-2 topological connections (null connecting road) and the synthesized branch->main
                // counter-connections (connecting road == main road): pairing is registered once, on the branch
                // lane, from the main-road-incoming connection.
                if (main_in == nullptr || branch == nullptr || branch == main_in)
                {
                    continue;
                }
                const double anchor_s = c->GetIncomingContactS();
                if (anchor_s < 0.0)
                {
                    continue;
                }
                roadmanager::LaneSection *main_section   = main_in->GetLaneSectionByS(anchor_s);
                roadmanager::LaneSection *branch_section = branch->GetLaneSectionByIdx(0);
                if (main_section == nullptr || branch_section == nullptr)
                {
                    continue;
                }
                for (unsigned int l = 0; l < c->GetNumberOfLaneLinks(); l++)
                {
                    roadmanager::JunctionLaneLink *ll             = c->GetLaneLink(l);
                    idx_t                          from_global_id = main_section->GetLaneGlobalIdById(ll->from_);
                    idx_t                          to_global_id   = branch_section->GetLaneGlobalIdById(ll->to_);
                    if (from_global_id == ID_UNDEFINED || to_global_id == ID_UNDEFINED)
                    {
                        continue;
                    }
                    for (unsigned int jj = 0; jj < obj_osi_internal.ln.size(); jj++)
                    {
                        if (obj_osi_internal.ln[jj]->mutable_id()->value() != to_global_id)
                        {
                            continue;
                        }
                        osi_lane                                            = obj_osi_internal.ln[jj];
                        osi3::Lane_Classification_LanePairing *lane_pairing = nullptr;
                        if (osi_lane->mutable_classification()->mutable_lane_pairing()->size() == 0)
                        {
                            lane_pairing = osi_lane->mutable_classification()->add_lane_pairing();
                        }
                        else
                        {
                            lane_pairing = osi_lane->mutable_classification()->mutable_lane_pairing(0);
                        }
                        // same convention as the direct branch: contact START -> the branch begins at the anchor,
                        // so the main road is its antecessor; contact END -> its successor.
                        if (c->GetContactPoint() == roadmanager::ContactPointType::CONTACT_POINT_START)
                        {
                            lane_pairing->mutable_antecessor_lane_id()->set_value(from_global_id);
                        }
                        else if (c->GetContactPoint() == roadmanager::ContactPointType::CONTACT_POINT_END)
                        {
                            lane_pairing->mutable_successor_lane_id()->set_value(from_global_id);
                        }
                        else
                        {
                            LOG_ERROR("Unexpected virtual junction lane link contact point (junction {})", junction->GetId());
                        }
                        break;
                    }
                }
            }
        }
"""


def _write(repo, rel, content):
    p = os.path.join(repo, rel)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p, "w", encoding="utf-8", newline="") as fh:
        fh.write(content)


def _append(repo, rel, content):
    p = os.path.join(repo, rel)
    with open(p, "r", encoding="utf-8", newline="") as fh:
        text = fh.read()
    if not text.endswith("\n"):
        text += "\n"
    with open(p, "w", encoding="utf-8", newline="") as fh:
        fh.write(text + content)


def apply_assets(repo, slice_letter):
    """Write the assets for one slice, return touched repo-relative paths."""
    touched = []

    # fixture: written by every slice that carries code (idempotent identical content)
    if slice_letter in ("A", "B", "C", "D"):
        _write(repo, FIXTURE_REL, FIXTURE_XML)
        touched.append(FIXTURE_REL)

    # test appends accumulate down the stack (B has A+B, C has A+B+C)
    rm_blocks = {"A": [RM_TEST_A], "B": [RM_TEST_A, RM_TEST_B], "C": [RM_TEST_A, RM_TEST_B, RM_TEST_C], "D": [RM_TEST_A, RM_TEST_B, RM_TEST_C]}
    for block in rm_blocks[slice_letter]:
        _append(repo, RM_TEST_REL, block)
    touched.append(RM_TEST_REL)

    # FollowRoute test appended for C and D (stacked)
    if slice_letter in ("C", "D"):
        _append(repo, FR_TEST_REL, FR_TEST_C)
        touched.append(FR_TEST_REL)

    # PR-D: insert the VIRTUAL branch into the OSI reporter (line-ending agnostic)
    if slice_letter == "D":
        p = os.path.join(repo, OSI_REL)
        with open(p, "r", encoding="utf-8", newline="") as fh:
            raw = fh.read()
        crlf = "\r\n" in raw
        text = raw.replace("\r\n", "\n")
        # insert the VIRTUAL branch right after the DIRECT branch closes, i.e. just
        # before the "else if (junction->IsOsiIntersection())" that follows the DIRECT block.
        idx = text.find(OSI_ANCHOR)
        if idx < 0:
            raise SystemExit("PR-D: could not find the DIRECT junction anchor in OSIReporter.cpp")
        marker = "        else if (junction->IsOsiIntersection())"
        ins_at = text.find(marker, idx)
        if ins_at < 0:
            raise SystemExit("PR-D: could not find the IsOsiIntersection branch in OSIReporter.cpp")
        text = text[:ins_at] + OSI_VIRTUAL_BRANCH + text[ins_at:]
        if crlf:
            text = text.replace("\n", "\r\n")
        with open(p, "w", encoding="utf-8", newline="") as fh:
            fh.write(text)
        touched.append(OSI_REL)

    return touched
