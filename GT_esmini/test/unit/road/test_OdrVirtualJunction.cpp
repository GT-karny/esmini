// test_OdrVirtualJunction.cpp -- P6 virtual junction S1 [GT_ODR:vj-model] data-model tests.
//
// Pure data-model coverage of the additive RoadManager.hpp members landed at Stage 1 (design
// odr_p6_virtual_junction_design.md section 2): defaults + setter/getter roundtrips only.
// NO xodr parsing here -- parse-time population is Stage 2 scope (fixture 23 end-to-end).
//
// Deliberately NOT tested (declaration-only until S2/S3, would not link):
//   - RoadLink 6-arg mid-contact ctor overload (defined in RoadManager.cpp at S2)
//   - Connection 5-arg virtual-connection ctor overload (defined at S2)
//   - OpenDrive::GetVirtualJunctionAtRoadS / GetVirtualJunctionAnchors (registry built at S3)
#include <gtest/gtest.h>

#include "RoadManager.hpp"

using namespace roadmanager;

// RoadLink: default-constructed and legacy 4-arg-constructed links are NOT virtual-junction
// links -- element_s_ < 0 (the mid-contact discriminator) and direction UNKNOWN.
TEST(OdrVirtualJunction, RoadLinkElementSDefaults)
{
    RoadLink default_link;
    EXPECT_DOUBLE_EQ(default_link.GetElementS(), -1.0);
    EXPECT_EQ(default_link.GetElementDir(), RoadLink::DIR_UNKNOWN);

    RoadLink legacy_link(SUCCESSOR, RoadLink::ELEMENT_TYPE_ROAD, 2, CONTACT_POINT_START);
    EXPECT_DOUBLE_EQ(legacy_link.GetElementS(), -1.0) << "legacy ctor must leave the mid-contact discriminator unset";
    EXPECT_EQ(legacy_link.GetElementDir(), RoadLink::DIR_UNKNOWN);
    EXPECT_EQ(legacy_link.GetElementId(), 2u);
    EXPECT_EQ(legacy_link.GetContactPointType(), CONTACT_POINT_START);
}

// Junction: VIRTUAL type exists and the S1 VirtualJunctionAttributes default to "absent"
// (ID_UNDEFINED / -1 / -1 / ORIENTATION_NONE -- Ex_Pedestrian_Crossing omits @orientation),
// and roundtrip through Set/GetVirtualAttributes.
TEST(OdrVirtualJunction, JunctionVirtualAttributesRoundtrip)
{
    Junction junction(888, "888", "vj", Junction::VIRTUAL);
    EXPECT_EQ(junction.GetType(), Junction::VIRTUAL);

    const Junction::VirtualJunctionAttributes& defaults = junction.GetVirtualAttributes();
    EXPECT_EQ(defaults.main_road_id_, ID_UNDEFINED);
    EXPECT_DOUBLE_EQ(defaults.s_start_, -1.0);
    EXPECT_DOUBLE_EQ(defaults.s_end_, -1.0);
    EXPECT_EQ(defaults.orientation_, Junction::ORIENTATION_NONE);

    Junction::VirtualJunctionAttributes attributes;
    attributes.main_road_id_ = 1;
    attributes.s_start_      = 95.0;
    attributes.s_end_        = 105.0;
    attributes.orientation_  = Junction::ORIENTATION_PLUS;
    junction.SetVirtualAttributes(attributes);

    const Junction::VirtualJunctionAttributes& stored = junction.GetVirtualAttributes();
    EXPECT_EQ(stored.main_road_id_, 1u);
    EXPECT_DOUBLE_EQ(stored.s_start_, 95.0);
    EXPECT_DOUBLE_EQ(stored.s_end_, 105.0);
    EXPECT_EQ(stored.orientation_, Junction::ORIENTATION_PLUS);
}

// Connection: contact-s / is_virtual_ defaults and the SetVirtual roundtrip. The legacy 3-arg
// ctor only assigns the two road pointers and the contact point (RoadManager.cpp), so nullptr
// roads are safe for a pure field-level test; the dtor only clears the (empty) lane links.
TEST(OdrVirtualJunction, ConnectionVirtualDefaults)
{
    Connection connection(nullptr, nullptr, CONTACT_POINT_START);
    EXPECT_DOUBLE_EQ(connection.GetIncomingContactS(), -1.0);
    EXPECT_DOUBLE_EQ(connection.GetOutgoingContactS(), -1.0);
    EXPECT_FALSE(connection.IsVirtual());

    connection.SetVirtual(true);
    EXPECT_TRUE(connection.IsVirtual()) << "kind-2 topological connections are flagged via SetVirtual (store-only in v1)";
}

// LaneRoadLaneConnection: public pass-through contact_s_ (mirrors the existing public
// contact_point_ member) defaults to "legacy placement".
TEST(OdrVirtualJunction, LaneRoadLaneConnectionContactS)
{
    LaneRoadLaneConnection lane_connection;
    EXPECT_DOUBLE_EQ(lane_connection.contact_s_, -1.0);

    lane_connection.contact_s_ = 100.0;
    EXPECT_DOUBLE_EQ(lane_connection.contact_s_, 100.0);
}

// RoadPath::PathNode: plain-struct contact_s defaults to "legacy end contact".
TEST(OdrVirtualJunction, PathNodeContactS)
{
    RoadPath::PathNode node;
    EXPECT_DOUBLE_EQ(node.contact_s, -1.0);
    EXPECT_EQ(node.link, nullptr);
}
