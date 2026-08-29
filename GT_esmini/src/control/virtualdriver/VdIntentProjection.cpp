#include "gt_esmini/control/virtualdriver/VdIntentProjection.hpp"

#include "gt_esmini/control/virtualdriver/PolicyDetail.hpp"

#include <algorithm>
#include <cmath>

// docs/virtualdriver/design/vd_intent_layer.md -- see VdIntentProjection.hpp for the contract.

namespace gt_esmini
{

namespace
{

constexpr double kEps          = 1.0e-9;
constexpr double kStopSpeedEps = 0.05;  // [m/s] at or below this the plan is stopped

// PolicyConstraint::Tier -> the token VdIntentReason::tier carries. Mirrors the
// serializer in VirtualDriverTelemetryJson.cpp; both are exercised by tests, and
// "safety" is load-bearing here (it exempts an intent from the dwell).
const char* TierToken(PolicyConstraint::Tier tier)
{
    switch (tier)
    {
        case PolicyConstraint::Tier::SAFETY:     return "safety";
        case PolicyConstraint::Tier::COMPLIANCE: return "compliance";
        case PolicyConstraint::Tier::COURTESY:   return "courtesy";
        case PolicyConstraint::Tier::COMFORT:    break;
    }
    return "comfort";
}

// Which vehicle a policy is reacting to, read back out of its own diagnostics.
//
// Deliberately the POLICY's own measurement rather than a second search of the
// scene: a re-derived "the lead vehicle" can disagree with the one the policy is
// actually maintaining a gap to, and then the reported subject is not the subject.
// (PolicyDetail.hpp states exactly this rule for reading the channel back.)
int SubjectOsiIdFor(const PolicyDetail& detail, const std::string& source)
{
    const char* key = nullptr;
    if (source == "lead_vehicle")        key = "gt.lead_vehicle.lead_osi_id";
    else if (source == "aeb")            key = "gt.aeb.lead_osi_id";
    else if (source == "conflict_point") key = "gt.conflict_point.other_osi_id";
    else if (source == "crosswalk")      key = "gt.crosswalk.ped_osi_id";
    // traffic_light / stop_sign / yield_sign have no other road user as a subject:
    // the ego is stopping for an instruction, not for a vehicle.
    if (key == nullptr) return -1;

    double value = 0.0;
    if (!TryGetDetail(detail, key, &value)) return -1;  // absent stays -1, never 0
    return static_cast<int>(value);
}

bool DetailIsTrue(const PolicyDetail& detail, const char* key)
{
    for (const auto& kv : detail)
    {
        if (kv.first == key) return kv.second == "true";
    }
    return false;
}

// A candidate is one intent AS SEEN THIS FRAME, before it is matched to a track.
struct Candidate
{
    IntentKind                 kind  = IntentKind::STOP;
    std::string                source;
    int                        subject_osi_id = -1;
    IntentPhase                phase          = IntentPhase::POSSIBLE;
    std::string                tier           = "comfort";
    bool                       committed      = false;
    bool                       binding_lon    = false;
    bool                       binding_lat    = false;
    std::vector<IntentBlocker> blockers;
    std::string                cancel_reason;  // ABORTING carries one while still live
    double                     distance_m   = -1.0;
    bool                       has_position = false;
    double                     x            = 0.0;
    double                     y            = 0.0;
    // STOP uses the closed-form eta rather than the map (design section 6-1).
    bool                       eta_is_stop = false;
};

// The phase a longitudinal intent (STOP / SLOW / YIELD) is in.
//
// design section 3-3 words EXECUTING as "decelerating", which is a state and not a
// quantity -- and, crucially, does not say WHICH intent is doing the decelerating
// when several are live. That subject is exactly what section 5 recovered, so it is
// what decides EXECUTING here (section 1-4 覆った想定5). Inventing a second
// definition of "decelerating" would be a second definition of the same quantity,
// which section 6-4 rules out.
//
// `responsible` is binding_lon for a policy constraint; for a road-geometry ceiling
// it is "no policy constraint is binding AND this is the nearest such ceiling",
// since binding_constraint_index only ever names policy constraints.
IntentPhase LongitudinalPhase(bool brake_light_on, bool responsible, bool committed)
{
    if (brake_light_on && responsible) return IntentPhase::EXECUTING;
    if (brake_light_on) return IntentPhase::ANNOUNCED;  // the lamp is on; this is not what lit it
    if (committed) return IntentPhase::PLANNED;         // decided, not yet acted on -- unobservable
    return IntentPhase::POSSIBLE;
}

int PhaseRank(IntentPhase phase)
{
    return static_cast<int>(phase);
}

bool IsObservablePhase(IntentPhase phase)
{
    return PhaseRank(phase) >= PhaseRank(IntentPhase::ANNOUNCED);
}

}  // namespace

// ─────────────────────────────── eta (design section 6) ───────────────────────────────

VdEtaMap BuildVdEtaMap(const VirtualDriverTelemetry& telemetry)
{
    VdEtaMap map;

    // Stage 1 -- the preview. Real planned times at real planned points, so this
    // stretch is exact rather than reconstructed.
    const auto& preview = telemetry.short_plan.preview;
    double      s_accum = 0.0;
    for (size_t i = 0; i < preview.size(); ++i)
    {
        if (i > 0)
        {
            const double dx = preview[i].x - preview[i - 1].x;
            const double dy = preview[i].y - preview[i - 1].y;
            s_accum += std::sqrt(dx * dx + dy * dy);
        }
        // Non-monotonic s cannot be inverted; a stalled preview (the ego at a
        // standstill piles points on one spot) simply stops extending the map.
        if (!map.s.empty() && s_accum <= map.s.back() + kEps) continue;
        map.s.push_back(s_accum);
        map.t.push_back(preview[i].t);
    }
    if (map.s.empty() && !preview.empty())
    {
        map.s.push_back(0.0);
        map.t.push_back(preview.front().t);
    }

    // Stage 2 -- integrate the mid/long ceiling beyond the preview horizon.
    const double preview_end_s = map.s.empty() ? 0.0 : map.s.back();
    const double preview_end_t = map.t.empty() ? 0.0 : map.t.back();

    const auto& profile = telemetry.midlong.v_target_profile;
    double      t_accum = preview_end_t;
    for (size_t i = 0; i + 1 < profile.size(); ++i)
    {
        const double s0 = profile[i].first;
        const double s1 = profile[i + 1].first;
        const double v0 = profile[i].second;
        const double v1 = profile[i + 1].second;

        // The first PLANNED STOP ends the map. Everything past it depends on how
        // long the stop lasts, which nothing here knows.
        if (map.s_stop_cutoff < 0.0 && v0 <= kStopSpeedEps)
        {
            map.s_stop_cutoff = s0;
            break;
        }

        if (s1 <= preview_end_s + kEps) continue;  // already covered, exactly, by the preview
        const double ds = s1 - s0;
        if (ds <= kEps) continue;

        const double v_sum = v0 + v1;
        if (v_sum <= kStopSpeedEps)
        {
            map.s_stop_cutoff = s0;
            break;
        }

        // Trapezoidal rule: divide by the segment MEAN speed. Using one end alone
        // always reads short across a deceleration.
        t_accum += 2.0 * ds / v_sum;

        if (!map.s.empty() && s1 <= map.s.back() + kEps) continue;
        map.s.push_back(s1);
        map.t.push_back(t_accum);

        if (v1 <= kStopSpeedEps)
        {
            map.s_stop_cutoff = s1;
            break;
        }
    }

    map.valid = map.s.size() >= 2;
    return map;
}

double VdEtaAt(const VdEtaMap& map, double distance_m)
{
    if (!map.valid || distance_m < 0.0) return -1.0;
    // Past the planned stop the question has no answer (design section 6-2).
    if (map.s_stop_cutoff >= 0.0 && distance_m > map.s_stop_cutoff + kEps) return -1.0;
    if (distance_m > map.s.back() + kEps) return -1.0;  // beyond the horizon: unknown, not zero
    if (distance_m <= map.s.front()) return map.t.front();

    for (size_t i = 1; i < map.s.size(); ++i)
    {
        if (distance_m > map.s[i]) continue;
        const double span = map.s[i] - map.s[i - 1];
        if (span <= kEps) return map.t[i];
        const double frac = (distance_m - map.s[i - 1]) / span;
        return map.t[i - 1] + frac * (map.t[i] - map.t[i - 1]);
    }
    return map.t.back();
}

double VdEtaToStop(double distance_m, double v_ego_mps)
{
    if (distance_m < 0.0 || v_ego_mps <= kStopSpeedEps) return -1.0;
    return 2.0 * distance_m / v_ego_mps;
}

// ─────────────────────────────── the projection ───────────────────────────────

VdIntentFrame ProjectVdIntents(VdIntentState&                state,
                               const VirtualDriverTelemetry& telemetry,
                               double                        dt,
                               const VdIntentConfig&         cfg)
{
    VdIntentFrame frame;

    if (!cfg.enabled)
    {
        // Both arrays empty AND nothing retained -- so a run that turns the layer
        // off mid-flight cannot publish a stale row on some later frame (design
        // section 9-3's first negative control).
        state.tracks.clear();
        return frame;
    }

    const VdEtaMap eta_map = cfg.eta_enabled ? BuildVdEtaMap(telemetry) : VdEtaMap{};

    std::vector<Candidate> candidates;

    // ---- (a) policy constraints: STOP / SLOW / YIELD -------------------------
    //
    // Position for a STOP comes from the "stop" markers ApplyPolicyConstraints
    // appends, which are emitted one per STOP_AT_S in constraint order. The count
    // is checked below rather than assumed: if the two ever stop being 1:1, no
    // position is reported at all, because a WRONG position is worse than none.
    size_t stop_constraint_count = 0;
    for (const auto& c : telemetry.policy.constraints)
    {
        if (c.kind == PolicyConstraint::Kind::STOP_AT_S) ++stop_constraint_count;
    }
    std::vector<const MidLongConstraint*> stop_markers;
    for (const auto& m : telemetry.midlong.constraints)
    {
        if (m.kind == "stop") stop_markers.push_back(&m);
    }
    const bool stop_markers_align = (stop_markers.size() == stop_constraint_count);

    size_t stop_seen = 0;
    for (size_t i = 0; i < telemetry.policy.constraints.size(); ++i)
    {
        const PolicyConstraint& c = telemetry.policy.constraints[i];

        Candidate cand;
        bool      keep = true;
        switch (c.kind)
        {
            case PolicyConstraint::Kind::STOP_AT_S:
                // design section 3-1-1: STOP is projected from the CONSTRAINT, never
                // from which policy raised it. "I meant to give way and ended up
                // stopping" then falls out for free -- the yield sign contributes a
                // YIELD row and the conflict resolver contributes a STOP row, both at
                // once. Branching on the policy here is exactly what would break that.
                cand.kind        = IntentKind::STOP;
                cand.distance_m  = c.s;
                cand.eta_is_stop = true;
                break;
            case PolicyConstraint::Kind::MAX_SPEED_TO_S:
                cand.kind       = (c.source == "yield_sign") ? IntentKind::YIELD : IntentKind::SLOW;
                cand.distance_m = c.s;
                break;
            case PolicyConstraint::Kind::MAX_SPEED:
                cand.kind = IntentKind::SLOW;
                // A blanket cap applies everywhere; there is no distance to it.
                cand.distance_m = -1.0;
                break;
            default:
                // NONE / WAIT_UNTIL, and Kind::YIELD -- which has no emitter anywhere
                // in the repository and no case in ApplyPolicyConstraints either, so
                // binding a vocabulary value to it would create an intent that could
                // never appear (design section 3-1).
                keep = false;
                break;
        }
        if (!keep) continue;

        cand.source         = c.source;
        cand.tier           = TierToken(c.tier);
        cand.subject_osi_id = SubjectOsiIdFor(telemetry.policy.detail, c.source);
        cand.committed      = (c.source == "traffic_light") &&
                         DetailIsTrue(telemetry.policy.detail, "gt.traffic_light.committed");
        cand.binding_lon = (telemetry.midlong.binding_constraint_index == static_cast<int>(i));
        cand.phase       = LongitudinalPhase(telemetry.brake_light_on, cand.binding_lon, cand.committed);

        if (c.kind == PolicyConstraint::Kind::STOP_AT_S)
        {
            if (stop_markers_align && stop_seen < stop_markers.size())
            {
                cand.has_position = true;
                cand.x            = stop_markers[stop_seen]->x;
                cand.y            = stop_markers[stop_seen]->y;
            }
            ++stop_seen;
        }

        candidates.push_back(cand);
    }

    // ---- (b) road-geometry ceilings: SLOW ------------------------------------
    //
    // These are not policy constraints, so binding_constraint_index cannot name
    // them and binding_lon stays false -- that flag reports what the planner
    // actually decided and is not stretched to cover something it did not measure.
    // Only the EXECUTING test uses the substitute rule.
    int    nearest_ceiling_idx = -1;
    double nearest_ceiling_s   = 0.0;
    for (size_t i = 0; i < telemetry.midlong.constraints.size(); ++i)
    {
        const MidLongConstraint& m = telemetry.midlong.constraints[i];
        if (m.kind != "curve" && m.kind != "speed_limit" && m.kind != "junction") continue;
        if (nearest_ceiling_idx < 0 || m.s < nearest_ceiling_s)
        {
            nearest_ceiling_idx = static_cast<int>(i);
            nearest_ceiling_s   = m.s;
        }
    }
    const bool road_ceiling_governs = (telemetry.midlong.binding_constraint_index < 0);

    for (size_t i = 0; i < telemetry.midlong.constraints.size(); ++i)
    {
        const MidLongConstraint& m = telemetry.midlong.constraints[i];
        if (m.kind != "curve" && m.kind != "speed_limit" && m.kind != "junction") continue;

        Candidate cand;
        cand.kind         = IntentKind::SLOW;
        cand.source       = m.kind;  // "curve" / "speed_limit" / "junction"
        cand.distance_m   = m.s;
        cand.has_position = true;
        cand.x            = m.x;
        cand.y            = m.y;
        const bool responsible =
            road_ceiling_governs && (static_cast<int>(i) == nearest_ceiling_idx);
        cand.phase = LongitudinalPhase(telemetry.brake_light_on, responsible, /*committed=*/false);
        candidates.push_back(cand);
    }

    // ---- (c) LANE_CHANGE -----------------------------------------------------
    //
    // design section 3-1-1: while an OVERTAKE is live it OWNS the lane changes it is
    // made of, and reporting both would draw one lateral movement as two rows.
    const bool overtake_live = (telemetry.overtake.phase != "idle");

    // Previous phase of a track, needed because COMPLETING and ABORTING are defined
    // relative to what the intent was doing a moment ago, not by this frame alone.
    auto previous_phase = [&state](IntentKind kind, const std::string& source) -> IntentPhase {
        for (const auto& track : state.tracks)
        {
            if (track.kind == kind && track.source == source) return track.phase;
        }
        return IntentPhase::POSSIBLE;
    };
    auto was_moving = [](IntentPhase phase) {
        return phase == IntentPhase::EXECUTING || phase == IntentPhase::COMPLETING ||
               phase == IntentPhase::ABORTING;
    };

    const auto&  lc              = telemetry.lane_change;
    const double lateral_offset  = std::fabs(telemetry.lane_offset);
    const bool   lateral_settled = lateral_offset <= cfg.abort_converged_offset_m;

    if (!overtake_live)
    {
        const IntentPhase prev = previous_phase(IntentKind::LANE_CHANGE, "route");
        // design section 1-4 覆った想定6: POSSIBLE is "the route says you are not where
        // you should be", which is true even when there is no lane to move into --
        // and that case is the only producer of the no_target_lane blocker.
        const bool route_wants_a_change = telemetry.route_lane.valid && !telemetry.route_lane.on_target_lane;

        Candidate cand;
        cand.kind       = IntentKind::LANE_CHANGE;
        cand.source     = "route";
        cand.distance_m = lc.dist_to_connection;
        cand.blockers   = lc.blockers;

        bool present = true;
        if (lc.armed)
        {
            cand.phase       = IntentPhase::EXECUTING;
            cand.binding_lat = true;
        }
        else if (was_moving(prev) && !lateral_settled)
        {
            // design section 9-2 item 9 -- the ONE bit that separates these two.
            // Both look identical from outside (armed dropped, the body is still off
            // centre); only aborted_reason says whether the plan finished or was
            // thrown away.
            if (lc.aborted_reason.empty())
            {
                cand.phase = IntentPhase::COMPLETING;
            }
            else
            {
                cand.phase         = IntentPhase::ABORTING;
                cand.cancel_reason = lc.aborted_reason;
            }
            cand.binding_lat = true;
        }
        else if (lc.signal_active)
        {
            cand.phase = IntentPhase::ANNOUNCED;
        }
        else if (route_wants_a_change && lc.dist_to_connection >= 0.0 &&
                 lc.dist_to_connection <= lc.required_m)
        {
            cand.phase = IntentPhase::PLANNED;
        }
        else if (route_wants_a_change)
        {
            cand.phase = IntentPhase::POSSIBLE;
        }
        else
        {
            present = false;
        }

        if (present) candidates.push_back(cand);
    }

    // ---- (d) OVERTAKE --------------------------------------------------------
    {
        const auto&       ot   = telemetry.overtake;
        const IntentPhase prev = previous_phase(IntentKind::OVERTAKE, "slow_lead");

        Candidate cand;
        cand.kind = IntentKind::OVERTAKE;
        // NOT "lead_vehicle": that token means the lead-vehicle POLICY emitted a
        // constraint, which can hold or not hold independently of an overtake being
        // considered.
        cand.source         = "slow_lead";
        cand.subject_osi_id = ot.lead_osi_id;
        cand.blockers       = ot.blockers;
        cand.distance_m     = -1.0;  // an overtake is not "at" a distance

        bool present = true;
        if (ot.phase == "signal_out")
        {
            cand.phase = IntentPhase::ANNOUNCED;
        }
        else if (ot.phase == "moving_out" || ot.phase == "pass")
        {
            cand.phase       = IntentPhase::EXECUTING;
            cand.binding_lat = lc.armed;
        }
        else if (ot.phase == "signal_back" || ot.phase == "moving_back")
        {
            // COMPLETING, not ABORTING: the return leg IS the plan running to its
            // end (design section 3-2-1).
            cand.phase       = IntentPhase::COMPLETING;
            cand.binding_lat = lc.armed;
        }
        else if (was_moving(prev) && !lateral_settled && !lc.aborted_reason.empty())
        {
            cand.phase         = IntentPhase::ABORTING;
            cand.cancel_reason = lc.aborted_reason;
            cand.binding_lat   = true;
        }
        else if (ot.considered)
        {
            cand.phase = IntentPhase::POSSIBLE;
        }
        else
        {
            present = false;
        }

        if (present) candidates.push_back(cand);
    }

    // ---- (e) TURN ------------------------------------------------------------
    //
    // ANNOUNCED and EXECUTING come from telemetry.junction_turn -- the LEGAL signal
    // lookahead, unchanged, which is what REQ-AD-021 verifies against. POSSIBLE is
    // the new observation scan, and only exists when that scan is switched on.
    // Keeping the two sources apart is what makes section 9-3's negative control
    // ("turn the scan off and POSSIBLE disappears while ANNOUNCED keeps working")
    // mean something.
    //
    // No blockers, ever (design section 8-6). A turn cannot be "blocked": waiting to
    // turn is a STOP intent with its own motive, and attaching that motive to the
    // TURN as well would book the same fact twice.
    {
        Candidate cand;
        cand.kind   = IntentKind::TURN;
        cand.source = "route";

        // on_connector alone is NOT a turn. A junction connector that carries the route
        // STRAIGHT through reports on_connector=true with dir==0, and calling that a TURN would
        // make the intent fire on every intersection the ego merely drives across -- which is
        // exactly the negative control in design section 9-3 ("straight through a junction
        // produces no TURN"). Caught on a real run, not by the unit test, because the unit test
        // only covered "nowhere near a junction": a field that is always true looks identical to
        // a field that is correct until something makes it say no.
        bool present = true;
        if (telemetry.junction_turn.on_connector && telemetry.junction_turn.dir != 0)
        {
            cand.phase       = IntentPhase::EXECUTING;
            cand.distance_m  = 0.0;
            cand.binding_lat = !lc.armed;
        }
        else if (!telemetry.junction_turn.on_connector && telemetry.junction_turn.dir != 0)
        {
            cand.phase      = IntentPhase::ANNOUNCED;
            cand.distance_m = telemetry.junction_turn.dist_to_entry_m;
        }
        else if (cfg.turn_lookahead_m > 0.0 && telemetry.junction_turn_observed.dir != 0 &&
                 !telemetry.junction_turn_observed.on_connector)
        {
            cand.phase      = IntentPhase::POSSIBLE;
            cand.distance_m = telemetry.junction_turn_observed.dist_to_entry_m;
        }
        else
        {
            present = false;
        }

        if (present) candidates.push_back(cand);
    }

    // ---- binding_lat: at most one owner ---------------------------------------
    // Several candidates can claim it (an overtake hop is a lane-change hop; a lane
    // change on a connector is both). Priority follows the controller's own: an
    // armed hop overrides the route, and an overtake owns the hop it borrowed.
    {
        int best = -1;
        for (size_t i = 0; i < candidates.size(); ++i)
        {
            if (!candidates[i].binding_lat) continue;
            if (best < 0)
            {
                best = static_cast<int>(i);
                continue;
            }
            const IntentKind a = candidates[static_cast<size_t>(best)].kind;
            const IntentKind b = candidates[i].kind;
            auto rank          = [](IntentKind k) { return k == IntentKind::OVERTAKE ? 3
                                                  : k == IntentKind::LANE_CHANGE     ? 2
                                                                                     : 1; };
            if (rank(b) > rank(a)) best = static_cast<int>(i);
        }
        for (size_t i = 0; i < candidates.size(); ++i)
        {
            candidates[i].binding_lat = (static_cast<int>(i) == best);
        }
    }

    // ---- match candidates to tracks -------------------------------------------
    std::vector<bool> track_matched(state.tracks.size(), false);

    for (const Candidate& cand : candidates)
    {
        VdIntentTrack* track = nullptr;
        for (size_t i = 0; i < state.tracks.size(); ++i)
        {
            if (track_matched[i]) continue;
            if (state.tracks[i].kind != cand.kind) continue;
            if (state.tracks[i].source != cand.source) continue;
            if (state.tracks[i].subject_osi_id != cand.subject_osi_id) continue;
            track            = &state.tracks[i];
            track_matched[i] = true;
            break;
        }

        if (track == nullptr)
        {
            VdIntentTrack fresh;
            fresh.id             = state.next_id++;
            fresh.kind           = cand.kind;
            fresh.source         = cand.source;
            fresh.subject_osi_id = cand.subject_osi_id;
            state.tracks.push_back(fresh);
            track_matched.push_back(true);
            track = &state.tracks.back();
        }

        // A candidate reappearing cancels an in-progress expiry.
        track->expiring      = false;
        track->dwell_left_s  = 0.0;
        track->cancel_reason = cand.cancel_reason;
        track->tier          = cand.tier;
        track->blockers      = cand.blockers;

        IntentPhase phase = cand.phase;
        // Once ANNOUNCED, never below it again: the signal WAS given, and reporting a
        // regression to POSSIBLE would claim the announcement never happened. It also
        // keeps intents[] free of phases section 3-2 marks as unobservable.
        if (track->reached_announced && PhaseRank(phase) < PhaseRank(IntentPhase::ANNOUNCED))
        {
            phase = IntentPhase::ANNOUNCED;
        }
        track->phase = phase;
        if (IsObservablePhase(phase)) track->reached_announced = true;
        if (phase == IntentPhase::EXECUTING) track->reached_executing = true;

        VdIntentReason reason;
        reason.id            = track->id;
        reason.kind          = track->kind;
        reason.phase         = track->phase;
        reason.source        = track->source;
        reason.tier          = track->tier;
        reason.binding_lon   = cand.binding_lon;
        reason.binding_lat   = cand.binding_lat;
        reason.committed     = cand.committed;
        reason.blockers      = cand.blockers;
        reason.cancel_reason = cand.cancel_reason;
        frame.reasons.push_back(reason);

        if (track->reached_announced)
        {
            VdIntent out;
            out.id             = track->id;
            out.kind           = track->kind;
            out.phase          = track->phase;
            out.distance_m     = cand.distance_m;
            out.subject_osi_id = cand.subject_osi_id;
            out.has_position   = cand.has_position;
            out.x              = cand.x;
            out.y              = cand.y;
            if (cfg.eta_enabled)
            {
                out.eta_s = cand.eta_is_stop ? VdEtaToStop(cand.distance_m, telemetry.speed)
                                             : VdEtaAt(eta_map, cand.distance_m);
            }
            frame.intents.push_back(out);
        }
    }

    // ---- expire the unmatched --------------------------------------------------
    //
    // An intent never just disappears between frames (design section 8-8): it is held
    // for at least one frame in an ending phase, with the blockers that were in its
    // way still attached -- because "why was it given up on" IS "what was in the way
    // just before".
    std::vector<VdIntentTrack> survivors;
    survivors.reserve(state.tracks.size());

    for (size_t i = 0; i < state.tracks.size(); ++i)
    {
        VdIntentTrack track = state.tracks[i];
        if (track_matched[i])
        {
            survivors.push_back(track);
            continue;
        }

        if (!track.expiring)
        {
            track.expiring = true;
            // COMPLETING vs ABANDONED: was the intent ever actually acted on? A stop
            // the ego made and then released has been COMPLETED; one that was
            // announced and then evaporated was ABANDONED. This is the same
            // distinction section 3-3 draws for the traffic light (green -> COMPLETING)
            // and section 3-4 for the lane change (announced-only -> ABANDONED),
            // stated once instead of per feature.
            track.phase = track.reached_executing ? IntentPhase::COMPLETING : IntentPhase::ABANDONED;

            if (track.phase == IntentPhase::ABANDONED)
            {
                if (!track.blockers.empty())
                {
                    track.cancel_reason = track.blockers.front().code;
                }
                else if (telemetry.route_lane.rerouted &&
                         (track.kind == IntentKind::LANE_CHANGE || track.kind == IntentKind::TURN))
                {
                    track.cancel_reason = kCancelRerouted;
                }
                else
                {
                    track.cancel_reason = kCancelConstraintCleared;
                }
            }
            else
            {
                track.cancel_reason.clear();  // nothing was cancelled; it finished
            }

            // tier == safety is exempt from the dwell but NOT from being seen to end:
            // it still gets this one frame. Holding an AEB row on screen after it
            // released would read as "still braking" (design section 8-9).
            track.dwell_left_s =
                (track.reached_announced && track.tier != "safety") ? cfg.min_dwell_s : 0.0;
        }
        else
        {
            track.dwell_left_s -= std::max(0.0, dt);
        }

        VdIntentReason reason;
        reason.id            = track.id;
        reason.kind          = track.kind;
        reason.phase         = track.phase;
        reason.source        = track.source;
        reason.tier          = track.tier;
        reason.blockers      = track.blockers;  // kept on purpose -- see above
        reason.cancel_reason = track.cancel_reason;
        frame.reasons.push_back(reason);

        if (track.reached_announced)
        {
            VdIntent out;
            out.id             = track.id;
            out.kind           = track.kind;
            out.phase          = track.phase;
            out.subject_osi_id = track.subject_osi_id;
            frame.intents.push_back(out);
        }

        if (track.dwell_left_s > kEps) survivors.push_back(track);
    }

    state.tracks = survivors;
    return frame;
}

}  // namespace gt_esmini
