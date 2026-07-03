// OdrJunctionGeom.cpp -- P7 junction geometry + junctionGroup side-model pass (clusters 8/9).
//
// Plan: GT_esmini/docs/opendrive_16_19_support_plan.md P7 (cluster 8 junction boundary /
// elevationGrid / junction-level objects+surface, cluster 9 document-level junctionGroup).
//
// Compiled INTO the upstream RoadManager static target (R1 exception; see
// EnvironmentSimulator/Modules/RoadManager/CMakeLists.txt "# [GT_ODR:cmake]"). L1 contract: parse +
// store + diagnose, no interpretation at storage time. This file DELIBERATELY does not touch the
// OdrJunctionExtras struct/file (P6 conflict-surface minimization) -- it stores into the separate
// OdrJunctionGeomExtras / OdrJunctionGroup structs keyed by junction id.
//
// Sparse: a junction produces an OdrJunctionGeomExtras entry only when it carries a boundary /
// elevationGrid / junction-level objects / junction-level surface. junctionGroup is document-level and
// stored whenever authored.
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

#include "CommonMini.hpp"  // LOG_*, SMALL_NUMBER
#include "RoadManager.hpp"  // roadmanager::OpenDrive / Road / LaneSection / Position (WP4 boundary eval)
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
std::string AttrS(const pugi::xml_node& n, const char* name)
{
    return n.attribute(name).value();
}

// Best-effort CRG file existence (same policy as OdrObjectExtras.cpp; kept local to avoid a shared
// TU dependency). doc_dir "" -> skip the check.
bool CrgFileExists(const std::string& file, const std::string& doc_dir, std::string& resolved_out)
{
    if (file.empty())
    {
        resolved_out.clear();
        return false;
    }
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

void ReadJunctionSurfaceCrgs(const pugi::xml_node&      surface,
                             std::vector<OdrCrgRecord>& out,
                             const std::string&         doc_dir,
                             const std::string&         junction_id)
{
    for (pugi::xml_node crg = surface.child("CRG"); crg; crg = crg.next_sibling("CRG"))
    {
        OdrCrgRecord rec;
        rec.file        = AttrS(crg, "file");
        rec.s_start     = AttrS(crg, "sStart");
        rec.s_end       = AttrS(crg, "sEnd");
        rec.orientation = AttrS(crg, "orientation");
        rec.mode        = AttrS(crg, "mode");
        rec.purpose     = AttrS(crg, "purpose");
        rec.s_offset    = crg.attribute("sOffset").as_double(0.0);
        rec.t_offset    = crg.attribute("tOffset").as_double(0.0);
        rec.x_offset    = crg.attribute("xOffset").as_double(0.0);
        rec.y_offset    = crg.attribute("yOffset").as_double(0.0);
        rec.z_offset    = crg.attribute("zOffset").as_double(0.0);
        rec.z_scale     = crg.attribute("zScale") ? crg.attribute("zScale").as_double(1.0) : 1.0;
        rec.h_offset    = crg.attribute("hOffset").as_double(0.0);
        if (!rec.file.empty())
        {
            std::string resolved;
            rec.file_exists  = CrgFileExists(rec.file, doc_dir, resolved);
            rec.file_checked = true;
            if (!rec.file_exists && !doc_dir.empty())
            {
                LOG_WARN("[GT_ODR] surface CRG file not found (CRG is stored L1 only, never evaluated): {} "
                         "(owner junction {})",
                         resolved.empty() ? rec.file : resolved, junction_id);
            }
        }
        out.push_back(std::move(rec));
    }
}
}  // namespace

