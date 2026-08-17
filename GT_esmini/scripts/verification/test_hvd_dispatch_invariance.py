"""Dispatch-invariance evidence for GT_esminiLib.cpp's ManualDrive ADAS wiring
(req-vd-ad:REQ-AD-025 REQ-AD-028, vd-func:FUNC-075, phase A).

manualdrive_adas_design.md §12 calls GT_esminiLib.cpp's controller-dispatch
else-if chain "the convergence point for all four controllers' OSI output":
RealDriver / PythonDriver / ManualDrive / VirtualDriver all resolve their HVD
rows through the SAME chain (see the `pushControllerState` lambda and the
`else if (auto* manualDrive = dynamic_cast<...>(ctrl))` / `else if (auto*
virtualDriver = ...)` branches). Adding the ManualDrive `AddADASFunctionEx`
loop is therefore the highest-risk edit in the wiring pass: a mistake there
(wrong branch, wrong guard, a stray fallthrough) could silently perturb what
the RealDriver branch emits, or make ManualDrive's own rows additive with the
legacy 24-slot block instead of replacing it.

This file is the negative evidence that did NOT happen:

  1. RealDriverController's HVD output is UNTOUCHED by the ManualDrive wiring:
     in this harness (headless, no UDP client feeding RealDriverController)
     it emits ZERO `vehicle_automated_driving_function` rows -- not 24, and
     specifically none named "gt.aeb"/"gt.fcw" (ManualDrive's report path
     leaking across the dispatch). See test_realdriver_path_... below for WHY
     the count is 0 and not 24 here, and where the 24-row table's CONTENT is
     actually pinned (it is NOT re-checked by this file).
  2. A ManualDrive run (ADAS config left at its default OFF) carries ONLY the
     2-row gt.aeb/gt.fcw block ControllerManualDrive::GetADASFunctions()
     produces -- the RealDriver 24-row block is genuinely ABSENT there, not
     merely unused alongside it. This is what "the two dispatch paths are
     disjoint, not additive" means operationally: ControllerManualDrive::
     GetADASStates() always returns empty (see its own header comment), so the
     fixed-24-slot path never fires for ManualDrive in the first place -- this
     test is what proves that stays true after the wiring change.

Runs GT_esminiLib.dll in-process via GtLib (gt_lib.py) -- no UDP, see that
module's docstring for why. Deliberately never skips: a dispatch-invariance
test that quietly turns into a skip when HVD capture comes back empty would
make a real dispatch regression and "environment not ready" look identical,
which is exactly the silent-instrument failure mode this project's
verification tooling is built to avoid (see gt_sim_test.py's own osi_misses/
hvd_misses "fail loudly, never fabricate a miss as success" precedent, which
this file follows for the same reason).

Do NOT run this yet -- it needs a Release build's GT_esminiLib.dll, which does
not exist at the time this file was written (see this task's PROCESS
CONSTRAINTS: the wiring-pass agent does not build).

Run (after a Release build):
    DriverScript/.venv/Scripts/python.exe -m pytest \
        GT_esmini/scripts/verification/test_hvd_dispatch_invariance.py -v
"""

from __future__ import annotations

from pathlib import Path

import gt_sim_test as gst  # noqa: E402  (side effect: puts scripts/ -- osi3 -- on sys.path)
from gt_lib import GtLib  # noqa: E402  (local module, same dir)
from osi3.osi_hostvehicledata_pb2 import HostVehicleData  # noqa: E402

REPO_ROOT = gst.REPO_ROOT

# --- fixture scenarios -------------------------------------------------------

# RealDriverController integration fixture (GT_esmini/test/CMakeLists.txt's
# GT_REALDRIVER_SCENARIOS set, "realdriver_f13_hvd_adas_passthrough" entry) --
# an existing, already-green RealDriver scenario. No ManualDrive/ADAS content
# at all; picked specifically because it is untouched by this feature and
# therefore the right asset to prove NOTHING moved for it.
REALDRIVER_SCENARIO = (
    REPO_ROOT
    / "GT_esmini"
    / "test"
    / "scenarios"
    / "realdriver_f13_hvd_adas_passthrough.xosc"
)

