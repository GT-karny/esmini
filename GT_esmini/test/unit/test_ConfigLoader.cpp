#include <gtest/gtest.h>

#include "gt_esmini/core/ConfigLoader.hpp"

// Pins the config path-resolution contract (audit F5). The historical bug was in
// GetCurrentModuleDirectory (host-process dir instead of module dir), which fed a
// wrong exe_dir here; these tests fix the resolution *rule* so a relative/normal
// config placement and the absolute-path ConfigFile injection both behave.
namespace gt_esmini
{
namespace
{

TEST(ConfigLoaderTest, ResolvesBareFilenameToSiblingConfigDir)
{
    ConfigLoader loader;
    // exe_dir = .../build/GT_esmini/Release  →  .../build/GT_esmini/Release/../config/virtual_driver.json
    EXPECT_EQ(loader.ResolveConfigPath("/opt/gt/build/GT_esmini/Release", "virtual_driver.json"),
              "/opt/gt/build/GT_esmini/Release/../config/virtual_driver.json");
}

TEST(ConfigLoaderTest, DetectsAbsolutePaths)
{
    EXPECT_TRUE(ConfigLoader::IsAbsolutePath("/etc/gt/virtual_driver.json"));   // POSIX
    EXPECT_TRUE(ConfigLoader::IsAbsolutePath("C:\\runs\\vd.json"));             // Windows drive
    EXPECT_TRUE(ConfigLoader::IsAbsolutePath("\\\\host\\share\\vd.json"));      // UNC

    EXPECT_FALSE(ConfigLoader::IsAbsolutePath("virtual_driver.json"));
    EXPECT_FALSE(ConfigLoader::IsAbsolutePath("config/virtual_driver.json"));
    EXPECT_FALSE(ConfigLoader::IsAbsolutePath(""));
}

TEST(ConfigLoaderTest, PassthroughKeepsAbsolutePathUnchanged)
{
    ConfigLoader loader;
    // Regression: the web backend injects an absolute ConfigFile; it must be used verbatim.
    EXPECT_EQ(loader.ResolveConfigPathOrPassthrough("/opt/gt/build/GT_esmini/Release",
                                                    "C:\\runs\\job42\\virtual_driver.json"),
              "C:\\runs\\job42\\virtual_driver.json");
    EXPECT_EQ(loader.ResolveConfigPathOrPassthrough("/ignored", "/abs/vd.json"), "/abs/vd.json");
}

TEST(ConfigLoaderTest, PassthroughResolvesBareFilename)
{
    ConfigLoader loader;
    // The default/normal case: a bare filename resolves under the module's config/ dir.
    EXPECT_EQ(loader.ResolveConfigPathOrPassthrough("/opt/gt/build/GT_esmini/Release", "virtual_driver.json"),
              "/opt/gt/build/GT_esmini/Release/../config/virtual_driver.json");
}

}  // namespace
}  // namespace gt_esmini
