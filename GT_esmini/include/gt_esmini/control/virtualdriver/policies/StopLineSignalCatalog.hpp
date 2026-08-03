#pragma once

#include <string>
#include <unordered_map>

namespace gt_esmini
{

enum class StopLineKind
{
    NONE,
    STOP_LINE
};

// Key is country + type[.subtype], matching upstream's traffic-signal catalog
// convention. Independent implementation; never touches signals_types_ (R1).
StopLineKind ClassifyStopLineType(const std::unordered_map<std::string, StopLineKind>& catalog,
                                  const std::string&                                  country,
                                  const std::string&                                  type,
                                  const std::string&                                  subtype);

bool LoadStopLineCatalog(const std::string& country);

const std::unordered_map<std::string, StopLineKind>& GetStopLineCatalog();

}  // namespace gt_esmini
