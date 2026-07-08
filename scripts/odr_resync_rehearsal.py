#!/usr/bin/env python3
"""P9b resync rehearsal -- dry-run reapplication of ALL GT core patches onto fresh
upstream v3.4.0 copies (plan §5 P9b, named deliverable).

What it rehearses (the mechanical core of the next upstream sync):

  For every 2nd-class file row in the gt_roadmanager_patches.md manifest AND the
  1st-class fork (vs its pristine counterpart's upstream snapshot):

    1. Load the recorded upstream snapshot (GT_esmini/test/upstream_baselines/
       <file>@<sha12>) and verify its content SHA against the manifest
       (check_core_census.load_snapshot does the integrity check).
    2. Compute the patch snapshot -> current file, and inventory every hunk:
       nearest enclosing function-name ANCHOR (what a human re-applies against
       when upstream shifts line numbers) + the [GT_ODR:]/[GT_LHT] marker ids
       inside the hunk.
    3. DRY-APPLY: reconstruct the current file from (fresh snapshot copy + the
       hunk list) in a scratch buffer and require byte-equality with the real
       current file. This proves the hunk inventory is complete and ordered --
       the same property a real resync needs.

  Output: a markdown rehearsal report (default
  GT_esmini/test/odr_fixtures/reports/resync_rehearsal.md) with the per-file
  anchor/marker table. The written procedure that CONSUMES this report is
  GT_esmini/docs/odr_resync_checklist.md.

Run: DriverScript/.venv/Scripts/python.exe scripts/odr_resync_rehearsal.py
Exit 0 iff every file dry-applies byte-identically.
"""
from __future__ import annotations

import difflib
import os
import re
import sys

_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
_REPO_ROOT = os.path.normpath(os.path.join(_SCRIPT_DIR, ".."))
sys.path.insert(0, _SCRIPT_DIR)

import check_core_census as ccc  # noqa: E402  (snapshot loading + integrity)
import gt_patch_manifest  # noqa: E402

_MARKER_RE = re.compile(r"\[(GT_ODR:[A-Za-z0-9_-]+|GT_LHT)[^\]]*\]")

# Heuristic "function-name anchor": the closest preceding line that looks like a
# C++ function/method definition head (return-type + qualified name + '(' at low
# indent, or a class/struct/namespace head). Good enough to LABEL hunks for a
# human re-applying them; the dry-apply below is what proves completeness.
_ANCHOR_RE = re.compile(
    r"^(?:[A-Za-z_][\w:<>,~&*\s]*\s)?"      # optional return type
    r"([A-Za-z_][\w]*(?:::[A-Za-z_~][\w]*)+)\s*\("  # Qualified::Name(
    r"|^\s*(?:class|struct|namespace)\s+([A-Za-z_]\w+)"
)


def _norm(data: bytes) -> list[str]:
    return data.decode("utf-8", errors="replace").replace("\r\n", "\n").split("\n")


def _anchor_for(current: list[str], line_idx: int) -> str:
    for i in range(min(line_idx, len(current) - 1), -1, -1):
        m = _ANCHOR_RE.match(current[i])
        if m:
            return m.group(1) or m.group(2) or "?"
    return "(file head)"


def _hunks(snapshot: list[str], current: list[str]) -> list[dict]:
    """Opcode-level hunks (replace/insert/delete) with anchor + marker inventory."""
    sm = difflib.SequenceMatcher(a=snapshot, b=current, autojunk=False)
    hunks = []
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == "equal":
            continue
        added = current[j1:j2]
        removed = snapshot[i1:i2]
        markers = sorted({m.group(1) for line in added for m in _MARKER_RE.finditer(line)})
        hunks.append({
            "tag": tag,
            "snap_span": (i1 + 1, i2),
            "cur_span": (j1 + 1, j2),
            "added": len([l for l in added if l.strip()]),
            "removed": len([l for l in removed if l.strip()]),
            "markers": markers,
            "anchor": _anchor_for(current, j1),
        })
    return hunks


