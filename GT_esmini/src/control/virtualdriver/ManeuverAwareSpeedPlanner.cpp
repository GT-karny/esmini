#include "gt_esmini/control/virtualdriver/ManeuverAwareSpeedPlanner.hpp"
#include "gt_esmini/control/common/JunctionTurn.hpp"
#include "gt_esmini/road/OdrSideModel.hpp"

#include "Entities.hpp"
#include "RoadManager.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

using namespace scenarioengine;

namespace gt_esmini
{

namespace
{
constexpr double kUnconstrained = 1.0e6;
constexpr double kCurveNotable = 30.0;  // [m/s] (~108 km/h)

std::string BindingKind(bool on_junction, double v_curve, double v_limit, double previous_limit)
{
    if (on_junction) return "junction";
    if (v_curve <= v_limit && v_curve < kCurveNotable) return "curve";
    if (previous_limit > 0.0 && v_limit < previous_limit - 1.0e-3) return "speed_limit";
    return {};
}

bool IsTurningConnector(roadmanager::Road* road, std::unordered_map<id_t, bool>& connector_cache)
{
    if (!road || road->GetJunction() == ID_UNDEFINED) return false;

    const id_t road_id = road->GetId();
    const auto cached = connector_cache.find(road_id);
    if (cached != connector_cache.end()) return cached->second;

    const bool is_turn = IsSharpJunctionConnector(road);
    connector_cache[road_id] = is_turn;
    return is_turn;
}

std::vector<MidLongScanSample> ScanRouteCeilings(const ManeuverAwareSpeedPlannerConfig& cfg,
                                          Object& obj,
                                          roadmanager::OpenDrive& odr,
                                          double step,
                                          double scan_dist)
{
    roadmanager::Position pos;
    pos.Duplicate(obj.pos_);
    pos.CopyRoute(obj.pos_);

    std::vector<MidLongScanSample> samples;
    std::unordered_map<id_t, bool> connector_is_turn;
    double s_ahead = 0.0;
    double previous_limit = -1.0;

    while (s_ahead <= scan_dist)
    {
        const double kappa = pos.GetCurvature();
        double v_curve = kUnconstrained;
        if (std::fabs(kappa) > 1.0e-4)
        {
            v_curve = std::sqrt(cfg.max_lateral_accel / std::fabs(kappa));
        }

        double v_limit = cfg.respect_speed_limit ? pos.GetSpeedLimit() : kUnconstrained;
        if (cfg.respect_speed_limit)
        {
            // ODR P2 (cluster 16 L2): fold the lane <speed> limit into the ceiling. Returns <= 0
            // when no lane speed record applies (all legacy assets) -> v_limit stays bit-identical.
            const double v_lane = gt_esmini::odr::GetLaneSpeedLimitForPosition(pos);
            if (v_lane > 0.0)
            {
                v_limit = std::min(v_limit, v_lane);
            }
        }
        roadmanager::Road* road = odr.GetRoadById(pos.GetTrackId());
        const bool on_junction = IsTurningConnector(road, connector_is_turn);
        const double v_turn = on_junction ? cfg.turn_speed : kUnconstrained;
        const double v_ceiling = std::clamp(std::min({v_curve, v_limit, v_turn}), cfg.min_speed, kUnconstrained);

        samples.push_back({s_ahead,
                           pos.GetX(),
                           pos.GetY(),
                           v_ceiling,
                           BindingKind(on_junction, v_curve, v_limit, previous_limit)});
        previous_limit = v_limit;

        // [Issue #31] straight-most (0.0), not the -1.0 convenience overload that randomizes
        // the connecting road when the prediction is off-route -> the junction speed scan would
        // flicker between the straight connector and a turn connector (spurious turn/curve decel).
        // A valid on-route route still wins in MoveToConnectingRoad. See RouteCrosswalkScan.
        const int ret = static_cast<int>(pos.MoveAlongS(step, 0.0, 0.0, true,
                                                        roadmanager::Position::MoveDirectionMode::HEADING_DIRECTION, true));
        if (ret < 0) break;
        s_ahead += step;
    }

    return samples;
}

}  // namespace

// vd_intent_layer.md section 5. Public (header-declared) so the attribution rule
// is unit-testable with synthetic samples; see the header for why that rule needs
// its own test rather than being inferred from the resulting profile.
PolicyFoldResult ApplyPolicyConstraints(std::vector<MidLongScanSample>&        samples,
                                        const ManeuverAwareSpeedPlannerConfig& cfg,
                                        const TrafficPolicySnapshot*           policy)
{
    PolicyFoldResult fold;
    if (!policy || !policy->valid || samples.empty()) return fold;

    // The ego's own sample -- the one whose speed is being commanded RIGHT NOW.
    // ScanRouteCeilings always emits s_ahead == 0 first, but this reads index 0
    // by way of "smallest s_ahead" rather than assuming it, so a future change to
    // the scan cannot silently re-point the attribution at a sample 2 m ahead.
    size_t ego_idx = 0;
    for (size_t i = 1; i < samples.size(); ++i)
    {
        if (samples[i].s_ahead < samples[ego_idx].s_ahead) ego_idx = i;
    }

    for (size_t ci = 0; ci < policy->constraints.size(); ++ci)
    {
        const auto&  constraint = policy->constraints[ci];
        const double cap        = std::max(0.0, constraint.value);
        int          marker_idx = -1;
        // Snapshot the ego sample's speed before this constraint touches it. A
        // constraint is credited only if it STRICTLY lowers that value: a
        // constraint that merely matches the speed something else already set
        // did not decide anything, and crediting it would report the wrong
        // subject on every frame two policies happen to agree.
        const double v_at_ego_before = samples[ego_idx].v;

        switch (constraint.kind)
        {
        case PolicyConstraint::Kind::MAX_SPEED:
            for (auto& sample : samples) sample.v = std::min(sample.v, cap);
            break;
        case PolicyConstraint::Kind::MAX_SPEED_TO_S:
            for (auto& sample : samples)
            {
                if (sample.s_ahead <= constraint.s) sample.v = std::min(sample.v, cap);
            }
            break;
        case PolicyConstraint::Kind::STOP_AT_S:
        {
            const double zero_from = std::max(0.0, constraint.s - std::max(0.0, cfg.stop_band));
            for (size_t i = 0; i < samples.size(); ++i)
            {
                if (samples[i].s_ahead >= zero_from)
                {
                    samples[i].v = 0.0;
                    if (marker_idx < 0) marker_idx = static_cast<int>(i);
                }
                else
                {
                    // AEB phase 1: a SAFETY-tier constraint (AebSafety) shapes its
                    // approach at emergency_decel instead of comfort_decel — the
                    // whole point of the tier is to brake harder than comfort
                    // policies when a collision-course gate has fired. Every
                    // pre-existing emitter defaults to COMFORT (bit-identical).
                    const double decel = (constraint.tier == PolicyConstraint::Tier::SAFETY) ? cfg.emergency_decel
                                                                                              : cfg.comfort_decel;
                    const double ramp = std::sqrt(2.0 * decel * (zero_from - samples[i].s_ahead));
                    samples[i].v = std::min(samples[i].v, ramp);
                }
            }
            if (marker_idx < 0 && zero_from > samples.back().s_ahead)
            {
                marker_idx = static_cast<int>(samples.size()) - 1;
            }
            break;
        }
        default:
            break;
        }

        if (samples[ego_idx].v < v_at_ego_before - 1.0e-9)
        {
            fold.binding_constraint_index = static_cast<int>(ci);
        }

        if (marker_idx >= 0)
        {
            const MidLongScanSample& sample = samples[static_cast<size_t>(marker_idx)];
            fold.markers.push_back({sample.s_ahead, sample.x, sample.y, 0.0, "stop"});
        }
    }

    return fold;
}

namespace
{

void ApplyComfortDecelPass(std::vector<MidLongScanSample>& samples, const ManeuverAwareSpeedPlannerConfig& cfg)
{
    for (int i = static_cast<int>(samples.size()) - 2; i >= 0; --i)
    {
        const double ds = samples[i + 1].s_ahead - samples[i].s_ahead;
        const double reachable = std::sqrt(samples[i + 1].v * samples[i + 1].v + 2.0 * cfg.comfort_decel * ds);
        samples[i].v = std::min(samples[i].v, reachable);
    }
}

void ApplyJerkSmoothing(std::vector<MidLongScanSample>& samples, const ManeuverAwareSpeedPlannerConfig& cfg, double step)
{
    if (cfg.comfort_jerk <= 1.0e-3 || samples.size() < 3) return;

    double v_ref = cfg.min_speed;
    for (const auto& sample : samples)
    {
        v_ref = std::max(v_ref, std::min(sample.v, 40.0));
    }

    const double ramp_dist = cfg.comfort_decel / cfg.comfort_jerk * v_ref;
    const int half = std::clamp(static_cast<int>(std::ceil(ramp_dist / step / 2.0)), 1, 15);

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
    {
        samples[i].v = std::min(samples[i].v, smoothed[i]);
    }
}

void EmitSnapshot(const std::vector<MidLongScanSample>& samples,
                  const std::vector<MidLongConstraint>& policy_markers,
                  MidLongPlannerSnapshot& snap)
{
    auto& profile = snap.v_target_profile;
    profile.reserve(samples.size());

    int segment_min = -1;
    auto flush_segment = [&]() {
        if (segment_min < 0) return;
        const MidLongScanSample& sample = samples[static_cast<size_t>(segment_min)];
        snap.constraints.push_back({sample.s_ahead, sample.x, sample.y, sample.v, sample.kind});
        segment_min = -1;
    };

    for (size_t i = 0; i < samples.size(); ++i)
    {
        const MidLongScanSample& sample = samples[i];
        profile.emplace_back(sample.s_ahead, sample.v);

        if (sample.kind.empty())
        {
            flush_segment();
        }
        else if (segment_min < 0 || sample.kind != samples[static_cast<size_t>(segment_min)].kind)
        {
            flush_segment();
            segment_min = static_cast<int>(i);
        }
        else if (sample.v < samples[static_cast<size_t>(segment_min)].v)
        {
            segment_min = static_cast<int>(i);
        }
    }
    flush_segment();

    for (const auto& marker : policy_markers)
    {
        snap.constraints.push_back(marker);
    }

    snap.valid = profile.size() >= 2;
}
}  // namespace

MidLongPlannerSnapshot ManeuverAwareSpeedPlanner::Plan(const MidLongContext& ctx)
{
    MidLongPlannerSnapshot snap;

    Object* obj = ctx.object;
    if (!obj) return snap;

    roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();
    if (!odr) return snap;

    const double step = std::max(0.5, cfg_.scan_step);
    const double scan_dist = std::max(step, ctx.scan_dist);

    std::vector<MidLongScanSample> samples = ScanRouteCeilings(cfg_, *obj, *odr, step, scan_dist);
    const PolicyFoldResult         fold    = ApplyPolicyConstraints(samples, cfg_, ctx.policy);
    ApplyComfortDecelPass(samples, cfg_);
    ApplyJerkSmoothing(samples, cfg_, step);
    EmitSnapshot(samples, fold.markers, snap);
    // Recorded during the fold, NOT re-derived from the finished profile: after
    // the comfort/jerk passes have shaped the numbers, "who lowered this" is no
    // longer recoverable from the profile alone.
    snap.binding_constraint_index = fold.binding_constraint_index;

    return snap;
}

}  // namespace gt_esmini
