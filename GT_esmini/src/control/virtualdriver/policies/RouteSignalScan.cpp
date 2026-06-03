#include "gt_esmini/control/virtualdriver/policies/RouteSignalScan.hpp"

#include "Entities.hpp"
#include "RoadManager.hpp"

#include <algorithm>
#include <cmath>

using namespace scenarioengine;

namespace gt_esmini
{

namespace
{
// Does `sig` face traffic travelling in the given s-direction on its road?
// Orientation POSITIVE applies to +s travel, NEGATIVE to -s, NONE to both.
bool SignalFacesTravel(const roadmanager::Signal* sig, double ds_dir)
{
    switch (sig->GetOrientation())
    {
        case roadmanager::RoadObject::Orientation::POSITIVE: return ds_dir > 0.0;
        case roadmanager::RoadObject::Orientation::NEGATIVE: return ds_dir < 0.0;
        case roadmanager::RoadObject::Orientation::NONE:     return true;
        default:                                             return true;
    }
}

// Does `sig` apply to the lane the ego is on at the crossing point? An empty
// validity list means "all lanes".
bool SignalAppliesToLane(const roadmanager::Signal* sig, id_t lane_global_id)
{
    const std::vector<id_t> valid = const_cast<roadmanager::Signal*>(sig)->GetAllValidGlobalLanes();
    if (valid.empty()) return true;
    return std::find(valid.begin(), valid.end(), lane_global_id) != valid.end();
}
}  // namespace

std::vector<ScannedSignal> ScanSignalsAhead(Object* ego, double lookahead, double step)
{
    std::vector<ScannedSignal> out;
    if (!ego) return out;

    roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();
    if (!odr) return out;

    step = std::max(0.5, step);

    // Isolated copy of the ego route (same idiom as the planner).
    roadmanager::Position pos;
    pos.Duplicate(ego->pos_);
    pos.CopyRoute(ego->pos_);

    id_t   prev_track = pos.GetTrackId();
    double prev_s     = pos.GetS();
    double traveled   = 0.0;

    while (traveled < lookahead)
    {
        const int ret = static_cast<int>(pos.MoveAlongS(step));
        if (ret < 0) break;  // end of route / off-route
        traveled += step;

        const id_t   cur_track = pos.GetTrackId();
        const double cur_s     = pos.GetS();

        // Only test signal crossings while staying on the same road; a road change
        // resets the s reference (the one-step gap at the boundary is negligible).
        if (cur_track == prev_track)
        {
            roadmanager::Road* road = odr->GetRoadById(cur_track);
            if (road)
            {
                const double lo     = std::min(prev_s, cur_s);
                const double hi      = std::max(prev_s, cur_s);
                const double ds_dir = cur_s - prev_s;
                const id_t   lane_g = pos.GetLaneGlobalId();

                const unsigned int n = road->GetNumberOfSignals();
                for (unsigned int i = 0; i < n; ++i)
                {
                    roadmanager::Signal* sig = road->GetSignal(i);
                    if (!sig) continue;
                    const double ss = sig->GetS();
                    if (ss < lo || ss > hi) continue;             // not crossed this step
                    if (!SignalFacesTravel(sig, ds_dir)) continue;
                    if (!SignalAppliesToLane(sig, lane_g)) continue;

                    // Distance ahead ≈ traveled, corrected to the exact s within the step.
                    const double frac = (hi > lo) ? (std::fabs(ss - prev_s) / (hi - lo)) : 0.0;
                    const double dist = (traveled - step) + frac * step;
                    out.push_back({sig, std::max(0.0, dist)});
                }
            }
        }

        prev_track = cur_track;
        prev_s     = cur_s;
    }

    std::sort(out.begin(), out.end(),
              [](const ScannedSignal& a, const ScannedSignal& b) { return a.distance_ahead < b.distance_ahead; });
    return out;
}

}  // namespace gt_esmini
