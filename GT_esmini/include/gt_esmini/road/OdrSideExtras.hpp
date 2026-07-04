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

// ---- P8 (cluster 22 L1): lane <link>/<predecessor|successor> 1.9 @layer. One entry per lane link
// element that carries @layer (sparse -- absent @layer produces no entry). link_dir names which link
// direction ("predecessor" | "successor"), id is the linked lane id (verbatim string), layer is the
// authored @layer token ("permanent" | "temporary"). ----
struct OdrLaneLinkLayer
{
    std::string link_dir;  // "predecessor" | "successor"
    std::string id;        // linked lane @id (verbatim)
    std::string layer;     // @layer ("permanent" | "temporary")
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

    // P8 cluster 22 L1: lane <link>/<predecessor|successor> 1.9 @layer records (sparse -- only lane
    // link elements carrying @layer). Counts as "has P2/P8 data" in ReadLaneNode's sparse gate.
    std::vector<OdrLaneLinkLayer> link_layers;
};

// ---- P8 (cluster 22 L1): a <validity> record carrying the 1.9 @layer. Sparse -- one entry per
// <validity> element (on a signal or object) that authored @layer; validity elements without @layer
// produce no entry. from_lane/to_lane are the (verbatim) lane subset the validity applies to. ----
struct OdrValidityLayer
{
    std::string from_lane;  // @fromLane (verbatim)
    std::string to_lane;    // @toLane (verbatim)
    std::string layer;      // @layer ("permanent" | "temporary")
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

    // P8 (1.9): plain xs:boolean flags. temporary = a temporary (e.g. roadworks) signal; invalidated
    // = the regulation is cancelled/crossed-out (excluded from the OSI logical ground truth). Both
    // *_present record whether the attribute was authored (sparse gate: an authored flag makes the
    // signal carry P8 data even with no P3/P4 children).
    bool temporary            = false;
    bool temporary_present    = false;  // @temporary authored?
    bool invalidated          = false;
    bool invalidated_present  = false;  // @invalidated authored?

    // P8 cluster 22 L1: <validity> records that authored @layer (sparse).
    std::vector<OdrValidityLayer> validity_layers;

    // True when this signal carries any P4 datum (drives sparse storage together with P3 children).
    bool HasAnyP4() const
    {
        return has_semantics || !static_boards.empty() || !vms_boards.empty();
    }