# ManualDrive ADAS batch fixture (resources/xosc/verification/manualdrive_adas_batch.yaml),
# run here WITHOUT the batch's manualdrive_config overrides -- i.e. against the
# BASE_MD_CONFIG default (adas_aeb_enabled=false, input_type="stub"). ADAS OFF
# is deliberate: this test is not about AEB firing, only about which HVD rows
# the dispatch emits, and the disjointness claim ("RealDriver's 24-row block is
# absent under ManualDrive") holds regardless of whether AEB is armed -- an
# ADAS-off run keeps the fixture minimal (no scripted input profile needed).
MANUALDRIVE_SCENARIO = (
    REPO_ROOT
    / "resources"
    / "xosc"
    / "verification"
    / "10_manualdrive_adas"
    / "md_aeb_no_conflict.xosc"
)

# Mirrors GT_esmini/test/CMakeLists.txt's GT_INTEGRATION_PATH_ARGS (the C++
# GT_Loader integration harness's own invocation of this exact
# realdriver_f13_hvd_adas_passthrough.xosc fixture): --path entries let esmini
# fall back to these directories if a referenced file isn't found via the
# xosc-relative resolution it tries first. Not strictly load-bearing here --
# both fixture xosc files already resolve their LogicFile/CatalogLocations
# correctly via ordinary xosc-relative paths (verified by reading both files)
# -- but included for parity with the C++ integration harness's own
# invocation, per this task's instruction to mirror it.
_PATH_ARGS = [
    "--path",
    str(REPO_ROOT / "resources" / "xodr"),
    "--path",
    str(REPO_ROOT / "resources" / "xosc"),
    "--path",
    str(REPO_ROOT / "resources"),
    "--path",
    str(REPO_ROOT / "GT_esmini" / "test" / "scenarios"),
]

_DT = 0.05
_N_STEPS = 60  # 3.0 s -- both fixtures' StopTrigger/story run well past this

# --- RealDriver's fixed 24-slot table: content is pinned ELSEWHERE, not here -
#
# NOT re-checked in this file, deliberately -- see test_realdriver_path_...
# below for why the row COUNT this harness observes is 0, not 24 (a headless
# run with no UDP client feeding RealDriverController never populates
# input_.adasStates, so the 24-row loop's size guard never fires). The
# table's CONTENT (Name enum order, label<->index mapping) is still verified,
# just not by this file:
#   * test/unit/realdriver/test_AdasSlotTable.cpp pins
#     gt_esmini::realdetail::kAdasSlots (the table itself) directly.
#   * GT_esminiLib.cpp's `AdasSlotTableMatchesOsi()` static_assert pins that
#     same table against the real osi_hostvehicledata.proto enum at compile
#     time.
# A prior version of this file duplicated that table as an expected-Name
# tuple and asserted it against a live (always-empty-here) HVD capture --
# dead weight now that the count assertion below makes the comparison
# unreachable, so it was removed rather than left to rot.

