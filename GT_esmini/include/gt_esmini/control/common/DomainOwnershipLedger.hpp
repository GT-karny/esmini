#pragma once

// feature:F7 — GT-side authority on "which controller owns which control
// domain of which object".
//
// WHY THIS EXISTS (upstream defect A — cannot be fixed under R1 Clean Core)
// ------------------------------------------------------------------------
// esmini's per-domain conflict resolution in ActivateControllerAction::Start()
// (OSCPrivateAction.cpp, the `>= osc v1.3` branch) finds the *incumbent*
// controller holding a requested domain and then calls DeactivateDomains() on
// `controller_` — the *incoming* controller — instead of on that incumbent.
// The incumbent therefore never gives up the domain it was supposed to lose,
// and both controllers report themselves active on it. Since
// ScenarioEngine steps every Active() controller in declaration order and each
// GT controller integrates its own physics, whichever one steps last silently
// takes over every domain.
//
// The consequence for this module: Controller::GetActiveDomains() answers
// "do I believe I am active?", which is NOT the same question as "am I the one
// entitled to write this domain?". Only the second question can arbitrate, and
// upstream has no state that answers it. This ledger is that state.
//
// ARBITRATION RULE — last claimer wins
// ------------------------------------
// A controller Claim()s the domain mask it was just activated on. Domains in
// the mask become owned by that controller, evicting whoever held them. Domains
// absent from the mask are released only if the claiming controller itself
// currently owns them — a controller can give up what it holds but can never
// evict a peer by omission. That rule makes the outcome independent of the
// order in which the two ActivateControllerActions happen to run, which is the
// property upstream's own bookkeeping lacks.
//
// SCOPE: this header records ownership. It does not decide what a controller
// does with the answer; see each controller's Step().

#include "CommonMini.hpp"
#include "gt_esmini/control/manualdrive/IFFBSink.hpp"  // FfbInterventionSample (return path)

#include <string>
#include <unordered_map>

namespace gt_esmini
{

/// The control domains this ledger arbitrates. Deliberately narrower than
/// esmini's ControlDomains (which also carries LIGHT/ANIM): only the two
/// domains that contend for the vehicle's motion state are tracked here.
enum class OwnedDomain
{
    LATERAL      = 0,  ///< steering
    LONGITUDINAL = 1,  ///< throttle / brake
    COUNT        = 2
};

const char* OwnedDomainToStr(OwnedDomain domain);

class DomainOwnershipLedger
{
public:
    static DomainOwnershipLedger& Instance();

    /// Record `controller`'s claim over `object_id`.
    /// @param domain_mask esmini ControlDomainMasks bits (LONG=0x1, LAT=0x2).
    ///                    Bits outside those two are ignored.
    /// Domains set in the mask become owned by `controller`; domains not set
    /// are released iff `controller` is their current owner.
    void Claim(int object_id, const void* controller, const std::string& controller_name, unsigned int domain_mask);

    /// Release every domain `controller` currently owns on `object_id`.
    /// Domains owned by someone else are left alone.
    void ReleaseAll(int object_id, const void* controller);

    bool        IsOwner(int object_id, const void* controller, OwnedDomain domain) const;
    bool        HasOwner(int object_id, OwnedDomain domain) const;
    const void* OwnerOf(int object_id, OwnedDomain domain) const;
    std::string OwnerName(int object_id, OwnedDomain domain) const;

    /// The one controller allowed to advance this object's body this frame.
    ///
    /// Every GT controller carries its own physics backend, so "both may write"
    /// means two independently integrated bodies race to stamp object->pos_ and
    /// the last one to Step() wins the whole vehicle. Exactly one integrator has
    /// to be picked, and picking it from the ledger (rather than from Step order)
    /// is what makes the outcome independent of declaration order.
    ///
    /// Rule, in order:
    ///   1. the LONGITUDINAL owner, if any;
    ///   2. otherwise the LATERAL owner, if any;
    ///   3. otherwise nobody.
    ///
    /// Longitudinal wins because pose advance is dominated by speed and because
    /// HVDStateApplier writes pose and speed together: keeping both from one body
    /// is what holds reported speed and travelled distance consistent. Splitting
    /// them is the documented 1.32-ratio failure this whole design exists to
    /// avoid.
    ///
    /// A controller that is Active() but owns nothing — which upstream defect A
    /// makes reachable — is not the integrator and must not write.
    const void* IntegratorOf(int object_id) const;
    bool        IsIntegrator(int object_id, const void* controller) const;

    // ---- feature:F7 S3: the command bus -------------------------------------
    //
    // The merge happens HERE, at the command stage, and the integrator then runs
    // ONE physics step over the merged command. The alternative — letting each
    // controller integrate its own body and merging the resulting states (A's
    // pose, B's speed) — produces a vehicle whose reported speed and actual
    // ground speed disagree by 32%, with the wrong number published to OSI, and
    // it looks entirely healthy in the speed column. That failure is the reason
    // this bus exists rather than a state-stage merge.
    //
    // Each domain owner publishes the channel it owns; the integrator consumes
    // the channels it does NOT own and supplies its own for the rest. Publishing
    // is refused from a non-owner, so a controller that upstream defect A left
    // spuriously Active() cannot inject commands.
    //
    // FRAME ALIGNMENT: controllers Step in declaration order, so the integrator
    // reads a channel published either earlier in this same frame (owner steps
    // first) or in the previous one (owner steps second) — at most one timestep
    // of lag, and which one depends on declaration order. The lag is bounded and
    // does not change the steady state; the committed matrix asserts both orders
    // pass, rather than assuming order does not matter.

