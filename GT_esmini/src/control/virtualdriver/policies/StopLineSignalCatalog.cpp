#include "gt_esmini/control/virtualdriver/policies/StopLineSignalCatalog.hpp"

#include "CommonMini.hpp"
#include "logger.hpp"

#include <fstream>
#include <sstream>

namespace gt_esmini
{

namespace
{
std::unordered_map<std::string, StopLineKind> g_stop_line_catalog;
// country -> load result; memoized so real-time policy ticks never re-hit the filesystem.
std::unordered_map<std::string, bool> g_load_attempted;

// unrecognized values fall back to NONE
StopLineKind ParseStopLineValue(const std::string& value)
{
    return (value == "stop_line") ? StopLineKind::STOP_LINE : StopLineKind::NONE;
}
}  // namespace

StopLineKind ClassifyStopLineType(const std::unordered_map<std::string, StopLineKind>& catalog,
                                  const std::string&                                  country,
                                  const std::string&                                  type,
                                  const std::string&                                  subtype)
{
    // "-1"/"none" mean no type, same as an empty string
    if (type.empty() || type == "-1" || type == "none")
    {
        return StopLineKind::NONE;
    }

    std::string key = country + type;
    if (!(subtype.empty() || subtype == "none" || subtype == "-1"))
    {
        key += "." + subtype;
    }

    const auto it = catalog.find(key);
    return (it != catalog.end()) ? it->second : StopLineKind::NONE;
}

bool LoadStopLineCatalog(const std::string& country)
{
    const auto attempted = g_load_attempted.find(country);
    if (attempted != g_load_attempted.end())
    {
        return attempted->second;
    }

    const std::string sign_filename = "traffic_signals/stop_line/" + country + "_stop_line.txt";
    bool               found        = false;

    const std::string file_path =
        LocateFile(sign_filename, {DirNameOf(SE_Env::Inst().GetEXEFilePath()) + "/../resources", "./resources"}, "Stop line mapping file", found);

    if (!found)
    {
        LOG_INFO("Stop line mapping file {} not located", sign_filename);  // most countries have none; not a warning
        g_load_attempted[country] = false;
        return false;
    }

    std::ifstream fs;
    fs.open(file_path.c_str());
    if (fs.fail())
    {
        LOG_ERROR("Failed to load stop line mapping file {}", file_path);
        g_load_attempted[country] = false;
        return false;
    }

    const char  delimiter = '=';
    std::string line;
    while (std::getline(fs, line))
    {
        if (line.find(delimiter) == std::string::npos) continue;  // blank/malformed line

        std::stringstream sstream(country + line);
        std::string       key   = "";
        std::string       value = "";
        std::getline(sstream, key, delimiter);
        std::getline(sstream, value, delimiter);
        g_stop_line_catalog.emplace(key, ParseStopLineValue(value));
    }

    g_load_attempted[country] = true;
    return true;
}

const std::unordered_map<std::string, StopLineKind>& GetStopLineCatalog()
{
    return g_stop_line_catalog;
}

}  // namespace gt_esmini
