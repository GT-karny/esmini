// feature:F7 — FFB override-detector replay harness.
//
// WHAT THIS IS FOR. The lateral intervention latch (OverrideManager::Update,
// see OverrideManager.cpp) compares the measured wheel axis against a shadow
// plant's prediction of an UNHELD wheel; the difference ("residual") sustained
// above override_residual_threshold for override_sustain_time latches to
// MANUAL. Re-evaluating that detector's numbers under a candidate config
// change (measured theta/tau, onset grace, ...) requires re-running it against
// real recorded telemetry — and until this file existed, there was no way to
// do that. A past report table (4 configurations x 3 real-wheel runs) exists
// in test_results/, but the tool that produced it is gone; per this project's
// discipline, a number with no traceable producer is not a measurement. This
// tool is that producer, made permanent.
//
// WHY THE SHADOW IS NOT REIMPLEMENTED HERE. scripts/ffb_spike/shadow_margin_
// report.py states its own operating principle plainly: never reimplement the
// shadow, keep exactly one source of truth, the shipped product code. A
// second implementation (there was one, scripts/ffb_spike/shadow_replay.py;
// it was deliberately deleted) can only ever prove that two copies of the
// same idea agree with each other, never that the shipped detector behaves
// the way the report claims. So this harness does not touch the shadow math
// at all: it drives the ACTUAL gt_esmini::OverrideManager class — the same
// object ControllerVirtualDriver constructs — through OverrideManager::
// Configure() / UpdateFfbSample() / Update(), and reads its results back
// through the public accessors (IsLateralManual(), GetFfbLatchDiagnostics()).
// Every number this tool prints is a real product code path, run outside the
// simulator loop but not reimplemented from it.
//
// THE ONE-ROW GATES LAG — READ THIS BEFORE TOUCHING ANY FIELD MAPPING BELOW.
// One recorded JSONL line holds TWO INSTANTS, not one. The top-level "ffb"
// block (target_active/commanded_force/position_error/target_norm) is the FFB
// sink's sample AFTER that frame's update. "ffb.gates" is a DIFFERENT
// instant: OverrideManager's diagnostic, computed from the sample it was
// handed BEFORE that update -- i.e. the sample that was written into the
// PREVIOUS line's "ffb" block (ControllerVirtualDriver calls UpdateFfbSample()
// then Update() each frame, but the telemetry snapshot for line N's "gates" is
// taken from state Update() left behind for the PRIOR frame's sample by the
// time line N is written; see VirtualDriverTelemetryJson.cpp's own header
// note, added alongside this fix). Measured directly on
// f7_realwheel_basic.jsonl, three consecutive frames, exact to 4 decimals:
//     line 6.05: target_norm-position_error = -0.0150, gates.actual_norm =  0.0000
//     line 6.06: target_norm-position_error = -0.0189, gates.actual_norm = -0.0150
//     line 6.07: target_norm-position_error = -0.0224, gates.actual_norm = -0.0188
// i.e. gates.actual_norm(N) == (target_norm - position_error)(N-1), exactly.
//
// THIS IS DANGEROUS PRECISELY BECAUSE IT HIDES. While the wheel sits at 0 (the
// stationary prologue of every one of these recordings) the two instants
// agree, so pairing "ffb" and "ffb.gates" from the SAME line validates
// perfectly for as long as nothing is moving -- and is silently one frame off
// for every frame where the wheel actually moves, which is the only part
// anyone cares about. This was caught, not assumed: the load-time actual_norm
// sanity check (LoadJsonl, originally written comparing same-line values)
// failed the instant the fixtures left their stationary prologue, which is
// exactly how this was found.
//
// CONSEQUENCES, enforced by RecordedFrame/LoadJsonl/Replay below:
//   - The sample fed to UpdateFfbSample() for frame i uses frame i's OWN
//     top-level ffb.* fields -- these are NOT shifted, they already describe
//     frame i.
//   - effective_force_signed is NOT one of the top-level ffb.* fields; the
//     only place it exists in the record is ffb.gates.effective_force, which
//     -- per the lag above -- is frame (i-1)'s force, not frame i's. So frame
//     i's raw effective_force_signed input is read from frame (i+1)'s
//     gates.effective_force instead (LoadJsonl shifts this in-place right
//     after parsing). This only works because these fixtures were recorded
//     at theta=0, where the dead-time history walk in OverrideManager::Update
//     degenerates to "use this frame's sample unmodified" -- so the recorded
//     diagnostic really is the RAW, undelayed force, independent of which
//     theta/tau/grace THIS tool replays under later.
//   - The expected diagnostic OUTPUT of replaying frame i is, for the same
//     reason, frame (i+1)'s ffb.gates.* -- actual_norm, residual, shadow_norm
//     AND sustain_accum all shift the same way, all checked (see Replay()'s
//     identity-check block; checking only one of the four would not catch a
//     mapping that is wrong for the others).
//   - The LAST recorded row can therefore never be replayed as an INPUT (its
//     effective_force_signed would have to come from a row that doesn't
//     exist) -- it is only ever the comparison target for the second-to-last
//     row's replay. Valid input frames are 0..(N-2).
//
// REPLAY VALIDITY — READ BEFORE TRUSTING ANY NUMBER PAST A LATCH. The
// recorded telemetry is an open-loop trace: the wheel's measured motion was
// produced by whatever command was ACTUALLY applied at record time. There is
// exactly one path from the detector to the applied command (Controller
// VirtualDriver.cpp: `lat_manual = override_mgr_.IsLateralManual(); if
// (lat_manual) cmd.steering = m.steering;` and `ffb->SetSteerTarget(auto_cmd.
// steering, active=!lat_manual)`). While lat_manual stays false, the detector
// influences nothing, so replaying the SAME recorded telemetry through a
// DIFFERENT detector config is still evaluating that config against a valid,
// unperturbed physical trace. The instant a config's replay latches
// (IsLateralManual() flips true), that stops being true: the real run would
// have started applying a different steering command and driving the FFB
// target differently from that point on, so every recorded sample after that
// instant belongs to a run that never happened under this config. This tool
// therefore STOPS replaying a run the moment its own replay latches, and
// reports that configuration as "evaluable only up to t=<latch time>", never
// silently continuing to print a peak residual computed past that point.
//
// WHY A STANDALONE add_executable() AND NOT THE unittest() GTEST MACRO. This
// is a manually-invoked measurement tool (multiple --jsonl runs x multiple
// configs, printed as a table for a human to read), not a pass/fail assertion
// suite — there is nothing here to register as a ctest case. It is built and
// registered the same way GT_esmini/test/integration/GT_Loader.cpp is: a
// plain add_executable() beside (not inside) the unittest() umbrella binary.
// See GT_esmini/test/CMakeLists.txt for the registration and its own comment
// on why this links GT_esminiLib_static (OverrideManager is an internal class,
// never exported from the DLL) rather than GT_esminiLib.
//
// THE ONLY INPUT: InputFrame{} IS ALWAYS DEFAULT — WHY, AND WHAT IT GUARANTEES.
// OverrideManager::Update(const InputFrame&, dt) has THREE code paths that can
// set the lateral latch, not one:
//   (1) the direct steering-threshold path (`std::abs(ps.steering) >
//       steering_threshold_`) — but it only runs when `input.pedal_steer` is
//       set AND `!ffb_sample_.active`;
//   (2) the ButtonBits::OVERRIDE bit inside `input.pedal_steer->buttons`;
//   (3) (opt-in build only) `input.motion_request`.
// This harness constructs every frame's InputFrame with pedal_steer and
// motion_request left at their default (unset) state — never populated from
// the recording, because these fixtures don't carry a raw hardware axis/
// button stream in the first place (the servo is driving the wheel; there is
// no separate human input device in a hands-off run). With pedal_steer and
// motion_request both unset, `if (input.pedal_steer)` and `if (input.
// motion_request)` are false every frame, so paths (1)-(3) above are
// structurally unreachable here. The ONLY remaining path into `lat_active` is
// the residual detector inside `if (lat_configured_manual_ && ffb_sample_.
// active)`. Consequently: every latch this tool ever reports is, by
// construction, a residual-detector latch — never a direct-input artefact of
// how the harness happens to feed it.
//
// SELF-VERIFICATION (built into this binary, run automatically for the
// "recorded" configuration on every --jsonl file):
//   1. Load-time sanity check: actual_norm = target_norm - position_error is
//      the sink's own invariant (IFFBSink.hpp). Recomputed from frame i's OWN
//      target_norm/position_error and compared against frame (i+1)'s recorded
//      gates.actual_norm -- the one-row lag above, applied here too (a
//      same-line comparison is exactly the mistake that hid the lag in the
//      first place; do not revert this to a same-line comparison). A
//      mismatch means the schema/lag mapping is wrong and aborts before any
//      config is replayed.
//   2. Identity replay: these fixtures were recorded under theta=tau=0 /
//      onset grace disabled / threshold=0.08 / sustain=0.10 (before this
//      program existed). Replaying the "recorded" canonical config must
//      reproduce, frame by frame, the SAME actual_norm/residual/shadow_norm/
//      sustain_accum the recording carries in frame (i+1)'s ffb.gates. This
//      is the closest thing to ground truth this tool has for "am I driving
//      OverrideManager correctly", and all four quantities are checked
//      automatically, not just one.
//   3. Deliberate-mis-latch check is NOT automatic (it would require changing
//      the shipped threshold/sustain, which this tool must never do by
//      default) — it is a manual invocation instead: run this binary once
//      with --threshold and/or --sustain set far below the shipped values
//      (e.g. --threshold 0.001 --label force_latch) and confirm the "LATCH"
//      column fires. See --help.

