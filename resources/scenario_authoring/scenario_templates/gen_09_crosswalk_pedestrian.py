#!/usr/bin/env python
"""gen_09_crosswalk_pedestrian.py — F2 Phase 3d extension: CrosswalkPedestrianAware.

SCENE INTENT
------------
Ego (VirtualDriverController) cruises a straight road (road_ref:
straight_crosswalk__mid, crosswalk object at s=250, footprint s 248..252) and
must yield to pedestrians at the OpenDRIVE crosswalk: stop for a crossing or
waiting pedestrian, NOT stop when the crosswalk is empty / already cleared, and
— on the ped-signal road (straight_crosswalk_pedsig__mid) — skip the yield for
a waiting ped held at a RED pedestrian signal while still stopping for a
jaywalker crossing against it.

PEDESTRIAN LOCOMOTION (empirically settled — see authoring_common helpers)
--------------------------------------------------------------------------
Static placement + heading + SpeedAction does NOT work: esmini's default
controller moves entities along the LANE (+s), ignoring the cross-road heading
(verified: the ped walked longitudinally along the sidewalk). Explicit relative
Orientation on trajectory vertices ALSO breaks: it resolves against the lane
driving direction, flips FollowTrajectoryAction's initialHeadingSign_ to -1 and
the action ends on the first step. The working, standardized pattern is the
upstream pedestrian.xosc one, minus vertex orientations:

    FollowTrajectoryAction over a 2-vertex polyline (lane +/-2 -> lane -/+2 at
    the crosswalk s, NO vertex Orientation) + step AbsoluteSpeedAction, both in
    one Event released by a SimulationTime trigger.

Verified telemetry: ped crosses laterally at exactly walk_speed (y +4.44 ->
-4.59 at constant x=250 for from_left), OSI dims length 0.6 / width 0.5 / type
'pedestrian'.

DETERMINISTIC ARRIVAL-TIMING MODEL (measured)
---------------------------------------------
Ego teleports at s=100 with init speed 13.9 m/s; the VirtualDriver HOLDS 13.9
on the straight (measured 13.80-13.90), reaching the crosswalk (s=250) at
    EGO_ARRIVAL = (250 - 100) / 13.9 = 10.79 s   (measured ~10.82)
The ped stands on the sidewalk lane centre (|t| = 4.5; roadway edge |t| = 3.5,
ego lane centre t = -1.75) and is released so that:
    conflict : ped is mid-ego-lane at EGO_ARRIVAL
               release = EGO_ARRIVAL - dist_to_ego_lane_mid / ped_speed
               (dist: from_left 6.25 m, from_right 2.75 m)
    lead     : ped steps onto the roadway ~1 s before ego arrival
               release = EGO_ARRIVAL - LEAD_MARGIN - 1.0 / ped_speed
Policy OFF, the conflict variants end with the ego driving through the ped's
position (OBB overlap) — that is the point of the sweep.

PEDESTRIAN SIGNAL (road B, variants 17-20)
------------------------------------------
Road B carries a dynamic type-1000002 signal (id 10, orientation "-", no
driving-lane validity — never an ego signal). Scenario-side single long
TrafficSignalController phase drives it; lamp order is red;green (verified via
OSI: state "on;off" -> red lamp constant; "off;on" -> green lamp constant).

VARIANT MATRIX (20, p001..p020; road A unless stated)
-----------------------------------------------------
    p001-p012 CROSSING sweep: ped_speed {1.0, 1.4, 2.0}
              x direction {from_left, from_right}
              x timing {conflict, lead}          (speed outer .. timing inner)
    p013-p014 WAITING ped at the crosswalk edge (never released), left/right
    p015      NEGATIVE no-ped (ego alone)
    p016      NEGATIVE already-crossed (released at t=1, road clear ~6.7 s
              before ego arrival margin)
    p017      road B: pedsig RED  + WAITING ped  -> ego must NOT stop
    p018      road B: pedsig RED  + JAYWALK      -> ego must stop
    p019      road B: pedsig GREEN + WAITING ped -> ego stops
    p020      road B: pedsig GREEN + crossing    -> ego stops

Per variant emits: <catalog_id>.xosc, <catalog_id>.meta.yaml,
<catalog_id>.annotation_required.yaml. All metas carry policies: [crosswalk]
(build_manifest.py forwards it to the batch manifest).

Usage:
    DriverScript/.venv/Scripts/python.exe \
        resources/scenario_authoring/scenario_templates/gen_09_crosswalk_pedestrian.py
"""

