#!/usr/bin/env python
"""Make SUMO 1.6.0 compile with a modern MSVC toolset.

SUMO 1.6.0 (2020) was built with the VS2017 toolset. generate_sumo_libs.sh pins
that via ``-T v141``; when it is not installed, the only route left is to fix the
source. This handles the one incompatibility that actually blocks netconvert.

WHAT BREAKS
-----------
Two structs derive from ``std::binary_function``, which C++17 REMOVED and which
current MSVC STLs no longer declare:

    src/microsim/MSLane.h(95): error C2504: 'binary_function': undefined base class
    src/microsim/output/MSDetectorControl.h(206): same

Note what does NOT fix it, because both look like they should:
``/std:c++14`` and ``_HAS_DEPRECATED_ADAPTOR_TYPEDEFS=1``. Both were verified to
reach the compiler (LanguageStandard=stdcpp14 and the define are present in the
generated .vcxproj) and the error is unchanged -- this STL has dropped the
adaptors outright rather than gating them on the language level.

WHY REMOVING THE BASE CLASS IS SAFE
-----------------------------------
``std::binary_function<A,B,R>`` contributes nothing but three typedefs
(first_argument_type / second_argument_type / result_type). Neither struct
declares anything else, and a grep across SUMO's whole src/ tree (excluding
vendored foreign/) finds no use of those typedefs at all. So the base class is
dead weight here and dropping it changes no behaviour -- unlike, say, silencing
the error with a local re-declaration of binary_function, which would put a
non-standard name into namespace std.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

MARKER = "GT_MSVC_COMPAT"

SITES: list[tuple[str, str, str]] = [
    (
        "src/microsim/MSLane.h",
        "    struct VehPosition : public std::binary_function < const MSVehicle*, double, bool > {",
        "    // GT_MSVC_COMPAT: std::binary_function was removed in C++17 and modern MSVC\n"
        "    // STLs no longer declare it. It only ever supplied typedefs that nothing in\n"
        "    // SUMO reads, so the base class is simply dropped.\n"
        "    struct VehPosition {",
    ),
    (
        "src/microsim/output/MSDetectorControl.h",
        "    struct detectorEquals : public std::binary_function< DetectorFilePair, MSDetectorFileOutput*, bool > {",
        "    // GT_MSVC_COMPAT: see MSLane.h -- removed-in-C++17 base class, typedefs unused.\n"
        "    struct detectorEquals {",
    ),
]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--sumo-root", required=True, type=Path)
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()

    rc = 0
    for rel, old, new in SITES:
        path = args.sumo_root / rel
        if not path.is_file():
            print(f"ERROR: not found: {path}", file=sys.stderr)
            return 1
        with open(path, "r", encoding="utf-8", newline="") as fh:
            src = fh.read()

        if MARKER in src:
            print(f"  already patched: {rel}")
            continue
        if src.count(old) != 1:
            print(
                f"ERROR: site not found exactly once in {rel} "
                f"(occurrences: {src.count(old)}) -- source differs from v1_6_0",
                file=sys.stderr,
            )
            rc = 2
            continue
        if args.check:
            print(f"  site present: {rel}")
            continue
        with open(path, "w", encoding="utf-8", newline="") as fh:
            fh.write(src.replace(old, new, 1))
        print(f"  patched: {rel}")
    return rc


if __name__ == "__main__":
    sys.exit(main())
