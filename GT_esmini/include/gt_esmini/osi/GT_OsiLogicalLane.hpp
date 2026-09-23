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

namespace roadmanager
{
    class OpenDrive;
}

// Forward declaration only: this header is included by GT_esminiLib-side consumers
// and by the unit gate binary, and neither should have to pull in protobuf just to
// name the output of the post-pass.
namespace osi3
{
    class GroundTruth;
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

// Flag gate for the logical-lane post-pass. Default OFF. Read ONCE from env
// GT_OSI_LOGICAL_LANE on first query ("1"/"true", case-insensitive -> ON;
// anything else / unset -> OFF). Same idiom as
// gt_esmini::odr::GetUseAuthoredJunctionBoundary(); the setter overrides the env
// read so unit tests are deterministic.
//
// The flag does NOT exist to protect goldens -- the 7 upstream byte-exact OSI
// size assertions it would have guarded are already skipped for GT
// (scripts/run_tests.sh, OSI 3.7.0 explicit presence). It exists to keep the
// incomplete intermediate model (S1..S2, logical lanes without boundaries) out
// of the default output, to keep ON/OFF bisectable inside one binary, and to
// leave the payload a choice. Design 7-2. It flips to default ON at S3.
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
// Hard no-op when the flag is OFF. As of S1 it emits reference_line[] and
// logical_lane[]; connectivity is S2 and logical_lane_boundary[] is S3.
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