    // True when this signal carries any P8 datum (flags or a validity @layer).
    bool HasAnyP8() const
    {
        return temporary_present || invalidated_present || !validity_layers.empty();
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

// A crossPath lane link (<startLaneLink>/<endLaneLink>). Per OpenDRIVE_Junction.xsd
// (t_junction_crossPath_laneLink): @s is the "s-coordinate of either start or end point in linked
// road" -- the LINKED road being @roadAtStart (startLaneLink) / @roadAtEnd (endLaneLink), i.e. the
// CROSSED road, NOT the crossing road. @from is the lane id on that linked (crossed) road; @to is
// the lane id on @crossingRoad. L1 raw storage.
struct OdrCrossPathLaneLink
{
    double s    = 0.0;  // @s -- position on the LINKED (crossed) road (roadAtStart / roadAtEnd)
    int    from = 0;    // @from lane id (on the linked/crossed road)
    int    to   = 0;    // @to lane id (on @crossingRoad)
};

// One sample of a synthesized pedestrian-path polyline: the crossingRoad centerline sampled across
// the crossing span. World coordinates + the road-frame s on the crossing road. L1/L2 storage for
// future policies (no consumer yet). Populated by the P5 stage-2 crosswalk synthesis.
struct OdrPedPathSample
{
    double s = 0.0;  // road-frame s on the crossing road
    double x = 0.0;  // world x
    double y = 0.0;  // world y
    double z = 0.0;  // world z
};

// <junction>/<crossPath> (1.8/1.9 pedestrian crossing carried by a common/virtual junction). L1
// storage only (crossPath -> CROSSWALK object synthesis is P5 stage 2; semantics stay deferred).
struct OdrCrossPath
{
    std::string          id;             // @id
    std::string          crossing_road;  // @crossingRoad -- the road that crosses
    std::string          road_at_start;  // @roadAtStart
    std::string          road_at_end;    // @roadAtEnd
    OdrCrossPathLaneLink start_lane_link;
    OdrCrossPathLaneLink end_lane_link;

    // ---- P5 stage 2 synthesis products (filled by SynthesizeCrosswalks, empty until then) ----
    // Sampled crossing-road centerline across the crossing span (world x/y/z + crossing-road s).
    // L1/L2 storage for future pedestrian policies; no runtime consumer yet.
    std::vector<OdrPedPathSample> ped_path;

    // The synthetic CROSSWALK RMObject id assigned to this crossPath, or 0 when no object was
    // synthesized (unresolvable crossing road / id collision / degenerate geometry). Diagnostic
    // handle only (the object itself lives on the crossed Road via Road::AddObject).
    unsigned int synth_object_id = 0;  // 0 = none
};

// <junction>/<roadSection> (1.8 crossing-junction: the s-range of a road where crossing traffic can
// appear). L1 storage only.
struct OdrJunctionRoadSection
{
    std::string id;       // @id
    std::string road_id;  // @roadId
    double      s_start = 0.0;
    double      s_end   = 0.0;
};

// <junction>/<priority> (STANDARD since <=1.5; the canonical priority source for feature F3). XSD
// allows MULTIPLE <priority> per junction, so these are stored as a list. Raw @high/@low strings.
struct OdrJunctionPriority
{
    std::string high;  // @high -- the higher-priority connecting road id
    std::string low;   // @low  -- the lower-priority connecting road id
};

// <junction>/<controller> (junction-scoped controller). L1 duplicate of the fork parse for side
// completeness (the fork already stores these on Junction; this mirrors them for the side model).
struct OdrJunctionController
{
    std::string id;        // @id
    std::string type;      // @type
    int         sequence = 0;  // @sequence
};

// <junction>/<connection>/<laneLink> 1.8/1.9 layer attributes (cluster 22 L1 slot reservation;
// semantics deferred to P8). Keyed by owning connection id + the laneLink's from/to lane ids so an
// F-week consumer can correlate against the fork-parsed Connection lane links. Raw strings ("" =
// attribute absent).
struct OdrLaneLinkExtras
{
    std::string connection_id;  // owning <connection>@id
    int         from = 0;       // <laneLink>@from
    int         to   = 0;       // <laneLink>@to
    std::string overlap_zone;   // @overlapZone (1.8)
    std::string from_layer;     // @fromLayer (1.9)
    std::string to_layer;       // @toLayer (1.9)
};

// Per-junction extras: crossing junction + crossPath (pedestrian crossing), virtual junction,
// junction priority + laneLink overlapZone/layer, controllers. Populated in P5 (crossPath /
// roadSection / priority / laneLink layer L1) and extended in P6/P7. Sparse: one entry per junction
// that carries at least one of these extras (plain junctions with none produce NO entry).
struct OdrJunctionExtras
{
    std::string junction_id;  // <junction>@id as authored
    std::string type_str;     // <junction>@type ("" = default/common, "virtual", "crossing", "direct")

    std::vector<OdrCrossPath>           cross_paths;
    std::vector<OdrJunctionRoadSection> road_sections;
    std::vector<OdrJunctionPriority>    priorities;   // XSD allows multiple
    std::vector<OdrJunctionController>  controllers;  // L1 duplicate of fork parse
    std::vector<OdrLaneLinkExtras>      lane_link_extras;  // cluster 22 L1 slot reservation
};

// Railroad + station family (switch/mainTrack/sideTrack/partner, station/platform/segment).
// L1-only, documented inactive. Populated in P9.
struct OdrRailroad
{
};

// ===========================================================================
// P7 (clusters 8/9/17/18/19): object-family L1 + lateralProfile shape/crossSectionSurface +
// surface/CRG + junction geometry (boundary/elevationGrid/junctionGroup). All L1 (parse + store +
// diagnose); no interpretation at storage time. Raw strings kept verbatim per the L1 contract, with
// parsed convenience doubles where a downstream WP needs the numeric value.
// ===========================================================================

// ---- cluster 18: <surface><CRG> (road-level, object-level, junction-level). All attrs incl. the
// 1.9 additions @xOffset/@yOffset. Raw string + parsed convenience double so the later OSI/eval WP
// need not re-parse. No evaluation of any kind (CRG is stored L1 only, never evaluated).
struct OdrCrgRecord
{
    std::string file;         // @file (path, resolved best-effort for the existence diagnostic)
    std::string s_start;      // @sStart
    std::string s_end;        // @sEnd
    std::string orientation;  // @orientation (same|opposite)
    std::string mode;         // @mode (attached|attached0|genuine|global)
    std::string purpose;      // @purpose (elevation|friction)
    double      s_offset = 0.0;  // @sOffset
    double      t_offset = 0.0;  // @tOffset
    double      x_offset = 0.0;  // @xOffset (1.9)
    double      y_offset = 0.0;  // @yOffset (1.9)
    double      z_offset = 0.0;  // @zOffset
    double      z_scale  = 1.0;  // @zScale (default 1)
    double      h_offset = 0.0;  // @hOffset
    bool        file_exists    = false;  // resolved existence (best-effort; false when unresolved)
    bool        file_checked   = false;  // whether an existence check was attempted
};

// ---- cluster 19: object <material> (t_road_objects_object_material). All attrs verbatim. ----
struct OdrObjectMaterial
{
    std::string surface;          // @surface
    std::string friction;         // @friction (verbatim; xs:double)
    std::string roughness;        // @roughness
    std::string road_mark_color;  // @roadMarkColor (1.9)
};

// ---- cluster 19: outline-level <markings>/<marking> (1.9 idiom used by ASAM Ex_Objects). L1 raw:
// the marking's own attrs + its <cornerReference @id> members. ----
struct OdrObjectMarking
{
    std::string width;         // @width
    std::string color;         // @color
    std::string z_offset;      // @zOffset
    std::string space_length;  // @spaceLength
    std::string line_length;   // @lineLength
    std::string start_offset;  // @startOffset
    std::string stop_offset;   // @stopOffset
    std::string side;          // @side (optional)
    std::string weight;        // @weight (optional)
    std::vector<std::string> corner_reference_ids;  // <cornerReference @id> members
};

// ---- cluster 19: object <outline> L1 (attrs + outline-level markings). Covers BOTH the singular
// (object/outline) and plural (object/outlines/outline) forms (the fixtures define both as ground
// truth). Corner geometry itself is parsed by upstream RM (cornerRoad/cornerLocal) and by the
// AppendCurveLocalCorners fork helper (curveLocal); here we only store the outline attributes upstream
// drops (@fillType/@laneType/@outer) plus the 1.9 outline-level <markings>. ----
struct OdrObjectOutline
{
    std::string id;         // @id
    std::string fill_type;  // @fillType
    std::string lane_type;  // @laneType
    std::string outer;      // @outer
    std::string closed;     // @closed
    bool        singular_form = false;  // true = object/outline; false = object/outlines/outline
    std::vector<OdrObjectMarking> markings;  // outline-level <markings>/<marking> (1.9)
};

// ---- cluster 19: object <skeleton> polyline vertex (t_road_objects_object_skeleton). Raw vertexRoad
// (s/t/dz/...) or vertexLocal attrs. kind distinguishes the vertex flavor. ----
struct OdrSkeletonVertex
{
    std::string kind;   // "vertexRoad" | "vertexLocal"
    std::string s;      // @s (vertexRoad)
    std::string t;      // @t (vertexRoad)
    std::string u;      // @u (vertexLocal)
    std::string v;      // @v (vertexLocal)
    std::string dz;     // @dz
    std::string radius; // @radius
    std::string id;     // @id
    std::string intersection_point;  // @intersectionPoint (t_bool)
};

// One <skeleton>/<polyline> (or other skeleton geometry). L1: its id + ordered vertices.
struct OdrSkeletonPolyline
{
    std::string                    id;  // @id
    std::vector<OdrSkeletonVertex> vertices;
};

// ---- cluster 19: object <borders>/<border> (t_road_objects_object_borders_border). Raw attrs. ----
struct OdrObjectBorder
{
    std::string width;                  // @width
    std::string type;                   // @type (concrete|curb|...)
    std::string outline_id;             // @outlineId
    std::string use_complete_outline;   // @useCompleteOutline (t_bool)
};

// ---- cluster 19: repeat lateral polynomial (1.9 @bT/@cT/@dT/@detachFromReferenceLine). Stored so the
// AdjustRepeatInstancePose fork helper can look it up by (road_id, object_id) later. base_s/base_length
// mirror the repeat's @s/@length for the pose remap. Parameterization: see AdjustRepeatInstancePose
// doc comment (normalized fraction f in [0,1] along the repeat, per the plan's declared semantics). ----
struct OdrRepeatLateralPoly
{
    double base_s      = 0.0;  // repeat @s
    double base_length = 0.0;  // repeat @length
    double t_start     = 0.0;  // repeat @tStart (linear ramp start)
    double t_end       = 0.0;  // repeat @tEnd   (linear ramp end)
    double bT          = 0.0;  // @bT
    double cT          = 0.0;  // @cT
    double dT          = 0.0;  // @dT
    bool   detach_from_reference_line = false;  // @detachFromReferenceLine
    bool   has_poly    = false;  // true when any of bT/cT/dT/detach was authored (sparse fast path)
};

// ---- cluster 19b: <objectReference> (t_road_objects_objectReference). Reference to another object;
// carries its own s/t placement. L1 raw + parsed doubles for the synthesis clone. road_id is the
// DECLARING road (where the reference lives + where the clone is synthesized). ----
struct OdrObjectReference
{
    std::string  road_id;         // DECLARING road@id
    std::string  ref_id;          // @id -- the REFERENCED object's id
    double       s        = 0.0;  // @s
    double       t        = 0.0;  // @t
    double       z_offset = 0.0;  // @zOffset
    std::string  valid_length;    // @validLength (verbatim)
    std::string  orientation;     // @orientation (+|-|none)
    unsigned int synth_object_id = 0;  // synthesized clone RMObject id (0 = none)
};

// ---- cluster 19b: <bridge> (t_road_objects_bridge). L1 raw + parsed; @type material class
// (concrete|steel|brick|wood). road_id is the road the bridge spans. ----
struct OdrBridge
{
    std::string  road_id;  // owning road@id
    std::string  id;       // @id
    std::string  name;     // @name
    std::string  type;     // @type (e_bridgeType)
    double       s      = 0.0;  // @s
    double       length = 0.0;  // @length
    unsigned int synth_object_id = 0;  // synthesized BRIDGE RMObject id (0 = none)
};

// Per-object extras beyond what upstream RMObject stores. One entry per <object> that carries at
// least one P7 datum (sparse). Keyed by (road_id, object_id) as authored strings.
struct OdrObjectExtras
{
    std::string road_id;    // owning <road>@id
    std::string object_id;  // <object>@id

    bool                            perp_to_road_present = false;  // @perpToRoad authored?
    std::string                     perp_to_road;                  // @perpToRoad raw ("true"/"false")
    std::vector<OdrObjectMaterial>  materials;   // <material> (XSD allows several)
    std::vector<OdrObjectOutline>   outlines;    // outline attrs + outline-level markings (L1)
    std::vector<OdrSkeletonPolyline> skeleton;   // <skeleton>/<polyline>
    std::vector<OdrObjectBorder>    borders;     // <borders>/<border>
    std::vector<OdrCrgRecord>       surface_crgs;// object-level <surface>/<CRG>
    OdrRepeatLateralPoly            repeat_poly; // <repeat> 1.9 lateral polynomial (has_poly gate)

    // P8 (1.9): plain xs:boolean flags (same semantics as OdrSignalExtras). invalidated object is
    // excluded from the OSI StationaryObject output; temporary is L1-only. *_present record authoring.
    bool temporary            = false;
    bool temporary_present    = false;  // @temporary authored?
    bool invalidated          = false;
    bool invalidated_present  = false;  // @invalidated authored?

    // P8 cluster 22 L1: <validity> records that authored @layer (sparse).
    std::vector<OdrValidityLayer> validity_layers;

    bool HasAny() const
    {
        return perp_to_road_present || !materials.empty() || !outlines.empty() || !skeleton.empty() ||
               !borders.empty() || !surface_crgs.empty() || repeat_poly.has_poly || temporary_present ||
               invalidated_present || !validity_layers.empty();
    }
};

// ---- cluster 17: road <lateralProfile> shape + crossSectionSurface L1 + degrade bookkeeping. ----
// One <shape> row: s + t + cubic poly (a/b/c/d) in t at that s.
struct OdrLateralShape
{
    double s = 0.0;
    double t = 0.0;
    double a = 0.0, b = 0.0, c = 0.0, d = 0.0;
};

// One <coefficients> row inside a crossSectionSurface strip/tOffset (cubic in s: a/b/c/d @ s).
struct OdrCssCoefficients
{
    double s = 0.0;
    double a = 0.0, b = 0.0, c = 0.0, d = 0.0;
};

// One crossSectionSurface <strip>: id + t-dependence flavor (constant/linear/quadratic/cubic) with
// its <width> and height coefficients. term_kind names which of constant|linear|quadratic|cubic was
// authored (the strip's t-height flavor); coeffs holds that element's <coefficients> rows.
struct OdrCssStrip
{
    std::string                     id;         // @id (1/-1/2/-2)
    std::string                     mode;       // @mode (outer strips only)
    std::string                     term_kind;  // "constant" | "linear" | "quadratic" | "cubic" | ""
    std::vector<OdrCssCoefficients> width;      // <width><coefficients>
    std::vector<OdrCssCoefficients> height;     // <constant|linear|quadratic|cubic><coefficients>
};

// The road-level lateralProfile extras (cluster 17). Sparse: one entry per road that authored a
// <shape> or a <crossSectionSurface> (a road with only <superelevation> gets no entry -- that path is
// handled bit-identically by upstream). Records whether a degrade-to-equivalent-superelevation was
// applied and the equivalent crossfall used (diagnostic handle for the later report/OSI WP).
struct OdrRoadLateralProfile
{
    std::string                  road_id;
    std::vector<OdrLateralShape> shapes;             // <shape> rows (DOM order)
    bool                         has_css = false;    // <crossSectionSurface> present
    std::vector<OdrCssCoefficients> css_t_offset;    // crossSectionSurface/<tOffset><coefficients>
    std::vector<OdrCssStrip>     css_strips;         // crossSectionSurface/surfaceStrips/<strip>

    // Degrade bookkeeping (filled by ApplyLateralProfileDegrade in the typed BuildSideModel overload).
    bool   authored_superelevation = false;  // road had authored <superelevation> -> degrade skipped
    bool   degrade_applied         = false;  // equivalent superelevation was synthesized
    double equiv_crossfall_slope   = 0.0;    // representative b (dz/dt at t=0) used for the degrade
};

// ===========================================================================
// P7 cluster 8/9: junction geometry (boundary/elevationGrid) + junction-level objects/surface +
// document-level junctionGroup. Stored in OdrJunctionGeom.cpp (new file), keyed by junction_id so
// the existing OdrJunctionExtras struct/file stays untouched (P6 conflict-surface minimization).
// ===========================================================================

// One <junction><boundary><segment>. Raw attrs (segment types: lane|position|joint per XSD).
struct OdrJunctionBoundarySegment
{
    std::string type;           // @type (lane|position|joint)
    std::string road_id;        // @roadId
    std::string boundary_lane;  // @boundaryLane
    std::string s_start;        // @sStart (may be "start"/"end" keyword or a number)
    std::string s_end;          // @sEnd
};

// One <junction><elevationGrid><elevation> row (center + left/right height lists, raw). ----
struct OdrJunctionGridElevation
{
    std::string center;  // @center
    std::string left;    // @left  (space-separated list, raw)
    std::string right;   // @right (space-separated list, raw)
};

// Per-junction geometry extras (cluster 8). Sparse: one entry per junction carrying a boundary /
// elevationGrid / junction-level objects / junction-level surface. Keyed by junction_id (authored).
struct OdrJunctionGeomExtras
{
    std::string junction_id;  // <junction>@id

    std::vector<OdrJunctionBoundarySegment> boundary;         // <boundary>/<segment>
    std::string                             grid_spacing;     // <elevationGrid>@gridSpacing
    std::string                             grid_s_start;     // <elevationGrid>@sStart
    std::vector<OdrJunctionGridElevation>   grid_elevations;  // <elevationGrid>/<elevation>
    bool                                    has_grid = false; // <elevationGrid> present
    std::vector<OdrCrgRecord>               surface_crgs;     // junction-level <surface>/<CRG>
    int                                     object_count = 0; // junction-level <objects>/<object> count (L1)

    bool HasAny() const
    {
        return !boundary.empty() || has_grid || !surface_crgs.empty() || object_count > 0;
    }
};

// ---- cluster 9: document-level <junctionGroup> (roundabout|interchange|unknown). L1: @id/@name/
// @type + the <junctionReference @junction> member ids. ----
struct OdrJunctionGroup
{
    std::string              id;        // @id
    std::string              name;      // @name
    std::string              type;      // @type (roundabout|interchange|unknown)
    std::vector<std::string> members;   // <junctionReference @junction> ids
};

// ===========================================================================
// P8 (cluster 4/22): 1.9 lane layers. A road may carry MULTIPLE <lanes> elements, each tagged
// @layer="permanent"|"temporary" (or untagged == permanent). The temporary layer describes a
// roadworks sub-range that overrides the permanent lanes over an s-range [t0,t1). L1 SHADOW storage
// only -- the s-range merge into a synthetic <lanes> that RoadManager walks is done in
// OdrLaneLayers.cpp (SelectLanesLayer/BuildMergedLanes); this struct records what was AUTHORED so a
// consumer/diagnostic can see the pre-merge layout. Sparse: one entry per road that authored either
// a @layer attribute or more than one <lanes> element.
// ===========================================================================

// One laneSection summary inside a layer (DOM order). s + optional @length + lane count.
struct OdrLaneLayerSection
{
    double s          = 0.0;
    double length     = 0.0;
    bool   has_length = false;  // @length authored (1.9; meaningful on temporary layers)
    int    lane_count = 0;      // number of <lane> elements (all sides) in this section
};

// One <lanes> layer of a road (DOM order).
struct OdrLaneLayer
{
    std::string                      name;  // @layer verbatim ("" when the attribute was absent == permanent)
    std::vector<OdrLaneLayerSection> sections;
    int                              lane_offset_count = 0;  // number of <laneOffset> in this layer
};

// Per-road lane-layer L1 record. temp_s_start/temp_s_end bound the temporary layer's coverage
// [t0,t1); active_mode is the mode SelectLanesLayer resolved for this parse ("permanent"|"temporary").
struct OdrRoadLaneLayers
{
    std::string               road_id;
    std::vector<OdrLaneLayer> layers;       // one per <lanes> element (DOM order)
    bool                      has_temporary = false;
    double                    temp_s_start  = 0.0;  // t0 (valid only when has_temporary)
    double                    temp_s_end    = 0.0;  // t1
    std::string               active_mode;          // "permanent" | "temporary" (resolved at parse)
};

}  // namespace odr
}  // namespace gt_esmini
