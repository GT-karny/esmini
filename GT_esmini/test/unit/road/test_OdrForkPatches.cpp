// test_OdrForkPatches.cpp -- FORK GOVERNANCE tests for the GT_RoadManager.cpp fork (plan P1).
//
// This suite is the machine guard for the thin fork patches applied to the swapped-in
// GT_RoadManager.cpp (plus the CMake swap-zone). It has two jobs:
//
//   1. MarkerCount  -- assert the on-disk marker inventory matches the patch manifest so a
//      stray/removed fork edit trips ctest. KEEP IN SYNC WITH:
//          GT_esmini/docs/gt_roadmanager_patches.md
//      (5x "[GT_ODR:" + >=1 "[GT_LHT]" in GT_RoadManager.cpp; 1x "[GT_ODR:cmake]" in the
//       RoadManager CMakeLists). Source-of-truth via the GT_ODR_REPO_ROOT compile def.
//
//   2. Behavioral proofs of the individual patches, driven through the REAL parser
//      (roadmanager::Position::GetOpenDrive()->LoadOpenDriveFile), using tiny deterministic
//      temp xodr files:
//        - CountryRevisionLegacyPreserving : the [GT_ODR:country-rev] legacy-preserving read
//          (absent=>0, explicit honored). Case (b) countryRevision="2012" -> TrafficLight is
//          the regression that FAILED before the patch (old default 2013 => 2012<2013 never
//          reached because the attribute was only read when ABSENT).
//        - IncludeHardError          : [GT_ODR:hook] BuildSideModel makes <include> a hard error.
//        - JunctionAbortResilience   : [GT_ODR:junc-abort] a dangling connectingRoad no longer
//          aborts the whole parse (load survives, the connection is skipped).
//        - HookIntegration           : after a good load, gt_esmini::odr::GetSideModel() is
//          non-null with plausible rev fields (the fork hook actually ran + registered).
//
// Temp files are written under GT_ODR_REPO_ROOT/build (a scratch dir) when available, else
// std::filesystem::temp_directory_path(); content is deterministic and cleaned up.
#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "RoadManager.hpp"
#include "gt_esmini/road/OdrSideModel.hpp"

namespace fs = std::filesystem;

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

std::size_t CountOccurrences(const std::string& hay, const std::string& needle)
{
    if (needle.empty())
    {
        return 0;
    }
    std::size_t n = 0, pos = 0;
    while ((pos = hay.find(needle, pos)) != std::string::npos)
    {
        ++n;
        pos += needle.size();
    }
    return n;
}

// A scratch directory for the temp xodr files. Prefer <repo>/build (gitignored) so nothing
// lands in the source tree; fall back to the OS temp dir.
fs::path ScratchDir()
{
    const std::string root = RepoRoot();
    std::error_code   ec;
    if (!root.empty())
    {
        fs::path cand = fs::path(root) / "build" / "odr_fork_tests";
        fs::create_directories(cand, ec);
        if (!ec && fs::is_directory(cand))
        {
            return cand;
        }
    }
    fs::path tmp = fs::temp_directory_path(ec) / "odr_fork_tests";
    fs::create_directories(tmp, ec);
    return tmp;
}

// Write `content` to <scratch>/<name> and return the absolute path.
std::string WriteTemp(const std::string& name, const std::string& content)
{
    const fs::path p = ScratchDir() / name;
    std::ofstream  out(p, std::ios::binary);
    out << content;
    out.close();
    return p.string();
}

// Load an xodr into the singleton OpenDrive (replace=true -> fresh network). Returns load ok.
bool LoadXodr(const std::string& abs_path)
{
    return roadmanager::Position::GetOpenDrive()->LoadOpenDriveFile(abs_path.c_str(), true);
}

// Classify a signal by dynamic type: true iff it is a roadmanager::TrafficLight.
bool IsTrafficLight(roadmanager::Signal* sig)
{
    return dynamic_cast<roadmanager::TrafficLight*>(sig) != nullptr;
}