from __future__ import annotations

import argparse
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
    make_crossing_trajectory,
    make_ego_vehicle,
    make_ped_crossing_act,
    make_pedestrian,
    make_route,
    make_virtual_driver_controller,
    step_dynamics,
    write_meta_yaml,
    write_scenario,
)
from scenariogeneration import xosc  # noqa: E402

# ---------------------------------------------------------------------------
# Road / geometry constants (straight_crosswalk[_pedsig]__mid)
# ---------------------------------------------------------------------------
ROAD_A = "straight_crosswalk__mid"
ROAD_B = "straight_crosswalk_pedsig__mid"
ROADFILE_REL = {
    ROAD_A: f"../../road_catalog/generated/{ROAD_A}.xodr",
    ROAD_B: f"../../road_catalog/generated/{ROAD_B}.xodr",
}
PHASE = "3d-crosswalk"

ROAD_ID = 0
DRIVE_LANE = -1  # RHT: ego drives lane -1 in +s
CROSSWALK_S = 250.0  # crosswalk centre (footprint 248..252)
EGO_START_S = 100.0
EGO_ROUTE_END_S = 450.0
EGO_SPEED = 13.9  # measured VirtualDriver cruise hold (13.80-13.90)
EGO_ARRIVAL = (CROSSWALK_S - EGO_START_S) / EGO_SPEED  # 10.79 s

# Sidewalk standing lanes and lateral distances (sidewalk centre |t| = 4.5,
# roadway edge |t| = 3.5, ego lane centre t = -1.75).
SIDEWALK_LANE = {"from_left": 2, "from_right": -2}
DEST_LANE = {"from_left": -2, "from_right": 2}
DIST_TO_EGO_LANE_MID = {"from_left": 6.25, "from_right": 2.75}
DIST_TO_ROADWAY = 1.0  # sidewalk centre -> roadway edge, both sides
LEAD_MARGIN = 1.0  # 'lead' timing: roadway entry this long before ego

# Standing pose: relative h=+pi/2 resolves (via the lane driving direction) to
# facing the roadway on BOTH sidewalks (verified: world h -pi/2 on the left
# sidewalk, +pi/2 on the right).
PED_FACE_ROAD_H = 1.5708

STOP_TIME_S = 35.0
PED_NAME = "Ped"

# Pedestrian signal (road B). Lamp order red;green — verified via OSI colors.
PEDSIG_ID = "10"
PEDSIG_RED_STATE = "on;off"
PEDSIG_GREEN_STATE = "off;on"
PEDSIG_PHASE_DURATION = 60.0  # single long phase, > max_time

# Param-sweep axes (order defines the p### index).
PED_SPEEDS = [1.0, 1.4, 2.0]
DIRECTIONS = ["from_left", "from_right"]
TIMINGS = ["conflict", "lead"]

ALREADY_CROSSED_RELEASE = 1.0
ALREADY_CROSSED_SPEED = 1.4


# ---------------------------------------------------------------------------
# Variant spec table (deterministic order -> stable p### assignment)
# ---------------------------------------------------------------------------


