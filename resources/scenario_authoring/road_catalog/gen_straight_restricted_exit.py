#!/usr/bin/env python
"""gen_straight_restricted_exit.py — long straight main road + a junction whose
exit ramp is reachable from ONLY ONE main-road lane, for VD overtake
verification (vd-func:FUNC-056).

Follows the same generator conventions as gen_t_junction.py / gen_4way_priority.py
(argparse CLI, authoring_common helpers, pinned header date, road.meta.yaml via
write_meta_yaml). See resources/scenario_authoring/README.md and
GT_esmini/docs/virtualdriver/design/overtake_maneuver.md section 9-4.

Topology (a highway-style FORK, not a T-junction): the main road terminates
into a junction; a THROUGH road continues in the same direction with the same
lane count (all lanes 1:1); an EXIT ramp (a single one-way driving lane)
diverges at --angle-deg and is reachable from ONLY the main road lane named by
--exit-lane. That last property is the whole point of this generator: without
it, RouteLanePlan's target_lanes for a route ending on the exit ramp would
include every main-road lane (any lane could reach the exit), so "overtaking
into the passing lane" could never *not* be part of the target-lane band and
"overtake past the point where you can still reach the exit" scenarios could
not be built. highway_example_with_merge_and_split.xodr's
`<laneLink from="-4" to="-1"/>` (junction 1, connection id=3) being the only
lane feeding its exit ramp is the precedent this mirrors
(docs/virtualdriver/design/lane_change_initiation.md section 3).

Usage:
    DriverScript/.venv/Scripts/python.exe \\
        resources/scenario_authoring/road_catalog/gen_straight_restricted_exit.py \\
        --length 800 --lanes 2 --exit-lane -2
    DriverScript/.venv/Scripts/python.exe \\
        resources/scenario_authoring/road_catalog/gen_straight_restricted_exit.py \\
        --length 800 --lanes 1 --exit-lane -1

Import path:  generators add scenario_authoring/ to sys.path so authoring_common
is importable.
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

# Pinned OpenDRIVE header date (scenariogeneration stamps datetime.now() --
# see normalize_header_date). Frozen so regeneration is byte-reproducible.
# A dedicated date (not gen_t_junction.py's) so this generator's outputs are
# distinguishable at a glance from the T-junction family's committed files.
_PINNED_DATE = "2026-08-04 00:00:00.000000"

# Fixed leg lengths for the two roads on the far side of the junction. Only
# the MAIN road's length is a CLI knob (that is the one the overtake distance
# budget in overtake_maneuver.md section 9-4 is designed against); the through
# and exit legs just need to be long enough to hold a couple of Waypoints and
# are not part of anyone's distance arithmetic.
_THROUGH_LEG_LENGTH = 100.0
_EXIT_LEG_LENGTH = 100.0
_JUNCTION_RADIUS = 30.0
_JUNCTION_ID = 100


# ---------------------------------------------------------------------------
# Road network builder
# ---------------------------------------------------------------------------


def make_restricted_exit_road(
    length: float,
    lanes: int,
    exit_lane: int,
    angle_deg: float,
) -> xodr.OpenDrive:
    """Return an OpenDrive fork: main road -> junction -> {through, exit}.

    Geometry:
      road 0: the main road (length *length*, *lanes* lanes per direction).
              Placed as an INCOMING leg (its END/successor meets the junction).
      road 1: the through continuation (same lane count as road 0, so the
              junction connects every lane 1:1 -- this is the "keep going
              straight" leg). Placed as an OUTGOING leg (its START/predecessor
              meets the junction) collinear with road 0 (angle 0).
      road 2: the exit ramp -- ONE lane, ONE direction (right_lanes=1,
              left_lanes=0; there is no "opposite direction" on an exit ramp).
              Placed as an OUTGOING leg at *angle_deg* off the through
              direction. The junction connects it to road 0 from ONLY
              *exit_lane* (see add_connection call below) -- never "every
              lane", which is the property this whole generator exists for.
    """
    angle_rad = math.radians(angle_deg)

    main_road = xodr.create_road(
        xodr.Line(length), id=0, left_lanes=lanes, right_lanes=lanes
    )
    through_road = xodr.create_road(
        xodr.Line(_THROUGH_LEG_LENGTH), id=1, left_lanes=lanes, right_lanes=lanes
    )
    exit_road = xodr.create_road(
        xodr.Line(_EXIT_LEG_LENGTH), id=2, left_lanes=0, right_lanes=1
    )

    jc = xodr.CommonJunctionCreator(id=_JUNCTION_ID, name="restricted_exit")
    # Main road: traffic flows main_road -> junction, so its END is the
    # junction side ("successor").
    jc.add_incoming_road_circular_geometry(
        main_road, radius=_JUNCTION_RADIUS, angle=math.pi, road_connection="successor"
    )
    # Through / exit: traffic flows junction -> {through, exit}, so their
    # START is the junction side ("predecessor").
    jc.add_incoming_road_circular_geometry(
        through_road, radius=_JUNCTION_RADIUS, angle=0.0, road_connection="predecessor"
    )
    jc.add_incoming_road_circular_geometry(
        exit_road, radius=_JUNCTION_RADIUS, angle=angle_rad, road_connection="predecessor"
    )

    # Through connection: equal lane counts on both roads -> scenariogeneration
    # connects every lane 1:1 automatically (both directions preserved).
    jc.add_connection(road_one_id=0, road_two_id=1)
    # Exit connection: ONLY exit_lane on road 0 feeds the exit ramp's single
    # driving lane (-1). This is the load-bearing line of this generator --
    # every other main-road lane has NO path to the exit ramp.
    jc.add_connection(
        road_one_id=0, road_two_id=2, lane_one_id=exit_lane, lane_two_id=-1
    )

    odr = xodr.OpenDrive("straight_restricted_exit", revMinor="6")
    for r in (main_road, through_road, exit_road):
        odr.add_road(r)
    odr.add_junction_creator(jc)
    odr.adjust_roads_and_lanes()

    # OpenDRIVE 1.6 (this catalog's declared version, matching gen_t_junction.py's
    # signalled variant) requires >=1 <elevation> child inside every
    # <elevationProfile>. scenariogeneration seeds an empty profile per road
    # (incl. junction-generated connecting roads); add a flat (level)
    # elevation record to any road that still lacks one.
    for _r in odr.roads.values():
        if not _r.elevationprofile.elevations:
            _r.add_elevation(0, 0, 0, 0, 0)

    return odr


# ---------------------------------------------------------------------------
# catalog_id builder
# ---------------------------------------------------------------------------


def build_catalog_id(length_int: int, lanes: int, exit_lane: int) -> str:
    """Return the catalog_id following section 6.3 naming conventions.

    Format: straight_restricted_exit__l{length}_lanes{lanes}_exit{exit_lane}
    (exit_lane is written with an 'm' prefix in place of the minus sign so the
    stem stays filesystem/URL-safe, e.g. exit_lane=-2 -> "exitm2").
    """
    exit_tag = f"exitm{abs(exit_lane)}" if exit_lane < 0 else f"exit{exit_lane}"
    return f"straight_restricted_exit__l{length_int}_lanes{lanes}_{exit_tag}"


# ---------------------------------------------------------------------------
# CLI entry point
# ---------------------------------------------------------------------------


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate a straight main road + restricted-exit junction "
        "xodr + road.meta.yaml (vd-func:FUNC-056 overtake verification)."
    )
    parser.add_argument(
        "--length",
        type=float,
        default=800.0,
        metavar="M",
        help="Length of the main road in metres. Default: 800.0.",
    )
    parser.add_argument(
        "--lanes",
        type=int,
        default=2,
        metavar="N",
        help="Number of lanes per direction on the main road. Default: 2.",
    )
    parser.add_argument(
        "--exit-lane",
        dest="exit_lane",
        type=int,
        default=-2,
        metavar="ID",
        help="The ONLY main-road lane id that connects to the exit ramp. "
        "Must be a negative (right-hand, ego-direction) lane id in "
        "[-lanes, -1]. Default: -2.",
    )
    parser.add_argument(
        "--angle-deg",
        type=float,
        default=20.0,
        metavar="DEG",
        help="Angle of the exit ramp off the through direction (degrees). "
        "Default: 20.0 (a shallow highway-exit-like fork, unlike "
        "gen_t_junction.py's 90 degree default).",
    )
    parser.add_argument(
        "--out",
        dest="out_dir",
        type=Path,
        default=None,
        metavar="DIR",
        help="Output directory. Default: <this file's dir>/generated.",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()

    if not (-args.lanes <= args.exit_lane <= -1):
        raise SystemExit(
            f"--exit-lane {args.exit_lane} is not a valid right-hand lane id "
            f"for --lanes {args.lanes} (want one of "
            f"{list(range(-args.lanes, 0))})"
        )

    out_dir: Path = (
        args.out_dir
        if args.out_dir is not None
        else (Path(__file__).resolve().parent / "generated")
    )
    out_dir.mkdir(parents=True, exist_ok=True)

    length_int = int(round(args.length))
    catalog_id = build_catalog_id(length_int, args.lanes, args.exit_lane)

    odr = make_restricted_exit_road(
        length=args.length,
        lanes=args.lanes,
        exit_lane=args.exit_lane,
        angle_deg=args.angle_deg,
    )

    xodr_path = out_dir / f"{catalog_id}.xodr"
    odr.write_xml(str(xodr_path))
    normalize_header_date(xodr_path, _PINNED_DATE)
    print(f"[xodr] -> {xodr_path}  ({len(odr.roads)} roads)")

    meta: dict = {
        "catalog_id": catalog_id,
        "kind": "road",
        "geometry_type": "fork_restricted_exit",
        "generator": {
            "script": "road_catalog/gen_straight_restricted_exit.py",
            "params": {
                "length": args.length,
                "lanes": args.lanes,
                "exit_lane": args.exit_lane,
                "angle_deg": args.angle_deg,
            },
        },
        "generated_at_commit": git_short_hash(),
    }
    meta_path = out_dir / f"{catalog_id}.road.meta.yaml"
    write_meta_yaml(meta_path, meta)
    print(f"[meta] -> {meta_path}")


if __name__ == "__main__":
    main()
