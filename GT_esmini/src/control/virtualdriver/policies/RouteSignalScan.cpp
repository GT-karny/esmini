#include "gt_esmini/control/virtualdriver/policies/RouteSignalScan.hpp"

#include "Entities.hpp"
#include "RoadManager.hpp"
#include "gt_esmini/road/OdrSideModel.hpp"

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

    id_t   prev_track  = pos.GetTrackId();
    double prev_s      = pos.GetS();
    id_t   prev_lane_g = pos.GetLaneGlobalId();
    double traveled    = 0.0;

    // Travel direction (sign of ds) on the current road. Seeded from the ego's
    // heading relative to its road, then tracked from actual s deltas.
    double dir = (std::cos(ego->pos_.GetHRelative()) >= 0.0) ? 1.0 : -1.0;

    // Test all signals of `road` with s inside [s_from, s_to] (any order), facing
    // travel direction `ds_dir`, applying to lane `lane_g`. `dist_at_s_from` is the
    // route distance of the s_from end; signal distance = that + |ss - s_from|.
    auto scanSegment =
        [&out, odr](roadmanager::Road* road, double s_from, double s_to, double ds_dir, id_t lane_g, double dist_at_s_from)
    {
        if (!road) return;
        const double lo = std::min(s_from, s_to);
        const double hi = std::max(s_from, s_to);

        const unsigned int n = road->GetNumberOfSignals();
        for (unsigned int i = 0; i < n; ++i)
        {
            roadmanager::Signal* sig = road->GetSignal(i);
            if (!sig) continue;
            const double ss = sig->GetS();
            if (ss < lo || ss > hi) continue;             // not inside this segment
            if (!SignalFacesTravel(sig, ds_dir)) continue;
            if (!SignalAppliesToLane(sig, lane_g)) continue;

            // P8: invalidated (1.9) -> excluded from the VD signal scan (see gt_roadmanager_patches.md P8).
            // Covers all downstream policies (StopYieldSignAware / TrafficLightAware consume this output).
            // Signals without stored extras return nullptr and are kept unchanged.
            const gt_esmini::odr::OdrSignalExtras* sx = gt_esmini::odr::GetSignalExtras(odr, sig);
            if (sx != nullptr && sx->invalidated) continue;

            out.push_back({sig, std::max(0.0, dist_at_s_from + std::fabs(ss - s_from))});
        }
    };

    while (traveled < lookahead)
    {
        const int ret = static_cast<int>(pos.MoveAlongS(step));
        if (ret < 0) break;  // end of route / off-route
        traveled += step;

        const id_t   cur_track  = pos.GetTrackId();
        const double cur_s      = pos.GetS();
        const id_t   cur_lane_g = pos.GetLaneGlobalId();

        if (cur_track == prev_track)
        {
            scanSegment(odr->GetRoadById(cur_track), prev_s, cur_s, cur_s - prev_s, cur_lane_g, traveled - step);
            if (std::fabs(cur_s - prev_s) > 1e-9)
            {
                dir = (cur_s > prev_s) ? 1.0 : -1.0;
            }
        }
        else
        {
            // Road boundary crossed inside this step. The naive same-road test above
            // would skip the residual segments entirely, so a signal in the last
            // <step metres of a road (the typical stop-line placement before a
            // junction) would flicker in and out of detection as the sampling phase
            // shifts with ego motion (audit VD-7). Test both residuals explicitly.

            // 1) Tail of the previous road: from prev_s to the boundary end in the
            //    direction of travel.
            double             consumed  = 0.0;  // route metres spent on the previous road this step
            roadmanager::Road* prev_road = odr->GetRoadById(prev_track);
            if (prev_road)
            {
                const double end_s = (dir > 0.0) ? prev_road->GetLength() : 0.0;
                scanSegment(prev_road, prev_s, end_s, dir, prev_lane_g, traveled - step);
                consumed = std::min(std::fabs(end_s - prev_s), step);
            }

            // 2) Head of the new road: from its entry end to cur_s. With step small
            //    relative to road length the sample lies within `step` of the entry
            //    end, so the nearer end identifies where we came in. (Roads shorter
            //    than ~2*step may pick the wrong end; the error is bounded by the
            //    road length. Roads shorter than the remaining step that MoveAlongS
            //    jumped over entirely are not scanned — known limitation.)
            roadmanager::Road* cur_road = odr->GetRoadById(cur_track);
            if (cur_road)
            {
                const double len     = cur_road->GetLength();
                const double new_dir = (cur_s <= len - cur_s) ? 1.0 : -1.0;
                const double entry_s = (new_dir > 0.0) ? 0.0 : len;
                scanSegment(cur_road, entry_s, cur_s, new_dir, cur_lane_g, (traveled - step) + consumed);
                dir = new_dir;
            }
        }

        prev_track  = cur_track;
        prev_s      = cur_s;
        prev_lane_g = cur_lane_g;
    }

    std::sort(out.begin(), out.end(),
              [](const ScannedSignal& a, const ScannedSignal& b) { return a.distance_ahead < b.distance_ahead; });
    return out;
}

}  // namespace gt_esmini