// Find a signal by name across all roads of the loaded network; nullptr if absent.
roadmanager::Signal* FindSignalByName(roadmanager::OpenDrive* odr, const std::string& name)
{
    if (odr == nullptr)
    {
        return nullptr;
    }
    for (unsigned int ri = 0; ri < odr->GetNumOfRoads(); ++ri)
    {
        roadmanager::Road* road = odr->GetRoadByIdx(ri);
        if (road == nullptr)
        {
            continue;
        }
        for (unsigned int si = 0; si < road->GetNumberOfSignals(); ++si)
        {
            roadmanager::Signal* sig = road->GetSignal(si);
            if (sig != nullptr && sig->GetName() == name)
            {
                return sig;
            }
        }
    }
    return nullptr;
}

// -- xodr templates ---------------------------------------------------------------------------

// A minimal straight road (id=1) with exactly ONE dynamic signal. `country`/`rev_attr` control
// the classification-relevant attributes; `rev_attr` is spliced in verbatim (e.g.
// ` countryRevision="2012"` or ""). Mirrors fixture 03's structure (a single 150m road with a
// dynamic type-1000001 signal), reduced to one signal so each case is independent.
std::string OneDynamicSignalRoad(const std::string& sig_name, const std::string& country, const std::string& rev_attr)
{
    return std::string("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n") +
           "<OpenDRIVE>\n"
           "  <header revMajor=\"1\" revMinor=\"8\" name=\"fork_test\" version=\"1.00\" date=\"2026-07-03T00:00:00\""
           " north=\"0.0\" south=\"0.0\" east=\"0.0\" west=\"0.0\"/>\n"
           "  <road name=\"main\" length=\"150.0\" id=\"1\" junction=\"-1\">\n"
           "    <link/>\n"
           "    <planView>\n"
           "      <geometry s=\"0.0\" x=\"0.0\" y=\"0.0\" hdg=\"0.0\" length=\"150.0\"><line/></geometry>\n"
           "    </planView>\n"
           "    <lanes>\n"
           "      <laneOffset s=\"0.0\" a=\"0.0\" b=\"0.0\" c=\"0.0\" d=\"0.0\"/>\n"
           "      <laneSection s=\"0.0\">\n"
           "        <left><lane id=\"1\" type=\"driving\" level=\"false\"><link/>"
           "<width sOffset=\"0.0\" a=\"3.5\" b=\"0.0\" c=\"0.0\" d=\"0.0\"/></lane></left>\n"
           "        <center><lane id=\"0\" type=\"none\" level=\"false\"><link/></lane></center>\n"
           "        <right><lane id=\"-1\" type=\"driving\" level=\"false\"><link/>"
           "<width sOffset=\"0.0\" a=\"3.5\" b=\"0.0\" c=\"0.0\" d=\"0.0\"/></lane></right>\n"
           "      </laneSection>\n"
           "    </lanes>\n"
           "    <objects/>\n"
           "    <signals>\n"
           "      <signal s=\"70.0\" t=\"-4.0\" id=\"1\" name=\"" +
           sig_name +
           "\" dynamic=\"yes\" orientation=\"-\" zOffset=\"5.0\""
           " country=\"" +
           country + "\"" + rev_attr +
           " type=\"1000001\" subtype=\"-1\" value=\"0.0\" height=\"0.5\" width=\"0.4\"/>\n"
           "    </signals>\n"
           "  </road>\n"
           "</OpenDRIVE>\n";
}

}  // namespace

