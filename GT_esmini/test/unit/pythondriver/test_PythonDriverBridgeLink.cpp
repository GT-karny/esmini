// Level 1: Link Verification Tests
// Purpose: Verify that PythonDriverBridge links correctly with Python symbols

#ifdef GT_ENABLE_EMBEDDED_PYTHON

#include <gtest/gtest.h>
#include "gt_esmini/control/pythondriver/PythonDriverBridge.hpp"

namespace gt_esmini
{
namespace test
{

// L1-001: Verify the class can be instantiated without link errors
TEST(PythonDriverBridgeLinkTest, CanInstantiate)
{
    PythonDriverBridge bridge;
    EXPECT_FALSE(bridge.IsInitialized());
    EXPECT_FALSE(bridge.HasFatalError());
}

// L1-002: Verify GetLastError returns empty string initially
TEST(PythonDriverBridgeLinkTest, EmptyLastErrorInitially)
{
    PythonDriverBridge bridge;
    EXPECT_TRUE(bridge.GetLastError().empty());
}

// L1-003: Verify destructor runs without crash (even without initialization)
TEST(PythonDriverBridgeLinkTest, DestructorWithoutInit)
{
    {
        PythonDriverBridge bridge;
        // Let it go out of scope
    }
    // If we reach here, destructor didn't crash
    SUCCEED();
}

} // namespace test
} // namespace gt_esmini

#endif // GT_ENABLE_EMBEDDED_PYTHON
