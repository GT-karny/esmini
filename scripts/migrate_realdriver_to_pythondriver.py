#!/usr/bin/env python3
"""Migrate XOSC controller properties from RealDriver to PythonDriverController."""

from __future__ import annotations

import argparse
import difflib
import re
from pathlib import Path
from typing import Iterable, List


UDP_PROPERTY_NAMES = {
    "BasePort",
    "Port",
    "ClientAddr",
    "ClientPort",
    "SendWaypoints",
    "WaypointPort",
}

DEFAULT_SCRIPT = "DriverScript/pythondriver/examples/scenario_drive_embedded.py"
DEFAULT_CLASS = "EmbeddedController"
DEFAULT_HOME = ""


def _ensure_property_block(text: str) -> str:
    if 'name="PythonScript"' in text:
        return text

    # Insert before closing </Properties>.
    inject = (
        f'            <Property name="PythonScript" value="{DEFAULT_SCRIPT}" />\n'
        f'            <Property name="PythonClass" value="{DEFAULT_CLASS}" />\n'
        f'            <Property name="PythonHome" value="{DEFAULT_HOME}" />\n'
    )
    return re.sub(r"(</Properties>)", inject + r"\1", text, count=1)


def migrate_text(text: str) -> str:
    migrated = text

    migrated = migrated.replace('value="RealDriverController"', 'value="PythonDriverController"')
    migrated = migrated.replace('name="RealDriverController"', 'name="PythonDriverController"')

    # Remove UDP properties.
    for name in UDP_PROPERTY_NAMES:
        migrated = re.sub(
            rf"^[ \t]*<Property name=\"{re.escape(name)}\" value=\"[^\"]*\"\s*/>\s*\n?",
            "",
            migrated,
            flags=re.MULTILINE,
        )

    # Ensure Python properties exist in each Properties block.
    blocks = re.findall(r"<Properties>[\s\S]*?</Properties>", migrated)
    for block in blocks:
        if 'value="PythonDriverController"' not in block and 'name="PythonScript"' in block:
            continue
        updated = _ensure_property_block(block)
        migrated = migrated.replace(block, updated, 1)

    return migrated


def resolve_inputs(inputs: Iterable[str]) -> List[Path]:
    files: List[Path] = []
    for item in inputs:
        p = Path(item)
        if p.is_file():
            files.append(p)
            continue
        files.extend(sorted(Path().glob(item)))
    return sorted(set(files))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("inputs", nargs="+", help="Input xosc files or glob patterns")
    parser.add_argument("--output-dir", default="", help="Output directory (default: in-place)")
    parser.add_argument("--patch-out", default="", help="Optional unified diff output path")
    parser.add_argument("--suffix", default="_pythondriver", help="Suffix when output-dir is not set")
    args = parser.parse_args()

    files = resolve_inputs(args.inputs)
    if not files:
        raise SystemExit("No input files matched")

    out_dir = Path(args.output_dir) if args.output_dir else None
    diffs: List[str] = []
    migrated_count = 0

    for src in files:
        original = src.read_text(encoding="utf-8")
        migrated = migrate_text(original)
        if migrated == original:
            continue

        if out_dir:
            out_dir.mkdir(parents=True, exist_ok=True)
            dst = out_dir / src.name
        else:
            dst = src.with_name(f"{src.stem}{args.suffix}{src.suffix}")

        dst.write_text(migrated, encoding="utf-8")
        migrated_count += 1

        diff = difflib.unified_diff(
            original.splitlines(),
            migrated.splitlines(),
            fromfile=str(src),
            tofile=str(dst),
            lineterm="",
        )
        diffs.extend(list(diff))

    if args.patch_out:
        Path(args.patch_out).write_text("\n".join(diffs) + ("\n" if diffs else ""), encoding="utf-8")

    print(f"migrated_files={migrated_count}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