namespace detail
{

void ParseJunctionGeom(const pugi::xml_node& root, OdrSideModel& model, const std::string& doc_dir)
{
    // ---- per-junction geometry (cluster 8) ----
    for (pugi::xml_node jn = root.child("junction"); jn; jn = jn.next_sibling("junction"))
    {
        OdrJunctionGeomExtras ex;
        ex.junction_id = AttrS(jn, "id");

        // <boundary>/<segment>
        pugi::xml_node boundary = jn.child("boundary");
        if (boundary)
        {
            for (pugi::xml_node seg = boundary.child("segment"); seg; seg = seg.next_sibling("segment"))
            {
                OdrJunctionBoundarySegment bs;
                bs.type          = AttrS(seg, "type");
                bs.road_id       = AttrS(seg, "roadId");
                bs.boundary_lane = AttrS(seg, "boundaryLane");
                bs.s_start       = AttrS(seg, "sStart");
                bs.s_end         = AttrS(seg, "sEnd");
                ex.boundary.push_back(std::move(bs));
            }
        }

        // <elevationGrid> + <elevation> rows (center/left/right).
        pugi::xml_node grid = jn.child("elevationGrid");
        if (grid)
        {
            ex.has_grid     = true;
            ex.grid_spacing = AttrS(grid, "gridSpacing");
            ex.grid_s_start = AttrS(grid, "sStart");
            for (pugi::xml_node el = grid.child("elevation"); el; el = el.next_sibling("elevation"))
            {
                OdrJunctionGridElevation ge;
                ge.center = AttrS(el, "center");
                ge.left   = AttrS(el, "left");
                ge.right  = AttrS(el, "right");
                ex.grid_elevations.push_back(std::move(ge));
            }
        }

        // junction-level <surface>/<CRG>
        for (pugi::xml_node surface = jn.child("surface"); surface; surface = surface.next_sibling("surface"))
        {
            ReadJunctionSurfaceCrgs(surface, ex.surface_crgs, doc_dir, ex.junction_id);
        }

        // junction-level <objects>/<object> (count L1; geometry deferred to a later WP).
        pugi::xml_node objects = jn.child("objects");
        if (objects)
        {
            for (pugi::xml_node o = objects.child("object"); o; o = o.next_sibling("object"))
            {
                ex.object_count++;
            }
        }

        if (ex.HasAny())
        {
            model.junction_geom.push_back(std::move(ex));
        }
    }

    // ---- document-level <junctionGroup> (cluster 9) ----
    for (pugi::xml_node jg = root.child("junctionGroup"); jg; jg = jg.next_sibling("junctionGroup"))
    {
        OdrJunctionGroup g;
        g.id   = AttrS(jg, "id");
        g.name = AttrS(jg, "name");
        g.type = AttrS(jg, "type");
        for (pugi::xml_node jr = jg.child("junctionReference"); jr; jr = jr.next_sibling("junctionReference"))
        {
            g.members.push_back(AttrS(jr, "junction"));
        }
        model.junction_groups.push_back(std::move(g));
    }
}

}  // namespace detail

// ================================================================================================
// Public accessors (keyed on the OpenDrive* registry key, mirroring the P5 junction accessors).
// ================================================================================================
const OdrJunctionGeomExtras* GetJunctionGeom(const void* opendrive_key, const std::string& junction_id)
{
    const OdrSideModel* m = GetSideModel(opendrive_key);
    if (m == nullptr)
    {
        return nullptr;
    }
    for (const OdrJunctionGeomExtras& ex : m->junction_geom)
    {
        if (ex.junction_id == junction_id)
        {
            return &ex;
        }
    }
    return nullptr;
}

bool GetJunctionGroups(const void* opendrive_key, std::vector<OdrJunctionGroup>& out)
{
    const OdrSideModel* m = GetSideModel(opendrive_key);
    if (m == nullptr || m->junction_groups.empty())
    {
        return false;
    }
    out = m->junction_groups;
    return true;
}

bool IsJunctionInRoundabout(const void* opendrive_key, const std::string& junction_id)
{
    const OdrSideModel* m = GetSideModel(opendrive_key);
    if (m == nullptr)
    {
        return false;
    }
    for (const OdrJunctionGroup& g : m->junction_groups)
    {
        if (g.type != "roundabout")
        {
            continue;
        }
        for (const std::string& member : g.members)
        {
            if (member == junction_id)
            {
                return true;
            }
        }
    }
    return false;
}

