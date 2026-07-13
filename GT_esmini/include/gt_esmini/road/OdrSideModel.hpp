// OdrSideModel.hpp -- GT-side OpenDRIVE "side model": a second, GT-owned pass over the same
// xodr DOM that records everything upstream RoadManager cannot (because RoadManager.hpp is
// pristine upstream and cannot grow new data structures -- see plan §3.1).
//
// Plan: GT_esmini/docs/opendrive_16_19_support_plan.md (P1: version awareness + no-silent-drop
// coverage audit + userData/dataQuality storage + include hard-error + removed-in-1.6 table).
//
// This module is DELIBERATELY testable without any RoadManager fork hook: call BuildSideModel()
// directly on a parsed pugi::xml_document. The fork hook (gt_esmini::odr::BuildSideModel(doc, this)
// just before CheckConnections()) is applied later by the orchestrator; nothing here depends on it.
//
// Namespace: gt_esmini::odr
//
// -------- Stored audit-entry format (STABLE -- tests assert on it) --------
// OdrAuditStats::entries holds, sorted and deduped, one string per unique (entry, context) pair:
//
//     "<path>|ctx=<ctx>"           for an unsupported ELEMENT
//     "<path>@<attr>|ctx=<ctx>"    for an unsupported ATTRIBUTE on a supported element
//     "<path>|ctx=<ctx>|removed16" for an element REMOVED in OpenDRIVE 1.6 (cluster 21)
//
//   * <path> is relative to the OpenDRIVE root, no leading slash, using the actual element names
//     (e.g. "road/lanes/laneSection/left/lane"). laneSection sides appear as left|center|right.
//   * <ctx> is the id of the nearest ancestor <road>/<junction> (@id), or empty string "" if none
//     (the literal text is empty between "ctx=" and the next "|" or end).
//   * The "|removed16" suffix is present ONLY for removed-in-1.6 hits (and those are counted in
//     OdrAuditStats::removed16_hits, NOT in unsupported_elements).
//
// The human-facing LOG_WARN line uses a different, prefixed rendering (see OdrCoverageAudit.cpp);
// the STORED format above is the contract for programmatic consumers and tests.
#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "gt_esmini/road/OdrSideExtras.hpp"

// Light forward declarations -- no heavy pugixml / RoadManager includes in the public header.
namespace pugi
{
class xml_document;
class xml_node;
}

namespace roadmanager
{
class OpenDrive;
class Road;
class Signal;
}
namespace roadmanager
{
class OpenDrive;
class Position;
class RMObject;
class Outline;
}

namespace gt_esmini
{
namespace odr
{

// A raw additionalData blob (userData / dataQuality) captured verbatim. The parser does NOT
// descend into or audit these -- their content is opaque to L1.
struct OdrExtraData
{
    std::string owner_path;  // path of the OWNING element (e.g. "road", "road/lanes/laneSection/left/lane")
    std::string context_id;  // nearest ancestor road@id / junction@id, or ""
    std::string xml;         // the element serialized back to XML (pugi print to string)
};

// Machine-verifiable coverage result (plan §3.3). Counts are always complete regardless of the
// per-parse log cap.
struct OdrAuditStats
{
    std::size_t unsupported_elements   = 0;  // count of [ODR-UNSUPPORTED] element hits (topmost only)
    std::size_t unsupported_attributes = 0;  // count of [ODR-UNSUPPORTED] attribute hits
    std::size_t removed16_hits         = 0;  // count of [ODR-REMOVED-1.6] hits (cluster 21)
    // Deduped + sorted. One string per unique (entry, context) pair, in the STORED format
    // documented at the top of this header.
    std::vector<std::string> entries;
};

// The side model for one parsed OpenDRIVE document. Owned by the registry, keyed by the caller's
// opaque instance pointer.
class OdrSideModel
{
public:
    // ---- cluster 1: version awareness (header) ----
    int         rev_major = -1;   // header@revMajor (missing -> -1)
    int         rev_minor = -1;   // header@revMinor (missing -> -1 + WARN)
    std::string header_name;      // header@name (informational; not read by RoadManager)
    std::string header_version;   // header@version (informational)

