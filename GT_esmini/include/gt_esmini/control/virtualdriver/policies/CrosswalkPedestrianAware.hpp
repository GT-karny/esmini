#pragma once

#include <vector>

#include "gt_esmini/control/virtualdriver/ITrafficPolicy.hpp"
#include "gt_esmini/control/virtualdriver/policies/RouteCrosswalkScan.hpp"  // crosswalk_geom::Pt

namespace gt_esmini
{

// Config for CrosswalkPedestrianAware. Flat keys are surfaced in VirtualDriverConfig
// (line-parsed, crosswalk_* prefix); this struct is the materialized form the policy
// consumes (mirrors ConflictPointResolverConfig).
struct CrosswalkPedestrianAwareConfig
{
    double lookahead              = 80.0;  // [m]   route scan horizon
    double step                   = 1.0;   // [m]   scan sampling (fine enough for ~3-4 m wide crosswalks)
    double standoff               = 3.0;   // [m]   stop this far before the footprint entry
    double wait_margin            = 2.0;   // [m]   ped within this distance of the footprint (outside it) counts as waiting
    bool   yield_to_waiting       = true;  // JP-law default: yield to peds waiting to cross
    bool   ped_signal_aware       = true;  // gate the WAITING rule on a linked pedestrian signal
    double signal_link_radius     = 10.0;  // [m]   |signal_s - crosswalk_s| on the same road
    double release_lateral_margin = 0.5;   // [m]   passage band = ego half-width + this
};

// Pure decision layer for CrosswalkPedestrianAware. NO engine headers — these are
// unit-tested in isolation (test_TrafficPolicies.cpp), following the suite's
// extraction pattern (stop_fsm / lead_idm / conflict_geom). Evaluate() flattens
// the engine state (TrafficLight lamps, entity poses/velocities) into these plain
// inputs and keeps only a thin wrapper.
namespace crosswalk_decide
{
// Pedestrian-signal phase, distilled to what the WAITING rule needs.
enum class PedPhase
{
    RED,       // constant-red on (peds must NOT cross) -> waiting rule suppressed
    GREEN,     // constant-green on (peds may cross)    -> waiting rule active
    AMBIGUOUS  // flashing / mixed / unreadable / read failure -> waiting rule active (safe side)
};

// One lamp reading, flattened from roadmanager::TrafficLight::Lamp (engine-free).
struct LampReading
{
    enum class Color
    {
        RED,
        GREEN,
        OTHER  // yellow etc. — unexpected on a pedestrian signal
    };
    bool  constant = false;  // MODE_CONSTANT
    bool  flashing = false;  // MODE_FLASHING
    bool  broken   = false;  // ignored entirely
    Color color    = Color::OTHER;
};

// Fold lamp readings into a phase. PARITY with TrafficLightAware.cpp's file-local
// ReadPhase() lamp-scanning mechanism, with two deliberate distinctions: FLASHING
// is AMBIGUOUS (a flashing ped signal is a clearance state, not a firm
// may-or-may-not-cross) and the result is the 3-way PedPhase the waiting rule
// consumes. Empty input, broken-only input, mixed constant colours or any
// unexpected colour -> AMBIGUOUS (safe side).
PedPhase FoldPedPhase(const std::vector<LampReading>& lamps);

// Plain pedestrian state for the classifier: world position + world velocity.
struct PedState
{
    double x  = 0.0;
    double y  = 0.0;
    double vx = 0.0;  // world velocity [m/s] (engine-maintained, not heading-reconstructed)
    double vy = 0.0;
    // WHICH pedestrian this is, in the OSI id space (filled by the engine-facing
    // wrapper via control/common/OsiIdentity.hpp; -1 == gt_esmini::kNoOsiId,
    // spelled literally so this decision layer stays engine-header-free). The
    // classifier never interprets it — it only carries it back out on the
    // pedestrian that blocked, so the policy can name its subject.
    int    osi_id = -1;
};

// Outcome of the blocking test for one crosswalk. `blocked` alone used to be the
// whole answer, which made the resulting stop unattributable: the diagnostics
// could say a crosswalk was holding the ego but never which body did it.
struct BlockResult
{
    bool blocked    = false;
    int  ped_osi_id = -1;  // PedState::osi_id of the blocking ped; -1 when !blocked
};

// Scalar knobs + precomputed gates for one crosswalk's blocking decision.
struct BlockParams
{
    double ego_half_width         = 1.0;   // [m]
    double wait_margin            = 2.0;   // [m] base wait band (before hysteresis)
    double release_lateral_margin = 0.5;   // [m] passage band = ego_half_width + this
    bool   waiting_rule_active    = true;  // yield_to_waiting AND the ped-signal gate (precomputed by the caller)
    bool   committed              = false; // this crosswalk is the latched one -> +0.5 m wait-band hysteresis
    bool   ego_inside_footprint   = false; // ego origin is ON the footprint -> WAITING rule suppressed
                                           // (must clear the crosswalk, not park on it; CROSSING unchanged)
};

// Does any pedestrian block the crosswalk? Two-layer classifier:
//   * CROSSING (never gated): a ped inside `footprint` blocks, UNLESS it is
//     outside the ego passage band AND clearly moving away from the ego path
//     (dot(v, away-direction) > 0.2 m/s). A stationary ped on the footprint
//     always blocks.
//   * WAITING (gated by p.waiting_rule_active, suppressed while
//     p.ego_inside_footprint): a ped outside the footprint within
//     wait_margin (+0.5 m hysteresis when p.committed) and NOT inside the
//     passage band.
// `ego_path`/`ego_s` = the walked route polyline + cumulative arc lengths;
// `s_entry`/`s_exit` = the crosswalk's route span (passage-band window is
// [s_entry-5, s_exit+5]).
// Returns the FIRST blocking pedestrian found in `peds` order (the scan stops
// there — the policy needs one subject for its single stop, not a census).
BlockResult CrosswalkBlocked(const std::vector<PedState>&           peds,
                             const std::vector<crosswalk_geom::Pt>& footprint,
                             const std::vector<crosswalk_geom::Pt>& ego_path,
                             const std::vector<double>&             ego_s,
                             double                                 s_entry,
                             double                                 s_exit,
                             const BlockParams&                     p);

}  // namespace crosswalk_decide

// Phase 3d extension (F2): yield to pedestrians at OpenDRIVE crosswalks.
//
// Two-layer model:
//
//   * CROSSING rule (unconditional collision avoidance): a pedestrian standing on
//     or moving across the crosswalk footprint blocks the ego. This is NEVER
//     signal-gated — a body on the roadway must not be driven into regardless of
//     any pedestrian light. A ped clearly outside the ego passage band AND moving
//     away is not a threat and does not block.
//
//   * WAITING rule (courtesy / law, signal-gatable): a pedestrian standing just
//     off the footprint (within wait_margin), plausibly about to step onto it. By
//     JP-law default the ego yields to them (yield_to_waiting). When a linked
//     pedestrian signal is present and ped_signal_aware is on, this rule is gated:
//     a RED pedestrian phase (peds must not cross) SUPPRESSES it (proceed);
//     GREEN / AMBIGUOUS / unreadable keeps it ACTIVE (safe side). The waiting rule
//     is also suppressed while the ego itself is ON the footprint — it must clear
//     a crosswalk it already occupies, never park on it (the crossing rule still
//     applies there).
//
// Why the occupancy-based ConflictPointResolver cannot cover this: a pedestrian
// waiting to cross has speed ~0 and its future "corridor" is empty (or a tiny
// stationary box that never sweeps into the ego corridor), so the space-time
// overlap test finds no conflict and the ego drives on. The waiting rule is a
// spatial-proximity + intent (signal / motion) judgement, not a corridor overlap.
//
// Latch (mirrors ConflictPointResolver's pattern): on a blocking ped the policy
// LATCHES onto the governing (nearest blocking) crosswalk, identified by
// road_id + object_id, and emits ONE STOP_AT_S a `standoff` before the footprint
// entry. Each frame the committed crosswalk is re-located in the fresh scan and
// its stop_s refreshed (pins the stop to a fixed spot as the ego crawls). Release
// when no blocking ped remains for that crosswalk (or it drops out of the scan),
// immediately re-committing to another blocking crosswalk if one exists. While
// held, a STRICTLY NEARER blocking crosswalk PREEMPTS the latch — a ped stepping
// onto a closer crosswalk must constrain the ego immediately, not wait for the
// farther hold to clear.
class CrosswalkPedestrianAware : public ITrafficPolicy
{
public:
    explicit CrosswalkPedestrianAware(const CrosswalkPedestrianAwareConfig& cfg = {}) : cfg_(cfg) {}
    TrafficPolicySnapshot Evaluate(const TrafficPolicyContext& ctx) override;

private:
    CrosswalkPedestrianAwareConfig cfg_;

    // Yield latch (single governing crosswalk at a time — one dominates the
    // approach; a strictly nearer blocking crosswalk preempts). Identity is
    // (road_id, object_id); stop_s refreshed each frame.
    bool         committed_           = false;
    unsigned int committed_road_id_   = 0;
    unsigned int committed_object_id_ = 0;
    double       committed_stop_s_    = 0.0;  // ego route-s of the governing footprint entry
    // The pedestrian currently holding the ego, in the OSI id space. Refreshed
    // every frame like stop_s (the blocking body can change while the latch on
    // the crosswalk itself persists), -1 when the latch is not held.
    int          committed_ped_osi_id_ = -1;
    // The governing crosswalk itself in the OSI id space (RMObject::GetGlobalId
    // -> StationaryObject.id). Kept next to committed_object_id_, which is the
    // OpenDRIVE <object id> and a different number.
    int          committed_cw_osi_id_  = -1;
};

}  // namespace gt_esmini
