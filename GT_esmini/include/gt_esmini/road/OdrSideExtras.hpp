// OdrSideExtras.hpp -- architecture placeholder skeletons for the GT OpenDRIVE side model.
//
// Plan: GT_esmini/docs/opendrive_16_19_support_plan.md (§3.1 OdrSideModel).
//
// These are intentionally EMPTY structs in phase P1: they reserve the shape of the side model
// so later phases can fill them in without touching the public header contract. NO logic here.
//
// Namespace: gt_esmini::odr
#pragma once

namespace gt_esmini
{
namespace odr
{

// Per-lane extras beyond what upstream RoadManager stores (walking/curb/shared/slipLane types,
// lane attributes direction/advisory/dynamic*/roadWorks, access/rule/speed/border/sway,
// lane layers). Populated in P2/P8.
struct OdrLaneExtras
{
};

// Per-signal extras: semantics family + participants, boards/VMS, positionRoad/Inertial,
// signalReference, dependency, temporary/invalidated flags. Populated in P3/P4.
struct OdrSignalExtras
{
};

// Per-junction extras: crossing junction + crossPath (pedestrian crossing), virtual junction,
// junction priority + laneLink overlapZone, boundary/elevationGrid, junctionGroup. Populated in
// P5 (crossPath/priority) and P6/P7.
struct OdrJunctionExtras
{
};

// Railroad + station family (switch/mainTrack/sideTrack/partner, station/platform/segment).
// L1-only, documented inactive. Populated in P9.
struct OdrRailroad
{
};

}  // namespace odr
}  // namespace gt_esmini
