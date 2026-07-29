#include "gt_esmini/control/virtualdriver/ResumeMergeProfile.hpp"

#include <algorithm>
#include <cmath>

namespace gt_esmini
{

namespace
{

// Coefficients of the design doc section 8-2 closed-form quintic in
// normalized time u = t/T. a/b/c are the three terms fixed by the INITIAL
// (t=0) boundary conditions (a=d0, b=v0_lat*T, c=a0_lat*T^2); p3/p4/p5 are
// the three terms solved for by the TERMINAL (t=T) boundary conditions
// d(T)=d'(T)=d''(T)=0.
struct QuinticCoeffs
{
    double a  = 0.0;
    double b  = 0.0;
    double c  = 0.0;
    double p3 = 0.0;
    double p4 = 0.0;
    double p5 = 0.0;
};

QuinticCoeffs ComputeQuinticCoeffs(double d0, double v0_lat, double a0_lat, double T)
{
    QuinticCoeffs qc;
    qc.a  = d0;
    qc.b  = v0_lat * T;
    qc.c  = a0_lat * T * T;
    qc.p3 = -10.0 * qc.a - 6.0 * qc.b - 1.5 * qc.c;
    qc.p4 =  15.0 * qc.a + 8.0 * qc.b + 1.5 * qc.c;
    qc.p5 =  -6.0 * qc.a - 3.0 * qc.b - 0.5 * qc.c;
    return qc;
}

double EvalPositionU(const QuinticCoeffs& qc, double u)
{
    const double u2 = u * u;
    const double u3 = u2 * u;
    const double u4 = u3 * u;
    const double u5 = u4 * u;
    return qc.a + qc.b * u + 0.5 * qc.c * u2 + qc.p3 * u3 + qc.p4 * u4 + qc.p5 * u5;
}

double EvalVelocityU(const QuinticCoeffs& qc, double u)
{
    const double u2 = u * u;
    const double u3 = u2 * u;
    const double u4 = u3 * u;
    return qc.b + qc.c * u + 3.0 * qc.p3 * u2 + 4.0 * qc.p4 * u3 + 5.0 * qc.p5 * u4;
}

double EvalAccelU(const QuinticCoeffs& qc, double u)
{
    const double u2 = u * u;
    const double u3 = u2 * u;
    return qc.c + 6.0 * qc.p3 * u + 12.0 * qc.p4 * u2 + 20.0 * qc.p5 * u3;
}

// Max |d''(u)| over the fixed sample grid (design doc section 8-2). u=0 is
// always the first grid point, and d''(0)=c=a0_lat*T^2 exactly -- which is
// exactly why max|d''| >= |a0_lat| always holds after dividing by T^2 (see
// header doc's STRUCTURAL FACT): the search can never "miss" this floor by
// under-sampling, because it is a grid point by construction, not something
// that could fall between samples.
double MaxAbsAccelOverUGrid(const QuinticCoeffs& qc)
{
    double max_abs = 0.0;
    for (int i = 0; i < kResumeMergeCurvatureSampleCount; ++i)
    {
        const double u = static_cast<double>(i) / static_cast<double>(kResumeMergeCurvatureSampleCount - 1);
        max_abs = std::max(max_abs, std::fabs(EvalAccelU(qc, u)));
    }
    return max_abs;
}

}  // namespace

double EvaluateQuinticOffset(double d0, double v0_lat, double a0_lat, double duration_s, double t)
{
    const double T = std::max(duration_s, 1.0e-9);  // divide-by-zero guard only, not a duration policy
    const QuinticCoeffs qc = ComputeQuinticCoeffs(d0, v0_lat, a0_lat, T);
    return EvalPositionU(qc, t / T);
}

double EvaluateQuinticVelocity(double d0, double v0_lat, double a0_lat, double duration_s, double t)
{
    const double T = std::max(duration_s, 1.0e-9);
    const QuinticCoeffs qc = ComputeQuinticCoeffs(d0, v0_lat, a0_lat, T);
    return EvalVelocityU(qc, t / T) / T;  // chain rule: du/dt = 1/T
}

double EvaluateQuinticAccel(double d0, double v0_lat, double a0_lat, double duration_s, double t)
{
    const double T = std::max(duration_s, 1.0e-9);
    const QuinticCoeffs qc = ComputeQuinticCoeffs(d0, v0_lat, a0_lat, T);
    return EvalAccelU(qc, t / T) / (T * T);  // chain rule: d''(t) = d''(u)/T^2
}

double SelectResumeMergeDuration(double                   d0,
                                 double                   v0_lat,
                                 double                   a0_lat,
                                 const ResumeMergeConfig& cfg,
                                 bool*                    out_comfort_unmet)
{
    // Never make it worse than what the driver handed over; if they handed
    // over inside the comfort band, stay inside it (see header doc's
    // STRUCTURAL FACT for why a_lat_comfort ALONE is infeasible whenever
    // |a0_lat| exceeds it).
    const double a_bound = std::max(cfg.a_lat_comfort, std::fabs(a0_lat));

    // Guard against a misconfigured (<=0, or min>max) range the same
    // defensive way AdSteeringEnvelope guards its own inputs -- this must
    // never divide by a non-positive T below, regardless of what the config
    // says.
    const double t_min = std::max(cfg.duration_min_s, 1.0e-3);
    const double t_max = std::max(cfg.duration_max_s, t_min);

    // Integer-indexed grid (not a float accumulator) so the candidate set is
    // exactly reproducible regardless of floating-point step-accumulation
    // drift -- deterministic per design doc section 8-2 ("反復の収束性に依存せず
    // 格子上で決まるのでユニットで厳密に固定できる").
    const int n_steps = static_cast<int>(std::floor((t_max - t_min) / kResumeMergeDurationStepS + 0.5));

    for (int i = 0; i <= n_steps; ++i)
    {
        double T = t_min + static_cast<double>(i) * kResumeMergeDurationStepS;
        if (T > t_max) T = t_max;

        const QuinticCoeffs qc        = ComputeQuinticCoeffs(d0, v0_lat, a0_lat, T);
        const double        max_abs_u = MaxAbsAccelOverUGrid(qc);
        const double        max_abs_t = max_abs_u / (T * T);

        // Epsilon-tolerant acceptance, NOT a tightened bound: whenever
        // |a0_lat| >= a_lat_comfort, a_bound == |a0_lat|, and d''(0) is
        // PINNED to a0_lat by construction (header doc's STRUCTURAL FACT,
        // same fact ComfortBoundIsInfeasibleBelowHandoverAccel pins) --
        // meaning MaxAbsAccelOverUGrid always samples u=0 and therefore
        // max_abs_t >= |a0_lat| == a_bound EXACTLY, not just approximately.
        // That candidate can only ever pass via floating-point equality, and
        // ComputeQuinticCoeffs's chain of multiplies/divides (T*T scaling
        // in, /(T*T) scaling back out) does not round-trip bit-exact, so the
        // "exact" equality case is decided by a few ULPs of accumulated
        // rounding noise rather than by the actual math. Without this
        // epsilon, that noise silently costs the search one extra grid step
        // (kResumeMergeDurationStepS) in EVERY a0-dominated case -- i.e. the
        // merge duration is systematically longer than the design calls for
        // precisely when the driver hands over with a lot of yaw. The
        // tolerance is relative (1e-9, matches double's ~1e-16 precision
        // with many orders of margin for the polynomial's multiply-chain
        // error growth) plus a tiny absolute floor (1e-12) for the a_bound
        // -> 0 edge; it accepts only genuine floating-point noise around the
        // structural equality, not a materially looser bound.
        if (max_abs_t <= a_bound * (1.0 + 1e-9) + 1e-12)
        {
            if (out_comfort_unmet) *out_comfort_unmet = false;
            return T;
        }
    }

    // No candidate in [T_min, T_max] satisfies a_bound -- ship the longest
    // allowed duration and say so, rather than silently returning a
    // trajectory that exceeds the bound (design doc section 5-4 discipline:
    // never silently give up).
    if (out_comfort_unmet) *out_comfort_unmet = true;
    return t_max;
}

bool ArmResumeMerge(ResumeMergeState& state, double d0, double v0_lat, double a0_lat, const ResumeMergeConfig& cfg)
{
    // Always reset first: a failed arm attempt below must never leave a
    // stale PREVIOUS arm's state looking active.
    state = ResumeMergeState{};

    if (!cfg.enabled) return false;
    if (std::fabs(d0) < cfg.min_offset_m) return false;

    bool comfort_unmet = false;
    const double duration_s = SelectResumeMergeDuration(d0, v0_lat, a0_lat, cfg, &comfort_unmet);

    state.active        = true;
    state.d0            = d0;
    state.v0_lat        = v0_lat;
    state.a0_lat        = a0_lat;
    state.duration_s    = duration_s;
    state.elapsed_s     = 0.0;
    state.a_bound       = std::max(cfg.a_lat_comfort, std::fabs(a0_lat));
    state.comfort_unmet = comfort_unmet;
    return true;
}

double EvaluateResumeMergeOffset(const ResumeMergeState& state, double t_ahead_s)
{
    if (!state.active) return 0.0;

    const double t = state.elapsed_s + t_ahead_s;
    if (t >= state.duration_s) return 0.0;  // merge complete: no target offset remains (see header doc)

    const double t_clamped = std::max(t, 0.0);  // guard a query before arm time; not a domain callers are expected to use
    return EvaluateQuinticOffset(state.d0, state.v0_lat, state.a0_lat, state.duration_s, t_clamped);
}

void AdvanceResumeMerge(ResumeMergeState& state, double dt)
{
    if (!state.active) return;

    state.elapsed_s += std::max(dt, 0.0);
    if (state.elapsed_s >= state.duration_s) state.active = false;
}

void DisarmResumeMerge(ResumeMergeState& state)
{
    state.active = false;
}

}  // namespace gt_esmini
