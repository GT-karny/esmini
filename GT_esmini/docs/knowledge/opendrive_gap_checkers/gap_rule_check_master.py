"""GT OpenDRIVE Annex F gap-rule checker suite — master runner.

Integrates the 19 per-category checker modules (check_*.py, each exposing
run_checks(file_path, root, roads, road_ids, junctions, junction_ids) -> list[(rule, detail, location)])
into one sweep over the local .xodr corpus (208 files; thirdparty/dist/build excluded).
Origin-buckets results (official ASAM calibration set vs GT-authored audit target vs
upstream) and emits a coverage + GT-violations report.

Run: e:/Repository/GT_esmini/esmini/DriverScript/.venv/Scripts/python.exe gap_rule_check_master.py
"""

import sys
import glob
import importlib.util
import xml.etree.ElementTree as ET
from pathlib import Path
from collections import Counter, defaultdict

sys.stdout.reconfigure(encoding="utf-8")

ROOT = Path(r"e:/Repository/GT_esmini/esmini")
SP = Path(__file__).resolve().parent
OUT = ROOT / "GT_esmini/docs/knowledge/opendrive_gap_rule_report.md"

# ---- load all category modules ----
MODULES = {}
for f in sorted(SP.glob("check_*.py")):
    name = f.stem
    spec = importlib.util.spec_from_file_location(name, f)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    if hasattr(mod, "run_checks"):
        MODULES[name] = mod.run_checks

# ---- corpus ----
files = []
for f in glob.glob(str(ROOT / "**/*.xodr"), recursive=True):
    rp = str(Path(f).relative_to(ROOT)).replace("\\", "/")
    if (
        rp.startswith(("thirdparty/", "dist/", "build/", "scratchpad/"))
        or "/build/" in rp
    ):
        continue
    if "/scratchpad/" in rp or "/adv/" in rp or "/checks_gap/" in rp:
        continue  # exclude our own adversarial audit fixtures (deliberately-broken xodr)
    files.append(f)
files = sorted(set(files))


def rel(f):
    return str(Path(f).relative_to(ROOT)).replace("\\", "/")


def bucket(f):
    rp = rel(f)
    if "test/odr_fixtures/official/" in rp:
        return "official(ASAM)"
    if "test/odr_fixtures/handauthored/" in rp:
        return "GT:handauthored"
    if rp.startswith("resources/xodr/"):
        return "GT:resources/xodr"
    if "scenario_authoring" in rp or "/generated/" in rp:
        return "GT:generated"
    if "GT_esmini/test/" in rp:
        return "GT:test"
    if rp.startswith("EnvironmentSimulator/") or "OSMP" in rp:
        return "upstream(対象外)"
    return "other"


flags = []  # (file, group, rule, detail, location)
parse_err = 0
run_errs = []  # (file, group, exc)
for f in files:
    try:
        root = ET.parse(f).getroot()
    except Exception:
        parse_err += 1
        continue
    roads = {r.get("id"): r for r in root.iter("road")}
    road_ids = set(roads)
    junctions = {j.get("id"): j for j in root.iter("junction")}
    junction_ids = set(junctions)
    for gname, fn in MODULES.items():
        try:
            res = fn(f, root, roads, road_ids, junctions, junction_ids)
        except Exception as e:
            run_errs.append((rel(f), gname, f"{type(e).__name__}: {e}"))
            continue
        for item in res or []:
            rule, detail, loc = (list(item) + ["", "", ""])[:3]
            flags.append((f, gname, rule, detail, loc))

# ---- aggregation ----
bkt_files = Counter(bucket(f) for f in files)
bkt_flag = Counter(bucket(fl[0]) for fl in flags)
byrule = Counter(fl[2] for fl in flags)
bygroup = Counter(fl[1] for fl in flags)
GT = [fl for fl in flags if bucket(fl[0]).startswith("GT:")]
OFFICIAL = [fl for fl in flags if bucket(fl[0]) == "official(ASAM)"]
ORDER = [
    "GT:resources/xodr",
    "GT:test",
    "GT:handauthored",
    "GT:generated",
    "official(ASAM)",
    "upstream(対象外)",
    "other",
]
buckets = [b for b in ORDER if b in bkt_files] + [
    b for b in bkt_files if b not in ORDER
]

