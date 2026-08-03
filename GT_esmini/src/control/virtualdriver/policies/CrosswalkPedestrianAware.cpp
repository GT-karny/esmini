#include "gt_esmini/control/virtualdriver/policies/CrosswalkPedestrianAware.hpp"

#include "gt_esmini/control/common/OsiIdentity.hpp"
#include "gt_esmini/control/virtualdriver/PolicyDetail.hpp"

#include "Entities.hpp"
#include "RoadManager.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace scenarioengine;

namespace gt_esmini
{

// ─────────────────────────── pure decision layer ──────────────────────────────
namespace crosswalk_decide
{
PedPhase FoldPedPhase(const std::vector<LampReading>& lamps)
{
    bool const_red = false, const_green = false, flashing_any = false, other = false;
    for (const LampReading& lamp : lamps)
    {
        if (lamp.broken) continue;
        if (!lamp.constant && !lamp.flashing) continue;  // OFF / undefined -> ignore
        if (lamp.flashing)
        {
            flashing_any = true;
            continue;
        }
        switch (lamp.color)
        {
            case LampReading::Color::RED:   const_red   = true; break;
            case LampReading::Color::GREEN: const_green = true; break;
            default:                        other       = true; break;
        }
    }

    // A flashing or mixed / unexpected reading is ambiguous (safe side). A clean
    // single constant colour is decisive.
    if (flashing_any || other) return PedPhase::AMBIGUOUS;
    if (const_red && const_green) return PedPhase::AMBIGUOUS;  // both on -> unreadable
    if (const_red)   return PedPhase::RED;
    if (const_green) return PedPhase::GREEN;
    return PedPhase::AMBIGUOUS;  // nothing readable
}

namespace
{
// World XY of the point on the polyline at cumulative arc length `s` (clamped to
// the span, interpolated within the bracketing segment).
void PathPointAtS(const std::vector<crosswalk_geom::Pt>& pts, const std::vector<double>& s_cum,
                  double s, double& x, double& y)
{
    if (pts.empty()) { x = 0.0; y = 0.0; return; }
    if (pts.size() != s_cum.size() || s <= s_cum.front())
    {
        x = pts.front()[0];
        y = pts.front()[1];
        return;
    }
    if (s >= s_cum.back())
    {
        x = pts.back()[0];
        y = pts.back()[1];
        return;
    }
    for (size_t i = 0; i + 1 < pts.size(); ++i)
    {
        if (s <= s_cum[i + 1])
        {
            const double seg = s_cum[i + 1] - s_cum[i];
            const double f   = seg > 1.0e-9 ? (s - s_cum[i]) / seg : 0.0;
            x = pts[i][0] + f * (pts[i + 1][0] - pts[i][0]);
            y = pts[i][1] + f * (pts[i + 1][1] - pts[i][1]);
            return;
        }
    }
    x = pts.back()[0];
    y = pts.back()[1];
}
}  // namespace

BlockResult CrosswalkBlocked(const std::vector<PedState>&           peds,
                             const std::vector<crosswalk_geom::Pt>& footprint,
                             const std::vector<crosswalk_geom::Pt>& ego_path,
                             const std::vector<double>&             ego_s,
                             double                                 s_entry,
                             double                                 s_exit,
                             const BlockParams&                     p)
{
    // Passage-band half-width: peds beyond this lateral offset from the ego route
    // are out of the ego's swept path.
    const double band   = p.ego_half_width + p.release_lateral_margin;
    const double win_lo = s_entry - 5.0;
    const double win_hi = s_exit + 5.0;

    // WAITING rule gate: the precomputed signal gate AND "ego not on the footprint"
    // — while the ego occupies the crosswalk it must clear it, never park on it.
    // The CROSSING rule below stays fully active (emergency braking for a body on
    // the roadway is never suppressed).
    const bool waiting_active = p.waiting_rule_active && !p.ego_inside_footprint;

    // Waiting-rule hysteresis: widen the wait band slightly while this crosswalk is
    // the committed one so a ped hovering at the boundary does not chatter the hold.
    const double wait_margin = p.wait_margin + (p.committed ? 0.5 : 0.0);

    for (const PedState& ped : peds)
    {
        // Lateral offset from the ego route (passage-band membership) + closest
        // path arc length (for the crossing "moving away" test).
        double lat_abs = std::numeric_limits<double>::infinity();
        double s_at    = 0.0;
        const bool has_lat =
            crosswalk_geom::LateralOffsetToPolyline(ego_path, ego_s, ped.x, ped.y, win_lo, win_hi, lat_abs, s_at);

        const bool in_band = has_lat && (lat_abs <= band);

        const bool inside_fp = crosswalk_geom::PointInPolygon(footprint, ped.x, ped.y);

        if (inside_fp)
        {
            // CROSSING rule (never signal-gated). A ped ON the footprint blocks,
            // UNLESS it is outside the ego passage band AND clearly moving away from
            // the ego path (so it will have left before we arrive).
            if (!in_band && has_lat)
            {
                double cpx = ped.x, cpy = ped.y;
                PathPointAtS(ego_path, ego_s, s_at, cpx, cpy);
                double       ux = ped.x - cpx, uy = ped.y - cpy;  // from path toward the ped
                const double un = std::sqrt(ux * ux + uy * uy);
                if (un > 1.0e-6)
                {
                    ux /= un;
                    uy /= un;
                    const double moving_away = ped.vx * ux + ped.vy * uy;  // [m/s] along away-direction
                    if (moving_away > 0.2)
                        continue;  // out of band and departing -> not a threat
                }
                // Out of band but stationary / approaching -> still on the roadway; block.
            }
            return {true, ped.osi_id};  // CROSSING block
        }

        // WAITING rule: ped just off the footprint, plausibly about to step on.
        if (!waiting_active) continue;
        if (in_band) continue;  // a ped standing in our lane off-crosswalk is out of scope here
        const double d = crosswalk_geom::DistanceToPolygon(footprint, ped.x, ped.y);
        if (d <= wait_margin)
            return {true, ped.osi_id};  // WAITING block
    }

    return {};
}
}  // namespace crosswalk_decide

// ───────────────────────── engine-facing thin wrapper ─────────────────────────
namespace
{
using crosswalk_decide::LampReading;
using crosswalk_decide::PedPhase;
using crosswalk_geom::Pt;

// Flatten a TrafficLight's lamps into plain readings for FoldPedPhase. PARITY with
// TrafficLightAware.cpp's file-local ReadPhase() lamp access (GetNrLamps/GetLamp,
// MODE_CONSTANT/MODE_FLASHING, COLOR_*); kept local per the design — do NOT modify
// TrafficLightAware to export it. Defensive (crash-proofing): null light, zero or
// absurd lamp counts (uninitialized nr_lamps_ on unsupported type combos — see
// FindLinkedPedSignal's TYPE_UNDEFINED guard) and out-of-range lamp access all
// yield an EMPTY reading set, which FoldPedPhase folds to AMBIGUOUS (safe side).
std::vector<LampReading> ExtractLampReadings(roadmanager::TrafficLight* tl)
{
    std::vector<LampReading> out;
    if (!tl) return out;

    constexpr size_t kMaxSaneLamps = 16;
    const size_t     n             = tl->GetNrLamps();
    if (n == 0 || n > kMaxSaneLamps) return out;  // absurd count -> unreadable

    try
    {
        for (size_t i = 0; i < n; ++i)
        {
            roadmanager::TrafficLight::Lamp* lamp = tl->GetLamp(i);  // lamps_.at() may throw
            if (!lamp) continue;

            LampReading r;
            r.broken = lamp->IsBroken();
            const roadmanager::Signal::LampMode mode = lamp->GetMode();
            r.constant = (mode == roadmanager::Signal::LampMode::MODE_CONSTANT);
            r.flashing = (mode == roadmanager::Signal::LampMode::MODE_FLASHING);
            switch (lamp->GetColor())
            {
                case roadmanager::COLOR_RED:   r.color = LampReading::Color::RED; break;
                case roadmanager::COLOR_GREEN: r.color = LampReading::Color::GREEN; break;
                default:                       r.color = LampReading::Color::OTHER; break;
            }
            out.push_back(r);
        }
    }
    catch (const std::out_of_range&)
    {
        out.clear();  // inconsistent lamp storage -> unreadable -> AMBIGUOUS
    }
    return out;
}

// Locate a scanned crosswalk by (road_id, object_id) in this frame's scan.
const ScannedCrosswalk* FindByIdentity(const CrosswalkScanResult& scan, unsigned int road_id, unsigned int object_id)
{
    for (const ScannedCrosswalk& cw : scan.crosswalks)
        if (cw.road_id == road_id && cw.object && cw.object->GetId() == object_id)
            return &cw;
    return nullptr;
}
}  // namespace

TrafficPolicySnapshot CrosswalkPedestrianAware::Evaluate(const TrafficPolicyContext& ctx)
{
    TrafficPolicySnapshot snap;
    if (!ctx.ego || !ctx.entities) return snap;

    Object* ego = ctx.ego;

    CrosswalkScanResult scan = ScanCrosswalksAhead(ego, cfg_.lookahead, cfg_.step, cfg_.signal_link_radius);
    if (scan.crosswalks.empty())
    {
        committed_ = false;  // nothing ahead -> drop any held yield
        return snap;
    }

    // Pt + s_cum views of the walked ego path (the pure classifier's inputs).
    std::vector<Pt>     ego_poly;
    std::vector<double> ego_s;
    ego_poly.reserve(scan.ego_path.size());
    ego_s.reserve(scan.ego_path.size());
    for (const RoutePathPoint& p : scan.ego_path)
    {
        ego_poly.push_back({p.x, p.y});
        ego_s.push_back(p.s_cum);
    }

    // Flatten pedestrians (exact opposite of ConflictPointResolver's VEHICLE
    // filter) into plain states. Velocity is the engine-maintained WORLD velocity
    // (pos_.GetVelX/GetVelY, updated per frame by ScenarioEngine; teleport frames
    // prefill from heading) — NOT reconstructed from speed x heading, which lags /
    // mis-signs for a ped walking backwards relative to its heading.
    std::vector<crosswalk_decide::PedState> peds;
    for (Object* o : ctx.entities->object_)
    {
        if (!o || o == ego) continue;
        if (o->GetType() != Object::Type::PEDESTRIAN) continue;
        crosswalk_decide::PedState ps;
        ps.x  = o->pos_.GetX();
        ps.y  = o->pos_.GetY();
        ps.vx = o->pos_.GetVelX();
        ps.vy = o->pos_.GetVelY();
        // Carry the identity through the flattening. Without this the classifier
        // answers "someone is crossing" and the id is gone by the time anyone
        // asks who (OsiIdentity.hpp).
        ps.osi_id = OsiIdOf(o);
        peds.push_back(ps);
    }

    // Decision scope this frame (W3 pattern, negative case included): a silent
    // frame with both counts non-zero means "evaluated and not blocked", which
    // used to be indistinguishable from "saw nothing ahead".
    AddDetail(snap.detail, "gt.crosswalk.crosswalks", static_cast<int>(scan.crosswalks.size()));
    AddDetail(snap.detail, "gt.crosswalk.peds", static_cast<int>(peds.size()));

    const double ego_half_width = 0.5 * ego->boundingbox_.dimensions_.width_;
    const double ego_x          = ego->pos_.GetX();
    const double ego_y          = ego->pos_.GetY();

    // WAITING-rule signal gate for one crosswalk (precomputed engine-side; the
    // pure classifier just consumes the bool). RED ped phase -> suppressed;
    // GREEN / AMBIGUOUS / no readable signal -> active (safe side).
    auto waitingGate = [&](const ScannedCrosswalk& cw) -> bool {
        if (!cfg_.yield_to_waiting) return false;
        if (cfg_.ped_signal_aware && cw.ped_signal)
        {
            auto* tl = dynamic_cast<roadmanager::TrafficLight*>(cw.ped_signal);
            if (crosswalk_decide::FoldPedPhase(ExtractLampReadings(tl)) == PedPhase::RED)
                return false;  // peds must not cross -> proceed past waiting peds
        }
        return true;
    };

    // Blocking test for one crosswalk: flatten the per-crosswalk gates and call
    // the pure classifier.
    auto blocked = [&](const ScannedCrosswalk& cw, bool committed) -> crosswalk_decide::BlockResult {
        crosswalk_decide::BlockParams p;
        p.ego_half_width         = ego_half_width;
        p.wait_margin            = cfg_.wait_margin;
        p.release_lateral_margin = cfg_.release_lateral_margin;
        p.waiting_rule_active    = waitingGate(cw);
        p.committed              = committed;
        p.ego_inside_footprint   = crosswalk_geom::PointInPolygon(cw.footprint, ego_x, ego_y);
        return crosswalk_decide::CrosswalkBlocked(peds, cw.footprint, ego_poly, ego_s, cw.s_entry, cw.s_exit, p);
    };

    // Governing crosswalk = nearest (scan is sorted by s_entry) with any blocking
    // ped, paired with the ped that made it blocking (the two travel together
    // because the latch stores both).
    struct Governing
    {
        const ScannedCrosswalk* cw         = nullptr;
        int                     ped_osi_id = kNoOsiId;
    };
    auto findGoverning = [&]() -> Governing {
        for (const ScannedCrosswalk& cw : scan.crosswalks)
        {
            const crosswalk_decide::BlockResult r = blocked(cw, /*committed=*/false);
            if (r.blocked) return {&cw, r.ped_osi_id};
        }
        return {};
    };

    if (committed_)
    {
        const ScannedCrosswalk* cur = FindByIdentity(scan, committed_road_id_, committed_object_id_);
        bool release = true;
        if (cur)
        {
            committed_stop_s_ = cur->s_entry;  // pin the stop to the current entry
            const crosswalk_decide::BlockResult r = blocked(*cur, /*committed=*/true);
            if (r.blocked)
            {
                release = false;  // still blocked -> hold
                // Refresh the subject like stop_s: the crosswalk stays latched
                // while the body doing the blocking can change (one ped finishes
                // crossing as the next steps on).
                committed_ped_osi_id_ = r.ped_osi_id;
            }
        }
        // else: passed it / route changed -> release.

        if (!release)
        {
            // Latch PREEMPTION: while holding for `cur`, a ped stepping onto a
            // STRICTLY NEARER crosswalk must constrain the ego immediately — without
            // this, the nearer crossing ped would get NO constraint until the
            // farther hold cleared (crossing rule defeated). findGoverning() walks
            // nearest-first, so a strictly smaller s_entry means a different,
            // closer, blocked crosswalk.
            const Governing gov = findGoverning();
            if (gov.cw && gov.cw->s_entry < cur->s_entry - 1.0e-6)
            {
                committed_road_id_    = gov.cw->road_id;
                committed_object_id_  = gov.cw->object ? gov.cw->object->GetId() : 0u;
                committed_cw_osi_id_  = OsiIdOf(gov.cw->object);
                committed_stop_s_     = gov.cw->s_entry;
                committed_ped_osi_id_ = gov.ped_osi_id;
            }
        }
        else
        {
            const Governing gov = findGoverning();
            if (gov.cw)
            {
                // Re-commit straight to a fresh governing crosswalk.
                committed_            = true;
                committed_road_id_    = gov.cw->road_id;
                committed_object_id_  = gov.cw->object ? gov.cw->object->GetId() : 0u;
                committed_cw_osi_id_  = OsiIdOf(gov.cw->object);
                committed_stop_s_     = gov.cw->s_entry;
                committed_ped_osi_id_ = gov.ped_osi_id;
            }
            else
            {
                committed_            = false;
                committed_ped_osi_id_ = kNoOsiId;
                committed_cw_osi_id_  = kNoOsiId;
            }
        }
    }
    else
    {
        const Governing gov = findGoverning();
        if (gov.cw)
        {
            committed_            = true;
            committed_road_id_    = gov.cw->road_id;
            committed_object_id_  = gov.cw->object ? gov.cw->object->GetId() : 0u;
            committed_cw_osi_id_  = OsiIdOf(gov.cw->object);
            committed_stop_s_     = gov.cw->s_entry;
            committed_ped_osi_id_ = gov.ped_osi_id;
        }
    }

    if (!committed_) return snap;  // free to proceed

    // Which crosswalk is holding the ego (identifiers were previously squashed
    // into the bare source string — the W2-shaped information loss).
    AddDetail(snap.detail, "gt.crosswalk.object_id", static_cast<int>(committed_object_id_));
    AddDetail(snap.detail, "gt.crosswalk.road_id", static_cast<int>(committed_road_id_));
    // The same crosswalk as OSI publishes it (StationaryObject.id). `object_id`
    // above is the OpenDRIVE <object id> and does NOT match it -- in OSI that
    // number only appears as the `object_id:<n>` source_reference identifier.
    // Named `object_osi_id` (not `osi_id`) so the whole partner-id family shares
    // one `_osi_id` suffix a consumer can scan for; the pairing with `object_id`
    // directly above is then legible without reading this comment.
    // A <repeat> crosswalk emits further instances under fresh ids; this names
    // the object, not the instance (OsiIdentity.hpp).
    AddDetail(snap.detail, "gt.crosswalk.object_osi_id", committed_cw_osi_id_);
    // WHO is crossing. Previously unanswerable: the pedestrians were flattened
    // into anonymous positions before the decision was made.
    AddDetail(snap.detail, "gt.crosswalk.ped_osi_id", committed_ped_osi_id_);
    AddDetail(snap.detail, "gt.crosswalk.stop_s_m", committed_stop_s_);

    PolicyConstraint c;
    c.kind   = PolicyConstraint::Kind::STOP_AT_S;
    c.s      = std::max(0.0, committed_stop_s_ - cfg_.standoff);
    c.value  = 0.0;
    c.source = "crosswalk";
    snap.constraints.push_back(c);
    snap.valid = true;
    return snap;
}

}  // namespace gt_esmini
