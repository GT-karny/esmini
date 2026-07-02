// test_OdrLaneTypeSync.cpp -- SYNC GUARD for the Python mirrors of Lane::LaneType (plan P2).
//
// Two Python files hand-mirror the RoadManager.hpp Lane::LaneType bitmask values:
//
//     GT_esmini/scripts/rm_lib.py                              (RM_LANE_TYPE_* full mirror)
//     GT_esmini/web/backend/services/road_geometry_service.py  (_LANE_TYPE_* viewer subset)
//
// This suite machine-checks both against the REAL C++ enum, so a value drift on either side
// (a C++ enum edit or a Python typo) trips ctest instead of silently mis-classifying lanes.
//
//   - RmLibFullMirror   : rm_lib.py must define the COMPLETE expected name set and every value
//                         must equal the C++ enum (incl. the ANY_DRIVING/ANY_ROAD compositions
//                         and ANY = -1). Unknown RM_LANE_TYPE_* names also fail (no C++ peer).
//   - WebServiceSubset  : road_geometry_service.py may mirror a SUBSET, but it must be
//                         non-empty, include CURB/BIDIRECTIONAL/CONNECTING_RAMP (the P2 lane
//                         type additions), and every mirrored value must equal the C++ enum.
//
// Parsing is a line-oriented regex scan (no Python interpreter needed): accepted expression
// forms are `1 << N`, `-1`, and `A | B | ...` compositions of previously parsed names.
#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "RoadManager.hpp"

