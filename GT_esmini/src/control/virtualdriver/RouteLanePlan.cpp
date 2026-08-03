#include "gt_esmini/control/virtualdriver/RouteLanePlan.hpp"

#include "RoadManager.hpp"
#include "LaneIndependentRouter.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

using namespace roadmanager;

namespace gt_esmini
{

namespace
{

// A route waypoint reduced to the 3 fields the band-construction algorithm needs.
struct WaypointInfo
{
    Road*  road    = nullptr;
    int    lane_id = 0;
    double s       = 0.0;
};

// route.minimal_waypoints_ -> WaypointInfo, collapsing consecutive same-road waypoints down to
// the first per road (a route may legally carry more than one waypoint on the same road).
std::vector<WaypointInfo> ExtractSkeleton(OpenDrive* odr, const std::vector<Position>& waypoints)
{
    std::vector<WaypointInfo> skeleton;
    for (const Position& wp : waypoints)
    {
        Road* road = odr->GetRoadById(wp.GetTrackId());
        if (road == nullptr)
        {
            continue;  // defensive: a route built on the loaded network should never hit this
        }
        if (!skeleton.empty() && skeleton.back().road->GetId() == road->GetId())
        {
            continue;  // same road as the previous waypoint -- keep only the first
        }
        skeleton.push_back({road, wp.GetLaneId(), wp.GetS()});
    }
    return skeleton;
}

void InsertSortedUnique(std::vector<int>& lanes, int lane_id)
{
    const auto it = std::lower_bound(lanes.begin(), lanes.end(), lane_id);
    if (it != lanes.end() && *it == lane_id)
    {
        return;
    }
    lanes.insert(it, lane_id);
}

// Does `link` (one end of its owning road's own <link>) resolve onward to `to_road` -- either a
// direct road reference, or a junction reference to a junction `to_road` itself belongs to? Both
// arms of a junction link to the JUNCTION (not to a specific connecting road), so this same test
// is applied symmetrically from either side of a hop (see ResolveHop below).
bool LinkResolvesTo(const RoadLink* link, const Road* to_road)
{
    if (link == nullptr || to_road == nullptr)
    {
        return false;
    }
    if (link->GetElementType() == RoadLink::ELEMENT_TYPE_ROAD)
    {
        return link->GetElementId() == to_road->GetId();
    }
    if (link->GetElementType() == RoadLink::ELEMENT_TYPE_JUNCTION)
    {
        return to_road->GetJunction() == link->GetElementId();
    }
    return false;
}

// The resolved link between two adjacent route roads, plus which end of EACH road the hop
// touches -- needed to normalize lane ids at road_ip1's connecting end via
// Road::GetConnectedLaneIdAtS.
struct RoadHop
{
    bool      found              = false;
    RoadLink* link               = nullptr;  // road_i's own link (fed to Road::GetConnectingLaneId)
    bool      i_exit_at_end      = true;     // road_i's connecting end: true = s=length, false = s=0
    bool      ip1_entry_at_start = true;     // road_ip1's connecting end: true = s=0, false = s=length
};

RoadHop ResolveHop(Road* road_i, Road* road_ip1)
{
    RoadHop hop;
    for (LinkType lt : {LinkType::SUCCESSOR, LinkType::PREDECESSOR})
    {
        RoadLink* link = road_i->GetLink(lt);
        if (!LinkResolvesTo(link, road_ip1))
        {
            continue;
        }

        hop.found         = true;
        hop.link          = link;
        hop.i_exit_at_end = (lt == LinkType::SUCCESSOR);

        // Resolve road_ip1's own connecting end by checking ITS links back to road_i.
        hop.ip1_entry_at_start = true;  // default if the check below can't independently confirm
        for (LinkType lt2 : {LinkType::PREDECESSOR, LinkType::SUCCESSOR})
        {
            RoadLink* back = road_ip1->GetLink(lt2);
            if (LinkResolvesTo(back, road_i))
            {
                hop.ip1_entry_at_start = (lt2 == LinkType::PREDECESSOR);
                break;
            }
        }
        return hop;
    }
    return hop;  // found == false
}

// Steps 5-6 of the design: resolve every road[i]->road[i+1] link, then propagate lane bands
// backward from the final waypoint's lane. Returns true iff every band came out non-empty; on
// false, plan.diagnostic explains why ("link_not_found" or "lane_discontinuity", the latter also
// stamping plan.discontinuity_road_id), and plan.bands holds only the bands already resolved
// before the break (i.e. for roads AFTER the break point in route order -- never before it).
bool BuildBandsFromSkeleton(const std::vector<WaypointInfo>& skeleton, RouteLanePlan& plan)
{
    const std::size_t n = skeleton.size();
    if (n < 2)
    {
        // The n==1 case is handled by the caller before this is ever invoked; guarded here too
        // so a future call site mistake can't underflow the hop count below.
        plan.diagnostic = "invalid_route";
        return false;
    }

    std::vector<RoadHop> hops(n - 1);
    for (std::size_t i = 0; i + 1 < n; ++i)
    {
        hops[i] = ResolveHop(skeleton[i].road, skeleton[i + 1].road);
        if (!hops[i].found)
        {
            plan.diagnostic = "link_not_found";
            return false;
        }
    }

    std::vector<RouteLaneBand> bands_reverse;  // built N-1 -> 0, reversed before returning

    // i = N-1 (final road): seeded directly from the final waypoint's own lane -- there is no
    // "connecting end" to scan here, this road simply IS the destination.
    {
        RouteLaneBand last_band;
        last_band.road_id          = skeleton[n - 1].road->GetId();
        last_band.lanes            = {skeleton[n - 1].lane_id};
        last_band.exit_s           = skeleton[n - 1].s;
        last_band.exit_at_road_end = hops[n - 2].ip1_entry_at_start;
        bands_reverse.push_back(last_band);
    }

    // i = N-2 .. 0, backward.
    for (std::size_t rev = 1; rev < n; ++rev)
    {
        const std::size_t ii  = n - 1 - rev;
        const RoadHop&     hop = hops[ii];

        Road* road_i   = skeleton[ii].road;
        Road* road_ip1 = skeleton[ii + 1].road;

        const RouteLaneBand& next_band = bands_reverse.back();  // road[ii+1]'s band, already resolved

        const double exit_s      = hop.i_exit_at_end ? road_i->GetLength() : 0.0;
        const double ip1_entry_s = hop.ip1_entry_at_start ? 0.0 : road_ip1->GetLength();

        RouteLaneBand band;
        band.road_id          = road_i->GetId();
        band.exit_s           = exit_s;
        band.exit_at_road_end = hop.i_exit_at_end;

        LaneSection* lsec = road_i->GetLaneSectionByS(exit_s);
        if (lsec != nullptr)
        {
            const unsigned int n_lanes = lsec->GetNumberOfLanes();
            for (unsigned int li = 0; li < n_lanes; ++li)
            {
                Lane* lane = lsec->GetLaneByIdx(li);
                if (lane == nullptr || !lane->IsDriving())
                {
                    continue;
                }

                const int next_lane = road_i->GetConnectingLaneId(hop.link, lane->GetId(), road_ip1->GetId());
                if (next_lane == 0)
                {
                    continue;
                }

                bool reaches_next_band = false;
                for (int band_lane : next_band.lanes)
                {
                    const int normalized = road_ip1->GetConnectedLaneIdAtS(band_lane, next_band.exit_s, ip1_entry_s);
                    if (normalized != 0 && normalized == next_lane)
                    {
                        reaches_next_band = true;
                        break;
                    }
                }
                if (reaches_next_band)
                {
                    InsertSortedUnique(band.lanes, lane->GetId());
                }
            }
        }

        if (band.lanes.empty())
        {
            plan.diagnostic           = "lane_discontinuity";
            plan.discontinuity_road_id = road_i->GetId();
            std::reverse(bands_reverse.begin(), bands_reverse.end());
            plan.bands = std::move(bands_reverse);
            return false;
        }

        bands_reverse.push_back(band);
    }

    std::reverse(bands_reverse.begin(), bands_reverse.end());
    plan.bands = std::move(bands_reverse);
    return true;
}

}  // namespace

RouteLanePlan BuildRouteLanePlan(const Route& route)
{
    RouteLanePlan plan;

    if (!route.IsValid())
    {
        plan.diagnostic = "invalid_route";
        return plan;
    }

    OpenDrive* odr = Position::GetOpenDrive();
    if (odr == nullptr)
    {
        plan.diagnostic = "no_opendrive";
        return plan;
    }

    if (route.minimal_waypoints_.empty())
    {
        plan.diagnostic = "empty_waypoints";
        return plan;
    }

    const std::vector<WaypointInfo> skeleton = ExtractSkeleton(odr, route.minimal_waypoints_);
    if (skeleton.empty())
    {
        plan.diagnostic = "empty_waypoints";
        return plan;
    }

    if (skeleton.size() == 1)
    {
        RouteLaneBand band;
        band.road_id          = skeleton[0].road->GetId();
        band.lanes            = {skeleton[0].lane_id};
        band.exit_at_road_end = true;
        band.exit_s           = skeleton[0].s;
        plan.bands.push_back(band);
        plan.valid = true;
        return plan;
    }

    if (BuildBandsFromSkeleton(skeleton, plan))
    {
        plan.valid = true;
        return plan;
    }

    if (plan.diagnostic != "lane_discontinuity")
    {
        // link_not_found (or any future non-recoverable reason): no reroute is attempted.
        return plan;
    }

    // Exactly one LaneIndependentRouter reroute attempt (never recurse past this point).
    // LaneIndependentRouter::CalculatePath picks its search direction (predecessor vs successor
    // off the start road) from start.GetHRelative(); a freshly SetLanePos'd Position defaults
    // that to 0.0 (Position::Init()'s default, meaning "+s"), which is only correct for a route
    // whose first leg happens to travel +s. Carry over the ORIGINAL waypoint's heading so a
    // route travelling -s off its first road reroutes in the same direction it was built in.
    Position start;
    start.SetLanePos(skeleton.front().road->GetId(), skeleton.front().lane_id, skeleton.front().s, 0.0);
    start.SetHeadingRelative(route.minimal_waypoints_.front().GetHRelative());
    Position target;
    target.SetLanePos(skeleton.back().road->GetId(), skeleton.back().lane_id, skeleton.back().s, 0.0);

    LaneIndependentRouter router(odr);
    std::vector<Node>     node_path = router.CalculatePath(start, target);

    if (node_path.empty())
    {
        plan.diagnostic = "reroute_failed";
        plan.valid      = false;
        plan.bands.clear();  // the first pass's partial bands (see BuildBandsFromSkeleton) don't
                             // describe this outcome; discontinuity_road_id is the only breadcrumb
                             // intentionally kept -- see the field's doc comment in the header.
        return plan;
    }

    std::vector<WaypointInfo> rerouted_skeleton;
    for (const Node& node : node_path)
    {
        if (node.road == nullptr)
        {
            continue;
        }
        if (!rerouted_skeleton.empty() && rerouted_skeleton.back().road->GetId() == node.road->GetId())
        {
            continue;  // collapse consecutive same-road nodes (e.g. virtual-junction anchor hops)
        }
        rerouted_skeleton.push_back({node.road, node.currentLaneId, 0.0});
    }

    if (rerouted_skeleton.empty())
    {
        plan.diagnostic = "reroute_failed";
        plan.valid      = false;
        plan.bands.clear();
        return plan;
    }
    rerouted_skeleton.front().s = skeleton.front().s;
    rerouted_skeleton.back().s  = skeleton.back().s;

    RouteLanePlan retry;
    retry.rerouted = true;

    if (rerouted_skeleton.size() == 1)
    {
        RouteLaneBand band;
        band.road_id          = rerouted_skeleton[0].road->GetId();
        band.lanes            = {rerouted_skeleton[0].lane_id};
        band.exit_at_road_end = true;
        band.exit_s           = rerouted_skeleton[0].s;
        retry.bands.push_back(band);
        retry.valid = true;
        return retry;
    }

    // Redo steps 5-6 on the rerouted skeleton; whatever this produces is reported as-is (a
    // second lane_discontinuity is not retried again -- see the header/design notes).
    if (BuildBandsFromSkeleton(rerouted_skeleton, retry))
    {
        retry.valid = true;
    }
    return retry;
}

RouteLaneStatus EvaluateRouteLaneStatus(const RouteLanePlan& plan, const Position& pos)
{
    RouteLaneStatus status;

    if (!plan.valid || plan.bands.empty())
    {
        status.reason = "no_plan";
        return status;
    }

    const id_t track_id = pos.GetTrackId();

    const RouteLaneBand* matched = nullptr;
    for (const RouteLaneBand& band : plan.bands)
    {
        if (band.road_id == track_id)
        {
            matched = &band;
            break;
        }
    }

    if (matched == nullptr)
    {
        status.reason  = "off_plan_road";
        status.road_id = track_id;
        return status;
    }

    status.road_id      = matched->road_id;
    status.ego_lane_raw = pos.GetLaneId();
    status.ego_lane     = status.ego_lane_raw;

    OpenDrive* odr  = Position::GetOpenDrive();
    Road*      road = (odr != nullptr) ? odr->GetRoadById(track_id) : nullptr;
    if (road != nullptr)
    {
        const int normalized = road->GetConnectedLaneIdAtS(status.ego_lane_raw, pos.GetS(), matched->exit_s);
        if (normalized != 0)
        {
            status.ego_lane = normalized;
        }
    }

    status.target_lanes   = matched->lanes;
    status.on_target_lane = std::find(matched->lanes.begin(), matched->lanes.end(), status.ego_lane) != matched->lanes.end();

    // The final band has no onward connection -- its exit_s is the destination waypoint's own s,
    // not a junction/road transition. Reporting a distance there would read as "the connection is
    // N metres ahead" (and, once the ego passes the destination s, as a hard 0.0 = "at the
    // connection now"), so report -1.0 = not applicable instead, per the field's documented
    // "-1 = unknown" contract.
    if (matched == &plan.bands.back())
    {
        status.dist_to_connection = -1.0;
    }
    else
    {
        const double dist = matched->exit_at_road_end ? (matched->exit_s - pos.GetS()) : (pos.GetS() - matched->exit_s);
        status.dist_to_connection = std::max(0.0, dist);
    }

    status.valid = true;
    return status;
}

}  // namespace gt_esmini
