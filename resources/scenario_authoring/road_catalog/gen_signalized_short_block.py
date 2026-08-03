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
import re
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

# Optional stop-line verification signal (design/stop_line_stop_target.md sec13.1).
# type=294 + country=OpenDRIVE is NOT an ASAM-defined pattern for a stop line --
# ASAM leaves stop-line marking out of scope for <signal>. It is the one
# real-world stop-line signal this repo has on file (multi_intersections.xodr
# road196: head id=290 at s=0.0, stop line id=292 at s=4.0, both orientation
# "-"). --stop-line-setback-a mirrors that asset's pattern so the paired-stop-line
# code path (RouteSignalScan::FindPairedStopLine / FindPairedStopLineByDistance)
# has a discriminating fixture; it is not a recommendation for how new OpenDRIVE
# assets should represent a stop line in general.
#
# --stop-line-setback-a places the line this far before junction A's ENTRY (the
# end of road 0), not before the head. The stop-line pairing anchor moved from
# the governing head to the junction it governs (SignalJunctionResolver) --
# far-side/mast-arm heads (--head-farside-offset-a below) sit across the
# junction from the entry, so "before the head" stopped being the invariant
# that governs whether pairing can succeed; "before the junction entry" is.
# head_setback and this parameter are now independent: with the near-side head
# (the default), the two invariants coincide only when stop_line_setback_a is
# compared against head_setback by the caller -- the generator itself no longer
# ties them together.
_STOP_LINE_TYPE = "294"
_STOP_LINE_COUNTRY = "OpenDRIVE"

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
    stop_line_setback_a: float | None = None,
    head_farside_offset_a: float | None = None,
) -> xodr.OpenDrive:
    """Return an OpenDrive with two signalised T-junctions `block_length` apart.

    `head_setback` places junction A's NEAR-SIDE head that far before the end
    of road 0 (junction A's entry); `head_offset` places junction B's head that
    far along road 2 (i.e. past junction A's exit).

    `stop_line_setback_a`, when given, additionally places a type=294 stop-line
    signal on road 0 that far before junction A's ENTRY (see _STOP_LINE_TYPE
    above) -- independent of where junction A's head is placed. None (the
    default) emits no stop-line signal at all -- the xodr is then identical to
    a call without this parameter.

    `head_farside_offset_a`, when given, REPLACES junction A's near-side head
    (the one `head_setback` would otherwise place on road 0) with a head on
    road 2 that far past junction A's exit -- the same physical placement style
    `head_offset` uses for junction B's head, mirroring a mast-arm/far-side
    signal mounted across the intersection. Because that placement's own
    road-link geometry resolves to junction B (road 2's successor), not A (see
    SignalJunctionResolver.hpp path (c)), this head is wired to junction A
    through an explicit OpenDRIVE <controller>/<control> instead (path (a) --
    the one resolution path that does not depend on physical mounting
    position). None (the default) keeps the near-side head on road 0 exactly as
    before; the two placements are mutually exclusive (at most one head governs
    junction A).
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

    if head_farside_offset_a is None:
        # Head governing junction A, on the ego's approach (near-side, the
        # default). Resolves to junction A via SignalJunctionResolver path (c)
        # (road 0's own successor link).
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
    else:
        # Far-side head governing junction A, standing on the short block past
        # junction A's exit -- same placement STYLE as junction B's head below
        # (orientation positive: it must still face ds_dir>0 on road 2, the
        # ego's own direction of travel there, NOT the intuitive-but-wrong
        # "negative to face back at the approaching driver" -- Signal
        # orientation is relative to the road's s-axis, not world-space facing
        # direction; every signal on the ego's route needs orientation positive
        # here since the ego always travels +s through this road network).
        # Physically past the junction, so path (c) (road 2's own successor
        # link, which is junction B) would resolve this to the WRONG junction;
        # wired to junction A via <controller> below instead (path (a)).
        block.add_signal(
            xodr.Signal(
                s=head_farside_offset_a,
                t=_LIGHT_T,
                country=_LIGHT_COUNTRY,
                Type=_LIGHT_TYPE,
                subtype="-1",
                name="light_junction_a_farside",
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

    if stop_line_setback_a is not None:
        # leg_length, not "head_s": junction A's entry is where road 0 ends
        # (its successor link, see the junction-A wiring above), independent of
        # where -- or whether -- a head sits on road 0 at all (head_farside_
        # offset_a leaves road 0 with no head whatsoever).
        junction_a_entry_s = leg_length
        line_a_s = junction_a_entry_s - stop_line_setback_a
        if line_a_s < 0.0:
            raise ValueError(
                f"stop_line_setback_a={stop_line_setback_a} places the stop line at "
                f"s={line_a_s} < 0 on road {_R_WEST} (junction A entry at s={junction_a_entry_s})"
            )
        # t=0.0 (spans the carriageway), unlike the head's t=_LIGHT_T (mounted
        # off to the side) -- mirrors multi_intersections.xodr road196, where
        # head id=290 sits at t=5.3 and its paired stop line id=292 at t=0.0.
        # No <validity>: an empty validity list applies to all lanes
        # (RouteSignalScan::SignalAppliesToLane), same as the head above.
        west.add_signal(
            xodr.Signal(
                s=line_a_s,
                t=0.0,
                country=_STOP_LINE_COUNTRY,
                Type=_STOP_LINE_TYPE,
                subtype="-1",
                name="stop_line_junction_a",
                dynamic=xodr.Dynamic.no,
                orientation=xodr.Orientation.positive,
                zOffset=0.0,
                width=3.75,
                height=0.03,
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
        "--stop-line-setback-a",
        type=float,
        default=None,
        metavar="M",
        help="Place an additional stop-line signal (type=294, country=OpenDRIVE, "
        "dynamic=no) this far before junction A's ENTRY (the end of road 0), on "
        "road 0 -- independent of where junction A's head is placed (see "
        "--head-setback / --head-farside-offset-a). Mirrors the one real-world "
        "stop-line signal in this repo (multi_intersections.xodr road196) in "
        "kind, not in the reference point it is measured from (that asset has "
        "no OpenDRIVE junction at all) -- not an ASAM-defined pattern, see "
        "docs/virtualdriver/design/stop_line_stop_target.md sec 1 / sec 14. "
        "Default: disabled (no stop-line signal emitted; xodr unchanged).",
    )
    parser.add_argument(
        "--head-farside-offset-a",
        type=float,
        default=None,
        metavar="M",
        help="Replace junction A's near-side head (--head-setback) with a head "
        "on road 2, this far past junction A's exit -- the same placement style "
        "--head-offset uses for junction B's head, mirroring a mast-arm/"
        "far-side signal mounted across the intersection. Wired to junction A "
        "via an OpenDRIVE <controller> (SignalJunctionResolver path (a)), since "
        "this placement's own road-link geometry (path (c)) resolves to "
        "junction B instead. Default: disabled (near-side head on road 0, "
        "unchanged).",
    )
    parser.add_argument(
        "--out-dir",
        type=Path,
        default=None,
        metavar="DIR",
        help="Output directory. Default: <this file's dir>/generated.",
    )
    return parser.parse_args()


def _wire_signal_to_junction_via_controller(
    xodr_path: Path, signal_name: str, controller_id: int = 1
) -> None:
    """Post-process *xodr_path*: add a top-level <controller> listing the signal
    named *signal_name* and reference that controller from the FIRST <junction>
    block in the file (junction A -- it is always written before junction B by
    this generator, see make_short_block_road). This is SignalJunctionResolver
    path (a), the only resolution path that does not depend on where the
    signal is physically mounted -- needed because --head-farside-offset-a
    places its head on road 2, whose own successor link (path (c)) resolves to
    junction B, not A.

    Not emitted by scenariogeneration (no Controller/Control class in this
    venv's version, checked 2026-08-02) -- a raw text splice on the already-
    written file, same idiom as the country-case fix in main() below. The
    <controller>/<control> shape mirrors the real one read from
    multi_intersections.xodr (ctrl002 / junction 146, see
    SignalJunctionResolver.hpp's module doc and its test fixtures) and OpenDRIVE
    1.6+ schema order (road*, controller*, junction*).

    Looks the signal's numeric id up by NAME (not by predicting scenariogeneration's
    id-assignment order) so this stays correct regardless of how many other
    signals main() has already asked the library to place.
    """
    text = xodr_path.read_text(encoding="utf-8")

    m = re.search(
        r'<signal\b[^>]*\bid="(\d+)"[^>]*\bname="'
        + re.escape(signal_name)
        + r'"[^>]*/>',
        text,
    )
    if not m:
        raise RuntimeError(
            f'{xodr_path}: no <signal name="{signal_name}"> found to wire to a controller'
        )
    signal_id = m.group(1)

    controller_block = (
        f'    <controller name="ctrl_{signal_name}" id="{controller_id}">\n'
        f'        <control signalId="{signal_id}" type="0"/>\n'
        f"    </controller>\n"
    )
    junction_idx = text.index("<junction ")
    text = text[:junction_idx] + controller_block + text[junction_idx:]

    # First </junction> closes the first <junction ...> (junction A) -- junction
    # elements do not nest, so this cannot land inside junction B's block.
    close_idx = text.index("</junction>")
    text = (
        text[:close_idx]
        + f'        <controller id="{controller_id}" type="0"/>\n    '
        + text[close_idx:]
    )

    xodr_path.write_text(text, encoding="utf-8")


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
    if args.head_setback != 8.0:
        # Not part of catalog_id by default (every pre-existing asset uses the
        # default 8.0), but stop_line_setback_a is now independent of
        # head_setback (see make_short_block_road docstring) -- a non-default
        # head_setback changes the xodr just as much as stop_line_setback_a
        # does and must be visible in the name, or two assets with different
        # head placement but the same stop-line setback would collide on the
        # same catalog_id/filename.
        catalog_id += f"_hs{int(round(args.head_setback))}"
    if args.stop_line_setback_a is not None:
        catalog_id += f"_sl{int(round(args.stop_line_setback_a))}"
    if args.head_farside_offset_a is not None:
        catalog_id += f"_fsa{int(round(args.head_farside_offset_a))}"

    odr = make_short_block_road(
        block_length=args.block_length,
        leg_length=args.leg_length,
        lanes=args.lanes,
        head_setback=args.head_setback,
        head_offset=args.head_offset,
        stop_line_setback_a=args.stop_line_setback_a,
        head_farside_offset_a=args.head_farside_offset_a,
    )

    xodr_path = out_dir / f"{catalog_id}.xodr"
    odr.write_xml(str(xodr_path))
    normalize_header_date(xodr_path, _PINNED_DATE)
    if args.stop_line_setback_a is not None:
        # scenariogeneration upper-cases every Signal `country=` string on
        # write. Harmless for 2-letter ISO codes ("de" -> "DE" still matches
        # the XSD's e_countryCode_iso3166alpha2 pattern), but it breaks the
        # ASAM-defined e_countryCode_deprecated sentinel, whose only valid
        # spelling is the exact-case literal "OpenDRIVE" (OpenDRIVE_Road.xsd
        # e_countryCode_deprecated) -- "OPENDRIVE" matches neither that
        # enumeration nor the 2/3-letter patterns. Restore the exact case
        # post-write, the same idiom normalize_header_date already uses.
        text = xodr_path.read_text(encoding="utf-8")
        fixed = text.replace(
            f'country="{_STOP_LINE_COUNTRY.upper()}"', f'country="{_STOP_LINE_COUNTRY}"'
        )
        if fixed != text:
            xodr_path.write_text(fixed, encoding="utf-8")
    if args.head_farside_offset_a is not None:
        _wire_signal_to_junction_via_controller(xodr_path, "light_junction_a_farside")
    print(f"[xodr] -> {xodr_path}  ({len(odr.roads)} roads, 2 junctions)")

    meta: dict = {
        "catalog_id": catalog_id,
        "kind": "road",
        "geometry_type": "G4+G4",
        "signage": "traffic_light x2"
        + (" + stop_line" if args.stop_line_setback_a is not None else "")
        + (
            " + farside_head_a(controller-wired)"
            if args.head_farside_offset_a is not None
            else ""
        ),
        "layout": {
            "ego_path": [_R_WEST, _JUNCTION_A_ID, _R_BLOCK, _JUNCTION_B_ID, _R_EAST],
            "junction_a_id": _JUNCTION_A_ID,
            "junction_b_id": _JUNCTION_B_ID,
            "block_road_id": _R_BLOCK,
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
    if args.head_farside_offset_a is None:
        meta["layout"]["light_junction_a"] = {
            "road": _R_WEST,
            "s": args.leg_length - args.head_setback,
        }
    else:
        meta["layout"]["light_junction_a_farside"] = {
            "road": _R_BLOCK,
            "s": args.head_farside_offset_a,
            "resolved_via": "controller_chain (SignalJunctionResolver path (a))",
        }
        meta["generator"]["params"][
            "head_farside_offset_a"
        ] = args.head_farside_offset_a
    # Inserted here (not in the initial layout dict literal above) so the
    # default (near-side) case's key order exactly matches the pre-rename meta
    # files byte-for-byte -- write_meta_yaml preserves insertion order.
    meta["layout"]["light_junction_b"] = {"road": _R_BLOCK, "s": args.head_offset}
    if args.stop_line_setback_a is not None:
        meta["layout"]["stop_line_junction_a"] = {
            "road": _R_WEST,
            "s": args.leg_length - args.stop_line_setback_a,
            "offset_from_junction_a_entry": args.stop_line_setback_a,
        }
        meta["generator"]["params"]["stop_line_setback_a"] = args.stop_line_setback_a
    meta_path = out_dir / f"{catalog_id}.road.meta.yaml"
    write_meta_yaml(meta_path, meta)
    print(f"[meta] -> {meta_path}")


if __name__ == "__main__":
    main()