// 1. Marker inventory matches the patch manifest (gt_roadmanager_patches.md).
TEST(OdrForkPatches, MarkerCount)
{
    const std::string root = RepoRoot();
    ASSERT_FALSE(root.empty()) << "GT_ODR_REPO_ROOT not defined";

    // GT_RoadManager.cpp: exactly 5 [GT_ODR: and at least 1 [GT_LHT].
    const std::string cpp_path = root + "/GT_esmini/src/road/GT_RoadManager.cpp";
    std::string       cpp;
    ASSERT_TRUE(ReadFileToString(cpp_path, cpp)) << "cannot read " << cpp_path;
    EXPECT_EQ(CountOccurrences(cpp, "[GT_ODR:"), 5u)
        << "GT_RoadManager.cpp [GT_ODR:] marker count drifted from gt_roadmanager_patches.md (expected 5).";
    EXPECT_GE(CountOccurrences(cpp, "[GT_LHT]"), 1u)
        << "GT_RoadManager.cpp lost its [GT_LHT] patch 1-A marker.";

    // RoadManager/CMakeLists.txt: exactly 2 [GT_ODR:cmake] markers -- the single R1 swap-zone
    // exception spans TWO edits (the odr_side/*.cpp source-list APPEND and the GT_esmini/include
    // include-directory APPEND). See gt_roadmanager_patches.md §0.
    const std::string cmake_path = root + "/EnvironmentSimulator/Modules/RoadManager/CMakeLists.txt";
    std::string       cmake;
    ASSERT_TRUE(ReadFileToString(cmake_path, cmake)) << "cannot read " << cmake_path;
    EXPECT_EQ(CountOccurrences(cmake, "[GT_ODR:cmake]"), 2u)
        << "RoadManager/CMakeLists.txt [GT_ODR:cmake] marker count drifted from gt_roadmanager_patches.md §0 (expected 2: source-list + include-dir).";
}

// 2. countryRevision legacy-preserving read: absent=>0 (honored), explicit values honored.
//    The gate is: TrafficLight iff ToLower(country)=="opendrive" && country_revision<2013 && dynamic.
TEST(OdrForkPatches, CountryRevisionLegacyPreserving)
{
    // (a) country="OpenDRIVE" WITHOUT countryRevision -> absent=>0, 0<2013 -> TrafficLight.
    {
        const std::string path = WriteTemp("cr_absent.xodr", OneDynamicSignalRoad("tl_absent", "OpenDRIVE", ""));
        ASSERT_TRUE(LoadXodr(path)) << "load failed: " << path;
        roadmanager::Signal* sig = FindSignalByName(roadmanager::Position::GetOpenDrive(), "tl_absent");
        ASSERT_NE(sig, nullptr) << "signal tl_absent not found";
        EXPECT_TRUE(IsTrafficLight(sig)) << "(a) absent countryRevision must classify as TrafficLight (absent=>0<2013)";
    }

    // (b) country="OpenDRIVE" countryRevision="2012" -> explicit honored, 2012<2013 -> TrafficLight.
    //     THIS FAILED BEFORE THE PATCH: the old code defaulted country_revision=2013 and only read
    //     the attribute when it was ABSENT, so an explicit 2012 was never applied -> stayed 2013
    //     -> 2013<2013 false -> demoted to a plain Signal.
    {
        const std::string path =
            WriteTemp("cr_2012.xodr", OneDynamicSignalRoad("tl_2012", "OpenDRIVE", " countryRevision=\"2012\""));
        ASSERT_TRUE(LoadXodr(path)) << "load failed: " << path;
        roadmanager::Signal* sig = FindSignalByName(roadmanager::Position::GetOpenDrive(), "tl_2012");
        ASSERT_NE(sig, nullptr) << "signal tl_2012 not found";
        EXPECT_TRUE(IsTrafficLight(sig)) << "(b) explicit countryRevision=2012 must classify as TrafficLight (regression pre-patch)";
    }

    // (c) country="OpenDRIVE" countryRevision="2021" -> explicit honored, 2021>=2013 -> plain Signal.
    {
        const std::string path =
            WriteTemp("cr_2021.xodr", OneDynamicSignalRoad("tl_2021", "OpenDRIVE", " countryRevision=\"2021\""));
        ASSERT_TRUE(LoadXodr(path)) << "load failed: " << path;
        roadmanager::Signal* sig = FindSignalByName(roadmanager::Position::GetOpenDrive(), "tl_2021");
        ASSERT_NE(sig, nullptr) << "signal tl_2021 not found";
        EXPECT_FALSE(IsTrafficLight(sig)) << "(c) countryRevision=2021 must stay a plain Signal (>=2013)";
    }

    // (d) country="DE" dynamic -> gate country mismatch -> plain Signal (unchanged by the patch).
    {
        const std::string path =
            WriteTemp("cr_de.xodr", OneDynamicSignalRoad("tl_de", "DE", " countryRevision=\"2021\""));
        ASSERT_TRUE(LoadXodr(path)) << "load failed: " << path;
        roadmanager::Signal* sig = FindSignalByName(roadmanager::Position::GetOpenDrive(), "tl_de");
        ASSERT_NE(sig, nullptr) << "signal tl_de not found";
        EXPECT_FALSE(IsTrafficLight(sig)) << "(d) country=DE must stay a plain Signal (gate country mismatch)";
    }

    roadmanager::Position::GetOpenDrive()->Clear();
}

