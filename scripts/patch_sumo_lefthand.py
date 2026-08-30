#!/usr/bin/env python
"""Apply the left-hand-traffic fix to SUMO's OpenDRIVE importer (SUMO 1.6.0).

WHAT IS BROKEN
--------------
netconvert cannot import a left-hand-traffic OpenDRIVE network. Without
``--lefthand`` every vehicle drives on the wrong side; with ``--lefthand`` the
lanes themselves land up to a full lane width off the road.

The mechanism (see .claude/skills/sumo-authoring/references/netconvert_traps.md,
section LHT): NBNetBuilder implements left-hand traffic by mirroring the network
in Y, building it as right-hand traffic, then mirroring back. In between,
``NBEdge::computeLaneShape`` calls ``move2side`` with an always-positive offset
that knows nothing about the mirrored frame, so lanes are laid out on the
geometric opposite side.

The fix is NOT to touch move2side. It is to stop the OpenDRIVE importer from
hard-coding "the right lane group runs along +s": under left-hand traffic the
two edge directions are swapped, which cancels the mirror-induced flip. Four
sites in NIImporter_OpenDrive.cpp encode that assumption.

WHY A SCRIPT AND NOT A .patch FILE
----------------------------------
No ``.patch`` exists in this repository -- the reference implementation that
carries one was built against SUMO **1.27.1**, seven years newer than the 1.6.0
we pin. A context diff would not apply. What transfers is the *reasoning*, so
this script locates each site by its own code rather than by line number and
refuses to guess: if a site is missing or already differs, it aborts and says
which one, instead of writing a half-patched importer.

SCOPE -- READ THIS BEFORE CLAIMING "LHT SUPPORT"
------------------------------------------------
These four sites are what the reference implementation identified as sufficient
for its road network. The same source notes that a genuinely upstreamable fix
has more to do (other places infer direction from lane sign). Treat the result
as "left-hand background traffic works on the maps we test", not as LHT support.
Verify with the odrplot lane-centre measurement, not by eyeballing the viewer:
the error is exactly 2x the lane-centre |t|, and on a symmetric two-way road it
hides because a real lane exists where the misplaced one lands. Measure on a
single-lane ramp.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

MARKER = "GT_LHT_PATCH"

# Each entry: (label, exact source text to find, replacement).
# The `old` strings are verbatim from SUMO v1_6_0 src/netimport/NIImporter_OpenDrive.cpp.
SITES: list[tuple[str, str, str]] = [
    (
        "lefthand option read",
        """            // build lanes to right
            NBEdge* currRight = nullptr;""",
        """            // GT_LHT_PATCH: under left-hand traffic the importer's "right lane
            // group runs along +s" assumption is inverted. Swapping the two edge
            // directions here cancels the flip that NBNetBuilder's mirror pass
            // introduces, so move2side and laneMap need no changes at all.
            const bool gtLefthand = OptionsCont::getOptions().getBool("lefthand");

            // build lanes to right
            NBEdge* currRight = nullptr;""",
    ),
    (
        "currRight edge direction + geometry",
        """                currRight = new NBEdge("-" + id, sFrom, sTo, (*j).rightType, defaultSpeed, (*j).rightLaneNumber, priorityR,
                                       NBEdge::UNSPECIFIED_WIDTH, NBEdge::UNSPECIFIED_OFFSET, geom, e->streetName, "", LaneSpreadFunction::RIGHT, true);""",
        """                currRight = new NBEdge("-" + id, gtLefthand ? sTo : sFrom, gtLefthand ? sFrom : sTo, (*j).rightType, defaultSpeed, (*j).rightLaneNumber, priorityR,
                                       NBEdge::UNSPECIFIED_WIDTH, NBEdge::UNSPECIFIED_OFFSET, gtLefthand ? geom.reverse() : geom, e->streetName, "", LaneSpreadFunction::RIGHT, true);""",
    ),
    (
        "currLeft edge direction + geometry",
        """                currLeft = new NBEdge(id, sTo, sFrom, (*j).leftType, defaultSpeed, (*j).leftLaneNumber, priorityL,
                                      NBEdge::UNSPECIFIED_WIDTH, NBEdge::UNSPECIFIED_OFFSET, geom.reverse(), e->streetName, "", LaneSpreadFunction::RIGHT, true);""",
        """                currLeft = new NBEdge(id, gtLefthand ? sFrom : sTo, gtLefthand ? sTo : sFrom, (*j).leftType, defaultSpeed, (*j).leftLaneNumber, priorityL,
                                      NBEdge::UNSPECIFIED_WIDTH, NBEdge::UNSPECIFIED_OFFSET, gtLefthand ? geom : geom.reverse(), e->streetName, "", LaneSpreadFunction::RIGHT, true);""",
    ),
    (
        "laneSection connection direction (right group)",
        """                            prevRight->addLane2LaneConnection((*k).first, currRight, (*k).second, NBEdge::L2L_VALIDATED);""",
        """                            // GT_LHT_PATCH: the edges were built head-to-tail above, so the
                            // laneSection-to-laneSection connection runs the other way too.
                            if (gtLefthand) {
                                currRight->addLane2LaneConnection((*k).second, prevRight, (*k).first, NBEdge::L2L_VALIDATED);
                            } else {
                                prevRight->addLane2LaneConnection((*k).first, currRight, (*k).second, NBEdge::L2L_VALIDATED);
                            }""",
    ),
    (
        "laneSection connection direction (left group)",
        """                            currLeft->addLane2LaneConnection((*k).first, prevLeft, (*k).second, NBEdge::L2L_VALIDATED);""",
        """                            // GT_LHT_PATCH: mirror of the right-group case above.
                            if (gtLefthand) {
                                prevLeft->addLane2LaneConnection((*k).second, currLeft, (*k).first, NBEdge::L2L_VALIDATED);
                            } else {
                                currLeft->addLane2LaneConnection((*k).first, prevLeft, (*k).second, NBEdge::L2L_VALIDATED);
                            }""",
    ),
]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--file", required=True, help="path to NIImporter_OpenDrive.cpp")
    ap.add_argument(
        "--check",
        action="store_true",
        help="report whether every site is present, change nothing",
    )
    args = ap.parse_args()

    path = Path(args.file)
    if not path.is_file():
        print(f"ERROR: not found: {path}", file=sys.stderr)
        return 1

    # newline="" on both read and write: without it Python rewrites every line
    # ending on Windows, so a five-site edit shows up as a whole-file diff and
    # nobody can review what actually changed.
    with open(path, "r", encoding="utf-8", newline="") as fh:
        src = fh.read()

    if MARKER in src:
        print(f"already patched ({MARKER} present): {path}")
        return 0

    # Locate everything BEFORE writing anything: a partially patched importer
    # compiles and silently produces wrong networks, which is worse than a
    # failed build.
    missing = [label for label, old, _ in SITES if src.count(old) != 1]
    if missing:
        print("ERROR: these sites were not found exactly once:", file=sys.stderr)
        for label in missing:
            hits = next(src.count(o) for lbl, o, _ in SITES if lbl == label)
            print(f"  - {label}  (occurrences: {hits})", file=sys.stderr)
        print(
            "\nThe importer differs from SUMO v1_6_0. Re-derive the sites against "
            "this source before patching; do NOT loosen the match.",
            file=sys.stderr,
        )
        return 2

    if args.check:
        print(f"all {len(SITES)} sites present and unpatched: {path}")
        return 0

    for label, old, new in SITES:
        src = src.replace(old, new, 1)
        print(f"  patched: {label}")

    with open(path, "w", encoding="utf-8", newline="") as fh:
        fh.write(src)
    print(f"wrote {path} ({len(SITES)} sites)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