    // ---- cluster 15: additionalData storage ----
    std::vector<OdrExtraData> user_data;     // every <userData> element, verbatim
    std::vector<OdrExtraData> data_quality;  // every <dataQuality> element, verbatim

    // ---- cluster 1/21: coverage audit ----
    OdrAuditStats audit;

    // ---- cluster 14: header license + default regulations (P4 L1) ----
    bool                              has_license = false;
    OdrLicense                        license;
    std::vector<OdrDefaultRegulation> default_regulations;  // roadRegulations + signalRegulations, DOM order

    // ---- cluster 13: document-level vmsGroup list (P4 L1) ----
    std::vector<OdrVmsGroup> vms_groups;

    // ---- extras skeletons (architecture placeholders, P2..P9) ----
    std::vector<OdrLaneExtras>     lane_extras;
    std::vector<OdrSignalExtras>   signal_extras;
    std::vector<OdrJunctionExtras> junction_extras;

    // ---- P9a cluster 20 (OdrRailroad.cpp): railroad switches (per-road) + root-level stations. L1
    // storage only, INERT -- stored and queryable but no runtime consumer (no rail runtime / no OSI /
    // no policy); documented-inactive per plan §5 P9. ----
    std::vector<OdrRailSwitch> rail_switches;
    std::vector<OdrStation>    stations;

    // ---- P7 clusters 17/18/19 (OdrObjectExtras.cpp) ----
    std::vector<OdrObjectExtras>       object_extras;      // per-object family L1 (cluster 19)
    std::vector<OdrObjectReference>    object_references;  // <objectReference> (cluster 19b)
    std::vector<OdrBridge>             bridges;            // <bridge> (cluster 19b)
    std::vector<OdrRoadLateralProfile> road_lateral;       // per-road shape/crossSectionSurface (cluster 17)
    std::vector<OdrCrgRecord>          road_surface_crgs;  // road-level <surface><CRG> (cluster 18)

    // ---- P7 clusters 8/9 (OdrJunctionGeom.cpp) ----
    std::vector<OdrJunctionGeomExtras> junction_geom;   // per-junction boundary/grid/objects/surface
    std::vector<OdrJunctionGroup>      junction_groups; // document-level <junctionGroup>

    // ---- P8 cluster 4/22 (OdrLaneLayers.cpp): 1.9 lane-layer shadow storage. Sparse: one entry per
    // road that authored @layer or more than one <lanes> element. ----
    std::vector<OdrRoadLaneLayers> lane_layers;

    // ---- P9b: road/<type @country> L1 (sparse; no runtime consumer). Closes the last pinned
    // [ODR-UNSUPPORTED] attribute (official Ex_Railway-Station road 4). ----
    std::vector<OdrRoadTypeExtra> road_type_extras;

    // ---- P8 (plan D3): synthetic merged-<lanes> DOM documents built by SelectLanesLayer (temporary
    // mode), keyed by road id. Owned here so they outlive the fork parse (RoadManager + ParseLaneExtras
    // both walk the same cached node). pugi::xml_document is only forward-declared in this header, so
    // OdrSideModel needs an out-of-line destructor (defined in OdrSideModel.cpp where the type is
    // complete) -- the unique_ptr member alone would otherwise force the destructor here.
    std::map<std::string, std::unique_ptr<pugi::xml_document>> merged_lanes_docs;

