#!/usr/bin/env python3
"""検知器の向きの実証（spine-work:derived-report-lint）。

意図的な違反データを作り、各検査が **実際に発火する** ことを確かめる。
KG の実データは一切触らない（fixture のコピーに対して実行する）。
2026-07-20 に「規約と反転した検知器が警報疲れを育てた」実例があるため、
検査を足したら必ずこの実証を通す。
"""
import copy
import shutil
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_knowledge_graph as kg  # noqa: E402

import yaml  # noqa: E402

FAILED = []


def expect(name, hit, want=True):
    ok = bool(hit) == want
    print(f"  [{'OK ' if ok else 'MISS'}] {name}")
    if not ok:
        FAILED.append(name)


def load(p):
    return yaml.safe_load(Path(p).read_text(encoding="utf-8"))


NS = load(kg.NAMESPACES_YAML)
GRAPH = load(kg.GRAPH_YAML)
NAMESPACES = {n["slug"]: n for n in NS["namespaces"] if n.get("slug")}
CATALOGS = kg.load_catalogs()


def run_values(catalogs, namespaces):
    errs = []
    kg.check_catalog_values(catalogs, namespaces, errs.append)
    return errs


print("== 1. 値域チェック（hard）==")
# 基準: 無改変で 0件
expect("baseline clean", run_values(CATALOGS, NAMESPACES), want=False)

bad = copy.deepcopy(CATALOGS)
bad["signal"][0]["exposure"] = ["osi", "telemetry"]  # 語彙外
expect("exposure 語彙外を検出", [e for e in run_values(bad, NAMESPACES) if "telemetry" in e])

bad = copy.deepcopy(CATALOGS)
bad["signal"][0]["state"] = "(c)"
expect("state 語彙外を検出", [e for e in run_values(bad, NAMESPACES) if "(c)" in e])

bad = copy.deepcopy(CATALOGS)
bad["signal"][0]["state"] = "（a）"  # 全角括弧の表記ゆれ
expect("state 全角表記ゆれを検出", [e for e in run_values(bad, NAMESPACES) if "未知の state" in e])

bad = copy.deepcopy(CATALOGS)
bad["gate"][0]["covers"] = "  "
expect("covers 空を検出", [e for e in run_values(bad, NAMESPACES) if "covers" in e])

bad = copy.deepcopy(CATALOGS)
bad["gate"][0]["blocking"] = "true"  # 文字列は不可
expect("blocking 非boolを検出", [e for e in run_values(bad, NAMESPACES) if "blocking" in e])

ns_bad = copy.deepcopy(NAMESPACES)
ns_bad["signal"]["face"] = "4"
expect("face 語彙外を検出", [e for e in run_values(CATALOGS, ns_bad) if "未知の face" in e])

ns_bad = copy.deepcopy(NAMESPACES)
del ns_bad["gate"]["face"]
expect("face 欠落を検出", [e for e in run_values(CATALOGS, ns_bad) if "face タグがありません" in e])

print("== 2. 規約2（恒久資産のファイル名の工程序数, hard）==")
with tempfile.TemporaryDirectory() as td:
    root = Path(td)
    (root / "GT_esmini" / "docs" / "knowledge").mkdir(parents=True)
    clean = root / "GT_esmini" / "docs" / "knowledge" / "signal_catalog.yaml"
    clean.write_text("x", encoding="utf-8")
    orig_root = kg.REPO_ROOT
    kg.REPO_ROOT = root
    try:
        errs = []
        kg.check_asset_naming(errs.append)
        expect("clean な資産名では発火しない", errs, want=False)
        (root / "GT_esmini" / "docs" / "knowledge" / "phase3_batch.yaml").write_text(
            "x", encoding="utf-8")
        errs = []
        kg.check_asset_naming(errs.append)
        expect("phase3_batch.yaml を検出", [e for e in errs if "phase3_batch" in e])
    finally:
        kg.REPO_ROOT = orig_root

print("== 3. 派生レポート / coupling-audit ==")
base = kg.spine_report(CATALOGS, NAMESPACES, GRAPH["edges"])
expect("面3→面2 直結辺: 実データでは 0件", base["coupling_direct_edge"], want=False)
expect("整合破れ: 実データでは 0件", base["obs_verdict_on_unemitted"], want=False)

# 面3(matcher) -> 面2(policy) の直結辺を注入
edges = copy.deepcopy(GRAPH["edges"])
edges.append({"from": "matcher:lane_keep", "to": "policy:lead_vehicle",
              "type": "observes", "source": "CLAUDE.md"})
inj = kg.spine_report(CATALOGS, NAMESPACES, edges)
expect("注入した面3→面2 直結辺を検出", inj["coupling_direct_edge"])

# 未emit(a) の signal を観測する辺を注入
unemitted = next(s["id"] for s in CATALOGS["signal"] if s.get("state") == "(a)")
edges = copy.deepcopy(GRAPH["edges"])
edges.append({"from": "matcher:speed_above", "to": f"signal:{unemitted}",
              "type": "observes", "source": "CLAUDE.md"})
inj = kg.spine_report(CATALOGS, NAMESPACES, edges)
expect("未emit signal を観測する辺を検出", inj["obs_verdict_on_unemitted"])

# observes 辺を全部消すと未観測 matcher が増える（母数がグラフ非依存であることの確認）
edges = [e for e in copy.deepcopy(GRAPH["edges"]) if e.get("type") != "observes"]
inj = kg.spine_report(CATALOGS, NAMESPACES, edges)
expect(f"observes 全削除で未観測 matcher が全数になる "
       f"({len(inj['coupling_unobserved'])}件)",
       len(inj["coupling_unobserved"]) > len(base["coupling_unobserved"]))

# (b) の signal に観測辺を足すと「未配線」から消える＝(a)/(b) の打ち手の区別が効く
wired_b = next(s["id"] for s in CATALOGS["signal"]
               if s.get("state") == "(b)"
               and any(s["id"] in x for x in base["obs_unwired"]))
edges = copy.deepcopy(GRAPH["edges"])
edges.append({"from": "matcher:speed_above", "to": f"signal:{wired_b}",
              "type": "observes", "source": "CLAUDE.md"})
inj = kg.spine_report(CATALOGS, NAMESPACES, edges)
expect(f"(b) signal を配線すると未配線から外れる ({wired_b})",
       len(inj["obs_unwired"]) == len(base["obs_unwired"]) - 1)

print()
if FAILED:
    print(f"FAILED: {len(FAILED)} — {FAILED}")
    sys.exit(1)
print("すべての検知器が意図した向きで発火した")
