// vd_intent_layer.md section 5 -- the winner ApplyPolicyConstraints' min() fold used to throw
// away.
//
// These tests exist because the attribution is INVISIBLE in the thing the planner produces: the
// speed profile comes out numerically identical whether binding_constraint_index names the right
// constraint, the wrong one, or nothing at all. A behavioural test can therefore never fail on a
// wrong attribution, which is exactly why the rule is pinned directly here.
//
// The rule: the LAST constraint (in emission order) to STRICTLY lower the ego's own sample wins.
// -1 means no policy constraint governs at the ego -- either none exist, or the road-geometry
// ceiling is already below all of them.

#include "gt_esmini/control/virtualdriver/ManeuverAwareSpeedPlanner.hpp"

#include <gtest/gtest.h>

using namespace gt_esmini;

namespace
{

// A flat 100 m ceiling at `v_ceiling`, sampled every 2 m starting AT the ego (s_ahead == 0).
std::vector<MidLongScanSample> FlatProfile(double v_ceiling, double length_m = 100.0, double step = 2.0)
{
    std::vector<MidLongScanSample> samples;
    for (double s = 0.0; s <= length_m; s += step)
    {
        MidLongScanSample sample;
        sample.s_ahead = s;
        sample.v       = v_ceiling;
        samples.push_back(sample);
    }
    return samples;
}

PolicyConstraint MaxSpeed(double cap, const char* source, PolicyConstraint::Tier tier = PolicyConstraint::Tier::COMFORT)
{
    PolicyConstraint c;
    c.kind   = PolicyConstraint::Kind::MAX_SPEED;
    c.value  = cap;
    c.source = source;
    c.tier   = tier;
    return c;
}

PolicyConstraint MaxSpeedToS(double cap, double s, const char* source)
{
    PolicyConstraint c;
    c.kind   = PolicyConstraint::Kind::MAX_SPEED_TO_S;
    c.value  = cap;
    c.s      = s;
    c.source = source;
    return c;
}

PolicyConstraint StopAtS(double s, const char* source, PolicyConstraint::Tier tier = PolicyConstraint::Tier::COMFORT)
{
    PolicyConstraint c;
    c.kind   = PolicyConstraint::Kind::STOP_AT_S;
    c.s      = s;
    c.source = source;
    c.tier   = tier;
    return c;
}

TrafficPolicySnapshot Snapshot(std::vector<PolicyConstraint> constraints)
{
    TrafficPolicySnapshot snap;
    snap.valid       = true;
    snap.constraints = std::move(constraints);
    return snap;
}

const ManeuverAwareSpeedPlannerConfig kCfg{};  // comfort_decel 2.0, stop_band 2.0

}  // namespace

// ───────────────────────────────── nothing is binding ─────────────────────────────────

TEST(PolicyConstraintFold, NoPolicySnapshotBindsNothing)
{
    auto samples = FlatProfile(13.9);
    const auto fold = ApplyPolicyConstraints(samples, kCfg, nullptr);
    EXPECT_EQ(fold.binding_constraint_index, -1);
    EXPECT_DOUBLE_EQ(samples.front().v, 13.9);  // profile untouched
}

TEST(PolicyConstraintFold, AnEmptyConstraintListBindsNothing)
{
    auto samples = FlatProfile(13.9);
    const auto snap = Snapshot({});
    EXPECT_EQ(ApplyPolicyConstraints(samples, kCfg, &snap).binding_constraint_index, -1);
}

// The negative control that matters most: a constraint EXISTS and is folded in, but the road
// ceiling is already lower, so it decides nothing. An implementation that credits "the last
// constraint that was applied" rather than "the last one that strictly lowered the ego sample"
// passes every other test here and fails this one.
TEST(PolicyConstraintFold, AConstraintAboveTheRoadCeilingBindsNothing)
{
    auto samples = FlatProfile(8.0);  // road already limits to 8 m/s
    const auto snap = Snapshot({MaxSpeed(20.0, "lead_vehicle")});

    const auto fold = ApplyPolicyConstraints(samples, kCfg, &snap);

    EXPECT_EQ(fold.binding_constraint_index, -1);
    EXPECT_DOUBLE_EQ(samples.front().v, 8.0);
}

