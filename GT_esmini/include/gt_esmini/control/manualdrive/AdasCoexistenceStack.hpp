#pragma once

// req-vd-ad:REQ-AD-025 / req-vd-ad:REQ-AD-028 / vd-func:FUNC-075
//
// AdasCoexistenceStack -- phase A of the ManualDrive ADAS coexistence stack
// (docs/virtualdriver/design/manualdrive_adas_design.md, "design" below).
// Wires AEB (design §3) plus its FCW warning pre-stage (design §3-2) onto the
// human-driven ManualDrive path. ACC/MSL (phase C) and LKA/LDW (phase D) are
// out of scope here; see design §10's phase table.
//
// ============================================================================
// TWO-LAYER SPLIT -- why this file has both a free function and a class
// ============================================================================
// AebSafety::Evaluate() needs real esmini Object*/Entities* (candidate search
// over the road network), so an end-to-end stack test would need the engine.
// The stack is therefore split into a PURE free function carrying every
// DECISION (ComputeManualAdasFrame, below) plus a thin engine-facing class
// (AdasCoexistenceStack) that only builds the TrafficPolicyContext and calls
// the policies -- same convention as AdSteeringEnvelope / PedalArbitrator /
// KickdownDetector. AdasCoexistenceStack's own body must stay nearly trivial:
// every decision lives in ComputeManualAdasFrame, which is exercised directly
// by test_AdasCoexistenceStack.cpp with no engine involved.
//
// ============================================================================
// THE TWO AebSafety INSTANCES -- the FCW mechanism (do not invent another)
// ============================================================================
// policies_ (a TrafficPolicyManager, not a bare AebSafety) is the
// INTERVENTION path: phase A Add()s exactly ONE AebSafety when
// cfg.aeb_enabled. It is a manager rather than a bare policy because phases C
// add lead/traffic-light/stop-sign policies to this SAME manager later.
//
// fcw_gate_ is a SECOND, independent AebSafety instance: the WARNING path.
// Its AebSafetyConfig is IDENTICAL to the intervention instance's except for
// the two gate thresholds (ttc_threshold, min_a_req), which are LOOSER (see
// DeriveFcwGateConfig below). Candidate SELECTION parameters (lookahead,
// lateral_tol, stop_margin) are held identical on purpose: both instances
// then evaluate the SAME candidate with the SAME gap/closing-speed inputs, so
// "warning fired" becomes a SUPERSET of "intervention fired" BY CONSTRUCTION
// -- design §3-2's ">= 0.8s warning lead" is a structural property of the two
// thresholds, not a tuning coincidence that could silently drift apart.
//
// Why two AebSafety instances rather than parsing gt.aeb.ttc_s back out of
// the intervention instance's own PolicyDetail: PolicyDetail
// (PolicyDetail.hpp) is an OBSERVATION channel -- fixed 3-decimal strings
// destined for OSI custom_detail. Feeding it back in as a CONTROL input would
// couple the control path to a telemetry string format and throw away
// precision for no reason; a second policy instance is the honest way to ask
// "what would AEB have decided at a looser threshold".
//
// ============================================================================
// REQUIRED DECELERATION -- derived from the CONSTRAINT, not the detail
// ============================================================================
// AebSafety emits PolicyConstraint{kind=STOP_AT_S, tier=SAFETY, source="aeb",
// s=max(0, gap-stop_margin)} on firing (see AebSafety.cpp's emit block). `s`
// here is the DISTANCE AHEAD OF THE EGO in metres -- NOT a route coordinate.
// ManualDrive has no route (no MidLongPlanner consuming route-s constraints),
// which is exactly why this distinction matters here and did not matter to
// any prior consumer of PolicyConstraint::s.
//
// ComputeManualAdasFrame derives its own required deceleration directly from
// this distance and the EGO's OWN speed (NOT AebSafety's internal closing
// speed, which already went into the intervention instance's fire/no-fire
// decision):
//
//   a_req = v^2 / (2*d),   v = ego_speed_mps, d = constraint.s
//
//   * v <= 0                 -> no request (already stopped; never divide
//                               by/into a non-positive speed).
//   * d <= kMinStopDistanceM -> a SATURATING request (>= the arbitrator's own
//                               full_brake_decel_mps2, so PedalArbitrator's
//                               clamp(ff, 0, 1) pins the feedforward at 1.0)
//                               instead of dividing by a near-zero distance.
//   * several qualifying constraints -> the STRICTEST wins, i.e. the one
//                               with the LARGEST resulting a_req (equivalently
//                               the SMALLEST d): at a fixed speed a shorter
//                               stopping distance is always the harder
//                               constraint, and honouring a laxer one of
//                               several simultaneously-qualifying constraints
//                               could mean missing the nearer stop point.
//   * only kind==STOP_AT_S && tier==SAFETY && source=="aeb" constraints
//     qualify. Every other kind/tier/source (MAX_SPEED ceilings, COMFORT-tier
//     lead-following, and any phase C/D policy once it exists) is IGNORED
//     here -- phase A arbitrates AEB only; folding other constraints into
//     ManualDrive pedals is phase C/D scope (design §4/§6).
//
// ============================================================================
// DOMAIN OWNERSHIP (design §2-3)
// ============================================================================
// When `owns_longitudinal` is false, ComputeManualAdasFrame does not
// arbitrate AT ALL: driver_cmd passes through untouched, no policy decision
// is honoured (even if the intervention/warning snapshots carry a firing
// constraint -- a split-domain ManualDrive instance must not act on a domain
// it does not own), and the returned decision is all-false
// (md-split-no-double-equipment, design §2-3). `cfg.aeb_enabled == false`
// takes the IDENTICAL bypass for the same reason (config authority): a
// disabled function must behave exactly like an unowned domain, not merely
// "usually" produce zero output. Neither bypass touches the shared
// KickdownDetector or PedalArbitrator state -- their latches are left frozen,
// not evolved, while the stack is not actually running on this domain this
// frame (evolving them on data the stack is refusing to act on would let an
// unowned/disabled frame quietly influence the NEXT owned/enabled frame's
// hysteresis/PI state).
//
// `detail` is EMPTY on both bypasses, not populated-with-zeros. This is a
// deliberate, separately pinned choice (test_AdasCoexistenceStack.cpp):
// a bypassed frame emitting e.g. gt.aeb.driver_brake=0.000/gt.aeb.brake_out=
// 0.000 would be indistinguishable, from the OSI stream alone, from a frame
// where the stack actually ran, measured a driver brake of exactly 0.0, and
// applied exactly 0.0 -- i.e. a genuine (negative) "AEB looked and found
// nothing" observation. Those are different facts (one is "did not look" and
// the other is "looked, found nothing") and the E2E matchers (verification
// plan §4-2) need to tell them apart; an absent key is the only
// unambiguous way to say "not reported this frame".
//
// ============================================================================
// RESULT DETAIL -- merge rule (do NOT merge the warning snapshot's detail)
// ============================================================================
// ManualAdasFrameResult::detail = intervention.detail, COPIED VERBATIM (the
// exact gt.aeb.gap_m / v_close_mps / lead_osi_id / ttc_s / a_req_mps2 /
// triggered keys AebSafety's own W3 diagnostics emit -- see AebSafety.cpp),
// PLUS the manual stack's own additions -- design §8-4 plus the two below:
//
//   gt.aeb.warning             bool    FCW pre-stage flag
//   gt.aeb.decel_request_mps2  double  this frame's a_req (0.0 = none)
//   gt.aeb.brake_request       double  pedals.aeb_brake_request (raw safety demand)
//   gt.aeb.suppressed          bool    pedals.aeb_suppressed (kickdown override)
//   gt.aeb.kickdown            bool    kickdown detector's RAW verdict (independent
//                                      of cfg.kickdown_suppress_enabled -- MSL, phase
//                                      C, reads the same shared signal)
//   gt.aeb.driver_brake        double  driver_cmd.brake, RAW, [0,1] unitless
//   gt.aeb.brake_out           double  pedals.brake_out, the brake ACTUALLY applied
//
// gt.aeb.driver_brake/brake_out exist because HostVehicleData's input block
// (OSI HVD) carries only the EFFECTIVE brake -- the raw driver value is not
// observable downstream of this stack unless it is reported here. Without
// it, the `brake_not_stacked` E2E matcher (verification plan §4-2,
// req-vd-ad:REQ-AD-025 step c: "the human's strong brake is never weakened,
// only topped up") would have to re-read the input PROFILE FILE to
// reconstruct what the driver did, i.e. judge the run against the test's own
// intent rather than against what the vehicle actually did -- the
// "instrument does not represent reality" failure mode this project has a
// documented history of avoiding. Both keys are emitted on EVERY frame the
// stack actually runs, firing or quiet: `brake_not_stacked`'s negative case
// (md-aeb-no-false-intervention) needs them precisely on the quiet frames,
// and a key that only appears when something fired would make "absent" mean
// two different things (see the bypass note above). On firing frames these
// two keys satisfy gt.aeb.brake_out == max(gt.aeb.driver_brake,
// gt.aeb.brake_request) -- PedalArbitrator's own max-composition claim
// (§3-1), restated in the observable OSI domain instead of only in the
// arbitrator's C++ internals.
//
// The WARNING snapshot's OWN detail is intentionally dropped: both AebSafety
// instances emit the SAME gt.aeb.* key set from their own Evaluate() call
// (they are the same policy, just parameterized differently), so merging
// both would leave two contradictory values for the same key in one
// custom_detail map with no way for a consumer to tell which is which. The
// warning path's only visible trace in `detail` is the derived
// gt.aeb.warning boolean.

