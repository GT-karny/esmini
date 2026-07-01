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

TEST(SimpleJsonTest, ToleratesLineComments)
{
    simplejson::Value root;
    std::string error;

    const char* text =
        "{\n"
        "    // leading comment\n"
        "    \"port\": 48199, // trailing comment after value\n"
        "    \"name\": \"ego\"\n"
        "    // final comment\n"
        "}\n";

    ASSERT_TRUE(simplejson::Parse(text, root, &error)) << error;

    int port = 0;
    std::string name;
    EXPECT_TRUE(root.GetInt("port", port));
    EXPECT_TRUE(root.GetString("name", name));
    EXPECT_EQ(port, 48199);
    EXPECT_EQ(name, "ego");
}

TEST(SimpleJsonTest, ToleratesTrailingCommaInObject)
{
    simplejson::Value root;
    std::string error;

    ASSERT_TRUE(simplejson::Parse(R"({"a":1,"b":2,})", root, &error)) << error;

    int a = 0;
    int b = 0;
    EXPECT_TRUE(root.GetInt("a", a));
    EXPECT_TRUE(root.GetInt("b", b));
    EXPECT_EQ(a, 1);
    EXPECT_EQ(b, 2);
}

TEST(SimpleJsonTest, ToleratesTrailingCommaInArray)
{
    simplejson::Value root;
    std::string error;

    ASSERT_TRUE(simplejson::Parse(R"({"vals":[1,2,3,]})", root, &error)) << error;

    const simplejson::Value* vals = root.Find("vals");
    ASSERT_NE(vals, nullptr);
    ASSERT_EQ(vals->type, simplejson::Value::Type::Array);
    ASSERT_EQ(vals->array_value.size(), 3u);
    EXPECT_DOUBLE_EQ(vals->array_value[2].number_value, 3.0);
}

TEST(SimpleJsonTest, CoercesStringBool)
{
    simplejson::Value root;
    std::string error;

    ASSERT_TRUE(simplejson::Parse(R"({"a":"true","b":"False","c":"TRUE"})", root, &error)) << error;

    bool a = false;
    bool b = true;
    bool c = false;
    EXPECT_TRUE(root.GetBool("a", a));
    EXPECT_TRUE(root.GetBool("b", b));
    EXPECT_TRUE(root.GetBool("c", c));  // case-insensitive
    EXPECT_TRUE(a);
    EXPECT_FALSE(b);
    EXPECT_TRUE(c);
}

TEST(SimpleJsonTest, CoercesStringNumber)
{
    simplejson::Value root;
    std::string error;

    ASSERT_TRUE(simplejson::Parse(R"({"port":"49001","ratio":"0.6"})", root, &error)) << error;

    int port = 0;
    double ratio = 0.0;
    EXPECT_TRUE(root.GetInt("port", port));
    EXPECT_TRUE(root.GetDouble("ratio", ratio));
    EXPECT_EQ(port, 49001);
    EXPECT_DOUBLE_EQ(ratio, 0.6);
}

TEST(SimpleJsonTest, RejectsGarbageStringCoercion)
{
    simplejson::Value root;
    std::string error;

    ASSERT_TRUE(simplejson::Parse(R"({"flag":"yes","num":"1.2.3","empty":""})", root, &error)) << error;

    bool flag = false;
    int num = 0;
    double dnum = 0.0;
    bool empty_bool = false;
    EXPECT_FALSE(root.GetBool("flag", flag));   // "yes" is not a bool
    EXPECT_FALSE(root.GetInt("num", num));       // "1.2.3" is not a number
    EXPECT_FALSE(root.GetDouble("num", dnum));
    EXPECT_FALSE(root.GetBool("empty", empty_bool));
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
