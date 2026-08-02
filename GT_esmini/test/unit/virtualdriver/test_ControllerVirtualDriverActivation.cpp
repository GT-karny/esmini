// feature:F7 scenario-driven handover — domain-transition classification truth
// table (docs/virtualdriver/design/scenario_control_handoff_design.md §3/§7 step 5).
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
#include "gt_esmini/control/manualdrive/ManualDriveConfig.hpp"
#include "gt_esmini/control/manualdrive/OverrideManager.hpp"

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

// --- feature:F7 — every route out of control must release the outputs -------
//
// The tests above are all happy paths: they check how Activate() CLASSIFIES a
// transition. None of them asks what happens on the routes that do not go
// through Activate() at all, and that gap hid a live defect: from
// OpenSCENARIO v1.3 onwards, an ActivateControllerAction that hands a domain
// to another controller deactivates the incumbent by calling
// Controller::DeactivateDomains(mask) directly (OSCPrivateAction.cpp), never
// touching Deactivate(). While that virtual was left un-overridden, the FFB
// servo was never stopped on that route.
//
// That is the worst failure this controller has, and it is not hypothetical:
// the device holds the last commanded force as an infinite-duration constant
// effect, and ScenarioEngine stops stepping a controller that is no longer
// active -- so nothing is left running that could ever release the wheel.
//
// Following this file's existing convention (ControllerVirtualDriver is not
// unit-constructible), the spy below MIRRORS the production overrides. If
// ControllerVirtualDriver::Deactivate/DeactivateDomains/TearDownControlOutputs
// change, change this too.

namespace
{

class TeardownSpyController : public Controller
{
public:
    TeardownSpyController() : Controller(MakeArgs()) {}

    int  teardown_count = 0;
    bool outputs_live   = false;

    // Mirrors ControllerVirtualDriver::Activate().
    int Activate(const ControlActivationMode (&mode)[static_cast<unsigned int>(ControlDomains::COUNT)]) override
    {
        const bool was_active = Active();
        const int  rc         = Controller::Activate(mode);
        const bool is_active  = Active();
        if (!was_active && is_active)      SetUp();
        else if (was_active && !is_active) TearDown();
        return rc;
    }

    // Mirrors ControllerVirtualDriver::Deactivate().
    void Deactivate() override
    {
        if (Active()) TearDown();
        Controller::Deactivate();
    }

    // Mirrors ControllerVirtualDriver::DeactivateDomains().
    void DeactivateDomains(unsigned int domains) override
    {
        const bool losing_lateral =
            IsActiveOnDomains(static_cast<unsigned int>(ControlDomainMasks::DOMAIN_MASK_LAT)) &&
            (domains & static_cast<unsigned int>(ControlDomainMasks::DOMAIN_MASK_LAT)) != 0;
        const bool goes_inactive = Active() && (GetActiveDomains() & ~domains) == 0;
        if (losing_lateral || goes_inactive) TearDown();
        Controller::DeactivateDomains(domains);
    }

    // Mirrors SetUpControlOutputs(): arms the guard at the TOP, so a setup that
    // bails part way through still leaves outputs to release.
    void SetUp()
    {
        released_    = false;
        outputs_live = true;
    }

    // Mirrors TearDownControlOutputs().
    void TearDown()
    {
        if (released_) return;
        released_    = true;
        outputs_live = false;
        ++teardown_count;
    }

private:
    bool released_ = true;   // nothing set up yet -> nothing to release

    static InitArgs* MakeArgs()
    {
        static scenarioengine::OSCProperties props;
        static InitArgs                      args{"spy_controller", "test", &props, nullptr, nullptr};
        return &args;
    }
};

void ActivateBoth(Controller& c)
{
    ControlActivationMode mode[static_cast<unsigned int>(ControlDomains::COUNT)];
    for (auto& m : mode) m = ControlActivationMode::UNDEFINED;
    mode[static_cast<unsigned int>(ControlDomains::DOMAIN_LAT)]  = ControlActivationMode::ON;
    mode[static_cast<unsigned int>(ControlDomains::DOMAIN_LONG)] = ControlActivationMode::ON;
    c.Activate(mode);
}

void ActivateLongOnly(Controller& c)
{
    ControlActivationMode mode[static_cast<unsigned int>(ControlDomains::COUNT)];
    for (auto& m : mode) m = ControlActivationMode::UNDEFINED;
    mode[static_cast<unsigned int>(ControlDomains::DOMAIN_LONG)] = ControlActivationMode::ON;
    c.Activate(mode);
}

constexpr unsigned int kLat = static_cast<unsigned int>(ControlDomainMasks::DOMAIN_MASK_LAT);
constexpr unsigned int kLon = static_cast<unsigned int>(ControlDomainMasks::DOMAIN_MASK_LONG);
constexpr unsigned int kAll = static_cast<unsigned int>(ControlDomainMasks::DOMAIN_MASK_ALL);

}  // namespace

