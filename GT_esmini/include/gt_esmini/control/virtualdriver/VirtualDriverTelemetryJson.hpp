#pragma once

#include "gt_esmini/control/virtualdriver/VirtualDriverTypes.hpp"

#include <string>

namespace gt_esmini
{

// Serialize a VirtualDriverTelemetry snapshot to the JSON shape consumed by
// GT_GetVirtualDriverTelemetry() (C-API) and the live UDP telemetry stream.
//
// Single source of truth for the wire shape: both the pull C-API and the live
// push path call this, so the web overlay (replay + live) sees identical JSON.
std::string ToJson(const VirtualDriverTelemetry& t);

}  // namespace gt_esmini
