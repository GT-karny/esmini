#!/usr/bin/env python
"""priority_injector.py — inject OpenDRIVE junction <priority> records.

WHY THIS EXISTS
---------------
`scenariogeneration` (the xodr generator used by the road_catalog generators)
cannot emit OpenDRIVE junction <priority> records (verified: 0 emitted; see
scenario_authoring_foundation.md §3.1/§5.3). VirtualDriver Phase 3e
(unsignalized-junction priority judgement) needs ground-truth priority data, so
this post-processor injects spec-conformant <priority> elements after generation.

OPENDRIVE REPRESENTATION (the spec-conformant ground truth chosen here)
-----------------------------------------------------------------------
Per the OpenDRIVE spec, a <junction> element may carry <priority> child
elements:

    <priority high="<connecting road id>" low="<connecting road id>"/>

`high` is the id of the connecting road with right-of-way; `low` is the id of
the connecting road that must yield. Both attributes reference *connecting road*
ids (the roads with junction="<this junction id>"), NOT incoming road ids.

We chose the connecting-road-id form (not incoming-road ids, not a <road>-level
attribute) because that is the only form the OpenDRIVE schema defines for
<junction><priority>, and it is unambiguous: a junction's right-of-way is a
relation between the movements *through* the junction (the connecting roads),
not between the approach legs.

WHAT UPSTREAM ESMINI DOES / DOES NOT PARSE
------------------------------------------
The upstream RoadManager (EnvironmentSimulator/Modules/RoadManager/RoadManager.cpp,
junction loop ~L5291) reads only the <connection> and <controller> children of
<junction>. It does NOT parse <priority> — the element is silently ignored, so
injecting it is load-safe (no warnings/errors). Phase 3e will add GT-side
extraction (roadmap §Phase 3e); THIS module's output is the spec-conformant
ground truth that GT-side extraction will read.

SCHEMA-VALID ORDERING
---------------------
OpenDRIVE sequences <junction> children as: connection* , priority* ,
controller* , surface? . We therefore insert <priority> elements immediately
after the last <connection> and before the first <controller>. (Upstream
esmini's pugixml sibling-walk is order-tolerant, but we honour the spec order so
the GT-side extractor and any schema validator are satisfied.)

IDEMPOTENCY
-----------
Re-running replaces all previously injected <priority> elements (every existing
<priority> child of each junction is removed first), so repeated runs produce
byte-identical output and never duplicate records.
"""

from __future__ import annotations

import argparse
from pathlib import Path

from lxml import etree


def _connecting_road_incoming_map(junction: etree._Element) -> dict[str, set[int]]:
    """Map each connecting road id -> set of incoming road ids that use it.

    A connecting road spans the junction and is referenced by one <connection>
    per traversal direction, so each id typically maps to two incoming roads.
    """
    mapping: dict[str, set[int]] = {}
    for conn in junction.findall("connection"):
        cr = conn.get("connectingRoad")
        inc = conn.get("incomingRoad")
        if cr is None or inc is None:
            continue
        mapping.setdefault(cr, set()).add(int(inc))
    return mapping


def inject_priority(
    xodr_path: str | Path,
    main_incoming_road_ids: list[int],
    out_path: str | Path | None = None,
) -> Path:
    """Inject <priority high low> records into every junction of *xodr_path*.

    For each ordered pair of connecting roads (A, B) within a junction where
    A is a *main* movement (all its incoming roads are in *main_incoming_road_ids*)
    and B is *not* main (at least one incoming road is outside the main set),
    emit <priority high="A" low="B"/>.

    Idempotent: existing <priority> elements are removed first.

    Returns the path written (out_path, or xodr_path if out_path is None).
    """
    main_set = set(main_incoming_road_ids)
    src = Path(xodr_path)
    parser = etree.XMLParser(remove_blank_text=False)
    tree = etree.parse(str(src), parser)
    root = tree.getroot()

    for junction in root.findall("junction"):
        # Idempotency: drop any previously injected <priority> records.
        for prio in junction.findall("priority"):
            junction.remove(prio)

        cr_incoming = _connecting_road_incoming_map(junction)
        # Classify each connecting road as main (priority) or not.
        main_crs = [cr for cr, inc in cr_incoming.items() if inc and inc <= main_set]
        minor_crs = [
            cr for cr, inc in cr_incoming.items() if not (inc and inc <= main_set)
        ]

        # Stable, deterministic ordering (numeric connecting-road id).
        main_crs.sort(key=int)
        minor_crs.sort(key=int)

        new_priorities: list[etree._Element] = []
        for high in main_crs:
            for low in minor_crs:
                p = etree.Element("priority")
                p.set("high", high)
                p.set("low", low)
                # Indent each element on its own line (matches the 8-space
                # indentation scenariogeneration uses for <connection> children).
                p.tail = "\n        "
                new_priorities.append(p)

        if not new_priorities:
            continue

        # Schema-valid position: after last <connection>, before first <controller>.
        controllers = junction.findall("controller")
        if controllers:
            insert_at = list(junction).index(controllers[0])
        else:
            connections = junction.findall("connection")
            insert_at = (
                (list(junction).index(connections[-1]) + 1)
                if connections
                else len(junction)
            )

        # Give the element before the insertion point a newline+indent tail so
        # the first injected <priority> starts on its own line.
        if insert_at > 0:
            prev = junction[insert_at - 1]
            prev.tail = "\n        "
        # Last injected element closes back to the </junction> indentation.
        new_priorities[-1].tail = "\n    "

        for offset, p in enumerate(new_priorities):
            junction.insert(insert_at + offset, p)

    dst = Path(out_path) if out_path is not None else src
    tree.write(str(dst), encoding="utf-8", xml_declaration=True)
    return dst


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Inject OpenDRIVE junction <priority> records into a generated xodr."
    )
    parser.add_argument("xodr", type=Path, help="Input xodr path.")
    parser.add_argument(
        "--main-roads",
        required=True,
        metavar="IDS",
        help="Comma-separated incoming road ids forming the priority (main) road, e.g. 0,1.",
    )
    parser.add_argument(
        "--out",
        type=Path,
        default=None,
        metavar="PATH",
        help="Output path. Default: overwrite the input file in place.",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    main_ids = [int(x) for x in args.main_roads.split(",") if x.strip() != ""]
    dst = inject_priority(args.xodr, main_ids, args.out)
    print(f"[priority] injected -> {dst}  (main incoming roads = {main_ids})")


if __name__ == "__main__":
    main()
