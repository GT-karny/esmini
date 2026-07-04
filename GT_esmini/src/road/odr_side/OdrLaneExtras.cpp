// OdrLaneExtras.cpp -- P2 lane-detail side storage (clusters 3 + 16 L1), border->width
// normalization (Ex_Lane-Border false-green fix) and the lane <speed> L2 lookup.
//
// Plan: GT_esmini/docs/opendrive_16_19_support_plan.md P2.
//
// Compiled INTO the upstream RoadManager static target (same R1 swap-zone exception as the other
// odr_side/*.cpp -- see EnvironmentSimulator/Modules/RoadManager/CMakeLists.txt "# [GT_ODR:cmake]").
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "CommonMini.hpp"
#include "RoadManager.hpp"
#include "gt_esmini/road/OdrSideModel.hpp"  // SelectLanesLayer (P8 D6)
#include "logger.hpp"  // LOG_WARN/LOG_INFO/LOG_ERROR (fmt-style)
#include "odr_side_internal.hpp"
#include "pugixml.hpp"

namespace gt_esmini
{
namespace odr
{
namespace detail
{

namespace
{

// ---------------------------------------------------------------------------
// Piecewise cubic algebra (pure; used by the border->width normalization)
// ---------------------------------------------------------------------------

// One cubic piece p(x) = a + b*dx + c*dx^2 + d*dx^3 with dx = x - s0, s0 relative to the
// owning lane section start (identical convention to <width>/<border> @sOffset).
struct Piece
{
    double s0 = 0.0, a = 0.0, b = 0.0, c = 0.0, d = 0.0;
};

// Taylor-shift a piece so its origin moves from p.s0 to s_new (>= p.s0): returns coefficients
// of the SAME polynomial expressed around s_new.
Piece ShiftTo(const Piece& p, double s_new)
{
    const double dx = s_new - p.s0;
    Piece        q;
    q.s0 = s_new;
    q.a  = p.a + p.b * dx + p.c * dx * dx + p.d * dx * dx * dx;
    q.b  = p.b + 2.0 * p.c * dx + 3.0 * p.d * dx * dx;
    q.c  = p.c + 3.0 * p.d * dx;
    q.d  = p.d;
    return q;
}

// The piece of `pieces` active at s (last piece with s0 <= s + eps); pieces must be sorted by s0.
// Returns nullptr when s precedes the first piece.
const Piece* ActivePiece(const std::vector<Piece>& pieces, double s)
{
    const Piece* found = nullptr;
    for (const Piece& p : pieces)
    {
        if (p.s0 <= s + SMALL_NUMBER)
        {
            found = &p;
        }
        else
        {
            break;
        }
    }
    return found;
}

// result(x) = lhs(x) + sign * rhs(x), re-expressed as pieces over the union of both breakpoint
// sets. Missing coverage (s before a list's first piece) is treated as the zero polynomial.
std::vector<Piece> Combine(const std::vector<Piece>& lhs, const std::vector<Piece>& rhs, double sign)
{
    std::set<double> breaks;
    for (const Piece& p : lhs)
    {
        breaks.insert(p.s0);
    }
    for (const Piece& p : rhs)
    {
        breaks.insert(p.s0);
    }
    std::vector<Piece> out;
    for (double s0 : breaks)
    {
        const Piece* pl = ActivePiece(lhs, s0);
        const Piece* pr = ActivePiece(rhs, s0);
        const Piece  l  = pl ? ShiftTo(*pl, s0) : Piece{s0, 0.0, 0.0, 0.0, 0.0};
        const Piece  r  = pr ? ShiftTo(*pr, s0) : Piece{s0, 0.0, 0.0, 0.0, 0.0};
        Piece        q;
        q.s0 = s0;
        q.a  = l.a + sign * r.a;
        q.b  = l.b + sign * r.b;
        q.c  = l.c + sign * r.c;
        q.d  = l.d + sign * r.d;
        out.push_back(q);
    }
    return out;
}

std::vector<Piece> BordersToPieces(const std::vector<OdrLaneBorder>& borders)
{
    std::vector<Piece> out;
    for (const OdrLaneBorder& b : borders)
    {
        out.push_back({b.s_offset, b.a, b.b, b.c, b.d});
    }
    std::sort(out.begin(), out.end(), [](const Piece& x, const Piece& y) { return x.s0 < y.s0; });
    return out;
}

std::vector<Piece> RuntimeWidthsToPieces(const roadmanager::Lane* lane)
{
    std::vector<Piece> out;
    for (unsigned int i = 0; i < lane->GetNumberOfLaneWidths(); i++)
    {
        const roadmanager::LaneWidth* w = lane->GetWidthByIndex(i);
        if (w != nullptr)
        {
            out.push_back({w->GetSOffset(), w->poly3_.GetA(), w->poly3_.GetB(), w->poly3_.GetC(), w->poly3_.GetD()});
        }
    }
    std::sort(out.begin(), out.end(), [](const Piece& x, const Piece& y) { return x.s0 < y.s0; });
    return out;
}

// ---------------------------------------------------------------------------
// Lane extras DOM pass helpers
// ---------------------------------------------------------------------------

bool ReadLaneNode(const pugi::xml_node& lane_node, OdrLaneExtras& ex)
{
    bool has_p2 = false;

    ex.type_str = lane_node.attribute("type").value();
    // The four tokens the [GT_ODR:lane-types] fork patch maps onto nearest enums are themselves
    // P2 data (exact source string kept for OSI-subtype fidelity / future native support).
    if (ex.type_str == "walking" || ex.type_str == "curb" || ex.type_str == "shared" || ex.type_str == "slipLane")
    {
        has_p2 = true;
    }

    // 1.8 lane attributes (cluster 3 L1).
    ex.direction              = lane_node.attribute("direction").value();
    ex.advisory               = lane_node.attribute("advisory").value();
    ex.dynamic_lane_direction = lane_node.attribute("dynamicLaneDirection").value();
    ex.dynamic_lane_type      = lane_node.attribute("dynamicLaneType").value();
    ex.road_works             = lane_node.attribute("roadWorks").value();
    if (!ex.direction.empty() || !ex.advisory.empty() || !ex.dynamic_lane_direction.empty() || !ex.dynamic_lane_type.empty() ||
        !ex.road_works.empty())
    {
        has_p2 = true;
    }

    // Cluster 16 L1 children.
    for (pugi::xml_node n = lane_node.child("speed"); n; n = n.next_sibling("speed"))
    {
        OdrLaneSpeed sp;
        sp.s_offset = atof(n.attribute("sOffset").value());
        sp.max      = atof(n.attribute("max").value());
        sp.unit     = n.attribute("unit").value();
        ex.speeds.push_back(sp);
        has_p2 = true;
    }
    for (pugi::xml_node n = lane_node.child("access"); n; n = n.next_sibling("access"))
    {
        OdrLaneAccess ac;
        ac.s_offset    = atof(n.attribute("sOffset").value());
        ac.rule        = n.attribute("rule").value();
        ac.restriction = n.attribute("restriction").value();  // <=1.5 attribute form
        for (pugi::xml_node r = n.child("restriction"); r; r = r.next_sibling("restriction"))
        {
            ac.restrictions.push_back(r.attribute("type").value());  // 1.6+ child form
        }
        ex.accesses.push_back(ac);
        has_p2 = true;
    }
    for (pugi::xml_node n = lane_node.child("rule"); n; n = n.next_sibling("rule"))
    {
        OdrLaneRule ru;
        ru.s_offset = atof(n.attribute("sOffset").value());
        ru.value    = n.attribute("value").value();
        ex.rules.push_back(ru);
        has_p2 = true;
    }
    for (pugi::xml_node rm = lane_node.child("roadMark"); rm; rm = rm.next_sibling("roadMark"))
    {
        for (pugi::xml_node n = rm.child("sway"); n; n = n.next_sibling("sway"))
        {
            OdrRoadMarkSway sw;
            sw.ds = atof(n.attribute("ds").value());
            sw.a  = atof(n.attribute("a").value());
            sw.b  = atof(n.attribute("b").value());
            sw.c  = atof(n.attribute("c").value());
            sw.d  = atof(n.attribute("d").value());
            ex.sways.push_back(sw);
            has_p2 = true;
        }
    }
    for (pugi::xml_node n = lane_node.child("border"); n; n = n.next_sibling("border"))
    {
        OdrLaneBorder bo;
        bo.s_offset = atof(n.attribute("sOffset").value());
        bo.a        = atof(n.attribute("a").value());
        bo.b        = atof(n.attribute("b").value());
        bo.c        = atof(n.attribute("c").value());
        bo.d        = atof(n.attribute("d").value());
        ex.borders.push_back(bo);
        has_p2 = true;
    }

    // P8 cluster 22 L1: lane <link>/<predecessor|successor> 1.9 @layer (sparse -- only when @layer
    // is authored). Counts as P2/P8 data so the entry is stored.
    if (pugi::xml_node link = lane_node.child("link"))
    {
        for (const char* dir : {"predecessor", "successor"})
        {
            for (pugi::xml_node ln = link.child(dir); ln; ln = ln.next_sibling(dir))
            {
                if (ln.attribute("layer").empty())
                {
                    continue;  // no @layer -> no record (bit-identical for legacy assets)
                }
                OdrLaneLinkLayer ll;
                ll.link_dir = dir;
                ll.id       = ln.attribute("id").value();
                ll.layer    = ln.attribute("layer").value();
                ex.link_layers.push_back(std::move(ll));
                has_p2 = true;
            }
        }
    }

    return has_p2;
}

double SpeedToMs(double value, const std::string& unit)
{
    if (unit == "km/h")
    {
        return value / 3.6;
    }
    if (unit == "mph")
    {
        return value * 0.44704;
    }
    // "" (ODR default) and "m/s"
    return value;
}

}  // namespace

void ParseLaneExtras(const pugi::xml_node& root, OdrSideModel& model, const void* opendrive_key)
{
    for (pugi::xml_node road = root.child("road"); road; road = road.next_sibling("road"))
    {
        const std::string road_id = road.attribute("id").value();
        // Walk the SAME <lanes> view RoadManager uses (permanent selection or the temporary-merge
        // synthetic DOM) so lane extras and the runtime lane structure agree (plan P8 D6). In
        // permanent mode / on legacy assets this returns road.child("lanes") unchanged.
        pugi::xml_node    lanes   = SelectLanesLayer(road, opendrive_key);
        if (!lanes)
        {
            continue;
        }
        int section_index = 0;
        for (pugi::xml_node sec = lanes.child("laneSection"); sec; sec = sec.next_sibling("laneSection"), section_index++)
        {
            const double section_s = atof(sec.attribute("s").value());
            for (const char* side : {"left", "center", "right"})
            {
                pugi::xml_node side_node = sec.child(side);
                if (!side_node)
                {
                    continue;
                }
                for (pugi::xml_node lane_node = side_node.child("lane"); lane_node; lane_node = lane_node.next_sibling("lane"))
                {
                    OdrLaneExtras ex;
                    ex.road_id       = road_id;
                    ex.section_index = section_index;
                    ex.section_s     = section_s;
                    ex.lane_id       = atoi(lane_node.attribute("id").value());
                    ex.side          = side;
                    if (ReadLaneNode(lane_node, ex))
                    {
                        model.lane_extras.push_back(std::move(ex));
                    }
                }
            }
        }
    }
}

void ApplyBorderWidths(const OdrSideModel& model, roadmanager::OpenDrive* od)
{
    if (od == nullptr)
    {
        return;
    }

    // Group border-carrying extras by (road_id, section_index, side).
    struct SectionKey
    {
        std::string road_id;
        int         section_index;
        std::string side;
        bool        operator<(const SectionKey& o) const
        {
            if (road_id != o.road_id)
                return road_id < o.road_id;
            if (section_index != o.section_index)
                return section_index < o.section_index;
            return side < o.side;
        }
    };
    std::map<SectionKey, std::vector<const OdrLaneExtras*>> groups;
    for (const OdrLaneExtras& ex : model.lane_extras)
    {
        if (!ex.borders.empty() && ex.side != "center")
        {
            groups[{ex.road_id, ex.section_index, ex.side}].push_back(&ex);
        }
    }
    if (groups.empty())
    {
        return;  // default path: zero work, zero behavior change
    }

    // Resolve a runtime road by its authored id string (string ids are legal since ODR 1.7).
    auto find_road = [od](const std::string& id_str) -> roadmanager::Road*
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
    };