# official-set rules that fired (calibration alarm — should be near-zero / genuine)
off_by_rule = Counter(fl[2] for fl in OFFICIAL)

lines = [
    "# OpenDRIVE gap-rule 監査 — Annex F 未実装ルールの自作 checker suite（拡張版）",
    "",
    "> qc-opendrive が実装しない Annex F 規範ルール（gap 259）を XML 直パースで自作 checker 化。",
    "> 19 カテゴリモジュール（`scratchpad/checks_gap/check_*.py`）を統合し 208 ファイルへ一括適用。",
    "> 公式ASAM=校正セット（発火≒0が期待・発火は要精査） / GT自作=**監査対象** / 上流=対象外。",
    "> flag は error でなく **review**（助言）。",
    "",
    f"- モジュール数 **{len(MODULES)}**  検査 **{len(files)}** files（parse失敗 {parse_err}／実行時例外 {len(run_errs)}）",
    f"- 違反総数 **{len(flags)}**  ├ GT自作 **{len(GT)}**  └ 公式ASAM **{len(OFFICIAL)}**",
    "",
    "## origin別（files / 違反数）",
    "",
    "| origin | files | 違反 |",
    "| :-- | --: | --: |",
]
for b in buckets:
    lines.append(f"| {b} | {bkt_files[b]} | {bkt_flag[b]} |")

lines += [
    "",
    "## カテゴリモジュール別 違反数",
    "",
    "| module | 件数 |",
    "| :-- | --: |",
]
for g, n in bygroup.most_common():
    lines.append(f"| {g} | {n} |")

lines += ["", "## ルール別 違反数（上位40）", "", "| rule | 件数 |", "| :-- | --: |"]
for rk, n in byrule.most_common(40):
    lines.append(f"| {rk} | {n} |")

if run_errs:
    lines += ["", f"## ⚠ 実行時例外 {len(run_errs)} 件（モジュール要修正）", ""]
    for fr, g, e in run_errs[:50]:
        lines.append(f"- **{g}** {e} — {fr}")

lines += [
    "",
    f"## ⚠ 公式ASAM校正セットで発火したルール（{len(OFFICIAL)}件）＝要精査",
    "",
]
if off_by_rule:
    lines += ["| rule | 件数 |", "| :-- | --: |"]
    for rk, n in off_by_rule.most_common():
        lines.append(f"| {rk} | {n} |")
else:
    lines.append("（なし＝校正クリーン）")

lines += ["", f"## ★ GT自作資産の違反（監査対象・全 {len(GT)}件、先頭150）", ""]
for f, g, rk, d, loc in GT[:150]:
    lines.append(f"- **{rk}** {d} — {rel(f)} :: {loc}  [{bucket(f)}]")

OUT.write_text("\n".join(lines) + "\n", encoding="utf-8")

# ---- console summary ----
print(
    f"modules: {len(MODULES)}  files: {len(files)}  parse_err: {parse_err}  run_errs: {len(run_errs)}"
)
print(f"violations: {len(flags)}  GT-authored: {len(GT)}  official: {len(OFFICIAL)}")
print("by origin (files / violations):")
for b in buckets:
    print(f"    {b:22s} {bkt_files[b]:5d} / {bkt_flag[b]}")
if run_errs:
    print(f"RUN ERRORS ({len(run_errs)}):")
    for fr, g, e in run_errs[:20]:
        print(f"    {g}: {e}  [{fr}]")
if off_by_rule:
    print(f"OFFICIAL-SET FLAGS ({len(OFFICIAL)}) — investigate:")
    for rk, n in off_by_rule.most_common(20):
        print(f"    {rk:50s} {n}")
print(f"[out] {OUT}")
