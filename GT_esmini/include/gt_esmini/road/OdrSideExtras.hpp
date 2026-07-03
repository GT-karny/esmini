// OdrSideExtras.hpp -- data structs for the GT OpenDRIVE side model.
//
// Plan: GT_esmini/docs/opendrive_16_19_support_plan.md (§3.1 OdrSideModel).
//
// P1 reserved these as empty placeholder skeletons; P2 fills OdrLaneExtras (lane types /
// 1.8 lane attributes / access / rule / speed / roadMark sway / borders); P3 fills
// OdrSignalExtras (cluster 12 L1: signal <dependency> / <reference>). The remaining
// structs stay empty until their phases (P4 signal semantics/VMS, P5-P7 junctions, P9 railroad).
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

// <signal>/<dependency>: this signal controls the state of the referenced signal (id), with an
// optional free-text type. L1 storage only (plan cluster 12; runtime semantics deferred).
struct OdrSignalDependency
{
    std::string id;    // @id -- the CONTROLLED signal's id
    std::string type;  // @type (optional)
};

// <signal>/<reference>: link between this signal and another signal or object (e.g. a static
// board that belongs to a signal). L1 storage only.
struct OdrSignalReferenceLink
{
    std::string element_type;  // @elementType: "object" | "signal"
    std::string element_id;    // @elementId
    std::string type;          // @type (optional)
};

// ---- P4 (clusters 10/13/14 L1): signal <semantics> + VMS boards + header regulations ----
//
// Speed / distance / time semantics share a common shape (a typed value with a unit). All values
// are kept as AUTHORED STRINGS for losslessness (L1 contract) with a parsed convenience double.
//
// SPEED NORMALIZATION: OpenDRIVE 1.8.1 and 1.9 both author semantics <speed> in ATTRIBUTE form
// (<speed type="maximum" value="60" unit="km/h"/>). To be robust against an earlier draft's
// hypothesised CHILD-element form (<speed><maximum value="60" unit="km/h"/></speed>), the parser
// ALSO accepts a single typed child element and normalizes it into the same OdrSemanticSpeed
// (child tag -> @type, child @value/@unit -> @value/@unit). Both forms therefore round-trip to a
// byte-identical OdrSemanticSpeed (see OdrSignalSemantics.cpp NormalizeSpeed + the unit test).

// One <speed> semantic: e_signals_semantics_speed type (maximum|minimum|recommended|zone + *End),
// double value, e_unitSpeed unit (m/s|mph|km/h). value_str kept verbatim; value is its parse.
struct OdrSemanticSpeed
{
    std::string type;       // @type (or the normalized child tag name)
    std::string value_str;  // @value verbatim ("" if absent)
    double      value = 0.0;
    std::string unit;       // @unit ("" = unspecified)
};

// One <supplementaryDistance>: e_signals_semantics_supplementaryDistance type (for|in), value,
// e_unitDistance unit (m|km|ft|mile).
struct OdrSemanticDistance
{
    std::string type;
    std::string value_str;
    double      value = 0.0;
    std::string unit;
};

// One <supplementaryTime>: e_signals_semantics_supplementaryTime type (day|time), value (no unit).
struct OdrSemanticTime
{
    std::string type;
    std::string value_str;
    double      value = 0.0;
};

// A traffic participant referenced by a semantic subtype (prohibited / supplementaryAllows /
// supplementaryProhibits). kind is the child tag ("vehicle"|"person"|"animal"); category is the
// participant's <type> child text (e_vehicleCategory / e_personCategory; empty for animal).
struct OdrSemanticParticipant
{
    std::string kind;      // "vehicle" | "person" | "animal"
    std::string category;  // <type> child value (e_vehicleCategory / e_personCategory); "" for animal
};

// The <semantics> block of a signal (or a header road/signal regulation). Every subtype list is
// sparse -- only populated subtypes carry entries. Simple typed subtypes store their @type string;
// participant-bearing subtypes store their participant lists; free subtypes (parking/routing/
// streetname/tourist/warning/supplementaryExplanatory) are counted by presence.
struct OdrSemantics
{
    std::vector<OdrSemanticSpeed>       speeds;                    // <speed>
    std::vector<std::string>            lane_types;                // <lane @type>
    std::vector<std::string>            priority_types;            // <priority @type>
    std::vector<OdrSemanticParticipant> prohibited;                // <prohibited>/{vehicle,person,animal}
    int                                 warning_count       = 0;   // <warning>
    int                                 routing_count       = 0;   // <routing>
    int                                 streetname_count    = 0;   // <streetname>
    int                                 parking_count       = 0;   // <parking>
    int                                 tourist_count       = 0;   // <tourist>
    std::vector<OdrSemanticTime>        supplementary_time;        // <supplementaryTime>
    std::vector<OdrSemanticParticipant> supplementary_allows;      // <supplementaryAllows>/{...}
    std::vector<OdrSemanticParticipant> supplementary_prohibits;   // <supplementaryProhibits>/{...}
    std::vector<OdrSemanticDistance>    supplementary_distance;    // <supplementaryDistance>
    std::vector<std::string>            supplementary_environment; // <supplementaryEnvironment @type>
    int                                 supplementary_explanatory_count = 0;  // <supplementaryExplanatory>

