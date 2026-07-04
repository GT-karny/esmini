// OdrObjectExtras.cpp -- P7 object-family + road-lateralProfile/surface side-model pass.
//
// Plan: GT_esmini/docs/opendrive_16_19_support_plan.md P7 (cluster 17 lateralProfile shape/
// crossSectionSurface, cluster 18 surface/CRG, cluster 19 object details -- material/perpToRoad/
// skeleton/borders/outline attrs+markings/objectReference/bridge/repeat lateral polynomial).
//
// Compiled INTO the upstream RoadManager static target (R1 exception; see
// EnvironmentSimulator/Modules/RoadManager/CMakeLists.txt "# [GT_ODR:cmake]"). L1 contract: parse +
// store + diagnose, no interpretation at storage time. The two fork helpers (AppendCurveLocalCorners /
// AdjustRepeatInstancePose) are implemented here too; their fork call sites are wired in a LATER WP,
// so they are temporarily unreferenced by the fork (that is expected).
//
// Sparse storage: a per-object entry is pushed only when the object carries a P7 datum; a per-road
// lateralProfile entry only when <shape> or <crossSectionSurface> was authored; road-surface CRG only
// when a road-level <surface><CRG> exists.
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

#include "CommonMini.hpp"  // id_t, SMALL_NUMBER, LOG_*
#include "RoadManager.hpp"
#include "gt_esmini/road/OdrSideModel.hpp"
#include "logger.hpp"
#include "odr_side_internal.hpp"
#include "pugixml.hpp"

