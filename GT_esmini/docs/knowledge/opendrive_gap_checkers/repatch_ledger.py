"""Second-pass ledger patcher: the ledger is ALREADY triaged (statuses are
implemented_gt / gap_geometry_math / gap_ambiguous, not bare 'gap'). This re-patches
specific rules whose status/reason changed after the gap_ambiguous re-examination:
 - 11 rules gap_ambiguous -> implemented_gt (deterministic sub-case now implemented)
 - 15 rules stay gap_ambiguous but get an evidence-grounded status_reason from the recheck
Recomputes meta counts, asserts accounting still closed. Idempotent per run (matches any
current status value for the listed rules).

Usage: python repatch_ledger.py changes.json
changes.json: {rule_name: {status, reason, function|null}}
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
CHECKER_DIR = "GT_esmini/docs/knowledge/opendrive_gap_checkers"

VALID = {"implemented_gt", "gap_geometry_math", "gap_niche", "gap_ambiguous", "gap_deferred"}


def yq(s):
    return s.replace("\\", "\\\\").replace('"', '\\"')


def main():
    changes = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
    bad = {v["status"] for v in changes.values()} - VALID
    if bad:
        raise SystemExit(f"invalid statuses: {bad}")

    text = LEDGER.read_text(encoding="utf-8")
    blocks = re.split(r"(?=^  - uid: )", text, flags=re.M)
    header, entries = blocks[0], blocks[1:]

    group_of = {}  # rule_name -> group (for checker path); infer from final_status
    fs = json.loads((ROOT / CHECKER_DIR / "final_status.json").read_text(encoding="utf-8"))
    for rn, info in fs.items():
        group_of[rn] = info.get("group", "")

    touched = set()
    out = []
    for entry in entries:
        m = re.search(r'^\s*rule_name:\s*"((?:[^"\\]|\\.)*)"', entry, re.M)
        if not m:
            out.append(entry)
            continue
        rname = m.group(1).replace('\\"', '"')
        if rname not in changes:
            out.append(entry)
            continue
        ch = changes[rname]
        status = ch["status"]
        reason = ch.get("reason", "")
        func = ch.get("function")
        indent = "    "
        # replace the status line
        entry = re.sub(r"^\s*status:\s*\S+\s*$", f"{indent}status: {status}", entry, count=1, flags=re.M)
        # replace or insert status_reason line (right after status)
        if re.search(r"^\s*status_reason:", entry, flags=re.M):
            entry = re.sub(r'^\s*status_reason:\s*".*?"\s*$',
                           f'{indent}status_reason: "{yq(reason)}"', entry, count=1, flags=re.M)
        else:
            entry = re.sub(r"(^\s*status: .*$)",
                           r"\1" + f'\n{indent}status_reason: "{yq(reason)}"', entry, count=1, flags=re.M)
        # checker line: present only for implemented_gt
        entry = re.sub(r'^\s*checker:\s*".*?"\s*\n', "", entry, flags=re.M)  # drop existing
        if status == "implemented_gt" and func:
            g = group_of.get(rname, "")
            entry = re.sub(r"(^\s*status_reason: .*$)",
                           r"\1" + f'\n{indent}checker: "{CHECKER_DIR}/check_{g}.py::{func}"',
                           entry, count=1, flags=re.M)
        out.append(entry)
        touched.add(rname)

    missing = set(changes) - touched
    if missing:
        raise SystemExit(f"rules not found in ledger: {sorted(missing)}")

    body = "".join(out)
    d = yaml.safe_load(header + body)
    rules = d["rules"]
    sc = Counter(r["status"] for r in rules)
    total = len(rules)
    gap259 = (sc.get("implemented_gt", 0) + sc.get("gap_geometry_math", 0) + sc.get("gap_niche", 0)
              + sc.get("gap_ambiguous", 0) + sc.get("gap_deferred", 0) + sc.get("gap", 0))

    meta_lines = [
        "meta:",
        f"  denominator_total: {total}",
        f"  qc_implemented: {sc.get('implemented_qc', 0)}",
        f"  gt_implemented: {sc.get('implemented_gt', 0)}",
        f'  coverage_pct: {(sc.get("implemented_qc", 0) + sc.get("implemented_gt", 0)) / total * 100:.1f}',
        f"  gap_geometry_math: {sc.get('gap_geometry_math', 0)}",
        f"  gap_niche: {sc.get('gap_niche', 0)}",
        f"  gap_ambiguous: {sc.get('gap_ambiguous', 0)}",
        f"  gap_deferred: {sc.get('gap_deferred', 0)}",
        f"  gap_untriaged: {sc.get('gap', 0)}  # must be 0 when alpha is complete",
        "  qc_uids_total: 23",
        "  qc_uids_not_in_annexf: 7  # xml/basic系や版・命名差",
    ]
    new_header = re.sub(r"^meta:\n(?:  .*\n)+", "\n".join(meta_lines) + "\n", header, flags=re.M)
    patched = new_header + body
    yaml.safe_load(patched)  # validate
    LEDGER.write_text(patched, encoding="utf-8")

    print(f"repatched {len(touched)} rules")
    print("status counts:", dict(sc))
    print(f"gap 259 accounting = {gap259} (expect 259);  untriaged(gap) = {sc.get('gap',0)}")
    assert gap259 == 259 and sc.get("gap", 0) == 0, "ACCOUNTING NOT CLOSED"
    print("accounting closed OK")


if __name__ == "__main__":
    main()
