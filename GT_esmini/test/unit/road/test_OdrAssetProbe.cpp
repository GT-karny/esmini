// test_OdrAssetProbe.cpp -- TrafficLight-classification + load-status probe over the whole xodr
// asset universe (plan P1 / seed for the P3 audit universe).
//
// Two modes:
//   * GT_ODR_PROBE_UPDATE=1 -> capture the baseline JSON to
//       GT_esmini/test/odr_fixtures/golden/trafficlight_classification.json  and PASS.
//   * otherwise             -> read that baseline and EXPECT_EQ per-file; a missing baseline FAILS
//       with an instruction to run update mode.
//
// The orchestrator (NOT this test) captures the committed baseline pre-patch, so behavior parity is
// provable. Both modes must be logically correct here.
//
// Notes:
//   * Universe = sorted *.xodr from a fixed set of repo dirs (skipping any that don't exist).
//   * Loading may LOG_ERROR on files with unknown lane types etc -- that's fine; we record load ok/fail.
//   * A load that CRASHES is a finding (no wrapping): no known repo file crashes today.
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "RoadManager.hpp"

namespace fs = std::filesystem;

namespace
{

// Per-file probe record. Ordered maps => deterministic serialization.
struct FileProbe
{
    bool                               load = false;
    std::map<std::string, std::string> signals;  // "<id>:<name>" -> "TrafficLight" | "Signal"
};

using ProbeResult = std::map<std::string, FileProbe>;  // relpath (forward slashes) -> record

std::string RepoRoot()
{
#ifdef GT_ODR_REPO_ROOT
    return std::string(GT_ODR_REPO_ROOT);
#else
    return std::string();
#endif
}

// Directories that make up the probe universe, relative to the repo root. Explicitly NOT official/.
const char* kUniverseDirs[] = {
    "resources/xodr",
    "resources/scenario_authoring/road_catalog/generated",
    "DriverScript/resources/xodr",
    "EnvironmentSimulator/Unittest/xodr",
    "GT_esmini/test/odr_fixtures/handauthored",
    "GT_esmini/test/odr_fixtures/generated",
};

std::string ToForwardSlashes(std::string s)
{
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}

// Collect the sorted list of xodr files as (relpath, abspath) pairs.
std::vector<std::pair<std::string, std::string>> CollectUniverse(const std::string& root)
{
    std::vector<std::pair<std::string, std::string>> files;
    const fs::path                                   root_path(root);
    for (const char* rel : kUniverseDirs)
    {
        fs::path dir = root_path / rel;
        std::error_code ec;
        if (!fs::is_directory(dir, ec))
        {
            continue;  // skip missing dirs
        }
        for (const auto& entry : fs::directory_iterator(dir, ec))
        {
            if (ec)
            {
                break;
            }
            if (!entry.is_regular_file())
            {
                continue;
            }
            const fs::path& p = entry.path();
            if (p.extension() != ".xodr")
            {
                continue;
            }
            std::string relpath = ToForwardSlashes(fs::relative(p, root_path, ec).string());
            files.emplace_back(relpath, p.string());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

// Classify all signals of all roads in the currently-loaded OpenDrive.
void ProbeLoadedRoads(roadmanager::OpenDrive* odr, FileProbe& rec)
{
    if (odr == nullptr)
    {
        return;
    }
    const unsigned int num_roads = odr->GetNumOfRoads();
    for (unsigned int ri = 0; ri < num_roads; ++ri)
    {
        roadmanager::Road* road = odr->GetRoadByIdx(ri);
        if (road == nullptr)
        {
            continue;
        }
        const unsigned int num_sig = road->GetNumberOfSignals();
        for (unsigned int si = 0; si < num_sig; ++si)
        {
            roadmanager::Signal* sig = road->GetSignal(si);
            if (sig == nullptr)
            {
                continue;
            }
            const std::string kind = (dynamic_cast<roadmanager::TrafficLight*>(sig) != nullptr) ? "TrafficLight" : "Signal";
            const std::string key  = std::to_string(sig->GetId()) + ":" + sig->GetName();
            rec.signals[key]       = kind;
        }
    }
}

ProbeResult RunProbe(const std::vector<std::pair<std::string, std::string>>& files)
{
    ProbeResult result;
    for (const auto& f : files)
    {
        FileProbe rec;
        // replace=true: each file gets a fresh road network.
        const bool ok = roadmanager::Position::GetOpenDrive()->LoadOpenDriveFile(f.second.c_str(), true);
        rec.load      = ok;
        if (ok)
        {
            ProbeLoadedRoads(roadmanager::Position::GetOpenDrive(), rec);
        }
        result[f.first] = std::move(rec);
    }
    // Leave a clean slate afterwards.
    roadmanager::Position::GetOpenDrive()->Clear();
    return result;
}

// ---- tiny deterministic JSON (sorted keys via std::map ordering) ----

void JsonEscape(std::ostream& os, const std::string& s)
{
    os << '"';
    for (char c : s)
    {
        switch (c)
        {
            case '"':
                os << "\\\"";
                break;
            case '\\':
                os << "\\\\";
                break;
            case '\n':
                os << "\\n";
                break;
            case '\r':
                os << "\\r";
                break;
            case '\t':
                os << "\\t";
                break;
            default:
                os << c;
                break;
        }
    }
    os << '"';
}

std::string Serialize(const ProbeResult& result)
{
    std::ostringstream os;
    os << "{\n";
    bool first_file = true;
    for (const auto& kv : result)
    {
        if (!first_file)
        {
            os << ",\n";
        }
        first_file = false;
        os << "  ";
        JsonEscape(os, kv.first);
        os << ": {\n    \"load\": " << (kv.second.load ? "true" : "false") << ",\n    \"signals\": {";
        bool first_sig = true;
        for (const auto& sig : kv.second.signals)
        {
            if (!first_sig)
            {
                os << ",";
            }
            first_sig = false;
            os << "\n      ";
            JsonEscape(os, sig.first);
            os << ": ";
            JsonEscape(os, sig.second);
        }
        if (!kv.second.signals.empty())
        {
            os << "\n    ";
        }
        os << "}\n  }";
    }
    os << "\n}\n";
    return os.str();
}

std::string BaselinePath(const std::string& root)
{
    return root + "/GT_esmini/test/odr_fixtures/golden/trafficlight_classification.json";
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

}  // namespace

TEST(OdrAssetProbe, TrafficLightClassification)
{
    const std::string root = RepoRoot();
    ASSERT_FALSE(root.empty()) << "GT_ODR_REPO_ROOT not defined";

    const auto files = CollectUniverse(root);
    ASSERT_FALSE(files.empty()) << "no xodr files found under the probe universe";

    const ProbeResult result     = RunProbe(files);
    const std::string serialized = Serialize(result);

    const char* update = std::getenv("GT_ODR_PROBE_UPDATE");
    const bool  do_update = update != nullptr && std::string(update) == "1";

    const std::string baseline_path = BaselinePath(root);

    if (do_update)
    {
        std::error_code ec;
        fs::create_directories(fs::path(baseline_path).parent_path(), ec);
        std::ofstream out(baseline_path, std::ios::binary);
        ASSERT_TRUE(out.good()) << "cannot write baseline " << baseline_path;
        out << serialized;
        out.close();

        // Report useful stats.
        size_t total_signals = 0, total_tl = 0, load_fail = 0;
        for (const auto& kv : result)
        {
            if (!kv.second.load)
            {
                ++load_fail;
            }
            for (const auto& sig : kv.second.signals)
            {
                ++total_signals;
                if (sig.second == "TrafficLight")
                {
                    ++total_tl;
                }
            }
        }
        std::cout << "[GT_ODR probe] UPDATE wrote baseline: " << files.size() << " files, " << load_fail
                  << " load failures, " << total_signals << " signals, " << total_tl << " TrafficLights\n";
        SUCCEED();
        return;
    }

    // Compare mode.
    std::string baseline;
    if (!ReadFileToString(baseline_path, baseline))
    {
        FAIL() << "baseline missing: " << baseline_path
               << "\n  Run once with env GT_ODR_PROBE_UPDATE=1 to capture it (orchestrator does this pre-patch).";
        return;
    }

    if (serialized == baseline)
    {
        SUCCEED();
        return;
    }

    // Produce a per-file unified diff summary to make mismatches actionable.
    ProbeResult baseline_files;  // we only re-serialize per-file, so parse minimally by line compare
    std::istringstream cur(serialized), base(baseline);
    std::string        cur_line, base_line;
    std::ostringstream diff;
    int                lineno = 0;
    while (std::getline(cur, cur_line) || std::getline(base, base_line))
    {
        ++lineno;
        // getline above is short-circuit; re-read cleanly below is complex, so fall back to a
        // simple whole-string mismatch report with first differing offset.
        break;
    }
    (void)lineno;
    // Simple first-difference report.
    size_t i = 0;
    while (i < serialized.size() && i < baseline.size() && serialized[i] == baseline[i])
    {
        ++i;
    }
    diff << "probe output differs from baseline at byte " << i << ".\n";
    auto ctx = [](const std::string& s, size_t at) {
        size_t start = at > 60 ? at - 60 : 0;
        return s.substr(start, 160);
    };
    diff << "  current : ..." << ctx(serialized, i) << "...\n";
    diff << "  baseline: ..." << ctx(baseline, i) << "...\n";

    EXPECT_EQ(serialized, baseline) << diff.str();
}