// A tie is not a decision either. Two policies agreeing on 10 m/s: the first one lowered the
// speed, the second changed nothing, so the FIRST keeps the credit.
TEST(PolicyConstraintFold, ATieDoesNotStealTheAttribution)
{
    auto samples = FlatProfile(13.9);
    const auto snap = Snapshot({MaxSpeed(10.0, "traffic_light"), MaxSpeed(10.0, "crosswalk")});

    EXPECT_EQ(ApplyPolicyConstraints(samples, kCfg, &snap).binding_constraint_index, 0);
}

// ───────────────────────────────── who wins ─────────────────────────────────

TEST(PolicyConstraintFold, TheLowestCapAtTheEgoWinsRegardlessOfEmissionOrder)
{
    // Emitted loosest-first...
    {
        auto samples = FlatProfile(13.9);
        const auto snap = Snapshot({MaxSpeed(12.0, "lead_vehicle"), MaxSpeed(6.0, "crosswalk")});
        EXPECT_EQ(ApplyPolicyConstraints(samples, kCfg, &snap).binding_constraint_index, 1);
        EXPECT_DOUBLE_EQ(samples.front().v, 6.0);
    }
    // ...and tightest-first. The fold is a min(), so the ORDER must not change the answer.
    {
        auto samples = FlatProfile(13.9);
        const auto snap = Snapshot({MaxSpeed(6.0, "crosswalk"), MaxSpeed(12.0, "lead_vehicle")});
        EXPECT_EQ(ApplyPolicyConstraints(samples, kCfg, &snap).binding_constraint_index, 0);
        EXPECT_DOUBLE_EQ(samples.front().v, 6.0);
    }
}

// The situation section 5-1 opens with: a red light, a lead vehicle and a crosswalk all live at
// once. Exactly one of them is deciding the speed, and before this field there was no way to say
// which.
TEST(PolicyConstraintFold, ThreeSimultaneousConstraintsResolveToOneWinner)
{
    auto samples = FlatProfile(13.9);
    const auto snap = Snapshot({
        StopAtS(80.0, "traffic_light", PolicyConstraint::Tier::COMPLIANCE),  // far -> gentle ramp
        MaxSpeed(9.0, "lead_vehicle"),
        MaxSpeedToS(11.0, 40.0, "crosswalk"),
    });

    const auto fold = ApplyPolicyConstraints(samples, kCfg, &snap);

    // At the ego, the 80 m stop ramp is sqrt(2*2*78) ~= 17.7 m/s (above the ceiling, so it does
    // not bind), the crosswalk caps 11, and the lead caps 9 -- the lead is the one actually
    // holding the car back.
    EXPECT_EQ(fold.binding_constraint_index, 1);
    EXPECT_DOUBLE_EQ(samples.front().v, 9.0);
}

// A SAFETY-tier stop shapes its approach at emergency_decel, so it can bind from much further
// out than a comfort-tier one at the same distance. That the two differ here is the whole reason
// tier lives on the constraint.
TEST(PolicyConstraintFold, ASafetyTierStopBindsWhereAComfortOneWouldNot)
{
    // 20 m ahead, zero_from = 18 m. comfort ramp = sqrt(2*2.0*18) = 8.49 -> binds under a 13.9
    // ceiling too, so pick a ceiling BETWEEN the two ramps to separate them:
    // emergency ramp = sqrt(2*8.0*18) = 16.97.
    const double ceiling = 12.0;

    {
        auto samples = FlatProfile(ceiling);
        const auto snap = Snapshot({StopAtS(20.0, "aeb", PolicyConstraint::Tier::SAFETY)});
        // 16.97 > 12.0 -> the emergency ramp does NOT bind this far out.
        EXPECT_EQ(ApplyPolicyConstraints(samples, kCfg, &snap).binding_constraint_index, -1);
    }
    {
        auto samples = FlatProfile(ceiling);
        const auto snap = Snapshot({StopAtS(20.0, "traffic_light", PolicyConstraint::Tier::COMFORT)});
        // 8.49 < 12.0 -> the comfort ramp binds, because braking gently means starting earlier.
        EXPECT_EQ(ApplyPolicyConstraints(samples, kCfg, &snap).binding_constraint_index, 0);
        EXPECT_NEAR(samples.front().v, 8.485, 1.0e-3);
    }
}