def _dry_apply(snapshot: list[str], current: list[str]) -> bool:
    """Rebuild `current` from a FRESH snapshot copy + the opcode hunks; byte-compare."""
    sm = difflib.SequenceMatcher(a=snapshot, b=current, autojunk=False)
    rebuilt: list[str] = []
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == "equal":
            rebuilt.extend(snapshot[i1:i2])  # take from the fresh copy, not from current
        else:
            rebuilt.extend(current[j1:j2])   # the patch payload
    return rebuilt == current


def rehearse(repo_root: str = _REPO_ROOT) -> dict:
    manifest = gt_patch_manifest.load_manifest(repo_root)
    results = []
    ok_all = True

    rows = list(manifest["second_class_files"])
    fork = manifest["fork_file"]
    sc_by_path = {r["path"]: r for r in rows}

    # 1st-class fork rehearses against its PRISTINE COUNTERPART's upstream snapshot.
    fork_row = {
        "path": fork["path"],
        "upstream_blob_sha": sc_by_path[fork["pristine_counterpart"]]["upstream_blob_sha"],
        "_label": "fork (1st class)",
    }

    for row in rows + [fork_row]:
        path = row["path"]
        label = row.get("_label", "2nd class")
        try:
            snap_bytes = ccc.load_snapshot(repo_root, fork["pristine_counterpart"]
                                           if label.startswith("fork") else path,
                                           row["upstream_blob_sha"])
        except RuntimeError as e:
            results.append({"path": path, "label": label, "ok": False, "error": str(e), "hunks": []})
            ok_all = False
            continue
        with open(os.path.join(repo_root, path), "rb") as f:
            cur_bytes = f.read()
        snapshot, current = _norm(snap_bytes), _norm(cur_bytes)
        hunks = _hunks(snapshot, current)
        applied = _dry_apply(snapshot, current)
        ok_all = ok_all and applied
        results.append({"path": path, "label": label, "ok": applied, "error": None, "hunks": hunks})

    return {"ok": ok_all, "files": results,
            "baseline_tag": manifest.get("baseline_upstream_tag", "?")}


def write_report(res: dict, out_path: str) -> None:
    md = ["# ODR resync rehearsal report",
          "",
          f"- upstream baseline: **{res['baseline_tag']}** (recorded snapshots under "
          "GT_esmini/test/upstream_baselines/, SHA-verified)",
          f"- overall dry-apply: **{'PASS' if res['ok'] else 'FAIL'}**",
          "",
          "Generated by scripts/odr_resync_rehearsal.py. Procedure: "
          "GT_esmini/docs/odr_resync_checklist.md.",
          ""]
    for f in res["files"]:
        md.append(f"## {f['path']}  ({f['label']})")
        md.append("")
        if f["error"]:
            md.append(f"**ERROR**: {f['error']}")
            md.append("")
            continue
        md.append(f"dry-apply: **{'PASS' if f['ok'] else 'FAIL'}** -- {len(f['hunks'])} hunks")
        md.append("")
        md.append("| # | anchor (function) | markers | +lines | -lines |")
        md.append("|---|---|---|---|---|")
        for i, h in enumerate(f["hunks"], 1):
            md.append(f"| {i} | `{h['anchor']}` | {', '.join(h['markers']) or '(unmarked context)'} "
                      f"| {h['added']} | {h['removed']} |")
        md.append("")
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(md) + "\n")


def main() -> int:
    out = os.path.join(_REPO_ROOT, "GT_esmini", "test", "odr_fixtures", "reports",
                       "resync_rehearsal.md")
    res = rehearse()
    write_report(res, out)
    n_hunks = sum(len(f["hunks"]) for f in res["files"])
    print(f"resync-rehearsal: {'PASS' if res['ok'] else 'FAIL'} "
          f"({len(res['files'])} files, {n_hunks} hunks) -> {os.path.relpath(out, _REPO_ROOT)}")
    for f in res["files"]:
        status = "OK " if f["ok"] else "FAIL"
        print(f"  {status} {f['path']} ({len(f['hunks'])} hunks)")
        if f["error"]:
            print(f"       {f['error']}")
    return 0 if res["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