#include "gt_esmini/control/manualdrive/ManualDriveConfig.hpp"
#include "gt_esmini/control/manualdrive/ManualDriveTypes.hpp"
#include "gt_esmini/control/manualdrive/OverrideManager.hpp"
#include "gt_esmini/common/SimpleJson.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace
{

using gt_esmini::FfbInterventionSample;
using gt_esmini::InputFrame;
using gt_esmini::ManualDriveConfig;
using gt_esmini::OverrideManager;
namespace json = gt_esmini::simplejson;

[[noreturn]] void Fail(const std::string& message)
{
    std::cerr << "ffb_override_replay: FATAL: " << message << std::endl;
    std::exit(1);
}

double RequireDouble(const json::Value& obj, const char* key, const std::string& context)
{
    double value = 0.0;
    if (!obj.GetDouble(key, value))
        Fail(context + ": missing/non-numeric field '" + key + "'");
    return value;
}

bool RequireBool(const json::Value& obj, const char* key, const std::string& context)
{
    bool value = false;
    if (!obj.GetBool(key, value))
        Fail(context + ": missing/non-bool field '" + key + "'");
    return value;
}

const json::Value& RequireObject(const json::Value& obj, const char* key, const std::string& context)
{
    const json::Value* value = obj.Find(key);
    if (!value || !value->IsObject())
        Fail(context + ": missing/non-object field '" + key + "'");
    return *value;
}

std::string FormatFixed(double value, int precision = 4)
{
    std::ostringstream os;
    os << std::fixed << std::setprecision(precision) << value;
    return os.str();
}

// One decoded frame of test_results/f7_realwheel_frozen_*/*.jsonl
// (VirtualDriverTelemetry JSON: top-level "ffb" object + its "gates" child).
// Only the fields this tool needs are decoded; everything else in the record
// (ego pose, preview points, midlong profile, ...) is irrelevant to the
// detector and is skipped.
struct RecordedFrame
{
    double sim_time = 0.0;

    // --- Replay INPUT: feeds OverrideManager::UpdateFfbSample() verbatim ---
    // target_active/commanded_force/position_error/target_norm are this
    // frame's OWN top-level "ffb" fields, unshifted -- see "THE ONE-ROW GATES
    // LAG" in the file header for why these four need no correction.
    bool   target_active          = false;  // ffb.target_active
    double commanded_force        = 0.0;    // ffb.commanded_force (diagnostics-only downstream)
    double position_error         = 0.0;    // ffb.position_error
    double target_norm            = 0.0;    // ffb.target_norm
    // Loaded from ffb.gates.effective_force, THEN OVERWRITTEN in-place by
    // LoadJsonl's shift loop below to hold the NEXT line's value. After that
    // shift this field is frame i's own raw, undelayed
    // FfbInterventionSample::effective_force_signed -- see "THE ONE-ROW GATES
    // LAG" in the file header for why the value one line down is the one
    // that actually belongs to this frame, and why that only works because
    // these fixtures were recorded at theta=0 (the dead-time history walk in
    // OverrideManager::Update degenerates to "use this frame's sample
    // unmodified" at theta=0, so the recorded diagnostic IS the raw input,
    // one frame removed). Do not read this field before the shift loop runs.
    double effective_force_signed = 0.0;

    // --- Recorded detector OUTPUTS — self-verification only, never fed back
    // in. Always read UNSHIFTED (this line's own gates.*); the shift is
    // applied at the comparison site (Replay(): compare diag computed from
    // sample(i) against frames[i+1]'s copies of these), never here.
    double rec_actual_norm   = 0.0;
    double rec_residual      = 0.0;
    double rec_shadow_norm   = 0.0;
    double rec_sustain_accum = 0.0;
};

std::vector<RecordedFrame> LoadJsonl(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open())
        Fail("cannot open '" + path + "'");

    std::vector<RecordedFrame> frames;
    std::string line;
    int line_no = 0;
    while (std::getline(file, line))
    {
        ++line_no;
        if (line.find_first_not_of(" \t\r\n") == std::string::npos)
            continue;  // tolerate blank trailing lines

        json::Value root;
        std::string parse_error;
        if (!json::Parse(line, root, &parse_error))
            Fail(path + ":" + std::to_string(line_no) + ": JSON parse error: " + parse_error);

        const std::string ctx = path + ":" + std::to_string(line_no);
        RecordedFrame f;
        f.sim_time = RequireDouble(root, "sim_time", ctx);

        const json::Value& ffb   = RequireObject(root, "ffb", ctx);
        const json::Value& gates = RequireObject(ffb, "gates", ctx + " ffb");

        f.target_active          = RequireBool(ffb, "target_active", ctx + " ffb");
        f.commanded_force        = RequireDouble(ffb, "commanded_force", ctx + " ffb");
        f.position_error         = RequireDouble(ffb, "position_error", ctx + " ffb");
        f.target_norm            = RequireDouble(ffb, "target_norm", ctx + " ffb");
        f.effective_force_signed = RequireDouble(gates, "effective_force", ctx + " ffb.gates");
        f.rec_actual_norm        = RequireDouble(gates, "actual_norm", ctx + " ffb.gates");
        f.rec_residual           = RequireDouble(gates, "residual", ctx + " ffb.gates");
        f.rec_shadow_norm        = RequireDouble(gates, "shadow_norm", ctx + " ffb.gates");
        f.rec_sustain_accum      = RequireDouble(gates, "sustain_accum", ctx + " ffb.gates");

        frames.push_back(f);
    }
    if (frames.empty())
        Fail(path + ": no frames decoded (empty file?)");

    // Load-time sanity check, and note the INDEX SHIFT — it is the whole point.
    //
    // ONE RECORDED LINE HOLDS TWO INSTANTS. The top-level "ffb" block is the
    // sink sample as of AFTER that frame's FFB update; "ffb.gates" is
    // OverrideManager's diagnostic, computed from the sample it was handed
    // BEFORE that update — i.e. the sample sitting in the PREVIOUS line's
    // "ffb" block (ControllerVirtualDriver calls UpdateFfbSample() then
    // Update(), and telemetry is written after the sink has moved on).
    // Measured on f7_realwheel_basic.jsonl:
    //     line 6.05: target-position_error = -0.0150, gates.actual_norm =  0.0000
    //     line 6.06: target-position_error = -0.0189, gates.actual_norm = -0.0150
    //     line 6.07: target-position_error = -0.0224, gates.actual_norm = -0.0188
    // i.e. gates.actual_norm(N) == (target_norm - position_error)(N-1), exactly.
    //
    // This is dangerous precisely because it hides: while the wheel sits at 0
    // the two instants agree, so pairing them same-line validates perfectly on
    // the stationary prologue and is silently one frame off for every moving
    // frame — the only part anyone cares about. Pair sample(N) with gates(N+1).
    //
    // Tolerance: both sides are independently rounded to 4 decimals by the
    // telemetry writer, so up to 2*5e-5 = 1e-4 of disagreement is expected
    // rounding; 1.5e-4 leaves a small margin for float summation order.
    // effective_force lives ONLY in the gates block, which means it is a
    // diagnostic and carries the same one-frame skew: gates.effective_force(N)
    // is the force the manager consumed from sample(N-1). Pairing it with
    // ffb.*(N) — as a naive same-line read does — feeds the detector a force
    // from one instant and a wheel position from the next. Shift it back onto
    // the sample it actually came from. Forward iteration is safe: frames[i+1]
    // is still original when it is read at step i.
    //
    // The final line's effective_force_signed is left UNSHIFTED (its
    // successor's gates do not exist to shift in) -- Replay() below must
    // never treat the final line as a replayable input, only ever as the
    // comparison target for the second-to-last one. Its stale value here is
    // dead data, not a usable sample.
    for (std::size_t i = 0; i + 1 < frames.size(); ++i)
        frames[i].effective_force_signed = frames[i + 1].effective_force_signed;

    for (std::size_t i = 0; i + 1 < frames.size(); ++i)
    {
        const double reconstructed = frames[i].target_norm - frames[i].position_error;
        if (std::abs(reconstructed - frames[i + 1].rec_actual_norm) > 1.5e-4)
        {
            Fail(path + ": frame " + std::to_string(i) + " (t=" +
                 FormatFixed(frames[i].sim_time) +
                 "): actual_norm sanity check FAILED across the known 1-frame skew "
                 "((target_norm-position_error)[N]=" + FormatFixed(reconstructed) +
                 " vs gates.actual_norm[N+1]=" + FormatFixed(frames[i + 1].rec_actual_norm) +
                 ") -- schema/field mapping or the skew assumption is wrong; "
                 "refusing to continue.");
        }
    }
    return frames;
}

