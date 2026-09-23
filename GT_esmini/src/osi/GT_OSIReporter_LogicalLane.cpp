/*
 * GT_esmini extension -- OSI logical lane post-pass.
 *
 * See gt_esmini/osi/GT_OsiLogicalLane.hpp for why this translation unit lives in
 * the ScenarioEngine swap zone rather than in GT_esminiLib, and why the pass runs
 * last in the static ground-truth build.
 *
 * STAGE: S2.5 -- adds LogicalLaneAssignment (design 2-6-1) on top of S1.
 *
 * S1 -- reference lines (design 2-1), logical lane bodies (2-2) and the
 * (road, lane section, lane) -> logical lane id index (3). Connectivity
 * (predecessor / successor / adjacent) is S2 and boundaries are S3, so
 * left_boundary_id / right_boundary_id are deliberately left empty here: they are
 * `repeated`, so the message stays well-formed, just not yet complete enough for
 * a strict OSI validator. That is exactly what the default-OFF flag is for
 * (design 7-2 reason 1).
 *
 * Design: GT_esmini/docs/osi/logical_lane_and_route_design.md 2-1 / 2-2 / 3 / 4 / 7
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
#include <string>
#include <vector>

namespace gt_esmini
{
namespace osi
{

namespace
{
// ---- feature flag (gt_esmini::odr::GetUseAuthoredJunctionBoundary idiom:
//      env read once on first query, setter overrides so tests are deterministic) ----
bool g_use_logical_lane        = false;
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
        g_use_logical_lane        = EnvIsTruthy(std::getenv("GT_OSI_LOGICAL_LANE"));
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

    // ---- pass 3: LogicalLane bodies (design 2-2). No connectivity, no boundaries.
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
        "[GT_OSI:logical-lane] post-pass enabled (S1) -- roads={} reference_line={} (points={} degenerate={}) logical_lane={} "
        "(connecting-road lanes={}) index={}",
        n_roads,
        gt->reference_line_size(),
        ref_line_points,
        degenerate_ref_lines,
        n_lanes,
        n_junction_lanes,
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
        // and every overlapped lane is in this one lane section. The proto expects
        // exactly that -- s_position "might be outside [s_start,s_end] of the lane
        // ... if the reference point is outside the lane, but the object overlaps".
        e.s_position    = s;
        e.t_position    = t;
        e.angle_to_lane = h_rel;
        e.lane_id       = lane_id;
        e.overlap_m     = overlap;
        e.is_anchor     = is_anchor;
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
