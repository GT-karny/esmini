#!/usr/bin/env python
"""Measure whether a SUMO net.xml's lanes actually sit on the OpenDRIVE road.

WHAT THIS IS FOR
----------------
netconvert can produce a net.xml whose lanes are laid out beside the road rather
than on it -- most notably for left-hand-traffic networks, where the error is
exactly ``2 * |lane centre t|`` (a full lane offset to the wrong side). Nothing
warns about it: netconvert exits 0, SUMO loads the net, vehicles drive, and the
only symptom is traffic in the wrong place.

THE DISCRIMINATOR
-----------------
Rather than trying to match lane identities across the two formats (fragile, and
the identity mapping is itself what the bug corrupts), this samples every SUMO
lane's own shape and asks the OpenDRIVE side a single question per point:

    is this world position on a drivable lane?

via ``esminiRMLib.GetInLaneType``. That is a binary, per-point answer, and it is
the same instrument Track A validated: sweeping perpendicular from a lane centre
on fabriksgatan it reports DRIVING out to 5 m and NONE from 8 m, i.e. it flips at
the road edge. Two things that look like they would work here do NOT and are
deliberately avoided: the library return codes (always 0, even 100 km off the
map) and click-vs-snapped distance (always exactly 0, because GetPositionData
echoes back the coordinates you gave it).

READING THE RESULT
------------------
``on_road_fraction`` near 1.0 means the lanes lie on the road. A broken LHT
import drops it sharply, because the misplaced lanes fall onto grass.

Measure on a **single-lane** road (a ramp). On a symmetric two-way road the
misplaced lane lands exactly where the opposite lane really is, so it still
reports DRIVING and the defect hides -- which is precisely why this needs a
measurement rather than a look at the viewer.

USAGE
    check_sumo_net_lane_alignment.py --xodr R.xodr --net R.net.xml [--json]
"""

from __future__ import annotations

import argparse
import json
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT / "GT_esmini" / "scripts"))

# Mirrors rm_lib's RM_LANE_TYPE_ANY_DRIVING.
DRIVING_MASK = (
    (1 << 1) | (1 << 17) | (1 << 18) | (1 << 19) | (1 << 20) | (1 << 22) | (1 << 9)
)


def load_net_lanes(net_path: Path) -> tuple[list[dict], tuple[float, float]]:
    """Return ([{edge, lane, points:[(x,y)]}], netOffset).

    SUMO shifts coordinates by ``netOffset`` on import, so the shapes must have
    it subtracted before they can be compared against OpenDRIVE world positions.
    Skipping that step makes every lane look misplaced by the same constant --
    an error that looks exactly like the bug being measured.
    """
    root = ET.parse(net_path).getroot()
    loc = root.find("location")
    offset = (0.0, 0.0)
    if loc is not None and loc.get("netOffset"):
        ox, oy = loc.get("netOffset").split(",")
        offset = (float(ox), float(oy))

    lanes: list[dict] = []
    for edge in root.findall("edge"):
        # function="internal" edges are junction interiors; they are generated
        # geometry rather than imported road, so they are not evidence either way.
        if edge.get("function") == "internal":
            continue
        for lane in edge.findall("lane"):
            shape = lane.get("shape")
            if not shape:
                continue
            pts = []
            for pair in shape.split():
                x, y = pair.split(",")[:2]
                pts.append((float(x) - offset[0], float(y) - offset[1]))
            if pts:
                lanes.append(
                    {
                        "edge": edge.get("id"),
                        "lane": lane.get("id"),
                        "allow": lane.get("allow"),
                        "disallow": lane.get("disallow"),
                        "points": pts,
                    }
                )
    return lanes, offset


def measure(xodr: Path, net: Path) -> dict:
    from rm_lib import EsminiRMLib  # type: ignore[attr-defined]

    lib_path = REPO_ROOT / "DriverScript" / "bin" / "esminiRMLib.dll"
    if not lib_path.is_file():
        raise SystemExit(
            f"esminiRMLib.dll not found at {lib_path} (Release build needed)"
        )

    lanes, offset = load_net_lanes(net)
    if not lanes:
        raise SystemExit(f"no non-internal lanes with a shape found in {net}")

    rm = EsminiRMLib(str(lib_path))
    if rm.Init(str(xodr)) < 0:
        raise SystemExit(f"esminiRMLib failed to load {xodr}")
    handle = rm.CreatePosition()

    total = 0
    on_road = 0
    per_lane = []
    try:
        for entry in lanes:
            hits = 0
            for x, y in entry["points"]:
                rm.SetWorldXYHPosition(handle, x, y, 0.0)
                if rm.GetInLaneType(handle) & DRIVING_MASK:
                    hits += 1
            n = len(entry["points"])
            total += n
            on_road += hits
            per_lane.append(
                {
                    "lane": entry["lane"],
                    "points": n,
                    "on_road": hits,
                    "fraction": hits / n if n else 0.0,
                }
            )
    finally:
        rm.DeletePosition(handle)

    per_lane.sort(key=lambda d: d["fraction"])
    return {
        "xodr": str(xodr),
        "net": str(net),
        "net_offset": offset,
        "lanes": len(lanes),
        "points": total,
        "on_road": on_road,
        "on_road_fraction": on_road / total if total else 0.0,
        "worst_lanes": per_lane[:10],
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--xodr", required=True, type=Path)
    ap.add_argument("--net", required=True, type=Path)
    ap.add_argument("--json", action="store_true")
    ap.add_argument(
        "--min-fraction",
        type=float,
        default=None,
        help="exit 1 when on_road_fraction is below this (for use as a check)",
    )
    args = ap.parse_args()

    result = measure(args.xodr, args.net)

    if args.json:
        print(json.dumps(result, indent=2))
    else:
        print(f"xodr : {result['xodr']}")
        print(f"net  : {result['net']}")
        print(f"netOffset applied: {result['net_offset']}")
        print(f"lanes: {result['lanes']}   sampled points: {result['points']}")
        print(
            f"on a drivable lane: {result['on_road']}/{result['points']} "
            f"= {result['on_road_fraction']:.3f}"
        )
        print("\nworst lanes:")
        for d in result["worst_lanes"]:
            print(f"  {d['fraction']:.3f}  {d['lane']}  ({d['on_road']}/{d['points']})")

    if args.min_fraction is not None and result["on_road_fraction"] < args.min_fraction:
        print(
            f"\nFAIL: on_road_fraction {result['on_road_fraction']:.3f} "
            f"< {args.min_fraction}",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
