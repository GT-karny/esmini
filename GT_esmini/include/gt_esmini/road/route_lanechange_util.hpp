/*
 * GT_esmini - route lane-change derivation helper (header-only)
 *
 * Shared by the GT_esminiRMLib C-API (GT_RM_CalcRoute) and the WASM embind
 * binding (GTRouteJS) so both report identical lane-change plans for a route
 * produced by roadmanager::LaneIndependentRouter.
 *
 * Header-only / inline: no separate translation unit, so no CMake changes are
 * needed on the GT_esminiLib side; the WASM build just compiles the includer.
 */

#pragma once

#include <vector>
#include <cstdint>

#include "LaneIndependentRouter.hpp"  // roadmanager::Node (full definition)

namespace gt_esmini
{
namespace route
{
    // A lane change required while driving along one road of the route.
    struct LaneChange
    {
        uint32_t roadId;      // road on which the change must happen
        double   s;           // road-entry s; change should complete before road end
        int      fromLaneId;  // lane entered on this road
        int      toLaneId;    // lane needed to connect onward
    };

    /**
     * Derive the lane-change plan from a LaneIndependentRouter path.
     *
     * The router explores every same-direction lane of each road, so for two
     * consecutive nodes (cur -> nxt) the lane actually used to leave cur.road
     * is stored as nxt.fromLaneId, while the lane entered on cur.road is
     * cur.currentLaneId. When they differ, a lane change is required on cur.road.
     *
     * NOTE: lane ids are compared as-is. On roads with multiple lane sections
     * the id of a physical lane can be renumbered between sections; for such
     * roads the comparison is approximate (flagged for verification).
     */
    inline std::vector<LaneChange> DeriveLaneChanges(const std::vector<roadmanager::Node>& path)
    {
        std::vector<LaneChange> changes;
        for (size_t i = 0; i + 1 < path.size(); ++i)
        {
            const roadmanager::Node& cur = path[i];
            const roadmanager::Node& nxt = path[i + 1];
            if (cur.road != nullptr && nxt.fromLaneId != cur.currentLaneId)
            {
                LaneChange lc;
                lc.roadId     = static_cast<uint32_t>(cur.road->GetId());
                lc.s          = 0.0;
                lc.fromLaneId = cur.currentLaneId;
                lc.toLaneId   = nxt.fromLaneId;
                changes.push_back(lc);
            }
        }
        return changes;
    }

}  // namespace route
}  // namespace gt_esmini