    OdrSideModel();
    ~OdrSideModel();
};

// Walk `doc`, build a side model, and register it under `opendrive_key` (an opaque instance
// pointer -- in production the OpenDrive* being parsed; in tests any stable address). Any existing
// entry for the key is cleared first, so a re-parse REPLACES rather than accumulates.
//
// Returns false ONLY on a hard error: an <include> element was found anywhere in the document.
// In that case the diagnostics have already been logged (LOG_ERROR) and the caller MUST abort the
// parse (plan P1: <include> is unsupported by design; resolution decision deferred to P9). All
// other conditions (unknown elements/attributes, removed-in-1.6 hits, missing version) return true
// and are recorded in the model's audit stats.
//
// Lifetime note: the registry keeps the model alive until the key is re-parsed (BuildSideModel
// clears+rebuilds) or explicitly cleared (ClearSideModel). Entries for a destroyed OpenDrive
// instance therefore PERSIST until one of those happens. This is acceptable in practice: esmini
// uses a singleton OpenDrive and each DLL statically links its own copy of this module (no
// cross-DLL registry sharing).
bool BuildSideModel(const pugi::xml_document& doc, const void* opendrive_key);

// Typed overload -- this is the one the fork hook binds to (`BuildSideModel(doc, this)` inside
// OpenDrive::ParseOpenDriveXML resolves here by exact match, no fork change needed). Behaves like
// the opaque-key overload (key = od) and ADDITIONALLY applies the P2 border->width normalization
// to od's freshly parsed lanes through the public Lane API (AddLaneWidth): for every lane that
// authored <border> elements and zero <width> elements, width_i = side_sign * (border_i -
// border_{i-1}) as piecewise-cubic algebra (plan P2, Ex_Lane-Border false-green fix).
bool BuildSideModel(const pugi::xml_document& doc, roadmanager::OpenDrive* od);

// ---- P2 cluster 16 L2: lane <speed> lookup ----
// Speed limit [m/s] authored via lane <speed> at (road_id, lane_id, s), resolved against the side
// model registered under `opendrive_key`. Returns 0.0 when no lane speed record applies (callers
// MUST treat <= 0 as "no lane limit" and keep their existing road-type-speed path bit-identical).
double GetLaneSpeedLimit(const void* opendrive_key, const std::string& road_id, int lane_id, double s);

// Convenience wrapper for runtime consumers (VD speed planning): resolves the OpenDrive singleton
// + road id string + lane id + s from `pos`. Same <= 0 contract as above.
double GetLaneSpeedLimitForPosition(const roadmanager::Position& pos);

// Fetch the model registered under `opendrive_key`, or nullptr if none.
const OdrSideModel* GetSideModel(const void* opendrive_key);

// ---- P4 signal-extras lookup (clusters 10/13 L1) ----
// Find the OdrSignalExtras for (road_id, signal_id) -- both AUTHORED xodr strings -- in the model
// registered under `opendrive_key`. Returns nullptr when no side model is registered, or the signal
// carries no stored extras (no dependency/reference/semantics/board). Deterministic: signal ids are
// unique within a road (XSD key k_road_signals_signalId), so the (road_id, signal_id) pair is unique.
const OdrSignalExtras* GetSignalExtras(const void* opendrive_key, const std::string& road_id, const std::string& signal_id);

// Convenience overload for runtime VD/OSI consumers: resolve extras from a live roadmanager::Signal*
// via the same xodr-id idiom [GT_ODR:sig-ref] uses (FindSignalByXodrId): the side model is keyed on
// the OpenDrive singleton, and the signal's owning road id + its GetId() (rendered as the authored
// string) form the lookup key. Signal ids are unique per road, but the SAME numeric id may recur on
// different roads; this resolves against the road the Signal belongs to, so it is unambiguous.
// `od` is the OpenDrive the signal was parsed from (the registry key). Returns nullptr on any miss.
const OdrSignalExtras* GetSignalExtras(const roadmanager::OpenDrive* od, const roadmanager::Signal* sig);

// Remove the model registered under `opendrive_key` (no-op if none).
void ClearSideModel(const void* opendrive_key);

// ---- P5 junction accessors (clusters 5/7/22; F3 priority handoff) ----
// The junction extras entry for `junction_id` in the side model registered under `opendrive_key`,
// or nullptr when there is no side model / no matching junction (junction ids are string-typed as
// authored, consistent with OdrJunctionExtras::junction_id). Implemented in OdrJunctionExtras.cpp.
const OdrJunctionExtras* GetJunctionExtras(const void* opendrive_key, const std::string& junction_id);

// Copy the <priority high low> list (XSD allows multiple) for `junction_id` into `out`. Returns
// false (and leaves `out` untouched) when there is no side model or no entry for that junction. The
// canonical junction-priority source for feature F3.
bool GetJunctionPriorities(const void* opendrive_key, const std::string& junction_id, std::vector<OdrJunctionPriority>& out);

// True if road_id appears as a <junction><connection @connectingRoad> anywhere in the document.
//
// ODR 1.8 widened road/@junction. Up to 1.6 the schema documented it as "ID of the junction to which
// the road belongs AS A CONNECTING ROAD"; 1.8/1.9 documents it as "ID of the junction to which the
// road belongs, FOR EXAMPLE connecting roads, CROSS PATHS, and roads of a junction boundary"
// (1.9 OpenDRIVE_Road.xsd, @junction). So @junction != -1 no longer implies "connecting road".
// Only connecting roads are expected to link at both ends -- a cross path road is attached through
// <crossPath><startLaneLink>/<endLaneLink> (1.9 OpenDRIVE_Junction.xsd, t_junction_crossPath: "the
// cross path itself is a separate road") and legally carries no <link> successor/predecessor.
// Called from the thin fork hook [GT_ODR:junc-connroad] in GT_RoadManager.cpp to scope the
// "connecting road ... lacks successor/predecessor" warning to roads it actually applies to.
// Parse-time only; walks the junction/connection DOM (small) rather than the not-yet-built model.
bool IsConnectingRoad(const pugi::xml_node& odr_node, const std::string& road_id);

// ---------------------------------------------------------------------------
// P9a cluster 20 accessors (railroad/station). Implemented in odr_side/OdrRailroad.cpp, keyed on the
// OpenDrive* registry key like the P5 junction accessors (upstream RoadManager stays pristine).
//
// IMPORTANT -- these expose L1 storage ONLY. The railroad/station data is stored and queryable but
// INERT: there is NO runtime consumer (no rail runtime, no OSI output, no policy). This is
// documented-inactive per plan §5 P9. Direct iteration over GetSideModel(key)->rail_switches /
// ->stations stays available like other side-model data.
// ---------------------------------------------------------------------------

// The <switch>@id `switch_id` on road `road_id` (both AUTHORED strings), or nullptr when there is no
// side model / no matching switch. Switch ids are unique within a road (per the railroad content
// model), so (road_id, switch_id) is unambiguous.
const OdrRailSwitch* GetRailSwitch(const void* opendrive_key, const std::string& road_id, const std::string& switch_id);

// Copy every <switch> owned by road `road_id` into `out`. Returns false (leaving `out` untouched)
// ONLY when there is no side model registered under `opendrive_key`; a road with an empty
// <railroad/> (or none) yields true with an empty `out`.
bool GetRoadRailSwitches(const void* opendrive_key, const std::string& road_id, std::vector<OdrRailSwitch>& out);

// The root-level <station>@id `station_id` (authored string), or nullptr when there is no side model /
// no matching station.
const OdrStation* GetStation(const void* opendrive_key, const std::string& station_id);

// ---------------------------------------------------------------------------
// P7 accessors (clusters 8/9/17/19). All keyed on the OpenDrive* registry key like the P5 junction
// accessors; upstream RoadManager stays pristine. Implemented in OdrObjectExtras.cpp (object/lateral)
// and OdrJunctionGeom.cpp (junction geometry / group). All return nullptr / false on any miss.
// ---------------------------------------------------------------------------

// Object-family extras for (road_id, object_id) -- both AUTHORED strings. nullptr on miss.
const OdrObjectExtras* GetObjectExtras(const void* opendrive_key, const std::string& road_id, const std::string& object_id);

// Road lateralProfile extras (shape / crossSectionSurface) for `road_id`. nullptr on miss.
const OdrRoadLateralProfile* GetRoadLateralProfile(const void* opendrive_key, const std::string& road_id);

// Junction geometry extras (boundary/elevationGrid/objects/surface) for `junction_id`. nullptr on miss.
// The later OSI reporter WP consumes the authored boundary polygon through this handle.
const OdrJunctionGeomExtras* GetJunctionGeom(const void* opendrive_key, const std::string& junction_id);

// Copy the document-level <junctionGroup> list into `out`. Returns false (out untouched) when there is
// no side model or no junctionGroup was authored.
bool GetJunctionGroups(const void* opendrive_key, std::vector<OdrJunctionGroup>& out);

// Policy hint: true iff `junction_id` is a member of any <junctionGroup type="roundabout">. Side helper
// only -- no consumer wiring / no policy change (that is a later feature-week concern).
bool IsJunctionInRoundabout(const void* opendrive_key, const std::string& junction_id);

// ---------------------------------------------------------------------------
// P7 WP4 (cluster 8 L3): authored junction <boundary> -> world polyline. FLAGGED, default OFF.
// ---------------------------------------------------------------------------

// One evaluated world-space vertex of an authored junction boundary polyline.
struct OdrBoundaryPoint
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

// Evaluate the authored <boundary>/<segment> list for `junction_id` (stored L1 by
// OdrJunctionGeom.cpp) into an ordered world-space polyline, appended to `xyz_out`. Segments are
// walked in AUTHORED order (XSD guarantees they run counter-clockwise and form a closed boundary):
//
//   * type="lane": walk the referenced road's edge -- the OUTER edge of @boundaryLane (relative to
//     the road center; signed t = sign(boundaryLane) * LaneSection::GetOuterOffset(s, boundaryLane))
//     -- from sStart to sEnd, sampling world XYZ via roadmanager::Position::SetTrackPos. sStart/sEnd
//     accept the XSD keywords "start"/"begin" (-> s=0) and "end" (-> road length) as well as a
//     numeric s (clamped to [0, length]). Sampling step is <= 2 m with >= 2 points emitted per
//     non-degenerate segment; a degenerate (sStart==sEnd) segment emits its single point.
//   * type="joint": a STRAIGHT connection perpendicular to a road end -- it contributes no vertices
//     of its own (the polyline already connects consecutive lane-segment endpoints with a straight
//     edge, which is exactly the joint). Documented no-op; kept for authored-order fidelity.
//
// Returns false (leaving `xyz_out` untouched) on any degenerate/dangling input: no side model, no
// boundary authored, unknown roadId, unresolvable lane/lane-section, or fewer than 3 resulting
// points (a polygon needs >= 3). Every failure logs a WARN so the caller can fall back to the
// upstream heuristic. Pure-ish (reads only od + the side model registered under `od`); the one side
// effect is a scratch roadmanager::Position it constructs and discards.
bool BuildAuthoredJunctionBoundaryPolyline(const void*                     opendrive_key,
                                           const std::string&              junction_id,
                                           roadmanager::OpenDrive*         od,
                                           std::vector<OdrBoundaryPoint>&  xyz_out);

// Flag gate for the WP4 OSI post-pass (GT_OSIReporter). Default OFF. Read ONCE from env
// GT_ODR_OSI_AUTHORED_JUNCTION_BOUNDARY on first query ("1"/"true", case-insensitive -> ON; anything
// else / unset -> OFF). The setter overrides the env read for tests (same idiom as WP2's
// SetCurveLocalMaxSegmentLength). When OFF the post-pass is a hard no-op and every existing OSI
// golden stays byte-identical.
void SetUseAuthoredJunctionBoundary(bool on);
bool GetUseAuthoredJunctionBoundary();

// ---------------------------------------------------------------------------
// P7 fork helpers (T2). Implemented in OdrObjectExtras.cpp; the fork call sites in
// GT_RoadManager.cpp are wired in a LATER WP, so these are temporarily unreferenced by the fork.
// ---------------------------------------------------------------------------

// Test knob for AppendCurveLocalCorners: max chord length [m] per tessellated segment (default 1.0).
// Read once from env GT_ODR_CURVELOCAL_SEGLEN on first use; this setter overrides it for tests.
void   SetCurveLocalMaxSegmentLength(double meters);
double GetCurveLocalMaxSegmentLength();

// [T2a] Tessellate a 1.9 <curveLocal> outline element into OutlineCornerLocal corners appended to
// `outline`. Reads @u/@v/@z/@height/@length/@hdg + the single child geometry (arc|line|paramPoly3)
// and samples it by arc length (max segment = GetCurveLocalMaxSegmentLength, >= 3 pts/segment) in the
// object-local (u,v) plane, preserving authored winding. `next_corner_id` is the running id assigned
// to appended corners (advanced past those consumed). Closure is left to the outline's closed_ flag
// (no duplicate closing point emitted). Degenerate input (zero-length / NaN) -> WARN + skip, return
// false. Returns true when >= 1 corner was appended. The fork wiring lives in a LATER WP.
bool AppendCurveLocalCorners(const pugi::xml_node& curve_local_node,
                             roadmanager::Road*    road,
                             roadmanager::RMObject* obj,
                             roadmanager::Outline* outline,
                             unsigned int&         next_corner_id);

// [T2b] Adjust one repeat-instance pose by the 1.9 lateral polynomial (@bT/@cT/@dT) and, when
// @detachFromReferenceLine is true, remap onto the start->end chord. `frac` in [0,1] is the normalized
// position along the repeat; s_io/t_io are updated in place. Looks the polynomial up from the side
// model via (road_id, object_id). Returns false quickly (leaving s_io/t_io untouched) when the object
// has NO lateral-poly record (legacy bit-identical fast path). See the .cpp doc block for the exact
// parameterization reading. The fork wiring lives in a LATER WP.
bool AdjustRepeatInstancePose(const roadmanager::RMObject* obj,
                              const roadmanager::Road*     road,
                              double                       s_inst,
                              double                       frac,
                              double&                      s_io,
                              double&                      t_io);

// ---------------------------------------------------------------------------
// P8 (cluster 4/22): 1.9 lane-layer selection. Implemented in odr_side/OdrLaneLayers.cpp.
// ---------------------------------------------------------------------------

// Select the <lanes> node the RoadManager fork walks for `road_node` (the [GT_ODR:lane-layers] fork
// hook calls this in place of road_node.child("lanes")). `opendrive_key` is the opaque parse key
// (the OpenDrive* being parsed) -- the SAME key BuildSideModel is registered under -- so the merged
// DOM has a home to live in (a per-key pending registry).
//
// Mode is read ONCE from env GT_ODR_LANE_LAYERS (case-insensitive; "temporary" opts into the merge,
// unset/"permanent" selects the permanent layer, unknown values WARN + permanent -- plan D1). In
// permanent mode (or on a single-<lanes> road) the ORIGINAL permanent node is returned unchanged (no
// copy -> lane global-id ordering is provably stable). In temporary mode the permanent + temporary
// layers are merged over the temporary s-range into a synthetic <lanes> document (plan D2/D3); the
// result is cached per (opendrive_key, road_id) so repeat calls (RoadManager + ParseLaneExtras)
// return the SAME node. Legacy assets (one untagged <lanes>) always take the no-copy fast path.
pugi::xml_node SelectLanesLayer(const pugi::xml_node& road_node, const void* opendrive_key);

// Test override for the GT_ODR_LANE_LAYERS mode (same idiom as WP4's SetUseAuthoredJunctionBoundary).
// In production the mode is latched ONCE from the env var on first use (D1: no runtime switching);
// this setter lets a unit test flip between permanent and temporary in a single process. Pass
// on=true for the temporary merge, on=false for permanent. SetLaneLayerModeUseEnv() reverts to the
// env-driven latch. NOT for production wiring.
void SetLaneLayerModeForTest(bool temporary_on);
void SetLaneLayerModeUseEnv();

// P9b: the lane-layer mode THIS process resolved (the D1 latch as currently effective: honors a
// test override, otherwise the once-latched env value). Returns "permanent" or "temporary".
// Read-only diagnostics for the GT_RM JSON API / web odr-metadata panel.
const char* GetLaneLayerModeName();

// ---------------------------------------------------------------------------
// P3 signal placement / cross-reference helpers (clusters 11/12).
// Implemented in odr_side/OdrSignalExtras.cpp; called from the two thin fork
// hooks [GT_ODR:sig-pos] and [GT_ODR:sig-ref] in GT_RoadManager.cpp.
// ---------------------------------------------------------------------------

// Where a signal's PHYSICAL pose comes from, resolved by ResolveSignalPose(). The fork builds
// roadmanager::Position(road->GetId(), s, t) from it; `road` is never null on return (defaults
// to the signal's logical road when no position child exists or resolution fails).
struct SignalPoseResolution
{
    roadmanager::Road* road = nullptr;
    double             s    = 0.0;
    double             t    = 0.0;
    bool               has_world_h = false;  // positionInertial@hdg present -> absolute heading
    double             world_h     = 0.0;
};

// [GT_ODR:sig-pos] Resolve <positionRoad>/<positionInertial> under `signal_node` (plan P3,
// cluster 12):
//   * <positionRoad>: attach the physical pose to the referenced road at (@s,@t); @zOffset/@hOffset
//     (and @pitch/@roll when present) override the signal's own values. Unknown @roadId -> WARN +
//     logical pose. Referenced roads must appear BEFORE the referring signal's road in the
//     document (parse-time limitation, documented in gt_roadmanager_patches.md).
//   * <positionInertial>: reverse-map (@x,@y,@z) via Position::XYZ2TrackPos to road/s/t. Off-road
//     (projection clamped beyond road ends, or |t| > 30 m) -> WARN + logical pose (skip).
//     @hdg (when present) becomes an absolute world heading override.
//   * 1.9 s/t omission: when the signal's own @s/@t are absent they are backfilled from the
//     resolved pose IF it lies on the signal's logical road; absent s/t WITHOUT any position
//     child is diagnosed with a WARN (defaults to 0/0).
// Mutates sig_s/sig_t (backfill) and z_offset/h_offset/pitch/roll (overrides) accordingly.
SignalPoseResolution ResolveSignalPose(const pugi::xml_node& signal_node,
                                       roadmanager::OpenDrive* odr,
                                       roadmanager::Road*      logical_road,
                                       double&                 sig_s,
                                       double&                 sig_t,
                                       double&                 z_offset,
                                       double&                 h_offset,
                                       double&                 pitch,
                                       double&                 roll);

// [GT_ODR:sig-ref] Materialize every road-level <signalReference> in `doc` as a clone of the
// referenced Signal (plan P3, cluster 12): the clone carries the REFERENCE's s/t/orientation and
// <validity> children, everything else from the referenced signal; dynamic targets clone as
// TrafficLight (consistent with [GT_ODR:tl-gate]). Clones are added to their road via
// Road::AddSignal with a fresh global id + SetAllValidLanes. Must run AFTER all roads are parsed
// (document-wide forward references) -- i.e. from the fork hook site just before
// CheckConnections(). Unresolvable references (unknown signal id / road) -> WARN + skip.
// Returns the created signals so the CALLER (fork) can register dynamic ones in
// OpenDrive::dynamic_signals_ (private member, hence not done here).
std::vector<roadmanager::Signal*> MaterializeSignalReferences(const pugi::xml_document& doc,
                                                              roadmanager::OpenDrive*   odr);

}  // namespace odr
}  // namespace gt_esmini
