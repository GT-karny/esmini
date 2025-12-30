#include <gtest/gtest.h>
#include "pugixml.hpp"
#include "logger.hpp"
#include "GT_ScenarioReader.hpp"

TEST(SimpleTest, InstantiateReader) {
    gt_esmini::GT_ScenarioReader reader(nullptr, nullptr, nullptr);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
