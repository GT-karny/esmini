#include "gt_esmini/control/virtualdriver/policies/TrafficLightAware.hpp"
#include "gt_esmini/control/virtualdriver/PolicyDetail.hpp"
#include "gt_esmini/control/virtualdriver/policies/RouteSignalScan.hpp"

#include "Entities.hpp"
#include "RoadManager.hpp"

#include <algorithm>
#include <cmath>

using namespace scenarioengine;

namespace gt_esmini
{

bool TrafficLightShouldStop(TrafficLightPhase phase, double dist, double v_ego, const TrafficLightParams& p)
{
    switch (phase)
    {
        case TrafficLightPhase::RED:
            return true;
        case TrafficLightPhase::YELLOW:
        {
            // Stop on yellow only if the light can still be reached and stopped at
            // within yellow_decel; if we are already too close, proceed through.
            const double decel   = std::max(0.5, p.yellow_decel);
            const double brake_d = (v_ego * v_ego) / (2.0 * decel);
            return dist >= brake_d;
        }
        case TrafficLightPhase::GREEN:
        case TrafficLightPhase::UNKNOWN:
        default:
            return false;
    }
}

bool IsVehicleLampIcon(int lamp_icon)
{
    switch (lamp_icon)
    {
        case roadmanager::LampIcon::ICON_PEDESTRIAN:
        case roadmanager::LampIcon::ICON_WALK:
        case roadmanager::LampIcon::ICON_DONT_WALK:
        case roadmanager::LampIcon::ICON_BICYCLE:
        case roadmanager::LampIcon::ICON_PEDESTRIAN_AND_BICYCLE:
        case roadmanager::LampIcon::ICON_TRAM:
        case roadmanager::LampIcon::ICON_BUS:
        case roadmanager::LampIcon::ICON_BUS_AND_TRAM: return false;
        default:                                      return true;  // blank head, arrows, countdown, unknown
    }
}

bool IsVehicleTrafficLightHead(const std::vector<int>& lamp_icons)
{
    return std::any_of(lamp_icons.begin(), lamp_icons.end(), [](int icon) { return IsVehicleLampIcon(icon); });
}

namespace
{
// Current phase of a traffic light: scan its lamps for an active one (CONSTANT or
// FLASHING) and map its colour. Picks the most restrictive if several are on.
// Mirrors the read path of GT_OSIReporter_Traffic::AddTrafficLightToGt.
TrafficLightPhase ReadPhase(roadmanager::TrafficLight* tl)
{
    bool red = false, yellow = false, green = false;
    const size_t n = tl->GetNrLamps();
    for (size_t i = 0; i < n; ++i)
    {
        roadmanager::TrafficLight::Lamp* lamp = tl->GetLamp(i);
        if (!lamp || lamp->IsBroken()) continue;
        const roadmanager::Signal::LampMode mode = lamp->GetMode();
        if (mode != roadmanager::Signal::LampMode::MODE_CONSTANT &&
            mode != roadmanager::Signal::LampMode::MODE_FLASHING)
            continue;
        switch (lamp->GetColor())
        {
            case roadmanager::COLOR_RED:    red = true; break;
            case roadmanager::COLOR_YELLOW: yellow = true; break;
            case roadmanager::COLOR_GREEN:  green = true; break;
            default: break;
        }
    }
    if (red)    return TrafficLightPhase::RED;
    if (yellow) return TrafficLightPhase::YELLOW;
    if (green)  return TrafficLightPhase::GREEN;
    return TrafficLightPhase::UNKNOWN;
}
}  // namespace

namespace
{
// Lamp icons of a head, in the form IsVehicleTrafficLightHead expects.
std::vector<int> LampIcons(roadmanager::TrafficLight* tl)
{
    std::vector<int> icons;
    const size_t     nr_lamps = tl->GetNrLamps();
    icons.reserve(nr_lamps);
    for (size_t i = 0; i < nr_lamps; ++i)
    {
        const roadmanager::TrafficLight::Lamp* lamp = tl->GetLamp(i);
        if (lamp) icons.push_back(static_cast<int>(lamp->GetIcon()));
    }
    return icons;
}

const char* PhaseToken(TrafficLightPhase phase)
{
    return phase == TrafficLightPhase::RED      ? "red"
           : phase == TrafficLightPhase::YELLOW ? "yellow"
           : phase == TrafficLightPhase::GREEN  ? "green"
                                                : "unknown";
}
}  // namespace

TrafficPolicySnapshot TrafficLightAware::Evaluate(const TrafficPolicyContext& ctx)
{
    TrafficPolicySnapshot snap;
    if (!ctx.ego) return snap;

    std::vector<RouteJunctionSpan> spans;
    std::vector<ScannedSignal>     signals =
        ScanSignalsAhead(ctx.ego, cfg_.lookahead, 2.0, cfg_.junction_guard_enabled ? &spans : nullptr);

    const double v_ego = ctx.ego->GetSpeed();

    bool   have_stop = false;  // some head wants us stopped somewhere
    double stop_s    = 0.0;    // nearest surviving target

    // Guard reporting: what the guard did to the constraint that actually came
    // out (or, when nothing came out, the suppression that removed it).
    JunctionStopAction winning_action    = JunctionStopAction::HOLD;
    std::uint32_t      winning_junction  = 0;
    bool               winning_blocked   = false;
    bool               suppressed        = false;
    std::uint32_t      suppressed_junction = 0;

    std::unordered_set<std::uint32_t> held_junctions;  // rebuilt latch (see header)
    bool                              governing_seen = false;

    for (const auto& s : signals)
    {
        auto* tl = dynamic_cast<roadmanager::TrafficLight*>(s.signal);
        if (!tl) continue;

        // A pedestrian / bicycle / tram head is a TrafficLight too (the tl-gate
        // promotes every dynamic signal), and its <validity> can name the ego's
        // own driving lane -- fabriksgatan_traffic_lights road 3 does exactly
        // that for its two type-1000002 heads -- so the scan's orientation and
        // lane filters do not reject it. The lamp icons are what tell them
        // apart. `continue`, not `break`: the nearest VEHICLE head still governs.
        if (!IsVehicleTrafficLightHead(LampIcons(tl))) continue;

        const int               id    = tl->GetId();
        const TrafficLightPhase phase = ReadPhase(tl);
        const bool              governing = !governing_seen;
        governing_seen                    = true;

        bool stop;
        if (governing)
        {
            // Commitment latch: GREEN releases; otherwise once we have decided to stop
            // (RED, or a feasible YELLOW) we hold that decision so a shrinking yellow
            // gap can't flip us back to "go" and a brief scan loss can't release it.
            if (phase == TrafficLightPhase::GREEN)
            {
                committed_[id] = false;
                stop           = false;
            }
            else if (committed_[id])
            {
                stop = true;  // already committed; hold until green
            }
            else
            {
                stop = TrafficLightShouldStop(phase, s.distance_ahead, v_ego, cfg_.params);
                if (stop) committed_[id] = true;
            }

            // Diagnostics for both decisions (tokens: red/yellow/green/unknown):
            // phase + distance explain a pass-through (green, or a yellow too close
            // to brake for) as well as a hold; `committed` marks the stop latch that
            // survives a shrinking yellow gap or a brief scan loss.
            AddDetail(snap.detail, "gt.traffic_light.signal_id", id);
            AddDetail(snap.detail, "gt.traffic_light.phase", PhaseToken(phase));
            AddDetail(snap.detail, "gt.traffic_light.dist_m", s.distance_ahead);
            AddDetail(snap.detail, "gt.traffic_light.committed", static_cast<bool>(committed_[id]));
        }
        else
        {
            // Non-governing head: read the latch, never write it, so the ordinary
            // stop/go semantics of the head that DOES govern stay untouched.
            const auto it     = committed_.find(id);
            const bool latched = (it != committed_.end() && it->second);
            stop = (phase != TrafficLightPhase::GREEN) &&
                   (latched || TrafficLightShouldStop(phase, s.distance_ahead, v_ego, cfg_.params));
        }

        if (!stop) continue;

        const double wanted = std::max(0.0, s.distance_ahead - cfg_.stop_margin);  // halt before the line

        if (!cfg_.junction_guard_enabled)
        {
            if (governing)
            {
                have_stop = true;
                stop_s    = wanted;
            }
            break;  // pre-guard behaviour: only the nearest light governs
        }

        JunctionStopResolution res = ResolveJunctionSafeStop(wanted, spans, v_ego, cfg_.junction, false);
        if (res.blocked && res.action == JunctionStopAction::SUPPRESS &&
            junction_committed_.count(res.junction_id) != 0)
        {
            // Already holding before this junction — re-resolve without the
            // braking-feasibility test so the target does not evaporate (and
            // lurch us into the box) as the remaining distance shrinks.
            res = ResolveJunctionSafeStop(wanted, spans, v_ego, cfg_.junction, true);
        }

        // A head that does NOT govern may only ever move the stop to before a
        // junction it forbids entering; it never contributes a stop of its own.
        if (!governing && res.action != JunctionStopAction::PULL_BACK) continue;

        if (res.action == JunctionStopAction::PULL_BACK) held_junctions.insert(res.junction_id);

        if (res.action == JunctionStopAction::SUPPRESS)
        {
            suppressed          = true;
            suppressed_junction = res.junction_id;
            continue;
        }

        if (!have_stop || res.s_stop < stop_s)
        {
            have_stop        = true;
            stop_s           = res.s_stop;
            winning_action   = res.action;
            winning_junction = res.junction_id;
            winning_blocked  = res.blocked;
        }
    }

    if (cfg_.junction_guard_enabled)
    {
        junction_committed_ = std::move(held_junctions);
    }

    // Only speak when a vehicle head was actually in scan — with nothing to
    // govern the ego there is no guard verdict to report, and an unconditional
    // KV would put a line in every OSI custom_detail of every frame.
    if (cfg_.junction_guard_enabled && governing_seen)
    {
        const char*   token       = "none";
        std::uint32_t junction_id = 0;
        bool          has_id      = false;
        if (have_stop)
        {
            token       = (winning_action == JunctionStopAction::PULL_BACK) ? "pull_back" : "hold";
            junction_id = winning_junction;
            has_id      = winning_blocked;
        }
        else if (suppressed)
        {
            token       = "suppress";
            junction_id = suppressed_junction;
            has_id      = true;
        }
        AddDetail(snap.detail, "gt.traffic_light.junction_action", token);
        if (has_id) AddDetail(snap.detail, "gt.traffic_light.junction_id", static_cast<int>(junction_id));
    }

    if (have_stop)
    {
        PolicyConstraint c;
        c.kind   = PolicyConstraint::Kind::STOP_AT_S;
        c.s      = stop_s;
        c.value  = 0.0;
        c.source = "traffic_light";
        snap.constraints.push_back(c);
        snap.valid = true;
        AddDetail(snap.detail, "gt.traffic_light.stop_s_m", stop_s);
    }

    return snap;
}

}  // namespace gt_esmini
