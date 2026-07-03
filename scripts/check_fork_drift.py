#!/usr/bin/env python
"""check_fork_drift.py -- verify the GT_RoadManager.cpp fork carries ONLY sanctioned edits.

Compares the swapped-in fork

    GT_esmini/src/road/GT_RoadManager.cpp

against the pristine upstream

    EnvironmentSimulator/Modules/RoadManager/RoadManager.cpp

via a difflib line-level diff. EVERY changed/added/removed hunk must be attributable to one of:

  (a) the fork HEADER comment block at the very top (the "GT_esmini modification (Clean Core
      exception)" block), or
  (b) a MARKER block -- a change whose added lines (or one of the <=3 fork lines immediately
      preceding the change, i.e. a +-3 context window) contain "[GT_LHT]" or "[GT_ODR:".

Any hunk that is NOT attributable is "unattributed drift" and fails the check (exit 1).

It also reports the total number of NON-BLANK added/changed fork lines attributable to
"[GT_ODR:" blocks. The expected count and the 150-line hard cap are SOURCED FROM THE
MACHINE-READABLE MANIFEST in GT_esmini/docs/gt_roadmanager_patches.md (fenced YAML block,
parsed via gt_patch_manifest.py) -- never from a constant in this file (the stale
`_DEFAULT_EXPECT_ODR = 16` this replaced is the motivating failure). If the manifest is
unreadable the count check is SKIPPED with a stderr warning, never compared against a
stale constant.

NOTE: this script is the coarse-grained LEGACY check (proximity attribution, see
_classify_block). The AUTHORITATIVE checker is scripts/check_core_census.py (sound
in-hunk attribution, two-sided fork/pristine census).

Pure text, no build/DLL needed. Importable: `check_drift()` returns a result dict; the
conformance harness (run_odr_conformance.py) calls it so quick/full profiles gate on it.

USAGE
-----
  check_fork_drift.py [--fork <path>] [--pristine <path>] [--expect-odr-lines N]
                      [--budget N] [--quiet] [--json]

Exit 0 iff there is no unattributed drift (and, when --expect-odr-lines is given, the
[GT_ODR:] non-blank line count matches it exactly).
"""
from __future__ import annotations

import argparse
import difflib
import json
import os
import sys

_THIS_DIR = os.path.dirname(os.path.abspath(__file__))
_REPO_ROOT = os.path.dirname(_THIS_DIR)

DEFAULT_FORK = os.path.join(_REPO_ROOT, "GT_esmini", "src", "road", "GT_RoadManager.cpp")
DEFAULT_PRISTINE = os.path.join(_REPO_ROOT, "EnvironmentSimulator", "Modules", "RoadManager", "RoadManager.cpp")

# Markers that sanction a change block.
_MARKERS = ("[GT_ODR:", "[GT_LHT]")
_ODR_MARKER = "[GT_ODR:"
_LHT_MARKER = "[GT_LHT]"
# Sentinel that identifies the fork header comment block (added at the top of the file).
_HEADER_SENTINEL = "GT_esmini modification"
# Context window (lines) allowed BEFORE a marker (the change sits just above its annotation).
_CTX = 3
# How far a marker's sanction reaches FORWARD in the fork file. A single logical patch can span
# more than one diff hunk when unchanged lines (braces/else) sit between the changed lines (e.g.
# the [GT_LHT] 1-A block: marker at the top, three swapped branches a dozen lines below). This
# bound keeps over-attribution tight (a stray edit further than this from any marker is caught).
_MARKER_FORWARD = 15
# Fallback display-only budget when the manifest is unreadable. The expected [GT_ODR:]
# line count has NO fallback: it comes from the manifest (gt_patch_manifest.load_manifest)
# or the count check is skipped with a warning -- never a stale constant.
_DEFAULT_BUDGET = 150


def _manifest_values():
    """(expect_odr_lines, budget) from the machine-readable manifest, or (None, None)."""
    try:
        import gt_patch_manifest
        m = gt_patch_manifest.load_manifest(_REPO_ROOT)
        return m["fork_odr_expect_lines"], m["fork_line_budget"]
    except Exception as e:
        print(f"WARNING: cannot read patch manifest ({e}); "
              "skipping the [GT_ODR:] line-count check", file=sys.stderr)
        return None, None


