/*
 * GT_esmini extension -- osi3::Route construction + L2 route progress.
 *
 * See gt_esmini/osi/RouteToOsiRoute.hpp for the layering and the module-dependency
 * note. Everything here is pure: const roadmanager inputs, no logging, no state.
 *
 * Design: GT_esmini/docs/osi/logical_lane_and_route_design.md 2-5 / 2-6-2 / 5
 * Knowledge graph: spine-work:osi-logical-lane
 */
#include "gt_esmini/osi/RouteToOsiRoute.hpp"

#include "RoadManager.hpp"
#include "osi_route.pb.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace gt_esmini
{
namespace osi
{

namespace
{
// A clipped stretch shorter than this is dropped rather than emitted. Lane section
// boundaries land exactly on the clip bounds all the time (the ego sitting at s=0 of a
// road, a destination waypoint at a section seam), and a zero-length RouteSegment is
// not a shorter route -- it is a segment the consumer cannot traverse.
constexpr double kMinSegmentLength = 1e-6;

// Map ONE band lane id onto every lane section of its road, in a single pass out from
// the section the band evaluated it in.
//
// Road::GetConnectedLaneIdAtS() is not used for this: its walk compares s against the
// START of the section it has already stepped off (GT_RoadManager.cpp
// Road::GetConnectedLaneIdAtS), so on a road with three or more lane sections it
// overshoots and reports the id in the LAST section regardless of s_target. Walking the
// links here is also linear rather than quadratic, which matters on the long routes that
// are the whole reason the HVD transport had to be fragmented.
//
// A section the chain cannot reach gets 0. LaneSection::GetConnectingLaneId returns the
// INCOMING id unchanged when a lane carries no link ("stay on same index"), so each step
// is checked against the next section's actual lane list -- otherwise a missing link
// would silently hand back an id that exists nowhere.
std::vector<int> MapLaneIdAcrossSections(const roadmanager::Road* road, unsigned exit_idx, int lane_id)
{
    const unsigned  n = road->GetNumberOfLaneSections();
    std::vector<int> ids(n, 0);
    if (n == 0 || exit_idx >= n || lane_id == 0)
    {
        return ids;
    }

    roadmanager::LaneSection* exit_sec = road->GetLaneSectionByIdx(exit_idx);
    if (exit_sec == nullptr || exit_sec->GetLaneById(lane_id) == nullptr)
    {
        return ids;
    }
    ids[exit_idx] = lane_id;

    // Backwards: section j's PREDECESSOR link names the lane in section j-1.
    for (unsigned j = exit_idx; j > 0; j--)
    {
        roadmanager::LaneSection* here = road->GetLaneSectionByIdx(j);
        roadmanager::LaneSection* prev = road->GetLaneSectionByIdx(j - 1);
        if (ids[j] == 0 || here == nullptr || prev == nullptr)
        {
            break;
        }
        const int candidate = here->GetConnectingLaneId(ids[j], roadmanager::LinkType::PREDECESSOR);
        if (candidate == 0 || prev->GetLaneById(candidate) == nullptr)
        {
            break;
        }
        ids[j - 1] = candidate;
    }

    // Forwards: section j's SUCCESSOR link names the lane in section j+1.
    for (unsigned j = exit_idx; j + 1 < n; j++)
    {
        roadmanager::LaneSection* here = road->GetLaneSectionByIdx(j);
        roadmanager::LaneSection* next = road->GetLaneSectionByIdx(j + 1);
        if (ids[j] == 0 || here == nullptr || next == nullptr)
        {
            break;
        }
        const int candidate = here->GetConnectingLaneId(ids[j], roadmanager::LinkType::SUCCESSOR);
        if (candidate == 0 || next->GetLaneById(candidate) == nullptr)
        {
            break;
        }
        ids[j + 1] = candidate;
    }

    return ids;
}

double SectionEndS(const roadmanager::Road* road, unsigned idx)
{
    const unsigned n = road->GetNumberOfLaneSections();
    if (idx + 1 < n)
    {
        roadmanager::LaneSection* next = road->GetLaneSectionByIdx(idx + 1);
        if (next != nullptr)
        {
            return next->GetS();
        }
    }
    return road->GetLength();
}
}  // namespace

// ---------------------------------------------------------------------------
// route identity
// ---------------------------------------------------------------------------

bool RouteSignature::operator==(const RouteSignature& o) const
{
    // Exact double comparison is what is wanted here: these s values are copied
    // straight out of the waypoint Positions, so "the same route" reproduces them
    // bit for bit, and a tolerance would only hide a route that genuinely moved its
    // start or destination by a hair.
    return n_waypoints == o.n_waypoints && first_road == o.first_road && last_road == o.last_road &&
           first_lane == o.first_lane && last_lane == o.last_lane && first_s == o.first_s && last_s == o.last_s;
}

RouteSignature MakeRouteSignature(const roadmanager::Route& route)
{
    RouteSignature sig;
    const std::vector<roadmanager::Position>& wps = route.minimal_waypoints_;
    sig.n_waypoints                               = wps.size();
    if (wps.empty())
    {
        return sig;
    }
    sig.first_road = wps.front().GetTrackId();
    sig.first_lane = wps.front().GetLaneId();
    sig.first_s    = wps.front().GetS();
    sig.last_road  = wps.back().GetTrackId();
    sig.last_lane  = wps.back().GetLaneId();
    sig.last_s     = wps.back().GetS();
    return sig;
}

// ---------------------------------------------------------------------------
// band (per road) -> segment (per lane section)
// ---------------------------------------------------------------------------

std::vector<RouteSectionSegment> ExpandRouteLanePlan(const roadmanager::Route&    route,
                                                     const roadmanager::Position& ego,
                                                     const RouteLanePlan&         plan,
                                                     RouteExpansionStart          start)
{
    std::vector<RouteSectionSegment> segments;

    if (!plan.valid || plan.bands.empty())
    {
        return segments;
    }
    roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();
    if (odr == nullptr)
    {
        return segments;
    }

    // Where the emitted route starts. A road may legally appear twice on a looping
    // route; the first match wins, which under-reports the remaining route rather
    // than over-reporting it.
    std::size_t start_band  = 0;
    bool        ego_on_plan = false;
    if (start == RouteExpansionStart::EgoPosition)
    {
        for (std::size_t i = 0; i < plan.bands.size(); i++)
        {
            if (plan.bands[i].road_id == ego.GetTrackId())
            {
                start_band  = i;
                ego_on_plan = true;
                break;
            }
        }
    }

    // The destination. bands.back().exit_s is the terminal waypoint's own s (the
    // final band is seeded straight from it -- RouteLanePlan.cpp BuildBandsFromSkeleton),
    // so this is the plan's own statement of where the route ends rather than a
    // second, possibly disagreeing, read of the waypoint list.
    const double route_end_s = plan.bands.back().exit_s;

    // Fallback start for an ego that is not on the plan at all: the route's own first
    // waypoint. Without it an off-route ego would get the whole first road, including
    // the stretch before the route even begins.
    const double route_start_s = route.minimal_waypoints_.empty() ? 0.0 : route.minimal_waypoints_.front().GetS();

    for (std::size_t bi = start_band; bi < plan.bands.size(); bi++)
    {
        const RouteLaneBand& band = plan.bands[bi];
        roadmanager::Road*   road = odr->GetRoadById(band.road_id);
        if (road == nullptr)
        {
            continue;
        }

        // The band's exit end IS the direction of travel on this road: the plan sets
        // exit_at_road_end from which of the road's own links leads onward
        // (RouteLanePlan.cpp ResolveHop: SUCCESSOR -> leaves at s=length -> drives +s).
        const bool     plus_s = band.exit_at_road_end;
        const unsigned n_sec  = road->GetNumberOfLaneSections();
        if (n_sec == 0)
        {
            continue;
        }

        // Clip window in ROAD s (lo <= hi regardless of travel direction).
        double lo = 0.0;
        double hi = road->GetLength();
        if (bi == start_band)
        {
            const double s0 = ego_on_plan ? ego.GetS() : route_start_s;
            // RouteStart mode always lands on route_start_s here: ego_on_plan is only
            // ever set in EgoPosition mode.
            if (plus_s)
            {
                lo = std::max(lo, s0);
            }
            else
            {
                hi = std::min(hi, s0);
            }
        }
        if (bi + 1 == plan.bands.size())
        {
            if (plus_s)
            {
                hi = std::min(hi, route_end_s);
            }
            else
            {
                lo = std::max(lo, route_end_s);
            }
        }
        if (hi - lo <= kMinSegmentLength)
        {
            continue;  // nothing of this road is still ahead of the ego
        }

        // The band's lane ids are valid at exit_s and nowhere else, so every section
        // resolves from there.
        const idx_t exit_idx_raw = road->GetLaneSectionIdxByS(band.exit_s);
        if (exit_idx_raw == IDX_UNDEFINED)
        {
            continue;
        }
        const unsigned exit_idx = static_cast<unsigned>(exit_idx_raw);

        // One walk per band lane, reused by every section below.
        std::vector<std::vector<int>> lane_by_section;
        lane_by_section.reserve(band.lanes.size());
        for (int band_lane : band.lanes)
        {
            lane_by_section.push_back(MapLaneIdAcrossSections(road, exit_idx, band_lane));
        }

        for (unsigned step = 0; step < n_sec; step++)
        {
            const unsigned            j    = plus_s ? step : (n_sec - 1 - step);
            roadmanager::LaneSection* lsec = road->GetLaneSectionByIdx(j);
            if (lsec == nullptr)
            {
                continue;
            }
            const double a = std::max(lsec->GetS(), lo);
            const double b = std::min(SectionEndS(road, j), hi);
            if (b - a <= kMinSegmentLength)
            {
                continue;  // section lies entirely outside the clip window
            }

            RouteSectionSegment seg;
            seg.road_id     = band.road_id;
            seg.section_idx = j;
            // Travel order. This is the one place the start_s > end_s convention of
            // osi_route.proto is produced; everything downstream just copies it.
            seg.start_s = plus_s ? a : b;
            seg.end_s   = plus_s ? b : a;

            for (const std::vector<int>& per_section : lane_by_section)
            {
                const int lane_here = per_section[j];
                if (lane_here != 0 && std::find(seg.lanes.begin(), seg.lanes.end(), lane_here) == seg.lanes.end())
                {
                    seg.lanes.push_back(lane_here);
                }
            }
            if (!seg.lanes.empty())
            {
                segments.push_back(std::move(seg));
            }
        }
    }

    return segments;
}

// ---------------------------------------------------------------------------
// L2
// ---------------------------------------------------------------------------

RouteProgress ComputeRouteProgress(const std::vector<RouteSectionSegment>& segments, const roadmanager::Position& ego)
{
    RouteProgress progress;

    const std::uint32_t ego_road = ego.GetTrackId();
    const double        ego_s    = ego.GetS();

    double cumulative = 0.0;
    for (std::size_t i = 0; i < segments.size(); i++)
    {
        const RouteSectionSegment& seg = segments[i];
        const double               len = std::fabs(seg.end_s - seg.start_s);

        if (!progress.on_route && seg.road_id == ego_road)
        {
            const double low  = std::min(seg.start_s, seg.end_s);
            const double high = std::max(seg.start_s, seg.end_s);
            if (ego_s >= low - kMinSegmentLength && ego_s <= high + kMinSegmentLength)
            {
                progress.on_route      = true;
                progress.segment_index = i;
                progress.s_along_route = cumulative + std::fabs(ego_s - seg.start_s);
            }
        }
        cumulative += len;
    }

    // The loop above never breaks early: route_length is the whole route's length and
    // has to be summed even after the ego's segment is found.
    progress.route_length = cumulative;
    return progress;
}

// ---------------------------------------------------------------------------
// osi3::Route
// ---------------------------------------------------------------------------

void BuildOsiRouteInto(const std::vector<RouteSectionSegment>& segments,
                       const LogicalLaneIndex&                 index,
                       std::uint64_t                           route_id,
                       osi3::Route*                            out)
{
    if (out == nullptr)
    {
        return;
    }
    out->Clear();
    out->mutable_route_id()->set_value(route_id);

    for (const RouteSectionSegment& seg : segments)
    {
        osi3::Route_RouteSegment* rs = nullptr;
        for (int lane_id : seg.lanes)
        {
            const auto it = index.find(LogicalLaneKey{seg.road_id, seg.section_idx, lane_id});
            if (it == index.end())
            {
                continue;  // no logical lane for it -- emitting the id anyway would dangle
            }
            if (rs == nullptr)
            {
                rs = out->add_route_segment();
            }
            osi3::Route_LogicalLaneSegment* lls = rs->add_lane_segment();
            lls->mutable_logical_lane_id()->set_value(it->second);
            lls->set_start_s(seg.start_s);
            lls->set_end_s(seg.end_s);
        }
    }
}

}  // namespace osi
}  // namespace gt_esmini