// 3. <include> is a hard parse error ([GT_ODR:hook] BuildSideModel returns false -> load fails).
TEST(OdrForkPatches, IncludeHardError)
{
    const std::string xodr = std::string("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n") +
                             "<OpenDRIVE>\n"
                             "  <header revMajor=\"1\" revMinor=\"5\" name=\"inc\" version=\"1.00\"/>\n"
                             "  <road name=\"r\" length=\"100.0\" id=\"1\" junction=\"-1\">\n"
                             "    <link/>\n"
                             "    <planView>\n"
                             "      <geometry s=\"0.0\" x=\"0.0\" y=\"0.0\" hdg=\"0.0\" length=\"100.0\"><line/></geometry>\n"
                             "    </planView>\n"
                             "    <lanes>\n"
                             "      <laneSection s=\"0.0\">\n"
                             "        <center><lane id=\"0\" type=\"none\" level=\"false\"/></center>\n"
                             "        <right><lane id=\"-1\" type=\"driving\" level=\"false\">"
                             "<width sOffset=\"0.0\" a=\"3.5\" b=\"0.0\" c=\"0.0\" d=\"0.0\"/></lane></right>\n"
                             "      </laneSection>\n"
                             "    </lanes>\n"
                             "    <include file=\"does_not_exist.xml\"/>\n"
                             "  </road>\n"
                             "</OpenDRIVE>\n";
    const std::string path = WriteTemp("include_error.xodr", xodr);
    EXPECT_FALSE(LoadXodr(path)) << "an <include> element must make LoadOpenDriveFile return false (hard error by design)";
    roadmanager::Position::GetOpenDrive()->Clear();
}

