#include "gt_esmini/control/virtualdriver/ManeuverAwareSpeedPlanner.hpp"

#include "Entities.hpp"
#include "RoadManager.hpp"
#include "CommonMini.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <vector>

using namespace scenarioengine;

namespace gt_esmini
{

namespace
{
// Sentinel for "no constraint here" — large enough that min() with any real
// commanded/limit speed always picks the real value, small enough that the
// backward-pass sqrt(v^2 + 2*a*ds) never overflows.
constexpr double kUnconstrained = 1.0e6;

// A curve only earns a marker if it actually slows the car meaningfully; a gentle
// highway radius (v_curve well above free-flow) is not a "constraint" worth a pin.
constexpr double kCurveNotable = 30.0;  // [m/s] (~108 km/h)

// Per-sample scratch: the ceiling and what caused it, plus world XY for markers.
struct ScanSample
{
    double      s_ahead = 0.0;
    double      x       = 0.0;
    double      y       = 0.0;
    double      v       = 0.0;       // ceiling at this sample (post-clamp)
    const char* kind    = nullptr;   // binding cause: "curve"|"junction"|"speed_limit" or null
};
}  // namespace

MidLongPlannerSnapshot ManeuverAwareSpeedPlanner::Plan(const MidLongContext& ctx)
{
    MidLongPlannerSnapshot snap;

    Object* obj = ctx.object;
    if (!obj) return snap;

    roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();
    if (!odr) return snap;

    const double step      = std::max(0.5, cfg_.scan_step);
    const double scan_dist = std::max(step, ctx.scan_dist);

    // Walk a copy of the ego route forward, isolated from the shared object
    // state (same pattern as TrajectoryShortPlanner / DetectJunctionTurn).
    roadmanager::Position pos;
    pos.Duplicate(obj->pos_);
    pos.CopyRoute(obj->pos_);

    std::vector<ScanSample> samples;
    double s_ahead   = 0.0;
    double prev_limit = -1.0;

    // Per-connecting-road turn/straight verdict, decided once and reused for every
    // sample that lands on the same road id (the scan resolution puts many samples
    // on one connector). True = the connector actually turns; straight ones are
    // left to the curvature limit so the ego does not brake crossing a junction it
    // drives straight through.
    std::unordered_map<id_t, bool> connector_is_turn;

    while (s_ahead <= scan_dist)
    {
        // Curvature ceiling: v = sqrt(a_lat / |kappa|). Straights -> kappa ~ 0
        // -> unconstrained. Junction connecting roads are sharp arcs -> low v.
        const double kappa = pos.GetCurvature();
        double v_curve = kUnconstrained;
        if (std::fabs(kappa) > 1.0e-4)
            v_curve = std::sqrt(cfg_.max_lateral_accel / std::fabs(kappa));

        // Speed-limit ceiling (esmini returns a sane heuristic if unauthored).
        const double v_lim = cfg_.respect_speed_limit ? pos.GetSpeedLimit() : kUnconstrained;

        // Junction connecting road: belt-and-suspenders cap for gently-modelled
        // junctions whose geometry curvature alone would not slow the ego enough.
        // Apply it ONLY when the connector actually turns; a straight pass-through
        // connecting road (kappa ~ 0) must keep flowing or the ego brakes for
        // nothing crossing a junction it drives straight through. Turn-vs-straight
        // is the connector's heading delta over its length (>= SHARP_TURN_RATE),
        // mirroring ControllerRouteDrive's connector test, cached per road id.
        roadmanager::Road* road = odr->GetRoadById(pos.GetTrackId());
        bool on_junction = false;
        if (road && road->GetJunction() != ID_UNDEFINED)
        {
            const id_t rid = road->GetId();
            auto       it  = connector_is_turn.find(rid);
            if (it != connector_is_turn.end())
            {
                on_junction = it->second;
            }
            else
            {
                bool is_turn = false;
                if (road->GetLength() > 0.1)
                {
                    roadmanager::Position p0;
                    roadmanager::Position pE;
                    p0.SetTrackPos(rid, 0.0, 0.0);
                    pE.SetTrackPos(rid, road->GetLength(), 0.0);
                    const double dh       = std::fabs(GetAngleInIntervalMinusPIPlusPI(pE.GetH() - p0.GetH()));
                    const double turnRate = dh / road->GetLength();
                    constexpr double SHARP_TURN_RATE = 0.04;  // rad/m (~2.3°/m)
                    is_turn = (turnRate >= SHARP_TURN_RATE);
                }
                connector_is_turn[rid] = is_turn;
                on_junction            = is_turn;
            }
        }
        const double v_turn = on_junction ? cfg_.turn_speed : kUnconstrained;

        double v_ceil = std::clamp(std::min({v_curve, v_lim, v_turn}), cfg_.min_speed, kUnconstrained);

        // Classify the binding cause (junction takes precedence over its own arc
        // curvature; a speed-limit marker is only meaningful at a decrease).
        const char* kind = nullptr;
        if (on_junction)
            kind = "junction";
        else if (v_curve <= v_lim && v_curve < kCurveNotable)
            kind = "curve";
        else if (prev_limit > 0.0 && v_lim < prev_limit - 1.0e-3)
            kind = "speed_limit";

        samples.push_back({s_ahead, pos.GetX(), pos.GetY(), v_ceil, kind});
        prev_limit = v_lim;

        const int ret = static_cast<int>(pos.MoveAlongS(step));
        if (ret < 0) break;  // end of route/road or off-route — stop scanning
        s_ahead += step;
    }

    // Phase 3: fold traffic-policy constraints into the ceiling. Each policy
    // (lead-vehicle / traffic-light / stop-yield) emits PolicyConstraints; the
    // strictest wins because every constraint only ever LOWERS the ceiling.
    //   MAX_SPEED        : cap every sample.
    //   MAX_SPEED_TO_S   : cap samples up to the constraint s (e.g. a creep zone).
    //   STOP_AT_S        : write a hard 0 from the stop point onward, AND ramp the
    //                      approach down to 0 under comfort_decel — both BYPASS the
    //                      forward-scan min_speed clamp. That clamp exists to keep
    //                      the car creeping through curves/junctions, but it must
    //                      not pin the approach to a stop at min_speed; without the
    //                      explicit ramp the car arrives at the stop point still
    //                      doing ~min_speed and crawls through (the backward pass
    //                      cannot help — it only min()s against the floored value).
    // (YIELD/WAIT_UNTIL are translated to the above by the policies themselves.)
    std::vector<MidLongConstraint> policy_markers;
    if (ctx.policy && ctx.policy->valid && !samples.empty())
    {
        for (const auto& c : ctx.policy->constraints)
        {
            const double cap = std::max(0.0, c.value);
            int          marker_idx = -1;  // sample to anchor a "stop" marker on
            switch (c.kind)
            {
                case PolicyConstraint::Kind::MAX_SPEED:
                    for (auto& s : samples) s.v = std::min(s.v, cap);
                    break;
                case PolicyConstraint::Kind::MAX_SPEED_TO_S:
                    for (auto& s : samples)
                        if (s.s_ahead <= c.s) s.v = std::min(s.v, cap);
                    break;
                case PolicyConstraint::Kind::STOP_AT_S:
                {
                    // Command a hard 0 from a band BEFORE the stop point onward, so
                    // the car brakes fully and settles at a firm standstill (the
                    // sqrt ramp alone only reaches 0 exactly at the point -> slow
                    // crawl). The remaining approach ramps to 0 at that band edge,
                    // bypassing the min_speed floor.
                    const double zero_from = std::max(0.0, c.s - std::max(0.0, cfg_.stop_band));
                    for (size_t i = 0; i < samples.size(); ++i)
                    {
                        if (samples[i].s_ahead >= zero_from)
                        {
                            samples[i].v = 0.0;
                            if (marker_idx < 0) marker_idx = static_cast<int>(i);
                        }
                        else
                        {
                            const double ramp = std::sqrt(2.0 * cfg_.comfort_decel * (zero_from - samples[i].s_ahead));
                            samples[i].v = std::min(samples[i].v, ramp);
                        }
                    }
                    // Stop beyond the scan horizon: still mark the last sample so
                    // the approach decelerates toward it.
                    if (marker_idx < 0 && zero_from > samples.back().s_ahead)
                        marker_idx = static_cast<int>(samples.size()) - 1;
                    break;
                }
                default:
                    break;
            }
            if (marker_idx >= 0)
            {
                const ScanSample& s = samples[static_cast<size_t>(marker_idx)];
                policy_markers.push_back({s.s_ahead, s.x, s.y, 0.0, "stop"});
            }
        }
    }

    // Backward comfort-deceleration pass: ensure each point is reachable from the
    // next under comfort_decel, so a low ceiling ahead propagates back and braking
    // starts early (anticipatory) instead of as a late hard stop.
    for (int i = static_cast<int>(samples.size()) - 2; i >= 0; --i)
    {
        const double ds          = samples[i + 1].s_ahead - samples[i].s_ahead;
        const double v_reachable = std::sqrt(samples[i + 1].v * samples[i + 1].v +
                                             2.0 * cfg_.comfort_decel * ds);
        samples[i].v = std::min(samples[i].v, v_reachable);
    }

    // Jerk-limited smoothing: a constant-deceleration ramp steps the driver's
    // speed reference at the scan resolution (the constraint distance is quantized
    // to the grid), and the speed PID turns each step into a brake pulse -> high
    // jerk. Round the deceleration shoulders with a centered moving average, then
    // take min() with the un-smoothed profile so the smoothing only ever LOWERS
    // the reference (the safety ceiling at the constraint floor is preserved, and
    // the onset spreads out -> bounded jerk). Window spans the jerk-limited accel
    // ramp time (comfort_decel/comfort_jerk seconds) at the local speed.
    if (cfg_.comfort_jerk > 1.0e-3 && samples.size() >= 3)
    {
        double v_ref = cfg_.min_speed;
        for (const auto& s : samples) v_ref = std::max(v_ref, std::min(s.v, 40.0));
        const double ramp_dist = cfg_.comfort_decel / cfg_.comfort_jerk * v_ref;  // [m]
        const int    half = std::clamp(static_cast<int>(std::ceil(ramp_dist / step / 2.0)), 1, 15);

        std::vector<double> smoothed(samples.size());
        for (int i = 0; i < static_cast<int>(samples.size()); ++i)
        {
            const int lo = std::max(0, i - half);
            const int hi = std::min(static_cast<int>(samples.size()) - 1, i + half);
            double sum = 0.0;
            for (int k = lo; k <= hi; ++k) sum += samples[static_cast<size_t>(k)].v;
            smoothed[static_cast<size_t>(i)] = sum / (hi - lo + 1);
        }
        for (size_t i = 0; i < samples.size(); ++i)
            samples[i].v = std::min(samples[i].v, smoothed[i]);
    }

    // Emit profile + one labelled constraint per contiguous constrained segment,
    // anchored at the segment's slowest sample (the binding point) with world XY.
    auto& prof = snap.v_target_profile;
    prof.reserve(samples.size());
    int seg_min = -1;  // index of slowest sample in the current segment, or -1
    auto flush_segment = [&]() {
        if (seg_min < 0) return;
        const ScanSample& s = samples[static_cast<size_t>(seg_min)];
        snap.constraints.push_back({s.s_ahead, s.x, s.y, s.v, std::string(s.kind)});
        seg_min = -1;
    };
    for (size_t i = 0; i < samples.size(); ++i)
    {
        const ScanSample& s = samples[i];
        prof.emplace_back(s.s_ahead, s.v);

        if (s.kind == nullptr)
        {
            flush_segment();
        }
        else if (seg_min < 0 || std::strcmp(s.kind, samples[static_cast<size_t>(seg_min)].kind) != 0)
        {
            flush_segment();   // different kind -> close previous, start new
            seg_min = static_cast<int>(i);
        }
        else if (s.v < samples[static_cast<size_t>(seg_min)].v)
        {
            seg_min = static_cast<int>(i);  // deeper minimum within same-kind run
        }
    }
    flush_segment();

    // Append policy "stop" markers (Phase 3) so the viewer can pin the stop point.
    for (auto& m : policy_markers)
        snap.constraints.push_back(m);

    snap.valid = prof.size() >= 2;
    return snap;
}

}  // namespace gt_esmini
