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
    };
    struct ObjectSlots
    {
        Slot slot[static_cast<unsigned int>(OwnedDomain::COUNT)];
    };

    const Slot* Find(int object_id, OwnedDomain domain) const;

    std::unordered_map<int, ObjectSlots> objects_;
};

}  // namespace gt_esmini