def build_specs() -> list[dict]:
    specs: list[dict] = []
    # p001-p012 crossing sweep
    for spd in PED_SPEEDS:
        for direction in DIRECTIONS:
            for timing in TIMINGS:
                specs.append(
                    {
                        "kind": "crossing",
                        "road": ROAD_A,
                        "ped_speed": spd,
                        "direction": direction,
                        "timing": timing,
                        "signal": None,
                    }
                )
    # p013-p014 waiting
    for direction in DIRECTIONS:
        specs.append(
            {
                "kind": "waiting",
                "road": ROAD_A,
                "ped_speed": None,
                "direction": direction,
                "timing": None,
                "signal": None,
            }
        )
    # p015 no-ped
    specs.append(
        {
            "kind": "no_ped",
            "road": ROAD_A,
            "ped_speed": None,
            "direction": None,
            "timing": None,
            "signal": None,
        }
    )
    # p016 already-crossed
    specs.append(
        {
            "kind": "already_crossed",
            "road": ROAD_A,
            "ped_speed": ALREADY_CROSSED_SPEED,
            "direction": "from_left",
            "timing": "early",
            "signal": None,
        }
    )
    # p017-p020 ped-signal road
    specs.append(
        {
            "kind": "red_waiting",
            "road": ROAD_B,
            "ped_speed": None,
            "direction": "from_left",
            "timing": None,
            "signal": "red",
        }
    )
    specs.append(
        {
            "kind": "red_jaywalk",
            "road": ROAD_B,
            "ped_speed": 1.4,
            "direction": "from_left",
            "timing": "conflict",
            "signal": "red",
        }
    )
    specs.append(
        {
            "kind": "green_waiting",
            "road": ROAD_B,
            "ped_speed": None,
            "direction": "from_left",
            "timing": None,
            "signal": "green",
        }
    )
    specs.append(
        {
            "kind": "green_crossing",
            "road": ROAD_B,
            "ped_speed": 1.4,
            "direction": "from_left",
            "timing": "conflict",
            "signal": "green",
        }
    )
    return specs


def crossing_release_time(ped_speed: float, direction: str, timing: str) -> float:
    """Release time for a crossing ped so the conflict/lead timing holds."""
    if timing == "conflict":
        return max(0.0, EGO_ARRIVAL - DIST_TO_EGO_LANE_MID[direction] / ped_speed)
    if timing == "lead":
        return max(0.0, EGO_ARRIVAL - LEAD_MARGIN - DIST_TO_ROADWAY / ped_speed)
    if timing == "early":
        return ALREADY_CROSSED_RELEASE
    raise ValueError(f"unknown timing '{timing}'")


# ---------------------------------------------------------------------------
# tests_for / expected_behavior text
# ---------------------------------------------------------------------------

_TESTS_FOR = {
    "crossing": lambda sp: (
        f"crosswalk yield to a pedestrian crossing at {sp['ped_speed']:g} m/s "
        f"{sp['direction']} with {sp['timing']} timing "
        f"({'ped mid-ego-lane at ego arrival' if sp['timing'] == 'conflict' else 'ped steps onto the roadway just before ego arrives'})"
    ),
    "waiting": lambda sp: (
        f"yield decision for a pedestrian WAITING at the crosswalk edge "
        f"({sp['direction']}, off the roadway, never enters)"
    ),
    "no_ped": lambda sp: "negative control: empty crosswalk must not slow the ego",
    "already_crossed": lambda sp: (
        "negative control: pedestrian released early fully clears the road "
        "well before ego arrival — no stop"
    ),
    "red_waiting": lambda sp: (
        "ped-signal discriminator: waiting ped held at a RED pedestrian signal "
        "— ego must NOT stop"
    ),
    "red_jaywalk": lambda sp: (
        "safety over signal: ped crosses AGAINST the red pedestrian signal — "
        "ego must still stop"
    ),
    "green_waiting": lambda sp: (
        "waiting ped at a GREEN pedestrian signal — ego must yield (stop)"
    ),
    "green_crossing": lambda sp: (
        "crossing ped at a GREEN pedestrian signal — ego must yield (stop)"
    ),
}

