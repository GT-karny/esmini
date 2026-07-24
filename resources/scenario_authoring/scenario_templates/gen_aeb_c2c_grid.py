#!/usr/bin/env python
"""gen_aeb_c2c_grid.py — AEB car-to-car (c2c) parametric stimulus grid.

SCENE INTENT
------------
16 straight-road, same-lane (no cut-in) car-to-car scenarios spanning the
three classic Euro NCAP AEB car-to-car test families:

    CCRs (Car-to-Car Rear stationary) — 7 cells: ego closes on a STOPPED lead
        at ego_speed in {10,20,...,70} km/h.
    CCRm (Car-to-Car Rear moving)     — 5 cells: ego closes on a lead cruising
        at a CONSTANT 20 km/h, ego_speed in {30,...,70} km/h.
    CCRb (Car-to-Car Rear braking)    — 4 cells: ego and lead both start at
        50 km/h; the lead brakes from t=2.0s at a fixed rate to a full stop,
        with the initial bumper-to-bumper gap (hw) and the brake rate as the
        two axes: hw in {12, 40} m, rate in {2, 6} m/s^2.

This is an EXPLORATION / STIMULUS layer (req-vd-ad:REQ-AD-010, REQ-AD-011) —
NOT wired into the regression gate or CI. It intentionally carries no
expectations.yaml / baseline: the point is to generate the stimulus grid for
later manual/automated sweep analysis, not to assert pass/fail per cell (that
is what resources/xosc/verification/aeb_safety_batch.yaml — the curated,
gated regression set — is for; see 07_aeb/cutin_hard_brake.xosc for the
hand-authored structural reference).

FILENAME CONTRACT (fixed — do not rename)
------------------------------------------
    ccrs_ego{10,20,30,40,50,60,70}
    ccrm_ego{30,40,50,60,70}_lead20
    ccrb_hw{12,40}_d{2,6}

GEOMETRY / PARAMETER MODEL
---------------------------
Road: resources/xodr/straight_500m_2lane.xodr (road id=1, lane -1), the same
asset 07_aeb/*.xosc uses. Ego teleports at s=30 (matches 07_aeb convention),
routed to s=480 with VirtualDriverController active (lateral+longitudinal).
Lead teleports ahead in the SAME lane (dLaneId==0, no cut-in), with only a
constant-speed (CCRs/CCRm) or constant-speed-then-brake (CCRb) longitudinal
action — no controller, matching the Lead in cutin_hard_brake.xosc.

Gap semantics: every "gap" in this generator is BUMPER-TO-BUMPER. Both
vehicles share the authoring_common standard bounding box (length 5.0 m,
center x=1.4 m ahead of the reference/origin point used by <LanePosition s=..>
i.e. front overhang 3.9 m, rear overhang 1.1 m; overhangs sum to the vehicle
length regardless of the split). For two identical vehicles the origin-to-
origin s-difference therefore equals the bumper gap plus exactly one vehicle
length:

    s_diff = bumper_gap + VEHICLE_LENGTH

CCRs / CCRm initial gap: TTC0 = 6.0 s against the closing speed, floored at
40 m (both deterministic floats, no RNG):

    closing_speed = ego_speed                       (CCRs, lead stopped)
    closing_speed = ego_speed - lead_speed           (CCRm, lead cruising)
    bumper_gap    = max(40.0, closing_speed * 6.0)

CCRb initial gap: given directly as the hw axis value (bumper-to-bumper),
independent of the TTC0 formula (initial closing speed is 0 — ego and lead
start at the same 50 km/h — so a closing-speed-scaled gap is undefined here).

DURATION / STOP-TRIGGER MODEL
------------------------------
gt_sim_test.py's batch() only reads defaults.max_time (a single global run
ceiling) — it does NOT read a per-entry max_time override (verified against
GT_esmini/scripts/verification/gt_sim_test.py `batch()`). Each generated xosc
therefore carries its OWN deterministic <StopTrigger> (short cells end their
own storyboard early); run()'s telemetry-silence "grace" window (~1s at
dt=0.05, see `run()` in gt_sim_test.py) then breaks the step loop as soon as
the VirtualDriverController deactivates on storyboard completion, so short
cells do not pay for the full global ceiling. This mirrors the existing
convention documented in aeb_safety_batch.yaml ("max_time >= longest
StopTrigger + ~2s grace"). The manifest's defaults.max_time is set to
max(cell duration) + 2.0 s of headroom.

Per-cell duration (deterministic, sizing-only — NOT a physical prediction of
when AEB fires, just a generous ceiling so no interesting event gets cut
off):

    ego_stop_time = ego_speed / ASSUMED_MIN_DECEL     (ASSUMED_MIN_DECEL=4.0 m/s^2)
    CCRs/CCRm: TTC0 = bumper_gap / closing_speed (as defined above, using the
               UN-adjusted closing-speed*6s value before the length offset)
               duration = TTC0 + ego_stop_time + MARGIN_S
    CCRb:      lead_stop_time = lead_speed / brake_rate
               duration = BRAKE_TIME_S + max(lead_stop_time, ego_stop_time)
                          + MARGIN_S

Usage:
    DriverScript/.venv/Scripts/python.exe \
        resources/scenario_authoring/scenario_templates/gen_aeb_c2c_grid.py
"""

