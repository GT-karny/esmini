#!/usr/bin/env python
"""odr_fixture_setup.py -- bootstrap ASAM OpenDRIVE 1.9 conformance fixtures.

WHY THIS EXISTS
---------------
The OpenDRIVE 1.6-1.9 support plan (GT_esmini/docs/archive/odr_1619_program/opendrive_16_19_support_plan.md,
P0 / section 3.3) needs a machine-verifiable conformance baseline built from the
official ASAM 1.9.0 release package. The ASAM assets are redistribution-restricted,
so this repository treats every extracted ASAM byte as LOCAL-ONLY: the extracted
trees under GT_esmini/test/odr_fixtures/{official,schema19} are gitignored and are
(re)materialised on demand from the source zips in thirdparty/opendrive/1.9/.

This module:
  1. Verifies the source zips against committed integrity pins (asam_pins.json).
  2. Extracts ONLY the .xodr entries of the examples/use-cases zip into
     official/, preserving the zip-internal relative paths.
  3. Extracts the 7 flat .xsd files of the schema zip into schema19/, TRANSFORMING
     each XSD's XML declaration from version="1.0" to version="1.1".

     WHY THE XSD TRANSFORM: the 1.9 XSDs declare `<?xml version="1.0"?>` yet carry
     `vc:minVersion="1.1"` (they use XSD-1.1 constructs). scripts/run_schema_comply.py
     selects the validation processor from the SCHEMA file's XML declaration
     (`lxml.etree.parse(schema).docinfo.xml_version`): "1.0" -> xmlschema.XMLSchema
     (XSD 1.0, reads the 1.1 schema as an empty/invalid schema -> every file fails),
     "1.1" -> xmlschema.XMLSchema11 (correct). Bumping the declaration to 1.1 routes
     the 1.9 XSDs to the XSD-1.1 processor. This mirrors upstream esmini's own 1.8
     precedent (their resources/schema/OpenDRIVE_1.8/local_schema/*.xsd are all 1.1).

The zips may be absent (fresh clone / CI without the ASAM package). That is not an
error: setup prints a single SKIP line and exits 0, and ensure_assets() returns
{"official": "skipped", "schema19": "skipped"} so callers can branch.

IMPORTABLE API
--------------
    from odr_fixture_setup import ensure_assets
    status = ensure_assets(repo_root, force=False)
    # -> {"official": "ok"|"skipped", "schema19": "ok"|"skipped"}

Stdlib only. Runnable from any CWD (repo root resolved from __file__).
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
import zipfile


# --------------------------------------------------------------------------- #
# Path helpers (repo-root relative, CWD-independent)
# --------------------------------------------------------------------------- #

def repo_root_from_file() -> str:
    """Repository root = parent of the scripts/ directory holding this file."""
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def _fixtures_dir(repo_root: str) -> str:
    return os.path.join(repo_root, "GT_esmini", "test", "odr_fixtures")


def _pins_path(repo_root: str) -> str:
    return os.path.join(_fixtures_dir(repo_root), "asam_pins.json")


def _source_dir(repo_root: str, pins: dict) -> str:
    return os.path.join(repo_root, *pins.get("source_dir", "thirdparty/opendrive/1.9").split("/"))


# Target subdir per logical zip key. Kept next to the pins for a single source of truth.
_TARGET_SUBDIR = {"examples": "official", "schema": "schema19"}
# Status-report key per logical zip key.
_STATUS_KEY = {"examples": "official", "schema": "schema19"}


# --------------------------------------------------------------------------- #
# Integrity pins
# --------------------------------------------------------------------------- #

def load_pins(repo_root: str) -> dict:
    with open(_pins_path(repo_root), "r", encoding="utf-8") as fh:
        return json.load(fh)


def _sha256_and_size(path: str) -> tuple[str, int]:
    h = hashlib.sha256()
    size = 0
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
            size += len(chunk)
    return h.hexdigest(), size


def _zip_counts(path: str) -> tuple[int, int]:
    """Return (xodr_count, xsd_count) of a zip archive."""
    with zipfile.ZipFile(path) as z:
        names = z.namelist()
    xodr = sum(1 for n in names if n.lower().endswith(".xodr"))
    xsd = sum(1 for n in names if n.lower().endswith(".xsd"))
    return xodr, xsd


def verify_pin(zip_path: str, spec: dict) -> list[str]:
    """Return a list of human-readable mismatch messages (empty == verified)."""
    problems: list[str] = []
    sha, size = _sha256_and_size(zip_path)
    if sha != spec["sha256"]:
        problems.append(f"sha256 mismatch: expected {spec['sha256']}, got {sha}")
    if size != spec["size"]:
        problems.append(f"size mismatch: expected {spec['size']}, got {size}")
    xodr, xsd = _zip_counts(zip_path)
    if xodr != spec["expected_xodr"]:
        problems.append(f".xodr count mismatch: expected {spec['expected_xodr']}, got {xodr}")
    if xsd != spec["expected_xsd"]:
        problems.append(f".xsd count mismatch: expected {spec['expected_xsd']}, got {xsd}")
    return problems


def compute_pins(repo_root: str) -> dict:
    """Recompute pins from the actual local zips (used by --print-pins)."""
    pins = load_pins(repo_root)
    src = _source_dir(repo_root, pins)
    out = {"source_dir": pins.get("source_dir", "thirdparty/opendrive/1.9"), "zips": {}}
    for key, spec in pins["zips"].items():
        zip_path = os.path.join(src, spec["filename"])
        entry = {"filename": spec["filename"], "target": spec.get("target", _TARGET_SUBDIR[key])}
        if os.path.isfile(zip_path):
            sha, size = _sha256_and_size(zip_path)
            xodr, xsd = _zip_counts(zip_path)
            entry.update(sha256=sha, size=size, expected_xodr=xodr, expected_xsd=xsd)
        else:
            entry["error"] = "zip not present"
        out["zips"][key] = entry
    return out


# --------------------------------------------------------------------------- #
# Extraction stamp (idempotency)
# --------------------------------------------------------------------------- #

def _stamp_path(target_dir: str) -> str:
    return os.path.join(target_dir, ".extracted.json")


def _read_stamp(target_dir: str) -> dict | None:
    p = _stamp_path(target_dir)
    if not os.path.isfile(p):
        return None
    try:
        with open(p, "r", encoding="utf-8") as fh:
            return json.load(fh)
    except (OSError, ValueError):
        return None


def _write_stamp(target_dir: str, sha256: str, files: list[str]) -> None:
    stamp = {"sha256": sha256, "files": sorted(files)}
    with open(_stamp_path(target_dir), "w", encoding="utf-8") as fh:
        json.dump(stamp, fh, indent=2, sort_keys=True)
        fh.write("\n")


def _stamp_matches(target_dir: str, sha256: str) -> bool:
    stamp = _read_stamp(target_dir)
    if stamp is None or stamp.get("sha256") != sha256:
        return False
    # Verify the recorded files still exist (guards against a half-deleted tree).
    for rel in stamp.get("files", []):
        if not os.path.isfile(os.path.join(target_dir, rel)):
            return False
    return True


# --------------------------------------------------------------------------- #
# Extraction
# --------------------------------------------------------------------------- #

def _extract_xodr(zip_path: str, target_dir: str) -> list[str]:
    """Extract only .xodr entries, preserving zip-internal relative paths.

    Returns the sorted list of written relative paths (forward slashes).
    """
    written: list[str] = []
    with zipfile.ZipFile(zip_path) as z:
        for info in z.infolist():
            name = info.filename
            if info.is_dir() or not name.lower().endswith(".xodr"):
                continue
            rel = name.replace("\\", "/")
            dest = os.path.join(target_dir, *rel.split("/"))
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            with z.open(info) as src, open(dest, "wb") as out:
                out.write(src.read())
            written.append(rel)
    return sorted(written)


def _transform_xsd_declaration(data: bytes) -> bytes:
    """Rewrite the leading XML declaration's version="1.0" to version="1.1".

    Only touches the declaration (the first `<?xml ... ?>` PI); the rest of the
    document is byte-preserved. Idempotent if already 1.1.
    """
    text = data.decode("utf-8")
    end = text.find("?>")
    if not text.lstrip().startswith("<?xml") or end == -1:
        # No XML declaration -> leave untouched (declaration-less XML defaults to 1.0;
        # the schema router would then pick XSD 1.0, which is out of scope for these files).
        return data
    decl = text[: end + 2]
    rest = text[end + 2:]
    new_decl = decl.replace('version="1.0"', 'version="1.1"').replace("version='1.0'", "version='1.1'")
    return (new_decl + rest).encode("utf-8")


def _extract_xsd(zip_path: str, target_dir: str) -> list[str]:
    """Extract .xsd entries flat, bumping each XML declaration to version 1.1.

    Returns the sorted list of written relative paths (forward slashes).
    """
    written: list[str] = []
    with zipfile.ZipFile(zip_path) as z:
        for info in z.infolist():
            name = info.filename
            if info.is_dir() or not name.lower().endswith(".xsd"):
                continue
            # Flat layout: the 1.9 XSDs cross-reference each other by bare filename.
            base = os.path.basename(name.replace("\\", "/"))
            data = _transform_xsd_declaration(z.read(info))
            dest = os.path.join(target_dir, base)
            os.makedirs(target_dir, exist_ok=True)
            with open(dest, "wb") as out:
                out.write(data)
            written.append(base)
    return sorted(written)


def _verify_xsd_versions(target_dir: str, files: list[str]) -> None:
    """Post-transform assertion: every written XSD reports docinfo.xml_version == '1.1'."""
    try:
        from lxml import etree  # noqa: WPS433 (import-inside-function is intentional)
    except ImportError:
        # lxml is required by the validator, but do not hard-fail setup if it is
        # missing in a bare environment -- extraction itself is stdlib-only.
        print("ODR-SETUP: WARN lxml unavailable, skipping post-transform XSD version check", flush=True)
        return
    for rel in files:
        p = os.path.join(target_dir, rel)
        ver = etree.parse(p).docinfo.xml_version
        if ver != "1.1":
            raise RuntimeError(
                f"ODR-SETUP: XSD version transform failed for {rel}: docinfo.xml_version={ver!r} (expected '1.1')"
            )


# --------------------------------------------------------------------------- #
# Public API
# --------------------------------------------------------------------------- #

def ensure_assets(repo_root: str, force: bool = False, allow_pin_mismatch: bool = False) -> dict:
    """Ensure ASAM fixtures are extracted. Idempotent.

    Returns {"official": <state>, "schema19": <state>} where state is "ok" or
    "skipped" (skipped == source zips absent). Raises SystemExit on pin mismatch
    unless allow_pin_mismatch is True. Raises RuntimeError on a failed XSD transform.
    """
    pins = load_pins(repo_root)
    src = _source_dir(repo_root, pins)
    fixtures = _fixtures_dir(repo_root)

    # If neither zip is present, this is a clean SKIP for all targets.
    present = {
        key: os.path.isfile(os.path.join(src, spec["filename"]))
        for key, spec in pins["zips"].items()
    }
    if not any(present.values()):
        print(f"ODR-SETUP: SKIP (ASAM zips not present at {pins.get('source_dir', 'thirdparty/opendrive/1.9')}/)", flush=True)
        return {v: "skipped" for v in _STATUS_KEY.values()}

    status: dict[str, str] = {}
    for key, spec in pins["zips"].items():
        status_key = _STATUS_KEY[key]
        target_dir = os.path.join(fixtures, _TARGET_SUBDIR[key])
        zip_path = os.path.join(src, spec["filename"])

        if not present[key]:
            print(f"ODR-SETUP: SKIP {status_key} ({spec['filename']} not present)", flush=True)
            status[status_key] = "skipped"
            continue

        # Verify pin before touching anything.
        problems = verify_pin(zip_path, spec)
        if problems:
            msg = f"ODR-SETUP: PIN MISMATCH for {spec['filename']}:\n  " + "\n  ".join(problems)
            if allow_pin_mismatch:
                print(msg + "\n  (continuing: --allow-pin-mismatch)", flush=True)
            else:
                print(msg, file=sys.stderr, flush=True)
                print(
                    "  Refusing to extract. Re-run with --allow-pin-mismatch to override, "
                    "or update pins with: python scripts/odr_fixture_setup.py --print-pins",
                    file=sys.stderr,
                    flush=True,
                )
                raise SystemExit(3)

        # Idempotency: skip when stamp matches (unless forced).
        if not force and _stamp_matches(target_dir, spec["sha256"]):
            print(f"ODR-SETUP: OK {status_key} (already extracted, stamp match)", flush=True)
            status[status_key] = "ok"
            continue

        os.makedirs(target_dir, exist_ok=True)
        if key == "examples":
            files = _extract_xodr(zip_path, target_dir)
        elif key == "schema":
            files = _extract_xsd(zip_path, target_dir)
            _verify_xsd_versions(target_dir, files)
        else:  # pragma: no cover - defensive
            raise RuntimeError(f"ODR-SETUP: unknown zip key {key!r}")

        _write_stamp(target_dir, spec["sha256"], files)
        print(f"ODR-SETUP: EXTRACTED {status_key} ({len(files)} files) -> {target_dir}", flush=True)
        status[status_key] = "ok"

    return status


# --------------------------------------------------------------------------- #
# CLI
# --------------------------------------------------------------------------- #

def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Extract ASAM OpenDRIVE 1.9 conformance fixtures (local-only).")
    parser.add_argument("--force", action="store_true", help="Re-extract even if the stamp matches.")
    parser.add_argument("--allow-pin-mismatch", action="store_true", help="Extract even if a zip fails its integrity pin.")
    parser.add_argument("--status-json", metavar="PATH", help="Write {official:..., schema19:...} status to PATH.")
    parser.add_argument("--print-pins", action="store_true", help="Print pins recomputed from the local zips (for asam_pins.json) and exit.")
    args = parser.parse_args(argv)

    repo_root = repo_root_from_file()

    if args.print_pins:
        print(json.dumps(compute_pins(repo_root), indent=2, sort_keys=True))
        return 0

    status = ensure_assets(repo_root, force=args.force, allow_pin_mismatch=args.allow_pin_mismatch)

    if args.status_json:
        with open(args.status_json, "w", encoding="utf-8") as fh:
            json.dump(status, fh, indent=2, sort_keys=True)
            fh.write("\n")
        print(f"ODR-SETUP: status written to {args.status_json}", flush=True)

    return 0


if __name__ == "__main__":
    sys.exit(main())