    /// Publish this frame's steering. Ignored unless `controller` owns LATERAL.
    /// `manual` is the publisher's own override_mgr_.IsLateralManual() —
    /// carried alongside the value so a device holder that is NOT the lateral
    /// owner (reverse split) can tell whether the servo should actively track
    /// this command or release the wheel (see ConsumeLateral / the servo-target
    /// call site in ManualDriveCoordinator). Meaningless for a LONGITUDINAL-only
    /// publisher; ignored there.
    void PublishLateral(int object_id, const void* controller, double steering, bool manual);

    /// Publish this frame's throttle/brake. Ignored unless `controller` owns
    /// LONGITUDINAL.
    void PublishLongitudinal(int object_id, const void* controller, double throttle, double brake);

    /// Read the owner-published channel. Returns false when the domain has no
    /// owner, or has one that has not published yet — in which case the caller
    /// keeps its own value rather than steering/accelerating by a stale command.
    /// `manual` mirrors the publisher's own lateral-manual state (see
    /// PublishLateral); undefined when the call returns false.
    bool ConsumeLateral(int object_id, double& steering, bool& manual) const;
    bool ConsumeLongitudinal(int object_id, double& throttle, double& brake) const;

    // ---- RETURN PATH: raw device axis ----------------------------------------
    //
    // Separate from PublishInterventionSample below: that struct is zeroed to
    // {} the instant the FFB servo goes inactive (by design — see
    // FfbInterventionSample's "active" semantics), so it cannot answer "where
    // is the wheel right now" once the servo has released it — which is
    // exactly the moment a reverse-split lateral owner with no device of its
    // own (VirtualDriver, input_type=stub) needs that answer: once its own
    // OverrideManager latches MANUAL, it must keep publishing the DRIVER's
    // real wheel position on the lateral bus (so ManualDriveCoordinator's
    // physics step actually follows the driver), not its own stale/zero local
    // input frame. This channel carries that raw position unconditionally,
    // straight from the device holder's InputFrame.pedal_steer.steering —
    // valid whether or not the FFB servo is currently tracking anything.
    //
    // Not gated on ownership, same reasoning as PublishInterventionSample: the
    // publisher is whoever physically holds the device, a different question
    // from who owns a control domain.
    void PublishDeviceAxis(int object_id, double axis);
    bool ConsumeDeviceAxis(int object_id, double& axis) const;

    // ---- RETURN PATH: force-feedback intervention sample ---------------------
    //
    // The command bus above carries commands OUTWARD (owner -> integrator). This
    // carries the servo's intervention sample BACK (device holder -> lateral
    // owner), and it is needed for exactly the same reason: under a split, the
    // controller that owns the steering domain is not the one holding the wheel.
    //
    // VirtualDriver runs input_type=stub, so its own GetFFBSink() is nullptr and
    // its `if (ffb)` guard drops the sample before OverrideManager ever sees it.
    // Without this hop the residual-based takeover detector has NO INPUT: the
    // shadow model predicts where an unloaded wheel would be given the force the
    // servo applied, so with no force sample there is no prediction, no residual,
    // and the latch can never fire. Measured on the real G29 in the reverse
    // split: target_track enabled=true in the log, |tt| = 0.0000 all run, and
    // turning the wheel by hand produced no override.
    //
    // Not gated on ownership: the publisher is whoever physically holds the
    // device, which is a different question from who owns a control domain.
    // The consumer side is where the lateral-owner check belongs.
    void PublishInterventionSample(int object_id, const FfbInterventionSample& sample);
    bool ConsumeInterventionSample(int object_id, FfbInterventionSample& out) const;

    /// Human-readable one-liner, e.g. `obj=0 lat=MD lon=VD`. Used for the
    /// per-frame debug trace; unowned domains render as `<none>`.
    std::string Describe(int object_id) const;

    /// Scenario teardown. Object ids and controller addresses are both reused
    /// across runs inside one process (the web backend and gt_sim_test run many
    /// scenarios per process), so a stale entry could otherwise make a fresh
    /// controller look like it already owns — or already lost — a domain.
    void Clear();

private:
    DomainOwnershipLedger() = default;

    struct Slot
    {
        const void* owner = nullptr;
        std::string name;

        // S3 command bus. `published` is cleared whenever the domain changes
        // hands, so the new owner's first frame can never be driven by the
        // previous owner's last command.
        bool   published = false;
        double steering  = 0.0;
        double throttle  = 0.0;
        double brake     = 0.0;
        bool   manual    = false;  // LATERAL only; see PublishLateral
    };
    struct ObjectSlots
    {
        Slot slot[static_cast<unsigned int>(OwnedDomain::COUNT)];

        // Return path. Per-object, not per-domain: there is one physical wheel.
        FfbInterventionSample ffb_sample;
        bool                  ffb_sample_published = false;

        // Return path: raw device axis (see PublishDeviceAxis).
        double device_axis           = 0.0;
        bool   device_axis_published = false;
    };

    const Slot* Find(int object_id, OwnedDomain domain) const;

    std::unordered_map<int, ObjectSlots> objects_;
};

}  // namespace gt_esmini