# The 24 RealDriver custom_name labels (GT_esmini/include/gt_esmini/control/
# ControllerRealDriverUtils.hpp's kAdasSlots -- the `label` field, which
# GT_esminiLib.cpp's pushControllerState() writes verbatim as each row's
# custom_name). Used by the ManualDrive-side test to assert none of these
# leaked in -- a stronger, more specific claim than just "row count == 2".
_REALDRIVER_LABELS = frozenset(
    {
        "BLIND_SPOT_WARNING",
        "FORWARD_COLLISION_WARNING",
        "LANE_DEPARTURE_WARNING",
        "PARKING_COLLISION_WARNING",
        "REAR_CROSS_TRAFFIC_WARNING",
        "AUTOMATIC_EMERGENCY_BRAKING",
        "AUTOMATIC_EMERGENCY_STEERING",
        "REVERSE_AUTOMATIC_EMERGENCY_BRAKING",
        "ADAPTIVE_CRUISE_CONTROL",
        "LANE_KEEPING_ASSIST",
        "ACTIVE_DRIVING_ASSISTANCE",
        "BACKUP_CAMERA",
        "SURROUND_VIEW_CAMERA",
        "NIGHT_VISION",
        "HEAD_UP_DISPLAY",
        "ACTIVE_PARKING_ASSISTANCE",
        "REMOTE_PARKING_ASSISTANCE",
        "TRAILER_ASSISTANCE",
        "AUTOMATIC_HIGH_BEAMS",
        "DRIVER_MONITORING",
        "URBAN_DRIVING",
        "HIGHWAY_AUTOPILOT",
        "CRUISE_CONTROL",
        "SPEED_LIMIT_CONTROL",
    }
)


def _collect_hvd_frames(scenario: Path) -> list[HostVehicleData]:
    """Run `scenario` headless in-process for _N_STEPS * _DT seconds and
    return every frame's parsed HostVehicleData (GT_GetOSIHostVehicleData,
    vehicle_id=-1 -- the ego/target vehicle, same rule as
    set_host_vehicle_inputs() per gt_lib.py's docstring). A step whose capture
    comes back None/unparseable is silently OMITTED from the returned list --
    NOT coerced into an empty/zero HostVehicleData -- so the caller can (and
    must) treat "collected is empty" as a hard capture failure rather than
    reading a fabricated all-default frame as a genuine measurement (the same
    discipline gt_sim_test.py's osi_misses/hvd_misses counters enforce for the
    batch harness)."""
    frames: list[HostVehicleData] = []
    with GtLib() as lib:
        args = [
            "--osc",
            str(scenario),
            "--headless",
            "--fixed_timestep",
            str(_DT),
            *_PATH_ARGS,
        ]
        rc = lib.init_with_args(args)
        assert (
            rc == 0
        ), f"GT_InitWithArgs failed (rc={rc}) for {scenario}: {lib.get_last_error()}"
        for _ in range(_N_STEPS):
            lib.step(_DT)
            raw = lib.get_osi_host_vehicle_data(-1)
            if raw is None:
                continue
            hvd = HostVehicleData()
            try:
                hvd.ParseFromString(raw)
            except Exception:
                continue
            frames.append(hvd)
    return frames


# ---------------------------------------------------------------------------
# RealDriver side: nothing moved
# ---------------------------------------------------------------------------


