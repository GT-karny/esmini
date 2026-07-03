// OdrSideExtras.hpp -- data structs for the GT OpenDRIVE side model.
//
// Plan: GT_esmini/docs/opendrive_16_19_support_plan.md (§3.1 OdrSideModel).
//
// P1 reserved these as empty placeholder skeletons; P2 fills OdrLaneExtras (lane types /
// 1.8 lane attributes / access / rule / speed / roadMark sway / borders). The remaining
// structs stay empty until their phases (P3/P4 signals, P5-P7 junctions, P9 railroad).
//
// Namespace: gt_esmini::odr
#pragma once

#include <string>
#include <vector>

namespace gt_esmini
{
namespace odr
{

// ---- P2 (cluster 16 L1) lane child records. All sOffset values are relative to the owning
// lane section start (as authored in the xodr). Raw strings are stored verbatim (L1 contract:
// parse + store + diagnose; no interpretation at storage time).
struct OdrLaneSpeed
{
    double      s_offset = 0.0;
    double      max      = 0.0;  // as authored (unit below)
    std::string unit;            // "" = m/s per ODR default; else "m/s" | "km/h" | "mph"
};

struct OdrLaneAccess
{
    double                   s_offset = 0.0;
    std::string              rule;          // 1.5+: "allow" | "deny" (may be empty pre-1.5 files)
    std::string              restriction;   // <=1.5 attribute form (access@restriction)
    std::vector<std::string> restrictions;  // 1.6+ child form (<restriction type="..."/>)
};

struct OdrLaneRule
{
    double      s_offset = 0.0;
    std::string value;  // free text, e.g. "no stopping"
};

struct OdrRoadMarkSway
{
    double ds = 0.0, a = 0.0, b = 0.0, c = 0.0, d = 0.0;
};

struct OdrLaneBorder
{
    double s_offset = 0.0, a = 0.0, b = 0.0, c = 0.0, d = 0.0;
};

// Per-lane extras beyond what upstream RoadManager stores. One entry per <lane> element
// (any side) that carries at least one P2 datum; plain driving lanes with none of these
// produce NO entry (keeps the side model sparse on legacy assets).
struct OdrLaneExtras
{
    // Key: (road_id, section_index, lane_id). section_s is the section start (absolute road s)
    // for sOffset resolution without re-walking the DOM.
    std::string road_id;            // road@id as authored (string ids are legal since 1.7)
    int         section_index = 0;  // ordinal of the laneSection within the road (DOM order)
    double      section_s     = 0.0;
    int         lane_id       = 0;
    std::string side;  // "left" | "center" | "right"

    // Exact lane@type source string (OSI subtype fidelity; the fork maps walking/curb/shared/
    // slipLane onto nearest existing enums -- see gt_roadmanager_patches.md [GT_ODR:lane-types]).
    std::string type_str;

    // 1.8 lane attributes (cluster 3 L1; @direction L2 is on the hold ledger). Raw values,
    // empty string = attribute absent.
    std::string direction;
    std::string advisory;
    std::string dynamic_lane_direction;
    std::string dynamic_lane_type;
    std::string road_works;

    // Cluster 16 L1 records.
    std::vector<OdrLaneSpeed>    speeds;
    std::vector<OdrLaneAccess>   accesses;
    std::vector<OdrLaneRule>     rules;
    std::vector<OdrRoadMarkSway> sways;    // union of this lane's roadMark <sway> records
    std::vector<OdrLaneBorder>   borders;  // source data for the P2 border->width normalization
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
