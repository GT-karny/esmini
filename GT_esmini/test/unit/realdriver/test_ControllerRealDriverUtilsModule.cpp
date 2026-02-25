#include <gtest/gtest.h>
#include "gt_esmini/control/ControllerRealDriverUtils.hpp"

namespace gt_esmini::realdetail
{
TEST(ControllerRealDriverUtilsModuleTest, Distance2DComputesEuclideanDistance)
{
    EXPECT_DOUBLE_EQ(Distance2D(0.0, 0.0, 3.0, 4.0), 5.0);
}

TEST(ControllerRealDriverUtilsModuleTest, UnknownAdasReturnsMinusOne)
{
    EXPECT_EQ(MapAdasFunctionNameToIndex("not_a_real_function"), -1);
}
} // namespace gt_esmini::realdetail
