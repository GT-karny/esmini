// OdrJunctionExtras.cpp -- P5 junction side-model pass.
//
// Plan: GT_esmini/docs/opendrive_16_19_support_plan.md P5 (cluster 5 crossing junction + crossPath,
// cluster 7 junction priority + laneLink overlapZone, cluster 22 laneLink 1.9 layer attributes).
//
// Compiled INTO the upstream RoadManager static target (R1 exception; see
// EnvironmentSimulator/Modules/RoadManager/CMakeLists.txt "# [GT_ODR:cmake]"). L1 contract: parse +
// store + diagnose, no interpretation at storage time. crossPath -> CROSSWALK object synthesis and
// the priority F3 consumer live elsewhere (P5 stage 2 / the F3 week). The <priority> accessor
// (GetJunctionPriorities) is the canonical F3 source.
//
// Sparse: a junction is pushed into model.junction_extras ONLY if it carries at least one of
// crossPath / roadSection / priority / controller / laneLink-layer datum -- plain junctions produce
// no entry, keeping the side model sparse on legacy assets.
#include <cstdlib>
#include <string>

#include "gt_esmini/road/OdrSideModel.hpp"
#include "odr_side_internal.hpp"
#include "pugixml.hpp"

namespace gt_esmini
{
namespace odr
{
namespace
{
// Read a crossPath <startLaneLink>/<endLaneLink> child (@s/@from/@to). Missing child leaves the
// link at its zero defaults.
void ReadCrossPathLaneLink(const pugi::xml_node& parent, const char* child_name, OdrCrossPathLaneLink& out)
{
    pugi::xml_node n = parent.child(child_name);
    if (!n)
    {
        return;
    }
    out.s    = atof(n.attribute("s").value());
    out.from = atoi(n.attribute("from").value());
    out.to   = atoi(n.attribute("to").value());
}
}  // namespace

namespace detail
{

void ParseJunctionExtras(const pugi::xml_node& root, OdrSideModel& model)
{
    for (pugi::xml_node jn = root.child("junction"); jn; jn = jn.next_sibling("junction"))
    {
        OdrJunctionExtras ex;
        bool              has_extra = false;

        // <crossPath> (cluster 5): pedestrian crossing carried by a common/virtual junction.
        for (pugi::xml_node cp = jn.child("crossPath"); cp; cp = cp.next_sibling("crossPath"))
        {
            OdrCrossPath c;
            c.id            = cp.attribute("id").value();
            c.crossing_road = cp.attribute("crossingRoad").value();
            c.road_at_start = cp.attribute("roadAtStart").value();
            c.road_at_end   = cp.attribute("roadAtEnd").value();
            ReadCrossPathLaneLink(cp, "startLaneLink", c.start_lane_link);
            ReadCrossPathLaneLink(cp, "endLaneLink", c.end_lane_link);
            ex.cross_paths.push_back(std::move(c));
            has_extra = true;
        }

        // <roadSection> (cluster 5): crossing-junction s-range of a road.
        for (pugi::xml_node rs = jn.child("roadSection"); rs; rs = rs.next_sibling("roadSection"))
        {
            OdrJunctionRoadSection s;
            s.id      = rs.attribute("id").value();
            s.road_id = rs.attribute("roadId").value();
            s.s_start = atof(rs.attribute("sStart").value());
            s.s_end   = atof(rs.attribute("sEnd").value());
            ex.road_sections.push_back(std::move(s));
            has_extra = true;
        }

        // <priority> (cluster 7, F3 canonical source): XSD allows multiple per junction.
        for (pugi::xml_node pr = jn.child("priority"); pr; pr = pr.next_sibling("priority"))
        {
            OdrJunctionPriority p;
            p.high = pr.attribute("high").value();
            p.low  = pr.attribute("low").value();
            ex.priorities.push_back(std::move(p));
            has_extra = true;
        }

        // <controller> (L1 duplicate of the fork parse for side completeness).
        for (pugi::xml_node ct = jn.child("controller"); ct; ct = ct.next_sibling("controller"))
        {
            OdrJunctionController c;
            c.id       = ct.attribute("id").value();
            c.type     = ct.attribute("type").value();
            c.sequence = atoi(ct.attribute("sequence").value());
            ex.controllers.push_back(std::move(c));
            has_extra = true;
        }

        // <connection>/<laneLink> 1.8/1.9 layer attributes (cluster 22 L1 slot reservation). Only a
        // laneLink carrying at least one of @overlapZone/@fromLayer/@toLayer produces an entry.
        for (pugi::xml_node cn = jn.child("connection"); cn; cn = cn.next_sibling("connection"))
        {
            const std::string conn_id = cn.attribute("id").value();
            for (pugi::xml_node ll = cn.child("laneLink"); ll; ll = ll.next_sibling("laneLink"))
            {
                const char* overlap = ll.attribute("overlapZone").value();
                const char* flayer  = ll.attribute("fromLayer").value();
                const char* tlayer  = ll.attribute("toLayer").value();
                if (overlap[0] == '\0' && flayer[0] == '\0' && tlayer[0] == '\0')
                {
                    continue;
                }
                OdrLaneLinkExtras l;
                l.connection_id = conn_id;
                l.from          = atoi(ll.attribute("from").value());
                l.to            = atoi(ll.attribute("to").value());
                l.overlap_zone  = overlap;
                l.from_layer    = flayer;
                l.to_layer      = tlayer;
                ex.lane_link_extras.push_back(std::move(l));
                has_extra = true;
            }
        }

        if (has_extra)
        {
            ex.junction_id = jn.attribute("id").value();
            ex.type_str    = jn.attribute("type").value();
            model.junction_extras.push_back(std::move(ex));
        }
    }
}

}  // namespace detail

// ---------------------------------------------------------------------------
// Public accessors (P5, F3 handoff). Free functions keyed by the OpenDrive* registry key, mirroring
// the GetLaneSpeedLimit pattern -- upstream Junction stays pristine (no priority members).
// ---------------------------------------------------------------------------

const OdrJunctionExtras* GetJunctionExtras(const void* opendrive_key, const std::string& junction_id)
{
    const OdrSideModel* m = GetSideModel(opendrive_key);
    if (m == nullptr)
    {
        return nullptr;
    }
    for (const OdrJunctionExtras& ex : m->junction_extras)
    {
        if (ex.junction_id == junction_id)
        {
            return &ex;
        }
    }
    return nullptr;
}

bool GetJunctionPriorities(const void* opendrive_key, const std::string& junction_id, std::vector<OdrJunctionPriority>& out)
{
    const OdrJunctionExtras* ex = GetJunctionExtras(opendrive_key, junction_id);
    if (ex == nullptr)
    {
        return false;
    }
    out = ex->priorities;
    return true;
}

}  // namespace odr
}  // namespace gt_esmini