// The five knobs this tool was built to sweep. Nothing else in
// ManualDriveConfig.ffb.target_track varies between configurations here.
struct DetectorConfig
{
    std::string label;
    double dead_time    = 0.0;   // theta, seconds (override_shadow_dead_time)
    double velocity_tau = 0.0;   // tau, seconds   (override_shadow_velocity_tau)
    double onset_grace  = 0.0;   // seconds, 0=off (override_shadow_onset_grace)
    double threshold    = 0.08;  // axis-fraction  (override_residual_threshold)
    double sustain_time = 0.10;  // seconds        (override_sustain_time)
    // NOTE: sustain_time is NOT present anywhere in the recording — the
    // fixture only ever logged the detector's OWN sustain_accum, never the
    // threshold it was being compared against as a time. It is supplied
    // purely from this struct, for every configuration, every run.
};

ManualDriveConfig BuildConfig(const DetectorConfig& dc)
{
    ManualDriveConfig cfg;  // every other field stays at its shipped default

    // Required for OverrideManager::Update() to reach the residual-detector
    // block at all (`if (lat_configured_manual_ && ffb_sample_.active)`) and
    // for the domain to actually latch when it fires — without these two,
    // the detector runs its math every frame but IsLateralManual() can never
    // become true.
    cfg.domain.lateral       = "manual";
    cfg.override_cfg.enabled = true;

    auto& tt = cfg.ffb.target_track;
    tt.override_shadow_dead_time    = dc.dead_time;
    tt.override_shadow_velocity_tau = dc.velocity_tau;
    tt.override_shadow_onset_grace  = dc.onset_grace;
    tt.override_residual_threshold  = dc.threshold;
    tt.override_sustain_time        = dc.sustain_time;
    // override_residual_reanchor_tau is deliberately left at ManualDriveConfig's
    // shipped default (1.5s) for every configuration below: it is drift
    // control for the shadow integrator, not one of the knobs this tool was
    // asked to sweep, and must not move alongside them.
    return cfg;
}

