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

// Reads one numeric diagnostic back out. Returns false -- leaving *out
// untouched -- when the key is absent or its value does not parse; a caller
// must never read a missing diagnostic as 0.0 (the same absent-is-not-zero
// discipline the Python-side _adas_detail_float applies to the OSI end of this
// same channel).
//
// ============================================================================
// WHEN IT IS LEGITIMATE TO READ THIS CHANNEL BACK -- READ BEFORE CALLING
// ============================================================================
// PolicyDetail is an OBSERVATION channel: fixed-precision strings destined for
// OSI custom_detail. Feeding it back into a CONTROL path would couple control
// to a telemetry string format and throw away precision (AdasCoexistenceStack.
// hpp makes exactly this argument for why FCW uses a second AebSafety instance
// instead of re-reading gt.aeb.ttc_s). This getter exists for the OTHER case:
// re-exporting one policy's own measurement as a DIFFERENT observable, where
// the value never reaches a pedal.
//
// The motivating caller is gt.acc.thw_actual_s, which is
// gt.lead_vehicle.gap_m / v_ego. Deriving it any other way would require a
// second lead-vehicle search, i.e. a SECOND definition of "the lead" that can
// disagree with the one actually maintaining the gap -- so reading the
// controlling policy's own number is not a shortcut here, it is the only way
// the reported headway is guaranteed to describe the vehicle the system is
// really following.
inline bool TryGetDetail(const PolicyDetail& detail, const std::string& key, double* out)
{
    for (const auto& kv : detail)
    {
        if (kv.first != key) continue;
        try
        {
            const double v = std::stod(kv.second);
            if (out != nullptr) *out = v;
            return true;
        }
        catch (...)
        {
            return false;
        }
    }
    return false;
}

}  // namespace gt_esmini
