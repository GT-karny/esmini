/*
 * GT_esmini extension -- OSI logical lane post-pass.
 *
 * See gt_esmini/osi/GT_OsiLogicalLane.hpp for why this translation unit lives in
 * the ScenarioEngine swap zone rather than in GT_esminiLib, and why the pass runs
 * last in the static ground-truth build.
 *
 * STAGE: S3 -- LogicalLaneBoundary (design 2-3) on top of S1 + S2.5b.
 *
 * S2.5b -- LogicalLaneAssignment (design 2-6-1), reported at the OSI reference
 * point (bounding-box centre) rather than the entity origin.
 *
 * S1 -- reference lines (design 2-1), logical lane bodies (2-2) and the
 * (road, lane section, lane) -> logical lane id index (3). Connectivity
 * (predecessor / successor / adjacent) is S2, and is `repeated`, so the message
 * stays well-formed without it.
 *
 * Design: GT_esmini/docs/osi/logical_lane_and_route_design.md 2-1 / 2-2 / 2-3 / 3 / 4 / 7
 * Knowledge graph: spine-work:osi-logical-lane
 */
#include "gt_esmini/osi/GT_OsiLogicalLane.hpp"

#include "CommonMini.hpp"
#include "RoadManager.hpp"
#include "GT_OSIReporter_Internals.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

namespace gt_esmini
{
namespace osi
{

namespace
{
// ---- feature flag: env read once on first query, setter overrides so tests are
//      deterministic. Same shape as gt_esmini::odr::GetUseAuthoredJunctionBoundary
//      but the OPPOSITE polarity -- that one is opt-in, this one has been an
//      opt-out since S3 (design 7-3), so "unset" means ON here. ----
bool g_use_logical_lane        = true;  // default ON since S3 (design 7-3)
bool g_use_logical_lane_inited = false;

// Same accepted-token set as the odr-side flag. Deliberately duplicated rather
// than shared: that helper sits in an anonymous namespace in
// odr_side/OdrJunctionGeom.cpp, and exporting it would create a cross-module
// dependency for four lines of string comparison.
bool EnvIsTruthy(const char* v)
{
    if (v == nullptr || v[0] == '\0')
    {
        return false;
    }
    std::string s(v);
    for (char& c : s)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s == "1" || s == "true" || s == "yes" || s == "on";
}

LogicalLaneIndex g_logical_lane_index;

// Same string the physical osi_lane and the traffic-sign pass use. The lane_map
// parser in _gt_to_scene keys off it (design 2-2).
constexpr const char* kSourceRefTypeOdr = "net.asam.opendrive";

// Lane-section seams: the last reference-line point of section j sits at
// (section j+1 start - SMALL_NUMBER/2), i.e. ~5e-11 m before the first point of
// the next section. Concatenating raw would emit two points at the same place,
// and ReferenceLine requires STRICTLY increasing s. 1 um is far below the 0.2 m
// minimum spacing RoadManager itself enforces between genuine points, so this
// can only ever collapse a seam.
constexpr double kSeamToleranceS = 1e-6;

bool IsDrivingLikeType(int t)
{
    return (t & static_cast<int>(roadmanager::Lane::LaneType::LANE_TYPE_ANY_DRIVING)) != 0;
}

// The four LogicalLane types the proto says have no Lane.Classification.Subtype
// counterpart, and for which physical_lane_reference "has no value".
bool HasNoPhysicalCounterpart(int t)
{
    return t == static_cast<int>(roadmanager::Lane::LaneType::LANE_TYPE_MEDIAN) ||
           t == static_cast<int>(roadmanager::Lane::LaneType::LANE_TYPE_CURB) ||
           t == static_cast<int>(roadmanager::Lane::LaneType::LANE_TYPE_TRAM) ||
           t == static_cast<int>(roadmanager::Lane::LaneType::LANE_TYPE_RAIL);
}

// ---------------------------------------------------------------------------
// S3 -- LogicalLaneBoundary (design 2-3)
// ---------------------------------------------------------------------------

// Vertical budget for the emitted polyline. The proto asks for <=2cm against the
// ideal line, deliberately stricter than the 5cm it allows in XY, "because Z
// differences between lanes influence driving very much". Halved, like the lateral
// budget below, so the INDEPENDENT measurement -- sampled at s values the
// construction never used -- has room before it reaches the requirement. A
// construction that only satisfied the requirement at its own sample points would
// be checking itself.
constexpr double kBoundaryBudgetZ = 0.01;

// Refinement stops at whichever comes first. 0.05 m is a quarter of the 0.2 m
// minimum spacing RoadManager enforces between genuine OSI points, so the grid can
// never become finer than the road model it is sampling.
constexpr int    kBoundaryMaxRefineDepth = 6;
constexpr double kBoundaryMinStepS       = 0.05;

double BoundaryBudgetXY()
{
    // Whatever the run is configured for, not a second hard-coded 5cm: --osi_lateral
    // deviation moves esmini's own OSI point density, and a boundary built to a
    // different budget than the reference line it shares would be inconsistent.
    return 0.5 * SE_Env::Inst().GetOSIMaxLateralDeviation();
}

// One SIDE of a lane edge: the lane whose surface meets it, and which of that lane's
// two edges it is. An edge normally has two sides -- lane k's outer edge is lane
// k+sign(k)'s inner edge -- and they agree in XY by construction, because
// LaneSection::GetInnerOffset(s, k) is DEFINED as GetOuterOffset(s, k - sign(k)).
//
// They do NOT always agree in Z. OpenDRIVE's <height> raises a lane above the road
// surface (0.12 m for the curbs and sidewalks in the shipped maps), and the proto
// spells out the consequence: "if two lanes have different Z heights ... then these
// lanes cannot share a boundary, since their boundaries have different Z heights."
// Evaluating each side ON ITS OWN LANE is what makes that difference visible -- a
// single SetTrackPos at the edge t lands on whichever of the two lanes Track2Lane
// happens to resolve, so it silently picks one height and hides the other.
struct EdgeSide
{
    int  lane_id = 0;
    bool outer   = true;  // true: the lane's outer edge; false: its inner edge
};

// One sample of an edge: the geometry (shared by every side, because an edge is ONE
// line in XY) plus the z each side sits at. A curb is a vertical face -- the same
// line at two heights -- so the sides must not be refined independently: two
// polylines each within budget of the ideal edge were measured up to 14 mm from EACH
// OTHER on multi_intersections, which is not what "the same line at two heights"
// means and leaves a consumer unable to match them.
struct EdgeSample
{
    double              s = 0.0;
    double              t = 0.0;
    double              x = 0.0;
    double              y = 0.0;
    std::vector<double> z;  // one per side, in the order the sides were given
};

// Builds an edge's polyline: seeded from the reference line's own s grid, then
// bisected wherever the chord strays further from the true edge than the budget.
//
// The seed grid is the reference line's because the boundary must live in that
// line's s coordinates, but it is NOT sufficient on its own: the reference line's
// point density is chosen for the reference line's curvature, while an edge several
// metres out rides a different radius, a width polynomial of its own, and -- on the
// far side of a curb -- a <height> that steps part way along. That is the place
// design 10-2 flagged as where the 5cm requirement could quietly break.
class EdgeBuilder
{
public:
    EdgeBuilder(roadmanager::Road* road, roadmanager::LaneSection* lsec, unsigned sec_idx, const std::vector<EdgeSide>& sides)
        : road_(road), lsec_(lsec), sec_idx_(sec_idx), sides_(sides), probes_(sides.size())
    {
    }

