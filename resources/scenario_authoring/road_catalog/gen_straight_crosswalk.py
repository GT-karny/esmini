#!/usr/bin/env python
"""gen_straight_crosswalk.py — generate a straight road with a mid-span
crosswalk (xodr) + road.meta.yaml, for the Phase 3d CrosswalkPedestrianAware
verification catalog (scenario set 09).

TWO catalog ids (one generator, --ped-signal flag toggles the second):
  * straight_crosswalk__mid          : straight road + native crosswalk object.
  * straight_crosswalk_pedsig__mid   : same road + a dynamic PEDESTRIAN signal
    (OpenDRIVE type 1000002) near the crosswalk. The signal is authored so it
    does NOT face the ego's +s travel direction (orientation "-") and carries NO
    validity records over the ego driving lanes — so upstream TrafficLightAware /
    ScanSignalsAhead never mistakes it for an ego traffic light. The C++ crosswalk
    scanner (F2 Phase 3d) finds it by type + s-range regardless of orientation.

ROAD GEOMETRY
-------------
  * single road id 0, straight Line, length 500 m.
  * one driving lane per direction (width 3.5), plus a sidewalk lane (type
    sidewalk, width 2.0) on each side so a pedestrian has a plausible standing
    place off the roadway. Lanes are built manually (create_road only emits
    driving lanes); esmini loads the sidewalk lanes cleanly (verified headless).
  * crosswalk OpenDRIVE object at s=250 emitted NATIVELY via
    xodr.Object(Type=ObjectType.crosswalk) + a closed Outline of CornerRoad
    corners spanning the full driving width (t in [-4, +4]) and s 248..252
    (footprint 4 m along travel). The object also carries consistent
    s/t/hdg/length/width attributes (bbox-fallback consumers). Mirrors the
    parking_demo.xodr crosswalk element shape.

RULE / rHT
----------
No <priority>, no junction; a plain two-way straight. Traffic-hand is RHT (ego
drives lane -1 in +s). No `rule` attribute is emitted by scenariogeneration for a
plain road (parity with gen_t_junction, which also emits none); recorded as
signage/geometry metadata only.

Usage:
    DriverScript/.venv/Scripts/python.exe \
        resources/scenario_authoring/road_catalog/gen_straight_crosswalk.py
    DriverScript/.venv/Scripts/python.exe \
        resources/scenario_authoring/road_catalog/gen_straight_crosswalk.py --ped-signal
"""

from __future__ import annotations

import argparse
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

# Pinned OpenDRIVE header date (scenariogeneration stamps datetime.now(); see
# normalize_header_date). Frozen so regeneration is byte-reproducible. A single
# fixed stamp shared by both variants of this generator.
_PINNED_DATE = "2026-06-13 00:00:00.000000"

# --- geometry constants ----------------------------------------------------
ROAD_ID = 0
ROAD_LEN = 500.0
DRIVE_W = 3.5  # one driving lane per direction
SIDEWALK_W = 2.0  # sidewalk lane per side (pedestrian standing area)

CROSSWALK_S = 250.0  # crosswalk centre s
CROSSWALK_HALF_S = 2.0  # footprint 248..252 along travel (4 m)
CROSSWALK_HALF_T = 4.0  # spans t -4..+4 (full driving width + margin)

# --- pedestrian signal constants (type 1000002, 2-lamp red/green) ----------
# Mirrors the fabriksgatan_traffic_lights.xodr pedestrian-signal conventions
# (type 1000002, country OpenDRIVE, subtype -1, height 0.55, width 0.4, zOffset
# 2.5). Placed at s within ~5 m of the crosswalk. orientation "-" so it faces
# AGAINST ego +s travel (never treated as an ego signal by ScanSignalsAhead);
# NO validity record over the driving lanes.
PEDSIG_S = 252.0
PEDSIG_T = -6.0
PEDSIG_ID = "10"
PEDSIG_TYPE = "1000002"
PEDSIG_COUNTRY = "OpenDRIVE"


