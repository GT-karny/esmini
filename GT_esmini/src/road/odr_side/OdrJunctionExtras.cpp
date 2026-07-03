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
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

#include "CommonMini.hpp"  // id_t, SMALL_NUMBER, LOG_*
#include "RoadManager.hpp"
#include "gt_esmini/road/OdrSideModel.hpp"
#include "logger.hpp"  // LOG_WARN/LOG_INFO (fmt-style)
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

// ---------------------------------------------------------------------------
// P5 stage 2: crossPath -> synthesized CROSSWALK RMObject + PedPath polyline
// ---------------------------------------------------------------------------
//
// Reserved synthetic object-id range. Rationale (see gt_roadmanager_patches / P5 report):
//   * id_t is uint32_t (CommonMini.hpp:45); ID_UNDEFINED == 0xffffffff (~4.29e9).
//   * Authored RMObject ids are read as as_uint() (small, typically < 1e4 in the crosswalk assets).
//   * Runtime OSI global ids (GetNewGlobalId) start at 0 and increment -- distinct namespace from
//     the authored/synthesized object id_ anyway (that is g_id_, set later via SetGlobalId()).
//   * We pick a high base well clear of authored ids yet far below ID_UNDEFINED so base+index never
//     wraps for any realistic crossPath count. Collision policy: before AddObject we scan the owning
//     road's existing objects; if ANY authored id is >= the base (or exactly equals our candidate),
//     we LOG_WARN and SKIP synthesis for that crossPath (never mutate authored objects, never crash).
namespace
{
constexpr unsigned int kCrosswalkSynthIdBase = 900000000u;  // 9e8; base + running index

// Default painted-crossing half width [m] when the crossing road's walking-lane width is
// unresolvable. ~4 m total crossing -> 2 m half width.
constexpr double kDefaultCrosswalkHalfWidth = 2.0;

// How many centerline samples for the PedPath polyline (>= 2). ~0.5-1 m step over a ~short span.
constexpr int kPedPathMinSamples = 2;

// Resolve a runtime road by its authored id string (string ids legal since ODR 1.7). Mirrors the
// OdrLaneExtras.cpp find_road helper.
roadmanager::Road* FindRoad(roadmanager::OpenDrive* od, const std::string& id_str)
{
    for (unsigned int i = 0; i < od->GetNumOfRoads(); i++)
    {
        roadmanager::Road* r = od->GetRoadByIdx(i);
        if (r != nullptr && (r->GetIdStr() == id_str || std::to_string(r->GetId()) == id_str))
        {
            return r;
        }
    }
    return nullptr;
}

// World position of (road, s, t) via a scratch Position (same lazy resolution OutlineCornerRoad uses).
void WorldAt(id_t road_id, double s, double t, double& x, double& y, double& z)
{
    roadmanager::Position pos;
    pos.SetTrackPos(road_id, s, t);
    x = pos.GetX();
    y = pos.GetY();
    z = pos.GetZ();
}

// Sampled squared XY distance from the crossing-road centerline at s to a fixed world point.
double DistSqToPointOnCrossing(id_t crossing_id, double s, double px, double py)
{
    double x = 0.0, y = 0.0, z = 0.0;
    WorldAt(crossing_id, s, 0.0, x, y, z);
    const double dx = x - px, dy = y - py;
    return dx * dx + dy * dy;
}

// Find the crossing-road s where its centerline is nearest to the crossed-road crossing point
// (world px,py). Coarse scan + local refine over [0, crossing_len].
double FindCrossingS(id_t crossing_id, double crossing_len, double px, double py)
{
    const int    coarse = 64;
    double       best_s = 0.0, best_d = 1e30;
    for (int i = 0; i <= coarse; i++)
    {
        const double s = crossing_len * static_cast<double>(i) / coarse;
        const double d = DistSqToPointOnCrossing(crossing_id, s, px, py);
        if (d < best_d)
        {
            best_d = d;
            best_s = s;
        }
    }
    // Refine within +/- one coarse step by bisection-ish sampling.
    double half = crossing_len / coarse;
    for (int iter = 0; iter < 24; iter++)
    {
        const double sl = std::max(0.0, best_s - half);
        const double sr = std::min(crossing_len, best_s + half);
        const double dl = DistSqToPointOnCrossing(crossing_id, sl, px, py);
        const double dr = DistSqToPointOnCrossing(crossing_id, sr, px, py);
        if (dl < best_d)
        {
            best_d = dl;
            best_s = sl;
        }
        if (dr < best_d)
        {
            best_d = dr;
            best_s = sr;
        }
        half *= 0.5;
    }
    return best_s;
}
}  // namespace