    EdgeSample Sample(double s)
    {
        EdgeSample out;
        out.s = s;
        out.z.resize(sides_.size());
        for (std::size_t i = 0; i < sides_.size(); i++)
        {
            // Offset from the lane's CENTRE to the requested edge. This is the same
            // placement SetLaneBoundaryPos uses for the physical boundary of an
            // unmarked lane (offset = SIGN(lane) * width/2), so the logical boundary
            // and the physical one it references sit on the same line rather than
            // merely near it. Evaluating ON the lane is also what brings the lane's
            // own <height> into z: a single SetTrackPos at the edge t would land on
            // whichever of the two lanes Track2Lane happens to resolve and silently
            // pick one of the two heights.
            //
            // The lane section index is passed explicitly: at s == the section end,
            // resolving it from s alone lands in the NEXT section and takes its widths.
            const double w      = lsec_->GetWidth(s, sides_[i].lane_id);
            const double sign   = (sides_[i].lane_id < 0) ? -1.0 : 1.0;
            const double offset = (sides_[i].outer ? 1.0 : -1.0) * sign * 0.5 * w;
            probes_[i].SetLanePos(road_->GetId(), sides_[i].lane_id, s, offset, sec_idx_);
            out.z[i] = probes_[i].GetZ();
            if (i == 0)
            {
                out.t = probes_[i].GetT();
                out.x = probes_[i].GetX();
                out.y = probes_[i].GetY();
            }
        }
        return out;
    }

