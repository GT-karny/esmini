#!/usr/bin/env python
"""check_core_census.py -- authoritative two-sided per-marker-id census checker (plan P6 S0).

Replaces the UNSOUND proximity attribution of check_fork_drift.py (`_classify_block`
-3/+15 window: an unmarked hunk within 15 lines after a marker was silently attributed
to that marker) with SOUND in-hunk attribution, driven entirely by the machine-readable
manifest in GT_esmini/docs/gt_roadmanager_patches.md (see gt_patch_manifest.py).

For every `second_class_files` row AND the `fork_file` it:

  1. loads the upstream snapshot GT_esmini/test/upstream_baselines/<basename>@<sha12>,
     verifying content integrity by recomputing the git blob SHA on the RAW stored bytes;
  2. diffs the current file against the snapshot (difflib, line endings normalized for
     the diff only);
  3. attributes every diff block's added lines to a marker id via SOUND rules:
       (a) an added line of the block itself contains [GT_ODR:<id>] (or ...:interp]);
       (b) the block lies entirely between [GT_ODR:<id>-begin] / [GT_ODR:<id>-end] lines
           that are themselves added lines (block form for hunks > 15 nonblank lines);
       (c) the block matches a `legacy_sites` manifest entry (exact current-file line
           span + nonblank count);
       (d) fork only: added lines containing [GT_LHT] -> the LHT bucket; the contiguous
           file-header comment block containing "GT_esmini modification" -> header bucket.
     Anything else -- including deletion-only blocks not covered by (c) -- is
     UNATTRIBUTED => FAIL.
     Within a multi-marker block, each nonblank added line attributes to the nearest
     PRECEDING in-block marker line; leading lines before the first marker attribute to
     the LAST marker in the block (diff-slide: a trailing close-brace of the block's last
     hunk that difflib slid to the front).
  4. compares the per-id NONBLANK census EXACTLY against the manifest, enforces budgets
     (incl. budget_groups), and enforces additive_only (any removal => FAIL);
  5. TWO-SIDED fork/pristine cross-check: expected fork census per id =
     fork_file.marker_census[id] + pristine-row marker_census[id] + overlap residual[id].
     A vj-* hunk present in pristine but missing in fork (forgotten mirror), or vice
     versa, shows as a per-id mismatch => FAIL.

SUBCOMMANDS
-----------
  check            (default) run the census; exit 0/1. --json for machine output.
  record-baselines snapshot `git cat-file blob HEAD:<path>` (raw bytes) for every
                   manifest file into GT_esmini/test/upstream_baselines/, verify blob
                   SHAs, and print the rows to paste into the manifest. --prune deletes
                   snapshots not referenced by the manifest. Re-run after every upstream
                   sync (Stage 0b: v3.4.0) -- one command re-records everything.
  selftest         synthetic attribution cases (R1..R6) incl. the mandatory RED test:
                   an unmarked hunk adjacent to an existing marker must FAIL here even
                   though the old proximity checker would have attributed it.

Pure text: stdlib + yaml only (imports gt_patch_manifest). No build/DLL needed.
"""
from __future__ import annotations

import argparse
import difflib
import hashlib
import json
import os
import re
import subprocess
import sys

_THIS_DIR = os.path.dirname(os.path.abspath(__file__))
if _THIS_DIR not in sys.path:
    sys.path.insert(0, _THIS_DIR)

import gt_patch_manifest  # noqa: E402

DEFAULT_REPO_ROOT = gt_patch_manifest.DEFAULT_REPO_ROOT
BASELINE_RELDIR = os.path.join("GT_esmini", "test", "upstream_baselines")

# Marker grammar (manifest rules.pristine_marker_strip): single [GT_ODR:<id>] for hunks
# <=15 nonblank lines, block [GT_ODR:<id>-begin]/[GT_ODR:<id>-end] for larger hunks,
# optional :interp suffix on interpretation-point hunks.
_MARKER_RE = re.compile(r"\[GT_ODR:(?P<id>[A-Za-z0-9_-]+?)(?P<blk>-begin|-end)?(?::interp)?\]")
_LHT_MARKER = "[GT_LHT]"
_HEADER_SENTINEL = "GT_esmini modification"

# Census bucket keys for the fork-only families (kept out of the [GT_ODR:] id namespace).
LHT_BUCKET = "GT_LHT"
HEADER_BUCKET = "__header__"