from __future__ import annotations

import sys
from dataclasses import dataclass
from pathlib import Path

# Make resources/scenario_authoring/ importable as the authoring package root.
_AUTHORING_ROOT = Path(__file__).resolve().parents[1]
if str(_AUTHORING_ROOT) not in sys.path:
    sys.path.insert(0, str(_AUTHORING_ROOT))

from authoring_common import (  # noqa: E402
    add_routed_actor_init,
    assemble_scenario,
    lane_pos,
    make_ego_vehicle,
    make_npc_vehicle,
    make_route,
    make_virtual_driver_controller,
    repo_root,
    sim_time_trigger,
    step_dynamics,
    write_scenario,
)
from scenariogeneration import xosc  # noqa: E402

# ---------------------------------------------------------------------------
# Road / geometry constants (straight_500m_2lane.xodr) — see module docstring.
# ---------------------------------------------------------------------------
ROADFILE_REL = "../../../xodr/straight_500m_2lane.xodr"
ROAD_ID = 1
LANE_ID = -1
EGO_TELEPORT_S = 30.0
EGO_ROUTE_END_S = 480.0

# Standard authoring_common vehicle bounding box length (make_ego_vehicle /
# make_npc_vehicle: BoundingBox(2.0, 5.0, 1.8, 1.4, 0.0, 0.9) -> length 5.0).
VEHICLE_LENGTH = 5.0

# ---------------------------------------------------------------------------
# Sweep axes (fixed filename contract — do not rename cells).
# ---------------------------------------------------------------------------
CCRS_EGO_KMH = [10.0, 20.0, 30.0, 40.0, 50.0, 60.0, 70.0]
CCRM_EGO_KMH = [30.0, 40.0, 50.0, 60.0, 70.0]
CCRM_LEAD_KMH = 20.0
CCRB_PARAMS = [(12.0, 2.0), (12.0, 6.0), (40.0, 2.0), (40.0, 6.0)]  # (hw_m, decel_mps2)
CCRB_EGO_LEAD_KMH = 50.0
CCRB_BRAKE_TIME_S = 2.0

# ---------------------------------------------------------------------------
# Duration-sizing constants (see module docstring).
# ---------------------------------------------------------------------------
TTC0_TARGET_S = 6.0
MIN_GAP_M = 40.0
ASSUMED_MIN_DECEL = 4.0  # m/s^2 — conservative sizing assumption only
MARGIN_S = 3.0
GLOBAL_MAX_TIME_HEADROOM_S = 2.0  # matches aeb_safety_batch.yaml's "~2s grace"


def kmh_to_ms(v_kmh: float) -> float:
    return v_kmh / 3.6


def bumper_gap_m(closing_speed_ms: float) -> float:
    """TTC0=6s gap against closing speed, floored at MIN_GAP_M."""
    return round(max(MIN_GAP_M, closing_speed_ms * TTC0_TARGET_S), 3)


def ego_stop_time_s(ego_speed_ms: float) -> float:
    return ego_speed_ms / ASSUMED_MIN_DECEL


@dataclass(frozen=True)
class Cell:
    stem: str
    family: str
    scenario: "xosc.Scenario"
    duration: float


# ---------------------------------------------------------------------------
# Shared entity/init builders
# ---------------------------------------------------------------------------


def _make_entities() -> xosc.Entities:
    entities = xosc.Entities()
    entities.add_scenario_object(
        "Ego", make_ego_vehicle(), make_virtual_driver_controller()
    )
    entities.add_scenario_object("Lead", make_npc_vehicle(model_id="1", name="car_red"))
    return entities


