#!/usr/bin/env python
"""gen_08_unsignalized.py — Phase 3e unsignalized priority-junction judgement.

SCENE INTENT
------------
Ego drives through an unsignalized priority junction while ONE cross vehicle
approaches on the crossing road, its arrival timed relative to the ego's nominal
arrival at the conflict point. The VirtualDriver must obey the right-of-way
implied by the road's priority/yield signage: proceed when it has priority,
yield when it does not.

PARAM SWEEP (12 variants, p001..p012)
-------------------------------------
    junction              in {4way_priority__main_ns, t_junction_priority__a90}
    ego_on_priority       in {true, false}
    cross_arrival_offset_s in {-2.0, 0.0, +2.0}
  = 2 x 2 x 3 = 12 variants.

`cross_arrival_offset_s` is how many seconds BEFORE(-) / AFTER(+) the ego's
nominal conflict-point arrival the cross vehicle reaches the same conflict point.
Offset 0 = simultaneous arrival (the hardest case); -2 = cross clearly first;
+2 = ego clearly first.

TOPOLOGY (empirically resolved from the generated xodrs + esmini dry runs)
--------------------------------------------------------------------------
4way_priority__main_ns  (legs: 0=E, 1=N, 2=W, 3=S; main/priority pair = N/S = (1,3),
                         minor pair = E/W = (0,2); <priority> high=NS-through):
    ego_on_priority=true : ego straight N->S  (road 1 -> conn 100 -> road 3),
                           cross straight E->W (road 0 -> conn 101 -> road 2).
                           Ego has right-of-way; cross must yield.
    ego_on_priority=false: ego straight E->W  (road 0 -> conn 101 -> road 2),
                           cross straight N->S (road 1 -> conn 100 -> road 3).
                           Ego is on the minor road and must yield.

t_junction_priority__a90 (roads: 0=W main, 1=E main, 2=S minor; main through = roads
                          0<->1, YIELD sign on minor road 2; <priority> high=through):
    A T-junction's minor leg cannot go "straight through", so:
    ego_on_priority=true : ego straight along the MAIN road  (road 0 W -> conn 100 ->
                           road 1 E). The cross vehicle comes off the MINOR leg
                           (road 2 S -> conn 102 -> road 1) and must yield.
    ego_on_priority=false: ego is on the MINOR leg and must TURN onto the main road
                           (road 2 S -> conn 102 -> road 1, exit lane 1). The cross
                           vehicle drives straight on the MAIN road (road 0 W ->
                           conn 100 -> road 1) and has right-of-way. (This is the
                           "ego must turn onto the main road" case the spec calls for.)

DETERMINISTIC ARRIVAL TIMING
----------------------------
Same launch-delay model as gen_07: the cross vehicle is teleported AT REST near
its leg start and launched at constant speed at a `release_time` so it reaches the
conflict point (junction centre) at  ego_nominal_arrival + cross_arrival_offset_s.
Speeds ~11 m/s for both actors. Stop trigger at 35 s.

Usage:
    DriverScript/.venv/Scripts/python.exe \
        resources/scenario_authoring/scenario_templates/gen_08_unsignalized.py
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
    write_meta_yaml,
    write_scenario,
)
from scenariogeneration import xosc  # noqa: E402

PHASE = "3e"
STOP_TIME_S = 35.0
SPEED = 11.0  # ~40 km/h, both ego and cross
EGO_TELEPORT_S = 20.0  # 80 m back from the junction entry
CROSS_TELEPORT_S = 2.0  # cross vehicle waits near its leg start, launched late
LEG_LEN = 100.0

# Distances to the conflict point (junction centre). Approach legs are 100 m;
# straight-through movements reach the centre ~20 m past the junction entry; turn
# movements (the T-minor turn) reach it ~16.6 m past the entry. Using a single
# nominal half-junction distance for both is adequate for timing the *arrival
# offset* (it only shifts the absolute release time, not the relative offset the
# annotation cares about).
EGO_HALF_JUNCTION = 18.0
CROSS_HALF_JUNCTION = 18.0
EGO_DIST_TO_CONFLICT = (LEG_LEN - EGO_TELEPORT_S) + EGO_HALF_JUNCTION
CROSS_DIST_TO_CONFLICT = (LEG_LEN - CROSS_TELEPORT_S) + CROSS_HALF_JUNCTION


# ---------------------------------------------------------------------------
# Topology table: (junction, ego_on_priority) -> route spec.
# Each entry is a dict of (road_id, lane_id) waypoint pairs for ego + cross.
# lane_id is the lane the actor occupies on that road (RHT: -1 toward, +1 away).
# ---------------------------------------------------------------------------

# 4-way: ego/cross both drive straight; entry lane -1, exit lane -1.
_4WAY = "4way_priority__main_ns"
_TPRIO = "t_junction_priority__a90"

# Each topology entry: ego_entry (road,lane), ego_exit (road,lane),
#                      cross_entry (road,lane), cross_exit (road,lane),
#                      ego_role, cross_role  (for documentation / expected_behavior).
_TOPOLOGY: dict[tuple[str, bool], dict] = {
    (_4WAY, True): {
        "ego_entry": (1, -1),
        "ego_exit": (3, -1),  # N -> S straight (priority)
        "cross_entry": (0, -1),
        "cross_exit": (2, -1),  # E -> W straight (minor)
        "ego_move": "straight through on the N-S priority road",
        "cross_move": "straight through on the E-W minor road",
        "ego_has_row": True,
    },
    (_4WAY, False): {
        "ego_entry": (0, -1),
        "ego_exit": (2, -1),  # E -> W straight (minor)
        "cross_entry": (1, -1),
        "cross_exit": (3, -1),  # N -> S straight (priority)
        "ego_move": "straight through on the E-W minor road",
        "cross_move": "straight through on the N-S priority road",
        "ego_has_row": False,
    },
    (_TPRIO, True): {
        "ego_entry": (0, -1),
        "ego_exit": (1, -1),  # W -> E straight on main
        "cross_entry": (2, -1),
        "cross_exit": (1, 1),  # S minor -> turn onto main
        "ego_move": "straight through on the main road",
        "cross_move": "turning off the minor (yield) leg onto the main road",
        "ego_has_row": True,
    },
    (_TPRIO, False): {
        "ego_entry": (2, -1),
        "ego_exit": (1, 1),  # S minor -> turn onto main
        "cross_entry": (0, -1),
        "cross_exit": (1, -1),  # W -> E straight on main
        "ego_move": "turning off the minor (yield) leg onto the main road",
        "cross_move": "straight through on the main road",
        "ego_has_row": False,
    },
}

JUNCTIONS = [_4WAY, _TPRIO]
EGO_ON_PRIORITY = [True, False]
CROSS_OFFSETS = [-2.0, 0.0, 2.0]


def _offset_slice(offset: float) -> str:
    if offset < 0:
        return "cross-first (cross clears before ego)"
    if offset == 0:
        return "simultaneous arrival (tightest conflict)"
    return "ego-first (ego clears before cross)"


# ---------------------------------------------------------------------------
# One variant
# ---------------------------------------------------------------------------


def build_variant(
    idx: int, junction: str, ego_on_priority: bool, cross_offset: float
) -> tuple[xosc.Scenario, dict, dict, str]:
    catalog_id = f"08_unsignalized_junction__p{idx:03d}"
    topo = _TOPOLOGY[(junction, ego_on_priority)]
    roadfile_rel = f"../../road_catalog/generated/{junction}.xodr"

    # --- entities -------------------------------------------------------
    entities = xosc.Entities()
    entities.add_scenario_object(
        "Ego", make_ego_vehicle(), make_virtual_driver_controller()
    )
    entities.add_scenario_object(
        "Cross", make_npc_vehicle(model_id="1", name="car_red")
    )

    # --- init -----------------------------------------------------------
    init = xosc.Init()

    ego_er, ego_el = topo["ego_entry"]
    ego_xr, ego_xl = topo["ego_exit"]
    ego_route = make_route(
        "ego_route",
        [lane_pos(ego_er, ego_el, EGO_TELEPORT_S), lane_pos(ego_xr, ego_xl, 80.0)],
    )
    add_routed_actor_init(
        init, "Ego", lane_pos(ego_er, ego_el, EGO_TELEPORT_S), ego_route, SPEED
    )
    init.add_init_action(
        "Ego", xosc.ActivateControllerAction(lateral=True, longitudinal=True)
    )

    cross_er, cross_el = topo["cross_entry"]
    cross_xr, cross_xl = topo["cross_exit"]
    cross_route = make_route(
        "cross_route",
        [
            lane_pos(cross_er, cross_el, CROSS_TELEPORT_S),
            lane_pos(cross_xr, cross_xl, 80.0),
        ],
    )
    # Teleport the cross vehicle at rest; launch it late via the Story Act.
    add_routed_actor_init(
        init, "Cross", lane_pos(cross_er, cross_el, CROSS_TELEPORT_S), cross_route, 0.0
    )

    ego_arrival = EGO_DIST_TO_CONFLICT / SPEED
    cross_travel_time = CROSS_DIST_TO_CONFLICT / SPEED
    cross_target_arrival = ego_arrival + cross_offset
    cross_release = max(0.0, cross_target_arrival - cross_travel_time)
    launch_act = make_launch_act(
        "CrossLaunch", [("Cross", round(cross_release, 3), SPEED)]
    )

    description = (
        f"Phase 3e unsignalized {junction}: ego "
        f"{'(priority)' if ego_on_priority else '(minor/yield)'} {topo['ego_move']}, "
        f"cross arrival offset {cross_offset:+g}s"
    )
    scenario = assemble_scenario(
        catalog_id, description, roadfile_rel, entities, init, STOP_TIME_S, launch_act
    )

    # --- meta.yaml (doc section 6.2 schema) -----------------------------
    if topo["ego_has_row"]:
        expected = (
            f"Ego has right-of-way ({topo['ego_move']}); proceed without "
            f"unnecessary yielding while the cross vehicle ({topo['cross_move']}) "
            f"gives way. No deadlock."
        )
    else:
        expected = (
            f"Ego must yield ({topo['ego_move']}); let the priority cross vehicle "
            f"({topo['cross_move']}) pass, then proceed. No cut-off, no deadlock."
        )
    meta = {
        "catalog_id": catalog_id,
        "kind": "scenario",
        "road_ref": junction,
        "phase": PHASE,
        "tests_for": (
            f"unsignalized priority compliance with ego on the "
            f"{'priority' if ego_on_priority else 'minor (yield)'} approach; probes "
            f"the {_offset_slice(cross_offset)} (cross_arrival_offset={cross_offset:+g}s)"
        ),
        "expected_behavior": expected,
        "evaluation": "annotation",
        "generator": {
            "script": "scenario_templates/gen_08_unsignalized.py",
            "params": {
                "junction": junction,
                "ego_on_priority": ego_on_priority,
                "cross_arrival_offset_s": cross_offset,
                "speed": SPEED,
                "ego_move": topo["ego_move"],
                "cross_move": topo["cross_move"],
            },
        },
        "generated_at_commit": git_short_hash(),
    }

    # --- annotation_required.yaml ---------------------------------------
    annotation = {
        "catalog_id": catalog_id,
        "labels": [
            {
                "id": "yield_correctness",
                "question": "優先関係に従った譲り判断は正しいか？（優先側なら不要な停止をせず、非優先側なら確実に譲るか）",
                "type": "pass_fail_discussion",
            },
            {
                "id": "gap_judgment",
                "question": "交差車との通過タイミング（ギャップ）判断は妥当か？",
                "type": "pass_fail_discussion",
            },
            {
                "id": "no_deadlock",
                "question": "デッドロック（相互停止のまま動けない）に陥らず、交差点を通過できたか？",
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
        description="Generate the Phase 3e unsignalized-junction scenario grid (12 variants)."
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

    # Grid order: junction (outer) x ego_on_priority x cross_offset (inner).
    grid = list(itertools.product(JUNCTIONS, EGO_ON_PRIORITY, CROSS_OFFSETS))
    for i, (junction, ego_prio, offset) in enumerate(grid, start=1):
        scenario, meta, annotation, catalog_id = build_variant(
            i, junction, ego_prio, offset
        )
        write_scenario(scenario, out_dir / f"{catalog_id}.xosc")
        write_meta_yaml(out_dir / f"{catalog_id}.meta.yaml", meta)
        write_meta_yaml(out_dir / f"{catalog_id}.annotation_required.yaml", annotation)
        print(f"[08] {catalog_id}  {junction} ego_prio={ego_prio} offset={offset:+g}")

    print(f"[08] generated {len(grid)} variants -> {out_dir}")


if __name__ == "__main__":
    main()