// A MAX_SPEED_TO_S whose s is BEHIND the ego (already passed) touches no sample at the ego and
// must not be credited.
TEST(PolicyConstraintFold, AConstraintThatEndsBeforeTheEgoBindsNothing)
{
    auto samples = FlatProfile(13.9);
    // s = -1 -> the "sample.s_ahead <= constraint.s" test fails for every sample including 0.
    const auto snap = Snapshot({MaxSpeedToS(3.0, -1.0, "crosswalk")});

    const auto fold = ApplyPolicyConstraints(samples, kCfg, &snap);

    EXPECT_EQ(fold.binding_constraint_index, -1);
    EXPECT_DOUBLE_EQ(samples.front().v, 13.9);
}

// The stop point is inside the hard-zero band, so the ego sample is driven to 0. That IS the
// binding constraint -- a stop is not exempt from attribution just because it zeroes the speed.
TEST(PolicyConstraintFold, AStopInsideTheZeroBandBindsAtZeroSpeed)
{
    auto samples = FlatProfile(13.9);
    const auto snap = Snapshot({StopAtS(1.0, "stop_sign", PolicyConstraint::Tier::COMPLIANCE)});

    const auto fold = ApplyPolicyConstraints(samples, kCfg, &snap);

    EXPECT_EQ(fold.binding_constraint_index, 0);
    EXPECT_DOUBLE_EQ(samples.front().v, 0.0);
}

// ───────────────────────────────── the profile is unchanged ─────────────────────────────────

// The whole point of section 5 is that the attribution is FREE: it is bookkeeping alongside a
// fold that already happened. This asserts the fold's own output is byte-for-byte what it was --
// if adding the index had perturbed a single sample, every regression baseline would move.
TEST(PolicyConstraintFold, TheFoldedProfileIsUnaffectedByTheAttribution)
{
    const auto snap = Snapshot({
        StopAtS(30.0, "traffic_light", PolicyConstraint::Tier::COMPLIANCE),
        MaxSpeed(11.0, "lead_vehicle"),
        MaxSpeedToS(7.0, 20.0, "crosswalk"),
    });

    auto folded = FlatProfile(13.9);
    ApplyPolicyConstraints(folded, kCfg, &snap);

    // Re-derive the expected profile by hand, applying the same three constraints in the same
    // order with no bookkeeping at all.
    auto expected = FlatProfile(13.9);
    for (auto& sample : expected)
    {
        const double zero_from = 30.0 - kCfg.stop_band;
        if (sample.s_ahead >= zero_from)
        {
            sample.v = 0.0;
        }
        else
        {
            sample.v = std::min(sample.v, std::sqrt(2.0 * kCfg.comfort_decel * (zero_from - sample.s_ahead)));
        }
        sample.v = std::min(sample.v, 11.0);
        if (sample.s_ahead <= 20.0) sample.v = std::min(sample.v, 7.0);
    }

    ASSERT_EQ(folded.size(), expected.size());
    for (size_t i = 0; i < folded.size(); ++i)
    {
        EXPECT_DOUBLE_EQ(folded[i].v, expected[i].v) << "sample " << i << " at s=" << folded[i].s_ahead;
    }
}

// The markers the fold appends are one per STOP_AT_S, unchanged by this feature.
TEST(PolicyConstraintFold, StopConstraintsStillProduceOneMarkerEach)
{
    auto samples = FlatProfile(13.9);
    const auto snap = Snapshot({StopAtS(30.0, "traffic_light"), MaxSpeed(11.0, "lead_vehicle"), StopAtS(50.0, "crosswalk")});

    const auto fold = ApplyPolicyConstraints(samples, kCfg, &snap);

    ASSERT_EQ(fold.markers.size(), 2u);
    EXPECT_EQ(fold.markers[0].kind, "stop");
    EXPECT_EQ(fold.markers[1].kind, "stop");
}
