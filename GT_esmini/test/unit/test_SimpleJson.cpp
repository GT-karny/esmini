#include <gtest/gtest.h>

#include "gt_esmini/common/SimpleJson.hpp"
#include "gt_esmini/control/virtualdriver/VirtualDriverConfig.hpp"

#include <filesystem>
#include <fstream>

namespace gt_esmini
{
namespace
{
std::filesystem::path WriteTempJson(const std::string& name, const std::string& content)
{
    const auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream file(path);
    file << content;
    return path;
}
}  // namespace

TEST(SimpleJsonTest, ParsesFlatValues)
{
    simplejson::Value root;
    std::string error;

    ASSERT_TRUE(simplejson::Parse(R"({"name":"ego","port":48199,"enabled":true,"ratio":1.5})", root, &error)) << error;

    std::string name;
    int port = 0;
    bool enabled = false;
    double ratio = 0.0;

    EXPECT_TRUE(root.GetString("name", name));
    EXPECT_TRUE(root.GetInt("port", port));
    EXPECT_TRUE(root.GetBool("enabled", enabled));
    EXPECT_TRUE(root.GetDouble("ratio", ratio));
    EXPECT_EQ(name, "ego");
    EXPECT_EQ(port, 48199);
    EXPECT_TRUE(enabled);
    EXPECT_DOUBLE_EQ(ratio, 1.5);
}

TEST(SimpleJsonTest, RejectsDuplicateObjectKeys)
{
    simplejson::Value root;
    std::string error;

    EXPECT_FALSE(simplejson::Parse(R"({"target_ip":"127.0.0.1","target_ip":"192.168.0.10"})", root, &error));
    EXPECT_NE(error.find("duplicate object key"), std::string::npos);
}

TEST(VirtualDriverConfigTest, LoadsRootKeysWithoutNestedCrossMatch)
{
    const auto path = WriteTempJson("gt_esmini_virtual_driver_config_test.json",
        R"({
            "vehicle_params_file": "vehicle.json",
            "horizon_s": 4.5,
            "respect_speed_limit": false,
            "input_port": 9111,
            "unused": { "horizon_s": 99.0, "input_port": 1 }
        })");

    VirtualDriverConfig config;
    ASSERT_TRUE(config.LoadFromFile(path.string()));

    EXPECT_EQ(config.vehicle_params_file, "vehicle.json");
    EXPECT_DOUBLE_EQ(config.horizon_s, 4.5);
    EXPECT_FALSE(config.respect_speed_limit);
    EXPECT_EQ(config.input_port, 9111);

    std::filesystem::remove(path);
}

}  // namespace gt_esmini