# ---------------------------------------------------------------------------
# Snapshot I/O
# ---------------------------------------------------------------------------
def git_blob_sha(data: bytes) -> str:
    """git blob SHA-1 = sha1(b'blob <len>\\x00' + content), on the EXACT bytes."""
    h = hashlib.sha1()
    h.update(b"blob %d\x00" % len(data))
    h.update(data)
    return h.hexdigest()


def snapshot_path(repo_root: str, file_path: str, blob_sha: str) -> str:
    base = os.path.basename(file_path.replace("\\", "/"))
    return os.path.join(repo_root, BASELINE_RELDIR, f"{base}@{blob_sha[:12]}")


def load_snapshot(repo_root: str, file_path: str, blob_sha: str) -> bytes:
    """Read + integrity-verify a snapshot. Raises RuntimeError with a clear instruction."""
    snap = snapshot_path(repo_root, file_path, blob_sha)
    if not os.path.isfile(snap):
        raise RuntimeError(
            f"missing upstream snapshot {os.path.relpath(snap, repo_root)} for {file_path} "
            f"(expected blob {blob_sha}); run: check_core_census.py record-baselines")
    with open(snap, "rb") as fh:
        data = fh.read()
    got = git_blob_sha(data)
    if got != blob_sha:
        raise RuntimeError(
            f"snapshot integrity FAILURE: {os.path.relpath(snap, repo_root)} recomputed blob sha "
            f"{got} != manifest upstream_blob_sha {blob_sha} (snapshot corrupted or line-ending "
            f"mangled; re-run record-baselines and fix the manifest row)")
    return data


def _norm_lines(data: bytes) -> list:
    """Decode + normalize line endings FOR THE DIFF ONLY (integrity is checked on raw bytes)."""
    text = data.decode("utf-8", errors="replace")
    text = text.replace("\r\n", "\n").replace("\r", "\n")
    return text.split("\n")


# ---------------------------------------------------------------------------
# Census core (pure: lines in, census out) -- also driven by selftest synthetics
# ---------------------------------------------------------------------------
def _single_markers(line: str) -> list:
    """[GT_ODR:<id>] single-form marker ids on this line (block-form begin/end excluded)."""
    return [m.group("id") for m in _MARKER_RE.finditer(line) if not m.group("blk")]


def _block_ranges(current: list, added_idx: set) -> tuple:
    """Pair [GT_ODR:<id>-begin]/[GT_ODR:<id>-end] lines (must be ADDED lines) into
    inclusive index ranges. Returns (ranges, errors)."""
    opens = {}
    ranges = []
    errors = []
    for i, ln in enumerate(current):
        for m in _MARKER_RE.finditer(ln):
            if not m.group("blk"):
                continue
            mid = m.group("id")
            if i not in added_idx:
                errors.append(f"block marker [GT_ODR:{mid}{m.group('blk')}] at current line {i + 1} "
                              f"is not an added line (stale marker in the baseline?)")
                continue
            if m.group("blk") == "-begin":
                if mid in opens:
                    errors.append(f"nested/unclosed [GT_ODR:{mid}-begin] at line {i + 1}")
                opens[mid] = i
            else:
                if mid not in opens:
                    errors.append(f"[GT_ODR:{mid}-end] at line {i + 1} without matching -begin")
                    continue
                ranges.append((mid, opens.pop(mid), i))
    for mid, i in opens.items():
        errors.append(f"[GT_ODR:{mid}-begin] at line {i + 1} never closed")
    return ranges, errors