TEST(ControllerVirtualDriverTeardownTest, DeactivateDomainsOnLateralReleasesOutputs)
{
    // THE BYPASS. Upstream reaches the incumbent controller through a base
    // Controller& and calls this; if it does not release, the servo keeps
    // pulling a wheel this controller no longer steers.
    TeardownSpyController spy;
    ActivateBoth(spy);
    ASSERT_TRUE(spy.outputs_live);

    Controller& as_base = spy;          // exactly how OSCPrivateAction.cpp holds it
    as_base.DeactivateDomains(kLat);

    EXPECT_EQ(spy.teardown_count, 1) << "handing the lateral domain to another "
                                        "controller must release the FFB servo";
    EXPECT_FALSE(spy.outputs_live);
}

TEST(ControllerVirtualDriverTeardownTest, DeactivateDomainsOnLongitudinalOnlyKeepsSteering)
{
    // The other side: a scenario that takes only the longitudinal domain
    // leaves us steering, so releasing the servo there would be a bug of the
    // opposite sign — the wheel would go dead mid-corner.
    TeardownSpyController spy;
    ActivateBoth(spy);

    Controller& as_base = spy;
    as_base.DeactivateDomains(kLon);

    EXPECT_EQ(spy.teardown_count, 0);
    EXPECT_TRUE(spy.outputs_live);
    EXPECT_TRUE(spy.Active());
}

TEST(ControllerVirtualDriverTeardownTest, LosingTheLastDomainReleasesEvenWithoutLateral)
{
    // A controller active only on the longitudinal domain still has telemetry
    // and a latch to put back; losing its last domain must release.
    TeardownSpyController spy;
    ActivateLongOnly(spy);
    ASSERT_TRUE(spy.Active());

    Controller& as_base = spy;
    as_base.DeactivateDomains(kLon);

    EXPECT_EQ(spy.teardown_count, 1);
    EXPECT_FALSE(spy.Active());
}

TEST(ControllerVirtualDriverTeardownTest, DoubleDeactivateReleasesExactlyOnce)
{
    TeardownSpyController spy;
    ActivateBoth(spy);

    spy.Deactivate();
    spy.Deactivate();

    EXPECT_EQ(spy.teardown_count, 1) << "a second Deactivate() must be a no-op, not "
                                        "a second release against already-released outputs";
}

TEST(ControllerVirtualDriverTeardownTest, DeactivateNestsIntoDeactivateDomainsAndStillReleasesOnce)
{
    // Deactivate() delegates to the upstream base, which calls the VIRTUAL
    // DeactivateDomains(ALL) — landing back in our own override. Without the
    // guard this route releases twice.
    TeardownSpyController spy;
    ActivateBoth(spy);

    spy.Deactivate();

    EXPECT_EQ(spy.teardown_count, 1);
    EXPECT_FALSE(spy.Active());
}

TEST(ControllerVirtualDriverTeardownTest, DeactivateWithoutEverActivatingDoesNotRelease)
{
    // Scenario aborted before this controller was ever activated: there is no
    // input source and no servo, and teardown would be running against
    // half-constructed state.
    TeardownSpyController spy;
    ASSERT_FALSE(spy.Active());

    spy.Deactivate();
    Controller& as_base = spy;
    as_base.DeactivateDomains(kAll);

    EXPECT_EQ(spy.teardown_count, 0);
}

TEST(ControllerVirtualDriverTeardownTest, ReactivationRearmsTheRelease)
{
    // The guard must not latch permanently: after a re-activation the next
    // hand-over has to release again.
    TeardownSpyController spy;
    ActivateBoth(spy);
    spy.Deactivate();
    ASSERT_EQ(spy.teardown_count, 1);

    ActivateBoth(spy);
    ASSERT_TRUE(spy.outputs_live);
    Controller& as_base = spy;
    as_base.DeactivateDomains(kLat);

    EXPECT_EQ(spy.teardown_count, 2);
}

// --- feature:F7 — the bypass route must also hand the latch back to AUTO ----
//
// The tests above prove that DeactivateDomains() reaches teardown. They do NOT
// prove what teardown then does to the intervention latch, and that coupling
// was resting on code reading alone: TearDownControlOutputs() calls
// OverrideManager::RequestAutoMode(), which is one of the three routes back to
// AUTO that share ResetInterventionStateOnReturnToAuto(). Two separately
// correct halves do not make a correct whole -- that assumption is exactly
// what the un-overridden virtual cost us in the first place.
//
// So the spy below drives a REAL OverrideManager the way the controller does,
// and the assertion is behavioural: after the bypass route fires, the driver
// must be able to override AGAIN.