#include "gt_esmini/control/common/VehicleCommand.hpp"
#include "gt_esmini/control/manualdrive/KickdownDetector.hpp"
#include "gt_esmini/control/manualdrive/PedalArbitrator.hpp"
#include "gt_esmini/control/virtualdriver/AdasFunctionReport.hpp"
#include "gt_esmini/control/virtualdriver/ITrafficPolicy.hpp"
#include "gt_esmini/control/virtualdriver/TrafficPolicyManager.hpp"
#include "gt_esmini/control/virtualdriver/VirtualDriverTypes.hpp"
#include "gt_esmini/control/virtualdriver/policies/AebSafety.hpp"

#include <memory>

namespace gt_esmini
{

// Minimum stop distance below which a_req = v^2/(2*d) is NOT evaluated
// directly -- see the "REQUIRED DECELERATION" block above -- and a saturating
// (full-brake) request is emitted instead, to avoid dividing by a near-zero
// distance. REQUIRES CALIBRATION (verification plan §5) only in the loose
// sense that its exact value has not been checked against a real braking/
// backend model: it only needs to stay comfortably below AebSafetyConfig's
// default stop_margin (2.0 m) and comfortably above floating-point noise in
// PolicyConstraint::s.
constexpr double kMinStopDistanceM = 0.5;  // [m]

// Phase A config (design §9's "adas.aeb" JSON section, minus the JSON
// plumbing itself -- ManualDriveConfig owns that).
struct ManualAdasStackConfig
{
    bool aeb_enabled              = false;  // design §9: default OFF for every ADAS function
    bool kickdown_suppress_enabled = true;  // design §3-2/§3-3: real-vehicle-style driver override