    for (const auto& kv : groups)
    {
        const SectionKey& key = kv.first;
        roadmanager::Road* road = find_road(key.road_id);
        if (road == nullptr || key.section_index < 0 || static_cast<unsigned int>(key.section_index) >= road->GetNumberOfLaneSections())
        {
            LOG_WARN("[GT_ODR] border->width: road '{}' laneSection {} not found in runtime model; skipping", key.road_id, key.section_index);
            continue;
        }
        roadmanager::LaneSection* ls = road->GetLaneSectionByIdx(static_cast<unsigned int>(key.section_index));
        if (ls == nullptr)
        {
            continue;
        }

        // Process the whole side inner->outer so each lane's inner boundary is known.
        // side sign: +1 for left (t grows outward), -1 for right.
        const double sign = (key.side == "left") ? 1.0 : -1.0;

        // Collect ALL side lanes (from the runtime section) sorted by |id| ascending, so lanes
        // that authored <width> still advance the running boundary correctly.
        std::vector<roadmanager::Lane*> side_lanes;
        for (unsigned int i = 0; i < ls->GetNumberOfLanes(); i++)
        {
            roadmanager::Lane* ln = ls->GetLaneByIdx(i);
            if (ln == nullptr || ln->GetId() == 0)
            {
                continue;
            }
            if ((key.side == "left") ? (ln->GetId() > 0) : (ln->GetId() < 0))
            {
                side_lanes.push_back(ln);
            }
        }
        std::sort(side_lanes.begin(),
                  side_lanes.end(),
                  [](const roadmanager::Lane* a, const roadmanager::Lane* b) { return abs(a->GetId()) < abs(b->GetId()); });

        auto find_extras = [&](int lane_id) -> const OdrLaneExtras*
        {
            for (const OdrLaneExtras* ex : kv.second)
            {
                if (ex->lane_id == lane_id)
                {
                    return ex;
                }
            }
            return nullptr;
        };

        // Running inner boundary in signed t, relative to the lane section reference line.
        // (laneOffset shifts inner and outer boundaries equally and cancels in the width algebra.)
        std::vector<Piece> inner = {{0.0, 0.0, 0.0, 0.0, 0.0}};
        for (roadmanager::Lane* lane : side_lanes)
        {
            const OdrLaneExtras* ex = find_extras(lane->GetId());
            if (lane->GetNumberOfLaneWidths() > 0)
            {
                // Lane authored widths (mixed side, or width+border: width prevails per spec).
                // Advance the boundary: outer = inner + sign * width.
                if (ex != nullptr)
                {
                    LOG_INFO("[GT_ODR] border->width: lane {} of road '{}' has both <width> and <border>; keeping widths (spec)",
                             lane->GetId(),
                             key.road_id);
                }
                inner = Combine(inner, RuntimeWidthsToPieces(lane), sign);
                continue;
            }
            if (ex == nullptr)
            {
                // No widths and no borders: geometric width stays zero today; boundary unchanged.
                continue;
            }
            // width = sign * (border - inner)  (piecewise; result pieces cover the union grid)
            const std::vector<Piece> border = BordersToPieces(ex->borders);
            std::vector<Piece>       width  = Combine(border, inner, -1.0);
            for (Piece& p : width)
            {
                p.a *= sign;
                p.b *= sign;
                p.c *= sign;
                p.d *= sign;
                if (p.a < -SMALL_NUMBER)
                {
                    LOG_WARN("[GT_ODR] border->width: negative width {:.3f} at sOffset {:.2f} (road '{}' lane {}); keeping algebra as-is",
                             p.a,
                             p.s0,
                             key.road_id,
                             lane->GetId());
                }
                lane->AddLaneWidth(new roadmanager::LaneWidth(p.s0, p.a, p.b, p.c, p.d));
            }
            inner = border;
        }
    }
}

}  // namespace detail

// ---------------------------------------------------------------------------
// Public P2 lookups
// ---------------------------------------------------------------------------

double GetLaneSpeedLimit(const void* opendrive_key, const std::string& road_id, int lane_id, double s)
{
    const OdrSideModel* m = GetSideModel(opendrive_key);
    if (m == nullptr || m->lane_extras.empty())
    {
        return 0.0;
    }
    // Entry with the greatest section_s <= s among matching (road, lane) entries carrying speeds.
    const OdrLaneExtras* best = nullptr;
    for (const OdrLaneExtras& ex : m->lane_extras)
    {
        if (ex.road_id == road_id && ex.lane_id == lane_id && !ex.speeds.empty() && ex.section_s <= s + SMALL_NUMBER)
        {
            if (best == nullptr || ex.section_s > best->section_s)
            {
                best = &ex;
            }
        }
    }
    if (best == nullptr)
    {
        return 0.0;
    }
    const double        ds   = s - best->section_s;
    const OdrLaneSpeed* rec  = nullptr;
    for (const OdrLaneSpeed& sp : best->speeds)
    {
        if (sp.s_offset <= ds + SMALL_NUMBER && (rec == nullptr || sp.s_offset > rec->s_offset))
        {
            rec = &sp;
        }
    }
    if (rec == nullptr)
    {
        return 0.0;
    }
    return detail::SpeedToMs(rec->max, rec->unit);
}

double GetLaneSpeedLimitForPosition(const roadmanager::Position& pos)
{
    const roadmanager::OpenDrive* od = roadmanager::Position::GetOpenDrive();
    if (od == nullptr)
    {
        return 0.0;
    }
    const OdrSideModel* m = GetSideModel(od);
    if (m == nullptr || m->lane_extras.empty())
    {
        return 0.0;  // fast path: legacy assets carry no lane <speed> -> bit-identical behavior
    }
    roadmanager::Road* road = od->GetRoadById(pos.GetTrackId());
    if (road == nullptr)
    {
        return 0.0;
    }
    return GetLaneSpeedLimit(od, road->GetIdStr(), pos.GetLaneId(), pos.GetS());
}

}  // namespace odr
}  // namespace gt_esmini
