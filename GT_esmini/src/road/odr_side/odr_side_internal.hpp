// odr_side_internal.hpp -- PRIVATE cross-TU glue for the OdrSideModel implementation.
// Not installed / not part of the public API. Only the three odr_side/*.cpp include it.
//
// Split across TUs to mirror the plan's file layout (OdrSideModel / OdrSideParser /
// OdrCoverageAudit) while keeping a single DOM walk.
#pragma once

#include <string>

#include "gt_esmini/road/OdrSideModel.hpp"
#include "pugixml.hpp"

namespace gt_esmini
{
namespace odr
{
namespace detail
{

// ---- OdrSideParser.cpp ----

// Read header revMajor/revMinor/name/version into `model`; LOG_INFO the detected version once and
// LOG_WARN on unknown/missing version (revMajor!=1 or revMinor>9 or revMinor missing).
void ReadVersion(const pugi::xml_node& root, OdrSideModel& model);

// Serialize `node` (including the element itself and its subtree) to a compact XML string.
std::string NodeToXml(const pugi::xml_node& node);

// ---- OdrCoverageAudit.cpp ----

// True if `path` is a whitelisted element path (exact match against the generated table).
bool IsWhitelistedPath(const std::string& path);

// True if `attr` is a read attribute of the whitelisted element at `path`. Requires that
// IsWhitelistedPath(path) is true.
bool IsWhitelistedAttr(const std::string& path, const std::string& attr);

// True if the element `path` was REMOVED in OpenDRIVE 1.6 (cluster 21 table).
bool IsRemovedIn16(const std::string& path);

// Walk the whole document from the OpenDRIVE root, filling model.audit (+ user_data/data_quality
// via the parser helpers). Sets `found_include` true if any <include> element was seen (the caller
// turns that into a hard error / false return). rev_minor drives removed-in-1.6 classification.
void RunCoverageWalk(const pugi::xml_node& root, OdrSideModel& model, bool& found_include);

}  // namespace detail
}  // namespace odr
}  // namespace gt_esmini