    // REQUIRES CALIBRATION (verification plan §5): design §3-2 targets a
    // warning lead of >= 0.8s ahead of intervention. These defaults satisfy
    // the LOOSENESS invariant DeriveFcwGateConfig() enforces (ttc_threshold
    // strictly larger, min_a_req strictly smaller than AebSafetyConfig's own
    // defaults: ttc_threshold=2.5, min_a_req=3.0) but are not measured
    // against real driving -- the verification plan's timed/red-asset batch
    // is what fixes them.
    double warning_ttc_threshold_s = 3.3;  // [s], > AebSafetyConfig default 2.5
    double warning_min_a_req_mps2  = 2.0;  // [m/s^2], < AebSafetyConfig default 3.0

    AebSafetyConfig         aeb;        // intervention-path thresholds (policy defaults)
    KickdownDetectorConfig  kickdown;   // shared detector config (design §3-3)
    PedalArbitratorConfig   arbitrator; // safety-stage pedal conversion config (design §3-4)
};

// One frame's stack output.
struct ManualAdasFrameResult
{
    PedalArbitrationSnapshot pedals;                       // what to apply this frame
    ManualAdasDecision       decision;                      // -> BuildManualAdasFunctionReport (AdasFunctionReport.hpp)
    PolicyDetail             detail;                        // merged diagnostics -> custom_detail (see merge rule above)
    double                   aeb_decel_request_mps2 = 0.0;  // this frame's a_req (0.0 = none requested)
};

// Achieved deceleration from two consecutive speed samples, POSITIVE when
// slowing -- matches PedalArbitrator.hpp's measured_decel_mps2 sign
// convention verbatim (that header's SIGN CONVENTION block: a NEGATIVE value
// means the vehicle is accelerating, not braking gently). dt<=0 -> 0.0 (no
// rate can be attributed to a non-positive time step -- same discipline as
// AdSteeringEnvelope's dt handling).
double ComputeMeasuredDecel(double v_now, double v_prev, double dt);

// Builds the WARNING (FCW) AebSafety instance's config from cfg.aeb: every
// field is copied verbatim except ttc_threshold/min_a_req, which come from
// cfg.warning_ttc_threshold_s/warning_min_a_req_mps2 and are then CLAMPED so
// they can never end up tighter than cfg.aeb's own thresholds -- see this
// header's "THE TWO AebSafety INSTANCES" block for why that invariant is what
// makes "warning fires no later than intervention" a structural guarantee
// rather than a tuning coincidence.
//
// GUARD CHOICE: clamp, not reject. This runs once at AdasCoexistenceStack
// construction time from a JSON-sourced config (design §9); throwing/
// aborting on a bad config value would turn a config mistake into a process
// crash on the ManualDrive control path, and silently honouring the caller's
// (wrong, tighter) numbers would break the superset guarantee INVISIBLY --
// exactly the undiagnosable "the warning fired late" failure mode this
// function exists to prevent. Clamping preserves the invariant
// unconditionally; at the degenerate clamped-to-equality boundary the warning
// may fire on the SAME frame as intervention (never later), which still
// satisfies the (non-strict) superset property this file's unit tests pin.
AebSafetyConfig DeriveFcwGateConfig(const ManualAdasStackConfig& cfg);

// The pure decision core -- see this header's top-of-file comment blocks for
// the full semantics (two-instance FCW mechanism, a_req derivation, domain
// bypass, detail merge rule). No esmini types: only TrafficPolicySnapshot /
// PedalSteerCommand / plain doubles, so every behavior is testable without
// the engine (test_AdasCoexistenceStack.cpp).
//
//   cfg                 : phase-A config (ManualAdasStackConfig above).
//   owns_longitudinal   : DomainOwnershipLedger::OwnerOf(..., LONGITUDINAL)
//                         result for THIS controller, as decided by the
//                         caller -- this function does not call the ledger
//                         itself (design §2-3; the caller passes the bool).
//   intervention        : this frame's TrafficPolicyManager::Evaluate(ctx)
//                         result (the intervention-path AebSafety, plus any
//                         later phase C/D policies once added to policies_).
//   warning              : this frame's fcw_gate_->Evaluate(ctx) result (the
//                         SECOND, looser-threshold AebSafety instance).
//   driver_cmd          : this frame's human pedal/steer command, PRE-ADAS.
//   ego_speed_mps       : ego's own ground speed (NOT AebSafety's internal
//                         closing speed -- see the a_req derivation block).
//   measured_decel_mps2 : this frame's ComputeMeasuredDecel() output.
//   dt                  : seconds since the last call.
//   kickdown            : the SHARED detector (design §3-3). Update()d with
//                         driver_cmd.throttle exactly once per call, UNLESS
//                         this call takes the domain-bypass early return (see
//                         "DOMAIN OWNERSHIP" above), in which case its state
//                         is left untouched.
//   arbitrator          : the SAFETY-stage pedal arbitrator (design §3-1).
//                         Arbitrate()d exactly once per call, same bypass
//                         exception.
ManualAdasFrameResult ComputeManualAdasFrame(const ManualAdasStackConfig& cfg,
                                              bool                         owns_longitudinal,
                                              const TrafficPolicySnapshot& intervention,
                                              const TrafficPolicySnapshot& warning,
                                              const PedalSteerCommand&     driver_cmd,
                                              double                       ego_speed_mps,
                                              double                       measured_decel_mps2,
                                              double                       dt,
                                              KickdownDetector&            kickdown,
                                              PedalArbitrator&             arbitrator);

// Engine-facing wrapper (b): owns the policies + stateful helpers, builds the
// TrafficPolicyContext-consuming calls, and delegates every decision to
// ComputeManualAdasFrame. This class's own body is deliberately close to
// trivial; see this header's top-of-file comment for why the split exists.
class AdasCoexistenceStack
{
public:
    explicit AdasCoexistenceStack(const ManualAdasStackConfig& cfg = {});