void SynthesizeCrosswalks(OdrSideModel& model, roadmanager::OpenDrive* od)
{
    if (od == nullptr || model.junction_extras.empty())
    {
        return;  // legacy fast path: zero crossPath -> zero synthesis, zero behavior change
    }

    unsigned int synth_index = 0;  // running index appended to the reserved base

    for (OdrJunctionExtras& jex : model.junction_extras)
    {
        for (OdrCrossPath& cp : jex.cross_paths)
        {
            // The crossing road (the separate pedestrian road, @crossingRoad) must resolve.
            roadmanager::Road* crossing = FindRoad(od, cp.crossing_road);
            if (crossing == nullptr)
            {
                LOG_WARN("[GT_ODR] crossPath synth: crossingRoad '{}' (junction {}) not found; skipping",
                         cp.crossing_road,
                         jex.junction_id);
                continue;
            }

            // The crossed road: roadAtStart / roadAtEnd. When they differ we cannot represent a
            // single footprint faithfully -> pick roadAtStart conservatively and WARN (documented).
            std::string crossed_id = cp.road_at_start;
            if (!cp.road_at_end.empty() && cp.road_at_end != cp.road_at_start)
            {
                LOG_WARN("[GT_ODR] crossPath synth: roadAtStart '{}' != roadAtEnd '{}' (junction {}); "
                         "using roadAtStart for the crosswalk footprint",
                         cp.road_at_start,
                         cp.road_at_end,
                         jex.junction_id);
            }
            roadmanager::Road* crossed = FindRoad(od, crossed_id);
            if (crossed == nullptr)
            {
                LOG_WARN("[GT_ODR] crossPath synth: crossed road '{}' (junction {}) not found; skipping",
                         crossed_id,
                         jex.junction_id);
                continue;
            }

            const id_t crossing_rid = crossing->GetId();
            const id_t crossed_rid   = crossed->GetId();
            const double            crossing_len  = crossing->GetLength();
            if (crossing_len < SMALL_NUMBER)
            {
                LOG_WARN("[GT_ODR] crossPath synth: crossingRoad '{}' has zero length; skipping", cp.crossing_road);
                continue;
            }

            // Crossing point on the crossed road (world), at the crossPath @s (linked-road s per XSD).
            // Fall back to the crossed road midpoint if @s is unset/out of range.
            double cross_s = cp.start_lane_link.s;
            if (cross_s < SMALL_NUMBER || cross_s > crossed->GetLength())
            {
                cross_s = 0.5 * crossed->GetLength();
            }
            double cpx = 0.0, cpy = 0.0, cpz = 0.0;
            WorldAt(crossed_rid, cross_s, 0.0, cpx, cpy, cpz);

            // Where the crossing road's centerline is nearest that point.
            const double cross_center_s = FindCrossingS(crossing_rid, crossing_len, cpx, cpy);

            // Longitudinal half-extent of the footprint ALONG the crossing road = how far the crossing
            // road must run to cover the crossed carriageway. Use the crossed road's total transverse
            // width (both sides) as the span; guard with a small floor.
            double carriageway = crossed->GetWidth(cross_s, 0);  // side 0 = both sides, any lane type
            if (carriageway < 1.0)
            {
                carriageway = 7.0;  // ~2 lanes; conservative floor so the crosswalk straddles the road
            }
            const double s_lo = std::max(0.0, cross_center_s - 0.5 * carriageway);
            const double s_hi = std::min(crossing_len, cross_center_s + 0.5 * carriageway);

            // Lateral half-width of the crosswalk = crossing road's walking-lane half width (@to lane
            // id on the crossing road); fall back to a sane default. GetLaneWidthByS is total width.
            double half_w = kDefaultCrosswalkHalfWidth;
            {
                const double w = crossing->GetLaneWidthByS(cross_center_s, cp.start_lane_link.to);
                if (w > SMALL_NUMBER)
                {
                    half_w = 0.5 * w;
                }
            }

            // Candidate synthetic id + collision check against the crossed road's authored objects.
            const unsigned int cand_id = kCrosswalkSynthIdBase + synth_index;
            bool               collide = false;
            for (idx_t j = 0; j < crossed->GetNumberOfObjects(); j++)
            {
                const roadmanager::RMObject* o = crossed->GetRoadObject(j);
                if (o == nullptr)
                {
                    continue;
                }
                const unsigned int oid = static_cast<unsigned int>(o->GetId());
                if (oid >= kCrosswalkSynthIdBase || oid == cand_id)
                {
                    collide = true;
                    break;
                }
            }
            if (collide)
            {
                LOG_WARN("[GT_ODR] crossPath synth: crossed road '{}' has an authored object id in the reserved "
                         "synthetic range (>= {}); skipping crosswalk synthesis for junction {} crossPath {}",
                         crossed_id,
                         kCrosswalkSynthIdBase,
                         jex.junction_id,
                         cp.id);
                continue;
            }

            // Build a closed 4-corner footprint in the CROSSING road's frame. OutlineCornerRoad
            // resolves world coords lazily via Position::SetTrackPos(crossing_rid, s, t) independent
            // of the owning (crossed) road, so registering these on the crossed road is valid and the
            // RouteCrosswalkScan consumer (corner->GetPos()) sees a rectangle straddling the crossed
            // road. Corner order goes around the rectangle (closed polygon).
            roadmanager::Outline* outline = new roadmanager::Outline(cand_id, roadmanager::Outline::FillType::FILL_TYPE_UNDEFINED, true);
            const double          corners[4][2] = {{s_lo, +half_w}, {s_hi, +half_w}, {s_hi, -half_w}, {s_lo, -half_w}};
            for (int c = 0; c < 4; c++)
            {
                roadmanager::OutlineCorner* corner = static_cast<roadmanager::OutlineCorner*>(
                    new roadmanager::OutlineCornerRoad(crossing_rid, corners[c][0], corners[c][1], 0.0, 0.0, 0.0, 0.0, 0.0));
                outline->AddCorner(corner);
            }

            // Object pose on the OWNING (crossed) road: s at the crossing point, t=0. length/width from
            // the footprint extents so the box fallback (if ever taken) is plausible too.
            double owner_x = 0.0, owner_y = 0.0, owner_z = 0.0;
            WorldAt(crossed_rid, cross_s, 0.0, owner_x, owner_y, owner_z);
            roadmanager::Position owner_pos;
            owner_pos.SetTrackPos(crossed_rid, cross_s, 0.0);
            const double owner_h = owner_pos.GetHRoad();

            roadmanager::RMObject* rm_obj = new roadmanager::RMObject(cross_s,
                                                                      0.0,
                                                                      cand_id,
                                                                      "crossPath_" + jex.junction_id + "_" + cp.id,
                                                                      roadmanager::RoadObject::Orientation(),
                                                                      0.0,
                                                                      roadmanager::RMObject::ObjectType::CROSSWALK,
                                                                      /*length*/ 2.0 * half_w,
                                                                      /*height*/ 0.0,
                                                                      /*width*/ (s_hi - s_lo),
                                                                      /*heading*/ 0.0,
                                                                      0.0,
                                                                      0.0,
                                                                      owner_x,
                                                                      owner_y,
                                                                      owner_z,
                                                                      owner_h);
            rm_obj->AddOutline(outline);
            crossed->AddObject(rm_obj);
            cp.synth_object_id = cand_id;
            synth_index++;

            // PedPath polyline: crossing-road centerline sampled across the crossing span.
            const double span   = s_hi - s_lo;
            int          nsamp  = static_cast<int>(std::ceil(span / 0.75)) + 1;
            if (nsamp < kPedPathMinSamples)
            {
                nsamp = kPedPathMinSamples;
            }
            cp.ped_path.clear();
            cp.ped_path.reserve(static_cast<size_t>(nsamp));
            for (int i = 0; i < nsamp; i++)
            {
                const double s = s_lo + span * static_cast<double>(i) / (nsamp - 1);
                OdrPedPathSample sample;
                sample.s = s;
                WorldAt(crossing_rid, s, 0.0, sample.x, sample.y, sample.z);
                cp.ped_path.push_back(sample);
            }

            LOG_INFO("[GT_ODR] crossPath synth: junction {} crossPath {} -> CROSSWALK obj id {} on road '{}' "
                     "(crossing road '{}' s[{:.2f},{:.2f}] halfW {:.2f}, {} ped-path samples)",
                     jex.junction_id,
                     cp.id,
                     cand_id,
                     crossed_id,
                     cp.crossing_road,
                     s_lo,
                     s_hi,
                     half_w,
                     cp.ped_path.size());
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