def test_realdriver_path_unfed_emits_zero_rows_and_never_leaks_manualdrive_labels():
    """RealDriver's HVD ADAS output in THIS harness, and why it is 0 rows, not 24.

    PROVENANCE (read this before "fixing" the count below to 24): RealDriver's
    OSI getter is `ControllerRealDriver::GetADASStates()`
    (src/control/ControllerRealDriver.cpp:761-764) --

        void ControllerRealDriver::GetADASStates(std::vector<int>& states) const
        {
            states = input_.adasStates;
        }

    -- and `input_.adasStates` is populated ONLY from RealDriverController's
    UDP input (a RealDriverClient feed). This harness runs headless with NO
    UDP client attached, so `input_.adasStates` stays empty for the entire
    run. GT_esminiLib.cpp's `pushControllerState()` lambda only runs its
    24-row `AddADASFunctionEx` loop when
    `adasStates.size() >= gt_esmini::realdetail::kAdasFunctionCount`
    (kAdasFunctionCount == 24); an empty vector never satisfies that guard.
    So RealDriver emits ZERO rows here -- and did so BEFORE this task's
    ManualDrive dispatch change too, since that guard and this scenario's UDP
    starvation are both unrelated to it. A nonzero count in a future run of
    this test therefore means one of two things, neither of which this test
    can tell apart on its own: RealDriver started emitting rows
    unconditionally (a RealDriver-side behavior change, orthogonal to the
    ManualDrive wiring), or the ManualDrive dispatch edit leaked execution
    into this branch. Either way it is worth a human's attention, hence this
    is asserted explicitly rather than silently accepted or skipped.

    The 24-row table's CONTENT (Name enum order, label<->index mapping) is
    NOT re-verified here -- see the module-level comment above
    _REALDRIVER_LABELS for where it actually is pinned.

    A genuine "RealDriver really does emit its live 24 rows, unperturbed" E2E
    check would need to feed adasStates over RealDriverController's UDP port
    from this harness (a synthetic RealDriverClient send loop). Deliberately
    NOT implemented here: this repo has a documented history of UDP-in-a-gate
    flakiness (loopback buffer pressure silently dropping frames -- see
    gt_sim_test.py's own capture_osi rationale), and adding a second UDP
    sender into an already in-process, socket-free harness is a cost/design
    decision for a human to make, not something to reach for silently inside
    a wiring-pass task.
    """
    frames = _collect_hvd_frames(REALDRIVER_SCENARIO)

    # FAIL LOUDLY, never skip: see this module's docstring for why "nothing
    # captured" must read as a test failure, not an inconclusive pass.
    assert frames, (
        f"no HVD frame was ever captured for {REALDRIVER_SCENARIO} over "
        f"{_N_STEPS} steps -- a dispatch-invariance test that captures nothing "
        "proves nothing about the dispatch and must not report success."
    )

    for i, hvd in enumerate(frames):
        rows = list(hvd.vehicle_automated_driving_function)

        # The leakage guard -- the property the ManualDrive dispatch edit
        # could plausibly have broken (GetADASFunctions()/AddADASFunctionEx
        # routed into the wrong dynamic_cast branch, or a stray fallthrough).
        # Checked independently of the row-count assertion below so it stays
        # meaningful on its own if RealDriver's row count ever legitimately
        # changes (e.g. this harness gains a UDP feed later).
        custom_names = {f.custom_name for f in rows}
        leaked = custom_names & {"gt.aeb", "gt.fcw"}
        assert not leaked, (
            f"frame {i}: ManualDrive's own row label(s) {sorted(leaked)} "
            "appeared on the RealDriver dispatch path -- the ManualDrive "
            "report path (GetADASFunctions/AddADASFunctionEx) must never be "
            "reached from the RealDriver branch."
        )

        # The row-count fact this harness actually produces -- see this
        # function's docstring for the full provenance chain.
        assert len(rows) == 0, (
            f"frame {i}: RealDriver HVD (headless, no UDP feed) carries "
            f"{len(rows)} vehicle_automated_driving_function row(s), expected "
            "exactly 0 in this harness -- see this test's docstring "
            "(ControllerRealDriver.cpp:761-764, input_.adasStates is "
            "UDP-fed only, never populated here)."
        )

        # req-vd-ad:REQ-AD-028 段b (phase B): AddADASFunctionEx grew a 6th,
        # DEFAULTED parameter carrying DriverOverride/custom_state. The whole
        # point of defaulting it is that non-ManualDrive callers stay
        # byte-identical on the wire, so no row on this path may carry either
        # field. Vacuously true while the row count is 0, and deliberately
        # kept anyway: if this harness ever gains a UDP feed and the count
        # becomes 24, this assertion is what catches the 6th argument having
        # been wired into the shared pushControllerState lambda by mistake.
        for f in rows:
            assert not f.HasField("driver_override"), (
                f"frame {i}: RealDriver row {f.custom_name!r} carries a "
                "driver_override submessage -- phase B's per-row override is "
                "ManualDrive-only and must leave every other dispatch branch's "
                "serialized HVD unchanged."
            )
            assert not f.custom_state, (
                f"frame {i}: RealDriver row {f.custom_name!r} carries "
                f"custom_state={f.custom_state!r} -- same invariance argument."
            )


# ---------------------------------------------------------------------------
# ManualDrive side: the two paths are disjoint, not additive
# ---------------------------------------------------------------------------


