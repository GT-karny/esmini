#!/usr/bin/env python
"""gen_4way_priority.py — generate a 4-way unsignalized priority junction.

Produces a G5 (4-way crossing) + G13 (priority road) road for VirtualDriver
Phase 3e: a four-legged crossing where one through-pair is the priority road
(right-of-way) and the crossing pair must yield. Pipeline:

    1. scenariogeneration builds the geometry + connections (CommonJunctionCreator).
    2. (optional) signage: priority-road sign on both main approaches +
       YIELD sign on both minor approaches.
    3. priority_injector injects OpenDRIVE <priority high low> records keyed to
       the main through-pair's connecting roads (scenariogeneration cannot emit
       these — see priority_injector.py docstring).
    4. write <catalog_id>.xodr + <catalog_id>.road.meta.yaml.

LEG / DIRECTION MAPPING (circular geometry angles, CCW from +x):
    leg 0 -> angle   0  -> East
    leg 1 -> angle  90  -> North
    leg 2 -> angle 180  -> West
    leg 3 -> angle 270  -> South
    NS through-pair = legs (1, 3);  EW through-pair = legs (0, 2).

SIGNAGE CONVENTIONS (mirrored exactly from resources/xodr/straight_yield_sign.xodr
and straight_stop_sign.xodr so the existing StopYieldSignAware policy recognizes
the yield signs):
    - YIELD (give-way): OpenDRIVE type "205", country "de"  -> OSI TYPE_GIVE_WAY
      (resources/traffic_signals/de_traffic_signals.txt: 205=TYPE_GIVE_WAY).
    - PRIORITY ROAD sign: OpenDRIVE type "306", country "de" -> OSI
      TYPE_RIGHT_OF_WAY_BEGIN (German StVO 306 "Vorfahrtstraße";
      de_traffic_signals.txt: 306=TYPE_RIGHT_OF_WAY_BEGIN).
      Country choice = "de" because the in-tree Swedish country file
      (se_traffic_signals.txt) defines NO give-way/stop/priority codes, so a
      Swedish yield sign would resolve to no OSI type and StopYieldSignAware
      would not recognize it. "de" matches the existing hand-authored yield/stop
      assets exactly, keeping policy recognition intact.
    - Placement mirrors straight_yield_sign.xodr: t=-3.57 (right of centreline,
      governing lane -1), orientation "+", zOffset 1.7, on the approach leg a
      short distance before the junction stop line.
"""

from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path

# Make resources/scenario_authoring/ importable as the authoring package root.
_AUTHORING_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(_AUTHORING_ROOT))

from authoring_common import (
    git_short_hash,
    normalize_header_date,
    write_meta_yaml,
)  # noqa: E402
from priority_injector import inject_priority  # noqa: E402
from scenariogeneration import xodr  # noqa: E402

# Pinned OpenDRIVE header date (scenariogeneration stamps datetime.now()).
# Frozen so regeneration is byte-reproducible (see normalize_header_date).
_PINNED_DATE = "2026-06-13 00:00:00.000000"

# Leg id -> compass direction (see module docstring).
_LEG_DIR = {0: "E", 1: "N", 2: "W", 3: "S"}
_THROUGH_PAIRS = {"ns": (1, 3), "ew": (0, 2)}

# Signage conventions mirrored from straight_yield_sign.xodr / straight_stop_sign.xodr.
_YIELD_TYPE = "205"  # de: TYPE_GIVE_WAY
_PRIORITY_TYPE = "306"  # de: TYPE_RIGHT_OF_WAY_BEGIN (priority road)
_SIGN_COUNTRY = "de"
_SIGN_T = -3.57  # right of centreline, governs lane -1
_SIGN_ZOFFSET = 1.7
_SIGN_SETBACK = 8.0  # metres before the junction (leg end) the sign sits


# ---------------------------------------------------------------------------
# Road network builder
# ---------------------------------------------------------------------------