_EXPECTED = {
    "crossing": (
        "Decelerate smoothly and stop (or deep-crawl) before the crosswalk, let "
        "the pedestrian clear the path, then resume without chatter."
    ),
    "waiting": (
        "Recognize the waiting pedestrian at the crosswalk edge and yield "
        "(stop/creep) at the crosswalk; no plow-through at cruise speed."
    ),
    "no_ped": (
        "Maintain cruise speed through the empty crosswalk; no unnecessary "
        "braking or stop."
    ),
    "already_crossed": (
        "Pedestrian has fully cleared the roadway before arrival; maintain "
        "cruise speed, no unnecessary stop."
    ),
    "red_waiting": (
        "The waiting pedestrian is held by the RED pedestrian signal; the ego "
        "must keep cruising (stopping here is the false-positive failure)."
    ),
    "red_jaywalk": (
        "The pedestrian violates the red — safety wins over signal state: stop "
        "before the crosswalk and yield."
    ),
    "green_waiting": (
        "GREEN pedestrian signal: the waiting pedestrian is entitled to cross; "
        "yield at the crosswalk."
    ),
    "green_crossing": (
        "GREEN pedestrian signal and the pedestrian is crossing; stop before "
        "the crosswalk and yield."
    ),
}


# ---------------------------------------------------------------------------
# One variant
# ---------------------------------------------------------------------------