namespace
{

class TeardownWithLatchSpy : public Controller
{
public:
    TeardownWithLatchSpy() : Controller(MakeArgs())
    {
        ManualDriveConfig cfg;
        cfg.override_cfg.enabled            = true;
        cfg.override_cfg.steering_threshold = 0.05;
        cfg.override_cfg.throttle_threshold = 0.10;
        cfg.override_cfg.brake_threshold    = 0.10;
        cfg.domain.lateral                  = "manual";
        cfg.domain.longitudinal             = "manual";
        mgr_.Configure(cfg);
    }

    OverrideManager& Manager() { return mgr_; }

    void Steer(double axis, double dt = 0.02)
    {
        InputFrame f;
        PedalSteerCommand ps;
        ps.steering = axis;
        f.pedal_steer = ps;
        f.connected   = true;
        mgr_.Update(f, dt);
    }

    void DeactivateDomains(unsigned int domains) override
    {
        const bool losing_lateral =
            IsActiveOnDomains(static_cast<unsigned int>(ControlDomainMasks::DOMAIN_MASK_LAT)) &&
            (domains & static_cast<unsigned int>(ControlDomainMasks::DOMAIN_MASK_LAT)) != 0;
        const bool goes_inactive = Active() && (GetActiveDomains() & ~domains) == 0;
        if ((losing_lateral || goes_inactive) && !released_)
        {
            released_ = true;
            // Mirrors TearDownControlOutputs(): stop the servo, then hand the
            // latch back to AUTO.
            mgr_.RequestAutoMode();
        }
        Controller::DeactivateDomains(domains);
    }

    void Arm() { released_ = false; }

private:
    OverrideManager mgr_;
    bool            released_ = false;

    static InitArgs* MakeArgs()
    {
        static scenarioengine::OSCProperties props;
        static InitArgs                      args{"latch_spy", "test", &props, nullptr, nullptr};
        return &args;
    }
};

}  // namespace

TEST(ControllerVirtualDriverTeardownTest, BypassRouteReturnsTheInterventionLatchToAuto)
{
    TeardownWithLatchSpy spy;
    ActivateBoth(spy);
    spy.Arm();

    // A driver overrides: centred start, then steers.
    spy.Steer(0.0);
    spy.Steer(0.30);
    ASSERT_TRUE(spy.Manager().IsLateralManual()) << "setup: the override must latch";

    // The scenario hands the lateral domain to another controller — the route
    // that skipped teardown entirely before 7678bb99.
    Controller& as_base = spy;
    as_base.DeactivateDomains(static_cast<unsigned int>(ControlDomainMasks::DOMAIN_MASK_LAT));

    EXPECT_FALSE(spy.Manager().IsLateralManual())
        << "control was taken away, so a latch describing a driver who no longer has "
           "the wheel must not survive it";
    EXPECT_FALSE(spy.Manager().IsAnyManual());
}

TEST(ControllerVirtualDriverTeardownTest, DriverCanOverrideAgainAfterTheBypassRoute)
{
    // The consequence that matters to the user, and the reason the reset has to
    // be the SAME one the RESUME button performs: carrying stale detector state
    // across the hand-back is what "I came back and then could not override
    // again" was made of.
    TeardownWithLatchSpy spy;
    ActivateBoth(spy);
    spy.Arm();

    spy.Steer(0.0);
    spy.Steer(0.30);
    ASSERT_TRUE(spy.Manager().IsLateralManual());

    Controller& as_base = spy;
    as_base.DeactivateDomains(static_cast<unsigned int>(ControlDomainMasks::DOMAIN_MASK_LAT));
    ASSERT_FALSE(spy.Manager().IsAnyManual());

    // Second intervention, same shape as the first. The wheel is still sitting
    // where the driver left it, so the return re-baselined the axis; a fresh
    // movement past the threshold must latch again.
    spy.Steer(0.30);
    spy.Steer(0.40);
    EXPECT_TRUE(spy.Manager().IsLateralManual())
        << "after the scenario took control away and gave it back, the driver must be "
           "able to override again";
}

TEST(ControllerVirtualDriverTeardownTest, SetupThatBailsPartWayThroughStillReleases)
{
    // SetUpControlOutputs() arms the guard on its FIRST line precisely so that
    // a setup which throws or early-returns afterwards still leaves something
    // that can be released. Modelled here by arming and then abandoning setup.
    TeardownSpyController spy;
    ActivateBoth(spy);          // arms the guard
    spy.outputs_live = false;   // pretend setup bailed after arming

    spy.Deactivate();

    EXPECT_EQ(spy.teardown_count, 1) << "a half-configured controller must still "
                                        "release — the unacceptable failure is a servo "
                                        "left running because setup did not finish";
}

}  // namespace gt_esmini