// ================================================================================================
// P7 WP4 (cluster 8 L3): authored junction <boundary> -> world polyline. FLAGGED, default OFF.
// ================================================================================================
namespace
{
// ---- feature flag (WP2 SetCurveLocalMaxSegmentLength idiom: env read once, setter overrides) ----
bool g_use_authored_boundary  = false;
bool g_use_authored_inited     = false;

bool EnvIsTruthy(const char* v)
{
    if (v == nullptr || v[0] == '\0')
    {
        return false;
    }
    std::string s(v);
    for (char& c : s)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s == "1" || s == "true" || s == "yes" || s == "on";
}

// Resolve an sStart/sEnd token: XSD t_grEqZeroOrContactPoint allows "start"/"begin"/"end" keywords
// or a non-negative s value. Returns the resolved s clamped to [0, road_len]; `ok` reports parse
// success (an empty token is treated as "start" -> 0.0, which is the schema-implied default anchor).
double ResolveSegmentS(const std::string& tok, double road_len, bool& ok)
{
    ok = true;
    if (tok.empty() || tok == "start" || tok == "begin")
    {
        return 0.0;
    }
    if (tok == "end")
    {
        return road_len;
    }
    char*        endp = nullptr;
    const double v    = std::strtod(tok.c_str(), &endp);
    if (endp == tok.c_str() || !std::isfinite(v))
    {
        ok = false;
        return 0.0;
    }
    if (v < 0.0)
    {
        return 0.0;
    }
    if (v > road_len)
    {
        return road_len;
    }
    return v;
}

// Sample the OUTER edge of `boundary_lane` on `road` from s0->s1 (either direction) and append the
// world points to `out`. Signed lateral offset = sign(boundary_lane) * GetOuterOffset(); a
// boundary_lane of 0 (center) degenerates to the reference line. Emits >= 2 points for a real span
// and exactly 1 for a degenerate (s0==s1) point-segment. Returns false on any RM resolution failure.
bool SampleLaneEdge(roadmanager::Road*             road,
                    int                            boundary_lane,
                    double                         s0,
                    double                         s1,
                    std::vector<OdrBoundaryPoint>& out,
                    const std::string&             junction_id)
{
    const double road_len = road->GetLength();
    const double span     = std::fabs(s1 - s0);

    // step <= 2 m, >= 2 points for a non-degenerate span.
    int n_pts = 2;
    if (span > SMALL_NUMBER)
    {
        n_pts = static_cast<int>(std::ceil(span / 2.0)) + 1;
        if (n_pts < 2)
        {
            n_pts = 2;
        }
    }
    else
    {
        n_pts = 1;  // degenerate point-segment (sStart==sEnd, e.g. a junction contact point)
    }

    roadmanager::Position pos;
    for (int i = 0; i < n_pts; i++)
    {
        const double f = (n_pts == 1) ? 0.0 : static_cast<double>(i) / static_cast<double>(n_pts - 1);
        double       s = s0 + (s1 - s0) * f;
        if (s < 0.0)
        {
            s = 0.0;
        }
        else if (s > road_len)
        {
            s = road_len;
        }

        double t = 0.0;
        if (boundary_lane != 0)
        {
            roadmanager::LaneSection* ls = road->GetLaneSectionByS(s);
            if (ls == nullptr)
            {
                LOG_WARN("[GT_ODR] authored junction boundary: no lane section at road {} s {} (junction {})",
                         road->GetId(),
                         s,
                         junction_id);
                return false;
            }
            const double outer = ls->GetOuterOffset(s, boundary_lane);
            t                  = (boundary_lane < 0) ? -outer : outer;
        }

        if (static_cast<int>(pos.SetTrackPos(road->GetId(), s, t)) < 0)
        {
            LOG_WARN("[GT_ODR] authored junction boundary: SetTrackPos failed at road {} s {} t {} (junction {})",
                     road->GetId(),
                     s,
                     t,
                     junction_id);
            return false;
        }

        OdrBoundaryPoint p;
        p.x = pos.GetX();
        p.y = pos.GetY();
        p.z = pos.GetZ();
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
        {
            LOG_WARN("[GT_ODR] authored junction boundary: non-finite point at road {} s {} (junction {})",
                     road->GetId(),
                     s,
                     junction_id);
            return false;
        }
        out.push_back(p);
    }
    return true;
}
}  // namespace

void SetUseAuthoredJunctionBoundary(bool on)
{
    g_use_authored_boundary = on;
    g_use_authored_inited   = true;  // suppress the env read so tests are deterministic
}

