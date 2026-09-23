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
// ground-truth build. Empty while the feature is OFF, and empty in S0 (the
// post-pass emits nothing yet) -- callers must treat "not found" as normal.
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
// Hard no-op when the flag is OFF, and in S0 a no-op even when it is ON: the
// emit lands in S1 (reference lines + lane bodies), S2 (connectivity) and S3
// (boundaries).
void BuildOsiLogicalLanes(roadmanager::OpenDrive* opendrive);

// Read-only view of the index built by the last BuildOsiLogicalLanes() call.
// Lives in the same translation unit as the post-pass so nothing else has to
// own it. Empty until S1 populates it.
const LogicalLaneIndex& GetLogicalLaneIndex();

}  // namespace osi
}  // namespace gt_esmini
