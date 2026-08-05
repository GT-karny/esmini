#include "gt_esmini/control/virtualdriver/AdasFunctionReport.hpp"

namespace gt_esmini
{

namespace
{
// One VD policy -> one OSI AD-function row.
//
// `sources` are the PolicyConstraint::source strings the policy emits (see each
// policy .cpp). StopYieldSignAware emits two ("stop_sign" / "yield_sign") from a
// single policy, which is why this is a list rather than a single string.
//
// `key_prefix` routes W3 diagnostics (gt.<policy>.*) onto the owning row, so a
// consumer reading the AEB function also gets the TTC / a_req that produced its
// state without having to correlate anything.
struct PolicyRow
{
    const char* custom_name;
    int         osi_name;
    const char* key_prefix;
    const char* sources[2];
};

// Only AEB and lead-vehicle following have a standard OSI name. The junction /
// crossing / signal policies are genuinely absent from the 24-value enum, so
// they take NAME_OTHER + custom_name rather than being force-fitted onto a
// nearby-sounding standard value (which would misreport GT_esmini's behavior to
// any consumer that trusts the enum).
constexpr PolicyRow kRows[] = {
    {"gt.aeb",            osi_adas::NAME_AUTOMATIC_EMERGENCY_BRAKING, "gt.aeb.",            {"aeb", nullptr}},
    {"gt.lead_vehicle",   osi_adas::NAME_ADAPTIVE_CRUISE_CONTROL,     "gt.lead_vehicle.",   {"lead_vehicle", nullptr}},
    {"gt.traffic_light",  osi_adas::NAME_OTHER,                       "gt.traffic_light.",  {"traffic_light", nullptr}},
    {"gt.stop_yield",     osi_adas::NAME_OTHER,                       "gt.stop_yield.",     {"stop_sign", "yield_sign"}},
    {"gt.conflict_point", osi_adas::NAME_OTHER,                       "gt.conflict_point.", {"conflict_point", nullptr}},
    {"gt.crosswalk",      osi_adas::NAME_OTHER,                       "gt.crosswalk.",      {"crosswalk", nullptr}},
};

bool EnabledFor(const VdPolicyEnableFlags& f, const char* custom_name)
{
    const std::string n = custom_name;
    if (n == "gt.aeb") return f.aeb;
    if (n == "gt.lead_vehicle") return f.lead;
    if (n == "gt.traffic_light") return f.traffic_light;
    if (n == "gt.stop_yield") return f.stop_yield;
    if (n == "gt.conflict_point") return f.conflict;
    if (n == "gt.crosswalk") return f.crosswalk;
    return false;
}

bool EmittedThisFrame(const TrafficPolicySnapshot& snapshot, const PolicyRow& row)
{
    for (const auto& c : snapshot.constraints)
        for (const char* src : row.sources)
            if (src && c.source == src) return true;
    return false;
}

bool StartsWith(const std::string& s, const char* prefix)
{
    const std::string p = prefix;
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}
}  // namespace

std::vector<AdasFunctionState> BuildAdasFunctionReport(const VdPolicyEnableFlags&   flags,
                                                       const TrafficPolicySnapshot& snapshot)
{
    std::vector<AdasFunctionState> report;
    report.reserve(sizeof(kRows) / sizeof(kRows[0]) + 1);

    for (const auto& row : kRows)
    {
        AdasFunctionState f;
        f.name        = row.osi_name;
        f.custom_name = row.custom_name;

        // UNAVAILABLE (not configured) / STANDBY (armed, quiet) / ACTIVE
        // (constrained the plan this frame). The STANDBY-vs-UNAVAILABLE split is
        // the whole point: "AEB was watching and chose not to fire" and "AEB was
        // never switched on" are different verdicts about the same silence.
        if (!EnabledFor(flags, row.custom_name))
            f.state = osi_adas::STATE_UNAVAILABLE;
        else if (EmittedThisFrame(snapshot, row))
            f.state = osi_adas::STATE_ACTIVE;
        else
            f.state = osi_adas::STATE_STANDBY;

        for (const auto& kv : snapshot.detail)
            if (StartsWith(kv.first, row.key_prefix)) f.detail.push_back(kv);

        report.push_back(std::move(f));
    }

    // Aggregate row: the VD stack itself is driving the vehicle. Reported
    // independently of any single policy, so a consumer can tell "an automated
    // driving function has control" from "this particular assist fired".
    AdasFunctionState vd;
    vd.name        = osi_adas::NAME_URBAN_DRIVING;
    vd.custom_name = "gt.virtual_driver";
    vd.state       = osi_adas::STATE_ACTIVE;
    report.push_back(std::move(vd));

    return report;
}

// ============================================================================
// ManualDrive ADAS coexistence report (req-vd-ad:REQ-AD-025 REQ-AD-028,
// vd-func:FUNC-075, phase A). See AdasFunctionReport.hpp for the full design
// rationale (no aggregate row, decision-boolean input, domain-ownership gate).
// ============================================================================
std::vector<AdasFunctionState> BuildManualAdasFunctionReport(const ManualAdasEnableFlags& flags,
                                                              bool                         owns_longitudinal_domain,
                                                              const ManualAdasDecision&    decision,
                                                              const PolicyDetail&          detail)
{
    std::vector<AdasFunctionState> report;
    report.reserve(4);  // AEB + FCW always; ACC / MSL when config-enabled (phase C)

    // design §2-3 (slug md-split-no-double-equipment): not owning the
    // longitudinal domain collapses to the same UNAVAILABLE verdict as a
    // config-disabled function -- both AEB and FCW are longitudinal-domain
    // functions in phase A, so the same ownership flag gates both rows.
    const bool domain_gate = owns_longitudinal_domain;

    // AEB (design §8-2: NAME_AUTOMATIC_EMERGENCY_BRAKING). State follows the
    // 3-value discipline: UNAVAILABLE (off / not owning) / STANDBY (armed,
    // quiet) / ACTIVE (intervening this frame).
    {
        AdasFunctionState f;
        f.name        = osi_adas::NAME_AUTOMATIC_EMERGENCY_BRAKING;
        f.custom_name = "gt.aeb";

        const bool gate_open = flags.aeb && domain_gate;

        if (!gate_open)
            f.state = osi_adas::STATE_UNAVAILABLE;
        else if (decision.aeb_intervening)
            f.state = osi_adas::STATE_ACTIVE;
        else
            f.state = osi_adas::STATE_STANDBY;

        // req-vd-ad:REQ-AD-028 段b -- see the header's DRIVER OVERRIDE block.
        // reported only while the gate is open (a function that was never
        // running cannot have been overridden); active + custom_state only
        // for the accelerator-origin override, whose `reasons` stays empty
        // because OSI's Reason enum has no accelerator value.
        if (gate_open)
        {
            f.driver_override.reported = true;
            f.driver_override.active   = decision.driver_override_accel;
            if (decision.driver_override_accel)
            {
                f.custom_state = kDriverOverrideAccel;
            }
        }

        // design §8-4: gt.aeb.* diagnostics (ttc_s / a_req_mps2 / triggered /
        // ..., plus gt.aeb.warning -- the FCW flag; see below) all route here
        // by key prefix, regardless of this row's own state -- a consumer
        // reading a STANDBY or UNAVAILABLE AEB row still gets the numbers
        // that produced the verdict (same convention as BuildAdasFunctionReport
        // above).
        for (const auto& kv : detail)
            if (StartsWith(kv.first, "gt.aeb.")) f.detail.push_back(kv);

        report.push_back(std::move(f));
    }

    // FCW (design §8-2: NAME_FORWARD_COLLISION_WARNING). Built from the same
    // AebSafety output as AEB, one threshold earlier (design §3-2): a frame
    // can have fcw_warning=true while aeb_intervening stays false (warning
    // precedes intervention, REQ-AD-025 step e), so this row's state is
    // computed independently of the AEB row's above, from decision.fcw_warning.
    {
        AdasFunctionState f;
        f.name        = osi_adas::NAME_FORWARD_COLLISION_WARNING;
        f.custom_name = "gt.fcw";

        const bool gate_open = flags.fcw && domain_gate;

        if (!gate_open)
            f.state = osi_adas::STATE_UNAVAILABLE;
        else if (decision.fcw_warning)
            f.state = osi_adas::STATE_ACTIVE;
        else
            f.state = osi_adas::STATE_STANDBY;

        // req-vd-ad:REQ-AD-028 段b: evaluated, never active. Kickdown
        // suppresses INTERVENTION, not the WARNING -- see the header's DRIVER
        // OVERRIDE block for why, and why this row is the in-run negative
        // control for the accelerator override on the AEB row above.
        if (gate_open)
        {
            f.driver_override.reported = true;
        }

        // No "gt.fcw." keys exist yet in phase A (design §8-4's table has no
        // FCW-specific quantity; gt.aeb.warning -- the FCW flag itself --
        // routes to the AEB row above by key prefix, intentionally, per the
        // design table). Routing "gt.fcw." here anyway (rather than omitting
        // it) keeps this row symmetric with AEB's and future-proofs it for a
        // later phase that adds one, at zero cost today since no caller emits
        // such a key.
        for (const auto& kv : detail)
            if (StartsWith(kv.first, "gt.fcw.")) f.detail.push_back(kv);

        report.push_back(std::move(f));
    }

    // ---- phase C rows (req-vd-ad:REQ-AD-026 / 030 / 031) -------------------
    // Emitted ONLY when the corresponding function is config-enabled, so a
    // phase-A/B config keeps producing exactly the two rows above and the
    // committed ManualDrive baselines are unmoved. See the header's PHASE C
    // ROWS block for the state mapping and for why "switched off by the
    // driver" collapses onto UNAVAILABLE.
    auto stateful_row = [&](int name, const char* custom_name, const char* detail_prefix, int fn_state,
                            bool override_brake_reason, bool override_accel_token)
    {
        AdasFunctionState f;
        f.name        = name;
        f.custom_name = custom_name;

        // Not owning the longitudinal domain collapses to UNAVAILABLE exactly
        // as it does for AEB/FCW above (slug md-split-no-double-equipment) --
        // the row is still EMITTED so a split run can be seen to have declined,
        // rather than the function silently disappearing from the stream.
        // `fn_state` 0 (driver has not switched it on) reports UNAVAILABLE too
        // -- the collapse the header documents.
        if (!domain_gate || fn_state == 0)
            f.state = osi_adas::STATE_UNAVAILABLE;
        else if (fn_state == 2)
            f.state = osi_adas::STATE_ACTIVE;
        else
            f.state = osi_adas::STATE_STANDBY;

        // req-vd-ad:REQ-AD-028 段b. `reported` follows the CONFIG+DOMAIN gate,
        // NOT the function's own on/off state: a config-enabled function on an
        // owned domain really was evaluated for an override this frame, so "no
        // override" is a measurement even while the driver has it switched
        // off. This is also what keeps the two facts the header's state
        // collapse merges (never installed vs installed-and-driver-off)
        // separable from outside.
        if (domain_gate)
        {
            f.driver_override.reported = true;
            f.driver_override.active   = override_brake_reason || override_accel_token;
            if (override_brake_reason)
            {
                f.driver_override.reasons.push_back(osi_adas::REASON_BRAKE_PEDAL);
            }
            if (override_accel_token)
            {
                f.custom_state = kDriverOverrideAccel;
            }
        }

        for (const auto& kv : detail)
            if (StartsWith(kv.first, detail_prefix)) f.detail.push_back(kv);

        report.push_back(std::move(f));
    };

    if (flags.acc)
    {
        // Both override producers can be live at once (a driver with one foot
        // on each pedal), so `active` is their OR and the two channels -- a
        // Reason for the brake, a custom_state token for the accelerator --
        // are populated independently. Collapsing them would force a choice
        // between two true statements.
        stateful_row(osi_adas::NAME_ADAPTIVE_CRUISE_CONTROL, "gt.acc", "gt.acc.", decision.acc_state,
                     decision.acc_driver_override_brake, decision.acc_driver_override_accel);
    }

    if (flags.msl)
    {
        stateful_row(osi_adas::NAME_SPEED_LIMIT_CONTROL, "gt.msl", "gt.msl.", decision.msl_state,
                     /*override_brake_reason=*/false, decision.msl_driver_override_accel);
    }

    // No aggregate row here -- see BuildManualAdasFunctionReport's doc comment
    // in AdasFunctionReport.hpp for why (unlike BuildAdasFunctionReport above,
    // which does add one).

    return report;
}

}  // namespace gt_esmini