def census_lines(snapshot: list, current: list, legacy_sites=(), is_fork: bool = False) -> dict:
    """Diff + attribute. Returns:
      census        {id: nonblank added-line count}  (fork extras: GT_LHT / __header__)
      unattributed  [block dicts]   deletions [block dicts with removed lines]
      errors        [str]           blocks [all non-equal block dicts]
    """
    sm = difflib.SequenceMatcher(a=snapshot, b=current, autojunk=False)
    opcodes = [op for op in sm.get_opcodes() if op[0] != "equal"]

    added_idx = set()
    for _tag, _i1, _i2, j1, j2 in opcodes:
        added_idx.update(range(j1, j2))
    ranges, errors = _block_ranges(current, added_idx)

    legacy = list(legacy_sites or [])
    census = {}
    blocks, unattributed, deletions = [], [], []

    def bump(mid: str, n: int = 1) -> None:
        census[mid] = census.get(mid, 0) + n

    for tag, i1, i2, j1, j2 in opcodes:
        added = current[j1:j2]
        nonblank = [ln for ln in added if ln.strip()]
        removed = i2 - i1
        span = f"{j1 + 1}-{j2}" if j2 > j1 else f"{j1 + 1}-{j1 + 1}"
        binfo = {"tag": tag, "span": span, "added_nonblank": len(nonblank), "removed": removed,
                 "sample": (nonblank[0].strip()[:100] if nonblank else "(deletion-only)")}
        blocks.append(binfo)
        if removed:
            deletions.append(binfo)
        if not nonblank and not removed:
            continue  # blank-only insertion: no census, no attribution needed

        # (d) header comment block (fork only; precedence over everything -- the header
        #     comment text itself mentions [GT_LHT]).
        if is_fork and any(_HEADER_SENTINEL in ln for ln in added):
            binfo["kind"] = HEADER_BUCKET
            bump(HEADER_BUCKET, len(nonblank))
            continue

        # (b) block form: block entirely inside a -begin/-end range.
        rng = next((mid for (mid, lo, hi) in ranges if j1 >= lo and (j2 - 1) <= hi and j2 > j1), None)
        if rng is not None:
            binfo["kind"] = rng
            bump(rng, len(nonblank))
            continue

        # (a)/(d) single markers on the block's OWN added lines ([GT_LHT] counts fork-only).
        marks = []  # (offset-in-block, id)
        for k, ln in enumerate(added):
            for mid in _single_markers(ln):
                marks.append((k, mid))
            if is_fork and _LHT_MARKER in ln and not _single_markers(ln):
                marks.append((k, LHT_BUCKET))
        if marks:
            last_id = marks[-1][1]
            for k, ln in enumerate(added):
                if not ln.strip():
                    continue
                owner = last_id  # leading lines -> last marker (diff-slide rule)
                for pos, mid in marks:
                    if pos <= k:
                        owner = mid
                    else:
                        break
                bump(owner)
            binfo["kind"] = "+".join(sorted({m for _, m in marks}))
            continue

        # (c) legacy_sites: exact span + nonblank count match (pre-S0 unmarked seam lines).
        site = next((s for s in legacy if str(s.get("fork_lines")) == span
                     and int(s.get("count", -1)) == len(nonblank)), None)
        if site is not None:
            mid = str(site["marker"])
            binfo["kind"] = f"legacy:{mid}"
            bump(mid, len(nonblank))
            continue

        binfo["kind"] = "UNATTRIBUTED"
        unattributed.append(binfo)

    return {"census": census, "blocks": blocks, "unattributed": unattributed,
            "deletions": deletions, "errors": errors}


def compare_census(measured: dict, expected: dict) -> list:
    """Exact two-way per-id comparison; returns mismatch strings (empty = match)."""
    problems = []
    for mid in sorted(set(measured) | set(expected)):
        m, e = measured.get(mid, 0), expected.get(mid, 0)
        if m != e:
            problems.append(f"{mid}: measured {m} != declared {e}")
    return problems


# ---------------------------------------------------------------------------
# check
# ---------------------------------------------------------------------------
def _expected_fork_census(manifest: dict, pristine_declared: dict) -> dict:
    """fork-vs-snapshot expectation = fork-only patches + mirrored pristine hunks + residuals."""
    fork = manifest["fork_file"]
    residual = {}
    for r in manifest.get("overlap_residuals") or []:
        residual[r["vj_marker"]] = residual.get(r["vj_marker"], 0) + int(r["residual_nonblank"])
    expected = {}
    for src in (fork["marker_census"], pristine_declared, residual):
        for mid, n in src.items():
            if n:
                expected[mid] = expected.get(mid, 0) + int(n)
    return expected