    // One ManualDrive frame. Insertion point (coordinator wiring, design
    // §2-2): call this AFTER cmd.{steering,throttle,brake} is assembled from
    // driver input and BEFORE DomainOwnershipLedger::PublishLateral/
    // PublishLongitudinal (ManualDriveCoordinator::RunFrame, "cmd assembled"
    // block just before the bus-publish calls) -- ComputeManualAdasFrame's
    // output (result.pedals) is what must reach the bus, not the raw driver
    // cmd, otherwise a split-domain peer consuming the bus never sees the
    // ADAS-arbitrated value (design §2-2's whole reason for this ordering).
    //
    //   ctx               : ego/entities/sim_time for this frame -- passed to
    //                       both AebSafety instances unmodified. policies_
    //                       and fcw_gate_ are evaluated EVERY frame regardless
    //                       of owns_longitudinal (Evaluate() is cheap pure
    //                       geometry and keeps AebSafety's cross-frame
    //                       dt_history_ encroachment debounce warm across an
    //                       ownership hand-off -- design §12's dynamic-
    //                       ownership risk item); the ARBITRATION bypass
    //                       ComputeManualAdasFrame performs is what design
    //                       §2-3 actually requires, not withholding
    //                       Evaluate() itself.
    //   owns_longitudinal : this controller's current LONGITUDINAL ownership
    //                       (DomainOwnershipLedger::OwnerOf), decided by the
    //                       caller (design §2-3).
    //   driver_cmd        : this frame's pre-ADAS pedal/steer command.
    //   dt                : seconds since the last call.
    ManualAdasFrameResult Step(const TrafficPolicyContext& ctx,
                                bool                        owns_longitudinal,
                                const PedalSteerCommand&    driver_cmd,
                                double                      dt);

    const ManualAdasStackConfig& Config() const
    {
        return cfg_;
    }

private:
    ManualAdasStackConfig cfg_;

    // INTERVENTION path (see this header's top-of-file comment). Holds ONE
    // AebSafety in phase A, added in the constructor only when
    // cfg_.aeb_enabled; phases C add lead/traffic-light/stop-sign here.
    TrafficPolicyManager policies_;

    // WARNING path: a SECOND, independent AebSafety instance with looser gate
    // thresholds (DeriveFcwGateConfig). Null when cfg_.aeb_enabled is false
    // -- Step() must not evaluate it in that case (mirrors policies_ staying
    // empty).
    std::unique_ptr<AebSafety> fcw_gate_;

    KickdownDetector kickdown_;
    PedalArbitrator  arbitrator_;

    // ComputeMeasuredDecel's v_prev anchor, carried across frames.
    double prev_speed_mps_ = 0.0;
};

}  // namespace gt_esmini
