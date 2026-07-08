#!/usr/bin/env python
"""gt_patch_manifest.py -- load the machine-readable GT patch manifest (single source of truth).

The manifest lives as a fenced YAML block inside

    GT_esmini/docs/gt_roadmanager_patches.md

bracketed by the HTML comment sentinels

    <!-- GT-2ND-CLASS-MANIFEST-BEGIN -->
    ```yaml
    ...
    ```
    <!-- GT-2ND-CLASS-MANIFEST-END -->

Consumers (no expected values embedded in scripts -- the stale `_DEFAULT_EXPECT_ODR=16`
incident in check_fork_drift.py is the motivating failure):

  - scripts/check_core_census.py      (authoritative two-sided census checker)
  - scripts/check_fork_drift.py       (legacy coarse-grained drift check)
  - scripts/run_odr_conformance.py    (conformance harness gates)
  - GT_esmini/test/unit/road/test_OdrForkPatches.cpp (ctest census; naive line parse)

`load_manifest(repo_root)` returns the parsed dict after validation; raises ValueError
with a clear message on any structural problem. No side effects on import.
"""
from __future__ import annotations

import json
import os
import sys

import yaml

SENTINEL_BEGIN = "<!-- GT-2ND-CLASS-MANIFEST-BEGIN -->"
SENTINEL_END = "<!-- GT-2ND-CLASS-MANIFEST-END -->"

MANIFEST_DOC_RELPATH = os.path.join("GT_esmini", "docs", "gt_roadmanager_patches.md")

_THIS_DIR = os.path.dirname(os.path.abspath(__file__))
DEFAULT_REPO_ROOT = os.path.dirname(_THIS_DIR)

# Top-level keys that MUST be present.
_REQUIRED_TOP_KEYS = (
    "version",
    "baseline_upstream_tag",
    "fork_odr_marker_total",
    "fork_lht_marker_min",
    "cmake_marker_total",
    "fork_odr_expect_lines",
    "fork_line_budget",
    "second_class_files",
    "fork_file",
    "overlap_residuals",
    "rules",
    "exclusions",
)
_REQUIRED_SC_ROW_KEYS = ("path", "upstream_blob_sha", "budget_nonblank", "additive_only", "marker_census")
_REQUIRED_FORK_KEYS = ("path", "pristine_counterpart", "marker_census", "lht_census", "header_census")


def _fail(msg: str):
    raise ValueError(f"gt_patch_manifest: {msg} (doc: {MANIFEST_DOC_RELPATH})")


def extract_yaml_text(md_text: str) -> str:
    """Extract the YAML payload between the sentinels, with the ```yaml fence stripped."""
    try:
        begin = md_text.index(SENTINEL_BEGIN)
        end = md_text.index(SENTINEL_END)
    except ValueError:
        _fail(f"sentinels not found ({SENTINEL_BEGIN} / {SENTINEL_END})")
    if end <= begin:
        _fail("END sentinel precedes BEGIN sentinel")
    body = md_text[begin + len(SENTINEL_BEGIN):end]
    lines = body.replace("\r\n", "\n").split("\n")
    # Strip the fence lines (```yaml ... ```). Everything between the first fence open and
    # the last fence close is the payload.
    fence_idx = [i for i, ln in enumerate(lines) if ln.strip().startswith("```")]
    if len(fence_idx) < 2:
        _fail("fenced ```yaml block not found between the sentinels")
    return "\n".join(lines[fence_idx[0] + 1:fence_idx[-1]])


def _check_census_dict(census, where: str) -> None:
    if not isinstance(census, dict):
        _fail(f"{where}.marker_census must be a mapping, got {type(census).__name__}")
    for k, v in census.items():
        if not isinstance(v, int) or isinstance(v, bool) or v < 0:
            _fail(f"{where}.marker_census[{k!r}] must be an int >= 0, got {v!r}")


