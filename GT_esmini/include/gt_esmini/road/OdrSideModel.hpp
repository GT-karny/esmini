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

}  // namespace odr
}  // namespace gt_esmini
