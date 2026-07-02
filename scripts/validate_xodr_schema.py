#!/usr/bin/env python
"""validate_xodr_schema.py -- schema-validate .xodr files, including OpenDRIVE 1.9.

WHY THIS EXISTS
---------------
The upstream validator scripts/run_schema_comply.py maps xodr revMinor -> XSD but
stops at revMinor "8" (OpenDRIVE 1.8). The 1.9 XSDs are ASAM redistribution-
restricted, so they are not committed under resources/schema; instead they are
extracted on demand into GT_esmini/test/odr_fixtures/schema19/ by
scripts/odr_fixture_setup.py (with the XML-declaration-to-1.1 transform that routes
them to xmlschema.XMLSchema11 -- see that module's docstring).

This CLI wraps run_schema_comply.py's XmlValidation WITHOUT modifying it:
  * imports it as a module (sys.path insert),
  * extends its module-level SCHEMA_MAPPINGS["xodr"] at RUNTIME with
    "9" -> <abs path to schema19/OpenDRIVE_Core.xsd>. get_xsd_to_validate() joins
    the mapping value onto xsd_files_path via os.path.join; os.path.join with an
    absolute second argument returns that absolute path (verified on Windows), so
    an absolute value bypasses the resources/schema base cleanly. No monkeypatch
    of the join is needed.
  * runs one fresh XmlValidation instance PER FILE so per-file PASS/FAIL is exact
    (the upstream class accumulates counters/errors across files).

For revMinor-9 files it auto-bootstraps the 1.9 XSDs via
odr_fixture_setup.ensure_assets(); if schema19 is unavailable the per-file result
is SKIP_NO_SCHEMA19 (a failure only under --strict).

Per-file results:
  PASS             schema-valid
  FAIL             schema-invalid (first 3 error lines shown)
  SKIP_NO_MAPPING  revMinor not in SCHEMA_MAPPINGS["xodr"]
  SKIP_NO_SCHEMA19 revMinor 9 but the 1.9 XSDs are not present locally

Exit 0 iff no FAIL (and no SKIP when --strict). Stdlib + xmlschema/lxml/pyyaml.
Runnable from any CWD. Run under DriverScript/.venv (has xmlschema 4.3.1, lxml).
"""
from __future__ import annotations

import argparse
import json
import os
import sys

_THIS_DIR = os.path.dirname(os.path.abspath(__file__))
_REPO_ROOT = os.path.dirname(_THIS_DIR)

# Make sibling scripts importable regardless of CWD.
if _THIS_DIR not in sys.path:
    sys.path.insert(0, _THIS_DIR)

import odr_fixture_setup  # noqa: E402  (sibling script, path inserted above)
import run_schema_comply  # noqa: E402  (upstream validator, imported unmodified)


SCHEMA19_CORE = os.path.join(
    _REPO_ROOT, "GT_esmini", "test", "odr_fixtures", "schema19", "OpenDRIVE_Core.xsd"
)

# Per-file result tags.
PASS = "PASS"
FAIL = "FAIL"
SKIP_NO_MAPPING = "SKIP_NO_MAPPING"
SKIP_NO_SCHEMA19 = "SKIP_NO_SCHEMA19"


def _register_rev9_mapping() -> None:
    """Extend the upstream mapping in place with revMinor 9 -> absolute 1.9 Core XSD.

    os.path.join(xsd_files_path, <abs>) == <abs>, so get_xsd_to_validate resolves the
    1.9 schema outside resources/schema without any method override.
    """
    run_schema_comply.SCHEMA_MAPPINGS["xodr"]["9"] = SCHEMA19_CORE


def _read_revminor(path: str) -> str | None:
    """Read xodr header revMinor without printing upstream error side effects."""
    v = run_schema_comply.XmlValidation()
    if not v.open_file(path):
        return None
    # open_file populates v.root; reuse the upstream extractor but swallow its logging.
    saved_errors = list(v.errors)
    rev = v.get_xml_header_minor_revision(path)
    v.errors = saved_errors  # discard any error appended by the extractor
    return rev


def _schema19_available() -> bool:
    return os.path.isfile(SCHEMA19_CORE)