def _make_init(ego_speed_ms: float, lead_s: float, lead_speed_ms: float) -> xosc.Init:
    init = xosc.Init()

    ego_route = make_route(
        "ego_route",
        [
            lane_pos(ROAD_ID, LANE_ID, EGO_TELEPORT_S),
            lane_pos(ROAD_ID, LANE_ID, EGO_ROUTE_END_S),
        ],
    )
    add_routed_actor_init(
        init,
        "Ego",
        lane_pos(ROAD_ID, LANE_ID, EGO_TELEPORT_S),
        ego_route,
        ego_speed_ms,
    )
    init.add_init_action(
        "Ego", xosc.ActivateControllerAction(lateral=True, longitudinal=True)
    )

    # Lead: teleport + constant speed only (no route, no controller) — matches
    # the Lead entity in 07_aeb/cutin_hard_brake.xosc before its cut-in.
    init.add_init_action(
        "Lead", xosc.TeleportAction(lane_pos(ROAD_ID, LANE_ID, lead_s))
    )
    init.add_init_action(
        "Lead", xosc.AbsoluteSpeedAction(lead_speed_ms, step_dynamics())
    )
    return init


def _make_brake_act(brake_time_s: float, brake_rate_mps2: float) -> xosc.Act:
    """Story Act: Lead brakes to a full stop at brake_rate_mps2 starting at
    SimulationTime > brake_time_s. Mirrors the LeadBrakeManeuver pattern in
    07_aeb/cutin_hard_brake.xosc (linear/rate dynamics dropping to 0)."""
    act = xosc.Act("LeadBrakeAct", sim_time_trigger("LeadBrakeAct_start", 0.0))
    mg = xosc.ManeuverGroup("Lead_mg")
    mg.add_actor("Lead")
    man = xosc.Maneuver("Lead_brake")
    ev = xosc.Event("Lead_go", xosc.Priority.overwrite)
    ev.add_action(
        "Lead_brake_speed",
        xosc.AbsoluteSpeedAction(
            0.0,
            xosc.TransitionDynamics(
                xosc.DynamicsShapes.linear, xosc.DynamicsDimension.rate, brake_rate_mps2
            ),
        ),
    )
    ev.add_trigger(sim_time_trigger("Lead_brake_trig", brake_time_s))
    man.add_event(ev)
    mg.add_maneuver(man)
    act.add_maneuver_group(mg)
    return act


# ---------------------------------------------------------------------------
# Per-family cell builders
# ---------------------------------------------------------------------------


def build_ccrs(ego_kmh: float) -> Cell:
    stem = f"ccrs_ego{int(ego_kmh)}"
    ego_ms = kmh_to_ms(ego_kmh)
    lead_ms = 0.0
    gap = bumper_gap_m(ego_ms)  # closing speed = ego speed (lead stopped)
    ttc0 = gap / ego_ms
    duration = round(ttc0 + ego_stop_time_s(ego_ms) + MARGIN_S, 1)

    entities = _make_entities()
    init = _make_init(ego_ms, EGO_TELEPORT_S + gap + VEHICLE_LENGTH, lead_ms)
    description = (
        f"AEB c2c grid CCRs: ego={ego_kmh:g}km/h vs stopped lead, gap={gap:g}m"
    )
    scenario = assemble_scenario(
        stem, description, ROADFILE_REL, entities, init, duration
    )
    return Cell(stem, "CCRs", scenario, duration)


def build_ccrm(ego_kmh: float, lead_kmh: float = CCRM_LEAD_KMH) -> Cell:
    stem = f"ccrm_ego{int(ego_kmh)}_lead{int(lead_kmh)}"
    ego_ms = kmh_to_ms(ego_kmh)
    lead_ms = kmh_to_ms(lead_kmh)
    closing_ms = ego_ms - lead_ms
    gap = bumper_gap_m(closing_ms)
    ttc0 = gap / closing_ms
    duration = round(ttc0 + ego_stop_time_s(ego_ms) + MARGIN_S, 1)

    entities = _make_entities()
    init = _make_init(ego_ms, EGO_TELEPORT_S + gap + VEHICLE_LENGTH, lead_ms)
    description = (
        f"AEB c2c grid CCRm: ego={ego_kmh:g}km/h vs lead={lead_kmh:g}km/h, gap={gap:g}m"
    )
    scenario = assemble_scenario(
        stem, description, ROADFILE_REL, entities, init, duration
    )
    return Cell(stem, "CCRm", scenario, duration)