    // True if the block carries no populated subtype (used to keep sparse storage: an empty
    // <semantics/> still yields an entry to be lossless, but see IsEmpty() callers).
    bool IsEmpty() const
    {
        return speeds.empty() && lane_types.empty() && priority_types.empty() && prohibited.empty() &&
               warning_count == 0 && routing_count == 0 && streetname_count == 0 && parking_count == 0 &&
               tourist_count == 0 && supplementary_time.empty() && supplementary_allows.empty() &&
               supplementary_prohibits.empty() && supplementary_distance.empty() &&
               supplementary_environment.empty() && supplementary_explanatory_count == 0;
    }
};

// <displayArea> child of a <vmsBoard>: recommended visualization rectangle in board-local coords.
struct OdrDisplayArea
{
    std::string index;   // @index (verbatim; xs:int)
    std::string width;   // @width  (xs:string per XSD)
    std::string height;  // @height (xs:string per XSD)
    std::string v;       // @v (board-local)
    std::string z;       // @z (board-local)
};

// A <sign> on a static board (t_road_signals_board_sign): board-local @v/@z placement.
struct OdrBoardSign
{
    std::string v;  // @v (board-local)
    std::string z;  // @z
};

// A <staticBoard> child of a <signal> (t_road_signals_staticBoard; extends t_road_signals_board,
// which extends _OpenDriveElement -> NO scalar board attributes). Carries <sign> children.
struct OdrStaticBoard
{
    std::vector<OdrBoardSign> signs;
};

// A <vmsBoard> child of a <signal> (variable message board), 1.8 + 1.9 (same placement: child of
// <signal>). t_road_signals_vmsBoard extends t_road_signals_board and adds displayType/
// displayHeight/displayWidth/v/z + <displayArea> children.
struct OdrVmsBoard
{
    std::string display_type;    // @displayType (e_road_signals_displayType)
    std::string display_width;   // @displayWidth
    std::string display_height;  // @displayHeight
    std::string v;               // @v
    std::string z;               // @z
    std::vector<OdrDisplayArea> display_areas;
};

// Per-signal extras (P3 cluster 12 L1 + P4 clusters 10/13 L1): children of one <signal>.
// One entry per signal that HAS at least one such child (signals without them get no entry).
// positionRoad/positionInertial are consumed directly by [GT_ODR:sig-pos] (ResolveSignalPose)
// and road-level <signalReference> is materialized by [GT_ODR:sig-ref]; neither needs storage.
// temporary/invalidated flags land here in P8.
struct OdrSignalExtras
{
    std::string road_id;    // owning <road>@id
    std::string signal_id;  // <signal>@id

    std::vector<OdrSignalDependency>    dependencies;  // P3
    std::vector<OdrSignalReferenceLink> references;    // P3

    // P4: at most one <semantics> per signal (XSD maxOccurs=1). has_semantics distinguishes an
    // authored empty <semantics/> (true, empty block) from no <semantics> at all (false).
    bool         has_semantics = false;
    OdrSemantics semantics;

    std::vector<OdrStaticBoard> static_boards;  // P4 cluster 13 (1.9 <staticBoard>)
    std::vector<OdrVmsBoard>    vms_boards;      // P4 cluster 13 (1.8+1.9 <vmsBoard>)

    // True when this signal carries any P4 datum (drives sparse storage together with P3 children).
    bool HasAnyP4() const
    {
        return has_semantics || !static_boards.empty() || !vms_boards.empty();
    }
};

// ---- P4 cluster 13: document-level <vmsGroup> (OpenDRIVE root child in 1.9; groups vmsBoards on
// one gantry). One entry per <vmsGroup>; its <vmsBoardReference> children link (signalId, vmsIndex,
// groupIndex) triples.
struct OdrVmsBoardReference
{
    std::string signal_id;    // @signalId
    std::string vms_index;    // @vmsIndex (verbatim; xs:int)
    std::string group_index;  // @groupIndex
};

struct OdrVmsGroup
{
    std::string                       id;  // @id
    std::vector<OdrVmsBoardReference> board_references;
};

// ---- P4 cluster 14: header/license + header/defaultRegulations ----
struct OdrLicense
{
    std::string name;     // @name  (full license name; informational)
    std::string spdxid;   // @spdxid (SPDX id / expression)
    std::string text;     // @text  (full license text)
    std::string resource; // @resource (URL)
};

// One <roadRegulations>/<signalRegulations> entry under <defaultRegulations>. is_signal selects
// which; road form has @type (e_roadType), signal form has @type/@subtype (both xs:string). Each
// reuses the <semantics> content model.
struct OdrDefaultRegulation
{
    bool         is_signal = false;  // false=roadRegulations, true=signalRegulations
    std::string  type;               // @type
    std::string  subtype;            // @subtype (signalRegulations only)
    bool         has_semantics = false;
    OdrSemantics semantics;
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
