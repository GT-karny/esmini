// feature:F7 scenario-driven handover — domain-transition classification truth
// table (docs/virtualdriver/scenario_control_handoff_design.md §3/§7 step 5).
//
// ControllerVirtualDriver::Activate() wraps the upstream
// scenarioengine::Controller::Activate() call with:
//
//   was_active = Active();            // before
//   rc = Controller::Activate(mode);  // upstream: pure bitmask update, no I/O
//   is_active = Active();             // after
//   if (!was_active &&  is_active) SetUpControlOutputs();    // INACTIVE->ACTIVE
//   else if (was_active && !is_active) TearDownControlOutputs(); // ACTIVE->INACTIVE
//   // else: no-op (ACTIVE->ACTIVE / INACTIVE->INACTIVE) — R-3, avoids the
//   // un-guarded input_source_->Init() double-open (design doc fact H).
//
// ControllerVirtualDriver itself is not practically unit-constructible (needs
// a live ScenarioEngine, config files, a physics backend, ...), so this file
// pins the CONTRACT that classification logic depends on: upstream
// scenarioengine::Controller::Activate()/Active() semantics, exercised
// directly. If ControllerVirtualDriver.cpp's classification is ever rewritten,
// mirror the change here too.

#include <gtest/gtest.h>

#include "Controller.hpp"
#include "OSCProperties.hpp"