def build_ccrb(hw_m: float, decel_mps2: float) -> Cell:
    stem = f"ccrb_hw{int(hw_m)}_d{int(decel_mps2)}"
    ego_ms = kmh_to_ms(CCRB_EGO_LEAD_KMH)
    lead_ms = kmh_to_ms(CCRB_EGO_LEAD_KMH)
    lead_stop_time = lead_ms / decel_mps2
    duration = round(
        CCRB_BRAKE_TIME_S + max(lead_stop_time, ego_stop_time_s(ego_ms)) + MARGIN_S,
        1,
    )

    entities = _make_entities()
    init = _make_init(ego_ms, EGO_TELEPORT_S + hw_m + VEHICLE_LENGTH, lead_ms)
    brake_act = _make_brake_act(CCRB_BRAKE_TIME_S, decel_mps2)
    description = (
        f"AEB c2c grid CCRb: ego=lead={CCRB_EGO_LEAD_KMH:g}km/h, hw={hw_m:g}m, "
        f"lead brakes at {decel_mps2:g}m/s^2 from t={CCRB_BRAKE_TIME_S:g}s"
    )
    scenario = assemble_scenario(
        stem, description, ROADFILE_REL, entities, init, duration, launch_act=brake_act
    )
    return Cell(stem, "CCRb", scenario, duration)


def build_all_cells() -> list[Cell]:
    """Fixed generation order: CCRs, then CCRm, then CCRb (does not affect the
    filename contract, only the order cells are emitted/listed in)."""
    cells: list[Cell] = []
    for v in CCRS_EGO_KMH:
        cells.append(build_ccrs(v))
    for v in CCRM_EGO_KMH:
        cells.append(build_ccrm(v))
    for hw, d in CCRB_PARAMS:
        cells.append(build_ccrb(hw, d))
    return cells


# ---------------------------------------------------------------------------
# Manifest (resources/xosc/verification/aeb_c2c_grid_batch.yaml)
# ---------------------------------------------------------------------------

OUT_SUBDIR = Path("resources") / "xosc" / "verification" / "aeb_c2c_grid"
MANIFEST_REL = Path("resources") / "xosc" / "verification" / "aeb_c2c_grid_batch.yaml"

_MANIFEST_HEADER = """\
name: aeb_c2c_grid
# AEB car-to-car (CCRs/CCRm/CCRb) パラメトリック・グリッド — 探索スイープ層。
# req-vd-ad:REQ-AD-010, REQ-AD-011 の刺激資産。
#
# 回帰ゲート/CI 非配線: このバッチは scripts/run_regression_gate.ps1 のどのステップにも
# 組み込まれていない。expectations/baseline を一切持たない探索用グリッドであり、
# 常設の合否判定（aeb_safety_batch.yaml が担う）とは別レイヤ。
#
# Generated by resources/scenario_authoring/scenario_templates/gen_aeb_c2c_grid.py
# — do not hand-edit; regenerate instead.
#
# defaults.max_time is a single GLOBAL run ceiling (gt_sim_test.py batch() has
# no per-entry max_time override — see the generator module docstring). Each
# scenario carries its own deterministic <StopTrigger>; shorter cells end
# early via run()'s telemetry-silence grace window.
#
# Run:
#   DriverScript/.venv/Scripts/python.exe GT_esmini/scripts/verification/gt_sim_test.py batch \\
#       resources/xosc/verification/aeb_c2c_grid_batch.yaml --out test_results/web/aeb_c2c_grid
"""


def manifest_text(cells: list[Cell]) -> str:
    global_max_time = round(
        max(c.duration for c in cells) + GLOBAL_MAX_TIME_HEADROOM_S, 1
    )
    lines = [_MANIFEST_HEADER.rstrip("\n")]
    lines.append("defaults:")
    lines.append("  dt: 0.05")
    lines.append("  osi: true")
    lines.append("  snapshots: 0")
    lines.append(f"  max_time: {global_max_time}")
    lines.append("scenarios:")
    for c in cells:
        rel = (OUT_SUBDIR / f"{c.stem}.xosc").as_posix()
        lines.append(f"  - scenario: {rel}")
        lines.append(f"    policies: [lead, aeb]")
    return "\n".join(lines) + "\n"


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def main() -> None:
    cells = build_all_cells()

    out_dir = repo_root() / OUT_SUBDIR
    out_dir.mkdir(parents=True, exist_ok=True)
    for c in cells:
        write_scenario(c.scenario, out_dir / f"{c.stem}.xosc")
        print(f"[aeb_c2c_grid] {c.stem}  family={c.family} duration={c.duration:g}s")

    manifest_path = repo_root() / MANIFEST_REL
    manifest_path.write_text(manifest_text(cells), encoding="utf-8")
    print(f"[aeb_c2c_grid] generated {len(cells)} cells -> {out_dir}")
    print(f"[aeb_c2c_grid] manifest -> {manifest_path}")


if __name__ == "__main__":
    main()