def _read_lines(path: str) -> list[str]:
    with open(path, "r", encoding="utf-8", errors="replace", newline="") as fh:
        text = fh.read()
    # Normalise line endings so CRLF/LF differences never show up as drift.
    text = text.replace("\r\n", "\n").replace("\r", "\n")
    return text.split("\n")


def _build_marker_coverage(fork_lines: list[str]) -> dict[str, list[bool]]:
    """For each fork line index, whether it is 'covered' by an ODR / LHT / header marker.

    A marker on line L sanctions fork lines in [L - _CTX, L + _MARKER_FORWARD]. Coverage is
    per kind so a block's changed lines can be checked against the right marker family. The
    header sentinel covers only itself + the small block around it (headers add contiguous lines,
    so the forward reach is enough).
    """
    n = len(fork_lines)
    cov = {"odr": [False] * n, "lht": [False] * n, "header": [False] * n}

    def mark(kind: str, center: int) -> None:
        lo = max(0, center - _CTX)
        hi = min(n, center + _MARKER_FORWARD + 1)
        for k in range(lo, hi):
            cov[kind][k] = True

    for i, ln in enumerate(fork_lines):
        if _HEADER_SENTINEL in ln:
            mark("header", i)
        if _ODR_MARKER in ln:
            mark("odr", i)
        if _LHT_MARKER in ln:
            mark("lht", i)
    return cov


def _classify_block(fork_lines: list[str], cov: dict[str, list[bool]], j1: int, j2: int, added: list[str]) -> str:
    """Classify a change block -> 'header' | 'odr' | 'lht' | 'unattributed'.

    Rule: EVERY changed fork line in the block must be marker-covered by the same kind (or the
    block is a pure deletion, in which case the deletion point / its immediate neighbours must be
    covered). Precedence when multiple kinds cover: header, then odr, then lht.
    """
    if j2 > j1:
        idxs = list(range(j1, j2))
    else:
        # Pure deletion: no fork lines removed. Check the seam (line before + at the deletion point).
        idxs = [k for k in (j1 - 1, j1) if 0 <= k < len(fork_lines)]
        if not idxs:
            idxs = [max(0, min(j1, len(fork_lines) - 1))]

    for kind in ("header", "odr", "lht"):
        if all(cov[kind][k] for k in idxs):
            return kind
    return "unattributed"


def check_drift(fork_path: str = DEFAULT_FORK, pristine_path: str = DEFAULT_PRISTINE):
    """Return a result dict.

    keys:
      ok             : bool  (no unattributed drift)
      odr_lines      : int   (non-blank added/changed fork lines in [GT_ODR:] blocks)
      lht_lines      : int   (same for [GT_LHT] blocks)
      header_lines   : int   (added header comment lines)
      total_changed  : int   (all non-blank added/changed fork lines across every block)
      blocks         : list of per-block dicts {kind, fork_span, added, removed, sample}
      unattributed   : list of per-block dicts (subset of blocks with kind == 'unattributed')
      error          : str   (present only on a hard error, e.g. missing file)
    """
    for p in (fork_path, pristine_path):
        if not os.path.exists(p):
            return {"ok": False, "error": f"file not found: {p}", "blocks": [], "unattributed": [],
                    "odr_lines": 0, "lht_lines": 0, "header_lines": 0, "total_changed": 0}

    pristine = _read_lines(pristine_path)
    fork = _read_lines(fork_path)

    cov = _build_marker_coverage(fork)
    sm = difflib.SequenceMatcher(a=pristine, b=fork, autojunk=False)
    blocks = []
    odr_lines = lht_lines = header_lines = total_changed = 0

    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == "equal":
            continue
        added = fork[j1:j2]  # inserted/replacement lines on the fork side
        removed = pristine[i1:i2]  # deleted/replaced lines on the pristine side
        kind = _classify_block(fork, cov, j1, j2, added)
        nonblank_added = sum(1 for ln in added if ln.strip())
        total_changed += nonblank_added
        if kind == "odr":
            odr_lines += nonblank_added
        elif kind == "lht":
            lht_lines += nonblank_added
        elif kind == "header":
            header_lines += nonblank_added
        sample = next((ln.strip() for ln in added if ln.strip()), "")
        if not sample and removed:
            sample = "(deletion) " + next((ln.strip() for ln in removed if ln.strip()), "")
        blocks.append({
            "kind": kind,
            "tag": tag,
            "fork_span": [j1 + 1, j2],  # 1-based inclusive-ish for humans
            "pristine_span": [i1 + 1, i2],
            "added": len(added),
            "added_nonblank": nonblank_added,
            "removed": len(removed),
            "sample": sample[:100],
        })

    unattributed = [b for b in blocks if b["kind"] == "unattributed"]
    return {
        "ok": len(unattributed) == 0,
        "odr_lines": odr_lines,
        "lht_lines": lht_lines,
        "header_lines": header_lines,
        "total_changed": total_changed,
        "blocks": blocks,
        "unattributed": unattributed,
    }


