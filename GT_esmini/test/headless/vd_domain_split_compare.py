"""feature:F7 — assert two matrix runs produced bit-identical trajectories.

Used to prove a change is behaviour-neutral: the S1 ownership ledger records and
logs who owns what, but nothing reads that record to decide anything yet, so
every csv must match the pre-change run exactly. The csv preamble carries the
esmini build version and the scenario path, both of which legitimately differ
between runs, so the comparison starts at the header row.

  DriverScript/.venv/Scripts/python.exe GT_esmini/test/headless/vd_domain_split_compare.py \
      --before <dir> --after <dir>
"""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from vd_domain_split_probe import csv_body_identical  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--before", required=True)
    ap.add_argument("--after", required=True)
    args = ap.parse_args()

    before, after = Path(args.before), Path(args.after)
    cases = sorted(p.stem for p in before.glob("*.csv"))
    if not cases:
        print(f"no csv found under {before}", file=sys.stderr)
        return 2

    failures = 0
    for name in cases:
        a, b = before / f"{name}.csv", after / f"{name}.csv"
        if not b.exists():
            print(f"{name:<26} MISSING in --after")
            failures += 1
            continue
        ok, msg = csv_body_identical(a, b)
        print(f"{name:<26} {'IDENTICAL' if ok else 'DIFFER':<10} {msg}")
        if not ok:
            failures += 1

    print(f"\n{len(cases) - failures}/{len(cases)} identical")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
