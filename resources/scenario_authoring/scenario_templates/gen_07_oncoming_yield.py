#!/usr/bin/env python
"""gen_07_oncoming_yield.py — Phase 3d oncoming-yield / left-turn-across-traffic.

SCENE INTENT
------------
Ego approaches the T-junction (road_ref: t_junction__a90) on the EAST main leg and
TURNS LEFT ACROSS the oncoming through-stream into the SOUTH minor leg (RHT
left-turn-across-traffic semantics; lht=false fixed for layer 1). Oncoming
vehicles approach on the WEST main leg and drive STRAIGHT through the junction.
The VirtualDriver must judge the oncoming gaps and decide when to commit the turn.

EMPIRICALLY-DETERMINED TOPOLOGY (t_junction__a90.xodr; see comments below)
-------------------------------------------------------------------------
The three incoming legs of t_junction__a90 (verified from the generated geometry
and an esmini route-resolution dry run):

    road 0 : starts (0,0)     hdg 0     -> WEST main leg  (drive EAST  toward junction)
    road 1 : starts (240,0)   hdg pi    -> EAST main leg  (drive WEST  toward junction)
    road 2 : starts (120,-120) hdg pi/2 -> SOUTH minor leg (drive NORTH toward junction)
    junction box ~ x in [100,140], y ~ 0; minor leg approaches from the south.

A car drives in lane -1 (RHT right-hand lane) in the +s direction toward the
junction (every leg has its successor = the junction). Turn direction into the
minor (south) leg, computed from the travel headings:

    ego from road 0 (heading EAST)  -> south minor  =  -90 deg  -> RIGHT turn
    ego from road 1 (heading WEST)  -> south minor  =  +90 deg  -> LEFT  turn

A RIGHT turn (road 0 start) peels off to the near side and does NOT cross the
oncoming stream. A LEFT turn (road 1 start) crosses directly in front of the
eastbound oncoming traffic. We therefore HARD-CODE:

    EGO START  = road 1 (east leg), lane -1, heading west
    EGO EXIT   = road 2 (south minor), lane 1  (driving south, away from junction)
                 -> traverses junction connecting road 102 (the road1<->road2 left turn)
    ONCOMING   = road 0 (west leg), lane -1, heading east, straight through
                 -> traverses connecting road 100 onto road 1, i.e. the stream the
                    ego's left turn must cross.

(esmini dry run confirmed: ego route resolves road 1 -> conn 102 -> road 2 lane 1;
 oncoming resolves road 0 -> conn 100 -> road 1 lane -1.)

DETERMINISTIC ARRIVAL-TIMING MODEL
----------------------------------
`first_gap_s` is the time headway of the FIRST oncoming car's junction-conflict
arrival relative to the ego's nominal junction-conflict arrival. The conflict
point is taken at the junction centre (~x=120, y=0).

Rather than teleport an oncoming car implausibly far back (the approach leg is
only 100 m, and high speed x long gap easily overflows it), every oncoming car is
teleported AT REST near the leg start (s = ONC_TELEPORT_S) and LAUNCHED at its
constant target speed at a per-car `release_time`, computed so it reaches the
conflict point at the desired arrival time:

    ego_arrival      = EGO_DIST_TO_CONFLICT / ego_speed              (fixed)
    car_k_arrival    = ego_arrival + first_gap_s + k * INTER_GAP_S   (k = 0,1,2)
    onc_travel_time  = ONC_DIST_TO_CONFLICT / oncoming_speed
    release_time_k   = max(0, car_k_arrival - onc_travel_time)

This keeps the model fully deterministic from params (seedless) and physically
clean (constant cruising speed, no implausible teleport distances).

PARAM SWEEP (24 variants, p001..p024)
-------------------------------------
    oncoming_speed in {8.0, 11.0, 14.0} m/s
    first_gap_s    in {1.5, 2.5, 3.5, 4.5}   (densest where the accept/reject
                                              decision boundary is expected — doc
                                              section 9-2)
    oncoming_count in {1, 3}                  (inter-vehicle gap fixed 2.0 s)
  = 3 x 4 x 2 = 24 variants.

Per variant emits: <catalog_id>.xosc, <catalog_id>.meta.yaml,
<catalog_id>.annotation_required.yaml.

Usage:
    DriverScript/.venv/Scripts/python.exe \
        resources/scenario_authoring/scenario_templates/gen_07_oncoming_yield.py
"""

from __future__ import annotations

import argparse
import itertools
import sys
from pathlib import Path

# Make resources/scenario_authoring/ importable as the authoring package root.
_AUTHORING_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(_AUTHORING_ROOT))

from authoring_common import (  # noqa: E402
    add_routed_actor_init,
    assemble_scenario,
    git_short_hash,
    lane_pos,
    make_ego_vehicle,
    make_launch_act,
    make_npc_vehicle,
    make_route,
    make_virtual_driver_controller,
    repo_root,
    write_meta_yaml,
    write_scenario,
)
from scenariogeneration import xosc  # noqa: E402

