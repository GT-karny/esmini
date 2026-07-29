#pragma once

#include "gt_esmini/control/virtualdriver/VirtualDriverTypes.hpp"

#include <cstdio>
#include <string>

// ============================================================================
// Policy diagnostics KV channel (capability_model §2.2 (a') / W3).
//
// Traffic policies compute numeric internals — AEB's TTC and required
// deceleration, arbitration margins, confidence — that used to die as locals
// inside Evaluate(). Without them "why did it fire / why didn't it" is not
// answerable from outside the process.
//
// These KVs travel as strings because their destination is the ONE generic slot
// OSI 3.7.0 offers for such quantities:
// HostVehicleData.VehicleAutomatedDrivingFunction.custom_detail (repeated
// KeyValuePair{string,string}). Keeping the same string form all the way from
// the policy to OSI means W1's forwarding is a copy, not a translation, and the
// telemetry JSON and the OSI stream cannot drift apart.
//
// KEY NAMING CONVENTION (decided 2026-07-20; a free-form string KV space
// without one turns into a junk drawer):
//
//   gt.<function>.<quantity>_<unit>      e.g. gt.aeb.ttc_s, gt.aeb.a_req_mps2
//
//   * prefix `gt.`     — GT_esmini namespace. Verdict-usable: a matcher may
//                        read these and a gate may fail on them.
//   * prefix `gt.dbg.` — debug only. Observable but explicitly OUTSIDE
//                        verdict trust (§2.2 exposure=`debug`); raw
//                        other-vehicle predictions belong here. The single
//                        prefix lets a lint mechanically exclude them from
//                        verdict paths.
//   * separators       — '.' between levels, '_' inside a word.
//   * unit suffix      — SI symbol at the end of the key (_s, _m, _mps,
//                        _mps2, _rad). The unit lives in the KEY, never in
//                        the value, so consumers parse a plain number.
//   * value            — fixed 3-decimal decimal string ("%.3f"), or
//                        "true"/"false" for booleans. Unitless discretes take
//                        no unit suffix: identifiers/counts are plain integer
//                        strings ("%d"), enum-like states are lower-case
//                        tokens ("hold", "red") from a finite set the emitting
//                        policy documents at its call site.
// ============================================================================

namespace gt_esmini
{

// Appends one numeric diagnostic under the convention above.
inline void AddDetail(PolicyDetail& detail, const std::string& key, double value)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.3f", value);
    detail.emplace_back(key, std::string(buf));
}

inline void AddDetail(PolicyDetail& detail, const std::string& key, bool value)
{
    detail.emplace_back(key, std::string(value ? "true" : "false"));
}

// Identifiers and counts (unitless): plain integer string. Also disambiguates
// AddDetail(d, k, 42), which would otherwise be ambiguous between the double
// and bool overloads.
inline void AddDetail(PolicyDetail& detail, const std::string& key, int value)
{
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d", value);
    detail.emplace_back(key, std::string(buf));
}

// Enum-like states: a lower-case token from a finite, policy-documented set.
inline void AddDetail(PolicyDetail& detail, const std::string& key, const char* value)
{
    detail.emplace_back(key, std::string(value));
}

}  // namespace gt_esmini
