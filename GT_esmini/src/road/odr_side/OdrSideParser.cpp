// OdrSideParser.cpp -- cluster 1 (version awareness) + additionalData serialization helper.
//
// Plan: GT_esmini/docs/opendrive_16_19_support_plan.md P1.
#include <sstream>
#include <string>

#include "CommonMini.hpp"
#include "logger.hpp"  // LOG_WARN/LOG_INFO/LOG_ERROR (fmt-style)
#include "odr_side_internal.hpp"
#include "pugixml.hpp"

namespace gt_esmini
{
namespace odr
{
namespace detail
{

void ReadVersion(const pugi::xml_node& root, OdrSideModel& model)
{
    pugi::xml_node header = root.child("header");
    if (!header)
    {
        LOG_WARN("[GT_ODR] no <header> element; version unknown (best-effort parse)");
        // rev_major / rev_minor stay -1 (their default).
        return;
    }

    pugi::xml_attribute maj = header.attribute("revMajor");
    pugi::xml_attribute min = header.attribute("revMinor");

    model.rev_major = maj.empty() ? -1 : maj.as_int(-1);
    if (min.empty())
    {
        model.rev_minor = -1;
        LOG_WARN("[GT_ODR] header@revMinor missing; storing -1 (best-effort parse)");
    }
    else
    {
        model.rev_minor = min.as_int(-1);
    }

    model.header_name    = header.attribute("name").value();
    model.header_version = header.attribute("version").value();

    // One LOG_INFO per parse: detected version + banner.
    LOG_INFO("[GT_ODR] detected OpenDRIVE version {}.{} -- GT ODR side model active",
             model.rev_major,
             model.rev_minor);

    // Unknown-version best-effort note (revMajor != 1, or revMinor beyond the range this plan
    // targets). This is a soft warning; parsing still proceeds.
    if (model.rev_major != 1 || model.rev_minor > 9)
    {
        LOG_WARN("[GT_ODR] OpenDRIVE version {}.{} is outside the supported 1.0-1.9 range; parsing best-effort",
                 model.rev_major,
                 model.rev_minor);
    }
}

std::string NodeToXml(const pugi::xml_node& node)
{
    std::ostringstream oss;
    // Compact: no indentation, no XML declaration. Captures the element + its whole subtree.
    node.print(oss, "", pugi::format_raw);
    return oss.str();
}

void ParseRoadTypeExtras(const pugi::xml_node& root, OdrSideModel& model)
{
    // P9b: road/<type @country> (t_road_type countryCode, 1.6+). Upstream reads @type/@s only;
    // @country is stored here L1 (sparse: one record per <type> that authors the attribute).
    for (pugi::xml_node road = root.child("road"); road; road = road.next_sibling("road"))
    {
        for (pugi::xml_node type_node = road.child("type"); type_node; type_node = type_node.next_sibling("type"))
        {
            pugi::xml_attribute country = type_node.attribute("country");
            if (country.empty())
            {
                continue;
            }
            OdrRoadTypeExtra rec;
            rec.road_id = road.attribute("id").value();
            rec.s       = type_node.attribute("s").as_double(0.0);
            rec.type    = type_node.attribute("type").value();
            rec.country = country.value();
            model.road_type_extras.push_back(std::move(rec));
        }
    }
}

}  // namespace detail
}  // namespace odr
}  // namespace gt_esmini