// 4. A dangling junction/connection/@connectingRoad no longer aborts the whole parse
//    ([GT_ODR:junc-abort]): the load survives, and the junction exists with the bad connection skipped.
//    Mirrors fixture 02 (valid connection id=0 -> road 10, dangling connection id=1 -> road "99").
TEST(OdrForkPatches, JunctionAbortResilience)
{
    const std::string xodr = std::string("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n") +
                             "<OpenDRIVE>\n"
                             "  <header revMajor=\"1\" revMinor=\"4\" name=\"junc\" version=\"1.00\"/>\n"
                             "  <road name=\"incoming\" length=\"100.0\" id=\"1\" junction=\"-1\">\n"
                             "    <link><successor elementType=\"junction\" elementId=\"2\"/></link>\n"
                             "    <planView><geometry s=\"0.0\" x=\"0.0\" y=\"0.0\" hdg=\"0.0\" length=\"100.0\"><line/></geometry></planView>\n"
                             "    <lanes><laneSection s=\"0.0\">\n"
                             "      <center><lane id=\"0\" type=\"none\" level=\"false\"><link/></lane></center>\n"
                             "      <right><lane id=\"-1\" type=\"driving\" level=\"false\"><link/>"
                             "<width sOffset=\"0.0\" a=\"3.5\" b=\"0.0\" c=\"0.0\" d=\"0.0\"/></lane></right>\n"
                             "    </laneSection></lanes>\n"
                             "  </road>\n"
                             "  <road name=\"connecting_ok\" length=\"20.0\" id=\"10\" junction=\"2\">\n"
                             "    <link><predecessor elementType=\"road\" elementId=\"1\" contactPoint=\"end\"/></link>\n"
                             "    <planView><geometry s=\"0.0\" x=\"100.0\" y=\"0.0\" hdg=\"0.0\" length=\"20.0\"><line/></geometry></planView>\n"
                             "    <lanes><laneSection s=\"0.0\">\n"
                             "      <center><lane id=\"0\" type=\"none\" level=\"false\"><link/></lane></center>\n"
                             "      <right><lane id=\"-1\" type=\"driving\" level=\"false\"><link><predecessor id=\"-1\"/></link>"
                             "<width sOffset=\"0.0\" a=\"3.5\" b=\"0.0\" c=\"0.0\" d=\"0.0\"/></lane></right>\n"
                             "    </laneSection></lanes>\n"
                             "  </road>\n"
                             "  <junction name=\"brokenJunction\" id=\"2\">\n"
                             "    <connection id=\"0\" incomingRoad=\"1\" connectingRoad=\"10\" contactPoint=\"start\">\n"
                             "      <laneLink from=\"-1\" to=\"-1\"/>\n"
                             "    </connection>\n"
                             "    <connection id=\"1\" incomingRoad=\"1\" connectingRoad=\"99\" contactPoint=\"start\">\n"
                             "      <laneLink from=\"-1\" to=\"-1\"/>\n"
                             "    </connection>\n"
                             "  </junction>\n"
                             "</OpenDRIVE>\n";
    const std::string path = WriteTemp("junc_dangling.xodr", xodr);
    ASSERT_TRUE(LoadXodr(path)) << "a dangling connectingRoad must NOT abort the whole parse (load should survive)";

    roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();
    ASSERT_NE(odr, nullptr);
    EXPECT_GE(odr->GetNumOfJunctions(), 1u) << "junction 2 must be present after the resilient load";
    // The dangling connection (id=1) is skipped, the valid one (id=0) is kept: exactly 1 connection.
    roadmanager::Junction* j = odr->GetJunctionByIdx(0);
    ASSERT_NE(j, nullptr);
    EXPECT_EQ(j->GetNumberOfConnections(), 1u)
        << "the dangling connection must be skipped (only the valid connection to road 10 survives)";

    roadmanager::Position::GetOpenDrive()->Clear();
}

// 5. After a successful load, the fork hook ran and registered a side model with plausible revs.
TEST(OdrForkPatches, HookIntegration)
{
    const std::string path = WriteTemp("hook_ok.xodr", OneDynamicSignalRoad("tl_hook", "OpenDRIVE", ""));
    ASSERT_TRUE(LoadXodr(path)) << "load failed: " << path;

    roadmanager::OpenDrive*             odr = roadmanager::Position::GetOpenDrive();
    const gt_esmini::odr::OdrSideModel* sm  = gt_esmini::odr::GetSideModel(odr);
    ASSERT_NE(sm, nullptr) << "the [GT_ODR:hook] BuildSideModel call must register a side model keyed by the OpenDrive*";
    EXPECT_EQ(sm->rev_major, 1) << "side model rev_major should be 1 for this header";
    EXPECT_EQ(sm->rev_minor, 8) << "side model rev_minor should be 8 for this header";

    roadmanager::Position::GetOpenDrive()->Clear();
}