const std::vector<DetectorConfig>& CanonicalConfigs()
{
    static const std::vector<DetectorConfig> configs = {
        // Exactly the configuration test_results/f7_realwheel_frozen_*/*.jsonl
        // was recorded under (theta=tau=grace=0). This row doubles as the
        // identity-replay self-check target -- see main()'s is_identity_target
        // and Replay()'s check_identity parameter.
        {"recorded", 0.0, 0.0, 0.0, 0.08, 0.10},
        // Measured transport delay + velocity lag alone, onset grace still off.
        {"theta_tau", 0.041, 0.018, 0.0, 0.08, 0.10},
        // Onset grace alone (commit abd17a95's mitigation), theta/tau still off.
        {"grace_only", 0.0, 0.0, 0.05, 0.08, 0.10},
        // Current shipped default. Equal to ManualDriveConfig's own default
        // member initializers -- BuildConfig() overrides nothing for this row
        // that the struct wasn't already going to produce.
        {"shipped", 0.041, 0.018, 0.05, 0.08, 0.10},
    };
    return configs;
}

// Tolerance for comparing this tool's freshly computed actual_norm/residual/
// shadow_norm/sustain_accum against the recording's own 4-decimal-rounded
// values (identity replay, "recorded" config only, frame i's replay output
// vs frame (i+1)'s recorded gates -- see "THE ONE-ROW GATES LAG" in the file
// header). Both the inputs replayed here (effective_force etc.) and the
// recorded outputs compared against went through the same 4-decimal
// rounding, so ~1e-4 of single-frame disagreement is expected rounding
// noise, not a bug. Kept generous (5e-4) to also cover the small compounding
// a rounded-input integrator can accumulate between re-anchor events
// (override_residual_reanchor_tau=1.5s bounds standing drift, it does not
// erase it every frame). This is a precision allowance, not a correctness
// one -- if a run fails at this tolerance, find the cause; do not widen the
// constant to make it pass.
constexpr double kIdentityTolerance = 5e-4;