# ---------------------------------------------------------------------------
# Road / geometry constants (t_junction__a90.xodr) — see module docstring.
# ---------------------------------------------------------------------------
ROAD_REF = "t_junction__a90"
ROADFILE_REL = f"../../road_catalog/generated/{ROAD_REF}.xodr"
ROADFILE_ABS = (
    repo_root()
    / "resources"
    / "scenario_authoring"
    / "road_catalog"
    / "generated"
    / f"{ROAD_REF}.xodr"
)
PHASE = "3d"

LEG_LEN = 100.0  # each approach leg length (s of junction entry)
EGO_ROAD = 1  # east main leg
EGO_EXIT_ROAD = 2  # south minor leg
EGO_EXIT_LANE = 1  # lane +1 on road 2 = driving south, away from junction
ONC_ROAD = 0  # west main leg
ONC_EXIT_ROAD = 1  # straight through onto the east leg
DRIVE_LANE = -1  # RHT right-hand lane toward the junction

EGO_TELEPORT_S = 20.0  # 80 m back from the junction entry (ego "settles")
EGO_SPEED = 13.9  # ~50 km/h cruise

# Half-junction distances to the conflict point (junction centre ~ x=120).
# Ego: road1 entry at s=100 (x=140) then ~16.6 m along conn road 102 to centre.
EGO_HALF_JUNCTION = 16.6
EGO_DIST_TO_CONFLICT = (LEG_LEN - EGO_TELEPORT_S) + EGO_HALF_JUNCTION  # 96.6 m

# Oncoming: teleported at rest near the leg start, launched late. The conflict
# point sits ~20 m past the road0 junction entry along the straight-through
# connecting road 100.
ONC_TELEPORT_S = 2.0
ONC_JUNCTION_OFFSET = 20.0
ONC_DIST_TO_CONFLICT = (LEG_LEN - ONC_TELEPORT_S) + ONC_JUNCTION_OFFSET  # 118 m

INTER_GAP_S = 2.0  # fixed inter-vehicle gap when oncoming_count == 3
STOP_TIME_S = 35.0

# Param-sweep axes (order defines the p### index).
ONCOMING_SPEEDS = [8.0, 11.0, 14.0]
FIRST_GAPS = [1.5, 2.5, 3.5, 4.5]
ONCOMING_COUNTS = [1, 3]


# ---------------------------------------------------------------------------
# tests_for distribution-slice description (doc section 9-2)
# ---------------------------------------------------------------------------


def _gap_slice(first_gap_s: float) -> str:
    """Where in the accept/reject decision distribution this gap probes."""
    if first_gap_s <= 1.5:
        return "tight gap (likely-reject boundary)"
    if first_gap_s <= 2.5:
        return "marginal gap (decision boundary core)"
    if first_gap_s <= 3.5:
        return "comfortable gap (likely-accept boundary)"
    return "wide gap (clear-accept)"


def _tests_for(oncoming_speed: float, first_gap_s: float, oncoming_count: int) -> str:
    stream = (
        "single oncoming car"
        if oncoming_count == 1
        else f"{oncoming_count}-car oncoming platoon"
    )
    return (
        f"left-turn-across-traffic gap judgement vs a {stream} at "
        f"{oncoming_speed:g} m/s; probes the {_gap_slice(first_gap_s)} "
        f"(first_gap={first_gap_s:g}s)"
    )


# ---------------------------------------------------------------------------
# One variant
# ---------------------------------------------------------------------------