def run_check(repo_root: str = DEFAULT_REPO_ROOT) -> dict:
    """Returns {ok, files:[{path, ok, census, expected, total, budget, problems}], failures:[str]}."""
    manifest = gt_patch_manifest.load_manifest(repo_root)
    failures = []
    files_out = []
    group_totals = {}
    sc_by_path = {row["path"]: row for row in manifest["second_class_files"]}
    pristine_measured = {}

    # --- second-class files (pristine copies) ---
    for row in manifest["second_class_files"]:
        path = row["path"]
        entry = {"path": path, "ok": True, "problems": []}
        files_out.append(entry)
        try:
            snap = load_snapshot(repo_root, path, row["upstream_blob_sha"])
        except RuntimeError as e:
            entry["ok"] = False
            entry["problems"].append(str(e))
            failures.append(f"{path}: {e}")
            continue
        with open(os.path.join(repo_root, path), "rb") as fh:
            cur = fh.read()
        res = census_lines(_norm_lines(snap), _norm_lines(cur), is_fork=False)
        pristine_measured[path] = res["census"]
        entry["census"] = res["census"]
        entry["expected"] = row["marker_census"]
        entry["total"] = sum(res["census"].values())
        entry["budget"] = row["budget_nonblank"]
        for b in res["unattributed"]:
            entry["problems"].append(f"UNATTRIBUTED block L{b['span']} (+{b['added_nonblank']} nb, "
                                     f"-{b['removed']}): {b['sample']}")
        entry["problems"] += res["errors"]
        entry["problems"] += compare_census(res["census"], row["marker_census"])
        if row.get("additive_only") and res["deletions"]:
            for b in res["deletions"]:
                entry["problems"].append(f"additive_only violated: deletion at L{b['span']} (-{b['removed']})")
        grp = row.get("budget_group")
        if grp:
            group_totals[grp] = group_totals.get(grp, 0) + entry["total"]
        elif entry["total"] > row["budget_nonblank"]:
            entry["problems"].append(f"budget exceeded: {entry['total']} > {row['budget_nonblank']}")
        if entry["problems"]:
            entry["ok"] = False
            failures += [f"{path}: {p}" for p in entry["problems"]]

    for grp, total in group_totals.items():
        cap = manifest.get("budget_groups", {}).get(grp, 0)
        if total > cap:
            failures.append(f"budget_group '{grp}' exceeded: {total} > {cap}")

    # --- fork (two-sided vs the pristine counterpart's snapshot) ---
    fork = manifest["fork_file"]
    fpath = fork["path"]
    entry = {"path": fpath + " (fork)", "ok": True, "problems": []}
    files_out.append(entry)
    counterpart = fork["pristine_counterpart"]
    prow = sc_by_path.get(counterpart)
    if prow is None:
        entry["problems"].append(f"pristine_counterpart {counterpart} has no second_class_files row")
    else:
        try:
            snap = load_snapshot(repo_root, counterpart, prow["upstream_blob_sha"])
        except RuntimeError as e:
            entry["problems"].append(str(e))
            snap = None
        if snap is not None:
            with open(os.path.join(repo_root, fpath), "rb") as fh:
                cur = fh.read()
            res = census_lines(_norm_lines(snap), _norm_lines(cur),
                               legacy_sites=fork.get("legacy_sites") or [], is_fork=True)
            measured = dict(res["census"])
            lht = measured.pop(LHT_BUCKET, 0)
            header = measured.pop(HEADER_BUCKET, 0)
            expected = _expected_fork_census(manifest, prow["marker_census"])
            entry["census"] = res["census"]
            entry["expected"] = expected
            entry["total"] = sum(measured.values())
            entry["budget"] = manifest["fork_line_budget"]
            for b in res["unattributed"]:
                entry["problems"].append(f"UNATTRIBUTED block L{b['span']} (+{b['added_nonblank']} nb, "
                                         f"-{b['removed']}): {b['sample']}")
            entry["problems"] += res["errors"]
            # TWO-SIDED: measured fork census must equal fork-own + pristine-declared + residual
            # per id (forgotten-mirror detector: a vj hunk in pristine but not in the fork makes
            # the fork side fall short of the pristine-declared count for that id).
            entry["problems"] += compare_census(measured, expected)
            if lht != fork["lht_census"]:
                entry["problems"].append(f"{LHT_BUCKET}: measured {lht} != declared lht_census {fork['lht_census']}")
            if header != fork["header_census"]:
                entry["problems"].append(f"header: measured {header} != declared header_census {fork['header_census']}")
            # Manifest self-consistency with the legacy fork-vs-pristine regime: the 150-line
            # budget counts fork-ONLY [GT_ODR:] lines + residuals (mirrored hunks are invisible
            # to the fork-vs-pristine diff and consume the in-place budget instead).
            fork_vs_pristine = sum(fork["marker_census"].values()) + \
                sum(int(r["residual_nonblank"]) for r in manifest.get("overlap_residuals") or [])
            if fork_vs_pristine != manifest["fork_odr_expect_lines"]:
                entry["problems"].append(
                    f"manifest inconsistency: fork marker_census+residuals {fork_vs_pristine} != "
                    f"fork_odr_expect_lines {manifest['fork_odr_expect_lines']}")
            if fork_vs_pristine > manifest["fork_line_budget"]:
                entry["problems"].append(
                    f"fork budget exceeded: {fork_vs_pristine} > {manifest['fork_line_budget']}")
    if entry["problems"]:
        entry["ok"] = False
        failures += [f"{fpath}: {p}" for p in entry["problems"]]

    return {"ok": not failures, "files": files_out, "failures": failures}