def make_4way_road(
    main: str,
    lanes: int,
    leg_length: float,
    add_signage: bool,
) -> tuple[xodr.OpenDrive, list[int], list[int]]:
    """Return (OpenDrive, main_leg_ids, minor_leg_ids) for a 4-way junction.

    Four legs at 0/90/180/270 deg share a CommonJunctionCreator (junction
    id=100, radius=20). The *main* through-pair gets the priority-road sign on
    both approaches; the minor pair gets YIELD signs (when add_signage).
    """
    main_legs = list(_THROUGH_PAIRS[main])
    minor_legs = list(_THROUGH_PAIRS["ew" if main == "ns" else "ns"])

    roads = [
        xodr.create_road(
            xodr.Line(leg_length), id=i, left_lanes=lanes, right_lanes=lanes
        )
        for i in range(4)
    ]

    junction_creator = xodr.CommonJunctionCreator(id=100, name="X4")
    for i, ang in enumerate([0.0, math.pi / 2, math.pi, 3 * math.pi / 2]):
        junction_creator.add_incoming_road_circular_geometry(
            roads[i], radius=20, angle=ang, road_connection="successor"
        )

    # Through-movements for both pairs + every crossing movement.
    main_a, main_b = main_legs
    minor_a, minor_b = minor_legs
    junction_creator.add_connection(
        road_one_id=main_a, road_two_id=main_b
    )  # priority through
    junction_creator.add_connection(
        road_one_id=minor_a, road_two_id=minor_b
    )  # minor through
    # Crossing connections (each main leg <-> each minor leg).
    for m in main_legs:
        for n in minor_legs:
            junction_creator.add_connection(road_one_id=m, road_two_id=n)

    odr = xodr.OpenDrive("4way_priority")
    for r in roads:
        odr.add_road(r)
    odr.add_junction_creator(junction_creator)
    odr.adjust_roads_and_lanes()

    if add_signage:
        # Priority-road sign on both main approaches.
        for leg in main_legs:
            roads[leg].add_signal(
                xodr.Signal(
                    s=leg_length - _SIGN_SETBACK,
                    t=_SIGN_T,
                    country=_SIGN_COUNTRY,
                    Type=_PRIORITY_TYPE,
                    subtype="-1",
                    name=f"priority_road_{_LEG_DIR[leg]}",
                    dynamic=xodr.Dynamic.no,
                    orientation=xodr.Orientation.positive,
                    zOffset=_SIGN_ZOFFSET,
                )
            )
        # YIELD sign on both minor approaches.
        for leg in minor_legs:
            roads[leg].add_signal(
                xodr.Signal(
                    s=leg_length - _SIGN_SETBACK,
                    t=_SIGN_T,
                    country=_SIGN_COUNTRY,
                    Type=_YIELD_TYPE,
                    subtype="-1",
                    name=f"yield_{_LEG_DIR[leg]}",
                    dynamic=xodr.Dynamic.no,
                    orientation=xodr.Orientation.positive,
                    zOffset=_SIGN_ZOFFSET,
                )
            )

    return odr, main_legs, minor_legs


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate a 4-way unsignalized priority junction xodr + road.meta.yaml."
    )
    parser.add_argument(
        "--main",
        choices=["ns", "ew"],
        default="ns",
        help="Which through-pair is the priority road. Default: ns.",
    )
    parser.add_argument(
        "--lanes",
        type=int,
        default=1,
        metavar="N",
        help="Lanes per direction on each leg. Default: 1.",
    )
    parser.add_argument(
        "--leg-length",
        type=float,
        default=100.0,
        metavar="M",
        help="Length of each approach leg in metres. Default: 100.0.",
    )
    parser.add_argument(
        "--signage",
        dest="signage",
        action="store_true",
        default=True,
        help="Add priority-road + yield signs (default ON).",
    )
    parser.add_argument(
        "--no-signage",
        dest="signage",
        action="store_false",
        help="Disable signage.",
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

    catalog_id = f"4way_priority__main_{args.main}"
    if args.lanes != 1:
        catalog_id += f"_l{args.lanes}"

    # 1. + 2. geometry + signage.
    odr, main_legs, minor_legs = make_4way_road(
        main=args.main,
        lanes=args.lanes,
        leg_length=args.leg_length,
        add_signage=args.signage,
    )

    xodr_path = out_dir / f"{catalog_id}.xodr"
    odr.write_xml(str(xodr_path))

    # Pin header date for reproducible output (before lxml rewrite).
    normalize_header_date(xodr_path, _PINNED_DATE)

    # 3. inject <priority> keyed to the main through-pair's incoming roads.
    inject_priority(xodr_path, main_incoming_road_ids=main_legs)
    print(f"[xodr] -> {xodr_path}  ({len(odr.roads)} roads, priority injected)")

    # 4. road.meta.yaml.
    meta: dict = {
        "catalog_id": catalog_id,
        "kind": "road",
        "geometry_type": "G5+G13",
        "signage": "priority_road+yield" if args.signage else "none",
        "priority": {
            "main": args.main,
            "injected": True,
            "main_legs": main_legs,
            "minor_legs": minor_legs,
        },
        "generator": {
            "script": "road_catalog/gen_4way_priority.py",
            "params": {
                "main": args.main,
                "lanes": args.lanes,
                "leg_length": args.leg_length,
                "signage": args.signage,
            },
        },
        "generated_at_commit": git_short_hash(),
    }
    meta_path = out_dir / f"{catalog_id}.road.meta.yaml"
    write_meta_yaml(meta_path, meta)
    print(f"[meta] -> {meta_path}")


if __name__ == "__main__":
    main()
