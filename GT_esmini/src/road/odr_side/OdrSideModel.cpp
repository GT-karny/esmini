// OdrSideModel.cpp -- registry + orchestration for the GT OpenDRIVE side model.
//
// Plan: GT_esmini/docs/opendrive_16_19_support_plan.md P1.
//
// Compiled INTO the upstream RoadManager static target (R1 exception, user-approved 2026-07-02;
// see EnvironmentSimulator/Modules/RoadManager/CMakeLists.txt marker "# [GT_ODR:cmake]").
// NOT part of GT_esminiLib's own source list -> no double compilation.
#include "gt_esmini/road/OdrSideModel.hpp"

#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "CommonMini.hpp"
#include "RoadManager.hpp"  // roadmanager::OpenDrive::GetOpenDriveFilename (typed overload)
#include "logger.hpp"  // LOG_WARN/LOG_INFO/LOG_ERROR (fmt-style)
#include "odr_side_internal.hpp"
#include "pugixml.hpp"

namespace gt_esmini
{
namespace odr
{

// Out-of-line so the pugi::xml_document unique_ptr members (merged_lanes_docs) can stay behind a
// forward declaration in the public header (this TU includes pugixml.hpp -> complete type here).
OdrSideModel::OdrSideModel()  = default;
OdrSideModel::~OdrSideModel() = default;

namespace
{
// Instance-keyed registry. A std::map (not unordered) keeps behavior deterministic and needs no
// hashing of the opaque pointer. Guarded by a mutex for the (unlikely) concurrent-parse case.
std::map<const void*, std::unique_ptr<OdrSideModel>>& Registry()
{
    static std::map<const void*, std::unique_ptr<OdrSideModel>> registry;
    return registry;
}

std::mutex& RegistryMutex()
{
    static std::mutex m;
    return m;
}

// P8 (plan D3): pending merged-<lanes> DOM documents built by SelectLanesLayer BEFORE the side model
// exists (the fork walks lanes at :4093, long before the [GT_ODR:hook] BuildSideModel call). Keyed by
// (opendrive_key -> road_id -> document). Guarded by the SAME RegistryMutex(). MoveMergedLanesDocs
// drains a key's map into the OdrSideModel when the model is built; a parse aborted before that leaves
// stale entries which the NEXT parse for the same key overwrites/drains (harmless -- documented D3).
std::map<const void*, std::map<std::string, std::unique_ptr<pugi::xml_document>>>& PendingMergedLanes()
{
    static std::map<const void*, std::map<std::string, std::unique_ptr<pugi::xml_document>>> pending;
    return pending;
}
}  // namespace

namespace detail
{
// Cross-TU access to the pending merged-lanes registry (SelectLanesLayer in OdrLaneLayers.cpp needs
// to insert/lookup; both share RegistryMutex via these helpers).
std::mutex& MergedLanesMutex();
std::map<std::string, std::unique_ptr<pugi::xml_document>>& PendingMergedLanesFor(const void* opendrive_key);
}  // namespace detail

namespace
{
// Directory containing `path` (POSIX/Windows separators), or "" when none. Used for the CRG
// file-existence diagnostic; kept dependency-free (no <filesystem> at this layer).
std::string DirOf(const std::string& path)
{
    const auto slash = path.find_last_of("/\\");
    return (slash == std::string::npos) ? std::string() : path.substr(0, slash);
}

// Core builder shared by both public overloads. `doc_dir` = directory of the source xodr ("" when
// unknown -> CRG existence checks are skipped). The typed overload additionally runs the mutating
// stage-2 passes on `od` after this returns.
bool BuildSideModelCore(const pugi::xml_document& doc, const void* opendrive_key, const std::string& doc_dir)
{
    auto model = std::make_unique<OdrSideModel>();

    // The OpenDRIVE root. Tolerate documents whose root is something else (nothing to audit).
    pugi::xml_node root = doc.child("OpenDRIVE");
    if (!root)
    {
        LOG_WARN("[GT_ODR] BuildSideModel: no <OpenDRIVE> root element found; empty side model");
    }
    else
    {
        detail::ReadVersion(root, *model);

        bool found_include = false;
        detail::RunCoverageWalk(root, *model, found_include);
        detail::CollectSignalExtras(root, *model);  // P3 cluster 12 + P4 clusters 10/13 signal L1
        detail::CollectHeaderAndGroupExtras(root, *model);  // P4 clusters 13/14 (vmsGroup / header)

        // P2: focused lane-detail pass (clusters 3+16 L1 storage; sparse on legacy assets). Walks the
        // same SelectLanesLayer view RoadManager uses (P8 D6).
        detail::ParseLaneExtras(root, *model, opendrive_key);

        // P5: junction pass (clusters 5/7/22 L1: crossPath/roadSection/priority/laneLink layers).
        detail::ParseJunctionExtras(root, *model);

        // P9a: railroad/station pass (cluster 20 L1: <road>/<railroad>/<switch> + root <station>).
        // Storage only, INERT (no runtime consumer); no stage-2 synthesis.
        detail::ParseRailroad(root, *model);

        // P8: 1.9 lane-layer shadow storage (clusters 4/22 L1: multi-<lanes>/@layer). Sparse.
        detail::ParseLaneLayers(root, *model);

        // P7: object family + road surface/lateralProfile (clusters 17/18/19 L1) and junction
        // geometry + junctionGroup (clusters 8/9 L1). Pure storage; stage-2 synthesis is separate.
        detail::ParseObjectExtras(root, *model, doc_dir);
        detail::ParseJunctionGeom(root, *model, doc_dir);

        // Register BEFORE returning (even on include hard-error) so diagnostics/stats are queryable.
        {
            std::lock_guard<std::mutex> lock(RegistryMutex());
            Registry()[opendrive_key] = std::move(model);  // clears any prior entry for this key
        }

        if (found_include)
        {
            // Hard error by design (plan P1). Diagnostics already logged during the walk.
            return false;
        }
        return true;
    }

    // No root: still register the (empty) model so a subsequent Get is well-defined.
    {
        std::lock_guard<std::mutex> lock(RegistryMutex());
        Registry()[opendrive_key] = std::move(model);
    }
    return true;
}
}  // namespace

bool BuildSideModel(const pugi::xml_document& doc, const void* opendrive_key)
{
    // Opaque-key entry: no source path available, so CRG existence checks are skipped ("" doc_dir).
    return BuildSideModelCore(doc, opendrive_key, std::string());
}

bool BuildSideModel(const pugi::xml_document& doc, roadmanager::OpenDrive* od)
{
    // Same registration/audit semantics as the opaque-key overload (key = od). The fork hook
    // `BuildSideModel(doc, this)` binds HERE by exact match -- no fork change was needed for P2.
    const std::string doc_dir = (od != nullptr) ? DirOf(od->GetOpenDriveFilename()) : std::string();
    const bool        ok      = BuildSideModelCore(doc, static_cast<const void*>(od), doc_dir);
    if (ok && od != nullptr)
    {
        OdrSideModel* m = detail::GetSideModelMutable(od);
        if (m != nullptr)
        {
            // P8 (plan D3): adopt any merged-<lanes> DOM documents SelectLanesLayer built during the
            // fork parse (temporary mode) so they outlive the parse alongside the model.
            detail::MoveMergedLanesDocs(od, *m);

            // P2 border->width normalization through the public Lane API (plan P2; no-op when
            // no lane authored <border> -- keeps legacy parses bit-identical).
            detail::ApplyBorderWidths(*m, od);

            // P5 stage 2: crossPath -> synthesized CROSSWALK RMObject + PedPath polyline (no-op when
            // no crossPath was parsed -- keeps legacy parses bit-identical).
            detail::SynthesizeCrosswalks(*m, od);

            // P7 stage 2: bridge/objectReference synthesis + lateralProfile degrade (no-op on legacy
            // assets -- keeps parses bit-identical when none of these elements were authored).
            detail::SynthesizeBridges(*m, od);
            detail::SynthesizeObjectReferences(*m, od);
            detail::ApplyLateralProfileDegrade(*m, od);
        }
    }
    return ok;
}

const OdrSideModel* GetSideModel(const void* opendrive_key)
{
    std::lock_guard<std::mutex> lock(RegistryMutex());
    auto&                       reg = Registry();
    auto                        it  = reg.find(opendrive_key);
    return it == reg.end() ? nullptr : it->second.get();
}

namespace detail
{
// Non-const registry lookup for in-TU passes that write synthesis products back into the model
// (P5 crosswalk synth_object_id / ped_path). Not exported: the public API stays read-only.
OdrSideModel* GetSideModelMutable(const void* opendrive_key)
{
    std::lock_guard<std::mutex> lock(RegistryMutex());
    auto&                       reg = Registry();
    auto                        it  = reg.find(opendrive_key);
    return it == reg.end() ? nullptr : it->second.get();
}

// P8 (plan D3): the pending merged-lanes registry shares RegistryMutex(). SelectLanesLayer (in
// OdrLaneLayers.cpp) locks it via these accessors while it inserts/looks up a per-road document.
std::mutex& MergedLanesMutex()
{
    return RegistryMutex();
}

std::map<std::string, std::unique_ptr<pugi::xml_document>>& PendingMergedLanesFor(const void* opendrive_key)
{
    // Caller holds MergedLanesMutex(). Returns (creating if absent) the per-road map for this key.
    return PendingMergedLanes()[opendrive_key];
}

void MoveMergedLanesDocs(const void* opendrive_key, OdrSideModel& model)
{
    std::lock_guard<std::mutex> lock(RegistryMutex());
    auto&                       pending = PendingMergedLanes();
    auto                        it      = pending.find(opendrive_key);
    if (it == pending.end())
    {
        return;  // permanent mode / legacy assets: nothing pending
    }
    model.merged_lanes_docs = std::move(it->second);
    pending.erase(it);
}
}  // namespace detail

void ClearSideModel(const void* opendrive_key)
{
    std::lock_guard<std::mutex> lock(RegistryMutex());
    Registry().erase(opendrive_key);
    PendingMergedLanes().erase(opendrive_key);  // P8 D3: clear both
}

}  // namespace odr
}  // namespace gt_esmini
