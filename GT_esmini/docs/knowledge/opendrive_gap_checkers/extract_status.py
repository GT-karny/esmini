"""Parse the implement/verify workflow journal.jsonl, extract each agent's structured
result (group + rules[] with status), and build an authoritative rule_name -> {status,
reason, group} mapping. Prefer the VERIFY result over the IMPLEMENT result for a group
(verify is the later, audited word). Emit final_status.json + a coverage summary.
Reports any group/rule gaps so we know exactly what still needs hand-work."""
import json
import sys
from pathlib import Path
from collections import defaultdict, Counter

sys.stdout.reconfigure(encoding="utf-8")

SP = Path(r"C:/Users/Owner/AppData/Local/Temp/claude/e--Repository-GT-esmini-esmini/41686c41-ce61-4296-9913-8755428933ec/scratchpad/checks_gap")
JOURNAL = Path(r"C:/Users/Owner/.claude/projects/e--Repository-GT-esmini-esmini/41686c41-ce61-4296-9913-8755463ec".replace("463ec", "8933ec")) / "subagents/workflows/wf_ae2895a2-020/journal.jsonl"
# fix path robustly:
JOURNAL = Path(r"C:/Users/Owner/.claude/projects/e--Repository-GT-esmini-esmini/41686c41-ce61-4296-9913-8755428933ec/subagents/workflows/wf_ae2895a2-020/journal.jsonl")

VALID = {"implemented_gt", "gap_geometry_math", "gap_niche", "gap_ambiguous", "gap_deferred"}

# collect results: each result line has a 'value' (the agent's returned object) and a label
implement_by_group = {}
verify_by_group = {}

def walk(obj):
    """Yield dicts that look like an agent structured result (have group + rules)."""
    if isinstance(obj, dict):
        if "group" in obj and "rules" in obj and isinstance(obj["rules"], list):
            yield obj
        for v in obj.values():
            yield from walk(v)
    elif isinstance(obj, list):
        for v in obj:
            yield from walk(v)

n_result = 0
for line in JOURNAL.read_text(encoding="utf-8").splitlines():
    try:
        d = json.loads(line)
    except Exception:
        continue
    if d.get("type") != "result":
        continue
    n_result += 1
    # the returned value may be under various keys
    val = d.get("value", d.get("result", d))
    if isinstance(val, str):
        try:
            val = json.loads(val)
        except Exception:
            pass
    found = list(walk(val))
    if not found:
        continue
    rec = found[0]
    g = rec.get("group")
    # distinguish by schema shape: verify has crash_free/changes_made; implement has self_test_summary
    is_verify = ("crash_free" in rec) or ("changes_made" in rec)
    if is_verify:
        verify_by_group[g] = rec
    else:
        implement_by_group[g] = rec

print(f"journal result lines: {n_result}")
print(f"verify groups: {len(verify_by_group)}  implement groups: {len(implement_by_group)}")

# Load full rule lists per group (ground truth of what must be classified)
group_rules = {}
for jf in SP.glob("rules_*.json"):
    g = jf.stem[len("rules_"):]
    group_rules[g] = [r["rule_name"] for r in json.loads(jf.read_text(encoding="utf-8"))]

final = {}
missing_report = []
for g, rule_names in group_rules.items():
    src = verify_by_group.get(g) or implement_by_group.get(g)
    src_kind = "verify" if g in verify_by_group else ("implement" if g in implement_by_group else "NONE")
    by_name = {}
    if src:
        for r in src.get("rules", []):
            by_name[r["rule_name"]] = r
    for rn in rule_names:
        r = by_name.get(rn)
        if r and r.get("status") in VALID:
            final[rn] = {
                "status": r["status"],
                "reason": r.get("reason", ""),
                "group": g,
                "function": r.get("function") or ("run_checks" if r["status"] == "implemented_gt" else None),
                "source": src_kind,
            }
        else:
            missing_report.append((g, rn, src_kind))

print(f"\nresolved rules: {len(final)} / 259")
print("status distribution:", dict(Counter(v["status"] for v in final.values())))
print("source distribution:", dict(Counter(v["source"] for v in final.values())))

if missing_report:
    print(f"\nUNRESOLVED ({len(missing_report)}) — need hand-work:")
    bg = defaultdict(list)
    for g, rn, sk in missing_report:
        bg[g].append(rn)
    for g in sorted(bg):
        print(f"  [{g}] src={dict(Counter(sk for gg,rr,sk in missing_report if gg==g))} ({len(bg[g])} rules):")
        for rn in bg[g]:
            print(f"      - {rn}")

(SP / "final_status.json").write_text(json.dumps(final, ensure_ascii=False, indent=2), encoding="utf-8")
print(f"\n[out] {SP / 'final_status.json'}")