def format_summary(res: dict) -> str:
    """One-line summary for the conformance harness."""
    if res["ok"]:
        n = len(res["files"])
        fork = next((f for f in res["files"] if f["path"].endswith("(fork)")), {})
        return (f"core-census: OK ({n} files; fork {fork.get('total', 0)} odr lines, "
                f"lht+header attributed; zero-edit 2nd-class baselines)")
    return f"core-census: FAIL ({len(res['failures'])} problem(s))"


def _print_check(res: dict) -> None:
    print("Core census check (manifest: GT_esmini/docs/gt_roadmanager_patches.md)")
    for f in res["files"]:
        status = "OK  " if f["ok"] else "FAIL"
        total = f.get("total", 0)
        budget = f.get("budget", "?")
        print(f"  {status} {f['path']}  total={total}/{budget}")
        census = f.get("census") or {}
        if census:
            expected = f.get("expected") or {}
            per_id = "  ".join(f"{mid}={n}/{expected.get(mid, census.get(mid))}"
                               for mid, n in sorted(census.items()))
            print(f"        {per_id}")
        for p in f.get("problems", []):
            print(f"     >> {p}")
    print("  " + format_summary(res))


# ---------------------------------------------------------------------------
# record-baselines
# ---------------------------------------------------------------------------
def _git(repo_root: str, *args: str) -> bytes:
    out = subprocess.run(["git", *args], cwd=repo_root, stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE)
    if out.returncode != 0:
        raise RuntimeError(f"git {' '.join(args)} failed: {out.stderr.decode('utf-8', 'replace').strip()}")
    return out.stdout


def record_baselines(repo_root: str = DEFAULT_REPO_ROOT, prune: bool = False) -> int:
    """Snapshot the CURRENT HEAD blob of every manifest file (raw bytes -- no eol mangling)."""
    manifest = gt_patch_manifest.load_manifest(repo_root)
    paths = []
    for row in manifest["second_class_files"]:
        if row["path"] not in paths:
            paths.append(row["path"])
    cp = manifest["fork_file"]["pristine_counterpart"]
    if cp not in paths:
        paths.append(cp)

    base_dir = os.path.join(repo_root, BASELINE_RELDIR)
    os.makedirs(base_dir, exist_ok=True)
    print(f"Recording upstream baselines into {BASELINE_RELDIR} (HEAD blobs, byte-exact):")
    written = []
    rc = 0
    for path in paths:
        head_sha = _git(repo_root, "rev-parse", f"HEAD:{path}").decode("ascii").strip()
        data = _git(repo_root, "cat-file", "blob", f"HEAD:{path}")  # binary stdout, no eol filter
        got = git_blob_sha(data)
        if got != head_sha:
            print(f"  ERROR {path}: recomputed blob sha {got} != git rev-parse {head_sha}", file=sys.stderr)
            rc = 1
            continue
        snap = snapshot_path(repo_root, path, head_sha)
        with open(snap, "wb") as fh:
            fh.write(data)
        written.append(os.path.basename(snap))
        print(f"  {os.path.basename(snap)}  <- {path}")
        print(f"      manifest row: upstream_blob_sha: {head_sha}")
    if prune:
        referenced = {os.path.basename(snapshot_path(repo_root, row["path"], row["upstream_blob_sha"]))
                      for row in manifest["second_class_files"]}
        referenced.update(written)
        for name in sorted(os.listdir(base_dir)):
            if "@" in name and name not in referenced:
                os.remove(os.path.join(base_dir, name))
                print(f"  pruned {name}")
    return rc


# ---------------------------------------------------------------------------
# selftest (synthetic attribution cases; touches NO real files)
# ---------------------------------------------------------------------------
def _lines(*chunks) -> list:
    out = []
    for c in chunks:
        out += c if isinstance(c, list) else [c]
    return out