# ---------------------------------------------------------------------------
# Road network builder
# ---------------------------------------------------------------------------


def make_straight_crosswalk_road(add_ped_signal: bool) -> xodr.OpenDrive:
    """Return an OpenDrive straight road with a mid-span crosswalk (and,
    optionally, a dynamic pedestrian signal near it)."""
    planview = xodr.PlanView()
    planview.add_geometry(xodr.Line(ROAD_LEN))

    # Lane section: center + one driving + one sidewalk per side.
    centerlane = xodr.Lane(lane_type=xodr.LaneType.none)
    centerlane.add_roadmark(xodr.std_roadmark_broken())
    ls = xodr.LaneSection(0, centerlane)

    # left side (ids +1 driving, +2 sidewalk)
    left_drive = xodr.Lane(lane_type=xodr.LaneType.driving, a=DRIVE_W)
    left_drive.add_roadmark(xodr.std_roadmark_solid())
    ls.add_left_lane(left_drive)
    ls.add_left_lane(xodr.Lane(lane_type=xodr.LaneType.sidewalk, a=SIDEWALK_W))

    # right side (ids -1 driving, -2 sidewalk) — ego drives lane -1 in +s (RHT)
    right_drive = xodr.Lane(lane_type=xodr.LaneType.driving, a=DRIVE_W)
    right_drive.add_roadmark(xodr.std_roadmark_solid())
    ls.add_right_lane(right_drive)
    ls.add_right_lane(xodr.Lane(lane_type=xodr.LaneType.sidewalk, a=SIDEWALK_W))

    lanes = xodr.Lanes()
    lanes.add_lanesection(ls)

    road = xodr.Road(ROAD_ID, planview, lanes)

    # --- native crosswalk object -------------------------------------------
    obj = xodr.Object(
        s=CROSSWALK_S,
        t=0.0,
        Type=xodr.ObjectType.crosswalk,
        id="1",
        name="crosswalk_mid",
        subtype="-1",
        zOffset=0.0,
        orientation=xodr.Orientation.none,
        hdg=0.0,
        length=2.0 * CROSSWALK_HALF_S,
        width=2.0 * CROSSWALK_HALF_T,
        height=0.0,
        validLength=0.0,
    )
    outline = xodr.Outline(
        closed=True,
        id=0,
        fill_type=xodr.FillType.pavement,
        lane_type=xodr.LaneType.sidewalk,
        outer=True,
    )
    corners = [
        (CROSSWALK_S - CROSSWALK_HALF_S, -CROSSWALK_HALF_T),
        (CROSSWALK_S + CROSSWALK_HALF_S, -CROSSWALK_HALF_T),
        (CROSSWALK_S + CROSSWALK_HALF_S, +CROSSWALK_HALF_T),
        (CROSSWALK_S - CROSSWALK_HALF_S, +CROSSWALK_HALF_T),
    ]
    for cid, (s, t) in enumerate(corners):
        outline.add_corner(xodr.CornerRoad(s=s, t=t, dz=0.0, height=0.0, id=cid))
    obj.add_outline(outline)
    road.add_object(obj)

    # --- optional dynamic pedestrian signal --------------------------------
    if add_ped_signal:
        sig = xodr.Signal(
            s=PEDSIG_S,
            t=PEDSIG_T,
            country=PEDSIG_COUNTRY,
            Type=PEDSIG_TYPE,
            subtype="-1",
            id=PEDSIG_ID,
            name="ped_signal",
            dynamic=xodr.Dynamic.yes,
            orientation=xodr.Orientation.negative,
            zOffset=2.5,
            hOffset=1.57,
            height=0.55,
            width=0.4,
        )
        # NB: deliberately NO add_validity() — no validity record over the ego
        # driving lanes, so the signal never governs the ego lane.
        road.add_signal(sig)

    odr = xodr.OpenDrive("straight_crosswalk")
    odr.add_road(road)
    odr.adjust_roads_and_lanes()

    # OpenDRIVE 1.5 (this catalog's declared version) requires >=1 <elevation>
    # child inside every <elevationProfile>. scenariogeneration seeds an empty
    # profile per road; add a flat (level) elevation record to any road that
    # still lacks one.
    for _r in odr.roads.values():
        if not _r.elevationprofile.elevations:
            _r.add_elevation(0, 0, 0, 0, 0)

    return odr