def format_summary(res: dict, budget: int = _DEFAULT_BUDGET) -> str:
    """One-line summary for the conformance harness."""
    if res.get("error"):
        return f"fork-drift: ERROR ({res['error']})"
    if res["ok"]:
        return f"fork-drift: OK ({res['odr_lines']}/{budget} lines)"
    return f"fork-drift: FAIL ({len(res['unattributed'])} unattributed hunk(s); {res['odr_lines']}/{budget} lines)"


def _print_report(res: dict, expect_odr: int | None, budget: int) -> None:
    print(f"Fork drift check: GT_RoadManager.cpp vs pristine RoadManager.cpp")
    if res.get("error"):
        print("  ERROR:", res["error"])
        return
    print(f"  change blocks: {len(res['blocks'])}  "
          f"(header={sum(1 for b in res['blocks'] if b['kind'] == 'header')}, "
          f"odr={sum(1 for b in res['blocks'] if b['kind'] == 'odr')}, "
          f"lht={sum(1 for b in res['blocks'] if b['kind'] == 'lht')}, "
          f"unattributed={len(res['unattributed'])})")
    for b in res["blocks"]:
        flag = "  " if b["kind"] != "unattributed" else ">>"
        print(f"  {flag} [{b['kind']:<12}] fork L{b['fork_span'][0]}-{b['fork_span'][1]} "
              f"(+{b['added_nonblank']} nb / +{b['added']} / -{b['removed']})  {b['sample']}")
    print(f"  [GT_ODR:] non-blank lines: {res['odr_lines']}/{budget}"
          + (f"  (expected {expect_odr})" if expect_odr is not None else ""))
    print(f"  [GT_LHT] non-blank lines : {res['lht_lines']}")
    print(f"  header comment lines     : {res['header_lines']}")
    print("  " + format_summary(res, budget))


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Verify GT_RoadManager.cpp fork drift (pure text).")
    ap.add_argument("--fork", default=DEFAULT_FORK)
    ap.add_argument("--pristine", default=DEFAULT_PRISTINE)
    ap.add_argument("--expect-odr-lines", type=int, default=None,
                    help="require the [GT_ODR:] non-blank line count to equal N "
                         "(default: fork_odr_expect_lines from the gt_roadmanager_patches.md manifest)")
    ap.add_argument("--budget", type=int, default=None,
                    help="hard line-budget shown in the summary (default: manifest fork_line_budget, else 150)")
    ap.add_argument("--quiet", action="store_true", help="only print the one-line summary")
    ap.add_argument("--json", action="store_true", help="print the full result dict as JSON")
    args = ap.parse_args(argv)

    expect_odr = args.expect_odr_lines
    budget = args.budget
    if expect_odr is None or budget is None:
        m_expect, m_budget = _manifest_values()
        if expect_odr is None:
            expect_odr = m_expect  # may stay None -> count check skipped (warned above)
        if budget is None:
            budget = m_budget if m_budget is not None else _DEFAULT_BUDGET

    res = check_drift(args.fork, args.pristine)

    if args.json:
        print(json.dumps(res, indent=2))
    elif args.quiet:
        print(format_summary(res, budget))
    else:
        _print_report(res, expect_odr, budget)

    ok = res["ok"]
    if expect_odr is not None and res.get("odr_lines") != expect_odr:
        if not args.json:
            print(f"  MISMATCH: [GT_ODR:] non-blank lines {res.get('odr_lines')} != expected {expect_odr}",
                  file=sys.stderr)
        ok = False
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