    // Appends the interior samples of [a, b] that the budget needs. a and b are taken
    // BY VALUE: the caller's `a` is typically out.back(), and every push_back below
    // can reallocate the vector it points into.
    void Refine(EdgeSample a, EdgeSample b, std::vector<EdgeSample>& out, int depth)
    {
        if (depth >= kBoundaryMaxRefineDepth || (b.s - a.s) <= 2.0 * kBoundaryMinStepS)
        {
            return;
        }
        // THREE interior probes, not just the midpoint. A width polynomial that bulges
        // symmetrically about the middle of an interval has zero deviation exactly at
        // the midpoint, so a midpoint-only test declares it converged and stops --
        // measured on soderleden road 0, where that left 0.33 m between the chord and
        // the real lane edge while every construction sample said 0.
        bool split = false;
        for (double u : {0.25, 0.5, 0.75})
        {
            const EdgeSample q = Sample(a.s + u * (b.s - a.s));
            if (PointDistance2D(q.x, q.y, a.x + u * (b.x - a.x), a.y + u * (b.y - a.y)) > BoundaryBudgetXY())
            {
                split = true;
                break;
            }
            // EVERY side's z, not just the one the geometry came from. Road 276 of
            // multi_intersections is a 22 m section whose sidewalk steps from 0.02 m
            // to 0.12 m at s=3: judged on the road-side z alone the whole section
            // converges at two points, and the sidewalk side is then interpolated
            // straight through its own step.
            for (std::size_t i = 0; i < q.z.size(); i++)
            {
                if (std::fabs(q.z[i] - (a.z[i] + u * (b.z[i] - a.z[i]))) > kBoundaryBudgetZ)
                {
                    split = true;
                    break;
                }
            }
            if (split)
            {
                break;
            }
        }
        if (!split)
        {
            return;
        }
        const EdgeSample m = Sample(0.5 * (a.s + b.s));
        Refine(a, m, out, depth + 1);
        out.push_back(m);
        Refine(m, b, out, depth + 1);
    }

private:
    roadmanager::Road*                 road_    = nullptr;
    roadmanager::LaneSection*          lsec_    = nullptr;
    unsigned                           sec_idx_ = 0;
    std::vector<EdgeSide>              sides_;
    std::vector<roadmanager::Position> probes_;
};


// (road, lane section, edge owner lane, the lane viewing it). The last field is
// what lets one edge carry two boundaries when the two sides differ in height; when
// they do not, both lanes' keys map to the same id and the sharing the proto expects
// -- and that S2's lateral adjacency can be recovered from -- is exact.
using BoundaryKey = std::tuple<std::uint32_t, unsigned, int, int>;

int MapPassingRule(const roadmanager::LaneRoadMark* mark)
{
    // PASSING_RULE_UNKNOWN is forbidden in ground truth ("must not be used in ground
    // truth"), so an edge with no marking falls to OTHER -- which the proto itself
    // names as the value "between LogicalLanes of TYPE_NORMAL and TYPE_CURB", i.e.
    // exactly the unmarked case.
    if (mark == nullptr)
    {
        return osi3::LogicalLaneBoundary_PassingRule_PASSING_RULE_OTHER;
    }
    switch (mark->GetLaneChange())
    {
        case roadmanager::LaneRoadMark::RoadMarkLaneChange::BOTH:
            return osi3::LogicalLaneBoundary_PassingRule_PASSING_RULE_BOTH_ALLOWED;
        case roadmanager::LaneRoadMark::RoadMarkLaneChange::NONE_LANECHANGE:
            return osi3::LogicalLaneBoundary_PassingRule_PASSING_RULE_NONE_ALLOWED;
        case roadmanager::LaneRoadMark::RoadMarkLaneChange::INCREASE:
            return osi3::LogicalLaneBoundary_PassingRule_PASSING_RULE_INCREASING_T;
        case roadmanager::LaneRoadMark::RoadMarkLaneChange::DECREASE:
            return osi3::LogicalLaneBoundary_PassingRule_PASSING_RULE_DECREASING_T;
        default:
            return osi3::LogicalLaneBoundary_PassingRule_PASSING_RULE_OTHER;
    }
}
}  // namespace

// ---------------------------------------------------------------------------
// pure mappings
// ---------------------------------------------------------------------------

int MapLaneTypeToLogicalLaneType(int rm_lane_type)
{
    using LT = roadmanager::Lane::LaneType;
    switch (static_cast<LT>(rm_lane_type))
    {
        case LT::LANE_TYPE_DRIVING:
        // A bidirectional lane is TYPE_NORMAL with MOVE_DIRECTION_BOTH_ALLOWED --
        // the proto spells this case out explicitly under TYPE_NORMAL.
        case LT::LANE_TYPE_BIDIRECTIONAL:
            return osi3::LogicalLane_Type_TYPE_NORMAL;
        case LT::LANE_TYPE_BIKING:
            return osi3::LogicalLane_Type_TYPE_BIKING;
        case LT::LANE_TYPE_SIDEWALK:
            return osi3::LogicalLane_Type_TYPE_SIDEWALK;
        case LT::LANE_TYPE_PARKING:
            return osi3::LogicalLane_Type_TYPE_PARKING;
        case LT::LANE_TYPE_STOP:
            return osi3::LogicalLane_Type_TYPE_STOP;
        case LT::LANE_TYPE_RESTRICTED:
            return osi3::LogicalLane_Type_TYPE_RESTRICTED;
        case LT::LANE_TYPE_BORDER:
            return osi3::LogicalLane_Type_TYPE_BORDER;
        case LT::LANE_TYPE_SHOULDER:
            return osi3::LogicalLane_Type_TYPE_SHOULDER;
        case LT::LANE_TYPE_ENTRY:
            return osi3::LogicalLane_Type_TYPE_ENTRY;
        case LT::LANE_TYPE_EXIT:
            return osi3::LogicalLane_Type_TYPE_EXIT;
        case LT::LANE_TYPE_ON_RAMP:
            return osi3::LogicalLane_Type_TYPE_ONRAMP;
        case LT::LANE_TYPE_OFF_RAMP:
            return osi3::LogicalLane_Type_TYPE_OFFRAMP;
        case LT::LANE_TYPE_CONNECTING_RAMP:
            return osi3::LogicalLane_Type_TYPE_CONNECTINGRAMP;
        // The four below have no Lane.Classification.Subtype counterpart, so the
        // physical lane reports them as SUBTYPE_OTHER / SUBTYPE_BORDER today. The
        // logical layer is where that distinction survives (design 2-2-1).
        case LT::LANE_TYPE_MEDIAN:
            return osi3::LogicalLane_Type_TYPE_MEDIAN;
        case LT::LANE_TYPE_CURB:
            return osi3::LogicalLane_Type_TYPE_CURB;
        case LT::LANE_TYPE_RAIL:
            return osi3::LogicalLane_Type_TYPE_RAIL;
        case LT::LANE_TYPE_TRAM:
            return osi3::LogicalLane_Type_TYPE_TRAM;
        default:
            // roadworks / special1-3 / none, and anything a future OpenDRIVE adds.
            // TYPE_UNKNOWN is forbidden in ground truth, so never emit it.
            return osi3::LogicalLane_Type_TYPE_OTHER;
    }
}

int MapMoveDirection(int rm_lane_type, int lane_id, bool right_hand_traffic)
{
    if (rm_lane_type == static_cast<int>(roadmanager::Lane::LaneType::LANE_TYPE_BIDIRECTIONAL))
    {
        return osi3::LogicalLane_MoveDirection_MOVE_DIRECTION_BOTH_ALLOWED;
    }
    if (!IsDrivingLikeType(rm_lane_type))
    {
        // Sidewalks, medians, curbs, shoulders, rails: nothing drives along them in
        // a road-traffic sense, and pedestrians walk a sidewalk both ways.
        return osi3::LogicalLane_MoveDirection_MOVE_DIRECTION_BOTH_ALLOWED;
    }
    // Same predicate as the physical lane's centerline_is_driving_direction
    // (GT_OSIReporter_Geometry.cpp): under RHT the negative lanes run with +s,
    // under LHT the positive ones do.
    const bool along_s = (lane_id < 0) == right_hand_traffic;
    return along_s ? osi3::LogicalLane_MoveDirection_MOVE_DIRECTION_INCREASING_S
                   : osi3::LogicalLane_MoveDirection_MOVE_DIRECTION_DECREASING_S;
}

// ---------------------------------------------------------------------------
// flag + index accessors
// ---------------------------------------------------------------------------

void SetUseOsiLogicalLane(bool on)
{
    g_use_logical_lane        = on;
    g_use_logical_lane_inited = true;  // suppress the env read so tests are deterministic
}

bool GetUseOsiLogicalLane()
{
    if (!g_use_logical_lane_inited)
    {
        // DEFAULT ON since S3 (design 7-3). Unset means on; the variable is now an
        // OPT-OUT, so it is only consulted when it is actually set. Reading it as
        // "truthy or off" the way the odr-side flags do would silently turn the layer
        // off for everyone who has the variable set to something unrecognised.
        const char* v      = std::getenv("GT_OSI_LOGICAL_LANE");
        g_use_logical_lane = (v == nullptr || v[0] == '\0') ? true : EnvIsTruthy(v);
        g_use_logical_lane_inited = true;
    }
    return g_use_logical_lane;
}

const LogicalLaneIndex& GetLogicalLaneIndex()
{
    return g_logical_lane_index;
}

// ---------------------------------------------------------------------------
// the post-pass
// ---------------------------------------------------------------------------

void BuildOsiLogicalLanesInto(roadmanager::OpenDrive* opendrive, osi3::GroundTruth* gt, LogicalLaneIndex* index)
{
    if (opendrive == nullptr || gt == nullptr || index == nullptr)
    {
        return;
    }
    index->clear();

    const unsigned n_roads = opendrive->GetNumOfRoads();

    // ---- pass 1: one ReferenceLine per road (design 2-1) ----------------------
    //
    // Per road, not per lane section: the proto strongly encourages neighbouring
    // lanes to share a reference line so that objects side by side get comparable
    // s. Sharing it across the whole road additionally keeps the OSI s identical
    // to the OpenDRIVE road s, which is what lets the index and the HVD route (S4)
    // resolve without a second coordinate conversion.
    std::vector<std::uint64_t> ref_line_id(n_roads, 0);
    unsigned                   ref_line_points      = 0;
    unsigned                   degenerate_ref_lines = 0;

    for (unsigned i = 0; i < n_roads; i++)
    {
        roadmanager::Road* road = opendrive->GetRoadByIdx(i);
        if (road == nullptr)
        {
            continue;
        }
        osi3::ReferenceLine* rl = gt->add_reference_line();
        rl->mutable_id()->set_value(GetNewGlobalId());
        // TYPE_POLYLINE is marked DEPRECATED in the proto (removed in 4.0.0) and
        // leaves t ambiguous where the polyline bends; the T-axis variant carries
        // the OpenDRIVE normal explicitly.
        rl->set_type(osi3::ReferenceLine_Type_TYPE_POLYLINE_WITH_T_AXIS);
        ref_line_id[i] = rl->id().value();

        bool   have_last = false;
        double last_s    = 0.0;
        for (unsigned j = 0; j < road->GetNumberOfLaneSections(); j++)
        {
            roadmanager::LaneSection* lsec = road->GetLaneSectionByIdx(j);
            if (lsec == nullptr)
            {
                continue;
            }
            for (const roadmanager::PointStruct& p : lsec->GetRefLineOSIPoints().GetPoints())
            {
                if (have_last && p.s <= last_s + kSeamToleranceS)
                {
                    continue;  // lane-section seam, or a degenerate repeat
                }
                osi3::ReferenceLine_ReferenceLinePoint* rp = rl->add_poly_line();
                rp->mutable_world_position()->set_x(p.x);
                rp->mutable_world_position()->set_y(p.y);
                rp->mutable_world_position()->set_z(p.z);
                // s is the OpenDRIVE road s straight through. The proto explicitly
                // allows this ("these rules allow directly putting OpenDRIVE S
                // coordinates into an OSI ReferenceLine") because arc length along
                // the reference line is always >= the chord between two samples.
                rp->set_s_position(p.s);
                // (nx, ny) is RoadManager's road normal: local +Y rotated by the
                // point's h/p/r (GT_RoadManager.cpp SetLaneOSIPoints). +Y is the
                // direction of increasing t, which is exactly what the proto asks
                // t_axis_yaw to be. The polarity is pinned by a unit test, because
                // a sign flip here still draws a perfectly plausible road.
                rp->set_t_axis_yaw(std::atan2(p.ny, p.nx));
                last_s    = p.s;
                have_last = true;
                ref_line_points++;
            }
        }
        if (rl->poly_line_size() < 2)
        {
            // "At least two points must be given." Reachable only if RoadManager
            // failed to place the road's reference line at all; emitting the line
            // anyway keeps every lane's reference_line_id resolvable.
            degenerate_ref_lines++;
            LOG_WARN("[GT_OSI:logical-lane] road {} reference line has {} point(s) -- OSI requires at least 2",
                     road->GetId(),
                     rl->poly_line_size());
        }
    }

    // ---- pass 2: LogicalLaneBoundary (design 2-3) -----------------------------
    //
    // One boundary per LANE EDGE per marking stretch. A section with n lanes has n+1
    // edges (the centre line plus each lane's outer edge), and neighbouring lanes
    // SHARE the edge between them -- which is what lets a consumer recover adjacency
    // from boundary ids alone, and is why S2 can be the last stage (design 8-alpha).
    // The one exception is the one the proto names: lanes at different <height>
    // (a curb is 0.12 m up) get one boundary each, identical in XY and apart in Z.
    //
    // Existing physical boundaries are not reused. There is no single source to reuse:
    // SetLaneBoundaryPoints() only builds a LaneBoundaryOSI for lanes with no road
    // marks, and a marked lane carries only its road mark lines. Both happen to sit on
    // the lane's outer edge in esmini's model, but neither covers all lanes.
    //
    // The ids emitted into physical_boundary_id[] are filtered against what is
    // actually in lane_boundary[]: this pass runs last, so that repeated field is
    // already complete, and checking membership here is what makes the reference
    // closure structural instead of something a probe has to hope for.
    std::set<std::uint64_t> emitted_physical_boundaries;
    for (int b = 0; b < gt->lane_boundary_size(); b++)
    {
        emitted_physical_boundaries.insert(gt->lane_boundary(b).id().value());
    }

    std::map<BoundaryKey, std::vector<std::uint64_t>> boundary_index;

    unsigned n_boundaries = 0, n_boundary_points = 0, n_split_by_roadmark = 0, n_split_by_height = 0;
    unsigned n_physical_linked = 0, n_physical_dropped = 0, n_boundaries_without_physical = 0;

    for (unsigned i = 0; i < n_roads; i++)
    {
        roadmanager::Road* road = opendrive->GetRoadByIdx(i);
        if (road == nullptr)
        {
            continue;
        }
        const unsigned n_sections = road->GetNumberOfLaneSections();
        for (unsigned j = 0; j < n_sections; j++)
        {
            roadmanager::LaneSection* lsec = road->GetLaneSectionByIdx(j);
            if (lsec == nullptr)
            {
                continue;
            }
            const double sec_start = lsec->GetS();
            const double sec_end   = (j + 1 < n_sections) ? road->GetLaneSectionByIdx(j + 1)->GetS() : road->GetLength();
            if (!(sec_end > sec_start))
            {
                continue;  // degenerate section -- pass 3 skips it too, and warns there
            }

            // The reference line's own s values inside this section, which the boundary
            // must be expressed in. Section ends are added explicitly: the last stored
            // point of a section sits SMALL_NUMBER/2 short of the seam (S1), and the
            // boundary has to reach [start_s, end_s] exactly to cover its lanes.
            std::vector<double> seed;
            seed.push_back(sec_start);
            for (const roadmanager::PointStruct& rp : lsec->GetRefLineOSIPoints().GetPoints())
            {
                if (rp.s > sec_start + kSeamToleranceS && rp.s < sec_end - kSeamToleranceS)
                {
                    seed.push_back(rp.s);
                }
            }
            seed.push_back(sec_end);

            // Every edge of the section, named by the lane that OWNS it: the centre
            // line (0) and each lane's outer edge. The owner is the lane whose
            // OpenDRIVE <roadMark> describes this edge.
            std::vector<int> edge_owners;
            edge_owners.push_back(0);
            for (idx_t k = 0; k < lsec->GetNumberOfLanes(); k++)
            {
                roadmanager::Lane* lane = lsec->GetLaneByIdx(k);
                if (lane != nullptr && !lane->IsCenter())
                {
                    edge_owners.push_back(lane->GetId());
                }
            }

            for (int edge_owner : edge_owners)
            {
                roadmanager::Lane* owner = lsec->GetLaneById(edge_owner);
                if (owner == nullptr)
                {
                    continue;
                }

                // The lanes that actually meet at this edge. The centre line is the
                // INNER edge of both lane -1 and lane +1; every other edge is lane k's
                // outer edge and lane k+sign(k)'s inner edge. A missing neighbour (the
                // outermost edge of a section) simply leaves one side.
                std::vector<EdgeSide> sides;
                if (edge_owner == 0)
                {
                    for (int nb : {-1, 1})
                    {
                        if (lsec->GetLaneById(nb) != nullptr)
                        {
                            sides.push_back(EdgeSide{nb, false});
                        }
                    }
                }
                else
                {
                    sides.push_back(EdgeSide{edge_owner, true});
                    const int nb = edge_owner + ((edge_owner < 0) ? -1 : 1);
                    if (lsec->GetLaneById(nb) != nullptr)
                    {
                        sides.push_back(EdgeSide{nb, false});
                    }
                }
                if (sides.empty())
                {
                    continue;  // an edge with no lane on either side is not an edge
                }

                // Do the sides share a Z? Sampled at both ends and the middle, because
                // <height> is piecewise along s and a pair that agrees at the start can
                // part company later in the same section.
                EdgeBuilder builder(road, lsec, j, sides);
                bool        split_z = false;
                if (sides.size() == 2)
                {
                    for (double frac : {0.0, 0.5, 1.0})
                    {
                        const EdgeSample probe = builder.Sample(sec_start + frac * (sec_end - sec_start));
                        if (std::fabs(probe.z[0] - probe.z[1]) > kBoundaryBudgetZ)
                        {
                            split_z = true;
                            break;
                        }
                    }
                }
                if (split_z)
                {
                    n_split_by_height++;
                }

                // Split points along s: the proto requires a NEW LogicalLaneBoundary
                // wherever the physical boundaries begin, end or change, so one boundary
                // per road mark rather than one per section. Without the split,
                // "consecutive boundaries must share a point" would be vacuous, and a
                // section whose marking changes half way would advertise one passing
                // rule for both halves.
                std::vector<double> cuts;
                cuts.push_back(sec_start);
                const unsigned n_marks = owner->GetNumberOfRoadMarks();
                for (unsigned m = 0; m < n_marks; m++)
                {
                    roadmanager::LaneRoadMark* mark = owner->GetLaneRoadMarkByIdx(m);
                    if (mark == nullptr)
                    {
                        continue;
                    }
                    const double mark_s = sec_start + mark->GetSOffset();
                    if (mark_s > cuts.back() + kBoundaryMinStepS && mark_s < sec_end - kBoundaryMinStepS)
                    {
                        cuts.push_back(mark_s);
                    }
                }
                cuts.push_back(sec_end);
                if (cuts.size() > 2)
                {
                    n_split_by_roadmark++;
                }

                // ONE point set per marking stretch, however many Z groups read it --
                // and the refinement above has already accounted for every side's z, so
                // each group is the same line with its own heights read off the same
                // samples.
                const std::size_t n_groups = split_z ? sides.size() : 1u;

                for (std::size_t c = 0; c + 1 < cuts.size(); c++)
                {
                    const double lo = cuts[c];
                    const double hi = cuts[c + 1];

                    std::vector<EdgeSample> pts;
                    pts.push_back(builder.Sample(lo));
                    for (double sv : seed)
                    {
                        if (sv <= lo + kSeamToleranceS || sv >= hi - kSeamToleranceS)
                        {
                            continue;
                        }
                        const EdgeSample next = builder.Sample(sv);
                        builder.Refine(pts.back(), next, pts, 0);
                        pts.push_back(next);
                    }
                    {
                        const EdgeSample last = builder.Sample(hi);
                        builder.Refine(pts.back(), last, pts, 0);
                        pts.push_back(last);
                    }

                    for (std::size_t gidx = 0; gidx < n_groups; gidx++)
                    {
                        osi3::LogicalLaneBoundary* lb = gt->add_logical_lane_boundary();
                        lb->mutable_id()->set_value(GetNewGlobalId());
                        lb->mutable_reference_line_id()->set_value(ref_line_id[i]);
                        for (const EdgeSample& pt : pts)
                        {
                            osi3::LogicalLaneBoundary_LogicalBoundaryPoint* bp = lb->add_boundary_line();
                            bp->mutable_position()->set_x(pt.x);
                            bp->mutable_position()->set_y(pt.y);
                            bp->mutable_position()->set_z(pt.z[gidx]);
                            bp->set_s_position(pt.s);
                            bp->set_t_position(pt.t);
                        }
                        n_boundary_points += static_cast<unsigned>(pts.size());

                        // The road mark covering THIS stretch: the last one that has
                        // begun by its start. cuts[] was built from those same s
                        // offsets, so the stretch is covered by exactly one.
                        roadmanager::LaneRoadMark* active = nullptr;
                        for (unsigned m = 0; m < n_marks; m++)
                        {
                            roadmanager::LaneRoadMark* mark = owner->GetLaneRoadMarkByIdx(m);
                            if (mark != nullptr && sec_start + mark->GetSOffset() <= lo + kBoundaryMinStepS)
                            {
                                active = mark;
                            }
                        }
                        lb->set_passing_rule(static_cast<osi3::LogicalLaneBoundary_PassingRule>(MapPassingRule(active)));

                        // PHYSICAL BOUNDARIES on this edge. Both candidates sit exactly
                        // on the owner lane's outer edge in esmini: SetLaneBoundaryPos
                        // and SetRoadMarkPos both resolve to offset = SIGN(lane)*width/2
                        // (the latter is called with a zero t offset, so
                        // LaneRoadMarkTypeLine's own GetTOffset never reaches the OSI
                        // output). The proto's "increasing T order" is therefore
                        // degenerate here -- they are all at one t -- and no ordering
                        // can be got wrong. The centre line (owner 0) is the inner edge
                        // of lanes +-1 and has no physical counterpart of its own unless
                        // lane 0 itself carries the marking, which is exactly what
                        // OpenDRIVE's centre <roadMark> is.
                        unsigned linked_here = 0;
                        if (owner->GetLaneBoundaryGlobalId() != ID_UNDEFINED)
                        {
                            const std::uint64_t pid = owner->GetLaneBoundaryGlobalId();
                            if (emitted_physical_boundaries.count(pid) != 0)
                            {
                                lb->add_physical_boundary_id()->set_value(pid);
                                linked_here++;
                            }
                            else
                            {
                                n_physical_dropped++;
                            }
                        }
                        else if (active != nullptr)
                        {
                            for (unsigned ti = 0; ti < active->GetNumberOfRoadMarkTypes(); ti++)
                            {
                                roadmanager::LaneRoadMarkType* mt = active->GetLaneRoadMarkTypeByIdx(ti);
                                if (mt == nullptr)
                                {
                                    continue;
                                }
                                for (unsigned li = 0; li < mt->GetNumberOfRoadMarkTypeLines(); li++)
                                {
                                    roadmanager::LaneRoadMarkTypeLine* line = mt->GetLaneRoadMarkTypeLineByIdx(li);
                                    if (line == nullptr)
                                    {
                                        continue;
                                    }
                                    // "The referenced LaneBoundary objects may be longer
                                    // than the LogicalLaneBoundary which references them,
                                    // but must never be shorter." A line that starts
                                    // inside its own road mark is shorter than the stretch
                                    // cut from that road mark, so it is left out rather
                                    // than referenced wrongly.
                                    if (line->GetSOffset() > SMALL_NUMBER)
                                    {
                                        n_physical_dropped++;
                                        continue;
                                    }
                                    if (emitted_physical_boundaries.count(line->GetGlobalId()) == 0)
                                    {
                                        n_physical_dropped++;
                                        continue;
                                    }
                                    lb->add_physical_boundary_id()->set_value(line->GetGlobalId());
                                    linked_here++;
                                }
                            }
                        }
                        n_physical_linked += linked_here;
                        if (linked_here == 0)
                        {
                            // Legal: "This list is empty if there are no physical lane
                            // boundaries to delimit a lane."
                            n_boundaries_without_physical++;
                        }

                        // Every side that reads this geometry gets the id. With one
                        // group that is both lanes -- the sharing the proto expects.
                        for (std::size_t si = 0; si < sides.size(); si++)
                        {
                            if (split_z && si != gidx)
                            {
                                continue;
                            }
                            boundary_index[BoundaryKey{road->GetId(), j, edge_owner, sides[si].lane_id}].push_back(
                                lb->id().value());
                        }
                        n_boundaries++;
                    }
                }
            }
        }
    }


    // ---- pass 3: LogicalLane bodies (design 2-2). No connectivity.
    unsigned n_lanes = 0, n_junction_lanes = 0;
    for (unsigned i = 0; i < n_roads; i++)
    {
        roadmanager::Road* road = opendrive->GetRoadByIdx(i);
        if (road == nullptr)
        {
            continue;
        }
        const unsigned    n_sections  = road->GetNumberOfLaneSections();
        const std::string street_name = road->GetName();
        const bool        rht         = road->GetRule() == roadmanager::Road::RoadRule::RIGHT_HAND_TRAFFIC;

        for (unsigned j = 0; j < n_sections; j++)
        {
            roadmanager::LaneSection* lsec = road->GetLaneSectionByIdx(j);
            if (lsec == nullptr)
            {
                continue;
            }
            const double start_s = lsec->GetS();
            const double end_s   = (j + 1 < n_sections) ? road->GetLaneSectionByIdx(j + 1)->GetS() : road->GetLength();
            if (!(end_s > start_s))
            {
                // "Requirement: end_s > start_s". A zero-length lane section is
                // malformed OpenDRIVE; skipping it leaves those lanes out of the
                // index, which callers already have to tolerate.
                LOG_WARN("[GT_OSI:logical-lane] road {} lane section {} is degenerate (s {} .. {}) -- skipped",
                         road->GetId(),
                         j,
                         start_s,
                         end_s);
                continue;
            }
            // Road-level speed, in m/s. OpenDRIVE's per-lane <speed> is not parsed
            // (design 10-4), so every lane of the section gets the same value.
            const double speed_limit_ms = road->GetSpeedByS(start_s);

            for (unsigned k = 0; k < lsec->GetNumberOfLanes(); k++)
            {
                roadmanager::Lane* lane = lsec->GetLaneByIdx(k);
                if (lane == nullptr || lane->IsCenter())
                {
                    continue;  // the centre lane has zero width -- no area, no logical lane
                }
                const int rm_type = static_cast<int>(lane->GetLaneType());
                const int lane_id = lane->GetId();

                osi3::LogicalLane* ll = gt->add_logical_lane();
                ll->mutable_id()->set_value(GetNewGlobalId());
                ll->set_type(static_cast<osi3::LogicalLane_Type>(MapLaneTypeToLogicalLaneType(rm_type)));
                ll->mutable_reference_line_id()->set_value(ref_line_id[i]);
                ll->set_start_s(start_s);
                ll->set_end_s(end_s);
                ll->set_move_direction(static_cast<osi3::LogicalLane_MoveDirection>(MapMoveDirection(rm_type, lane_id, rht)));

                // SOURCE REFERENCE -- prefixed identifiers, matching the physical
                // osi_lane rather than the bare ids the proto shows, so the one
                // lane_map parser in _gt_to_scene resolves both faces (design 2-2).
                osi3::ExternalReference* src = ll->add_source_reference();
                src->set_type(kSourceRefTypeOdr);
                src->add_identifier(fmt::format("road_id:{}", road->GetId()));
                src->add_identifier(fmt::format("road_s:{}", start_s));
                src->add_identifier(fmt::format("lane_id:{}", lane_id));

                // PHYSICAL LANE REFERENCE (design 2-2-3)
                if (!HasNoPhysicalCounterpart(rm_type))
                {
                    // Inside an OSI intersection the physical lanes are fused into a
                    // single TYPE_INTERSECTION lane carrying the junction's global
                    // id, so lane->GetGlobalId() names no osi3::Lane at all and
                    // would dangle. Several logical lanes pointing at one physical
                    // lane is explicitly allowed by the proto.
                    const id_t physical_id = lane->IsOSIIntersection() ? lane->GetOSIIntersectionId() : lane->GetGlobalId();
                    if (physical_id != ID_UNDEFINED)
                    {
                        osi3::LogicalLane_PhysicalLaneReference* plr = ll->add_physical_lane_reference();
                        plr->mutable_physical_lane_id()->set_value(physical_id);
                        // "s position ON THE LOGICAL LANE", so the lane's own range.
                        plr->set_start_s(start_s);
                        plr->set_end_s(end_s);
                    }
                }
                if (lane->IsOSIIntersection())
                {
                    n_junction_lanes++;
                }

                // BOUNDARIES (design 2-3). "Right" is in reference line direction, so
                // the right boundary is the one with the SMALLER t: for a left lane
                // (id > 0) that is its inner edge, for a right lane its outer one. The
                // inner edge of lane k IS the outer edge of lane k - sign(k), which is
                // how the two lanes either side of an edge end up naming one id.
                const int  outer_owner = lane_id;
                const int  inner_owner = lane_id - ((lane_id < 0) ? -1 : 1);
                const auto outer_it    = boundary_index.find(BoundaryKey{road->GetId(), j, outer_owner, lane_id});
                const auto inner_it    = boundary_index.find(BoundaryKey{road->GetId(), j, inner_owner, lane_id});
                if (outer_it != boundary_index.end() && inner_it != boundary_index.end())
                {
                    const std::vector<std::uint64_t>& outer = outer_it->second;
                    const std::vector<std::uint64_t>& inner = inner_it->second;
                    for (std::uint64_t bid : (lane_id > 0) ? outer : inner)
                    {
                        ll->add_left_boundary_id()->set_value(bid);
                    }
                    for (std::uint64_t bid : (lane_id > 0) ? inner : outer)
                    {
                        ll->add_right_boundary_id()->set_value(bid);
                    }
                }
                else
                {
                    // Only reachable if the section's lane ids are not consecutive from
                    // the centre outwards, which OpenDRIVE does not allow and
                    // LaneSection::GetOuterOffset (a recursion stepping by 1) already
                    // assumes. Say so rather than emit a lane with no boundary at all.
                    LOG_WARN("[GT_OSI:logical-lane] road {} section {} lane {} has no edge for one of its sides -- boundaries omitted",
                             road->GetId(),
                             j,
                             lane_id);
                }

                if (!street_name.empty())
                {
                    ll->set_street_name(street_name);
                }

                // TRAFFIC RULE -- only where something drives. A road-level speed
                // limit on a sidewalk or a median is noise.
                if (speed_limit_ms > SMALL_NUMBER && IsDrivingLikeType(rm_type))
                {
                    osi3::LogicalLane_TrafficRule* tr = ll->add_traffic_rule();
                    tr->set_traffic_rule_type(osi3::LogicalLane_TrafficRule_TrafficRuleType_TRAFFIC_RULE_TYPE_SPEED_LIMIT);
                    // OSI's TrafficSignValue::Unit has NO metres-per-second member
                    // for velocities -- only km/h and mph -- while GetSpeedByS()
                    // returns m/s (the parser normalises every authored unit into
                    // it). Emitting the raw number under a velocity unit would
                    // understate every limit by 3.6x.
                    tr->mutable_speed_limit()->mutable_speed_limit_value()->set_value(speed_limit_ms * 3.6);
                    tr->mutable_speed_limit()->mutable_speed_limit_value()->set_value_unit(
                        osi3::TrafficSignValue_Unit_UNIT_KILOMETER_PER_HOUR);
                    // Validity runs FROM start_s TOWARDS end_s in the direction of
                    // travel, so on a DECREASING_S lane the two are reversed. Left
                    // unset on both-ways lanes, where the rule simply applies to the
                    // whole lane in both directions.
                    if (ll->move_direction() == osi3::LogicalLane_MoveDirection_MOVE_DIRECTION_INCREASING_S)
                    {
                        tr->mutable_traffic_rule_validity()->set_start_s(start_s);
                        tr->mutable_traffic_rule_validity()->set_end_s(end_s);
                    }
                    else if (ll->move_direction() == osi3::LogicalLane_MoveDirection_MOVE_DIRECTION_DECREASING_S)
                    {
                        tr->mutable_traffic_rule_validity()->set_start_s(end_s);
                        tr->mutable_traffic_rule_validity()->set_end_s(start_s);
                    }
                }

                (*index)[LogicalLaneKey{road->GetId(), j, lane_id}] = ll->id().value();
                n_lanes++;
            }
        }
    }

    LOG_INFO(
        "[GT_OSI:logical-lane] post-pass enabled (S3) -- roads={} reference_line={} (points={} degenerate={}) logical_lane={} "
        "(connecting-road lanes={}) logical_lane_boundary={} (points={} split-by-roadmark={} split-by-height={} "
        "physical-linked={} physical-dropped={} without-physical={}) index={}",
        n_roads,
        gt->reference_line_size(),
        ref_line_points,
        degenerate_ref_lines,
        n_lanes,
        n_junction_lanes,
        n_boundaries,
        n_boundary_points,
        n_split_by_roadmark,
        n_split_by_height,
        n_physical_linked,
        n_physical_dropped,
        n_boundaries_without_physical,
        index->size());
}

void BuildOsiLogicalLanes(roadmanager::OpenDrive* opendrive)
{
    // Cleared unconditionally, before the flag check: a stale index from an
    // earlier load inside the same process must not survive into a run with the
    // feature OFF, where every lookup has to miss.
    g_logical_lane_index.clear();

    if (opendrive == nullptr || !GetUseOsiLogicalLane())
    {
        return;
    }

    if (obj_osi_internal.static_gt == nullptr)
    {
        // The reporter allocates static_gt in its constructor, so the real call site
        // can never see null. A unit test that pokes this function without a live
        // OSIReporter can, and must not crash on it.
        LOG_WARN("[GT_OSI:logical-lane] post-pass ran before the OSI reporter allocated its static GroundTruth -- skipped");
        return;
    }

    BuildOsiLogicalLanesInto(opendrive, obj_osi_internal.static_gt, &g_logical_lane_index);
}

// ---------------------------------------------------------------------------
// L1 / L1-b -- LogicalLaneAssignment (design 2-6-1)
// ---------------------------------------------------------------------------

bool ResolveOsiReferencePoint(const roadmanager::Position& pos, const ObjectBox& box, roadmanager::Position* out)
{
    if (out == nullptr)
    {
        return false;
    }

    // Duplicate first, unconditionally: the caller's fallback contract is "use pos",
    // and handing back a copy of pos in the short-circuit below means the caller can
    // read `out` the same way in every branch.
    //
    // Duplicate copies the road caches (track_idx_ / lane_section_idx_ / osi_point_idx_)
    // and deliberately does NOT copy route_, so the copy neither owns nor frees the
    // caller's route -- Position's destructor deletes route_, and a copy that shared it
    // would delete the simulation's own route when it went out of scope.
    out->Duplicate(pos);

    double dx = 0.0, dy = 0.0, dz = 0.0;
    RotateVec3d(pos.GetH(), pos.GetP(), pos.GetR(), box.center_x, box.center_y, box.center_z, dx, dy, dz);
    if (std::fabs(dx) < SMALL_NUMBER && std::fabs(dy) < SMALL_NUMBER)
    {
        // Origin IS the box centre (pedestrians, and any catalogue entry authored that
        // way). Returning pos's own s/t bit for bit is both cheaper and exact; a
        // round-trip through XYZ2TrackPos would re-derive them and could land a ULP off.
        return true;
    }

    // Pinned to the entity's own road (design 2-6-1): s/t have to be on the reference
    // line of the lane the assignment names. With a roadId given, XYZ2TrackPos looks at
    // that one road only, so this is also the cheapest form of the call.
    //
    // Z_ABS is forced rather than inherited: the mode the entity carries may be Z_REL,
    // under which XYZ2TrackPos reads the z argument as GetZ() + z and would double the
    // elevation. z only ever breaks ties between vertically stacked roads (the search
    // ignores differences below 2 m), and there is exactly one road in the search here,
    // so it cannot change the answer -- but a doubled z can, on a steep grade.
    const int mode = (pos.GetMode(roadmanager::Position::PosModeType::SET) & ~roadmanager::Position::PosMode::Z_MASK) |
                     roadmanager::Position::PosMode::Z_ABS;

    const roadmanager::Position::ReturnCode rc =
        out->XYZ2TrackPos(pos.GetX() + dx, pos.GetY() + dy, pos.GetZ() + dz, mode, false, pos.GetTrackId());
    if (static_cast<int>(rc) < 0 || out->GetTrackId() != pos.GetTrackId())
    {
        // Could not be placed on the entity's road at all. Restore the origin rather
        // than report ST from some other road's reference line.
        out->Duplicate(pos);
        return false;
    }
    return true;
}

std::vector<LogicalLaneAssignmentEntry> ComputeLogicalLaneAssignments(const roadmanager::Position& pos,
                                                                     const ObjectBox&             box,
                                                                     const LogicalLaneIndex&      index)
{
    std::vector<LogicalLaneAssignmentEntry> out;
    if (index.empty())
    {
        return out;  // feature OFF, or no static ground truth was ever built
    }

    roadmanager::Road* road = pos.GetRoadById(pos.GetTrackId());
    if (road == nullptr)
    {
        return out;  // off road: nothing to be assigned to
    }

    const double s       = pos.GetS();
    const idx_t  sec_idx = road->GetLaneSectionIdxByS(s);
    if (sec_idx == IDX_UNDEFINED)
    {
        return out;
    }
    roadmanager::LaneSection* lsec = road->GetLaneSectionByIdx(sec_idx);
    if (lsec == nullptr)
    {
        return out;
    }

    const double t = pos.GetT();
    // Position keeps h_relative_ in [0, 2pi). Every other angle this reporter
    // emits is wrapped to [-pi, pi] first (see the orientation block in
    // GT_OSIReporter_Moving.cpp), and leaving this one unwrapped would put a
    // 2pi step between a vehicle drifting a hair left of the lane direction and
    // one drifting a hair right -- exactly the comparison angle_to_lane is for.
    // Design 2-6-1 said GetHRelative() raw; corrected here, doc updated.
    const double h_rel = GetAngleInIntervalMinusPIPlusPI(pos.GetHRelative());

    // The lane area the body overlaps is around the BOX CENTRE, which sits
    // center_x ahead of the entity origin that s/t are taken from. Rotating that
    // offset by the heading relative to the road gives its lateral part; the
    // half-extent is the standard projection of an oriented box onto the t axis,
    // so a yawing vehicle reaches further sideways than its width alone.
    const double sin_h    = std::sin(h_rel);
    const double cos_h    = std::cos(h_rel);
    const double t_centre = t + box.center_x * sin_h + box.center_y * cos_h;
    const double half_t   = 0.5 * (std::fabs(box.length * sin_h) + std::fabs(box.width * cos_h));
    const double body_lo  = t_centre - half_t;
    const double body_hi  = t_centre + half_t;

    // Lane edges live in the same t frame as Position::GetT(), which is
    //   offset_ + road lane offset + signed centre offset   (GT_RoadManager.cpp)
    // so the road-level <laneOffset> has to be added back to the section-local,
    // always-positive accumulated widths that Get{Inner,Outer}Offset return.
    const double lane_offset     = road->GetLaneOffset(s);
    const int    anchor_lane_id  = pos.GetLaneId();
    bool         anchor_resolved = false;

    // WHAT IS EMITTED is measured at the OSI reference point -- the bounding-box
    // centre, which is what base.position carries and what the proto calls "the
    // object reference point". WHICH LANES ARE OVERLAPPED, and the lane section they
    // are looked up in, stay anchored on the entity ORIGIN above: that is where the
    // physical assigned_lane_id comes from, so keeping both faces on the same
    // Position is what makes entry [0] agree with it. Design 2-6-1.
    roadmanager::Position ref_pos;
    const bool            ref_ok = ResolveOsiReferencePoint(pos, box, &ref_pos);
    const double          ref_s  = ref_pos.GetS();
    const double          ref_t  = ref_pos.GetT();

    for (idx_t k = 0; k < lsec->GetNumberOfLanes(); k++)
    {
        roadmanager::Lane* lane = lsec->GetLaneByIdx(k);
        if (lane == nullptr || lane->IsCenter())
        {
            continue;  // the centre lane has no area, and no logical lane either
        }
        const int    lane_id  = lane->GetId();
        const double sign     = (lane_id < 0) ? -1.0 : 1.0;
        const double edge_in  = lane_offset + sign * lsec->GetInnerOffset(s, lane_id);
        const double edge_out = lane_offset + sign * lsec->GetOuterOffset(s, lane_id);
        const double lane_lo  = std::min(edge_in, edge_out);
        const double lane_hi  = std::max(edge_in, edge_out);

        const double overlap   = std::min(body_hi, lane_hi) - std::max(body_lo, lane_lo);
        const bool   is_anchor = (lane_id == anchor_lane_id);
        if (!is_anchor && !(overlap > kLogicalLaneOverlapThresholdM))
        {
            continue;
        }

        const auto it = index.find(LogicalLaneKey{road->GetId(), static_cast<unsigned>(sec_idx), lane_id});
        if (it == index.end())
        {
            continue;  // a section the post-pass skipped -- callers treat this as normal
        }

        LogicalLaneAssignmentEntry e;
        e.assigned_lane_id = it->second;
        // Same s/t/angle on every entry: one reference line per road (design 2-1),
        // and every overlapped lane is in this one lane section. The reference point
        // may well sit outside the lane named here, and outside its [start_s, end_s]
        // -- the proto says so in as many words: s_position "might be outside
        // [s_start,s_end] of the lane ... if the reference point is outside the lane,
        // but the object overlaps".
        e.s_position    = ref_s;
        e.t_position    = ref_t;
        e.angle_to_lane = h_rel;
        e.lane_id       = lane_id;
        e.overlap_m     = overlap;
        e.is_anchor     = is_anchor;
        e.ref_point_ok  = ref_ok;
        out.push_back(e);
        anchor_resolved = anchor_resolved || is_anchor;
    }

    if (anchor_resolved && out.size() > 1)
    {
        // Anchor first, everything else in lane-section order. Keeps entry [0] in
        // agreement with the physical assigned_lane_id for any consumer that only
        // reads one.
        std::stable_partition(out.begin(), out.end(), [](const LogicalLaneAssignmentEntry& e) { return e.is_anchor; });
    }
    return out;
}

void EmitLogicalLaneAssignment(osi3::MovingObject_MovingObjectClassification* classification,
                               const roadmanager::Position&                   pos,
                               const ObjectBox&                               box)
{
    if (classification == nullptr || !GetUseOsiLogicalLane())
    {
        return;
    }
    for (const LogicalLaneAssignmentEntry& e : ComputeLogicalLaneAssignments(pos, box, g_logical_lane_index))
    {
        osi3::LogicalLaneAssignment* a = classification->add_logical_lane_assignment();
        a->mutable_assigned_lane_id()->set_value(e.assigned_lane_id);
        a->set_s_position(e.s_position);
        a->set_t_position(e.t_position);
        a->set_angle_to_lane(e.angle_to_lane);
    }
}

}  // namespace osi
}  // namespace gt_esmini