# ---------------------------------------------------------------------------
# catalog_id builder
# ---------------------------------------------------------------------------


def build_catalog_id(ped_signal: bool) -> str:
    """catalog_id: straight_crosswalk[_pedsig]__mid."""
    stem = "straight_crosswalk_pedsig" if ped_signal else "straight_crosswalk"
    return f"{stem}__mid"


# ---------------------------------------------------------------------------
# CLI entry point
# ---------------------------------------------------------------------------


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate a straight-road-with-crosswalk xodr + road.meta.yaml."
    )
    parser.add_argument(
        "--ped-signal",
        dest="ped_signal",
        action="store_true",
        default=False,
        help="Add a dynamic pedestrian signal (type 1000002) near the crosswalk. "
        "Changes catalog_id to straight_crosswalk_pedsig__mid.",
    )
    parser.add_argument(
        "--out-dir",
        type=Path,
        default=None,
        metavar="DIR",
        help="Output directory. Default: <this file's dir>/generated.",
    )
    return parser.parse_args()


def _build_and_write(ped_signal: bool, out_dir: Path) -> None:
    catalog_id = build_catalog_id(ped_signal)
    odr = make_straight_crosswalk_road(add_ped_signal=ped_signal)

    xodr_path = out_dir / f"{catalog_id}.xodr"
    odr.write_xml(str(xodr_path))
    normalize_header_date(xodr_path, _PINNED_DATE)
    if ped_signal:
        # scenariogeneration force-uppercases <signal country> (Signal.get_attributes
        # does str(country).upper()), turning the generic "OpenDRIVE" country into
        # "OPENDRIVE", which is not a valid OpenDRIVE e_countryCode enum value
        # (the enum is the mixed-case "OpenDRIVE"). Restore the canonical case.
        _text = xodr_path.read_text(encoding="utf-8")
        xodr_path.write_text(
            _text.replace('country="OPENDRIVE"', 'country="OpenDRIVE"'),
            encoding="utf-8",
        )
    print(
        f"[xodr] -> {xodr_path}  ({len(odr.roads)} road, "
        f"{'ped-signal' if ped_signal else 'no-signal'})"
    )

    meta: dict = {
        "catalog_id": catalog_id,
        "kind": "road",
        # New G-code for a straight road with a native crosswalk object.
        "geometry_type": "G1S",
        "signage": "ped_signal" if ped_signal else "crosswalk",
        "rule": "RHT",
        "generator": {
            "script": "road_catalog/gen_straight_crosswalk.py",
            "params": {
                "road_len": ROAD_LEN,
                "drive_lane_width": DRIVE_W,
                "sidewalk_width": SIDEWALK_W,
                "crosswalk_s": CROSSWALK_S,
                "crosswalk_half_s": CROSSWALK_HALF_S,
                "crosswalk_half_t": CROSSWALK_HALF_T,
                "ped_signal": ped_signal,
            },
        },
        "generated_at_commit": git_short_hash(),
    }
    meta_path = out_dir / f"{catalog_id}.road.meta.yaml"
    write_meta_yaml(meta_path, meta)
    print(f"[meta] -> {meta_path}")


def main() -> None:
    args = parse_args()
    out_dir: Path = (
        args.out_dir
        if args.out_dir is not None
        else (Path(__file__).resolve().parent / "generated")
    )
    out_dir.mkdir(parents=True, exist_ok=True)
    _build_and_write(args.ped_signal, out_dir)


if __name__ == "__main__":
    main()
