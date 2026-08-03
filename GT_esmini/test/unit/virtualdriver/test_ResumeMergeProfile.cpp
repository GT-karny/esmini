// feature:F7 unit tests for the pure AUTO_RESUME merge trajectory profile.
//
// Background (see ResumeMergeProfile.hpp and
// docs/virtualdriver/design/resume_merge_trajectory_design.md sections 8-1..8-7):
// on a manual->AUTO_RESUME transition, the reference the safety envelope is
// handed is a raw step (TrajectoryShortPlanner snaps to the routed lane
// center every frame, no ramp). This module generates a smooth quintic
// lateral-offset profile from the captured hand-over state (d0, v0_lat,
// a0_lat) instead, so the merge starts with no discontinuity in commanded
// steering angle. Test names below follow the design doc's section 8-7
// table verbatim.

#include <gtest/gtest.h>

#include "gt_esmini/control/virtualdriver/ResumeMergeProfile.hpp"

#include <algorithm>
#include <cmath>

namespace gt_esmini
{
namespace
{

ResumeMergeConfig MakeEnabledCfg()
{
    ResumeMergeConfig cfg;
    cfg.enabled = true;
    return cfg;
}

}  // namespace

// --- 1: shipped default -----------------------------------------------------

TEST(ResumeMergeProfileTest, ShippedDefaultEnablesResumeMerge)
{
    // Shipped ON since 2026-07-28, after the merge's smoothness was confirmed
    // on the real wheel. The test is kept (and renamed) rather than deleted:
    // its job is to make the shipped default a deliberate, visible decision, so
    // it has to move when the decision moves. kResumeMergeDefaultEnabled in
    // ResumeMergeProfile.hpp is the C++-side single source of truth; the same
    // value is mirrored by hand into config/virtual_driver.json and the web
    // backend's DEFAULT_VIRTUAL_DRIVER_CONFIG.
    EXPECT_TRUE(ResumeMergeConfig{}.enabled);
}

// --- 3: all six boundary conditions, including a0_lat != 0 ------------------

TEST(ResumeMergeProfileTest, ProfileSatisfiesAllSixBoundaryConditions)
{
    struct Case
    {
        double d0;
        double v0_lat;
        double a0_lat;
    };
    const Case cases[] = {
        {3.3, 0.0, 0.0},
        {3.3, 0.24, 0.0},
        {3.3, 0.24, 1.0},
        {3.3, 0.24, 2.58},
        {3.3, 0.24, -2.58},
        {-2.0, -0.1, -1.2},
        {-2.0, 0.3, 1.8},
    };

    for (const auto& c : cases)
    {
        const double T = 4.0;  // arbitrary fixed T: the boundary conditions are structural, true for ANY T

        EXPECT_NEAR(EvaluateQuinticOffset(c.d0, c.v0_lat, c.a0_lat, T, 0.0), c.d0, 1e-9)
            << "d(0) d0=" << c.d0 << " v0_lat=" << c.v0_lat << " a0_lat=" << c.a0_lat;
        EXPECT_NEAR(EvaluateQuinticVelocity(c.d0, c.v0_lat, c.a0_lat, T, 0.0), c.v0_lat, 1e-9)
            << "d'(0)";
        EXPECT_NEAR(EvaluateQuinticAccel(c.d0, c.v0_lat, c.a0_lat, T, 0.0), c.a0_lat, 1e-9)
            << "d''(0)";

        EXPECT_NEAR(EvaluateQuinticOffset(c.d0, c.v0_lat, c.a0_lat, T, T), 0.0, 1e-9) << "d(T)";
        EXPECT_NEAR(EvaluateQuinticVelocity(c.d0, c.v0_lat, c.a0_lat, T, T), 0.0, 1e-9) << "d'(T)";
        EXPECT_NEAR(EvaluateQuinticAccel(c.d0, c.v0_lat, c.a0_lat, T, T), 0.0, 1e-9) << "d''(T)";
    }
}

// --- 3b: start curvature matches hand-over exactly ---------------------------
//
// Regression pin for the design doc section 8-2 defect: a d''(0)=0 design
// produced a -6.212 degree steering-angle step at hand-over for a0_lat=2.58.
// The fix pins d''(0) to the ACTUAL a0_lat, so the mismatch must be zero for
// every nonzero a0_lat -- this is what "natural steering operation" (design
// doc section 8-1-1) cashes out to structurally.

TEST(ResumeMergeProfileTest, StartCurvatureMatchesHandoverExactly)
{
    for (const double a0_lat : {1.0, 2.58, -2.58, 4.0, -0.37})
    {
        const double accel_at_zero = EvaluateQuinticAccel(3.3, 0.24, a0_lat, /*duration_s=*/3.5, /*t=*/0.0);
        EXPECT_NEAR(accel_at_zero, a0_lat, 1e-9) << "a0_lat=" << a0_lat;
    }
}

// --- 4: lateral accel never exceeds handover-or-comfort, both signs --------

TEST(ResumeMergeProfileTest, LateralAccelNeverExceedsHandoverOrComfort)
{
    const double d0_values[]     = {0.6, 1.0, 3.3, -0.6, -1.0, -3.3};
    const double v0_lat_values[] = {0.0, 0.24, -0.24, 1.0, -1.0};
    const double a0_lat_values[] = {0.0, 1.0, -1.0, 2.58, -2.58, 4.0, -4.0};

    ResumeMergeConfig cfg = MakeEnabledCfg();
    cfg.a_lat_comfort      = 1.5;

    for (double d0 : d0_values)
        for (double v0_lat : v0_lat_values)
            for (double a0_lat : a0_lat_values)
            {
                ResumeMergeState state;
                ASSERT_TRUE(ArmResumeMerge(state, d0, v0_lat, a0_lat, cfg));

                // Reproduce the SAME grid the implementation used to select
                // T (kResumeMergeCurvatureSampleCount points over u in
                // [0,1]) -- this is a check of internal consistency ("does
                // the chosen T actually satisfy the criterion it was chosen
                // by"), not a claim about the true continuous maximum
                // between grid points.
                double max_abs_accel = 0.0;
                for (int i = 0; i < kResumeMergeCurvatureSampleCount; ++i)
                {
                    const double u = static_cast<double>(i) / static_cast<double>(kResumeMergeCurvatureSampleCount - 1);
                    const double t = u * state.duration_s;
                    max_abs_accel = std::max(max_abs_accel,
                                              std::fabs(EvaluateQuinticAccel(state.d0, state.v0_lat, state.a0_lat,
                                                                              state.duration_s, t)));
                }

                EXPECT_LE(max_abs_accel, state.a_bound + 1e-6)
                    << "d0=" << d0 << " v0_lat=" << v0_lat << " a0_lat=" << a0_lat
                    << " T=" << state.duration_s << " a_bound=" << state.a_bound;
            }
}

// --- 4b: v0_lat=a0_lat=0 duration matches the closed form -------------------

TEST(ResumeMergeProfileTest, ZeroInitialDerivativesMatchClosedForm)
{
    ResumeMergeConfig cfg = MakeEnabledCfg();
    cfg.a_lat_comfort      = 1.5;

    const double d0 = 3.3;
    bool         comfort_unmet = false;
    const double T = SelectResumeMergeDuration(d0, /*v0_lat=*/0.0, /*a0_lat=*/0.0, cfg, &comfort_unmet);

    // u-domain max|d''| for v0_lat=a0_lat=0 is exactly 10/sqrt(3) (design doc
    // section 8-2), independent of T -- so the closed-form T solves
    // (10/sqrt(3))*d0/T^2 = a_lat_comfort.
    const double expected_T = std::sqrt((10.0 / std::sqrt(3.0)) * std::fabs(d0) / cfg.a_lat_comfort);
    EXPECT_NEAR(T, expected_T, kResumeMergeDurationStepS)
        << "grid T=" << T << " closed-form T=" << expected_T;
    EXPECT_FALSE(comfort_unmet);
}

// --- 4c: unmet bound is reported at the duration ceiling --------------------

TEST(ResumeMergeProfileTest, ReportsUnmetBoundAtDurationCeiling)
{
    ResumeMergeConfig cfg = MakeEnabledCfg();  // shipped duration_max_s=6.0, a_lat_comfort=1.5

    // A large enough d0 (with v0_lat=a0_lat=0) that even T_max cannot bring
    // the closed-form peak under a_lat_comfort: (10/sqrt(3))*d0/T_max^2 > 1.5
    // requires d0 > T_max^2 * 1.5 / (10/sqrt(3)) ~= 9.353; 15.0 clears that.
    const double d0 = 15.0;

    bool         comfort_unmet = false;
    const double T = SelectResumeMergeDuration(d0, /*v0_lat=*/0.0, /*a0_lat=*/0.0, cfg, &comfort_unmet);

    EXPECT_TRUE(comfort_unmet);
    EXPECT_NEAR(T, cfg.duration_max_s, 1e-9);

    // The bound really is unmet at T_max -- not just a flag with no teeth.
    const double closed_form_peak = (10.0 / std::sqrt(3.0)) * d0 / (T * T);
    EXPECT_GT(closed_form_peak, cfg.a_lat_comfort);
}

// --- 4d: comfort bound is structurally infeasible below the handover accel -

TEST(ResumeMergeProfileTest, ComfortBoundIsInfeasibleBelowHandoverAccel)
{
    // Structural fact (header doc): d''(0) is pinned to a0_lat, so
    // max|d''(t)| over the whole trajectory can never go below |a0_lat|, for
    // ANY T. This is why a_bound = max(a_lat_comfort, |a0_lat|) rather than
    // a_lat_comfort alone.
    const double d0_values[]     = {1.0, 3.3, -1.0, -3.3};
    const double a0_lat_values[] = {2.0, 4.0, -2.0, -4.0};  // all exceed a_lat_comfort=1.5 in magnitude
    const double T_values[]      = {1.5, 2.0, 3.0, 4.5, 6.0};

    for (double d0 : d0_values)
        for (double a0_lat : a0_lat_values)
            for (double T : T_values)
            {
                double max_abs_accel = 0.0;
                for (int i = 0; i < kResumeMergeCurvatureSampleCount; ++i)
                {
                    const double u = static_cast<double>(i) / static_cast<double>(kResumeMergeCurvatureSampleCount - 1);
                    const double t = u * T;
                    max_abs_accel = std::max(max_abs_accel,
                                              std::fabs(EvaluateQuinticAccel(d0, /*v0_lat=*/0.24, a0_lat, T, t)));
                }
                EXPECT_GE(max_abs_accel, std::fabs(a0_lat) - 1e-9)
                    << "d0=" << d0 << " a0_lat=" << a0_lat << " T=" << T;
            }
}

// --- 5: duration clamped to the configured range ----------------------------

TEST(ResumeMergeProfileTest, DurationClampedToConfiguredRange)
{
    ResumeMergeConfig cfg = MakeEnabledCfg();
    cfg.duration_min_s = 2.0;
    cfg.duration_max_s = 5.0;
    cfg.a_lat_comfort  = 1.5;

    // Tiny offset: the closed-form T (~1.455s) is below duration_min_s, so
    // the grid search must clamp UP to exactly duration_min_s (the very
    // first grid candidate already satisfies the bound).
    {
        bool         comfort_unmet = false;
        const double T = SelectResumeMergeDuration(0.55, 0.0, 0.0, cfg, &comfort_unmet);
        EXPECT_NEAR(T, cfg.duration_min_s, 1e-9);
        EXPECT_FALSE(comfort_unmet);
    }
    // Huge offset: the closed-form T is far beyond duration_max_s, so the
    // search must clamp DOWN to exactly duration_max_s and report the bound
    // unmet.
    {
        bool         comfort_unmet = false;
        const double T = SelectResumeMergeDuration(20.0, 0.0, 0.0, cfg, &comfort_unmet);
        EXPECT_NEAR(T, cfg.duration_max_s, 1e-9);
        EXPECT_TRUE(comfort_unmet);
    }
    // Every candidate in between must also stay in range.
    for (double d0 : {0.55, 1.0, 3.3, 6.0, 20.0})
    {
        const double T = SelectResumeMergeDuration(d0, 0.1, 0.5, cfg, nullptr);
        EXPECT_GE(T, cfg.duration_min_s - 1e-9);
        EXPECT_LE(T, cfg.duration_max_s + 1e-9);
    }
}

// --- 6: merge direction opposes the offset sign, both signs of d0 ----------

TEST(ResumeMergeProfileTest, MergeDirectionOpposesOffsetSign)
{
    for (const double d0 : {3.3, -3.3})
    {
        ResumeMergeState  state;
        ResumeMergeConfig cfg = MakeEnabledCfg();
        ASSERT_TRUE(ArmResumeMerge(state, d0, /*v0_lat=*/0.0, /*a0_lat=*/0.0, cfg));

        double prev_abs_offset = std::fabs(d0);
        for (double frac : {0.1, 0.3, 0.5, 0.7, 0.9})
        {
            const double t   = frac * state.duration_s;
            const double off = EvaluateQuinticOffset(state.d0, state.v0_lat, state.a0_lat, state.duration_s, t);
            const double vel = EvaluateQuinticVelocity(state.d0, state.v0_lat, state.a0_lat, state.duration_s, t);

            // Velocity opposes d0's sign: moving TOWARD zero, never away.
            if (d0 > 0.0)
                EXPECT_LT(vel, 0.0) << "frac=" << frac << " d0=" << d0;
            else
                EXPECT_GT(vel, 0.0) << "frac=" << frac << " d0=" << d0;

            // Offset stays on the same side as d0 (never overshoots past
            // zero) and its magnitude shrinks monotonically for this
            // v0_lat=a0_lat=0 case.
            EXPECT_GE(off * d0, 0.0) << "frac=" << frac << " d0=" << d0;
            EXPECT_LE(std::fabs(off), prev_abs_offset + 1e-9) << "frac=" << frac;
            prev_abs_offset = std::fabs(off);
        }
    }
}

// --- 11: does not arm below the configured minimum offset -------------------

TEST(ResumeMergeProfileTest, DoesNotArmBelowMinOffset)
{
    ResumeMergeConfig cfg = MakeEnabledCfg();
    cfg.min_offset_m       = 0.5;

    ResumeMergeState state;
    state.active = true;  // pre-set to confirm a failed arm resets it, not just refuses to set it true

    EXPECT_FALSE(ArmResumeMerge(state, /*d0=*/0.49, 0.0, 0.0, cfg));
    EXPECT_FALSE(state.active);

    // Boundary: exactly at min_offset_m DOES arm -- the spec's gate is a
    // strict '<' ("|d0| < min_offset_m" fails to arm), so d0==min_offset_m
    // is the smallest value that arms.
    EXPECT_TRUE(ArmResumeMerge(state, /*d0=*/0.5, 0.0, 0.0, cfg));
    EXPECT_TRUE(state.active);

    // Disabled refuses regardless of offset size.
    // Set explicitly rather than leaning on the default: this assertion is
    // about the enabled=false path, and it silently stopped testing that the
    // day the shipped default flipped to true (2026-07-28).
    ResumeMergeConfig disabled_cfg;
    disabled_cfg.enabled = false;
    EXPECT_FALSE(ArmResumeMerge(state, /*d0=*/10.0, 0.0, 0.0, disabled_cfg));
    EXPECT_FALSE(state.active);
}

// --- Known-good fixtures (design doc section 8-2, independently verified) --
//
// Not part of the mandated 8-7 list by name, but pins the exact numbers the
// design doc's own grid search produced -- the strongest available
// regression guard against a future refactor silently changing the math.

TEST(ResumeMergeProfileTest, KnownGoodFixturesMatchDesignDocTable)
{
    struct Fixture
    {
        double d0;
        double v0_lat;
        double a0_lat;
        double a_comfort;
        double expected_T;
        double expected_peak_a_lat;
    };
    const Fixture fixtures[] = {
        {3.3, 0.24, 0.0, 1.5, 3.90, 1.492},
        {3.3, 0.24, 1.0, 1.5, 4.35, 1.482},
        {3.3, 0.24, 2.58, 1.5, 3.50, 2.580},
        {3.3, 0.24, -2.58, 1.5, 3.15, 2.580},
    };

    for (const auto& f : fixtures)
    {
        ResumeMergeConfig cfg = MakeEnabledCfg();
        cfg.a_lat_comfort      = f.a_comfort;

        bool         comfort_unmet = false;
        const double T = SelectResumeMergeDuration(f.d0, f.v0_lat, f.a0_lat, cfg, &comfort_unmet);
        EXPECT_NEAR(T, f.expected_T, 1e-6) << "a0_lat=" << f.a0_lat;
        EXPECT_FALSE(comfort_unmet) << "a0_lat=" << f.a0_lat;

        // Finer than the 201-point selection grid, to probe the TRUE
        // continuous peak rather than re-deriving the selection grid's own
        // (self-consistent-by-construction) answer.
        double            max_abs_accel = 0.0;
        constexpr int     kFineProbeCount = 2001;
        for (int i = 0; i < kFineProbeCount; ++i)
        {
            const double t = T * static_cast<double>(i) / static_cast<double>(kFineProbeCount - 1);
            max_abs_accel = std::max(max_abs_accel, std::fabs(EvaluateQuinticAccel(f.d0, f.v0_lat, f.a0_lat, T, t)));
        }
        // Tolerance consistent with the grid-search method (dT=0.05s), not
        // tighter than the method supports (design doc section 5-4).
        EXPECT_NEAR(max_abs_accel, f.expected_peak_a_lat, 0.01) << "a0_lat=" << f.a0_lat;
    }
}

// --- Supplementary coverage of the state machine (Advance/Disarm/inactive) -

TEST(ResumeMergeProfileTest, EvaluateResumeMergeOffsetTracksElapsedPlusLookahead)
{
    ResumeMergeState  state;
    ResumeMergeConfig cfg = MakeEnabledCfg();
    ASSERT_TRUE(ArmResumeMerge(state, /*d0=*/3.3, /*v0_lat=*/0.24, /*a0_lat=*/0.0, cfg));

    EXPECT_NEAR(EvaluateResumeMergeOffset(state, 0.0), 3.3, 1e-9);

    AdvanceResumeMerge(state, state.duration_s * 0.5);
    const double off_mid      = EvaluateResumeMergeOffset(state, 0.0);
    const double expected_mid = EvaluateQuinticOffset(3.3, 0.24, 0.0, state.duration_s, state.duration_s * 0.5);
    EXPECT_NEAR(off_mid, expected_mid, 1e-9);
    EXPECT_LT(std::fabs(off_mid), 3.3);  // moved toward zero

    // A lookahead beyond the REMAINING duration reads 0 (the merge would be
    // over by then), even though the state is still active right now.
    EXPECT_EQ(EvaluateResumeMergeOffset(state, state.duration_s), 0.0);
}

TEST(ResumeMergeProfileTest, AdvanceResumeMergeDeactivatesAtCompletion)
{
    ResumeMergeState  state;
    ResumeMergeConfig cfg = MakeEnabledCfg();
    ASSERT_TRUE(ArmResumeMerge(state, /*d0=*/3.3, 0.0, 0.0, cfg));

    AdvanceResumeMerge(state, state.duration_s + 1.0);
    EXPECT_FALSE(state.active);
    EXPECT_EQ(EvaluateResumeMergeOffset(state, 0.0), 0.0);
}

TEST(ResumeMergeProfileTest, AdvanceResumeMergeIgnoresNegativeDt)
{
    ResumeMergeState  state;
    ResumeMergeConfig cfg = MakeEnabledCfg();
    ASSERT_TRUE(ArmResumeMerge(state, /*d0=*/3.3, 0.0, 0.0, cfg));

    AdvanceResumeMerge(state, -1.0);
    EXPECT_TRUE(state.active);
    EXPECT_EQ(state.elapsed_s, 0.0);
}

TEST(ResumeMergeProfileTest, DisarmResumeMergeImmediatelyZeroesOffset)
{
    ResumeMergeState  state;
    ResumeMergeConfig cfg = MakeEnabledCfg();
    ASSERT_TRUE(ArmResumeMerge(state, /*d0=*/3.3, 0.24, 0.0, cfg));
    ASSERT_NE(EvaluateResumeMergeOffset(state, 0.0), 0.0);

    DisarmResumeMerge(state);
    EXPECT_FALSE(state.active);
    EXPECT_EQ(EvaluateResumeMergeOffset(state, 0.0), 0.0);
}

TEST(ResumeMergeProfileTest, InactiveStateAlwaysEvaluatesToZero)
{
    ResumeMergeState state;  // default: active=false
    EXPECT_EQ(EvaluateResumeMergeOffset(state, 0.0), 0.0);
    EXPECT_EQ(EvaluateResumeMergeOffset(state, 5.0), 0.0);
    EXPECT_EQ(EvaluateResumeMergeOffset(state, -1.0), 0.0);
}

}  // namespace gt_esmini