namespace gt_esmini
{
namespace
{

using scenarioengine::Controller;

enum class Transition
{
    SETUP,     // INACTIVE -> ACTIVE
    TEARDOWN,  // ACTIVE -> INACTIVE
    NOOP       // ACTIVE -> ACTIVE, or INACTIVE -> INACTIVE
};

// Controller::Controller(InitArgs*) hard-fails (LOG_ERROR_AND_QUIT) on a null
// args pointer, and unconditionally dereferences args->properties — a real
// (if minimal, no "mode" property) OSCProperties is required or the
// constructor itself crashes before Activate() is ever exercised.
class TestController : public Controller
{
public:
    TestController() : Controller(MakeArgs()) {}

private:
    static InitArgs* MakeArgs()
    {
        static scenarioengine::OSCProperties props;
        static InitArgs                      args{"test_controller", "test", &props, nullptr, nullptr};
        return &args;
    }
};

// Mirrors ControllerVirtualDriver::Activate()'s classification exactly (see
// file header). Only LAT/LONG are driven — DOMAIN_LIGHT/DOMAIN_ANIM are left
// UNDEFINED throughout, matching how ActivateControllerAction addresses VD in
// every GT scenario (OSC1.2's lateral/longitudinal attributes only).
Transition ActivateAndClassify(Controller& c, ControlActivationMode lat, ControlActivationMode lon)
{
    ControlActivationMode mode[static_cast<unsigned int>(ControlDomains::COUNT)];
    for (auto& m : mode) m = ControlActivationMode::UNDEFINED;
    mode[static_cast<unsigned int>(ControlDomains::DOMAIN_LAT)]  = lat;
    mode[static_cast<unsigned int>(ControlDomains::DOMAIN_LONG)] = lon;

    const bool was_active = c.Active();
    c.Activate(mode);
    const bool is_active = c.Active();

    if (!was_active && is_active) return Transition::SETUP;
    if (was_active && !is_active) return Transition::TEARDOWN;
    return Transition::NOOP;
}

}  // namespace

TEST(ControllerVirtualDriverActivationTest, FreshControllerBothDomainsOnIsSetup)
{
    TestController c;
    ASSERT_FALSE(c.Active());
    EXPECT_EQ(ActivateAndClassify(c, ControlActivationMode::ON, ControlActivationMode::ON), Transition::SETUP);
    EXPECT_TRUE(c.Active());
}

TEST(ControllerVirtualDriverActivationTest, FreshControllerBothDomainsOffIsNoop)
{
    // Matches upstream OSCPrivateAction.cpp: an ActivateControllerAction with
    // lateral="false" longitudinal="false" on an already-inactive controller
    // never reaches Controller::Deactivate() — it flows through Activate()
    // with OFF modes (design doc Fact A). Starting from fresh/inactive, OFF
    // on an already-off domain is a true no-op.
    TestController c;
    ASSERT_FALSE(c.Active());
    EXPECT_EQ(ActivateAndClassify(c, ControlActivationMode::OFF, ControlActivationMode::OFF), Transition::NOOP);
    EXPECT_FALSE(c.Active());
}

TEST(ControllerVirtualDriverActivationTest, StayingActiveIsNoop)
{
    // R-3: re-sending ON while already active on both domains must NOT be
    // classified as a fresh setup (design doc fact H: input_source_->Init()
    // has no re-entry guard and would re-open the joystick / orphan haptic
    // effects on a second call).
    TestController c;
    ASSERT_EQ(ActivateAndClassify(c, ControlActivationMode::ON, ControlActivationMode::ON), Transition::SETUP);
    EXPECT_EQ(ActivateAndClassify(c, ControlActivationMode::ON, ControlActivationMode::ON), Transition::NOOP);
    EXPECT_TRUE(c.Active());
}

TEST(ControllerVirtualDriverActivationTest, FullTeardownFromBothActive)
{
    TestController c;
    ASSERT_EQ(ActivateAndClassify(c, ControlActivationMode::ON, ControlActivationMode::ON), Transition::SETUP);
    EXPECT_EQ(ActivateAndClassify(c, ControlActivationMode::OFF, ControlActivationMode::OFF), Transition::TEARDOWN);
    EXPECT_FALSE(c.Active());
}

TEST(ControllerVirtualDriverActivationTest, ReactivationAfterTeardownIsSetupAgain)
{
    // §2 confirmation item ② of the design doc: re-Activate() runs through the
    // EXACT SAME SetUpControlOutputs() path as the very first activation —
    // pinned here as "classified as SETUP both times", not merely "Active()
    // becomes true both times".
    TestController c;
    EXPECT_EQ(ActivateAndClassify(c, ControlActivationMode::ON, ControlActivationMode::ON), Transition::SETUP);
    EXPECT_EQ(ActivateAndClassify(c, ControlActivationMode::OFF, ControlActivationMode::OFF), Transition::TEARDOWN);
    EXPECT_EQ(ActivateAndClassify(c, ControlActivationMode::ON, ControlActivationMode::ON), Transition::SETUP);
    EXPECT_TRUE(c.Active());
}

TEST(ControllerVirtualDriverActivationTest, PartialDomainOffWhileOtherStaysActiveIsNoop)
{
    // ActivateControllerAction lateral="false" longitudinal="true": one domain
    // drops but the controller as a whole remains Active() (the other domain
    // still holds a bit) — must NOT tear down the shared control outputs
    // (FFB/input source/override latch are controller-wide, not per-domain).
    TestController c;
    ASSERT_EQ(ActivateAndClassify(c, ControlActivationMode::ON, ControlActivationMode::ON), Transition::SETUP);
    EXPECT_EQ(ActivateAndClassify(c, ControlActivationMode::OFF, ControlActivationMode::UNDEFINED), Transition::NOOP);
    EXPECT_TRUE(c.Active());  // DOMAIN_LONG bit still set
}

TEST(ControllerVirtualDriverActivationTest, TurningOffTheLastRemainingDomainIsTeardown)
{
    // Continuation of the previous case: once LONG also drops, Active() goes
    // to false and teardown must fire exactly then (not before).
    TestController c;
    ASSERT_EQ(ActivateAndClassify(c, ControlActivationMode::ON, ControlActivationMode::ON), Transition::SETUP);
    ASSERT_EQ(ActivateAndClassify(c, ControlActivationMode::OFF, ControlActivationMode::UNDEFINED), Transition::NOOP);
    ASSERT_TRUE(c.Active());
    EXPECT_EQ(ActivateAndClassify(c, ControlActivationMode::UNDEFINED, ControlActivationMode::OFF), Transition::TEARDOWN);
    EXPECT_FALSE(c.Active());
}

TEST(ControllerVirtualDriverActivationTest, UndefinedOnBothDomainsIsAlwaysNoop)
{
    // UNDEFINED means "leave this domain's bit exactly as it is" — never a
    // transition either way, whatever the starting state.
    TestController c;
    EXPECT_EQ(ActivateAndClassify(c, ControlActivationMode::UNDEFINED, ControlActivationMode::UNDEFINED), Transition::NOOP);
    EXPECT_FALSE(c.Active());

    ASSERT_EQ(ActivateAndClassify(c, ControlActivationMode::ON, ControlActivationMode::ON), Transition::SETUP);
    EXPECT_EQ(ActivateAndClassify(c, ControlActivationMode::UNDEFINED, ControlActivationMode::UNDEFINED), Transition::NOOP);
    EXPECT_TRUE(c.Active());
}

}  // namespace gt_esmini