struct ReplayResult
{
    bool   latched       = false;
    double latch_time    = 0.0;
    double peak_residual = 0.0;  // over the VALID prefix only (up to and incl. any latch frame)
    double peak_sustain  = 0.0;  // ditto
    int    frames_run    = 0;

    bool   identity_checked          = false;
    bool   identity_ok               = false;
    double identity_max_actual_err   = 0.0;
    double identity_max_residual_err = 0.0;
    double identity_max_shadow_err   = 0.0;
    double identity_max_sustain_err  = 0.0;
    double identity_fail_time        = 0.0;
};

ReplayResult Replay(const std::vector<RecordedFrame>& frames, const DetectorConfig& dc, bool check_identity)
{
    OverrideManager mgr;
    mgr.Configure(BuildConfig(dc));

    ReplayResult result;
    result.identity_checked = check_identity;
    result.identity_ok      = true;

    if (frames.size() < 2)
        return result;  // nothing valid to replay -- see "THE ONE-ROW GATES LAG"

    // Valid INPUT frames are 0..(N-2), never N-1: frame i's
    // effective_force_signed was shifted in LoadJsonl to hold frame (i+1)'s
    // recorded value, so the last line has nothing valid to shift in from
    // and must never be fed to UpdateFfbSample() as if it were a real
    // sample -- see the file header and LoadJsonl's shift-loop comment.
    const std::size_t last_input = frames.size() - 2;

    double prev_sim_time = 0.0;
    for (std::size_t i = 0; i <= last_input; ++i)
    {
        const RecordedFrame& rf = frames[i];
        // dt from consecutive sim_time samples. The recordings step at a
        // fixed 0.01s and frame 0's own sim_time (0.0100) already equals that
        // nominal step, so frame 0 uses its own sim_time as dt (equivalent to
        // an implicit t=0 predecessor) rather than diffing against a
        // frame -1 that does not exist.
        const double dt = (i == 0) ? rf.sim_time : (rf.sim_time - prev_sim_time);
        prev_sim_time   = rf.sim_time;

        FfbInterventionSample sample;
        sample.active                 = rf.target_active;
        sample.commanded_force        = rf.commanded_force;
        sample.position_error         = rf.position_error;
        sample.target_norm            = rf.target_norm;
        // Already shifted in-place by LoadJsonl -- see RecordedFrame's own
        // comment and "THE ONE-ROW GATES LAG" in the file header.
        sample.effective_force_signed = rf.effective_force_signed;

        // Order matters and matches the real controller (ControllerVirtualDriver.cpp):
        // UpdateFfbSample() BEFORE Update(), every frame.
        mgr.UpdateFfbSample(sample);
        // InputFrame{} is always default here — see file header, "THE ONLY
        // INPUT", for the full guarantee this gives: the only way this call
        // can set the lateral latch is the residual detector.
        mgr.Update(InputFrame{}, dt);

        const auto& diag = mgr.GetFfbLatchDiagnostics();
        result.frames_run    = static_cast<int>(i) + 1;
        result.peak_residual = std::max(result.peak_residual, diag.residual);
        result.peak_sustain  = std::max(result.peak_sustain, diag.sustain_accum);

        // Compare against frame i+1's gates, NOT frame i's: one recorded line
        // holds two instants, and the diagnostic produced from sample(i) is
        // the one the recording wrote into line i+1 (see "THE ONE-ROW GATES
        // LAG" in the file header). All four detector outputs are checked,
        // not just residual/sustain_accum -- a mismatch confined to one
        // quantity would mean the alignment itself is still wrong.
        if (check_identity)
        {
            const RecordedFrame& expected = frames[i + 1];
            const double actual_err   = std::abs(diag.actual_norm - expected.rec_actual_norm);
            const double residual_err = std::abs(diag.residual - expected.rec_residual);
            const double shadow_err   = std::abs(diag.shadow_norm - expected.rec_shadow_norm);
            const double sustain_err  = std::abs(diag.sustain_accum - expected.rec_sustain_accum);
            result.identity_max_actual_err   = std::max(result.identity_max_actual_err, actual_err);
            result.identity_max_residual_err = std::max(result.identity_max_residual_err, residual_err);
            result.identity_max_shadow_err   = std::max(result.identity_max_shadow_err, shadow_err);
            result.identity_max_sustain_err  = std::max(result.identity_max_sustain_err, sustain_err);
            const double worst = std::max(std::max(actual_err, residual_err), std::max(shadow_err, sustain_err));
            if (result.identity_ok && worst > kIdentityTolerance)
            {
                result.identity_ok        = false;
                result.identity_fail_time = expected.sim_time;
            }
        }

        if (mgr.IsLateralManual())
        {
            result.latched    = true;
            result.latch_time = rf.sim_time;
            break;  // replay validity ends here -- see file header "REPLAY VALIDITY"
        }
    }
    return result;
}