def run_selftest(quiet: bool = False) -> bool:
    base = [f"base line {i};" for i in range(60)]
    marked = ["// [GT_ODR:test-a] synthetic patch", "int patch_a = 1;", "int patch_b = 2;"]
    results = []

    def case(name: str, ok: bool, detail: str = "") -> None:
        results.append(ok)
        if not quiet or not ok:
            print(f"  {'PASS' if ok else 'FAIL'}  {name}{('  -- ' + detail) if detail else ''}")

    # R1 -- THE RED TEST: an unmarked hunk 5 lines BELOW a marked hunk (inside the old
    # checker's +15 proximity window) MUST be reported UNATTRIBUTED here.
    cur = _lines(base[:20], marked, base[20:25], "int sneaky = 666;", base[25:])
    res = census_lines(base, cur, is_fork=False)
    red_fail = bool(res["unattributed"])
    red_is_sneaky = red_fail and all("sneaky" in b["sample"] for b in res["unattributed"]) \
        and len(res["unattributed"]) == 1 and res["census"].get("test-a") == 3
    # Live proof the OLD checker attributes the same hunk (unsound proximity):
    old_attributed = None
    try:
        import check_fork_drift as cfd
        cov = cfd._build_marker_coverage(cur)
        sneaky_idx = cur.index("int sneaky = 666;")
        old_attributed = cfd._classify_block(cur, cov, sneaky_idx, sneaky_idx + 1, ["int sneaky = 666;"]) == "odr"
    except Exception:
        pass
    case("R1 RED: unmarked hunk beside a marker -> UNATTRIBUTED FAIL",
         red_fail and red_is_sneaky and old_attributed is not False,
         f"new=UNATTRIBUTED({red_fail}), old-proximity-checker-would-attribute={old_attributed}")

    # R2 -- green: only the marked hunk, census matches.
    cur = _lines(base[:20], marked, base[20:])
    res = census_lines(base, cur, is_fork=False)
    case("R2 green: marked hunk, census matches",
         not res["unattributed"] and not compare_census(res["census"], {"test-a": 3}))

    # R3 -- census mismatch: marked hunk but declared count wrong.
    case("R3 census mismatch: declared 2 != measured 3",
         bool(compare_census(res["census"], {"test-a": 2})))

    # R4 -- forgotten mirror: pristine carries the hunk (declared), fork lacks it.
    pristine_cur = _lines(base[:20], marked, base[20:])
    fork_cur = list(base)
    pris = census_lines(base, pristine_cur, is_fork=False)
    pris_ok = not compare_census(pris["census"], {"test-a": 3})
    fork_res = census_lines(base, fork_cur, is_fork=True)
    expected_fork = {"test-a": 3}  # fork-own {} + pristine-declared {test-a: 3} + residual 0
    case("R4 forgotten mirror: pristine has hunk, fork lacks it -> per-id mismatch",
         pris_ok and bool(compare_census(fork_res["census"], expected_fork)))

    # R5 -- additive_only violation: a deletion in an additive-only file.
    cur = base[:30] + base[31:]
    res = census_lines(base, cur, is_fork=False)
    case("R5 additive_only: deletion detected (and unattributed)",
         bool(res["deletions"]) and bool(res["unattributed"]))

    # R6 -- block form: >15-line hunk wrapped in -begin/-end attributes correctly.
    big = (["// [GT_ODR:big-begin] large synthetic hunk"] +
           [f"int big_{i} = {i};" for i in range(18)] +
           ["// [GT_ODR:big-end]"])
    cur = _lines(base[:40], big, base[40:])
    res = census_lines(base, cur, is_fork=False)
    case("R6 block form: -begin/-end hunk attributes to its id",
         not res["unattributed"] and not compare_census(res["census"], {"big": 20}))

    ok = all(results)
    if not quiet or not ok:
        print(f"  selftest: {'ALL PASS' if ok else 'FAILURES PRESENT'} ({sum(results)}/{len(results)})")
    return ok


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Two-sided per-marker-id census checker (manifest-driven).")
    ap.add_argument("command", nargs="?", default="check", choices=["check", "record-baselines", "selftest"])
    ap.add_argument("--repo-root", default=DEFAULT_REPO_ROOT)
    ap.add_argument("--json", action="store_true", help="machine output (check only)")
    ap.add_argument("--prune", action="store_true", help="record-baselines: delete unreferenced snapshots")
    args = ap.parse_args(argv)

    if args.command == "selftest":
        print("check_core_census selftest (synthetic attribution cases):")
        return 0 if run_selftest() else 1

    if args.command == "record-baselines":
        try:
            return record_baselines(args.repo_root, prune=args.prune)
        except (RuntimeError, ValueError) as e:
            print(f"ERROR: {e}", file=sys.stderr)
            return 1

    try:
        res = run_check(args.repo_root)
    except (RuntimeError, ValueError) as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 1
    if args.json:
        print(json.dumps(res, indent=2))
    else:
        _print_check(res)
    return 0 if res["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
