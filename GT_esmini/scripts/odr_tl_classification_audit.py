#!/usr/bin/env python
"""odr_tl_classification_audit.py -- P3 TrafficLight-gate classification audit (plan P3, cluster 11).

Machine-generates the BEFORE/AFTER TrafficLight-classification diff of the P3 gate relaxation
over the whole xodr asset universe ("every xodr any gate loads"), WITHOUT loading any DLL:
the gate is a pure function of three signal attributes, so it is evaluated analytically.

    BEFORE (P1 state, GT_RoadManager.cpp:4894):
        TrafficLight  iff  lower(country)=="opendrive" AND country_revision<2013 AND dynamic
        where country_revision = explicit @countryRevision if present else 0  ([GT_ODR:country-rev])
    AFTER (P3 [GT_ODR:tl-gate]):
        TrafficLight  iff  dynamic
    dynamic = (@dynamic == "yes")   (parser: "no"/unknown/absent -> false)

Cross-validation: the analytic BEFORE (and, post-patch, AFTER) model is checked against the
C++ probe golden GT_esmini/test/odr_fixtures/golden/trafficlight_classification.json
(test_OdrAssetProbe, dynamic_cast<TrafficLight> ground truth) with --check-golden=before|after.
A mismatch means the analytic model is wrong and the audit cannot be trusted -> exit 1.

The committed golden universe (test_OdrAssetProbe kUniverseDirs) deliberately EXCLUDES the
gitignored ASAM official/ tree; this audit ADDS it (flagged origin=official) so the reviewed
diff covers the full "loaded by any gate" universe, including the conformance RM/OSI layers.

Outputs (default --out GT_esmini/test/odr_fixtures/reports/tl_gate_audit):
    <out>.json  -- full per-signal table (deterministic, sorted)
    <out>.md    -- reviewable flip table (one row per classification flip)

Usage:
    DriverScript/.venv/Scripts/python.exe GT_esmini/scripts/odr_tl_classification_audit.py \
        [--check-golden before|after] [--out <prefix>] [--quiet]

Exit 0 iff (no parse errors) and (golden cross-check, if requested, matches).
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import xml.etree.ElementTree as ET

_THIS_DIR = os.path.dirname(os.path.abspath(__file__))
_REPO_ROOT = os.path.dirname(os.path.dirname(_THIS_DIR))

# Same dirs as test_OdrAssetProbe kUniverseDirs (committed golden universe) ...
GOLDEN_UNIVERSE_DIRS = [
    "resources/xodr",
    "resources/scenario_authoring/road_catalog/generated",
    "DriverScript/resources/xodr",
    "EnvironmentSimulator/Unittest/xodr",
    "GT_esmini/test/odr_fixtures/handauthored",
    "GT_esmini/test/odr_fixtures/generated",
]
# ... plus the local-only ASAM official tree (flagged; NOT part of the committed golden).
OFFICIAL_DIR = "GT_esmini/test/odr_fixtures/official"

GOLDEN_JSON = os.path.join(
    _REPO_ROOT,
    "GT_esmini",
    "test",
    "odr_fixtures",
    "golden",
    "trafficlight_classification.json",
)
DEFAULT_OUT = os.path.join(
    _REPO_ROOT, "GT_esmini", "test", "odr_fixtures", "reports", "tl_gate_audit"
)


def atoi(s: str) -> int:
    """C atoi: parse an optionally-signed decimal prefix; garbage -> 0."""
    s = (s or "").strip()
    i, sign = 0, 1
    if i < len(s) and s[i] in "+-":
        sign = -1 if s[i] == "-" else 1
        i += 1
    n = 0
    got = False
    while i < len(s) and s[i].isdigit():
        n = n * 10 + int(s[i])
        i += 1
        got = True
    return sign * n if got else 0


def classify_before(
    country: str, country_revision_attr: str | None, dynamic: bool
) -> str:
    rev = 0
    if country_revision_attr is not None and country_revision_attr != "":
        # pugixml as_uint: non-numeric -> 0; numeric prefix parsed.
        rev = max(atoi(country_revision_attr), 0)
    gate = (country.lower() == "opendrive") and (rev < 2013) and dynamic
    return "TrafficLight" if gate else "Signal"


def classify_after(dynamic: bool) -> str:
    return "TrafficLight" if dynamic else "Signal"


def collect_files():
    """[(relpath, abspath, origin)] sorted by relpath; origin in {golden_universe, official}."""
    files = []
    for rel_dir in GOLDEN_UNIVERSE_DIRS:
        d = os.path.join(_REPO_ROOT, rel_dir)
        if not os.path.isdir(d):
            continue
        for name in os.listdir(d):
            if name.endswith(".xodr"):
                p = os.path.join(d, name)
                files.append(
                    (
                        os.path.relpath(p, _REPO_ROOT).replace("\\", "/"),
                        p,
                        "golden_universe",
                    )
                )
    od = os.path.join(_REPO_ROOT, OFFICIAL_DIR)
    for root, _dirs, names in os.walk(od):
        for name in names:
            if name.endswith(".xodr"):
                p = os.path.join(root, name)
                files.append(
                    (os.path.relpath(p, _REPO_ROOT).replace("\\", "/"), p, "official")
                )
    files.sort(key=lambda x: x[0])
    return files


def audit_file(abspath: str):
    """Return (rows, err). rows = per-<signal> dicts in document order."""
    try:
        tree = ET.parse(abspath)
    except ET.ParseError as e:
        return [], f"XML parse error: {e}"
    rows = []
    root = tree.getroot()
    for road in root.iter("road"):
        road_id = road.get("id", "")
        signals = road.find("signals")
        if signals is None:
            continue
        for sig in signals:
            if sig.tag != "signal":
                continue  # signalReference etc. -- clones copy the referenced class, never flip
            country = sig.get("country", "")
            rev_attr = sig.get("countryRevision")
            dynamic = sig.get("dynamic", "") == "yes"
            before = classify_before(country, rev_attr, dynamic)
            after = classify_after(dynamic)
            rows.append(
                {
                    "road": road_id,
                    "id": atoi(sig.get("id", "")),
                    "name": sig.get("name", ""),
                    "type": sig.get("type", ""),
                    "dynamic": sig.get("dynamic", ""),
                    "country": country,
                    "countryRevision": rev_attr if rev_attr is not None else "(absent)",
                    "before": before,
                    "after": after,
                    "flip": before != after,
                }
            )
    return rows, None


def probe_map(rows, key_field):
    """Reproduce test_OdrAssetProbe's map semantics: {\"<id>:<name>\": kind}, last write wins."""
    m = {}
    for r in rows:
        m[f"{r['id']}:{r['name']}"] = r[key_field]
    return m