struct CliOptions
{
    std::vector<std::string> jsonl_paths;
    bool           has_override = false;
    DetectorConfig custom       = {"custom", 0.041, 0.018, 0.05, 0.08, 0.10};  // starts from shipped default
};

void PrintHelp()
{
    std::cout <<
        "ffb_override_replay --jsonl <path> [--jsonl <path> ...]\n"
        "                     [--theta S] [--tau S] [--grace S]\n"
        "                     [--threshold FRAC] [--sustain S] [--label NAME]\n"
        "\n"
        "Replays recorded real-wheel FFB telemetry through the shipping\n"
        "OverrideManager residual detector (no reimplementation -- see the file's\n"
        "header comment). With no --theta/--tau/--grace/--threshold/--sustain\n"
        "flags, evaluates the 4 canonical configurations (recorded / theta_tau /\n"
        "grace_only / shipped) against every --jsonl run and prints a table.\n"
        "Passing any one of those flags instead evaluates exactly ONE custom\n"
        "configuration (unspecified knobs default to the shipped values) -- e.g.\n"
        "to run the deliberate-mis-latch self-check:\n"
        "  ffb_override_replay --jsonl <path> --threshold 0.001 --label force_latch\n";
}

void ParseArgs(int argc, char** argv, CliOptions& opt)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        auto next = [&](const char* flag) -> std::string {
            if (i + 1 >= argc)
                Fail(std::string(flag) + " requires a value");
            return argv[++i];
        };

        if (arg == "--jsonl")
            opt.jsonl_paths.push_back(next("--jsonl"));
        else if (arg == "--theta")
        {
            opt.custom.dead_time = std::stod(next("--theta"));
            opt.has_override      = true;
        }
        else if (arg == "--tau")
        {
            opt.custom.velocity_tau = std::stod(next("--tau"));
            opt.has_override         = true;
        }
        else if (arg == "--grace")
        {
            opt.custom.onset_grace = std::stod(next("--grace"));
            opt.has_override        = true;
        }
        else if (arg == "--threshold")
        {
            opt.custom.threshold = std::stod(next("--threshold"));
            opt.has_override      = true;
        }
        else if (arg == "--sustain")
        {
            opt.custom.sustain_time = std::stod(next("--sustain"));
            opt.has_override          = true;
        }
        else if (arg == "--label")
            opt.custom.label = next("--label");
        else if (arg == "--help" || arg == "-h")
        {
            PrintHelp();
            std::exit(0);
        }
        else
            Fail("unrecognized argument '" + arg + "'");
    }
    if (opt.jsonl_paths.empty())
        Fail("at least one --jsonl <path> is required (see --help)");
}

}  // namespace