def build_variant(idx: int, sp: dict) -> tuple[xosc.Scenario, dict, dict, str]:
    catalog_id = f"09_crosswalk_pedestrian__p{idx:03d}"
    kind = sp["kind"]
    has_ped = kind != "no_ped"
    is_crossing = sp["ped_speed"] is not None

    # --- entities ---------------------------------------------------------
    entities = xosc.Entities()
    entities.add_scenario_object(
        "Ego", make_ego_vehicle(), make_virtual_driver_controller()
    )
    if has_ped:
        entities.add_scenario_object(PED_NAME, make_pedestrian())

    # --- init -------------------------------------------------------------
    init = xosc.Init()
    ego_route = make_route(
        "ego_route",
        [
            lane_pos(ROAD_ID, DRIVE_LANE, EGO_START_S),
            lane_pos(ROAD_ID, DRIVE_LANE, EGO_ROUTE_END_S),
        ],
    )
    add_routed_actor_init(
        init, "Ego", lane_pos(ROAD_ID, DRIVE_LANE, EGO_START_S), ego_route, EGO_SPEED
    )
    init.add_init_action(
        "Ego", xosc.ActivateControllerAction(lateral=True, longitudinal=True)
    )

    release_t = None
    if has_ped:
        start_lane = SIDEWALK_LANE[sp["direction"]]
        # Standing pose: sidewalk lane centre at the crosswalk s, facing the
        # roadway (relative h=+pi/2 resolves to face the road on both sides).
        ped_start = xosc.LanePosition(
            CROSSWALK_S,
            0.0,
            str(start_lane),
            str(ROAD_ID),
            orientation=xosc.Orientation(
                h=PED_FACE_ROAD_H, reference=xosc.ReferenceContext.relative
            ),
        )
        init.add_init_action(PED_NAME, xosc.TeleportAction(ped_start))
        init.add_init_action(PED_NAME, xosc.AbsoluteSpeedAction(0.0, step_dynamics()))

    # --- launch act (crossing variants only) --------------------------------
    launch_act = None
    if is_crossing:
        release_t = round(
            crossing_release_time(sp["ped_speed"], sp["direction"], sp["timing"]), 3
        )
        traj = make_crossing_trajectory(
            "ped_crossing",
            ROAD_ID,
            CROSSWALK_S,
            SIDEWALK_LANE[sp["direction"]],
            DEST_LANE[sp["direction"]],
        )
        launch_act = make_ped_crossing_act(
            "PedLaunch", PED_NAME, release_t, sp["ped_speed"], traj
        )

    description_bits = [f"Phase 3d crosswalk: {kind}"]
    if is_crossing:
        description_bits.append(
            f"ped {sp['ped_speed']:g} m/s {sp['direction']} {sp['timing']} "
            f"(release {release_t:g}s)"
        )
    elif kind in ("waiting", "red_waiting", "green_waiting"):
        description_bits.append(f"ped standing {sp['direction']} at the crosswalk edge")
    if sp["signal"]:
        description_bits.append(f"ped-signal {sp['signal'].upper()}")
    description = "; ".join(description_bits)

    scenario = assemble_scenario(
        catalog_id,
        description,
        ROADFILE_REL[sp["road"]],
        entities,
        init,
        STOP_TIME_S,
        launch_act,
    )

    # --- ped-signal phases (road B): single long-duration phase ------------
    if sp["signal"] is not None:
        state = PEDSIG_RED_STATE if sp["signal"] == "red" else PEDSIG_GREEN_STATE
        tsc = xosc.TrafficSignalController("PedSignalController")
        ph = xosc.Phase(sp["signal"], PEDSIG_PHASE_DURATION)
        ph.add_signal_state(PEDSIG_ID, state)
        tsc.add_phase(ph)
        scenario.roadnetwork.add_traffic_signal_controller(tsc)

    # --- meta.yaml ----------------------------------------------------------
    params: dict = {
        "kind": kind,
        "ego_speed": EGO_SPEED,
        "ego_arrival_s": round(EGO_ARRIVAL, 3),
        "crosswalk_s": CROSSWALK_S,
    }
    if has_ped:
        params["direction"] = sp["direction"]
    if is_crossing:
        params["ped_speed"] = sp["ped_speed"]
        params["timing_offset"] = sp["timing"]
        params["release_t"] = release_t
    if sp["signal"]:
        params["ped_signal"] = sp["signal"]
        params["ped_signal_state"] = (
            PEDSIG_RED_STATE if sp["signal"] == "red" else PEDSIG_GREEN_STATE
        )

    meta = {
        "catalog_id": catalog_id,
        "kind": "scenario",
        "road_ref": sp["road"],
        "phase": PHASE,
        "tests_for": _TESTS_FOR[kind](sp),
        "expected_behavior": _EXPECTED[kind],
        "evaluation": "annotation",
        "policies": ["crosswalk"],
        "generator": {
            "script": "scenario_templates/gen_09_crosswalk_pedestrian.py",
            "params": params,
        },
        "generated_at_commit": git_short_hash(),
    }

    # --- annotation_required.yaml -------------------------------------------
    annotation = {
        "catalog_id": catalog_id,
        "labels": [
            {
                "id": "yielded_to_waiting_ped",
                "question": "横断歩道の歩行者（横断中／待機中）への譲りは適切か？（信号赤で待機中の歩行者には譲らないのが正解）",
                "type": "pass_fail_discussion",
            },
            {
                "id": "no_unnecessary_stop",
                "question": "不要な停止・過剰な減速がないか？（歩行者不在／横断済み／赤信号待機の場面で巡航を維持できているか）",
                "type": "pass_fail_discussion",
            },
            {
                "id": "smooth_stop_no_chatter",
                "question": "停止・再発進は滑らかか？（急減速・チャタリング・ブレーキ点滅がないか）",
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
        description="Generate the Phase 3d crosswalk-pedestrian scenario grid (20 variants)."
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

    specs = build_specs()
    for i, sp in enumerate(specs, start=1):
        scenario, meta, annotation, catalog_id = build_variant(i, sp)
        write_scenario(scenario, out_dir / f"{catalog_id}.xosc")
        write_meta_yaml(out_dir / f"{catalog_id}.meta.yaml", meta)
        write_meta_yaml(out_dir / f"{catalog_id}.annotation_required.yaml", annotation)
        extra = ""
        if sp["ped_speed"] is not None:
            extra = f" spd={sp['ped_speed']:g} {sp['direction']} {sp['timing']}"
        elif sp["direction"]:
            extra = f" {sp['direction']}"
        if sp["signal"]:
            extra += f" sig={sp['signal']}"
        print(f"[09] {catalog_id}  {sp['kind']}{extra}")

    print(f"[09] generated {len(specs)} variants -> {out_dir}")


if __name__ == "__main__":
    main()