def test_manualdrive_path_realdriver_24_slot_block_is_absent(tmp_path):
    # Prep the ManualDrive fixture the way the harness's own batch() does
    # (gt_sim_test.py's controller=="manualdrive" branch): an absolute
    # ConfigFile must be injected because the in-process harness resolves a
    # relative ConfigFile against the host python.exe, not the DLL's would-be
    # exe dir (see _prepare_manualdrive_xosc's own docstring). Reusing these
    # two already-tested helpers (test_manualdrive_harness.py) rather than
    # re-deriving the same absolute-path-injection logic here keeps this file
    # from silently drifting out of sync with the one place that logic is
    # actually exercised.
    cfg = gst._write_manualdrive_config({}, tmp_path / "manual_drive.run.json")
    scen_to_run = gst._prepare_manualdrive_xosc(
        MANUALDRIVE_SCENARIO, tmp_path / "run", cfg
    )

    frames = _collect_hvd_frames(scen_to_run)

    assert frames, (
        f"no HVD frame was ever captured for {MANUALDRIVE_SCENARIO} (prepared "
        f"as {scen_to_run}) over {_N_STEPS} steps -- a dispatch-invariance "
        "test that captures nothing proves nothing about the dispatch and "
        "must not report success."
    )

    for i, hvd in enumerate(frames):
        rows = list(hvd.vehicle_automated_driving_function)
        names = {f.custom_name for f in rows}

        # The load-bearing assertion: NOT 24+2=26 (additive), NOT 24 (legacy
        # path still firing), exactly the 2 rows ControllerManualDrive::
        # GetADASFunctions() -> BuildManualAdasFunctionReport() produces
        # (gt.aeb + gt.fcw, both UNAVAILABLE with ADAS config left OFF).
        assert len(rows) == 2, (
            f"frame {i}: ManualDrive HVD carries {len(rows)} "
            "vehicle_automated_driving_function row(s), expected exactly 2 "
            f"(gt.aeb + gt.fcw) -- got custom_name(s) {sorted(names)}. A count "
            "of 24 or 26 here would mean the RealDriver 24-slot path fired "
            "for ManualDrive too (additively or instead), i.e. the two "
            "dispatch paths are NOT disjoint."
        )
        assert names == {"gt.aeb", "gt.fcw"}, (
            f"frame {i}: ManualDrive HVD row labels are {sorted(names)}, "
            "expected exactly {'gt.aeb', 'gt.fcw'}."
        )

        leaked = names & _REALDRIVER_LABELS
        assert not leaked, (
            f"frame {i}: RealDriver slot-table label(s) {sorted(leaked)} "
            "appeared on the ManualDrive dispatch path -- the fixed 24-slot "
            "GetADASStates() path must never fire for ManualDrive "
            "(ControllerManualDrive::GetADASStates() is deliberately always "
            "empty; see its header comment)."
        )

        # req-vd-ad:REQ-AD-028 段b (phase B): with the ADAS config left OFF
        # (this fixture's BASE_MD_CONFIG default) both rows are UNAVAILABLE,
        # i.e. the function was never running -- so the override channel must
        # be UNWRITTEN, not written-as-inactive. An absent submessage is the
        # only way OSI can say "nobody looked"; emitting an explicit
        # active=false here would tell a face-3 consumer that a switched-off
        # function had been observed and found un-overridden, which is a
        # measurement that never happened.
        for f in rows:
            assert not f.HasField("driver_override"), (
                f"frame {i}: ManualDrive row {f.custom_name!r} is "
                "UNAVAILABLE (ADAS config OFF) yet carries a driver_override "
                "submessage -- a function that was not running cannot have "
                "been overridden, and reporting it as evaluated-but-inactive "
                "would let a negative matcher pass on a run where the stack "
                "never executed."
            )
            assert not f.custom_state, (
                f"frame {i}: ManualDrive row {f.custom_name!r} carries "
                f"custom_state={f.custom_state!r} with ADAS config OFF."
            )