def validate(m) -> dict:
    if not isinstance(m, dict):
        _fail(f"top level must be a mapping, got {type(m).__name__}")
    for k in _REQUIRED_TOP_KEYS:
        if k not in m:
            _fail(f"missing required top-level key '{k}'")
    if m["version"] != 1:
        _fail(f"unsupported manifest version {m['version']!r} (expected 1)")
    for k in ("fork_odr_marker_total", "fork_lht_marker_min", "cmake_marker_total",
              "fork_odr_expect_lines", "fork_line_budget"):
        if not isinstance(m[k], int) or isinstance(m[k], bool) or m[k] < 0:
            _fail(f"'{k}' must be an int >= 0, got {m[k]!r}")
    if not isinstance(m["second_class_files"], list) or not m["second_class_files"]:
        _fail("'second_class_files' must be a non-empty list")
    for i, row in enumerate(m["second_class_files"]):
        where = f"second_class_files[{i}]"
        if not isinstance(row, dict):
            _fail(f"{where} must be a mapping")
        for k in _REQUIRED_SC_ROW_KEYS:
            if k not in row:
                _fail(f"{where} ('{row.get('path', '?')}') missing key '{k}'")
        if not isinstance(row["upstream_blob_sha"], str) or len(row["upstream_blob_sha"]) != 40:
            _fail(f"{where}.upstream_blob_sha must be a 40-char hex sha, got {row['upstream_blob_sha']!r}")
        _check_census_dict(row["marker_census"], where)
        grp = row.get("budget_group")
        if grp is not None and grp not in (m.get("budget_groups") or {}):
            _fail(f"{where}.budget_group '{grp}' not declared in top-level budget_groups")
    fork = m["fork_file"]
    if not isinstance(fork, dict):
        _fail("'fork_file' must be a mapping")
    for k in _REQUIRED_FORK_KEYS:
        if k not in fork:
            _fail(f"fork_file missing key '{k}'")
    _check_census_dict(fork["marker_census"], "fork_file")
    for k in ("lht_census", "header_census"):
        if not isinstance(fork[k], int) or isinstance(fork[k], bool) or fork[k] < 0:
            _fail(f"fork_file.{k} must be an int >= 0")
    for i, site in enumerate(fork.get("legacy_sites") or []):
        where = f"fork_file.legacy_sites[{i}]"
        for k in ("marker", "fork_lines", "count"):
            if k not in site:
                _fail(f"{where} missing key '{k}'")
        if not isinstance(site["count"], int) or site["count"] < 0:
            _fail(f"{where}.count must be an int >= 0")
    for i, r in enumerate(m["overlap_residuals"]):
        where = f"overlap_residuals[{i}]"
        for k in ("site", "vj_marker", "residual_nonblank"):
            if k not in r:
                _fail(f"{where} missing key '{k}'")
        if not isinstance(r["residual_nonblank"], int) or r["residual_nonblank"] < 0:
            _fail(f"{where}.residual_nonblank must be an int >= 0")
    return m


def load_manifest(repo_root: str = DEFAULT_REPO_ROOT) -> dict:
    """Parse + validate the manifest; returns the dict. Raises ValueError on any problem."""
    doc_path = os.path.join(repo_root, MANIFEST_DOC_RELPATH)
    if not os.path.isfile(doc_path):
        _fail(f"manifest doc not found: {doc_path}")
    with open(doc_path, "r", encoding="utf-8") as fh:
        md_text = fh.read()
    yaml_text = extract_yaml_text(md_text)
    try:
        m = yaml.safe_load(yaml_text)
    except yaml.YAMLError as e:
        _fail(f"YAML parse error: {e}")
    return validate(m)


def main(argv=None) -> int:
    """Debug aid: print the parsed manifest as JSON."""
    repo_root = argv[0] if argv else DEFAULT_REPO_ROOT
    try:
        m = load_manifest(repo_root)
    except ValueError as e:
        print(str(e), file=sys.stderr)
        return 1
    print(json.dumps(m, indent=2, sort_keys=False))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