namespace
{

std::string RepoRoot()
{
#ifdef GT_ODR_REPO_ROOT
    return std::string(GT_ODR_REPO_ROOT);
#else
    return std::string();
#endif
}

bool ReadFileToString(const std::string& path, std::string& out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

// The expected C++ truth table: every LANE_TYPE_* name the Python full mirror must carry,
// paired with its value from the REAL enum (RoadManager.hpp). REFERENCE_LINE (an alias of
// NONE) and TUNNEL (a pseudo type, not a lane type bit) are deliberately NOT mirrored.
const std::vector<std::pair<std::string, int64_t>>& CppLaneTypeTable()
{
    using roadmanager::Lane;
    static const std::vector<std::pair<std::string, int64_t>> table = {
        {"NONE", static_cast<int64_t>(Lane::LANE_TYPE_NONE)},
        {"DRIVING", static_cast<int64_t>(Lane::LANE_TYPE_DRIVING)},
        {"STOP", static_cast<int64_t>(Lane::LANE_TYPE_STOP)},
        {"SHOULDER", static_cast<int64_t>(Lane::LANE_TYPE_SHOULDER)},
        {"BIKING", static_cast<int64_t>(Lane::LANE_TYPE_BIKING)},
        {"SIDEWALK", static_cast<int64_t>(Lane::LANE_TYPE_SIDEWALK)},
        {"BORDER", static_cast<int64_t>(Lane::LANE_TYPE_BORDER)},
        {"RESTRICTED", static_cast<int64_t>(Lane::LANE_TYPE_RESTRICTED)},
        {"PARKING", static_cast<int64_t>(Lane::LANE_TYPE_PARKING)},
        {"BIDIRECTIONAL", static_cast<int64_t>(Lane::LANE_TYPE_BIDIRECTIONAL)},
        {"MEDIAN", static_cast<int64_t>(Lane::LANE_TYPE_MEDIAN)},
        {"SPECIAL1", static_cast<int64_t>(Lane::LANE_TYPE_SPECIAL1)},
        {"SPECIAL2", static_cast<int64_t>(Lane::LANE_TYPE_SPECIAL2)},
        {"SPECIAL3", static_cast<int64_t>(Lane::LANE_TYPE_SPECIAL3)},
        {"ROADWORKS", static_cast<int64_t>(Lane::LANE_TYPE_ROADWORKS)},
        {"TRAM", static_cast<int64_t>(Lane::LANE_TYPE_TRAM)},
        {"RAIL", static_cast<int64_t>(Lane::LANE_TYPE_RAIL)},
        {"ENTRY", static_cast<int64_t>(Lane::LANE_TYPE_ENTRY)},
        {"EXIT", static_cast<int64_t>(Lane::LANE_TYPE_EXIT)},
        {"OFF_RAMP", static_cast<int64_t>(Lane::LANE_TYPE_OFF_RAMP)},
        {"ON_RAMP", static_cast<int64_t>(Lane::LANE_TYPE_ON_RAMP)},
        {"CURB", static_cast<int64_t>(Lane::LANE_TYPE_CURB)},
        {"CONNECTING_RAMP", static_cast<int64_t>(Lane::LANE_TYPE_CONNECTING_RAMP)},
        {"ANY_DRIVING", static_cast<int64_t>(Lane::LANE_TYPE_ANY_DRIVING)},
        {"ANY_ROAD", static_cast<int64_t>(Lane::LANE_TYPE_ANY_ROAD)},
        {"ANY", static_cast<int64_t>(Lane::LANE_TYPE_ANY)},
    };
    return table;
}

// Evaluate a Python-side integer expression against already-parsed constants.
// Accepted forms: "1 << N", "-1", or "NAME | NAME | ..." (names must be parsed already).
// Returns true + value on success; false when the expression is not one of these forms.
bool EvalIntExpr(const std::string& expr, const std::map<std::string, int64_t>& known, int64_t& out)
{
    static const std::regex shift_re(R"(^\s*1\s*<<\s*(\d+)\s*$)");
    static const std::regex minus_one_re(R"(^\s*-1\s*$)");
    std::smatch             m;
    if (std::regex_match(expr, m, shift_re))
    {
        out = static_cast<int64_t>(1) << std::stoi(m[1].str());
        return true;
    }
    if (std::regex_match(expr, minus_one_re))
    {
        out = -1;
        return true;
    }
    // "A | B | ..." composition of previously parsed names.
    static const std::regex name_re(R"(^\s*([A-Za-z_][A-Za-z0-9_]*)\s*$)");
    int64_t                 acc = 0;
    std::size_t             pos = 0;
    bool                    any = false;
    while (pos <= expr.size())
    {
        const std::size_t bar   = expr.find('|', pos);
        const std::string token = expr.substr(pos, (bar == std::string::npos ? expr.size() : bar) - pos);
        if (!std::regex_match(token, m, name_re))
        {
            return false;
        }
        const auto it = known.find(m[1].str());
        if (it == known.end())
        {
            return false;  // composition may only reference constants defined ABOVE it
        }
        acc |= it->second;
        any = true;
        if (bar == std::string::npos)
        {
            break;
        }
        pos = bar + 1;
    }
    out = acc;
    return any;
}

// Scan `content` line by line for `<prefix><NAME> = <expr>` assignments and evaluate them.
// Returns a map NAME (without prefix) -> value. Unevaluable matches land in `bad_lines`.
std::map<std::string, int64_t> ParsePyConstants(const std::string& content, const std::string& prefix, std::vector<std::string>& bad_lines)
{
    std::map<std::string, int64_t> full;    // keyed by the FULL identifier (for compositions)
    std::map<std::string, int64_t> result;  // keyed by the stripped NAME (for the assertions)
    const std::regex               assign_re("^\\s*(" + prefix + "([A-Z0-9_]+))\\s*=\\s*(.+?)\\s*$");

    std::istringstream in(content);
    std::string        line;
    while (std::getline(in, line))
    {
        std::smatch m;
        if (!std::regex_match(line, m, assign_re))
        {
            continue;
        }
        int64_t value = 0;
        if (!EvalIntExpr(m[3].str(), full, value))
        {
            bad_lines.push_back(line);
            continue;
        }
        full[m[1].str()]   = value;
        result[m[2].str()] = value;
    }
    return result;
}

}  // namespace

// 1. rm_lib.py: full mirror -- complete name set, every value equal to the C++ enum.
TEST(OdrLaneTypeSync, RmLibFullMirror)
{
    const std::string root = RepoRoot();
    ASSERT_FALSE(root.empty()) << "GT_ODR_REPO_ROOT not defined";

    const std::string py_path = root + "/GT_esmini/scripts/rm_lib.py";
    std::string       py;
    ASSERT_TRUE(ReadFileToString(py_path, py)) << "cannot read " << py_path;

    std::vector<std::string> bad_lines;
    const auto               parsed = ParsePyConstants(py, "RM_LANE_TYPE_", bad_lines);
    for (const std::string& l : bad_lines)
    {
        ADD_FAILURE() << "rm_lib.py: unparseable RM_LANE_TYPE_* expression (allowed: `1 << N`, `-1`, `A | B | ...`): " << l;
    }
    ASSERT_FALSE(parsed.empty()) << "rm_lib.py defines no RM_LANE_TYPE_* constants (P2 block missing?)";

    // (a) the FULL expected name set must be present, each with the exact C++ value.
    for (const auto& expected : CppLaneTypeTable())
    {
        const auto it = parsed.find(expected.first);
        if (it == parsed.end())
        {
            ADD_FAILURE() << "rm_lib.py is missing RM_LANE_TYPE_" << expected.first << " (C++ value " << expected.second << ")";
            continue;
        }
        EXPECT_EQ(it->second, expected.second)
            << "rm_lib.py RM_LANE_TYPE_" << expected.first << " drifted from RoadManager.hpp Lane::LANE_TYPE_" << expected.first;
    }

    // (b) no stray names: every RM_LANE_TYPE_* must have a C++ counterpart in the table.
    for (const auto& kv : parsed)
    {
        bool known = false;
        for (const auto& expected : CppLaneTypeTable())
        {
            if (expected.first == kv.first)
            {
                known = true;
                break;
            }
        }
        EXPECT_TRUE(known) << "rm_lib.py defines RM_LANE_TYPE_" << kv.first << " which has no Lane::LANE_TYPE_" << kv.first
                           << " counterpart in RoadManager.hpp";
    }
}

// 2. road_geometry_service.py: subset mirror -- non-empty, P2 types present, values equal.
TEST(OdrLaneTypeSync, WebServiceSubset)
{
    const std::string root = RepoRoot();
    ASSERT_FALSE(root.empty()) << "GT_ODR_REPO_ROOT not defined";

    const std::string py_path = root + "/GT_esmini/web/backend/services/road_geometry_service.py";
    std::string       py;
    ASSERT_TRUE(ReadFileToString(py_path, py)) << "cannot read " << py_path;

    // The viewer constants are all plain `1 << N` bits; compositions (_DRIVABLE_MASK) are
    // intentionally NOT matched by the `_LANE_TYPE_` prefix and stay out of scope here.
    std::vector<std::string> bad_lines;
    const auto               parsed = ParsePyConstants(py, "_LANE_TYPE_", bad_lines);
    for (const std::string& l : bad_lines)
    {
        ADD_FAILURE() << "road_geometry_service.py: unparseable _LANE_TYPE_* expression: " << l;
    }
    ASSERT_FALSE(parsed.empty()) << "road_geometry_service.py defines no _LANE_TYPE_* constants";

    // (a) every mirrored name must exist in C++ with the same value (subset allowed).
    for (const auto& kv : parsed)
    {
        bool found = false;
        for (const auto& expected : CppLaneTypeTable())
        {
            if (expected.first == kv.first)
            {
                found = true;
                EXPECT_EQ(kv.second, expected.second) << "road_geometry_service.py _LANE_TYPE_" << kv.first
                                                      << " drifted from RoadManager.hpp Lane::LANE_TYPE_" << kv.first;
                break;
            }
        }
        EXPECT_TRUE(found) << "road_geometry_service.py defines _LANE_TYPE_" << kv.first << " which has no Lane::LANE_TYPE_" << kv.first
                           << " counterpart in RoadManager.hpp";
    }

    // (b) the P2 lane-type additions must be mirrored (viewer classifies them as drivable/curb).
    for (const char* required : {"CURB", "BIDIRECTIONAL", "CONNECTING_RAMP"})
    {
        EXPECT_NE(parsed.find(required), parsed.end())
            << "road_geometry_service.py must mirror _LANE_TYPE_" << required << " (plan P2 lane types)";
    }
}
