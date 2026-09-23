/*
 * GT_esmini extension -- OSI logical lanes (osi3 GroundTruth.reference_line /
 * logical_lane_boundary / logical_lane) and the index the HostVehicleData
 * route builder resolves ids through.
 *
 * Design: GT_esmini/docs/osi/logical_lane_and_route_design.md
 * Spec vs. current state: GT_esmini/docs/osi/logical_lane_and_route.md
 * Knowledge graph: spine-work:osi-logical-lane (capability_model.md 2.2a W4).
 *
 * WHY A FREE FUNCTION AND NOT AN OSIReporter MEMBER
 * -------------------------------------------------
 * OSIReporter.hpp is upstream (R1 Clean Core) and carries no GT members. The
 * post-pass therefore runs as a GT free function called from the GT fork of
 * OSIReporter.cpp, the same shape ApplyAuthoredJunctionBoundaries already has.
 * Its translation unit (GT_OSIReporter_LogicalLane.cpp) is compiled INTO the
 * upstream ScenarioEngine target via that module's existing GT swap block,
 * because it has to touch obj_osi_internal, which is defined there. It is NOT
 * part of lineage:gt_osireporter -- there is no upstream counterpart file, so
 * it never widens the inbound fork-sync diff.
 *
 * Namespace: gt_esmini::osi, mirroring gt_esmini::odr (the other GT module that
 * is compiled into an upstream target and consumed from a fork file).
 */
#pragma once

#include <cstdint>
#include <map>
#include <tuple>
#include <vector>

namespace roadmanager
{
    class OpenDrive;
    class Position;
}

// Forward declaration only: this header is included by GT_esminiLib-side consumers
// and by the unit gate binary, and neither should have to pull in protobuf just to
// name the output of the post-pass.
namespace osi3
{
    class GroundTruth;
    // Protobuf generates a nested message as a namespace-scope class named
    // <Parent>_<Nested>, so this names MovingObject::MovingObjectClassification.
    class MovingObject_MovingObjectClassification;
}