def build_variant(
    idx: int, oncoming_speed: float, first_gap_s: float, oncoming_count: int
) -> tuple[xosc.Scenario, dict, dict, str]:
    catalog_id = f"07_oncoming_yield__p{idx:03d}"

    # --- entities -------------------------------------------------------
    entities = xosc.Entities()
    entities.add_scenario_object(
        "Ego", make_ego_vehicle(), make_virtual_driver_controller()
    )
    onc_names = [f"Onc{k}" for k in range(oncoming_count)]
    for k, name in enumerate(onc_names):
        # Stagger the standing oncoming cars along the leg so they don't overlap
        # before launch (later cars sit slightly further back).
        entities.add_scenario_object(
            name, make_npc_vehicle(model_id="1", name="car_red")
        )

    # --- init -----------------------------------------------------------
    init = xosc.Init()

    # Ego: teleport on east leg, route via the left turn onto the south minor leg.
    ego_route = make_route(
        "ego_route",
        [
            lane_pos(EGO_ROAD, DRIVE_LANE, EGO_TELEPORT_S),
            lane_pos(EGO_EXIT_ROAD, EGO_EXIT_LANE, 80.0),
        ],
        xodr_path=ROADFILE_ABS,
    )
    add_routed_actor_init(
        init,
        "Ego",
        lane_pos(EGO_ROAD, DRIVE_LANE, EGO_TELEPORT_S),
        ego_route,
        EGO_SPEED,
    )
    init.add_init_action(
        "Ego", xosc.ActivateControllerAction(lateral=True, longitudinal=True)
    )

    # Oncoming cars: teleported AT REST near the west-leg start, straight-through
    # route, launched later (see make_launch_act). Standing positions are spaced
    # so the cars don't physically overlap while waiting.
    ego_arrival = EGO_DIST_TO_CONFLICT / EGO_SPEED
    onc_travel_time = ONC_DIST_TO_CONFLICT / oncoming_speed
    launches: list[tuple[str, float, float]] = []
    for k, name in enumerate(onc_names):
        stand_s = ONC_TELEPORT_S + k * 7.0  # 7 m apart while waiting (> car length 5)
        onc_route = make_route(
            f"{name}_route",
            [
                lane_pos(ONC_ROAD, DRIVE_LANE, stand_s),
                lane_pos(ONC_EXIT_ROAD, DRIVE_LANE, 80.0),
            ],
            xodr_path=ROADFILE_ABS,
        )
        # Teleport at rest (init speed 0); launch handled by the Story Act.
        add_routed_actor_init(
            init, name, lane_pos(ONC_ROAD, DRIVE_LANE, stand_s), onc_route, 0.0
        )
        car_arrival = ego_arrival + first_gap_s + k * INTER_GAP_S
        # Account for the extra standing setback of trailing cars (they start k*7 m
        # closer to the junction, so their travel distance is shorter).
        travel_time_k = (ONC_DIST_TO_CONFLICT - k * 7.0) / oncoming_speed
        release_t = max(0.0, car_arrival - travel_time_k)
        launches.append((name, round(release_t, 3), oncoming_speed))

    launch_act = make_launch_act("OncomingLaunch", launches)

    description = (
        f"Phase 3d oncoming-yield: ego left-turn across {oncoming_count} oncoming "
        f"car(s) at {oncoming_speed:g} m/s, first gap {first_gap_s:g}s"
    )
    scenario = assemble_scenario(
        catalog_id, description, ROADFILE_REL, entities, init, STOP_TIME_S, launch_act
    )

    # --- meta.yaml (doc section 6.2 schema) -----------------------------
    meta = {
        "catalog_id": catalog_id,
        "kind": "scenario",
        "road_ref": ROAD_REF,
        "phase": PHASE,
        "tests_for": _tests_for(oncoming_speed, first_gap_s, oncoming_count),
        "expected_behavior": (
            "Yield to the oncoming through-stream; commit the left turn only when an "
            "adequate gap appears, with smooth deceleration/creep and no cut-off of "
            "the oncoming car(s)."
        ),
        "evaluation": "annotation",
        "generator": {
            "script": "scenario_templates/gen_07_oncoming_yield.py",
            "params": {
                "oncoming_speed": oncoming_speed,
                "first_gap_s": first_gap_s,
                "oncoming_count": oncoming_count,
                "inter_gap_s": INTER_GAP_S if oncoming_count > 1 else None,
                "ego_speed": EGO_SPEED,
                "lht": False,
            },
        },
        "generated_at_commit": git_short_hash(),
    }

    # --- annotation_required.yaml ---------------------------------------
    annotation = {
        "catalog_id": catalog_id,
        "labels": [
            {
                "id": "gap_acceptance_valid",
                "question": "ギャップ判断は妥当か？（受け入れ／拒否の判断が交通状況に照らして正しいか）",
                "type": "pass_fail_discussion",
            },
            {
                "id": "stop_creep_natural",
                "question": "停止・クリープ挙動は自然か？（不自然な急停止やためらいがないか）",
                "type": "pass_fail_discussion",
            },
            {
                "id": "safety_margin_adequate",
                "question": "対向車との安全マージンは十分か？（割り込み・幅寄せがないか）",
                "type": "pass_fail_discussion",
            },
        ],
    }

    return scenario, meta, annotation, catalog_id


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate the Phase 3d oncoming-yield scenario grid (24 variants)."
    )
    parser.add_argument(
        "--out-dir",
        type=Path,
        default=None,
        metavar="DIR",
        help="Output directory. Default: <this file's dir>/generated.",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    out_dir: Path = (
        args.out_dir
        if args.out_dir is not None
        else (Path(__file__).resolve().parent / "generated")
    )
    out_dir.mkdir(parents=True, exist_ok=True)

    # Grid order: oncoming_speed (outer) x first_gap_s x oncoming_count (inner).
    # Fixed iteration order => stable p### assignment across regenerations.
    grid = list(itertools.product(ONCOMING_SPEEDS, FIRST_GAPS, ONCOMING_COUNTS))
    for i, (spd, gap, count) in enumerate(grid, start=1):
        scenario, meta, annotation, catalog_id = build_variant(i, spd, gap, count)
        write_scenario(scenario, out_dir / f"{catalog_id}.xosc")
        write_meta_yaml(out_dir / f"{catalog_id}.meta.yaml", meta)
        write_meta_yaml(out_dir / f"{catalog_id}.annotation_required.yaml", annotation)
        print(f"[07] {catalog_id}  spd={spd:g} gap={gap:g} count={count}")

    print(f"[07] generated {len(grid)} variants -> {out_dir}")


if __name__ == "__main__":
    main()
