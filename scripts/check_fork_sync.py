#!/usr/bin/env python
"""check_fork_sync.py -- INBOUND upstream sync gate for the GT fork lineages (audit R4).

Reads the machine-readable manifest GT_esmini/docs/fork_sync_manifest.yaml and, for each
fork lineage (gt_roadmanager / gt_osireporter / roadgen), lists the upstream commits that
landed on the upstream counterpart files AFTER the lineage's recorded base commit:

    git log <base.commit>..<upstream_ref> -- <upstream_paths>

minus the commits declared `ported` in the manifest (already ported verbatim,
re-implemented GT-side, or reviewed & rejected). Anything left is a PENDING upstream
change the fork may be missing -- the failure mode behind audit CORE-1 (an upstream
segfault fix that was silently never picked up).

ROLE (do not confuse with the outbound checkers):
  - scripts/check_fork_drift.py / scripts/check_core_census.py answer the OUTBOUND
    question "is every FORK-side deviation from upstream sanctioned by the patch
    manifest?" (drift of our edits).
  - THIS script answers the INBOUND question "have we taken (or consciously reviewed)
    every UPSTREAM update to the files we forked?" (missed upstream fixes).

Exit code: 0 even when pending commits exist (WARN report); --strict turns any pending
commit into exit 1. Hard errors (missing manifest, unresolvable base SHA) always exit 2.

Upstream ref resolution: tries `upstream_ref` from the manifest (default upstream/master);
if that ref does not exist locally (e.g. CI checkout of the GT repo without the upstream
remote), --fetch fetches `upstream_branch` from `upstream_url` and uses FETCH_HEAD.
Without --fetch, an unresolvable upstream ref is reported as SKIP (exit 0) so the gate
never blocks offline runs.

Pure text: stdlib + PyYAML only (same dependency as gt_patch_manifest.py). No build/DLL
needed; works standalone from any checkout with full git history.

USAGE
-----
  check_fork_sync.py [--repo-root PATH] [--manifest PATH] [--lineage ID]
                     [--upstream-ref REF] [--fetch] [--strict] [--json] [--quiet]
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys

import yaml

_THIS_DIR = os.path.dirname(os.path.abspath(__file__))
DEFAULT_REPO_ROOT = os.path.dirname(_THIS_DIR)
MANIFEST_RELPATH = os.path.join("GT_esmini", "docs", "fork_sync_manifest.yaml")


class SyncError(RuntimeError):
    """Hard configuration/environment error (exit 2)."""


# ---------------------------------------------------------------------------
# git plumbing
# ---------------------------------------------------------------------------
def _git(repo_root: str, *args: str, check: bool = True) -> str:
    out = subprocess.run(["git", *args], cwd=repo_root,
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if check and out.returncode != 0:
        raise SyncError(f"git {' '.join(args)} failed: "
                        f"{out.stderr.decode('utf-8', 'replace').strip()}")
    return out.stdout.decode("utf-8", "replace")


def _rev_parse(repo_root: str, rev: str) -> str | None:
    out = subprocess.run(["git", "rev-parse", "--verify", "--quiet", f"{rev}^{{commit}}"],
                         cwd=repo_root, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if out.returncode != 0:
        return None
    return out.stdout.decode("ascii").strip()


def resolve_upstream(repo_root: str, manifest: dict, ref_override: str | None,
                     fetch: bool) -> tuple[str | None, str, str | None]:
    """Return (sha, description, skip_reason). sha None => SKIP with skip_reason."""
    ref = ref_override or manifest.get("upstream_ref", "upstream/master")
    sha = _rev_parse(repo_root, ref)
    if sha:
        return sha, ref, None
    url = manifest.get("upstream_url")
    branch = manifest.get("upstream_branch", "master")
    if fetch and url:
        out = subprocess.run(["git", "fetch", "--quiet", url, branch], cwd=repo_root,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if out.returncode == 0:
            sha = _rev_parse(repo_root, "FETCH_HEAD")
            if sha:
                return sha, f"FETCH_HEAD ({url} {branch})", None
        return None, ref, (f"fetch from {url} {branch} failed: "
                           f"{out.stderr.decode('utf-8', 'replace').strip()[:200]}")
    return None, ref, (f"upstream ref '{ref}' not found locally "
                       f"(add the upstream remote or re-run with --fetch)")


# ---------------------------------------------------------------------------
# manifest
# ---------------------------------------------------------------------------
def load_manifest(path: str) -> dict:
    if not os.path.isfile(path):
        raise SyncError(f"manifest not found: {path}")
    with open(path, "r", encoding="utf-8") as fh:
        try:
            m = yaml.safe_load(fh)
        except yaml.YAMLError as e:
            raise SyncError(f"manifest YAML parse error: {e}")
    if not isinstance(m, dict) or m.get("version") != 1:
        raise SyncError("manifest must be a mapping with version: 1")
    lineages = m.get("lineages")
    if not isinstance(lineages, list) or not lineages:
        raise SyncError("manifest 'lineages' must be a non-empty list")
    for i, ln in enumerate(lineages):
        where = f"lineages[{i}] ({ln.get('id', '?')})"
        for key in ("id", "fork_paths", "upstream_paths", "base"):
            if key not in ln:
                raise SyncError(f"{where}: missing key '{key}'")
        base = ln["base"]
        commit = str(base.get("commit", ""))
        if len(commit) != 40:
            raise SyncError(f"{where}: base.commit must be a 40-char sha, got {commit!r}")
        for p in ln.get("ported") or []:
            if len(str(p.get("commit", ""))) != 40:
                raise SyncError(f"{where}: ported[].commit must be a 40-char sha")
    return m


# ---------------------------------------------------------------------------
# core check
# ---------------------------------------------------------------------------
def check_lineage(repo_root: str, lineage: dict, upstream_sha: str) -> dict:
    lid = lineage["id"]
    base = lineage["base"]["commit"]
    if _rev_parse(repo_root, base) is None:
        raise SyncError(f"lineage '{lid}': base commit {base[:12]} not found in this "
                        f"repository (shallow clone? fetch full history)")
    ported = {str(p["commit"]) for p in (lineage.get("ported") or [])}
    ported_notes = {str(p["commit"]): str(p.get("reason", "")).strip()
                    for p in (lineage.get("ported") or [])}
    raw = _git(repo_root, "log", "--format=%H%x09%ad%x09%s", "--date=short",
               f"{base}..{upstream_sha}", "--", *lineage["upstream_paths"])
    pending, handled = [], []
    for line in raw.splitlines():
        sha, date, subject = (line.split("\t", 2) + ["", ""])[:3]
        entry = {"sha": sha, "date": date, "subject": subject}
        if sha in ported:
            entry["reason"] = ported_notes.get(sha, "")
            handled.append(entry)
        else:
            pending.append(entry)
    return {
        "id": lid,
        "base": base,
        "base_method": lineage["base"].get("method", "?"),
        "upstream_paths": list(lineage["upstream_paths"]),
        "pending": pending,
        "ported": handled,
        "clean": not pending,
    }


def run_check(repo_root: str, manifest_path: str, lineage_filter: str | None,
              ref_override: str | None, fetch: bool) -> dict:
    manifest = load_manifest(manifest_path)
    upstream_sha, upstream_desc, skip = resolve_upstream(repo_root, manifest,
                                                         ref_override, fetch)
    result = {"upstream_ref": upstream_desc, "upstream_sha": upstream_sha,
              "skipped": skip, "lineages": []}
    if skip:
        return result
    for ln in manifest["lineages"]:
        if lineage_filter and ln["id"] != lineage_filter:
            continue
        result["lineages"].append(check_lineage(repo_root, ln, upstream_sha))
    if lineage_filter and not result["lineages"]:
        raise SyncError(f"no lineage with id '{lineage_filter}' in the manifest")
    return result


# ---------------------------------------------------------------------------
# reporting
# ---------------------------------------------------------------------------
def format_summary(res: dict) -> str:
    if res.get("skipped"):
        return f"fork-sync: SKIP ({res['skipped']})"
    total = sum(len(l["pending"]) for l in res["lineages"])
    n = len(res["lineages"])
    if total == 0:
        return f"fork-sync: OK ({n} lineage(s) clean vs {res['upstream_ref']})"
    dirty = sum(1 for l in res["lineages"] if l["pending"])
    return (f"fork-sync: WARN ({total} un-ported upstream commit(s) across "
            f"{dirty}/{n} lineage(s) vs {res['upstream_ref']})")


def _print_report(res: dict, quiet: bool) -> None:
    print("Fork sync check (manifest: GT_esmini/docs/fork_sync_manifest.yaml)")
    if res.get("skipped"):
        print("  " + format_summary(res))
        return
    print(f"  upstream: {res['upstream_ref']} @ {res['upstream_sha'][:12]}")
    for ln in res["lineages"]:
        status = "OK  " if ln["clean"] else "WARN"
        print(f"  {status} {ln['id']}  base={ln['base'][:12]} ({ln['base_method']})  "
              f"pending={len(ln['pending'])}  ported={len(ln['ported'])}")
        if not quiet:
            for c in ln["pending"]:
                print(f"     >> PENDING {c['sha'][:12]} {c['date']}  {c['subject']}")
            for c in ln["ported"]:
                reason = f"  -- {c['reason']}" if c.get("reason") else ""
                print(f"        ported  {c['sha'][:12]} {c['date']}  {c['subject']}{reason}")
    print("  " + format_summary(res))


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(
        description="Inbound upstream sync gate: list upstream commits not yet "
                    "ported into the GT fork lineages (WARN by default).")
    ap.add_argument("--repo-root", default=DEFAULT_REPO_ROOT)
    ap.add_argument("--manifest", default=None,
                    help=f"manifest path (default: <repo-root>/{MANIFEST_RELPATH})")
    ap.add_argument("--lineage", default=None, help="check a single lineage id only")
    ap.add_argument("--upstream-ref", default=None,
                    help="override the manifest upstream_ref (e.g. a pinned sha)")
    ap.add_argument("--fetch", action="store_true",
                    help="fetch upstream_url/upstream_branch when the local ref is missing "
                         "(for CI checkouts without the upstream remote)")
    ap.add_argument("--strict", action="store_true",
                    help="exit 1 when any pending upstream commit exists (default: WARN, exit 0)")
    ap.add_argument("--json", action="store_true", help="machine output")
    ap.add_argument("--quiet", action="store_true",
                    help="one line per lineage + summary (no per-commit rows)")
    args = ap.parse_args(argv)

    manifest_path = args.manifest or os.path.join(args.repo_root, MANIFEST_RELPATH)
    try:
        res = run_check(args.repo_root, manifest_path, args.lineage,
                        args.upstream_ref, args.fetch)
    except SyncError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2

    if args.json:
        print(json.dumps(res, indent=2))
    else:
        _print_report(res, args.quiet)

    if res.get("skipped"):
        return 0
    pending = sum(len(l["pending"]) for l in res["lineages"])
    return 1 if (args.strict and pending) else 0


if __name__ == "__main__":
    sys.exit(main())
