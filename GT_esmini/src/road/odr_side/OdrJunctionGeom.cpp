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
#include <cstdlib>
#include <string>
#include <vector>

#include "CommonMini.hpp"  // LOG_*
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

}  // namespace odr
}  // namespace gt_esmini
