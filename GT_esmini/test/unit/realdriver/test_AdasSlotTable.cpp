#include <gtest/gtest.h>
#include "gt_esmini/control/ControllerRealDriverUtils.hpp"

// ============================================================================
// The fixed 24-slot ADAS frame (RealDriver / PythonDriver / ManualDrive) used to
// carry only labels, and the emitted OSI rows were hard-coded to NAME_OTHER. The
// slot order happens to satisfy `osi_name == index + 2` for the first 13 slots,
// which made the whole array look like a simple offset of the OSI enum -- it is
// not: NIGHT_VISION / HEAD_UP_DISPLAY are swapped relative to that assumption and
// slots 15..19 shift again (capability_model §2.2a, residual debt).
//
// These tests pin the label->enum pairing so the drift cannot come back silently.
// The enum values themselves are pinned to the real .proto by static_assert in
// GT_esminiLib.cpp (control must not depend on osi, GT_esmini/CLAUDE.md §2).
// ============================================================================

namespace gt_esmini::realdetail
{

TEST(AdasSlotTableTest, HasExactlyTheFixedFrameSize)
{
    EXPECT_EQ(kAdasSlots.size(), kAdasFunctionCount);
}

TEST(AdasSlotTableTest, EverySlotCarriesTheMatchingOsiNameValue)
{
    // Expected values read off osi_hostvehicledata.proto (OSI 3.7.0)
    // enum HostVehicleData.VehicleAutomatedDrivingFunction.Name.
    constexpr int kExpected[kAdasFunctionCount] = {
        2,   // BLIND_SPOT_WARNING
        3,   // FORWARD_COLLISION_WARNING
        4,   // LANE_DEPARTURE_WARNING
        5,   // PARKING_COLLISION_WARNING
        6,   // REAR_CROSS_TRAFFIC_WARNING
        7,   // AUTOMATIC_EMERGENCY_BRAKING
        8,   // AUTOMATIC_EMERGENCY_STEERING
        9,   // REVERSE_AUTOMATIC_EMERGENCY_BRAKING
        10,  // ADAPTIVE_CRUISE_CONTROL
        11,  // LANE_KEEPING_ASSIST
        12,  // ACTIVE_DRIVING_ASSISTANCE
        13,  // BACKUP_CAMERA
        14,  // SURROUND_VIEW_CAMERA
        21,  // NIGHT_VISION      <- not 15; the offset assumption breaks here
        20,  // HEAD_UP_DISPLAY   <- not 16; swapped with NIGHT_VISION
        15,  // ACTIVE_PARKING_ASSISTANCE
        16,  // REMOTE_PARKING_ASSISTANCE
        17,  // TRAILER_ASSISTANCE
        18,  // AUTOMATIC_HIGH_BEAMS
        19,  // DRIVER_MONITORING
        22,  // URBAN_DRIVING
        23,  // HIGHWAY_AUTOPILOT
        24,  // CRUISE_CONTROL
        25,  // SPEED_LIMIT_CONTROL
    };

    for (std::size_t i = 0; i < kAdasFunctionCount; ++i)
    {
        EXPECT_EQ(kAdasSlots[i].osi_name, kExpected[i])
            << "slot " << i << " (" << kAdasSlots[i].label << ")";
    }
}

TEST(AdasSlotTableTest, NightVisionAndHeadUpDisplayAreNotOffsetByTwo)
{
    // Regression guard for the specific pair that was wrong. Written as an
    // explicit negative so a future "simplify to index + 2" refactor fails here.
    const auto night = kAdasSlots[MapAdasFunctionNameToIndex("NIGHT_VISION")];
    const auto hud   = kAdasSlots[MapAdasFunctionNameToIndex("HEAD_UP_DISPLAY")];

    EXPECT_EQ(night.osi_name, 21);
    EXPECT_EQ(hud.osi_name, 20);
    EXPECT_GT(night.osi_name, hud.osi_name);  // enum order is the reverse of slot order
}

TEST(AdasSlotTableTest, SlotOrderIsTheInboundWireContract)
{
    // The index order is a round-trip contract: inbound HVD maps custom_name to a
    // slot index, outbound writes the slot label back. Reordering slots would
    // silently remap every external client's ADAS states, so it is pinned here in
    // addition to the pre-existing ADAPTIVE_CRUISE_CONTROL == 8 assertion.
    EXPECT_EQ(MapAdasFunctionNameToIndex("BLIND_SPOT_WARNING"), 0);
    EXPECT_EQ(MapAdasFunctionNameToIndex("AUTOMATIC_EMERGENCY_BRAKING"), 5);
    EXPECT_EQ(MapAdasFunctionNameToIndex("ADAPTIVE_CRUISE_CONTROL"), 8);
    EXPECT_EQ(MapAdasFunctionNameToIndex("SPEED_LIMIT_CONTROL"), 23);
}

TEST(AdasSlotTableTest, LabelLookupStaysCaseInsensitiveAfterTableMerge)
{
    EXPECT_EQ(MapAdasFunctionNameToIndex("night_vision"), MapAdasFunctionNameToIndex("NIGHT_VISION"));
    EXPECT_EQ(MapAdasFunctionNameToIndex("not_a_real_function"), -1);
}

}  // namespace gt_esmini::realdetail