namespace gt_esmini
{
namespace odr
{
namespace
{
// ---- small helpers ----------------------------------------------------------------------------

double AttrD(const pugi::xml_node& n, const char* name, double def = 0.0)
{
    pugi::xml_attribute a = n.attribute(name);
    return a ? a.as_double(def) : def;
}
std::string AttrS(const pugi::xml_node& n, const char* name)
{
    return n.attribute(name).value();
}
bool HasAttr(const pugi::xml_node& n, const char* name)
{
    return !n.attribute(name).empty();
}

// Best-effort file-existence check for the CRG diagnostic. `doc_dir` = xodr directory ("" -> skip).
// Absolute @file is checked as-is; relative is resolved against doc_dir. No dependency beyond a
// portable fopen (matches odr_side's no-extra-platform-API constraint). Never evaluates the CRG.
bool CrgFileExists(const std::string& file, const std::string& doc_dir, std::string& resolved_out)
{
    if (file.empty())
    {
        resolved_out.clear();
        return false;
    }
    // Absolute if it starts with '/', '\\' or has a drive letter "X:".
    const bool is_abs = (file.size() > 0 && (file[0] == '/' || file[0] == '\\')) ||
                        (file.size() > 1 && file[1] == ':');
    std::string path = (is_abs || doc_dir.empty()) ? file : (doc_dir + "/" + file);
    resolved_out     = path;
    FILE* f          = std::fopen(path.c_str(), "rb");
    if (f != nullptr)
    {
        std::fclose(f);
        return true;
    }
    return false;
}

// Read a <surface>/<CRG> element into `out` (all attrs incl. 1.9 xOffset/yOffset). Runs the
// file-existence diagnostic once (LOG_WARN when missing). `owner` describes the CRG owner for the log.
void ReadCrg(const pugi::xml_node& crg, OdrCrgRecord& out, const std::string& doc_dir, const std::string& owner)
{
    out.file        = AttrS(crg, "file");
    out.s_start     = AttrS(crg, "sStart");
    out.s_end       = AttrS(crg, "sEnd");
    out.orientation = AttrS(crg, "orientation");
    out.mode        = AttrS(crg, "mode");
    out.purpose     = AttrS(crg, "purpose");
    out.s_offset    = AttrD(crg, "sOffset");
    out.t_offset    = AttrD(crg, "tOffset");
    out.x_offset    = AttrD(crg, "xOffset");
    out.y_offset    = AttrD(crg, "yOffset");
    out.z_offset    = AttrD(crg, "zOffset");
    out.z_scale     = HasAttr(crg, "zScale") ? AttrD(crg, "zScale", 1.0) : 1.0;
    out.h_offset    = AttrD(crg, "hOffset");

    if (!out.file.empty())
    {
        std::string resolved;
        out.file_exists  = CrgFileExists(out.file, doc_dir, resolved);
        out.file_checked = !doc_dir.empty() || !out.file.empty();
        if (!out.file_exists && !doc_dir.empty())
        {
            LOG_WARN("[GT_ODR] surface CRG file not found (CRG is stored L1 only, never evaluated): {} (owner {})",
                     resolved.empty() ? out.file : resolved,
                     owner);
        }
    }
}

// Read a <surface> element's <CRG> children into `out` (a road/object/junction may carry several).
void ReadSurfaceCrgs(const pugi::xml_node& surface, std::vector<OdrCrgRecord>& out, const std::string& doc_dir, const std::string& owner)
{
    for (pugi::xml_node crg = surface.child("CRG"); crg; crg = crg.next_sibling("CRG"))
    {
        OdrCrgRecord rec;
        ReadCrg(crg, rec, doc_dir, owner);
        out.push_back(std::move(rec));
    }
}

// Read one outline element's L1 (attrs + outline-level <markings>). `singular` distinguishes the
// object/outline (singular) form from the object/outlines/outline (plural) form.
void ReadOutline(const pugi::xml_node& ol, bool singular, OdrObjectOutline& out)
{
    out.id            = AttrS(ol, "id");
    out.fill_type     = AttrS(ol, "fillType");
    out.lane_type     = AttrS(ol, "laneType");
    out.outer         = AttrS(ol, "outer");
    out.closed        = AttrS(ol, "closed");
    out.singular_form = singular;

    pugi::xml_node markings = ol.child("markings");
    if (markings)
    {
        for (pugi::xml_node m = markings.child("marking"); m; m = m.next_sibling("marking"))
        {
            OdrObjectMarking mk;
            mk.width        = AttrS(m, "width");
            mk.color        = AttrS(m, "color");
            mk.z_offset     = AttrS(m, "zOffset");
            mk.space_length = AttrS(m, "spaceLength");
            mk.line_length  = AttrS(m, "lineLength");
            mk.start_offset = AttrS(m, "startOffset");
            mk.stop_offset  = AttrS(m, "stopOffset");
            mk.side         = AttrS(m, "side");
            mk.weight       = AttrS(m, "weight");
            for (pugi::xml_node cr = m.child("cornerReference"); cr; cr = cr.next_sibling("cornerReference"))
            {
                mk.corner_reference_ids.push_back(AttrS(cr, "id"));
            }
            out.markings.push_back(std::move(mk));
        }
    }
}

// Read a <skeleton> element's polyline geometry (vertexRoad/vertexLocal). Other skeleton geometry
// (paramPoly3 etc.) is stored as an empty-vertex polyline placeholder for L1 completeness.
void ReadSkeleton(const pugi::xml_node& skel, std::vector<OdrSkeletonPolyline>& out)
{
    for (pugi::xml_node geo = skel.first_child(); geo; geo = geo.next_sibling())
    {
        if (geo.type() != pugi::node_element)
        {
            continue;
        }
        OdrSkeletonPolyline poly;
        poly.id = AttrS(geo, "id");
        for (pugi::xml_node v = geo.first_child(); v; v = v.next_sibling())
        {
            if (v.type() != pugi::node_element)
            {
                continue;
            }
            OdrSkeletonVertex sv;
            sv.kind               = v.name();
            sv.s                  = AttrS(v, "s");
            sv.t                  = AttrS(v, "t");
            sv.u                  = AttrS(v, "u");
            sv.v                  = AttrS(v, "v");
            sv.dz                 = AttrS(v, "dz");
            sv.radius             = AttrS(v, "radius");
            sv.id                 = AttrS(v, "id");
            sv.intersection_point = AttrS(v, "intersectionPoint");
            poly.vertices.push_back(std::move(sv));
        }
        out.push_back(std::move(poly));
    }
}

// Read the 1.9 repeat lateral polynomial (@bT/@cT/@dT/@detachFromReferenceLine + base s/length/t) from
// the FIRST <repeat> child that carries any of them. has_poly gates the sparse fast path.
void ReadRepeatPoly(const pugi::xml_node& object, OdrRepeatLateralPoly& out)
{
    for (pugi::xml_node rep = object.child("repeat"); rep; rep = rep.next_sibling("repeat"))
    {
        const bool has = HasAttr(rep, "bT") || HasAttr(rep, "cT") || HasAttr(rep, "dT") ||
                         HasAttr(rep, "detachFromReferenceLine");
        if (!has)
        {
            continue;
        }
        out.base_s                     = AttrD(rep, "s");
        out.base_length                = AttrD(rep, "length");
        out.t_start                    = AttrD(rep, "tStart");
        out.t_end                      = AttrD(rep, "tEnd");
        out.bT                         = AttrD(rep, "bT");
        out.cT                         = AttrD(rep, "cT");
        out.dT                         = AttrD(rep, "dT");
        out.detach_from_reference_line = (AttrS(rep, "detachFromReferenceLine") == "true");
        out.has_poly                   = true;
        return;  // one lateral-poly record per object (first repeat carrying it)
    }
}

// Read one crossSectionSurface <coefficients> row (cubic in s).
OdrCssCoefficients ReadCssCoefficients(const pugi::xml_node& c)
{
    OdrCssCoefficients row;
    row.s = AttrD(c, "s");
    row.a = AttrD(c, "a");
    row.b = AttrD(c, "b");
    row.c = AttrD(c, "c");
    row.d = AttrD(c, "d");
    return row;
}

void ReadCoefficientsList(const pugi::xml_node& parent, std::vector<OdrCssCoefficients>& out)
{
    for (pugi::xml_node c = parent.child("coefficients"); c; c = c.next_sibling("coefficients"))
    {
        out.push_back(ReadCssCoefficients(c));
    }
}

}  // namespace

namespace detail
{

// ================================================================================================
// T1/T4/T5 -- parse pass (L1 storage). No mutation of `od`; pure DOM read into `model`.
// ================================================================================================
void ParseObjectExtras(const pugi::xml_node& root, OdrSideModel& model, const std::string& doc_dir)
{
    for (pugi::xml_node road = root.child("road"); road; road = road.next_sibling("road"))
    {
        const std::string road_id = AttrS(road, "id");

        // ---- road-level <surface>/<CRG> (cluster 18) ----
        for (pugi::xml_node surface = road.child("surface"); surface; surface = surface.next_sibling("surface"))
        {
            ReadSurfaceCrgs(surface, model.road_surface_crgs, doc_dir, "road " + road_id);
        }

        // ---- road <lateralProfile> shape / crossSectionSurface (cluster 17) ----
        pugi::xml_node lp = road.child("lateralProfile");
        if (lp)
        {
            OdrRoadLateralProfile rec;
            rec.road_id = road_id;
            bool any    = false;

            for (pugi::xml_node sh = lp.child("shape"); sh; sh = sh.next_sibling("shape"))
            {
                OdrLateralShape s;
                s.s = AttrD(sh, "s");
                s.t = AttrD(sh, "t");
                s.a = AttrD(sh, "a");
                s.b = AttrD(sh, "b");
                s.c = AttrD(sh, "c");
                s.d = AttrD(sh, "d");
                rec.shapes.push_back(s);
                any = true;
            }

            pugi::xml_node css = lp.child("crossSectionSurface");
            if (css)
            {
                rec.has_css = true;
                any         = true;
                pugi::xml_node toff = css.child("tOffset");
                if (toff)
                {
                    ReadCoefficientsList(toff, rec.css_t_offset);
                }
                pugi::xml_node strips = css.child("surfaceStrips");
                if (strips)
                {
                    for (pugi::xml_node st = strips.child("strip"); st; st = st.next_sibling("strip"))
                    {
                        OdrCssStrip strip;
                        strip.id   = AttrS(st, "id");
                        strip.mode = AttrS(st, "mode");
                        pugi::xml_node width = st.child("width");
                        if (width)
                        {
                            ReadCoefficientsList(width, strip.width);
                        }
                        // Exactly one t-height flavor may be present (constant|linear|quadratic|cubic).
                        static const char* const kFlavors[] = {"constant", "linear", "quadratic", "cubic"};
                        for (const char* flavor : kFlavors)
                        {
                            pugi::xml_node hn = st.child(flavor);
                            if (hn)
                            {
                                strip.term_kind = flavor;
                                ReadCoefficientsList(hn, strip.height);
                                break;
                            }
                        }
                        rec.css_strips.push_back(std::move(strip));
                    }
                }
            }

            if (any)
            {
                model.road_lateral.push_back(std::move(rec));
            }
        }

        // ---- road/objects children: object / objectReference / bridge (cluster 19) ----
        pugi::xml_node objects = road.child("objects");
        if (!objects)
        {
            continue;
        }

        for (pugi::xml_node obj = objects.child("object"); obj; obj = obj.next_sibling("object"))
        {
            OdrObjectExtras ex;
            ex.road_id   = road_id;
            ex.object_id = AttrS(obj, "id");

            if (HasAttr(obj, "perpToRoad"))
            {
                ex.perp_to_road_present = true;
                ex.perp_to_road         = AttrS(obj, "perpToRoad");
            }

            for (pugi::xml_node mat = obj.child("material"); mat; mat = mat.next_sibling("material"))
            {
                OdrObjectMaterial m;
                m.surface         = AttrS(mat, "surface");
                m.friction        = AttrS(mat, "friction");
                m.roughness       = AttrS(mat, "roughness");
                m.road_mark_color = AttrS(mat, "roadMarkColor");
                ex.materials.push_back(std::move(m));
            }

            // Both outline forms: object/outline (singular) and object/outlines/outline (plural).
            for (pugi::xml_node ol = obj.child("outline"); ol; ol = ol.next_sibling("outline"))
            {
                OdrObjectOutline o;
                ReadOutline(ol, /*singular=*/true, o);
                ex.outlines.push_back(std::move(o));
            }
            pugi::xml_node outlines = obj.child("outlines");
            if (outlines)
            {
                for (pugi::xml_node ol = outlines.child("outline"); ol; ol = ol.next_sibling("outline"))
                {
                    OdrObjectOutline o;
                    ReadOutline(ol, /*singular=*/false, o);
                    ex.outlines.push_back(std::move(o));
                }
            }

            for (pugi::xml_node skel = obj.child("skeleton"); skel; skel = skel.next_sibling("skeleton"))
            {
                ReadSkeleton(skel, ex.skeleton);
            }

            pugi::xml_node borders = obj.child("borders");
            if (borders)
            {
                for (pugi::xml_node b = borders.child("border"); b; b = b.next_sibling("border"))
                {
                    OdrObjectBorder br;
                    br.width                = AttrS(b, "width");
                    br.type                 = AttrS(b, "type");
                    br.outline_id           = AttrS(b, "outlineId");
                    br.use_complete_outline = AttrS(b, "useCompleteOutline");
                    ex.borders.push_back(std::move(br));
                }
            }

            for (pugi::xml_node surface = obj.child("surface"); surface; surface = surface.next_sibling("surface"))
            {
                ReadSurfaceCrgs(surface, ex.surface_crgs, doc_dir, "object " + ex.object_id + " (road " + road_id + ")");
            }

            ReadRepeatPoly(obj, ex.repeat_poly);

            // P8 (1.9): @temporary / @invalidated plain xs:boolean flags (sparse -- record only when
            // authored).
            if (HasAttr(obj, "temporary"))
            {
                ex.temporary_present = true;
                ex.temporary         = obj.attribute("temporary").as_bool(false);
            }
            if (HasAttr(obj, "invalidated"))
            {
                ex.invalidated_present = true;
                ex.invalidated         = obj.attribute("invalidated").as_bool(false);
            }
            // P8 cluster 22 L1: <validity> records carrying @layer (sparse).
            for (pugi::xml_node vn = obj.child("validity"); vn; vn = vn.next_sibling("validity"))
            {
                if (vn.attribute("layer").empty())
                {
                    continue;
                }
                OdrValidityLayer vl;
                vl.from_lane = vn.attribute("fromLane").value();
                vl.to_lane   = vn.attribute("toLane").value();
                vl.layer     = vn.attribute("layer").value();
                ex.validity_layers.push_back(std::move(vl));
            }

            if (ex.HasAny())
            {
                model.object_extras.push_back(std::move(ex));
            }
        }

        // <objectReference> (cluster 19b): store L1; clone synthesis is stage 2.
        for (pugi::xml_node ref = objects.child("objectReference"); ref; ref = ref.next_sibling("objectReference"))
        {
            OdrObjectReference r;
            r.road_id      = road_id;
            r.ref_id       = AttrS(ref, "id");
            r.s            = AttrD(ref, "s");
            r.t            = AttrD(ref, "t");
            r.z_offset     = AttrD(ref, "zOffset");
            r.valid_length = AttrS(ref, "validLength");
            r.orientation  = AttrS(ref, "orientation");
            model.object_references.push_back(std::move(r));
        }

        // <bridge> (cluster 19b): store L1; footprint synthesis is stage 2.
        for (pugi::xml_node br = objects.child("bridge"); br; br = br.next_sibling("bridge"))
        {
            OdrBridge b;
            b.road_id = road_id;
            b.id      = AttrS(br, "id");
            b.name    = AttrS(br, "name");
            b.type    = AttrS(br, "type");
            b.s       = AttrD(br, "s");
            b.length  = AttrD(br, "length");
            model.bridges.push_back(std::move(b));
        }
    }
}

}  // namespace detail

// ================================================================================================
// Synthesis + degrade helpers shared across the stage-2 passes.
// ================================================================================================
namespace
{
// Reserved synthetic object-id bases (distinct ranges so a debugger/consumer can tell them apart;
// all far below ID_UNDEFINED=0xffffffff and above authored ids -- same rationale as the P5 crosswalk
// base 9e8). Collision policy mirrors SynthesizeCrosswalks: scan the owning road's authored objects,
// and if ANY authored id is >= our base (or == the candidate), WARN + SKIP synthesis (never mutate
// authored objects, never crash).
constexpr unsigned int kBridgeSynthIdBase   = 910000000u;  // 9.1e8
constexpr unsigned int kObjRefSynthIdBase    = 920000000u;  // 9.2e8

// Small bridge deck height [m] for the synthesized footprint box (declared constant; bridges have no
// authored "height", they span a length).
constexpr double kBridgeDeckHeight = 0.1;

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

void WorldAt(id_t road_id, double s, double t, double& x, double& y, double& z)
{
    roadmanager::Position pos;
    pos.SetTrackPos(road_id, s, t);
    x = pos.GetX();
    y = pos.GetY();
    z = pos.GetZ();
}

// True if the road carries an authored object id in [base, ...) or == cand (collision -> skip synth).
bool HasReservedIdCollision(roadmanager::Road* road, unsigned int base, unsigned int cand)
{
    for (idx_t j = 0; j < road->GetNumberOfObjects(); j++)
    {
        const roadmanager::RMObject* o = road->GetRoadObject(j);
        if (o == nullptr)
        {
            continue;
        }
        const unsigned int oid = static_cast<unsigned int>(o->GetId());
        if (oid >= base || oid == cand)
        {
            return true;
        }
    }
    return false;
}

// Locate a runtime RMObject by authored id string, searching `first` then all roads. Returns the
// object and (out) its owning road. nullptr on miss.
roadmanager::RMObject* FindObjectById(roadmanager::OpenDrive* od,
                                      roadmanager::Road*      first,
                                      const std::string&      obj_id,
                                      roadmanager::Road**     owner_out)
{
    unsigned int want = 0;
    bool         numeric = !obj_id.empty();
    for (char ch : obj_id)
    {
        if (ch < '0' || ch > '9')
        {
            numeric = false;
            break;
        }
    }
    if (numeric)
    {
        want = static_cast<unsigned int>(std::strtoul(obj_id.c_str(), nullptr, 10));
    }
    auto scan = [&](roadmanager::Road* r) -> roadmanager::RMObject* {
        if (r == nullptr)
        {
            return nullptr;
        }
        for (idx_t j = 0; j < r->GetNumberOfObjects(); j++)
        {
            roadmanager::RMObject* o = r->GetRoadObject(j);
            if (o != nullptr && numeric && static_cast<unsigned int>(o->GetId()) == want)
            {
                if (owner_out != nullptr)
                {
                    *owner_out = r;
                }
                return o;
            }
        }
        return nullptr;
    };
    if (roadmanager::RMObject* hit = scan(first))
    {
        return hit;
    }
    for (unsigned int i = 0; i < od->GetNumOfRoads(); i++)
    {
        roadmanager::Road* r = od->GetRoadByIdx(i);
        if (r == first)
        {
            continue;
        }
        if (roadmanager::RMObject* hit = scan(r))
        {
            return hit;
        }
    }
    return nullptr;
}

}  // namespace

namespace detail
{

// ================================================================================================
// T3a -- SynthesizeBridges: <bridge> -> BRIDGE RMObject spanning [s, s+length] across the full lane
// section width. OSI mapping BRIDGE->TYPE_BRIDGE already exists (GT_OSIReporter.cpp:708-713).
// ================================================================================================
void SynthesizeBridges(OdrSideModel& model, roadmanager::OpenDrive* od)
{
    if (od == nullptr || model.bridges.empty())
    {
        return;  // legacy fast path
    }

    unsigned int synth_index = 0;
    for (OdrBridge& br : model.bridges)
    {
        roadmanager::Road* road = FindRoad(od, br.road_id);
        if (road == nullptr)
        {
            LOG_WARN("[GT_ODR] bridge synth: road '{}' not found; skipping bridge {}", br.road_id, br.id);
            continue;
        }
        const double road_len = road->GetLength();
        double       s0        = br.s;
        double       s1        = br.s + br.length;
        if (s1 <= s0 + SMALL_NUMBER || s0 < 0.0 || s0 > road_len)
        {
            LOG_WARN("[GT_ODR] bridge synth: bridge {} on road '{}' has degenerate span s[{:.2f},{:.2f}]; skipping",
                     br.id, br.road_id, s0, s1);
            continue;
        }
        s1 = std::min(s1, road_len);
        const double mid_s = 0.5 * (s0 + s1);

        // Bridges span the whole road: use the total lane-section width at mid-s (any lane type) as the
        // lateral extent. Fall back to a conservative 8 m when unresolvable.
        double full_w = road->GetWidth(mid_s, 0, roadmanager::Lane::LaneType::LANE_TYPE_ANY);
        if (full_w < 1.0)
        {
            full_w = 8.0;
        }
        const double half_w = 0.5 * full_w;

        const unsigned int cand_id = kBridgeSynthIdBase + synth_index;
        if (HasReservedIdCollision(road, kBridgeSynthIdBase, cand_id))
        {
            LOG_WARN("[GT_ODR] bridge synth: road '{}' has an authored object id in the reserved synthetic range "
                     "(>= {}); skipping bridge {}",
                     br.road_id, kBridgeSynthIdBase, br.id);
            continue;
        }

        const id_t rid = road->GetId();
        // Closed 4-corner footprint across the full width, spanning [s0,s1] (deck height kBridgeDeckHeight).
        roadmanager::Outline* outline = new roadmanager::Outline(cand_id, roadmanager::Outline::FillType::FILL_TYPE_UNDEFINED, true);
        const double          corners[4][2] = {{s0, +half_w}, {s1, +half_w}, {s1, -half_w}, {s0, -half_w}};
        for (int c = 0; c < 4; c++)
        {
            outline->AddCorner(static_cast<roadmanager::OutlineCorner*>(
                new roadmanager::OutlineCornerRoad(rid, corners[c][0], corners[c][1], 0.0, kBridgeDeckHeight, 0.0, 0.0, 0.0)));
        }

        double ox = 0.0, oy = 0.0, oz = 0.0;
        WorldAt(rid, mid_s, 0.0, ox, oy, oz);
        roadmanager::Position opos;
        opos.SetTrackPos(rid, mid_s, 0.0);
        const std::string name = br.name.empty() ? ("bridge_" + br.id) : br.name;

        roadmanager::RMObject* obj = new roadmanager::RMObject(mid_s,
                                                              0.0,
                                                              cand_id,
                                                              name,
                                                              roadmanager::RoadObject::Orientation(),
                                                              0.0,
                                                              roadmanager::RMObject::ObjectType::BRIDGE,
                                                              /*length*/ (s1 - s0),
                                                              /*height*/ kBridgeDeckHeight,
                                                              /*width*/ full_w,
                                                              /*heading*/ 0.0,
                                                              0.0,
                                                              0.0,
                                                              ox,
                                                              oy,
                                                              oz,
                                                              opos.GetHRoad());
        obj->AddOutline(outline);
        road->AddObject(obj);
        br.synth_object_id = cand_id;
        synth_index++;

        LOG_INFO("[GT_ODR] bridge synth: bridge {} ('{}', type '{}') -> BRIDGE obj id {} on road '{}' "
                 "s[{:.2f},{:.2f}] width {:.2f}",
                 br.id, name, br.type, cand_id, br.road_id, s0, s1, full_w);
    }
}

// ================================================================================================
// T3b -- SynthesizeObjectReferences: <objectReference> -> clone RMObject of the referenced object at
// the reference's s/t/zOffset/orientation on the DECLARING road. cornerLocal outlines rebase to the
// new center (same u/v); cornerRoad outlines translate by (ds,dt) when SAME road, else skip (WARN,
// bbox still emitted). Dangling reference -> WARN + skip.
// ================================================================================================
void SynthesizeObjectReferences(OdrSideModel& model, roadmanager::OpenDrive* od)
{
    if (od == nullptr || model.object_references.empty())
    {
        return;  // legacy fast path
    }

    unsigned int synth_index = 0;
    for (OdrObjectReference& ref : model.object_references)
    {
        roadmanager::Road* decl = FindRoad(od, ref.road_id);
        if (decl == nullptr)
        {
            LOG_WARN("[GT_ODR] objectReference synth: declaring road '{}' not found; skipping ref id {}",
                     ref.road_id, ref.ref_id);
            continue;
        }
        roadmanager::Road*     owner = nullptr;
        roadmanager::RMObject* src   = FindObjectById(od, decl, ref.ref_id, &owner);
        if (src == nullptr)
        {
            LOG_WARN("[GT_ODR] objectReference synth: referenced object id '{}' (declared on road '{}') not found "
                     "(dangling); skipping",
                     ref.ref_id, ref.road_id);
            continue;
        }

        const unsigned int cand_id = kObjRefSynthIdBase + synth_index;
        if (HasReservedIdCollision(decl, kObjRefSynthIdBase, cand_id))
        {
            LOG_WARN("[GT_ODR] objectReference synth: declaring road '{}' has an authored object id in the reserved "
                     "synthetic range (>= {}); skipping ref id {}",
                     ref.road_id, kObjRefSynthIdBase, ref.ref_id);
            continue;
        }

        const id_t   rid = decl->GetId();
        double       ox = 0.0, oy = 0.0, oz = 0.0;
        WorldAt(rid, ref.s, ref.t, ox, oy, oz);
        roadmanager::Position opos;
        opos.SetTrackPos(rid, ref.s, ref.t);

        roadmanager::RoadObject::Orientation orient = roadmanager::RoadObject::Orientation::NONE;
        if (ref.orientation == "+")
        {
            orient = roadmanager::RoadObject::Orientation::POSITIVE;
        }
        else if (ref.orientation == "-")
        {
            orient = roadmanager::RoadObject::Orientation::NEGATIVE;
        }

        roadmanager::RMObject* clone = new roadmanager::RMObject(ref.s,
                                                               ref.t,
                                                               cand_id,
                                                               src->GetName() + "_ref",
                                                               orient,
                                                               ref.z_offset,
                                                               src->GetType(),
                                                               src->GetLength(),
                                                               src->GetHeight(),
                                                               src->GetWidth(),
                                                               src->GetHOffset(),
                                                               src->GetPitch(),
                                                               src->GetRoll(),
                                                               ox,
                                                               oy,
                                                               oz,
                                                               opos.GetHRoad());

        // Rebase outlines. cornerLocal: same u/v, new center s/t (the clone's placement). cornerRoad:
        // translate corners by (ds,dt) only when the reference sits on the SAME road as the source
        // object; otherwise skip (bbox still emitted via the RMObject dimensions).
        const bool same_road = (owner == decl);
        for (unsigned int oi = 0; oi < src->GetNumberOfOutlines(); oi++)
        {
            roadmanager::Outline* src_ol = src->GetOutline(oi);
            if (src_ol == nullptr)
            {
                continue;
            }
            roadmanager::Outline* new_ol = new roadmanager::Outline(cand_id, src_ol->fillType_, src_ol->closed_);
            bool                  kept    = false;
            for (roadmanager::OutlineCorner* c : src_ol->corner_)
            {
                if (auto* cl = dynamic_cast<roadmanager::OutlineCornerLocal*>(c))
                {
                    // Same local u/v/z/height; rebased to the clone's center s/t on the declaring road.
                    roadmanager::OutlineCornerLocal* nc =
                        new roadmanager::OutlineCornerLocal(rid, ref.s, ref.t, cl->u_, cl->v_, cl->zLocal_, cl->height_, cl->heading_);
                    nc->SetId(cl->GetId());
                    new_ol->AddCorner(static_cast<roadmanager::OutlineCorner*>(nc));
                    kept = true;
                }
                else if (auto* cr = dynamic_cast<roadmanager::OutlineCornerRoad*>(c))
                {
                    if (!same_road)
                    {
                        continue;  // cannot faithfully translate a road-frame corner onto a different road
                    }
                    const double ds = ref.s - src->GetS();
                    const double dt = ref.t - src->GetT();
                    roadmanager::OutlineCornerRoad* nc = new roadmanager::OutlineCornerRoad(
                        rid, cr->s_ + ds, cr->t_ + dt, cr->dz_, cr->height_, ref.s, ref.t, cr->center_heading_);
                    nc->SetId(cr->GetId());
                    new_ol->AddCorner(static_cast<roadmanager::OutlineCorner*>(nc));
                    kept = true;
                }
            }
            if (kept && new_ol->corner_.size() >= 2)
            {
                clone->AddOutline(new_ol);
            }
            else
            {
                if (!same_road && !src_ol->corner_.empty())
                {
                    LOG_WARN("[GT_ODR] objectReference synth: ref id {} on road '{}' references object on road '{}'; "
                             "road-frame outline not rebased (bbox only)",
                             ref.ref_id, ref.road_id, owner ? owner->GetIdStr() : std::string("?"));
                }
                delete new_ol;
            }
        }

        decl->AddObject(clone);
        ref.synth_object_id = cand_id;
        synth_index++;

        LOG_INFO("[GT_ODR] objectReference synth: ref id {} -> clone obj id {} on road '{}' at s={:.2f} t={:.2f}",
                 ref.ref_id, cand_id, ref.road_id, ref.s, ref.t);
    }
}

// ================================================================================================
// T4 -- ApplyLateralProfileDegrade: lateralProfile <shape>/<crossSectionSurface> -> equivalent
// superelevation (WARN'd approximation). DECLARED SEMANTICS (implemented exactly):
//
//   * Only when the road has NO authored <superelevation> (any authored -> WARN "degrade skipped",
//     store L1 only). authored_superelevation is detected via Road::GetNumberOfSuperElevations() > 0
//     (the fork parses <superelevation> into that list before this stage-2 pass runs).
//
//   * <shape>: group by s; equivalent superelevation angle theta(s) = atan(b) where b is the LINEAR
//     coefficient of the shape polynomial (slope dz/dt at t=0) at that s. Add one Elevation(s, theta,
//     0, 0, 0) per distinct shape s via Road::AddSuperElevation (piecewise CONSTANT roll). esmini's
//     superelevation poly evaluates to the road ROLL (UpdateRollByS: roll = poly3.Evaluate(ds)); the
//     world z contribution at lateral t is z += sin(roll)*t (Track2XYZ: v_out[2]=cos(pitch)*sin(roll)*t
//     with pitch=0 here). So Elevation(s, atan(b),0,0,0) reproduces z(t)=sin(atan(b))*t = b/sqrt(1+b^2)*t
//     -- i.e. the crossfall slope b at t=0, approximated (the native t-dependent cubic shape is NOT
//     evaluated).
//
//   * <crossSectionSurface>: the equivalent linear crossfall is the LINEAR-strip coefficient covering
//     the driving carriageway. surfaceStrips <strip> with a <linear><coefficients a=.../> express a
//     per-t height slope; we take the representative b_css = the linear coefficient of the inner-right
//     strip (@id="-1"), or the first linear strip found if that id is absent. Emit the same single
//     piecewise-constant Elevation(0, atan(b_css),0,0,0). (tOffset only shifts the lateral origin; it
//     does not change the t=0 crossfall slope, so it is not folded into the equivalent angle.)
//
//   * Emit LOG_WARN ONCE per road, explicitly stating it is an APPROXIMATION.
// ================================================================================================
void ApplyLateralProfileDegrade(OdrSideModel& model, roadmanager::OpenDrive* od)
{
    if (od == nullptr || model.road_lateral.empty())
    {
        return;  // legacy fast path
    }

    for (OdrRoadLateralProfile& lp : model.road_lateral)
    {
        roadmanager::Road* road = FindRoad(od, lp.road_id);
        if (road == nullptr)
        {
            continue;
        }

        lp.authored_superelevation = (road->GetNumberOfSuperElevations() > 0);
        if (lp.authored_superelevation)
        {
            LOG_WARN("[GT_ODR] road {}: lateralProfile <shape>/<crossSectionSurface> present but authored "
                     "superelevation exists; degrade skipped (L1 stored only)",
                     lp.road_id);
            continue;
        }

        bool applied = false;
        if (!lp.shapes.empty())
        {
            // Group by distinct s (DOM order gives ascending s in the fixtures; add each unique s once).
            double last_s = -1e30;
            for (const OdrLateralShape& sh : lp.shapes)
            {
                if (std::fabs(sh.s - last_s) < SMALL_NUMBER)
                {
                    continue;  // same s already emitted
                }
                const double theta = std::atan(sh.b);  // slope dz/dt at t=0 -> equivalent roll angle
                road->AddSuperElevation(new roadmanager::Elevation(sh.s, theta, 0.0, 0.0, 0.0));
                lp.equiv_crossfall_slope = sh.b;  // record the last representative slope
                last_s                   = sh.s;
                applied                  = true;
            }
        }
        else if (lp.has_css)
        {
            // Representative linear crossfall: inner-right strip (@id="-1") linear coefficient, else the
            // first linear strip's coefficient.
            double b_css     = 0.0;
            bool   found     = false;
            for (const OdrCssStrip& st : lp.css_strips)
            {
                if (st.term_kind == "linear" && !st.height.empty() && st.id == "-1")
                {
                    b_css = st.height.front().a;
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                for (const OdrCssStrip& st : lp.css_strips)
                {
                    if (st.term_kind == "linear" && !st.height.empty())
                    {
                        b_css = st.height.front().a;
                        found = true;
                        break;
                    }
                }
            }
            const double theta = std::atan(b_css);
            road->AddSuperElevation(new roadmanager::Elevation(0.0, theta, 0.0, 0.0, 0.0));
            lp.equiv_crossfall_slope = b_css;
            applied                  = true;
        }

        if (applied)
        {
            lp.degrade_applied = true;
            LOG_WARN("[GT_ODR] road {}: lateralProfile <shape>/<crossSectionSurface> approximated as equivalent "
                     "superelevation (crossfall at t=0, slope {:.5f}); native t-dependent elevation is NOT "
                     "evaluated (approximation)",
                     lp.road_id, lp.equiv_crossfall_slope);
        }
    }
}

}  // namespace detail

// ================================================================================================
// T5 -- public accessors (keyed on the OpenDrive* registry key like the P5 junction accessors).
// ================================================================================================
const OdrObjectExtras* GetObjectExtras(const void* opendrive_key, const std::string& road_id, const std::string& object_id)
{
    const OdrSideModel* m = GetSideModel(opendrive_key);
    if (m == nullptr)
    {
        return nullptr;
    }
    for (const OdrObjectExtras& ex : m->object_extras)
    {
        if (ex.road_id == road_id && ex.object_id == object_id)
        {
            return &ex;
        }
    }
    return nullptr;
}

const OdrRoadLateralProfile* GetRoadLateralProfile(const void* opendrive_key, const std::string& road_id)
{
    const OdrSideModel* m = GetSideModel(opendrive_key);
    if (m == nullptr)
    {
        return nullptr;
    }
    for (const OdrRoadLateralProfile& lp : m->road_lateral)
    {
        if (lp.road_id == road_id)
        {
            return &lp;
        }
    }
    return nullptr;
}

// ================================================================================================
// T2 -- fork helpers (implemented + unit-tested now; fork call sites wired in a LATER WP).
// ================================================================================================
namespace
{
// Configurable curveLocal tessellation knob. Default 1.0 m max chord; env GT_ODR_CURVELOCAL_SEGLEN
// overrides on first use; the setter overrides for tests. Guarded by a simple init flag (no atomics
// needed: parse is single-threaded, and the value is idempotent).
double g_curvelocal_seglen  = 1.0;
bool   g_curvelocal_inited  = false;

double CurveLocalSegLen()
{
    if (!g_curvelocal_inited)
    {
        const char* env = std::getenv("GT_ODR_CURVELOCAL_SEGLEN");
        if (env != nullptr && env[0] != '\0')
        {
            const double v = std::atof(env);
            if (v > SMALL_NUMBER)
            {
                g_curvelocal_seglen = v;
            }
        }
        g_curvelocal_inited = true;
    }
    return g_curvelocal_seglen;
}
}  // namespace

void SetCurveLocalMaxSegmentLength(double meters)
{
    g_curvelocal_seglen = (meters > SMALL_NUMBER) ? meters : 1.0;
    g_curvelocal_inited = true;  // suppress the env read so tests are deterministic
}

double GetCurveLocalMaxSegmentLength()
{
    return CurveLocalSegLen();
}

bool AppendCurveLocalCorners(const pugi::xml_node& curve_local_node,
                             roadmanager::Road*    road,
                             roadmanager::RMObject* obj,
                             roadmanager::Outline* outline,
                             unsigned int&         next_corner_id)
{
    if (!curve_local_node || road == nullptr || obj == nullptr || outline == nullptr)
    {
        return false;
    }

    const double u0     = AttrD(curve_local_node, "u");
    const double v0     = AttrD(curve_local_node, "v");
    const double z0     = AttrD(curve_local_node, "z");
    const double height = AttrD(curve_local_node, "height");
    const double length = AttrD(curve_local_node, "length");
    const double hdg    = AttrD(curve_local_node, "hdg");

    if (!(length > SMALL_NUMBER) || !std::isfinite(length) || !std::isfinite(u0) || !std::isfinite(v0) ||
        !std::isfinite(hdg))
    {
        LOG_WARN("[GT_ODR] curveLocal: degenerate/NaN input (length {:.4f}); skipping segment", length);
        return false;
    }

    // Deterministic sample count: at least 3 points/segment, otherwise ceil(length / maxSeg) + 1.
    const double seg = CurveLocalSegLen();
    int          n   = static_cast<int>(std::ceil(length / std::max(seg, SMALL_NUMBER))) + 1;
    if (n < 3)
    {
        n = 3;
    }

    // Child geometry (choice: arc | line | paramPoly3). Tessellate in object-local (u,v).
    pugi::xml_node arc_n   = curve_local_node.child("arc");
    pugi::xml_node line_n  = curve_local_node.child("line");
    pugi::xml_node pp3_n   = curve_local_node.child("paramPoly3");

    int appended = 0;
    // Sample p in [0, length]; the LAST point of one curveLocal coincides with the FIRST of the next
    // only for polylines -- for a closed outline we emit ALL n points (the outline's closed_ handles
    // final closure, no duplicate closing point). Winding preserved by walking p ascending.
    for (int i = 0; i < n; i++)
    {
        const double p  = length * static_cast<double>(i) / (n - 1);
        double       du = 0.0, dv = 0.0;  // local offset from (u0,v0) in the curveLocal's own frame

        if (arc_n)
        {
            const double k = AttrD(arc_n, "curvature");
            if (std::fabs(k) < SMALL_NUMBER)
            {
                du = p;  // straight
                dv = 0.0;
            }
            else
            {
                const double r     = 1.0 / k;
                const double theta = p * k;  // swept angle
                // Arc starting along +local-x, center at (0, r): standard OpenDRIVE arc.
                du = r * std::sin(theta);
                dv = r * (1.0 - std::cos(theta));
            }
        }
        else if (line_n)
        {
            du = p;
            dv = 0.0;
        }
        else if (pp3_n)
        {
            // pRange normalized -> parameter in [0,1]; arcLength -> [0,length]. Evaluate the cubic.
            const std::string prange = AttrS(pp3_n, "pRange");
            const double       t      = (prange == "normalized") ? (p / length) : p;
            const double       aU = AttrD(pp3_n, "aU"), bU = AttrD(pp3_n, "bU"), cU = AttrD(pp3_n, "cU"), dU = AttrD(pp3_n, "dU");
            const double       aV = AttrD(pp3_n, "aV"), bV = AttrD(pp3_n, "bV"), cV = AttrD(pp3_n, "cV"), dV = AttrD(pp3_n, "dV");
            du = aU + bU * t + cU * t * t + dU * t * t * t;
            dv = aV + bV * t + cV * t * t + dV * t * t * t;
        }
        else
        {
            LOG_WARN("[GT_ODR] curveLocal: no arc/line/paramPoly3 child; skipping");
            return false;
        }

        // Rotate the local segment frame by hdg and translate to (u0, v0).
        const double cu = std::cos(hdg), su = std::sin(hdg);
        const double u  = u0 + du * cu - dv * su;
        const double v  = v0 + du * su + dv * cu;
        if (!std::isfinite(u) || !std::isfinite(v))
        {
            continue;  // skip a NaN sample, keep the rest
        }

        roadmanager::OutlineCornerLocal* corner =
            new roadmanager::OutlineCornerLocal(road->GetId(), obj->GetS(), obj->GetT(), u, v, z0, height, obj->GetHOffset());
        corner->SetId(next_corner_id++);
        outline->AddCorner(static_cast<roadmanager::OutlineCorner*>(corner));
        appended++;
    }

    return appended > 0;
}

bool AdjustRepeatInstancePose(const roadmanager::RMObject* obj,
                              const roadmanager::Road*     road,
                              double                       s_inst,
                              double                       frac,
                              double&                      s_io,
                              double&                      t_io)
{
    // PARAMETERIZATION READING (documented judgment call):
    //   The 1.9 Object XSD annotates @bT/@cT/@dT only as "Coefficient b/c/d for t." with no unit and
    //   no stated parameter. Two readings are possible: (A) polynomial in meters along the repeat
    //   (t += bT*ds + cT*ds^2 + dT*ds^3, ds = s - repeat.s), (B) polynomial in a normalized fraction
    //   f in [0,1]. Reading (A) yields implausibly large lateral shifts for realistic coefficients on
    //   long repeats (e.g. the P7 fixture's bT=0.02 over 120 m => +2.4 m from bT alone). Reading (B)
    //   yields sub-metre corrections consistent with a lateral-profile refinement on top of the
    //   tStart->tEnd linear ramp. Per the plan's declared semantics we implement (B):
    //       t(f) = tStart + (tEnd - tStart)*f + bT*f + cT*f^2 + dT*f^3,  f in [0,1].
    //   The linear (tStart->tEnd) ramp is the base the fork already applies; here we ADD the polynomial
    //   correction (bT*f + cT*f^2 + dT*f^3) to the incoming t_io so the caller keeps its ramp.
    if (obj == nullptr || road == nullptr)
    {
        return false;
    }
    // The side model is keyed on the OpenDrive singleton (the same instance the fork registered under).
    const OdrSideModel* m = GetSideModel(static_cast<const void*>(roadmanager::Position::GetOpenDrive()));
    if (m == nullptr)
    {
        return false;
    }
    // Resolve the object's lateral-poly record by (road_id, object_id).
    const std::string road_id = road->GetIdStr().empty() ? std::to_string(road->GetId()) : road->GetIdStr();
    const OdrObjectExtras* ex = nullptr;
    for (const OdrObjectExtras& e : m->object_extras)
    {
        if (e.road_id == road_id && static_cast<unsigned int>(std::strtoul(e.object_id.c_str(), nullptr, 10)) ==
                                        static_cast<unsigned int>(obj->GetId()))
        {
            ex = &e;
            break;
        }
    }
    if (ex == nullptr || !ex->repeat_poly.has_poly)
    {
        return false;  // legacy fast path: no lateral-poly record
    }

    const OdrRepeatLateralPoly& rp = ex->repeat_poly;
    double f = frac;
    if (f < 0.0)
    {
        f = 0.0;
    }
    else if (f > 1.0)
    {
        f = 1.0;
    }
    const double poly = rp.bT * f + rp.cT * f * f + rp.dT * f * f * f;
    t_io += poly;

    if (rp.detach_from_reference_line)
    {
        // DETACH: connect the repeat start/end world positions as a straight chord and remap the
        // instance onto it. This runs post-parse (GetRepeatInstances is called by viewer/OSI AFTER
        // SetRoadOSI), so world evaluation via roadmanager::Position is valid. We compute the world
        // point on the chord at fraction f, then inverse-map it back to (s,t) on the road via
        // XYZ2TrackPos so the caller's downstream road-frame consumers stay consistent.
        double sx = 0.0, sy = 0.0, sz = 0.0, ex_ = 0.0, ey = 0.0, ez = 0.0;
        WorldAt(road->GetId(), rp.base_s, rp.t_start, sx, sy, sz);
        WorldAt(road->GetId(), rp.base_s + rp.base_length, rp.t_end, ex_, ey, ez);
        const double cx = sx + (ex_ - sx) * f;
        const double cy = sy + (ey - sy) * f;
        roadmanager::Position pos;
        // XYZ2TrackPos maps a world point back to road/s/t on the current road set.
        pos.SetInertiaPos(cx, cy, 0.0, 0.0, 0.0, 0.0);
        if (pos.GetTrackId() != ID_UNDEFINED)
        {
            s_io = pos.GetS();
            t_io = pos.GetT();
        }
        else
        {
            // Inverse mapping unreliable -> degrade to "approximated as attached" (documented fallback).
            LOG_WARN("[GT_ODR] repeat detachFromReferenceLine: inverse map failed for object {} on road {}; "
                     "approximated as attached",
                     obj->GetId(), road_id);
        }
    }
    else
    {
        s_io = s_inst;  // attached: s follows the repeat instance placement unchanged
    }
    return true;
}

}  // namespace odr
}  // namespace gt_esmini

