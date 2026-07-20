"""Patch GT_esmini/docs/knowledge/opendrive_rule_ledger.yaml in place: for every rule_name
present in final_status.json, replace its `status: gap` line with the final triaged status
(implemented_gt / gap_geometry_math / gap_niche / gap_ambiguous / gap_deferred), insert a
status_reason line, and (for implemented_gt) a checker pointer line. Recomputes meta counts.
Validates the patched file still parses and the rule count/status accounting is closed
(every one of the 259 original gap rules ends up either implemented_gt or a gap_* reason
bucket - zero left at bare status: gap).

Usage: DriverScript/.venv/Scripts/python.exe update_ledger.py final_status.json
final_status.json: {rule_name: {status, reason, group, function}}
"""
import sys
import re
import json
import yaml
from pathlib import Path
from collections import Counter

sys.stdout.reconfigure(encoding="utf-8")

ROOT = Path(r"e:/Repository/GT_esmini/esmini")
LEDGER = ROOT / "GT_esmini/docs/knowledge/opendrive_rule_ledger.yaml"

VALID_STATUSES = {"implemented_gt", "gap_geometry_math", "gap_niche", "gap_ambiguous", "gap_deferred"}


def yq(s):
    return s.replace("\\", "\\\\").replace('"', '\\"')


def main():
    mapping_path = Path(sys.argv[1])
    mapping = json.loads(mapping_path.read_text(encoding="utf-8"))

    bad = {v["status"] for v in mapping.values()} - VALID_STATUSES
    if bad:
        raise SystemExit(f"invalid status values in mapping: {bad}")

    text = LEDGER.read_text(encoding="utf-8")
    blocks = re.split(r"(?=^  - uid: )", text, flags=re.M)
    header, entries = blocks[0], blocks[1:]

    touched = set()
    out_entries = []
    for entry in entries:
        m = re.search(r'^\s*rule_name:\s*"((?:[^"\\]|\\.)*)"', entry, re.M)
        if not m:
            out_entries.append(entry)
            continue
        rname = m.group(1).replace('\\"', '"')
        if rname not in mapping:
            out_entries.append(entry)
            continue
        info = mapping[rname]
        status = info["status"]
        reason = info.get("reason", "")
        group = info.get("group", "")
        func = info.get("function")

        sm = re.search(r"^(\s*)status:\s*gap\s*$", entry, re.M)
        if not sm:
            raise SystemExit(f"rule {rname}: expected 'status: gap' line not found (already patched? re-run from a clean checkout)")
        indent = sm.group(1)
        extra = f'{indent}status: {status}\n{indent}status_reason: "{yq(reason)}"\n'
        if status == "implemented_gt" and func:
            extra += f'{indent}checker: "GT_esmini/docs/knowledge/opendrive_gap_checkers/check_{group}.py::{func}"\n'
        entry = entry[: sm.start()] + extra + entry[sm.end() + 1:]
        out_entries.append(entry)
        touched.add(rname)

    missing = set(mapping) - touched
    if missing:
        raise SystemExit(f"rule_names in mapping not found in ledger (typo?): {sorted(missing)}")

    body = "".join(out_entries)

    # recompute meta block
    d = yaml.safe_load(header + body)
    rules = d["rules"]
    status_counts = Counter(r["status"] for r in rules)
    total = len(rules)
    still_gap = status_counts.get("gap", 0)

    meta_lines = [
        "meta:",
        f"  denominator_total: {total}",
        f"  qc_implemented: {status_counts.get('implemented_qc', 0)}",
        f"  gt_implemented: {status_counts.get('implemented_gt', 0)}",
        f'  coverage_pct: "{(status_counts.get("implemented_qc", 0) + status_counts.get("implemented_gt", 0)) / total * 100:.1f}"',
        f"  gap_geometry_math: {status_counts.get('gap_geometry_math', 0)}",
        f"  gap_niche: {status_counts.get('gap_niche', 0)}",
        f"  gap_ambiguous: {status_counts.get('gap_ambiguous', 0)}",
        f"  gap_deferred: {status_counts.get('gap_deferred', 0)}",
        f"  gap_untriaged: {still_gap}  # must be 0 when alpha is complete",
        "  qc_uids_total: 23",
        "  qc_uids_not_in_annexf: 7  # xml/basic系や版・命名差",
    ]
    new_header = re.sub(
        r"^meta:\n(?:  .*\n)+",
        "\n".join(meta_lines) + "\n",
        header,
        flags=re.M,
    )

    patched = new_header + body
    # validate
    d2 = yaml.safe_load(patched)
    assert len(d2["rules"]) == total, "rule count changed during patch!"

    LEDGER.write_text(patched, encoding="utf-8")
    print(f"patched {len(touched)} rules")
    print("status counts:", dict(status_counts))
    if still_gap:
        print(f"WARNING: {still_gap} rules still at bare 'gap' status (untriaged) — accounting NOT closed")
        for r in d2["rules"]:
            if r["status"] == "gap":
                print("  -", r["rule_name"])
    else:
        print("accounting closed: 0 rules left untriaged")


if __name__ == "__main__":
    main()
