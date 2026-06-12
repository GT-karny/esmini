#!/usr/bin/env python
"""gen_t_junction.py — generate an unsignalized T-junction road (xodr) + road.meta.yaml.

Promoted from scratch/scenariogeneration_eval/gen_t_junction.py (evaluation prototype).
Changes from prototype:
  - argparse CLI for key geometry parameters
  - catalog_id naming convention (§6.3 of scenario_authoring_foundation.md)
  - road.meta.yaml output via authoring_common.write_meta_yaml
  - math.radians/math.pi instead of hardcoded 3.14159
  - signal code path gated behind --signal flag (M-A default = unsignalized)
  - no xosc generation (roads only; scenario generation is in scenario_templates/)

Usage:
    DriverScript/.venv/Scripts/python.exe resources/scenario_authoring/road_catalog/gen_t_junction.py
    DriverScript/.venv/Scripts/python.exe resources/scenario_authoring/road_catalog/gen_t_junction.py \\
        --angle-deg 60 --leg-length 120 --lanes 2

Import path:  generators add scenario_authoring/ to sys.path so authoring_common is importable.
"""
from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path

# Make resources/scenario_authoring/ importable as the authoring package root.
_AUTHORING_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(_AUTHORING_ROOT))

from authoring_common import git_short_hash, write_meta_yaml  # noqa: E402
from scenariogeneration import xodr  # noqa: E402


# ---------------------------------------------------------------------------
# Road network builder
# ---------------------------------------------------------------------------

def make_t_junction_road(
    angle_deg: float,
    leg_length: float,
    lanes: int,
    add_signal: bool,
) -> xodr.OpenDrive:
    """Return an OpenDrive T-junction with three incoming legs.

    Geometry:
      road 0: placed at angle 0   (one collinear leg)
      road 1: placed at angle 180 (opposite collinear leg — together 0+1 form the main road)
      road 2: placed at angle_deg (minor leg; default 90 = right-angle T)

    All three legs share a CommonJunctionCreator (junction id=100, radius=20).
    Each leg has *lanes* lanes per direction.
    """
    angle_rad = math.radians(angle_deg)

    geom = xodr.Line(leg_length)
    road0 = xodr.create_road(geom, id=0, left_lanes=lanes, right_lanes=lanes)
    road1 = xodr.create_road(xodr.Line(leg_length), id=1, left_lanes=lanes, right_lanes=lanes)
    road2 = xodr.create_road(xodr.Line(leg_length), id=2, left_lanes=lanes, right_lanes=lanes)

    junction_creator = xodr.CommonJunctionCreator(id=100, name="T")
    junction_creator.add_incoming_road_circular_geometry(
        road0, radius=20, angle=0, road_connection="successor"
    )
    junction_creator.add_incoming_road_circular_geometry(
        road1, radius=20, angle=math.pi, road_connection="successor"
    )
    junction_creator.add_incoming_road_circular_geometry(
        road2, radius=20, angle=angle_rad, road_connection="successor"
    )

    # Connect all three leg-pairs (straight-through 0↔1, and both turns via 2).
    junction_creator.add_connection(road_one_id=0, road_two_id=1)
    junction_creator.add_connection(road_one_id=0, road_two_id=2)
    junction_creator.add_connection(road_one_id=1, road_two_id=2)

    odr = xodr.OpenDrive("t_junction")
    for r in (road0, road1, road2):
        odr.add_road(r)
    odr.add_junction_creator(junction_creator)
    odr.adjust_roads_and_lanes()

    # Optional traffic signal on the ego approach leg (road 0) near the stop line.
    if add_signal:
        signal = xodr.Signal(
            s=leg_length - 10.0,
            t=-4.0,
            country="de",
            Type="1000001",
            subtype="-1",
            name="ego_traffic_light",
            dynamic=xodr.Dynamic.yes,
            orientation=xodr.Orientation.positive,
        )
        road0.add_signal(signal)

    return odr


# ---------------------------------------------------------------------------
# catalog_id builder
# ---------------------------------------------------------------------------

def build_catalog_id(angle_deg: int, lanes: int) -> str:
    """Return the catalog_id following §6.3 naming conventions.

    Format: t_junction__a{angle}[_l{lanes}]
    The lanes suffix is omitted when lanes == 1 (the canonical single-lane default).
    """
    base = f"t_junction__a{angle_deg}"
    if lanes != 1:
        base += f"_l{lanes}"
    return base


# ---------------------------------------------------------------------------
# CLI entry point
# ---------------------------------------------------------------------------

def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate an unsignalized T-junction xodr + road.meta.yaml."
    )
    parser.add_argument(
        "--angle-deg",
        type=float,
        default=90.0,
        metavar="DEG",
        help="Angle of the minor leg from the main road (degrees). Default: 90.",
    )
    parser.add_argument(
        "--leg-length",
        type=float,
        default=100.0,
        metavar="M",
        help="Length of each approach leg in metres. Default: 100.0.",
    )
    parser.add_argument(
        "--lanes",
        type=int,
        default=1,
        metavar="N",
        help="Number of lanes per direction on each leg. Default: 1.",
    )
    parser.add_argument(
        "--signal",
        action="store_true",
        default=False,
        help="Add a traffic signal on the ego approach leg (road 0). Default: off.",
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

    out_dir: Path = args.out_dir if args.out_dir is not None else (
        Path(__file__).resolve().parent / "generated"
    )
    out_dir.mkdir(parents=True, exist_ok=True)

    angle_deg_int = int(round(args.angle_deg))
    catalog_id = build_catalog_id(angle_deg_int, args.lanes)
    signage = "signal" if args.signal else "none"

    # Build road network.
    odr = make_t_junction_road(
        angle_deg=args.angle_deg,
        leg_length=args.leg_length,
        lanes=args.lanes,
        add_signal=args.signal,
    )

    # Write xodr.
    xodr_path = out_dir / f"{catalog_id}.xodr"
    odr.write_xml(str(xodr_path))
    print(f"[xodr] -> {xodr_path}  ({len(odr.roads)} roads)")

    # Write road.meta.yaml.
    meta: dict = {
        "catalog_id": catalog_id,
        "kind": "road",
        "geometry_type": "G4",
        "signage": signage,
        "priority": "none",
        "generator": {
            "script": "road_catalog/gen_t_junction.py",
            "params": {
                "angle_deg": args.angle_deg,
                "leg_length": args.leg_length,
                "lanes": args.lanes,
                "signal": args.signal,
            },
        },
        "generated_at_commit": git_short_hash(),
    }
    meta_path = out_dir / f"{catalog_id}.road.meta.yaml"
    write_meta_yaml(meta_path, meta)
    print(f"[meta] -> {meta_path}")


if __name__ == "__main__":
    main()
