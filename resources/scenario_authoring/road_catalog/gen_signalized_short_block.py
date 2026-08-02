#!/usr/bin/env python
"""gen_signalized_short_block.py — two signalised T-junctions separated by a SHORT block.

Purpose: the road geometry that makes the "don't block the box" rule observable
(policy:traffic_light junction guard, JunctionStopGuard.hpp).

    road 0 (west approach) --[ junction A ]-- road 2 (short block) --[ junction B ]-- road 4 (east exit)
                                  |                                       |
                              road 1 (minor)                          road 3 (minor)

Two vehicle traffic-light heads on the ego's path:

  * head on road 0, near its end  -> governs entry into junction A
  * head on road 2, `head_offset` metres past junction A's exit -> governs entry
    into junction B

The block between the two junctions is deliberately shorter than the distance a
stopped car needs to stand clear of junction A. So when B is RED, the stop line
for B is a target the ego cannot occupy without leaving its tail inside junction
A — and while A is GREEN, the nearest-head rule alone cannot see that in time,
because A's head masks B's until the ego is already committed.

That combination is what discriminates:
  * without the junction guard the ego drives into A, brakes for B's stop line,
    and comes to rest inside junction A (and stays there while B stays red);
  * with it the ego holds BEFORE junction A and crosses both only once B releases.

Everything else (lane count, leg length, sign conventions) follows the same
conventions as gen_t_junction.py / gen_4way_priority.py in this directory.

Usage:
    DriverScript/.venv/Scripts/python.exe \\
        resources/scenario_authoring/road_catalog/gen_signalized_short_block.py
    ... --block-length 20 --head-offset 4
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
from scenariogeneration import xodr  # noqa: E402

# Pinned OpenDRIVE header date (scenariogeneration stamps datetime.now()).
# Frozen so regeneration is byte-reproducible (see normalize_header_date).
_PINNED_DATE = "2026-08-02 00:00:00.000000"

# Traffic-light head conventions, mirrored from gen_t_junction.py --signal:
# OpenDRIVE type 1000001 = the plain 3-lamp vehicle head. RoadManager's
# traffic_light_type_map gives it three ICON_NONE lamps, which is what makes
# IsVehicleTrafficLightHead accept it (a pedestrian head would be ignored).
_LIGHT_TYPE = "1000001"
_LIGHT_COUNTRY = "de"
_LIGHT_T = -4.0  # right of the centreline: governs the ego's lane -1
_LIGHT_ZOFFSET = 3.0

# Junction radius. Smaller than the 20 m used by the single-junction generators:
# two junction footprints have to fit either side of a block only ~12 m long.
_JUNCTION_RADIUS = 8.0

_JUNCTION_A_ID = 100
_JUNCTION_B_ID = 200

# Road ids. road 2 is the short block and is shared by both junctions.
_R_WEST, _R_MINOR_A, _R_BLOCK, _R_MINOR_B, _R_EAST = 0, 1, 2, 3, 4


# ---------------------------------------------------------------------------
# Road network builder
# ---------------------------------------------------------------------------


def make_short_block_road(
    block_length: float,
    leg_length: float,
    lanes: int,
    head_setback: float,
    head_offset: float,
) -> xodr.OpenDrive:
    """Return an OpenDrive with two signalised T-junctions `block_length` apart.

    `head_setback` places junction A's head that far before the end of road 0;
    `head_offset` places junction B's head that far along road 2 (i.e. past
    junction A's exit).
    """
    west = xodr.create_road(
        xodr.Line(leg_length), id=_R_WEST, left_lanes=lanes, right_lanes=lanes
    )
    minor_a = xodr.create_road(
        xodr.Line(leg_length), id=_R_MINOR_A, left_lanes=lanes, right_lanes=lanes
    )
    block = xodr.create_road(
        xodr.Line(block_length), id=_R_BLOCK, left_lanes=lanes, right_lanes=lanes
    )
    minor_b = xodr.create_road(
        xodr.Line(leg_length), id=_R_MINOR_B, left_lanes=lanes, right_lanes=lanes
    )
    east = xodr.create_road(
        xodr.Line(leg_length), id=_R_EAST, left_lanes=lanes, right_lanes=lanes
    )

    # Junction A: west approach + north minor leg + the block heading east.
    # The block attaches by its PREDECESSOR here and its successor at junction B,
    # so travelling +s on road 2 goes A -> B (the ego's direction).
    junction_a = xodr.CommonJunctionCreator(id=_JUNCTION_A_ID, name="A")
    junction_a.add_incoming_road_circular_geometry(
        west, radius=_JUNCTION_RADIUS, angle=math.pi, road_connection="successor"
    )
    junction_a.add_incoming_road_circular_geometry(
        minor_a, radius=_JUNCTION_RADIUS, angle=math.pi / 2, road_connection="successor"
    )
    junction_a.add_incoming_road_circular_geometry(
        block, radius=_JUNCTION_RADIUS, angle=0.0, road_connection="predecessor"
    )
    junction_a.add_connection(road_one_id=_R_WEST, road_two_id=_R_BLOCK)  # through
    junction_a.add_connection(road_one_id=_R_WEST, road_two_id=_R_MINOR_A)
    junction_a.add_connection(road_one_id=_R_BLOCK, road_two_id=_R_MINOR_A)

    # Junction B: the block arriving from the west + north minor leg + east exit.
    # startnum: CommonJunctionCreator numbers its own connecting roads from 100 by
    # default, so a second junction in the same OpenDrive has to be moved out of
    # the way or the connecting-road ids collide (IdAlreadyExists).
    junction_b = xodr.CommonJunctionCreator(id=_JUNCTION_B_ID, name="B", startnum=200)
    junction_b.add_incoming_road_circular_geometry(
        block, radius=_JUNCTION_RADIUS, angle=math.pi, road_connection="successor"
    )
    junction_b.add_incoming_road_circular_geometry(
        minor_b, radius=_JUNCTION_RADIUS, angle=math.pi / 2, road_connection="successor"
    )
    junction_b.add_incoming_road_circular_geometry(
        east, radius=_JUNCTION_RADIUS, angle=0.0, road_connection="predecessor"
    )
    junction_b.add_connection(road_one_id=_R_BLOCK, road_two_id=_R_EAST)  # through
    junction_b.add_connection(road_one_id=_R_BLOCK, road_two_id=_R_MINOR_B)
    junction_b.add_connection(road_one_id=_R_EAST, road_two_id=_R_MINOR_B)

    # DE (ISO 3166-1 alpha-2) signal country codes are only valid from OpenDRIVE
    # 1.6 onward (the 1.5 e_countryCode enum accepts only full names / alpha-3),
    # and this road always carries DE-country signals.
    odr = xodr.OpenDrive("signalized_short_block", revMinor="6")
    for r in (west, minor_a, block, minor_b, east):
        odr.add_road(r)
    odr.add_junction_creator(junction_a)
    odr.add_junction_creator(junction_b)
    odr.adjust_roads_and_lanes()

    # Every <elevationProfile> needs >= 1 <elevation> child; scenariogeneration
    # seeds empty ones (incl. on junction-generated connecting roads).
    for _r in odr.roads.values():
        if not _r.elevationprofile.elevations:
            _r.add_elevation(0, 0, 0, 0, 0)

    # Head governing junction A, on the ego's approach.
    west.add_signal(
        xodr.Signal(
            s=leg_length - head_setback,
            t=_LIGHT_T,
            country=_LIGHT_COUNTRY,
            Type=_LIGHT_TYPE,
            subtype="-1",
            name="light_junction_a",
            dynamic=xodr.Dynamic.yes,
            orientation=xodr.Orientation.positive,
            zOffset=_LIGHT_ZOFFSET,
            width=0.45,
            height=3.22,
        )
    )
    # Head governing junction B, standing on the short block just past junction
    # A's exit. This is the one whose stop line the ego cannot occupy.
    block.add_signal(
        xodr.Signal(
            s=head_offset,
            t=_LIGHT_T,
            country=_LIGHT_COUNTRY,
            Type=_LIGHT_TYPE,
            subtype="-1",
            name="light_junction_b",
            dynamic=xodr.Dynamic.yes,
            orientation=xodr.Orientation.positive,
            zOffset=_LIGHT_ZOFFSET,
            width=0.45,
            height=3.22,
        )
    )

    return odr


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate two signalised T-junctions separated by a short block."
    )
    parser.add_argument(
        "--block-length",
        type=float,
        default=12.0,
        metavar="M",
        help="Length of the block between the two junctions. Default: 12.0.",
    )
    parser.add_argument(
        "--leg-length",
        type=float,
        default=100.0,
        metavar="M",
        help="Length of the approach / exit / minor legs. Default: 100.0.",
    )
    parser.add_argument(
        "--lanes",
        type=int,
        default=1,
        metavar="N",
        help="Lanes per direction. Default: 1.",
    )
    parser.add_argument(
        "--head-setback",
        type=float,
        default=8.0,
        metavar="M",
        help="Junction A's head, this far before the end of road 0. Default: 8.0.",
    )
    parser.add_argument(
        "--head-offset",
        type=float,
        default=3.0,
        metavar="M",
        help="Junction B's head, this far along the block (= past junction A's "
        "exit). Default: 3.0.",
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

    catalog_id = f"signalized_short_block__b{int(round(args.block_length))}"
    if args.lanes != 1:
        catalog_id += f"_l{args.lanes}"

    odr = make_short_block_road(
        block_length=args.block_length,
        leg_length=args.leg_length,
        lanes=args.lanes,
        head_setback=args.head_setback,
        head_offset=args.head_offset,
    )

    xodr_path = out_dir / f"{catalog_id}.xodr"
    odr.write_xml(str(xodr_path))
    normalize_header_date(xodr_path, _PINNED_DATE)
    print(f"[xodr] -> {xodr_path}  ({len(odr.roads)} roads, 2 junctions)")

    meta: dict = {
        "catalog_id": catalog_id,
        "kind": "road",
        "geometry_type": "G4+G4",
        "signage": "traffic_light x2",
        "layout": {
            "ego_path": [_R_WEST, _JUNCTION_A_ID, _R_BLOCK, _JUNCTION_B_ID, _R_EAST],
            "junction_a_id": _JUNCTION_A_ID,
            "junction_b_id": _JUNCTION_B_ID,
            "block_road_id": _R_BLOCK,
            "light_junction_a": {
                "road": _R_WEST,
                "s": args.leg_length - args.head_setback,
            },
            "light_junction_b": {"road": _R_BLOCK, "s": args.head_offset},
        },
        "generator": {
            "script": "road_catalog/gen_signalized_short_block.py",
            "params": {
                "block_length": args.block_length,
                "leg_length": args.leg_length,
                "lanes": args.lanes,
                "head_setback": args.head_setback,
                "head_offset": args.head_offset,
            },
        },
        "generated_at_commit": git_short_hash(),
    }
    meta_path = out_dir / f"{catalog_id}.road.meta.yaml"
    write_meta_yaml(meta_path, meta)
    print(f"[meta] -> {meta_path}")


if __name__ == "__main__":
    main()