namespace gt_esmini
{
namespace osi
{

// Key of the one thing that ties the static logical-lane layer to the per-frame
// consumers (HVD route, LogicalLaneAssignment): the OpenDRIVE address of a lane.
//
//   <0> road id (roadmanager id_t == uint32_t)
//   <1> lane section index within that road
//   <2> OpenDRIVE lane id (signed, 0 == centre lane)
using LogicalLaneKey = std::tuple<std::uint32_t, unsigned, int>;

// (road, lane section, lane) -> osi3 LogicalLane global id.
//
// Populated by BuildOsiLogicalLanes() and rebuilt from scratch on every static
// ground-truth build. Empty while the feature is OFF, and a road the post-pass
// had to skip has no entry either -- callers must treat "not found" as normal.
using LogicalLaneIndex = std::map<LogicalLaneKey, std::uint64_t>;

// Flag gate for the logical-lane post-pass. DEFAULT ON since S3. Read ONCE from
// env GT_OSI_LOGICAL_LANE on first query: UNSET means on, and a set value is
// parsed as "1"/"true"/"yes"/"on" -> ON, anything else -> OFF. So the variable is
// an OPT-OUT (`GT_OSI_LOGICAL_LANE=0`), not an opt-in. The setter overrides the env
// read so unit tests are deterministic.
//
// It differs from gt_esmini::odr::GetUseAuthoredJunctionBoundary() in exactly that
// respect, on purpose: that one is opt-in, so "unset -> off" is right there and
// wrong here.
//
// The flag does NOT exist to protect goldens -- the 7 upstream byte-exact OSI
// size assertions it would have guarded are already skipped for GT
// (scripts/run_tests.sh, OSI 3.7.0 explicit presence). What is left of its purpose
// after S3 is keeping ON/OFF bisectable inside one binary and leaving the payload a
// choice (measured 1.26x..2.85x of the static ground truth, design 7-3). The third
// reason it had -- keeping an incomplete intermediate model out of the default
// output -- expired when the boundaries landed.
void SetUseOsiLogicalLane(bool on);
bool GetUseOsiLogicalLane();

// Static ground-truth post-pass: emit reference_line[] / logical_lane_boundary[]
// / logical_lane[] and fill the index.
//
// Called once per OpenDRIVE load, from the GT fork of
// OSIReporter::CreateOSIStaticGroundTruthFromODR(), AFTER the physical lane /
// boundary / intersection passes and after ApplyAuthoredJunctionBoundaries().
// That ordering is the id-numbering discipline of design 3: every existing RM
// and OSI global id is already assigned by then, so the fresh GetNewGlobalId()
// values this pass draws cannot move any of them. Drawing them during OpenDRIVE
// load instead would renumber every lane and break both the ODR conformance OSI
// goldens and the lane_map join behind signal:ego_lane.
//
// Hard no-op when the flag is OFF. Emits reference_line[], logical_lane[] and
// logical_lane_boundary[], with predecessor / successor / left / right adjacency
// resolved in a final pass once every id exists (design 2-4).
void BuildOsiLogicalLanes(roadmanager::OpenDrive* opendrive);

// The pass itself, with both outputs passed in explicitly.
//
// BuildOsiLogicalLanes() is the wiring: it resolves the reporter's static
// GroundTruth and the module-level index and calls this. Tests call this
// directly with their own GroundTruth, which is the only way to exercise the
// emit without standing up an OSIReporter (the reporter allocates
// obj_osi_internal.static_gt in its constructor -- see the null guard in the
// wrapper).
//
// Does NOT check the feature flag: the flag is a property of the call site, and
// a test that has asked for a build wants one. Both arguments are required.
void BuildOsiLogicalLanesInto(roadmanager::OpenDrive* opendrive, osi3::GroundTruth* gt, LogicalLaneIndex* index);

// Read-only view of the index built by the last BuildOsiLogicalLanes() call.
// Lives in the same translation unit as the post-pass so nothing else has to
// own it. This is the ONLY coupling between the static logical-lane layer and
// the per-frame consumers (S2.5 assignment, S4 route).
const LogicalLaneIndex& GetLogicalLaneIndex();

// ---------------------------------------------------------------------------
// L1 / L1-b -- LogicalLaneAssignment (design 2-6-1)
// ---------------------------------------------------------------------------

// The entity's bounding box, flattened to the four numbers the lateral overlap
// test needs. scenarioengine::OSCBoundingBox itself is an anonymous-struct
// typedef, so it can be neither forward declared nor named here without pulling
// ScenarioEngine into a header that GT_esminiLib and the unit gate both include.
//
//   length / width     extents along the entity's own x / y axes [m]
//   center_x/_y/_z     offset from the entity ORIGIN to the box centre, entity
//                      frame [m]. esmini positions a vehicle by its origin (rear
//                      axle for the shipped catalogue), while the box -- and hence
//                      both the lane area it overlaps and the OSI reference point
//                      -- sits center_x ahead of it.
struct ObjectBox
{
    double length   = 0.0;
    double width    = 0.0;
    double center_x = 0.0;
    double center_y = 0.0;
    double center_z = 0.0;
};

// THE OSI REFERENCE POINT, RESOLVED ONTO THE ROAD
// -----------------------------------------------
// osi_common.proto defines BaseMoving.position as "the center (x,y,z) of the
// bounding box", and LogicalLaneAssignment.s_position / t_position as the ST
// coordinates of "the object reference point". For a MovingObject those are the
// same point, so everything GT reports about where an entity is along a lane or a
// route has to be measured at the box centre -- not at the entity origin, which is
// the rear axle for the shipped catalogue and sits 1.4 m behind it. The same
// convention is already pinned for ego_planned_path (signal_catalog.yaml: "base.position
// と同じ bbox 中心", i.e. Position::GetOsiX/Y/Z = origin + R(h,p,r) * bbox centre).
//
// `out` is overwritten with a Position at that point. It is a DUPLICATE of `pos`,
// which is what keeps the XY -> road search local (the copy carries track_idx_ /
// lane_section_idx_ / osi_point_idx_), and the search is additionally pinned to
// pos.GetTrackId(): the reference point must be expressed on the reference line of
// the lane it is reported against, so letting it snap freely to the next road would
// silently change coordinate frames mid-message. A centre that has already passed
// the end of the road therefore reads as that road's end s rather than as a
// negative s on the next one -- bounded by the centre offset, and only for the
// frame or two a road transition lasts (design 10-15).
//
// Position::Duplicate does not copy route_, so `out` never owns or frees the
// caller's route. Returns false when the point could not be resolved, in which case
// the caller should fall back to `pos` itself.
bool ResolveOsiReferencePoint(const roadmanager::Position& pos, const ObjectBox& box, roadmanager::Position* out);

// One osi3::LogicalLaneAssignment, plus four numbers that are NOT emitted and
// exist so tests and probes can say why an entry is present.
struct LogicalLaneAssignmentEntry
{
    std::uint64_t assigned_lane_id = 0;
    // ST of the OSI REFERENCE POINT (bounding-box centre), not of the entity
    // origin -- see ResolveOsiReferencePoint above.
    double s_position    = 0.0;
    double t_position    = 0.0;
    double angle_to_lane = 0.0;
    // diagnostics ------------------------------------------------------------
    int    lane_id       = 0;      // OpenDRIVE lane id this entry resolved from
    double overlap_m     = 0.0;    // lateral overlap between box and lane [m]
    bool   is_anchor     = false;  // lane that Position itself reports the object on
    bool   ref_point_ok  = false;  // false -> s/t fell back to the entity origin
};

// osi_common.proto: "any object overlapping the lane more than 5cm has to be
// assigned to the lane". Strictly greater, as written.
constexpr double kLogicalLaneOverlapThresholdM = 0.05;

// Which logical lanes does this object overlap, and where is it in their ST
// frame? Pure: reads Position and the index, writes nothing, touches no protobuf.
//
// The anchor lane -- the one Position::GetLaneId() reports, i.e. the same lane
// the physical assigned_lane_id is derived from -- is ALWAYS first in the result
// and always present (when it is in the index), whatever its overlap. Parity with
// the physical face matters more here than the 5cm rule: an object whose origin
// sits in a lane is assigned to it, and a consumer reading entry [0] gets the
// same lane on both faces.
//
// Every entry carries the SAME s/t/angle, because every lane of a road shares one
// reference line (design 2-1) and all overlapped lanes live in one lane section.
//
// TWO POINTS, ON PURPOSE. The emitted s/t are the OSI reference point's
// (ResolveOsiReferencePoint), because that is what the proto asks for. Which lanes
// are overlapped, and which lane section they are looked up in, are still decided
// from the entity ORIGIN's s/t: that keeps entry [0] and the lane section in
// agreement with the physical assigned_lane_id, which is derived from the same
// Position, and it leaves the 5 cm overlap predicate exactly as S2.5 validated it.
// The proto anticipates the two diverging -- s_position "might be outside
// [s_start,s_end] of the lane ... if the reference point is outside the lane".
//
// Returns empty when the object is off-road, when the index is empty (feature
// OFF), or when the lane section is degenerate.
std::vector<LogicalLaneAssignmentEntry> ComputeLogicalLaneAssignments(const roadmanager::Position& pos,
                                                                     const ObjectBox&             box,
                                                                     const LogicalLaneIndex&      index);

// Fill moving_object_classification.logical_lane_assignment[] for one object,
// from the index of the last static ground-truth build.
//
// Hard no-op when the feature flag is OFF -- the flag is checked HERE and not at
// the call site, so the fork file GT_OSIReporter_Moving.cpp carries only the call.
void EmitLogicalLaneAssignment(osi3::MovingObject_MovingObjectClassification* classification,
                               const roadmanager::Position&                   pos,
                               const ObjectBox&                               box);

// ---- pure field mappings (design 2-2-1 / 2-2-2) ----
//
// Both return the numeric value of an osi3 enumerator rather than the enum type,
// so that this header stays protobuf-free. Call sites that care compare against
// static_cast<int>(osi3::LogicalLane_Type_TYPE_...).

// roadmanager::Lane::LaneType (a bit flag) -> osi3::LogicalLane::Type.
// Unmapped / unknown types collapse to TYPE_OTHER; the centre lane never reaches
// here because it is not emitted at all.
int MapLaneTypeToLogicalLaneType(int rm_lane_type);

// -> osi3::LogicalLane::MoveDirection, from the lane's OpenDRIVE side and the
// road's traffic hand. Non-driving lanes (sidewalk, median, curb, ...) and
// bidirectional lanes are BOTH_ALLOWED; driving lanes get INCREASING_S when the
// lane's driving direction follows the reference line. Same predicate the
// physical lane's centerline_is_driving_direction uses.
int MapMoveDirection(int rm_lane_type, int lane_id, bool right_hand_traffic);

}  // namespace osi
}  // namespace gt_esmini
