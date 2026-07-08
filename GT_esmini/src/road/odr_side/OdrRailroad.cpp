// OdrRailroad.cpp -- P9a railroad/station side-model pass (plan §5 P9, cluster 20).
//
// Plan: GT_esmini/docs/opendrive_16_19_support_plan.md P9 (cluster 20: switch/mainTrack/sideTrack/
// partner, station/platform/segment -- L1 storage + RM-API accessors, documented INACTIVE).
//
// Compiled INTO the upstream RoadManager static target (R1 exception; see
// EnvironmentSimulator/Modules/RoadManager/CMakeLists.txt "# [GT_ODR:cmake]"). L1 contract: parse +
// store + diagnose, no interpretation at storage time.
//
// INERT: the railroad (<road>/<railroad>/<switch>) and station (root <station>) families are parsed
// and made queryable (GetRailSwitch / GetRoadRailSwitches / GetStation) but NOTHING consumes them at
// runtime -- there is no rail runtime, no OSI output, no policy. Documented-inactive per plan §5 P9.
//
// Sparse: a road with an empty <railroad/> (the official files carry many) stores nothing; a switch
// with no mainTrack/sideTrack/partner still stores its own attributes (a switch element IS the datum).
#include <cstdlib>
#include <string>
#include <vector>

#include "CommonMini.hpp"  // LOG_*
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
// Read a <switch>/<mainTrack>|<sideTrack> child (@id/@s/@dir). Returns true if the child existed.
bool ReadSwitchTrackLink(const pugi::xml_node& sw, const char* child_name, OdrSwitchTrackLink& out)
{
    pugi::xml_node n = sw.child(child_name);
    if (!n)
    {
        return false;
    }
    out.id  = n.attribute("id").value();
    out.s   = atof(n.attribute("s").value());
    out.dir = n.attribute("dir").value();
    return true;
}
}  // namespace

namespace detail
{

void ParseRailroad(const pugi::xml_node& root, OdrSideModel& model)
{
    // ---- <road>/<railroad>/<switch> : per-road railway switches ----
    for (pugi::xml_node rn = root.child("road"); rn; rn = rn.next_sibling("road"))
    {
        const std::string road_id = rn.attribute("id").value();
        // A road may author at most one <railroad>; iterate defensively in case of duplicates.
        for (pugi::xml_node rr = rn.child("railroad"); rr; rr = rr.next_sibling("railroad"))
        {
            for (pugi::xml_node sw = rr.child("switch"); sw; sw = sw.next_sibling("switch"))
            {
                OdrRailSwitch s;
                s.road_id  = road_id;
                s.name     = sw.attribute("name").value();
                s.id       = sw.attribute("id").value();
                s.position = sw.attribute("position").value();

                s.has_main_track = ReadSwitchTrackLink(sw, "mainTrack", s.main_track);
                s.has_side_track = ReadSwitchTrackLink(sw, "sideTrack", s.side_track);

                pugi::xml_node pn = sw.child("partner");
                if (pn)
                {
                    s.partner_name = pn.attribute("name").value();
                    s.partner_id   = pn.attribute("id").value();
                    s.has_partner  = true;
                }

                model.rail_switches.push_back(std::move(s));
            }
            // Empty <railroad/> stores nothing (intentional: keeps the model sparse on the many
            // official files that author an empty railroad container).
        }
    }

    // ---- root-level <station>/<platform>/<segment> ----
    for (pugi::xml_node st = root.child("station"); st; st = st.next_sibling("station"))
    {
        OdrStation station;
        station.id   = st.attribute("id").value();
        station.name = st.attribute("name").value();
        station.type = st.attribute("type").value();

        for (pugi::xml_node pf = st.child("platform"); pf; pf = pf.next_sibling("platform"))
        {
            OdrStationPlatform platform;
            platform.id   = pf.attribute("id").value();
            platform.name = pf.attribute("name").value();

            for (pugi::xml_node sg = pf.child("segment"); sg; sg = sg.next_sibling("segment"))
            {
                OdrStationSegment seg;
                seg.road_id = sg.attribute("roadId").value();
                seg.s_start = atof(sg.attribute("sStart").value());
                seg.s_end   = atof(sg.attribute("sEnd").value());
                seg.side    = sg.attribute("side").value();
                platform.segments.push_back(std::move(seg));
            }
            station.platforms.push_back(std::move(platform));
        }

        model.stations.push_back(std::move(station));
    }
}

}  // namespace detail

// ---------------------------------------------------------------------------
// Public accessors (P9a). Free functions keyed by the OpenDrive* registry key, mirroring the P5
// GetJunctionExtras pattern -- upstream RoadManager stays pristine (no railroad/station members).
//
// These expose L1 storage ONLY; the data is INERT (no runtime consumer). Documented-inactive per
// plan §5 P9. Direct iteration over GetSideModel(key)->rail_switches / ->stations stays available.
// ---------------------------------------------------------------------------

const OdrRailSwitch* GetRailSwitch(const void* opendrive_key, const std::string& road_id, const std::string& switch_id)
{
    const OdrSideModel* m = GetSideModel(opendrive_key);
    if (m == nullptr)
    {
        return nullptr;
    }
    for (const OdrRailSwitch& s : m->rail_switches)
    {
        if (s.road_id == road_id && s.id == switch_id)
        {
            return &s;
        }
    }
    return nullptr;
}

bool GetRoadRailSwitches(const void* opendrive_key, const std::string& road_id, std::vector<OdrRailSwitch>& out)
{
    const OdrSideModel* m = GetSideModel(opendrive_key);
    if (m == nullptr)
    {
        return false;  // no side model registered; out untouched
    }
    out.clear();
    for (const OdrRailSwitch& s : m->rail_switches)
    {
        if (s.road_id == road_id)
        {
            out.push_back(s);
        }
    }
    return true;  // true even when empty (road had an empty/absent <railroad>)
}

const OdrStation* GetStation(const void* opendrive_key, const std::string& station_id)
{
    const OdrSideModel* m = GetSideModel(opendrive_key);
    if (m == nullptr)
    {
        return nullptr;
    }
    for (const OdrStation& st : m->stations)
    {
        if (st.id == station_id)
        {
            return &st;
        }
    }
    return nullptr;
}

}  // namespace odr
}  // namespace gt_esmini
