// OdrLaneLayers.cpp -- P8 1.9 lane-layer selection + s-range merge (clusters 4/22).
//
//   * SelectLanesLayer  -- the [GT_ODR:lane-layers] fork hook target: pick (or synthesize) the
//                          <lanes> node RoadManager walks. Mode from env GT_ODR_LANE_LAYERS (D1).
//   * BuildMergedLanes  -- pure merge core (unit-testable): permanent + temporary layer -> one
//                          synthetic <lanes> over the temporary s-range (D2: laneOffset re-anchor +
//                          section re-open with width/height/roadMark sOffset shift).
//   * ParseLaneLayers   -- L1 shadow storage of the authored layer layout into model.lane_layers.
//
// Compiled INTO the upstream RoadManager static target (R1 swap-zone exception, see
// EnvironmentSimulator/Modules/RoadManager/CMakeLists.txt marker "# [GT_ODR:cmake]").
//
// Plan: GT_esmini/docs/opendrive_16_19_support_plan.md P8 (design D1-D3, D6).
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "CommonMini.hpp"  // SMALL_NUMBER, LOG_*
#include "gt_esmini/road/OdrSideModel.hpp"
#include "logger.hpp"
#include "odr_side_internal.hpp"
#include "pugixml.hpp"

namespace gt_esmini
{
namespace odr
{
namespace detail
{
// Defined in OdrSideModel.cpp (share the registry mutex + the pending merged-lanes map).
std::mutex& MergedLanesMutex();
std::map<std::string, std::unique_ptr<pugi::xml_document>>& PendingMergedLanesFor(const void* opendrive_key);

// Exposed so the unit test can exercise the pure merge core directly.
void BuildMergedLanes(const pugi::xml_node& permanent,
                      const pugi::xml_node& temporary,
                      double                road_length,
                      pugi::xml_node        out_lanes);
}  // namespace detail

namespace
{

// ---------------------------------------------------------------------------
// Mode selection (plan D1). Read ONCE from env GT_ODR_LANE_LAYERS, case-insensitive; unset /
// "permanent" -> permanent (default), "temporary" -> temporary opt-in merge, unknown -> WARN +
// permanent. No runtime switching (the value is latched on first query).
// ---------------------------------------------------------------------------
enum class LaneLayerMode
{
    Permanent,
    Temporary
};

LaneLayerMode ResolveModeFromEnv()
{
    const char* raw = std::getenv("GT_ODR_LANE_LAYERS");
    if (raw == nullptr || raw[0] == '\0')
    {
        return LaneLayerMode::Permanent;
    }
    std::string v(raw);
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (v == "permanent")
    {
        return LaneLayerMode::Permanent;
    }
    if (v == "temporary")
    {
        return LaneLayerMode::Temporary;
    }
    LOG_WARN("[GT_ODR:lane-layers] unknown GT_ODR_LANE_LAYERS='{}' (expected 'permanent' or 'temporary'); using permanent",
             raw);
    return LaneLayerMode::Permanent;
}

// Test override (plan D1 testability, same idiom as WP4 SetUseAuthoredJunctionBoundary). -1 = use the
// env latch; 0 = force permanent; 1 = force temporary. Default -1.
int& ModeOverride()
{
    static int override_mode = -1;
    return override_mode;
}

LaneLayerMode LaneMode()
{
    if (ModeOverride() == 0)
    {
        return LaneLayerMode::Permanent;
    }
    if (ModeOverride() == 1)
    {
        return LaneLayerMode::Temporary;
    }
    // Env latch: read ONCE (D1: no runtime switching in production).
    static const LaneLayerMode mode = ResolveModeFromEnv();
    return mode;
}

// True when a <lanes> element's @layer marks it the temporary layer. Absent / any other value ==
// permanent (per D2: an attribute-less <lanes> is treated as permanent).
bool IsTemporaryLayer(const pugi::xml_node& lanes)
{
    return std::strcmp(lanes.attribute("layer").value(), "temporary") == 0;
}

double AttrD(const pugi::xml_node& n, const char* name, double def = 0.0)
{
    pugi::xml_attribute a = n.attribute(name);
    return a ? a.as_double(def) : def;
}

// Set (creating if absent) a numeric attribute using pugixml's locale-independent double writer.
void SetNumAttr(pugi::xml_node n, const char* name, double value)
{
    pugi::xml_attribute a = n.attribute(name);
    if (!a)
    {
        a = n.append_attribute(name);
    }
    a.set_value(value);
}

// ---------------------------------------------------------------------------
// Cubic Taylor-shift: coefficients of the SAME polynomial re-expressed around an origin shifted
// forward by ds (>= 0). p(x) = a + b*dx + c*dx^2 + d*dx^3 with dx from the old origin; the returned
// coefficients are for dx' from the origin + ds. (Same algebra as OdrLaneExtras.cpp ShiftTo, inlined
// to keep this TU self-contained.)
// ---------------------------------------------------------------------------
struct Cubic
{
    double a = 0.0, b = 0.0, c = 0.0, d = 0.0;
};

Cubic ReadCubic(const pugi::xml_node& n)
{
    return Cubic{AttrD(n, "a"), AttrD(n, "b"), AttrD(n, "c"), AttrD(n, "d")};
}

Cubic ShiftCubic(const Cubic& p, double ds)
{
    Cubic q;
    q.a = p.a + p.b * ds + p.c * ds * ds + p.d * ds * ds * ds;
    q.b = p.b + 2.0 * p.c * ds + 3.0 * p.d * ds * ds;
    q.c = p.c + 3.0 * p.d * ds;
    q.d = p.d;
    return q;
}

void WriteCubic(pugi::xml_node n, const Cubic& c)
{
    SetNumAttr(n, "a", c.a);
    SetNumAttr(n, "b", c.b);
    SetNumAttr(n, "c", c.c);
    SetNumAttr(n, "d", c.d);
}

// ---------------------------------------------------------------------------
// Temporary-layer coverage [t0, t1) (plan D2). t0 = smallest temporary laneSection @s; t1 = last
// temporary laneSection @s + @length (defaulting to road_length when @length is absent). A middle
// @length that does not meet the next section's @s (> 1e-6) WARNs (coverage is still treated as the
// continuous [t0, t1]).
// ---------------------------------------------------------------------------
struct TempRange
{
    double t0 = 0.0;
    double t1 = 0.0;
};

TempRange ComputeTempRange(const pugi::xml_node& temporary, double road_length)
{
    TempRange r;
    std::vector<pugi::xml_node> secs;
    for (pugi::xml_node s = temporary.child("laneSection"); s; s = s.next_sibling("laneSection"))
    {
        secs.push_back(s);
    }
    if (secs.empty())
    {
        r.t0 = 0.0;
        r.t1 = road_length;
        return r;
    }
    r.t0 = AttrD(secs.front(), "s");
    for (std::size_t i = 0; i < secs.size(); ++i)
    {
        const double s_i = AttrD(secs[i], "s");
        if (i + 1 < secs.size())
        {
            const double s_next = AttrD(secs[i + 1], "s");
            if (secs[i].attribute("length"))
            {
                const double end_i = s_i + AttrD(secs[i], "length");
                if (std::fabs(end_i - s_next) > 1e-6)
                {
                    LOG_WARN("[GT_ODR:lane-layers] temporary laneSection @s={:.3f} @length ends at {:.3f} but the next "
                             "section starts at {:.3f}; treating coverage as continuous",
                             s_i,
                             end_i,
                             s_next);
                }
            }
        }
        else
        {
            // Last section: t1 = s + length (or road end when @length omitted).
            r.t1 = secs[i].attribute("length") ? (s_i + AttrD(secs[i], "length")) : road_length;
        }
    }
    return r;
}

// The permanent laneSection ACTIVE at s (greatest @s <= s + eps), or a null node when none precedes s.
pugi::xml_node ActivePermanentSection(const pugi::xml_node& permanent, double s)
{
    pugi::xml_node found;
    for (pugi::xml_node sec = permanent.child("laneSection"); sec; sec = sec.next_sibling("laneSection"))
    {
        if (AttrD(sec, "s") <= s + 1e-6)
        {
            found = sec;
        }
        else
        {
            break;  // laneSections are authored in ascending @s
        }
    }
    return found;
}

// The permanent laneOffset ACTIVE at s, or a null node when none precedes s.
pugi::xml_node ActivePermanentLaneOffset(const pugi::xml_node& permanent, double s)
{
    pugi::xml_node found;
    for (pugi::xml_node lo = permanent.child("laneOffset"); lo; lo = lo.next_sibling("laneOffset"))
    {
        if (AttrD(lo, "s") <= s + 1e-6)
        {
            found = lo;
        }
        else
        {
            break;
        }
    }
    return found;
}

// ---------------------------------------------------------------------------
// Re-open a permanent laneSection at s=t1 (plan D2 (d)): deep-copy the section active at t1, set its
// @s to t1, and advance each lane's width/height/roadMark @sOffset by ds0 = t1 - original section s.
// An entry with sOffset <= ds0 is Taylor-shifted to sOffset=0 (width polynomials only; height/roadMark
// values are copied as-is); entries with sOffset > ds0 get sOffset -= ds0. Only the LAST active entry
// with sOffset <= ds0 survives at sOffset=0 (earlier ones are subsumed).
// ---------------------------------------------------------------------------
void ReopenSectionSOffsets(pugi::xml_node lane, double ds0)
{
    for (const char* tag : {"width", "height", "roadMark"})
    {
        const bool is_width = std::strcmp(tag, "width") == 0;

        // Gather the child nodes of this tag with their sOffset.
        std::vector<pugi::xml_node> nodes;
        for (pugi::xml_node n = lane.child(tag); n; n = n.next_sibling(tag))
        {
            nodes.push_back(n);
        }
        // Find the last node with sOffset <= ds0 (the one active at t1).
        int active = -1;
        for (int i = 0; i < static_cast<int>(nodes.size()); ++i)
        {
            if (AttrD(nodes[i], "sOffset") <= ds0 + 1e-6)
            {
                active = i;
            }
        }
        for (int i = 0; i < static_cast<int>(nodes.size()); ++i)
        {
            const double so = AttrD(nodes[i], "sOffset");
            if (so > ds0 + 1e-6)
            {
                SetNumAttr(nodes[i], "sOffset", so - ds0);  // shifted into the re-opened section
                continue;
            }
            if (i == active)
            {
                // The active entry becomes sOffset=0. width is a polynomial -> Taylor-shift by ds0-so;
                // height/roadMark carry values only -> copy as-is (no polynomial to shift).
                if (is_width)
                {
                    const double ds = ds0 - so;
                    WriteCubic(nodes[i], ShiftCubic(ReadCubic(nodes[i]), ds));
                }
                SetNumAttr(nodes[i], "sOffset", 0.0);
            }
            else
            {
                // Superseded by the active entry -> drop it.
                lane.remove_child(nodes[i]);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// SelectLanesLayer cache: build (once) or fetch the synthetic <lanes> for (opendrive_key, road_id).
// Returns the <lanes> element of the cached document.
// ---------------------------------------------------------------------------
pugi::xml_node BuildOrFetchMerged(const void*           opendrive_key,
                                  const std::string&    road_id,
                                  const pugi::xml_node& permanent,
                                  const pugi::xml_node& temporary,
                                  double                road_length)
{
    std::lock_guard<std::mutex> lock(detail::MergedLanesMutex());
    auto&                       by_road = detail::PendingMergedLanesFor(opendrive_key);
    auto                        it      = by_road.find(road_id);
    if (it != by_road.end() && it->second)
    {
        return it->second->child("lanes");  // idempotent: same node on every re-call
    }

    auto           doc   = std::unique_ptr<pugi::xml_document>(new pugi::xml_document());
    pugi::xml_node lanes = doc->append_child("lanes");
    detail::BuildMergedLanes(permanent, temporary, road_length, lanes);
    pugi::xml_node result       = lanes;
    by_road[road_id]            = std::move(doc);
    return result;
}

}  // namespace

// ================================================================================================
// Public: SelectLanesLayer (the [GT_ODR:lane-layers] fork hook target).
// ================================================================================================
pugi::xml_node SelectLanesLayer(const pugi::xml_node& road_node, const void* opendrive_key)
{
    // Collect the road's <lanes> layers.
    pugi::xml_node permanent;  // first non-temporary (incl. attribute-less)
    pugi::xml_node temporary;  // first temporary
    int            n_permanent = 0;
    int            n_temporary = 0;
    for (pugi::xml_node l = road_node.child("lanes"); l; l = l.next_sibling("lanes"))
    {
        if (IsTemporaryLayer(l))
        {
            if (!temporary)
            {
                temporary = l;
            }
            ++n_temporary;
        }
        else
        {
            if (!permanent)
            {
                permanent = l;
            }
            ++n_permanent;
        }
    }

    if (n_permanent > 1)
    {
        LOG_WARN("[GT_ODR:lane-layers] road '{}' has {} non-temporary <lanes>; using the first, ignoring the rest",
                 road_node.attribute("id").value(),
                 n_permanent);
    }
    if (n_temporary > 1)
    {
        LOG_WARN("[GT_ODR:lane-layers] road '{}' has {} temporary <lanes>; using the first, ignoring the rest",
                 road_node.attribute("id").value(),
                 n_temporary);
    }

    // Fast paths (identical behavior to upstream road_node.child("lanes") -- no copy):
    //   * no permanent layer authored but a temporary one exists -> use the temporary as-is (WARN);
    //   * single-<lanes> road / permanent mode / no temporary layer -> return permanent unchanged.
    if (!permanent && temporary)
    {
        LOG_WARN("[GT_ODR:lane-layers] road '{}' has only a temporary <lanes> (no permanent layer); using it as-is",
                 road_node.attribute("id").value());
        return temporary;
    }
    if (LaneMode() == LaneLayerMode::Permanent || !temporary || !permanent)
    {
        return permanent ? permanent : road_node.child("lanes");
    }

    // Temporary opt-in merge (plan D2). Cache the synthetic doc per (key, road).
    const std::string road_id      = road_node.attribute("id").value();
    const double      road_length  = AttrD(road_node, "length");
    return BuildOrFetchMerged(opendrive_key, road_id, permanent, temporary, road_length);
}

void SetLaneLayerModeForTest(bool temporary_on)
{
    ModeOverride() = temporary_on ? 1 : 0;
}

void SetLaneLayerModeUseEnv()
{
    ModeOverride() = -1;
}

namespace detail
{

// ================================================================================================
// BuildMergedLanes -- pure merge core (plan D2). `out_lanes` is an EMPTY <lanes> element the caller
// owns; this fills it with laneOffset + laneSection children in ascending-s order (deterministic
// global-id assignment when RoadManager later walks it).
// ================================================================================================
void BuildMergedLanes(const pugi::xml_node& permanent,
                      const pugi::xml_node& temporary,
                      double                road_length,
                      pugi::xml_node        out_lanes)
{
    const TempRange range = ComputeTempRange(temporary, road_length);
    const double    t0    = range.t0;
    const double    t1    = range.t1;

    // ---- (a/b/c) laneOffset: permanent s<t0, all temporary, permanent s>=t1, plus a re-anchored
    // permanent entry at exactly t1 when none starts there but a permanent offset is active. ----
    for (pugi::xml_node lo = permanent.child("laneOffset"); lo; lo = lo.next_sibling("laneOffset"))
    {
        if (AttrD(lo, "s") < t0 - 1e-6)
        {
            out_lanes.append_copy(lo);
        }
    }
    for (pugi::xml_node lo = temporary.child("laneOffset"); lo; lo = lo.next_sibling("laneOffset"))
    {
        out_lanes.append_copy(lo);
    }
    // Re-anchor: if no permanent laneOffset begins exactly at t1 but one is active there, Taylor-shift
    // it to s=t1 so the offset is continuous across the temporary range's end.
    bool perm_offset_at_t1 = false;
    for (pugi::xml_node lo = permanent.child("laneOffset"); lo; lo = lo.next_sibling("laneOffset"))
    {
        if (std::fabs(AttrD(lo, "s") - t1) < 1e-6)
        {
            perm_offset_at_t1 = true;
            break;
        }
    }
    if (!perm_offset_at_t1 && t1 < road_length - 1e-6)
    {
        pugi::xml_node active = ActivePermanentLaneOffset(permanent, t1);
        if (active)
        {
            const double   ds     = t1 - AttrD(active, "s");
            pugi::xml_node copied = out_lanes.append_copy(active);
            SetNumAttr(copied, "s", t1);
            WriteCubic(copied, ShiftCubic(ReadCubic(active), ds));
        }
    }
    for (pugi::xml_node lo = permanent.child("laneOffset"); lo; lo = lo.next_sibling("laneOffset"))
    {
        if (AttrD(lo, "s") >= t1 - 1e-6)
        {
            out_lanes.append_copy(lo);
        }
    }

    // ---- (d) laneSection: permanent s<t0, all temporary, permanent s>=t1, plus a re-opened permanent
    // section at s=t1 when none starts there but a permanent section is active there (t1 < length). ----
    for (pugi::xml_node sec = permanent.child("laneSection"); sec; sec = sec.next_sibling("laneSection"))
    {
        if (AttrD(sec, "s") < t0 - 1e-6)
        {
            out_lanes.append_copy(sec);
        }
    }
    for (pugi::xml_node sec = temporary.child("laneSection"); sec; sec = sec.next_sibling("laneSection"))
    {
        out_lanes.append_copy(sec);
    }
    bool perm_section_at_t1 = false;
    for (pugi::xml_node sec = permanent.child("laneSection"); sec; sec = sec.next_sibling("laneSection"))
    {
        if (std::fabs(AttrD(sec, "s") - t1) < 1e-6)
        {
            perm_section_at_t1 = true;
            break;
        }
    }
    if (!perm_section_at_t1 && t1 < road_length - 1e-6)
    {
        pugi::xml_node active = ActivePermanentSection(permanent, t1);
        if (active)
        {
            const double   ds0    = t1 - AttrD(active, "s");
            pugi::xml_node copied = out_lanes.append_copy(active);
            SetNumAttr(copied, "s", t1);
            copied.remove_attribute("length");  // re-opened section spans to the next section / road end
            for (const char* side : {"left", "center", "right"})
            {
                pugi::xml_node side_node = copied.child(side);
                if (!side_node)
                {
                    continue;
                }
                for (pugi::xml_node lane = side_node.child("lane"); lane; lane = lane.next_sibling("lane"))
                {
                    ReopenSectionSOffsets(lane, ds0);
                }
            }
        }
    }
    for (pugi::xml_node sec = permanent.child("laneSection"); sec; sec = sec.next_sibling("laneSection"))
    {
        if (AttrD(sec, "s") >= t1 - 1e-6)
        {
            out_lanes.append_copy(sec);
        }
    }
}

// ================================================================================================
// ParseLaneLayers -- L1 shadow storage (plan P8 cluster 4/22). Sparse: one entry per road that
// authored @layer on a <lanes> OR more than one <lanes> element.
// ================================================================================================
void ParseLaneLayers(const pugi::xml_node& root, OdrSideModel& model)
{
    const bool temp_mode = (LaneMode() == LaneLayerMode::Temporary);

    for (pugi::xml_node road = root.child("road"); road; road = road.next_sibling("road"))
    {
        std::vector<pugi::xml_node> lanes_nodes;
        bool                        any_layer_attr = false;
        for (pugi::xml_node l = road.child("lanes"); l; l = l.next_sibling("lanes"))
        {
            lanes_nodes.push_back(l);
            if (l.attribute("layer"))
            {
                any_layer_attr = true;
            }
        }
        if (lanes_nodes.size() <= 1 && !any_layer_attr)
        {
            continue;  // legacy single untagged <lanes> -> no entry (sparse)
        }

        OdrRoadLaneLayers rec;
        rec.road_id           = road.attribute("id").value();
        const double road_len = AttrD(road, "length");

        pugi::xml_node temporary_node;
        for (const pugi::xml_node& l : lanes_nodes)
        {
            OdrLaneLayer layer;
            layer.name = l.attribute("layer").value();
            for (pugi::xml_node lo = l.child("laneOffset"); lo; lo = lo.next_sibling("laneOffset"))
            {
                layer.lane_offset_count++;
            }
            for (pugi::xml_node sec = l.child("laneSection"); sec; sec = sec.next_sibling("laneSection"))
            {
                OdrLaneLayerSection s;
                s.s          = AttrD(sec, "s");
                s.has_length = !sec.attribute("length").empty();
                s.length     = s.has_length ? AttrD(sec, "length") : 0.0;
                for (const char* side : {"left", "center", "right"})
                {
                    pugi::xml_node side_node = sec.child(side);
                    if (!side_node)
                    {
                        continue;
                    }
                    for (pugi::xml_node lane = side_node.child("lane"); lane; lane = lane.next_sibling("lane"))
                    {
                        s.lane_count++;
                    }
                }
                layer.sections.push_back(std::move(s));
            }
            if (IsTemporaryLayer(l) && !temporary_node)
            {
                temporary_node = l;
            }
            rec.layers.push_back(std::move(layer));
        }

        if (temporary_node)
        {
            rec.has_temporary        = true;
            const TempRange range     = ComputeTempRange(temporary_node, road_len);
            rec.temp_s_start          = range.t0;
            rec.temp_s_end            = range.t1;
        }
        // active_mode is which layer the parse actually selected for this road (D2): temporary only
        // when the merge path is taken (temp mode AND both layers present); permanent otherwise.
        const bool has_permanent = std::any_of(lanes_nodes.begin(), lanes_nodes.end(), [](const pugi::xml_node& l) {
            return !IsTemporaryLayer(l);
        });
        rec.active_mode = (temp_mode && rec.has_temporary && has_permanent) ? "temporary" : "permanent";

        model.lane_layers.push_back(std::move(rec));
    }
}

}  // namespace detail
}  // namespace odr
}  // namespace gt_esmini