bool GetUseAuthoredJunctionBoundary()
{
    if (!g_use_authored_inited)
    {
        g_use_authored_boundary = EnvIsTruthy(std::getenv("GT_ODR_OSI_AUTHORED_JUNCTION_BOUNDARY"));
        g_use_authored_inited   = true;
    }
    return g_use_authored_boundary;
}

bool BuildAuthoredJunctionBoundaryPolyline(const void*                    opendrive_key,
                                           const std::string&             junction_id,
                                           roadmanager::OpenDrive*        od,
                                           std::vector<OdrBoundaryPoint>& xyz_out)
{
    if (od == nullptr)
    {
        return false;
    }
    const OdrJunctionGeomExtras* geom = GetJunctionGeom(opendrive_key, junction_id);
    if (geom == nullptr || geom->boundary.empty())
    {
        return false;  // no authored boundary -> caller keeps the heuristic (not a warning)
    }

    std::vector<OdrBoundaryPoint> pts;
    for (const OdrJunctionBoundarySegment& seg : geom->boundary)
    {
        if (seg.type == "joint")
        {
            // A "joint" segment is perpendicular to a road start/end -- a STRAIGHT connection between
            // the preceding and following lane segments. The polyline already joins consecutive
            // vertices with a straight edge, so a joint contributes no vertices of its own. Kept as a
            // documented no-op to preserve authored-order fidelity.
            continue;
        }
        if (seg.type != "lane")
        {
            LOG_WARN("[GT_ODR] authored junction boundary: unsupported segment type '{}' (junction {}) -> heuristic",
                     seg.type,
                     junction_id);
            return false;
        }

        // roadId (string) -> numeric road id.
        char*         road_endp = nullptr;
        const long    road_id_l = std::strtol(seg.road_id.c_str(), &road_endp, 10);
        if (road_endp == seg.road_id.c_str())
        {
            LOG_WARN("[GT_ODR] authored junction boundary: non-numeric roadId '{}' (junction {}) -> heuristic",
                     seg.road_id,
                     junction_id);
            return false;
        }
        roadmanager::Road* road = od->GetRoadById(static_cast<id_t>(road_id_l));
        if (road == nullptr)
        {
            LOG_WARN("[GT_ODR] authored junction boundary: dangling roadId '{}' (junction {}) -> heuristic",
                     seg.road_id,
                     junction_id);
            return false;
        }

        // boundaryLane (int; may be empty -> 0/center).
        int boundary_lane = 0;
        if (!seg.boundary_lane.empty())
        {
            char* lane_endp = nullptr;
            boundary_lane   = static_cast<int>(std::strtol(seg.boundary_lane.c_str(), &lane_endp, 10));
            if (lane_endp == seg.boundary_lane.c_str())
            {
                LOG_WARN("[GT_ODR] authored junction boundary: non-numeric boundaryLane '{}' (junction {}) -> heuristic",
                         seg.boundary_lane,
                         junction_id);
                return false;
            }
        }

        const double road_len = road->GetLength();
        bool         s0_ok = false, s1_ok = false;
        const double s0 = ResolveSegmentS(seg.s_start, road_len, s0_ok);
        const double s1 = ResolveSegmentS(seg.s_end, road_len, s1_ok);
        if (!s0_ok || !s1_ok)
        {
            LOG_WARN("[GT_ODR] authored junction boundary: unparseable sStart/sEnd ('{}'/'{}') road {} (junction {}) -> heuristic",
                     seg.s_start,
                     seg.s_end,
                     seg.road_id,
                     junction_id);
            return false;
        }

        if (!SampleLaneEdge(road, boundary_lane, s0, s1, pts, junction_id))
        {
            return false;  // SampleLaneEdge already logged
        }
    }

    if (pts.size() < 3)
    {
        LOG_WARN("[GT_ODR] authored junction boundary: only {} point(s) evaluated (junction {}) -> heuristic",
                 pts.size(),
                 junction_id);
        return false;
    }

    xyz_out.insert(xyz_out.end(), pts.begin(), pts.end());
    return true;
}

}  // namespace odr
}  // namespace gt_esmini
