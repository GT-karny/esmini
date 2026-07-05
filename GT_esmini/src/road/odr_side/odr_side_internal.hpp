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

// ---- OdrSideModel.cpp ----

// Non-const registry lookup (mirrors the public const GetSideModel). For in-TU passes that write
// synthesis products back into the model. The public API stays read-only.
OdrSideModel* GetSideModelMutable(const void* opendrive_key);

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

// ---- OdrLaneExtras.cpp (plan P2) ----

// Focused second pass over road/lanes/laneSection/{left,center,right}/lane, filling
// model.lane_extras (sparse: one entry per lane carrying at least one P2 datum). Walks the SAME
// <lanes> view RoadManager uses (SelectLanesLayer(road, opendrive_key)) so the extras and the runtime
// structure agree under a temporary lane-layer merge (plan P8 D6). `opendrive_key` is the parse key
// SelectLanesLayer caches its merged DOM under.
void ParseLaneExtras(const pugi::xml_node& root, OdrSideModel& model, const void* opendrive_key);

// Border->width normalization: for every lane in `model` that authored <border> elements and
// whose runtime Lane has zero LaneWidth records, synthesize width polynomials
// width = side_sign * (border_outer - border_inner) (piecewise cubic algebra) and inject them
// through the public Lane::AddLaneWidth API. Called only from the typed BuildSideModel overload.
void ApplyBorderWidths(const OdrSideModel& model, roadmanager::OpenDrive* od);

// ---- OdrLaneLayers.cpp (plan P8) ----

// Focused pass over each road's <lanes> layers: fills model.lane_layers (sparse: one entry per road
// that authored @layer or >1 <lanes>). Also records the active_mode resolved for this parse.
void ParseLaneLayers(const pugi::xml_node& root, OdrSideModel& model);

// Move any pending merged-<lanes> documents built for `opendrive_key` (by SelectLanesLayer) into the
// side model `model`, so they live as long as the model (longer than the fork's parse). Called from
// the typed BuildSideModel overload after the core build. No-op when nothing is pending (permanent
// mode / legacy assets never register a pending doc).
void MoveMergedLanesDocs(const void* opendrive_key, OdrSideModel& model);

// ---- OdrSignalExtras.cpp (P3 + P4) ----

// Collect <signal>/<dependency> + <signal>/<reference> (P3 cluster 12 L1) AND the P4 signal-namespace
// extras -- <semantics> (cluster 10), <staticBoard>/<vmsBoard>/<displayArea> (cluster 13) -- into
// model.signal_extras (sparse: one entry per signal carrying any such child).
void CollectSignalExtras(const pugi::xml_node& root, OdrSideModel& model);

// Collect the P4 document/header-level extras: root <vmsGroup> (cluster 13) and
// header/license + header/defaultRegulations (cluster 14, each regulation reusing the <semantics>
// content model). Sparse: nothing stored when the elements are absent.
void CollectHeaderAndGroupExtras(const pugi::xml_node& root, OdrSideModel& model);

// Parse a <semantics> node into `out` (shared by signals and header regulations). Both the 1.9/1.8
// attribute-form <speed> and a hypothesised child-element form normalize to the same OdrSemanticSpeed.
void ParseSemantics(const pugi::xml_node& semantics_node, OdrSemantics& out);

// ---- OdrJunctionExtras.cpp (P5) ----

// Focused pass over <junction> children, filling model.junction_extras (sparse: one entry per
// junction carrying crossPath / roadSection / priority / controller / laneLink-layer data).
// Clusters 5 (crossPath/roadSection), 7 (priority + laneLink overlapZone), 22 (laneLink layers).
void ParseJunctionExtras(const pugi::xml_node& root, OdrSideModel& model);

// P5 stage 2: for every parsed crossPath (any junction type) synthesize a closed 4-corner CROSSWALK
// RMObject straddling the crossed road and register it via the public Road::AddObject API, and store
// a sampled crossing-road centerline polyline back into the model's crossPath records. Mutates `od`
// only (adds objects), and writes synth_object_id / ped_path into `model`. Called only from the
// typed BuildSideModel overload. No-op when the model carries no crossPath (legacy assets).
void SynthesizeCrosswalks(OdrSideModel& model, roadmanager::OpenDrive* od);

// ---- OdrRailroad.cpp (P9a cluster 20) ----

// Focused pass over each <road>/<railroad>/<switch> (per-road railway switches) and each root-level
// <station> (platforms/segments), filling model.rail_switches / model.stations. L1 storage only,
// INERT (no runtime consumer). An empty <railroad/> stores nothing; a road/station with no relevant
// children produces no entry (keeps the side model sparse on legacy assets).
void ParseRailroad(const pugi::xml_node& root, OdrSideModel& model);

// ---- OdrObjectExtras.cpp (P7 clusters 17/18/19) ----

// Focused pass over road/objects children (object/objectReference/bridge), road/surface, and
// road/lateralProfile, filling model.object_extras / model.road_surface_crgs / model.road_lateral.
// `doc_dir` is the directory of the xodr (for CRG file-existence diagnostics; "" -> skip the check).
void ParseObjectExtras(const pugi::xml_node& root, OdrSideModel& model, const std::string& doc_dir);

// P7 stage 2 (typed overload only): synthesize BRIDGE + objectReference clone RMObjects and apply the
// lateralProfile shape/crossSectionSurface -> equivalent superelevation degrade. Mutates `od`
// (adds objects / superelevation records) and writes synth ids / degrade bookkeeping into `model`.
// No-op on legacy assets (no bridge/objectReference/shape/crossSectionSurface).
void SynthesizeBridges(OdrSideModel& model, roadmanager::OpenDrive* od);
void SynthesizeObjectReferences(OdrSideModel& model, roadmanager::OpenDrive* od);
void ApplyLateralProfileDegrade(OdrSideModel& model, roadmanager::OpenDrive* od);

// ---- OdrJunctionGeom.cpp (P7 clusters 8/9) ----

// Focused pass over <junction> children (boundary/elevationGrid/objects/surface) + document-level
// <junctionGroup>, filling model.junction_geom / model.junction_groups. `doc_dir` as above (CRG).
void ParseJunctionGeom(const pugi::xml_node& root, OdrSideModel& model, const std::string& doc_dir);

}  // namespace detail
}  // namespace odr
}  // namespace gt_esmini
