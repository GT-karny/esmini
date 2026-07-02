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

#include "CommonMini.hpp"
#include "logger.hpp"  // LOG_WARN/LOG_INFO/LOG_ERROR (fmt-style)
#include "odr_side_internal.hpp"
#include "pugixml.hpp"

namespace gt_esmini
{
namespace odr
{

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
}  // namespace

bool BuildSideModel(const pugi::xml_document& doc, const void* opendrive_key)
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

        // P2: focused lane-detail pass (clusters 3+16 L1 storage; sparse on legacy assets).
        detail::ParseLaneExtras(root, *model);

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

bool BuildSideModel(const pugi::xml_document& doc, roadmanager::OpenDrive* od)
{
    // Same registration/audit semantics as the opaque-key overload (key = od). The fork hook
    // `BuildSideModel(doc, this)` binds HERE by exact match -- no fork change was needed for P2.
    const bool ok = BuildSideModel(doc, static_cast<const void*>(od));
    if (ok && od != nullptr)
    {
        const OdrSideModel* m = GetSideModel(od);
        if (m != nullptr)
        {
            // P2 border->width normalization through the public Lane API (plan P2; no-op when
            // no lane authored <border> -- keeps legacy parses bit-identical).
            detail::ApplyBorderWidths(*m, od);
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

void ClearSideModel(const void* opendrive_key)
{
    std::lock_guard<std::mutex> lock(RegistryMutex());
    Registry().erase(opendrive_key);
}

}  // namespace odr
}  // namespace gt_esmini