def validate_file(path: str) -> tuple[str, list[str]]:
    """Validate one .xodr file. Returns (result_tag, error_lines)."""
    rev = _read_revminor(path)
    if rev is None:
        return FAIL, [f"{path}: header/revMinor missing or unparseable"]

    if rev not in run_schema_comply.SCHEMA_MAPPINGS["xodr"]:
        return SKIP_NO_MAPPING, [f"{path}: revMinor={rev} has no schema mapping"]

    if rev == "9" and not _schema19_available():
        return SKIP_NO_SCHEMA19, [f"{path}: revMinor=9 but schema19 not present (run odr_fixture_setup.py)"]

    # Fresh instance per file so counters/errors are file-local.
    v = run_schema_comply.XmlValidation()
    if not v.open_file(path):
        return FAIL, [e for e in v.errors] or [f"{path}: parse error"]

    xsd = v.get_xsd_to_validate(rev, path)
    if xsd is None:
        # get_xsd_to_validate logged into v.errors on failure.
        return FAIL, list(v.errors) or [f"{path}: could not resolve schema for revMinor={rev}"]

    v.validate(path, xsd)
    if v.get_count_of_files_failed_to_validate() == 0 and v.get_count_of_files_validated() > 0:
        return PASS, []
    return FAIL, list(v.errors)


def _display_path(abs_path: str) -> str:
    """Repo-relative path with forward slashes; falls back to abs (cross-drive on Windows)."""
    try:
        return os.path.relpath(abs_path, _REPO_ROOT).replace("\\", "/")
    except ValueError:
        return abs_path.replace("\\", "/")


def _collect_xodr(paths: list[str]) -> list[str]:
    """Expand files/dirs to a sorted, de-duplicated list of .xodr files."""
    out: set[str] = set()
    for p in paths:
        if os.path.isfile(p):
            if p.lower().endswith(".xodr"):
                out.add(os.path.abspath(p))
        elif os.path.isdir(p):
            for root, _dirs, files in os.walk(p):
                for f in files:
                    if f.lower().endswith(".xodr"):
                        out.add(os.path.abspath(os.path.join(root, f)))
        else:
            print(f"WARN: path not found: {p}", file=sys.stderr, flush=True)
    return sorted(out)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Schema-validate .xodr files (OpenDRIVE 1.4-1.9).")
    parser.add_argument("paths", nargs="+", help="Files or directories (dirs recurse for *.xodr).")
    parser.add_argument("--report", metavar="JSON", help="Write a JSON report to this path.")
    parser.add_argument("--strict", action="store_true", help="Treat any SKIP as failure (non-zero exit).")
    parser.add_argument("--no-bootstrap", action="store_true", help="Do not auto-extract the 1.9 XSDs.")
    args = parser.parse_args(argv)

    _register_rev9_mapping()

    files = _collect_xodr(args.paths)
    if not files:
        print("No .xodr files found.", file=sys.stderr, flush=True)
        return 1

    # Auto-bootstrap the 1.9 XSDs only if some file is revMinor 9 and they are missing.
    if not args.no_bootstrap and not _schema19_available():
        needs_19 = any(_read_revminor(f) == "9" for f in files)
        if needs_19:
            odr_fixture_setup.ensure_assets(_REPO_ROOT)

    results: list[dict] = []
    counts = {PASS: 0, FAIL: 0, SKIP_NO_MAPPING: 0, SKIP_NO_SCHEMA19: 0}
    for f in files:
        tag, errs = validate_file(f)
        counts[tag] += 1
        results.append({"path": _display_path(f), "result": tag, "errors": errs[:3]})

    # --- Summary table (deterministic order: by path) ---
    width = max((len(r["path"]) for r in results), default=4)
    print("")
    print(f"{'RESULT':<16} {'FILE':<{width}}")
    print(f"{'-' * 16} {'-' * width}")
    for r in results:
        print(f"{r['result']:<16} {r['path']:<{width}}")
        if r["result"] == FAIL:
            for line in r["errors"]:
                print(f"    {line}")
    print("")
    print(
        f"Summary: {counts[PASS]} PASS, {counts[FAIL]} FAIL, "
        f"{counts[SKIP_NO_MAPPING]} SKIP_NO_MAPPING, {counts[SKIP_NO_SCHEMA19]} SKIP_NO_SCHEMA19 "
        f"(total {len(results)})"
    )

    if args.report:
        report = {
            "counts": counts,
            "total": len(results),
            "results": results,
        }
        report_dir = os.path.dirname(os.path.abspath(args.report))
        os.makedirs(report_dir, exist_ok=True)
        with open(args.report, "w", encoding="utf-8") as fh:
            json.dump(report, fh, indent=2, sort_keys=True)
            fh.write("\n")
        print(f"Report written to {args.report}")

    total_skips = counts[SKIP_NO_MAPPING] + counts[SKIP_NO_SCHEMA19]
    if counts[FAIL] > 0:
        return 1
    if args.strict and total_skips > 0:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
