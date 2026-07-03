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

    // ---- extras skeletons (architecture placeholders, P2..P9) ----
    std::vector<OdrLaneExtras>     lane_extras;
    std::vector<OdrSignalExtras>   signal_extras;
    std::vector<OdrJunctionExtras> junction_extras;
    std::vector<OdrRailroad>       railroads;
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

// Remove the model registered under `opendrive_key` (no-op if none).
void ClearSideModel(const void* opendrive_key);

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