int main(int argc, char** argv)
{
    CliOptions opt;
    ParseArgs(argc, argv, opt);

    const std::vector<DetectorConfig> configs =
        opt.has_override ? std::vector<DetectorConfig>{opt.custom} : CanonicalConfigs();

    for (const std::string& path : opt.jsonl_paths)
    {
        const std::vector<RecordedFrame> frames = LoadJsonl(path);
        std::cout << "\n=== " << path << " (" << frames.size() << " frames, t=["
                   << FormatFixed(frames.front().sim_time, 2) << ".." << FormatFixed(frames.back().sim_time, 2)
                   << "]) ===\n";

        std::cout << std::left << std::setw(12) << "config" << std::right << std::setw(14) << "peak_resid"
                   << std::setw(10) << "margin" << std::setw(16) << "max_sustain" << std::setw(22) << "latch"
                   << "\n";

        for (const DetectorConfig& dc : configs)
        {
            // Only the "recorded" canonical config is the identity-replay
            // self-check target -- it is the one and only configuration
            // these files were actually captured under (see file header).
            const bool is_identity_target = !opt.has_override && dc.label == "recorded";
            const ReplayResult r          = Replay(frames, dc, is_identity_target);

            std::cout << std::left << std::setw(12) << dc.label << std::right;
            if (r.latched)
            {
                std::cout << std::setw(14) << "N/A" << std::setw(10) << "N/A" << std::setw(16) << "N/A"
                           << std::setw(22) << ("LATCH t=" + FormatFixed(r.latch_time, 2)) << "\n";
                std::cout << "  -- replay INVALID from t=" << FormatFixed(r.latch_time, 2)
                           << "s onward: this configuration LATCHES MANUAL, and past that instant the\n"
                              "     applied steering command differs from what was recorded (file header,\n"
                              "     REPLAY VALIDITY). Reported as unevaluable, not as a measurement.\n";
                std::cout << "  -- (pre-latch prefix only, NOT a full-run figure) peak_residual="
                           << FormatFixed(r.peak_residual) << " max_sustain=" << FormatFixed(r.peak_sustain)
                           << " over " << r.frames_run << " frames\n";
            }
            else
            {
                const double margin = (r.peak_residual > 1e-9) ? (dc.threshold / r.peak_residual)
                                                                : std::numeric_limits<double>::infinity();
                std::cout << std::setw(14) << FormatFixed(r.peak_residual);
                std::cout << std::setw(10) << (std::isinf(margin) ? std::string("inf") : FormatFixed(margin, 2));
                std::cout << std::setw(16) << FormatFixed(r.peak_sustain) << std::setw(22) << "no"
                           << "\n";
            }

            if (r.identity_checked)
            {
                std::cout << "  identity-replay self-check (" << r.frames_run
                           << " frames vs frame(i+1)'s recorded gates.{actual_norm,residual,shadow_norm,"
                              "sustain_accum}, tolerance "
                           << FormatFixed(kIdentityTolerance) << "): " << (r.identity_ok ? "PASS" : "FAIL");
                if (!r.identity_ok)
                    std::cout << " at t=" << FormatFixed(r.identity_fail_time, 2);
                std::cout << " (max err: actual_norm=" << FormatFixed(r.identity_max_actual_err)
                           << ", residual=" << FormatFixed(r.identity_max_residual_err)
                           << ", shadow_norm=" << FormatFixed(r.identity_max_shadow_err)
                           << ", sustain_accum=" << FormatFixed(r.identity_max_sustain_err) << ")\n";
            }
        }
    }
    return 0;
}