def flip_rationale(r) -> str:
    """One-line per-row rationale for the reviewed diff (why the class changes / stays)."""
    if not r["flip"]:
        return ""
    return (
        f"dynamic=yes but BEFORE-gate false: country='{r['country']}' "
        f"(lower {'==' if r['country'].lower() == 'opendrive' else '!='} 'opendrive'), "
        f"countryRevision={r['countryRevision']}"
        " -> P3 gate keys on @dynamic only, so this dynamic signal is promoted to TrafficLight"
        " (= OSI traffic_light; consumers: TrafficSignalController, GT_OSIReporter_Traffic,"
        " VD TrafficLightAware -- see plan P3)."
    )


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(
        description="P3 TrafficLight gate classification audit (analytic)."
    )
    ap.add_argument(
        "--check-golden",
        choices=["before", "after"],
        default=None,
        help="cross-validate the analytic model against the committed C++ probe golden",
    )
    ap.add_argument(
        "--out", default=DEFAULT_OUT, help="output prefix (.json/.md appended)"
    )
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args(argv)

    files = collect_files()
    if not files:
        print("ERROR: no xodr files found (wrong repo root?)", file=sys.stderr)
        return 1

    table = {}
    parse_errors = []
    for rel, ap_, origin in files:
        rows, err = audit_file(ap_)
        if err:
            parse_errors.append((rel, err))
            continue
        table[rel] = {"origin": origin, "signals": rows}

    # ---- golden cross-check (analytic model vs C++ dynamic_cast probe) ----
    golden_mismatches = []
    if args.check_golden:
        with open(GOLDEN_JSON, "r", encoding="utf-8") as fh:
            golden = json.load(fh)
        for rel, grec in golden.items():
            if not grec.get("load", False):
                continue  # load-failed files carry no classification
            ours = table.get(rel)
            if ours is None:
                golden_mismatches.append(
                    f"{rel}: in golden but not in analytic universe"
                )
                continue
            want = grec.get("signals", {})
            got = probe_map(ours["signals"], args.check_golden)
            if want != got:
                only_w = {k: v for k, v in want.items() if got.get(k) != v}
                only_g = {k: v for k, v in got.items() if want.get(k) != v}
                golden_mismatches.append(f"{rel}: golden={only_w} analytic={only_g}")
        for rel, rec in table.items():
            if (
                rec["origin"] == "golden_universe"
                and rel not in golden
                and rec["signals"]
            ):
                golden_mismatches.append(
                    f"{rel}: in analytic universe but not in golden"
                )

    # ---- outputs ----
    flips = [
        (rel, r)
        for rel, rec in sorted(table.items())
        for r in rec["signals"]
        if r["flip"]
    ]
    n_signals = sum(len(rec["signals"]) for rec in table.values())
    n_files = len(table)

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out + ".json", "w", encoding="utf-8", newline="\n") as fh:
        json.dump(
            {
                "files": table,
                "summary": {
                    "files": n_files,
                    "signals": n_signals,
                    "flips": len(flips),
                },
                "parse_errors": [f"{r}: {e}" for r, e in parse_errors],
            },
            fh,
            sort_keys=True,
            indent=1,
        )
        fh.write("\n")

    md = [
        "# P3 TrafficLight gate relaxation -- classification diff (machine-generated)",
        "",
        "Gate BEFORE: `lower(country)=='opendrive' && countryRevision<2013 && dynamic` "
        "(countryRevision absent => 0, P1 legacy-preserving read).",
        "Gate AFTER (`[GT_ODR:tl-gate]`): `dynamic`.",
        "",
        f"Universe: {n_files} xodr files / {n_signals} signals "
        f"(committed probe universe + local ASAM official tree).",
        f"**Flips: {len(flips)}** (every other signal keeps its classification).",
        "",
    ]
    if flips:
        md += [
            "| file | road | signal id | name | type | dynamic | country | countryRevision | before -> after |",
            "|---|---|---|---|---|---|---|---|---|",
        ]
        for rel, r in flips:
            md.append(
                f"| {rel} | {r['road']} | {r['id']} | {r['name']} | {r['type']} | {r['dynamic']} "
                f"| {r['country']} | {r['countryRevision']} | {r['before']} -> {r['after']} |"
            )
        md.append("")
        md.append("## Per-flip rationale")
        md.append("")
        for rel, r in flips:
            md.append(
                f"- **{rel}** road {r['road']} signal {r['id']} `{r['name']}`: {flip_rationale(r)}"
            )
        md.append("")
    if parse_errors:
        md += ["## Parse errors (excluded from audit)", ""]
        md += [f"- {r}: {e}" for r, e in parse_errors] + [""]
    with open(args.out + ".md", "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(md) + "\n")

    if not args.quiet:
        print(f"universe: {n_files} files, {n_signals} signals; flips: {len(flips)}")
        for rel, r in flips:
            print(
                f"  FLIP {rel} road={r['road']} id={r['id']} name={r['name']} "
                f"country={r['country']} rev={r['countryRevision']} {r['before']}->{r['after']}"
            )
        for rel, e in parse_errors:
            print(f"  PARSE-ERROR {rel}: {e}", file=sys.stderr)
        if args.check_golden:
            print(
                f"golden cross-check ({args.check_golden}): "
                + (
                    "OK"
                    if not golden_mismatches
                    else f"{len(golden_mismatches)} MISMATCHES"
                )
            )
            for m in golden_mismatches[:20]:
                print("  MISMATCH " + m, file=sys.stderr)
        print(f"wrote {os.path.relpath(args.out + '.json', _REPO_ROOT)} / .md")

    if parse_errors or golden_mismatches:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
