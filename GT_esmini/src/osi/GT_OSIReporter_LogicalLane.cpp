/*
 * GT_esmini extension -- OSI logical lane post-pass (S0 scaffolding).
 *
 * See gt_esmini/osi/GT_OsiLogicalLane.hpp for why this translation unit lives in
 * the ScenarioEngine swap zone rather than in GT_esminiLib, and why the pass runs
 * last in the static ground-truth build.
 *
 * S0 emits NOTHING. The whole point of this stage is that the flag can be flipped
 * with the serialized static GroundTruth staying byte-for-byte identical, which is
 * what scripts/probe_osi_logical_lane_size.py measures. The emit arrives in S1
 * (reference lines + lane bodies), S2 (connectivity) and S3 (boundaries).
 *
 * Design: GT_esmini/docs/osi/logical_lane_and_route_design.md 3 / 4 / 7
 * Knowledge graph: spine-work:osi-logical-lane
 */
#include "gt_esmini/osi/GT_OsiLogicalLane.hpp"

#include "CommonMini.hpp"
#include "RoadManager.hpp"
#include "GT_OSIReporter_Internals.hpp"

#include <cctype>
#include <cstdlib>
#include <string>

namespace gt_esmini
{
namespace osi
{

namespace
{
// ---- feature flag (gt_esmini::odr::GetUseAuthoredJunctionBoundary idiom:
//      env read once on first query, setter overrides so tests are deterministic) ----
bool g_use_logical_lane        = false;
bool g_use_logical_lane_inited = false;

// Same accepted-token set as the odr-side flag. Deliberately duplicated rather
// than shared: that helper sits in an anonymous namespace in
// odr_side/OdrJunctionGeom.cpp, and exporting it would create a cross-module
// dependency for four lines of string comparison.
bool EnvIsTruthy(const char* v)
{
    if (v == nullptr || v[0] == '\0')
    {
        return false;
    }
    std::string s(v);
    for (char& c : s)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s == "1" || s == "true" || s == "yes" || s == "on";
}

LogicalLaneIndex g_logical_lane_index;
}  // namespace

void SetUseOsiLogicalLane(bool on)
{
    g_use_logical_lane        = on;
    g_use_logical_lane_inited = true;  // suppress the env read so tests are deterministic
}

bool GetUseOsiLogicalLane()
{
    if (!g_use_logical_lane_inited)
    {
        g_use_logical_lane        = EnvIsTruthy(std::getenv("GT_OSI_LOGICAL_LANE"));
        g_use_logical_lane_inited = true;
    }
    return g_use_logical_lane;
}

const LogicalLaneIndex& GetLogicalLaneIndex()
{
    return g_logical_lane_index;
}

void BuildOsiLogicalLanes(roadmanager::OpenDrive* opendrive)
{
    // Cleared unconditionally, before the flag check: a stale index from an
    // earlier load inside the same process must not survive into a run with the
    // feature OFF, where every lookup has to miss.
    g_logical_lane_index.clear();

    if (opendrive == nullptr || !GetUseOsiLogicalLane())
    {
        return;
    }

    if (obj_osi_internal.static_gt == nullptr)
    {
        // The reporter allocates static_gt in its constructor, so the real call site
        // can never see null. A unit test that pokes this function without a live
        // OSIReporter can, and must not crash on it.
        LOG_WARN("[GT_OSI:logical-lane] post-pass ran before the OSI reporter allocated its static GroundTruth -- skipped");
        return;
    }

    // S0 has nothing to emit. This line is the only observable difference between
    // OFF and ON, and it exists on purpose: without it, "the bytes are identical"
    // would also be true of a flag that is never read at all, and the probe could
    // not tell a wired gate from a dead one.
    LOG_INFO("[GT_OSI:logical-lane] post-pass enabled (S0: emits nothing) -- roads={} static lane={} lane_boundary={}",
             opendrive->GetNumOfRoads(),
             obj_osi_internal.static_gt->lane_size(),
             obj_osi_internal.static_gt->lane_boundary_size());
}

}  // namespace osi
}  // namespace gt_esmini
